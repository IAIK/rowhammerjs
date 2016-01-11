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
// ./rowhammer [-c number of cores] [-n number of reads] [-d number of dimms] [-t nsecs] [-p percent]
//

#define MIN(X,Y) (((X) < (Y)) ? (X) : (Y))
#define MAX(X,Y) (((X) > (Y)) ? (X) : (Y))

#include <linux/perf_event.h>
#include <linux/hw_breakpoint.h>
#include <sys/ioctl.h>
#include <asm/unistd.h>
//#include <assert.h>
#define assert(X) do { if (!(X)) { fprintf(stderr,"assertion '" #X "' failed\n"); exit(-1); } } while (0)
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
uint64_t number_of_seconds_to_hammer = 720000;

// This vector will be filled with all the pages we can get access to for a
// given row size.
std::vector<std::vector<uint8_t*>> pages_per_row;

// The number of memory reads to try.
#define NUMBER_OF_READS (1*1000*1000)
uint64_t number_of_reads = NUMBER_OF_READS;

size_t CORES = 2;
size_t DIMMS = 2;
// haswell 9889 ivy 144 sandy 48763
ssize_t ROW_INDEX = -1;
// haswell 3 ivy 10 sandy 9
ssize_t OFFSET1 = -1;
// haswell 6 ivy 0 sandy 4
ssize_t OFFSET2 = -1;


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
  //fprintf(stderr,"[!] Initializing large memory mapping ...");
  for (uint64_t index = 0; index < *mapping_size; index += 0x1000) {
    uint64_t* temporary = reinterpret_cast<uint64_t*>(
        static_cast<uint8_t*>(*mapping) + index);
    temporary[0] = index;
  }
  //fprintf(stderr,"done\n");
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
  static const int h0[] = { 6, 10, 12, 14, 16, 17, 18, 20, 22, 24, 25, 26, 27, 28, 30, 32, 33, 35, 36 };
  static const int h1[] = { 7, 11, 13, 15, 17, 19, 20, 21, 22, 23, 24, 26, 28, 29, 31, 33, 34, 35, 37 };

  int count = sizeof(h0) / sizeof(h0[0]);
  int hash = 0;
  for (int i = 0; i < count; i++) {
    hash ^= (phys_addr >> h0[i]) & 1;
  }
  if (CORES == 2)
    return hash;
  count = sizeof(h1) / sizeof(h1[0]);
  int hash1 = 0;
  for (int i = 0; i < count; i++) {
    hash1 ^= (phys_addr >> h1[i]) & 1;
  }
  return hash1 << 1 | hash;
}
size_t get_dram_mapping(void* phys_addr_p) {
  size_t single_dimm_shift = 0;
  if (DIMMS == 1)
    single_dimm_shift = 1;
#if defined(SANDY) || defined(IVY) || defined(HASWELL) || defined(SKYLAKE)
  uint64_t phys_addr = (uint64_t) phys_addr_p;
#if defined(SANDY)
#define ARCH_SHIFT (1)
  static const size_t h0[] = { 14, 18 };
  static const size_t h1[] = { 15, 19 };
  static const size_t h2[] = { 16, 20 };
  static const size_t h3[] = { 17, 21 };
  static const size_t h4[] = { 17, 21 };
  static const size_t h5[] = { 6 };
#elif defined(IVY) || defined(HASWELL)
#define ARCH_SHIFT (1)
  static const size_t h0[] = { 14, 18 };
  static const size_t h1[] = { 15, 19 };
  static const size_t h2[] = { 16, 20 };
  static const size_t h3[] = { 17, 21 };
  static const size_t h4[] = { 17, 21 };
  static const size_t h5[] = { 7, 8, 9, 12, 13, 18, 19 };
#elif defined(SKYLAKE)
#define ARCH_SHIFT (2)
  static const size_t h0[] = { 7, 14 };
  static const size_t h1[] = { 15, 19 };
  static const size_t h2[] = { 16, 20 };
  static const size_t h3[] = { 17, 21 };
  static const size_t h4[] = { 18, 22 };
  static const size_t h5[] = { 8, 9, 12, 13, 18, 19 };
#endif

  size_t hash = 0;
  size_t count = sizeof(h0) / sizeof(h0[0]);
  for (size_t i = 0; i < count; i++) {
    hash ^= (phys_addr >> (h0[i] - single_dimm_shift)) & 1;
  }
  size_t hash1 = 0;
  count = sizeof(h1) / sizeof(h1[0]);
  for (size_t i = 0; i < count; i++) {
    hash1 ^= (phys_addr >> (h1[i] - single_dimm_shift)) & 1;
  }
  size_t hash2 = 0;
  count = sizeof(h2) / sizeof(h2[0]);
  for (size_t i = 0; i < count; i++) {
    hash2 ^= (phys_addr >> (h2[i] - single_dimm_shift)) & 1;
  }
  size_t hash3 = 0;
  count = sizeof(h3) / sizeof(h3[0]);
  for (size_t i = 0; i < count; i++) {
    hash3 ^= (phys_addr >> (h3[i] - single_dimm_shift)) & 1;
  }
  size_t hash4 = 0;
  count = sizeof(h4) / sizeof(h4[0]);
  for (size_t i = 0; i < count; i++) {
    hash4 ^= (phys_addr >> (h4[i] - single_dimm_shift)) & 1;
  }
  size_t hash5 = 0;
  if (DIMMS == 2)
  {
    count = sizeof(h5) / sizeof(h5[0]);
    for (size_t i = 0; i < count; i++) {
      hash5 ^= (phys_addr >> h5[i]) & 1;
    }
  }
  return (hash5 << 5) | (hash4 << 4) | (hash3 << 3) | (hash2 << 2) | (hash1 << 1) | hash;
#else
#define ARCH_SHIFT (1)
  return 0;
#endif
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

// Extract the physical page number from a Linux /proc/PID/pagemap entry.
uint64_t frame_number_from_pagemap(uint64_t value) {
  return value & ((1ULL << 54) - 1);
}

uint64_t get_physical_addr(uint64_t virtual_addr) {
  uint64_t value;
  off_t offset = (virtual_addr / 4096) * sizeof(value);
  int got = pread(pagemap, &value, sizeof(value), offset);
  assert(got == 8);

  // Check the "page present" flag.
  assert(value & (1ULL << 63));

  uint64_t frame_num = frame_number_from_pagemap(value);
  return (frame_num * 4096) | (virtual_addr & (4095));
}

#define ROW_SIZE (128*1024*DIMMS*ARCH_SHIFT)
#define ADDR_COUNT (32)

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
      if ((phys2 / ROW_SIZE) != ((phys1 / ROW_SIZE)+1) && in_same_cache_set(phys1, phys2, -1)) {
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

size_t histf[1500];
size_t hists[1500];

void HammerThread() {
return;
}

void dummy(volatile uint64_t* a,volatile uint64_t* b, volatile uint64_t* c, volatile uint64_t* d)
{
  if (a == b && c == d)
    exit(-1);
}

//#define PERF_COUNTERS
#ifdef PERF_COUNTERS
class Perf {
  static long
  perf_event_open(struct perf_event_attr *hw_event, pid_t pid,
                 int cpu, int group_fd, unsigned long flags)
  {
     int ret;

     ret = syscall(__NR_perf_event_open, hw_event, pid, cpu,
                    group_fd, flags);
     return ret;
  }

  int fd_;

 public:
  Perf(size_t pid, size_t config) {
    struct perf_event_attr pe = {};
    pe.type = PERF_TYPE_HARDWARE;
    pe.size = sizeof(pe);
    pe.config = config;
    pe.disabled = 0;
    pe.exclude_kernel = 0;
    pe.exclude_hv = 0;
    pe.pinned = 1;
    pe.inherit = 1;

    fd_ = perf_event_open(&pe, pid, -1, -1, 0);
    assert(fd_ >= 0);
  }

  void start() {
    int rc = ioctl(fd_, PERF_EVENT_IOC_RESET, 0);
    assert(rc == 0);
    rc = ioctl(fd_, PERF_EVENT_IOC_ENABLE, 0);
    assert(rc == 0);
  }

  size_t stop() {
    int rc = ioctl(fd_, PERF_EVENT_IOC_DISABLE, 0);
    assert(rc == 0);
    size_t count;
    int got = read(fd_, &count, sizeof(count));
    assert(got == sizeof(count));
    return count;
  }
};
#endif

//#define MEASURE_EVICTION
uint64_t HammerAddressesStandard(
    const std::pair<uint64_t, uint64_t>& first_range,
    const std::pair<uint64_t, uint64_t>& second_range,
    uint64_t number_of_reads) {

#ifdef PERF_COUNTERS
  Perf perfh(0,PERF_COUNT_HW_CACHE_REFERENCES);
  Perf perfm(0,PERF_COUNT_HW_CACHE_MISSES);
  perfh.start();
  perfm.start();
#endif
  faddrs[0] = (uint64_t*) first_range.first;
  saddrs[0] = (uint64_t*) second_range.first;

  pick(faddrs,+1);
  pick(saddrs,+1);

  volatile uint64_t* f = faddrs[0];
  volatile uint64_t* s = saddrs[0];

  uint64_t sum = 0;
  size_t t0 = rdtsc();
  size_t t = 0,t2 = 0,delta = 0,delta2 = 0;
  while (number_of_reads-- > 0) {
#ifdef MEASURE_EVICTION
    rdtsc();
    t = rdtsc();
#endif
    *f;
#ifdef MEASURE_EVICTION
    t2 = rdtsc2();
    histf[MAX(0,MIN(99,(t2 - t) / 5))]++;
    rdtsc2();
    rdtsc();
    t = rdtsc();
#endif
    *s;
#ifdef MEASURE_EVICTION
    t2 = rdtsc2();
    hists[MAX(MIN((t2 - t) / 5,99),0)]++;
#endif

#ifdef EVICTION_BASED
   for (size_t i = 1; i < 18; i += 1)
   {
     *faddrs[i];
     *saddrs[i];
     *faddrs[i+1];
     *saddrs[i+1];
     *faddrs[i];
     *saddrs[i];
     *faddrs[i+1];
     *saddrs[i+1];
     *faddrs[i];
     *saddrs[i];
     *faddrs[i+1];
     *saddrs[i+1];
     *faddrs[i];
     *saddrs[i];
     *faddrs[i+1];
     *saddrs[i+1];
     *faddrs[i];
     *saddrs[i];
     *faddrs[i+1];
     *saddrs[i+1];
   }
#else
#if defined(SKYLAKE) && !defined(NO_CLFLUSHOPT)
   asm volatile("clflushopt (%0)" : : "r" (f) : "memory");
   asm volatile("clflushopt (%0)" : : "r" (s) : "memory");
#else
   asm volatile("clflush (%0)" : : "r" (f) : "memory");
   asm volatile("clflush (%0)" : : "r" (s) : "memory");
#endif
#endif
  }
  printf("%zu ",(rdtsc2() - t0) / (NUMBER_OF_READS));
#ifdef MEASURE_EVICTION
for (size_t i = 0; i < 100; ++i)
{
  printf("%zu,%zu\n",i * 5, histf[i] + hists[i]);
  histf[i] = 0;
  hists[i] = 0;
}
#endif
#ifdef PERF_COUNTERS
  size_t th = perfh.stop();
  size_t tm = perfm.stop();
  printf("  Hits: %zu\n",th);
  printf("Misses: %zu\n",tm);
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
uint64_t HammerAllReachablePages(void* memory_mapping, uint64_t memory_mapping_size, HammerFunction* hammer,
    uint64_t number_of_reads) {
  uint64_t total_bitflips = 0;

  pages_per_row.resize(memory_mapping_size / ROW_SIZE);
  pagemap = open("/proc/self/pagemap", O_RDONLY);
  assert(pagemap >= 0);

  //fprintf(stderr,"[!] Identifying rows for accessible pages ... ");
  for (uint64_t offset = 0; offset < memory_mapping_size; offset += 0x1000) { // maybe * DIMMS
    uint8_t* virtual_address = static_cast<uint8_t*>(memory_mapping) + offset;
    uint64_t page_frame_number = GetPageFrameNumber(pagemap, virtual_address);
    uint64_t physical_address = page_frame_number * 0x1000;
    uint64_t presumed_row_index = physical_address / ROW_SIZE;
    //printf("[!] put va %lx pa %lx into row %ld\n", (uint64_t)virtual_address,
    //    physical_address, presumed_row_index);
    if (presumed_row_index > pages_per_row.size()) {
      pages_per_row.resize(presumed_row_index);
    }
    pages_per_row[presumed_row_index].push_back(virtual_address);
    //printf("[!] done\n");
  }
  //fprintf(stderr,"Done\n");
  srand(rdtsc());
  // We should have some pages for most rows now.
  //for (uint64_t row_index = 0; row_index < pages_per_row.size(); ++row_index) { // scan all rows
  while (1) {
    uint64_t row_index = ROW_INDEX < 0? rand()%pages_per_row.size():ROW_INDEX; // fix to specific row
    bool cont = false;
    for (int64_t offset = 0; offset < 3; ++offset)
    {
      if (pages_per_row[row_index + offset].size() != ROW_SIZE/4096)
      {
        cont = true;
        fprintf(stderr,"[!] Can't hammer row %ld - only got %ld (of %ld) pages\n",
            row_index+offset, pages_per_row[row_index+offset].size(),ROW_SIZE/4096);
        break;
      }
    }
    if (cont)
      continue;
    printf("[!] Hammering rows %ld/%ld/%ld of %ld (got %ld/%ld/%ld pages)\n",
        row_index, row_index+1, row_index+2, pages_per_row.size(),
        pages_per_row[row_index].size(), pages_per_row[row_index+1].size(),
        pages_per_row[row_index+2].size());
    if (OFFSET1 < 0)
      OFFSET1 = -1;
    // Iterate over all pages we have for the first row.
    for (uint8_t* first_row_page : pages_per_row[row_index])
    {
      if (OFFSET1 >= 0)
        first_row_page = pages_per_row[row_index].at(OFFSET1);
      if (OFFSET2 < 0)
        OFFSET2 = -1;
      for (uint8_t* second_row_page : pages_per_row[row_index+2])
      {
        if (OFFSET2 >= 0)
          second_row_page = pages_per_row[row_index+2].at(OFFSET2);
        if (get_dram_mapping(first_row_page) != get_dram_mapping(second_row_page))
        {
          if (OFFSET1 >= 0 && OFFSET2 >= 0 && ROW_INDEX >= 0)
          {
            printf("[!] Combination not valid for your architecture, won't be in the same bank.\n");
            exit(-1);
          }
          continue;
        }
        uint32_t offset_line = 0;
        uint8_t cnt = 0;
        // Set all the target pages to 0xFF.
#define VAL ((uint64_t)((offset % 2) == 0 ? 0 : -1ULL))
        for (int32_t offset = 0; offset < 3; offset += 1)
        for (uint8_t* target_page8 : pages_per_row[row_index+offset]) {
          uint64_t* target_page = (uint64_t*)target_page8;
          for (uint32_t index = 0; index < 512; ++index)
            target_page[index] = VAL;
        }
        // Now hammer the two pages we care about.
        std::pair<uint64_t, uint64_t> first_page_range(
            reinterpret_cast<uint64_t>(first_row_page+offset_line),
            reinterpret_cast<uint64_t>(first_row_page+offset_line+0x1000));
        std::pair<uint64_t, uint64_t> second_page_range(
            reinterpret_cast<uint64_t>(second_row_page+offset_line),
            reinterpret_cast<uint64_t>(second_row_page+offset_line+0x1000));
        
        size_t number_of_bitflips_in_target = 0;
#ifdef FIND_EXPLOITABLE_BITFLIPS
        for (size_t tries = 0; tries < 2; ++tries)
#endif
        {
        hammer(first_page_range, second_page_range, number_of_reads);
        // Now check the target pages.
        int32_t offset = 1;
        for (; offset < 2; offset += 1)
        for (const uint8_t* target_page8 : pages_per_row[row_index+offset]) {
          const uint64_t* target_page = (const uint64_t*) target_page8;
          for (uint32_t index = 0; index < 512; ++index) {
            if (target_page[index] != VAL) {
              ++number_of_bitflips_in_target;
              fprintf(stderr,"[!] Found %zu. flip (0x%016lx != 0x%016lx) in row %ld (%lx) (when hammering "
                  "rows %zu and %zu at first offset %zu and second offset %zu\n", number_of_bitflips_in_target, target_page[index], VAL, row_index+offset,
                  GetPageFrameNumber(pagemap, (uint8_t*)target_page + index)*0x1000+(((size_t)target_page + index)%0x1000),
                  row_index,row_index +2,OFFSET1 < 0?-(OFFSET1+1):OFFSET1,OFFSET2 < 0?-(OFFSET2+1):OFFSET2);
            }
          }
#ifdef FIND_EXPLOITABLE_BITFLIPS
          if (number_of_bitflips_in_target > 0)
          {
            for (uint32_t index = 0; index < 512; ++index) {
              if ((((uint64_t*)target_page)[index] & 0x3) == 0x3 && (((uint64_t*)target_page)[index] & 0x1FFFFF000ULL) != 0)
              {
                printf("exploitable bitflip: %lx\n",((uint64_t*)target_page)[index]);
                exit(0);
              }
            }
          }
#endif
        }
#ifdef FIND_EXPLOITABLE_BITFLIPS
        if (number_of_bitflips_in_target == 0)
          break;
        else
          number_of_reads = 16*NUMBER_OF_READS;
#endif
        }
        number_of_reads = NUMBER_OF_READS;
        if (OFFSET2 < 0)
          OFFSET2--;
        else
          break;
      }
      if (OFFSET1 < 0)
        OFFSET1--;
      else
        break;
    }
  }
  return total_bitflips;
}

void HammerAllReachableRows(HammerFunction* hammer, uint64_t number_of_reads) {
  uint64_t mapping_size;
  void* mapping;
  SetupMapping(&mapping_size, &mapping);

  HammerAllReachablePages(mapping, mapping_size,
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
  while ((opt = getopt(argc, argv, "t:p:c:d:r:f:s:")) != -1) {
    switch (opt) {
      case 't':
        number_of_seconds_to_hammer = atoi(optarg);
        break;
      case 'p':
        fraction_of_physical_memory = atof(optarg);
        break;
      case 'c':
        CORES = atof(optarg) * ARCH_SHIFT;
        break;
      case 'd':
        DIMMS = atoi(optarg);
        break;
      case 'r':
        ROW_INDEX = atof(optarg);
        break;
      case 'f':
        OFFSET1 = atof(optarg);
        break;
      case 's':
        OFFSET2 = atoi(optarg);
        break;
      default:
        fprintf(stderr, "Usage: %s [-t nsecs] [-p percent] [-c cores] [-d dimms] [-r row] [-f first_offset] [-s second_offset]\n",
            argv[0]);
        exit(EXIT_FAILURE);
    }
  }
  assert(DIMMS == 1 || DIMMS == 2);

  signal(SIGALRM, HammeredEnough);

  //fprintf(stderr,"[!] Starting the testing process...\n");
  alarm(number_of_seconds_to_hammer);
  HammerAllReachableRows(&HammerAddressesStandard, number_of_reads);
}
