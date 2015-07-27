// Copyright 2015, Daniel Gruss, Clémentine Maurice
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// This code is a modification of the double_sided_rowhammer program:
// https://github.com/google/rowhammer-test
// and is copyright by Google
//
// ./double_sided_rowhammer_ivy [-n number of reads] [-d dimms] [-t nsecs] [-p percent]
//

#define MIN(X,Y) (((X) < (Y)) ? (X) : (Y))
#define MAX(X,Y) (((X) > (Y)) ? (X) : (Y))

#include <asm/unistd.h>
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <linux/kernel-page-flags.h>
#include <map>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mount.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/sysinfo.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <vector>

namespace {

// for debugging
//#define MEASURE_EVICTION

// The fraction of physical memory that should be mapped for testing.
double fraction_of_physical_memory = 0.4;

// The time to hammer before aborting. Defaults to one hour.
uint64_t number_of_seconds_to_hammer = 7200;

// Number of DIMMs, Default: 2
uint64_t number_of_dimms = 2;
#define ROW_SIZE (128*1024*number_of_dimms)

// Number of congruent addresses to find for possible use during eviction
#define ADDR_COUNT (25)

// This vector will be filled with all the pages we can get access to for a
// given row size.
std::vector<std::vector<uint8_t*>> pages_per_row;

// The number of memory reads to try.
uint64_t number_of_reads = (2*1024*1024);

// Obtain the size of the physical memory of the system.
uint64_t GetPhysicalMemorySize() {
  struct sysinfo info;
  sysinfo( &info );
  return (size_t)info.totalram * (size_t)info.mem_unit;
}

int pagemap = -1;

uint64_t GetPageFrameNumber(int pagemap, uint8_t* virtual_address) {
  // Read the entry in the pagemap.
  uint64_t value;
  int got = pread(pagemap, &value, 8,
                  (reinterpret_cast<uintptr_t>(virtual_address) / 0x1000) * 8);
  assert(got == 8);
  uint64_t page_frame_number = value & ((1ULL << 54)-1);
  return page_frame_number;
}

void SetupMapping(uint64_t* mapping_size, void** mapping) {
  *mapping_size =
    static_cast<uint64_t>((static_cast<double>(GetPhysicalMemorySize()) *
          fraction_of_physical_memory));

  *mapping = mmap(NULL, *mapping_size, PROT_READ | PROT_WRITE,
      MAP_POPULATE | MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
  assert(*mapping != (void*)-1);

  // Initialize the mapping so that the pages are non-empty.
  printf("[!] Initializing large memory mapping ...");
  for (uint64_t index = 0; index < *mapping_size; index += 0x1000) {
    uint64_t* temporary = reinterpret_cast<uint64_t*>(
        static_cast<uint8_t*>(*mapping) + index);
    temporary[0] = index;
  }
  printf("done\n");
}

// Given a physical memory address, this hashes the address and
// returns the number of the cache slice that the address maps to.
//
// This assumes a 2-core Sandy Bridge CPU.
//
// "bad_bit" lets us test whether this hash function is correct.  It
// inverts whether the given bit number is included in the set of
// address bits to hash.
int get_cache_slice(uint64_t phys_addr, int bad_bit) {
  // On a 4-core machine, the CPU's hash function produces a 2-bit
  // cache slice number, where the two bits are defined by "h1" and
  // "h2":
  //
  // h1 function:
  //   static const int bits[] = { 18, 19, 21, 23, 25, 27, 29, 30, 31 };
  // h2 function:
  //   static const int bits[] = { 17, 19, 20, 21, 22, 23, 24, 26, 28, 29, 31 };
  //
  // This hash function is described in the paper "Practical Timing
  // Side Channel Attacks Against Kernel Space ASLR".
  //
  // On a 2-core machine, the CPU's hash function produces a 1-bit
  // cache slice number which appears to be the XOR of h1 and h2.

  // XOR of h1 and h2:
  static const int bits[] = { 17, 18, 20, 22, 24, 25, 26, 27, 28, 30 };

  int count = sizeof(bits) / sizeof(bits[0]);
  int hash = 0;
  for (int i = 0; i < count; i++) {
    hash ^= (phys_addr >> bits[i]) & 1;
  }
  if (bad_bit != -1) {
    hash ^= (phys_addr >> bad_bit) & 1;
  }
  return hash;
}

bool in_same_cache_set(uint64_t phys1, uint64_t phys2, int bad_bit) {
  // For Sandy Bridge, the bottom 17 bits determine the cache set
  // within the cache slice (or the location within a cache line).
  uint64_t mask = ((uint64_t) 1 << 17) - 1;
  return ((phys1 & mask) == (phys2 & mask) &&
          get_cache_slice(phys1, bad_bit) == get_cache_slice(phys2, bad_bit));
}

uint64_t rdtsc() {
  uint64_t a, d;
  asm volatile ("cpuid" ::: "rax","rbx","rcx","rdx");
  asm volatile ("rdtscp" : "=a" (a), "=d" (d) : : "rcx");
  a = (d<<32) | a;
  return a;
}
uint64_t rdtsc2() {
  uint64_t a, d;
  asm volatile ("rdtscp" : "=a" (a), "=d" (d) : : "rcx");
  asm volatile ("cpuid" ::: "rax","rbx","rcx","rdx");
  a = (d<<32) | a;
  return a;
}

int g_pagemap_fd = -1;

// Extract the physical page number from a Linux /proc/PID/pagemap entry.
uint64_t frame_number_from_pagemap(uint64_t value) {
  return value & ((1ULL << 54) - 1);
}

uint64_t get_physical_addr(uint64_t virtual_addr) {
  uint64_t value;
  off_t offset = (virtual_addr / 4096) * sizeof(value);
  int got = pread(g_pagemap_fd, &value, sizeof(value), offset);
  assert(got == 8);

  // Check the "page present" flag.
  assert(value & (1ULL << 63));

  uint64_t frame_num = frame_number_from_pagemap(value);
  return (frame_num * 4096) | (virtual_addr & (4095));
}

bool in_same_bank(uint64_t phys1, uint64_t phys2) {
  return ((((phys1 >> 14) & 0x7) ^ ((phys1 >> 18) & 0x7)) == (((phys2 >> 14) & 0x7) ^ ((phys2 >> 18) & 0x7))) && (((phys1 >> 17) & 0x1) == ((phys2 >> 17) & 0x1));
}

void pick(volatile uint64_t** addrs, int step)
{
  uint8_t* buf = (uint8_t*) addrs[0];
  uint64_t phys1 = get_physical_addr((uint64_t)buf);
  uint64_t presumed_row_index = phys1 / ROW_SIZE;
  int found = 1;
  //printf("%zx\n",phys1 / ROW_SIZE); // Print this for the watch_firefox tool
  presumed_row_index += step;
  while (found < ADDR_COUNT)
  {
    for (uint8_t* second_row_page : pages_per_row[presumed_row_index]) {
      uint64_t phys2 = get_physical_addr((uint64_t)second_row_page);
      if ((phys2 / ROW_SIZE) != ((phys1 / ROW_SIZE)+1) && /*in_same_bank(phys1,phys2) &&*/ in_same_cache_set(phys1, phys2, -1)) {
        addrs[found] = (uint64_t*)second_row_page;
        //printf("%zx\n",phys2 / ROW_SIZE); // Print this for the watch_firefox tool
        found++;
      }
    }
    presumed_row_index += step;
  }
}

volatile uint64_t* faddrs[ADDR_COUNT];
volatile uint64_t* saddrs[ADDR_COUNT];

size_t histf[15];
size_t hists[15];

void HammerThread() {
return;
}

void dummy(volatile uint64_t* a,volatile uint64_t* b, volatile uint64_t* c, volatile uint64_t* d)
{
  if (a == b && c == d)
    exit(-1);
}

uint64_t HammerAddressesStandard(
    const std::pair<uint64_t, uint64_t>& first_range,
    const std::pair<uint64_t, uint64_t>& second_range,
    uint64_t number_of_reads_l) {

  faddrs[0] = (uint64_t*) first_range.first;
  saddrs[0] = (uint64_t*) second_range.first;

  pick(faddrs,+1);
  pick(saddrs,+1);

  volatile uint64_t* f = faddrs[0];
  volatile uint64_t* s = saddrs[0];
  volatile uint64_t* f1 = faddrs[1];
  volatile uint64_t* f2 = faddrs[2];
  volatile uint64_t* f3 = faddrs[3];
  volatile uint64_t* f4 = faddrs[4];
  volatile uint64_t* f5 = faddrs[5];
  volatile uint64_t* f6 = faddrs[6];
  volatile uint64_t* f7 = faddrs[7];
  volatile uint64_t* f8 = faddrs[8];
  volatile uint64_t* f9 = faddrs[9];
  volatile uint64_t* f10 = faddrs[10];
  volatile uint64_t* f11 = faddrs[11];
  volatile uint64_t* f12 = faddrs[12];
  volatile uint64_t* f13 = faddrs[13];
  volatile uint64_t* f14 = faddrs[14];
  volatile uint64_t* f15 = faddrs[15];

  volatile uint64_t* s1 = saddrs[1];
  volatile uint64_t* s2 = saddrs[2];
  volatile uint64_t* s3 = saddrs[3];
  volatile uint64_t* s4 = saddrs[4];
  volatile uint64_t* s5 = saddrs[5];
  volatile uint64_t* s6 = saddrs[6];
  volatile uint64_t* s7 = saddrs[7];
  volatile uint64_t* s8 = saddrs[8];
  volatile uint64_t* s9 = saddrs[9];
  volatile uint64_t* s10 = saddrs[10];
  volatile uint64_t* s11 = saddrs[11];
  volatile uint64_t* s12 = saddrs[12];
  volatile uint64_t* s13 = saddrs[13];
  volatile uint64_t* s14 = saddrs[14];
  volatile uint64_t* s15 = saddrs[15];

  uint64_t sum = 0;
  size_t t0 = rdtsc();
  size_t t = 0,t2 = 0,delta = 0,delta2 = 0;
#define F(X,Y,Z) *f##X; *s##X; *f##Y; *s##Y; *f##Z; *s##Z
#define F2(X,Y,Z) F(X,Y,Z); F(X,Y,Z)
#define F3(X,Y,Z) F(X,Y,Z); F(X,Y,Z); F(X,Y,Z)
  while (number_of_reads_l-- > 0) {
    F3(1,2,3);
    F3(4,5,6);
    F3(7,8,9);
    F3(10,11,12);
    F2(13,14,15);

#ifdef MEASURE_EVICTION
    rdtsc();
    t = rdtsc();
#endif
    *f;
#ifdef MEASURE_EVICTION
    t2 = rdtsc2();
    histf[MAX(0,MIN(14,(t2 - t) / 20))]++;
    rdtsc2();
    rdtsc();
    t = rdtsc();
#endif
    *s;
#ifdef MEASURE_EVICTION
    t2 = rdtsc2();
    hists[MAX(MIN((t2 - t) / 20,14),0)]++;
#endif

// this is replaced by the eviction accesses above
//    asm volatile("clflush (%0)" : : "r" (f) : "memory");
//    asm volatile("clflush (%0)" : : "r" (s) : "memory");
  }
  printf("%zu ",(rdtsc() - t0) / (number_of_reads));
#ifdef MEASURE_EVICTION
for (size_t i = 0; i < 15; ++i)
{
  printf("%3zu: %15zu %15zu\n",i * 20, histf[i], hists[i]);
  histf[i] = 0;
  hists[i] = 0;
}
#endif

  //dummy(f0,f1,s0,s1);
  return sum;
}

typedef uint64_t(HammerFunction)(
    const std::pair<uint64_t, uint64_t>& first_range,
    const std::pair<uint64_t, uint64_t>& second_range,
    uint64_t number_of_reads);

// A comprehensive test that attempts to hammer adjacent rows for a given
// assumed row size (and assumptions of sequential physical addresses for
// various rows.
uint64_t HammerAllReachablePages(uint64_t presumed_row_size,
    void* memory_mapping, uint64_t memory_mapping_size, HammerFunction* hammer,
    uint64_t number_of_reads) {
  uint64_t total_bitflips = 0;

  pages_per_row.resize(memory_mapping_size / presumed_row_size);
  pagemap = open("/proc/self/pagemap", O_RDONLY);
  assert(pagemap >= 0);
  g_pagemap_fd = pagemap;

  printf("[!] Identifying rows for accessible pages ... ");
  for (uint64_t offset = 0; offset < memory_mapping_size; offset += (0x1000*number_of_dimms)) {
    uint8_t* virtual_address = static_cast<uint8_t*>(memory_mapping) + offset;
    uint64_t page_frame_number = GetPageFrameNumber(pagemap, virtual_address);
    uint64_t physical_address = page_frame_number * 0x1000;
    uint64_t presumed_row_index = physical_address / presumed_row_size;
    //printf("[!] put va %lx pa %lx into row %ld\n", (uint64_t)virtual_address,
    //    physical_address, presumed_row_index);
    if (presumed_row_index > pages_per_row.size()) {
      pages_per_row.resize(presumed_row_index);
    }
    pages_per_row[presumed_row_index].push_back(virtual_address);
    //printf("[!] done\n");
  }
  printf("Done\n");
  srand(rdtsc());
  // We should have some pages for most rows now.
  
  // either scan systematically:
  for (uint64_t row_index = 0; row_index < pages_per_row.size(); ++row_index) { // scan all rows
  // or scan random:
  //while (1) {    uint64_t row_index = rand()%pages_per_row.size();
  // or fix to specific row:
  //while (1) {    uint64_t row_index = 123; // fix to specific row
    bool cont = false;
    for (int64_t offset = 0; offset < 3; ++offset)
    {
      if (pages_per_row[row_index + offset].size() != 32)
      {
        cont = true;
        printf("[!] Can't hammer row %ld - only got %ld pages\n",
            row_index+offset, pages_per_row[row_index+offset].size());
        break;
      }
    }
    if (cont)
      continue;
    printf("[!] Hammering rows %ld/%ld/%ld of %ld (got %ld/%ld/%ld pages)\n",
        row_index, row_index+1, row_index+2, pages_per_row.size(),
        pages_per_row[row_index].size(), pages_per_row[row_index+1].size(),
        pages_per_row[row_index+2].size());
    // Iterate over all pages we have for the first row.
    for (uint8_t* first_row_page : pages_per_row[row_index])
    //uint8_t* first_row_page = pages_per_row[row_index].at(10); // use this to fix hammering to a specific page
    {
      for (uint8_t* second_row_page : pages_per_row[row_index+2])
      //uint8_t* second_row_page = pages_per_row[row_index+2].at(0); // use this to fix hammering to a specific page
      {
        // you should not find more bitflips if you disable this check but you can try
        if (!in_same_bank(GetPageFrameNumber(pagemap, second_row_page)*0x1000,GetPageFrameNumber(pagemap, first_row_page)*0x1000))
        {
          continue;
        }
        // Iterate over all pages we have for the second row.
        uint32_t offset_line = 0;
        // Set all the target pages to 0xFF.
        for (int32_t offset = 1; offset < 2; offset += 1)        
        for (uint8_t* target_page : pages_per_row[row_index+offset]) {
          memset(target_page, 0xFF, 0x1000);
        }

        // Now hammer the two pages we care about.
        std::pair<uint64_t, uint64_t> first_page_range(
            reinterpret_cast<uint64_t>(first_row_page+offset_line),
            reinterpret_cast<uint64_t>(first_row_page+offset_line+0x1000));
        std::pair<uint64_t, uint64_t> second_page_range(
            reinterpret_cast<uint64_t>(second_row_page+offset_line),
            reinterpret_cast<uint64_t>(second_row_page+offset_line+0x1000));
        
        hammer(first_page_range, second_page_range, number_of_reads);
        // Now check the target pages.
        uint64_t number_of_bitflips_in_target = 0;
        int32_t offset = 1;
        for (; offset < 2; offset += 1)
        for (const uint8_t* target_page : pages_per_row[row_index+offset]) {
          for (uint32_t index = 0; index < 0x1000; ++index) {
            if (target_page[index] != 0xFF) {
              ++number_of_bitflips_in_target;
              printf("[!] Found %zu. flip (0x%X != 0xFF) in row %ld (%lx) when hammering "
                  "%lx (%p)...\n", number_of_bitflips_in_target, target_page[index],  row_index+offset,
                  GetPageFrameNumber(pagemap, (uint8_t*)target_page + index)*0x1000+(((size_t)target_page + index)%0x1000),
                  GetPageFrameNumber(pagemap, first_row_page)*0x1000+offset_line, first_row_page);
            }
          }
        }
      }
    }
  }
  return total_bitflips;
}

void HammerAllReachableRows(HammerFunction* hammer, uint64_t number_of_reads) {
  uint64_t mapping_size;
  void* mapping;
  SetupMapping(&mapping_size, &mapping);

  HammerAllReachablePages(ROW_SIZE, mapping, mapping_size,
                          hammer, number_of_reads);
}

void HammeredEnough(int sig) {
  printf("[!] Spent %ld seconds hammering, exiting now.\n",
      number_of_seconds_to_hammer);
  fflush(stdout);
  fflush(stderr);
  exit(0);
}

}  // namespace

int main(int argc, char** argv) {
  // Turn off stdout buffering when it is a pipe.
  setvbuf(stdout, NULL, _IONBF, 0);

  int opt;
  while ((opt = getopt(argc, argv, "n:d:t:p:")) != -1) {
    switch (opt) {
      case 't':
        number_of_seconds_to_hammer = atoi(optarg);
        break;
      case 'd':
        number_of_dimms = atoi(optarg);
        break;
      case 'n':
        number_of_reads = atoi(optarg);
        break;
      case 'p':
        fraction_of_physical_memory = atof(optarg);
        break;
      default:
        fprintf(stderr, "Usage: %s [-n number of reads] [-d dimms] [-t nsecs] [-p percent]\n",
            argv[0]);
        exit(EXIT_FAILURE);
    }
  }

  signal(SIGALRM, HammeredEnough);

  printf("[!] Starting the testing process...\n");
  alarm(number_of_seconds_to_hammer);
  HammerAllReachableRows(&HammerAddressesStandard, number_of_reads);
}
