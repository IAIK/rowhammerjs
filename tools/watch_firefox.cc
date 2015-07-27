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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <map>
#include <set>
#include <algorithm>


// fill addresses in here in the following scheme:
// 1x check for bitflip area start address
// 1x check for bitflip area end address
// 64x faddrs array from double_sided_rowhammer_* program
// 64x saddrs array from double_sided_rowhammer_* program
static const size_t target_addrs[] = {
0x4d444000,
0x4d446000,

0x4d423000,
0x4d503000,
0x4d563000,
0x4d6a3000,
0x4d6c3000,
0x4d783000,
0x4d7e3000,
0x4d883000,
0x4d8e3000,
0x4d9a3000,
0x4d9c3000,
0x4da03000,
0x4da63000,
0x4db23000,
0x4db43000,
0x4dca3000,
0x4dcc3000,
0x4dd83000,
0x4dde3000,
0x4de23000,
0x4de43000,
0x4df03000,
0x4df63000,
0x4e083000,
0x4e0e3000,
0x4e1a3000,
0x4e1c3000,
0x4e203000,
0x4e263000,
0x4e323000,
0x4e343000,
0x4e4a3000,
0x4e4c3000,
0x4e583000,
0x4e5e3000,
0x4e623000,
0x4e643000,
0x4e703000,
0x4e763000,
0x4e803000,
0x4e863000,
0x4e923000,
0x4e943000,
0x4ea83000,
0x4eae3000,
0x4eba3000,
0x4ebc3000,
0x4ec23000,
0x4ec43000,
0x4ed03000,
0x4ed63000,
0x4eea3000,
0x4eec3000,
0x4ef83000,
0x4efe3000,
0x4f0a3000,
0x4f0c3000,
0x4f183000,
0x4f1e3000,
0x4f223000,
0x4f243000,
0x4f303000,
0x4f363000,
0x4f483000,
0x4d466000,
0x4d526000,
0x4d546000,
0x4d686000,
0x4d6e6000,
0x4d7a6000,
0x4d7c6000,
0x4d8a6000,
0x4d8c6000,
0x4d986000,
0x4d9e6000,
0x4da26000,
0x4da46000,
0x4db06000,
0x4db66000,
0x4dc86000,
0x4dce6000,
0x4dda6000,
0x4ddc6000,
0x4de06000,
0x4de66000,
0x4df26000,
0x4df46000,
0x4e0a6000,
0x4e0c6000,
0x4e186000,
0x4e1e6000,
0x4e226000,
0x4e246000,
0x4e306000,
0x4e366000,
0x4e486000,
0x4e4e6000,
0x4e5a6000,
0x4e5c6000,
0x4e606000,
0x4e666000,
0x4e726000,
0x4e746000,
0x4e826000,
0x4e846000,
0x4e906000,
0x4e966000,
0x4eaa6000,
0x4eac6000,
0x4eb86000,
0x4ebe6000,
0x4ec06000,
0x4ec66000,
0x4ed26000,
0x4ed46000,
0x4ee86000,
0x4eee6000,
0x4efa6000,
0x4efc6000,
0x4f086000,
0x4f0e6000,
0x4f1a6000,
0x4f1c6000,
0x4f206000,
0x4f266000,
0x4f326000,
0x4f346000,
0x4f4a6000,

};

#define N_TARGET_ADDRS (128+2)

size_t found_target_addrs = 0;

std::map<size_t,size_t> virt2phys;
std::map<size_t,size_t> phys2virtsel;
std::set<size_t> oldvirt;
std::set<size_t> virt;
std::map<size_t,size_t> virt2size;
std::map<size_t,std::string> virt2string;

size_t startvaddr = 0;

uint64_t rdtsc() {
  uint64_t a, d;
  asm volatile ("rdtsc" : "=a" (a), "=d" (d));
  a = (d<<32) | a;
  return a;
}

size_t vaddr2paddr(FILE* pagemap, size_t virtual_address) {
  uint64_t value;
  fseek(pagemap, (virtual_address / 0x1000) * 8, SEEK_SET);
  int got = fread(&value, 1, 8, pagemap);
  return ((value & ((1ULL << 54)-1)) << 12) | (virtual_address & 0xFFF);
}

int main()
{
  size_t alloccounter = 0;
  FILE* f = popen("/bin/ps -FA | /bin/grep '/firefox' | /bin/grep -oE '[0-9]+'","r");
  char* line = 0;
  size_t n = 0;
  if (getline(&line,&n,f) == -1)
  {
    exit(printf("firefox not running\n"));
  }
  pclose(f);
  char* end = 0;
  size_t pid = strtoull(line,&end,10);
  if (end == line)
  {
    exit(printf("pid parsing failed\n"));
  }
  printf("%zu\n",pid);
  char procfsname[128];
  snprintf(procfsname,128,"/proc/%zu/maps",pid);
  char procfsphys[128];
  snprintf(procfsphys,128,"/proc/%zu/pagemap",pid);
  f = fopen(procfsname,"r");
  FILE* fp = fopen(procfsphys,"r");
  size_t t0 = rdtsc();
  size_t round = 0;
  while (1)
  {
    round++;
    while (getline(&line,&n,f) != -1)
    {
      line[strlen(line)-1] = 0;
      size_t vstart = 0;
      size_t vend = 0;
      sscanf(line,"%zx-%zx",&vstart,&vend);
      if (virt2string.find(vstart) == virt2string.end())
      {
        virt2string[vstart] = line + 73;
      }
      virt.insert(vstart);
      if (oldvirt.find(vstart) == oldvirt.end())
      {
        //printf("new: addr %zx, old size %zu, new size %zu (%s)\n",vstart,virt2size[vstart],vend - vstart, virt2string[vstart].c_str());
      }
      else if (virt2size[vstart] != vend - vstart)
      {
        //printf("chg: addr %zx, old size %zu, new size %zu (%s)\n",vstart,virt2size[vstart],vend - vstart, virt2string[vstart].c_str());
      }
      virt2size[vstart] = vend - vstart;
    }
    for (auto v : oldvirt)
    {
      if (virt.find(v) == virt.end())
      {
        //printf("del: addr %zx, old size %zu (%s)\n",v,virt2size[v], virt2string[v].c_str());
        virt2string.erase(virt2string.find(v));
        for (size_t i = 0; i < virt2size[v]; i += 4096)
        {
        if (((v+i) & 0x1fffff) != 0 || (virt2phys[v+i] & 0x1fffff) != 0)
            continue;
          if (virt2phys[v+i] != 0)
          {
            //printf("unm: %zx -> %zx\n",v + i,virt2phys[v+i]);
          }
          virt2phys.erase(virt2phys.find(v+i));
        }
        virt2size.erase(virt2size.find(v));
      }
    }
    for (auto v : virt)
    {
      for (size_t i = 0; i < virt2size[v]; i += 4096)
      {
        size_t paddr = vaddr2paddr(fp,v + i);
        if (((v+i) & 0x1fffff) != 0 || (paddr & 0x1fffff) != 0 || paddr == 0)
          continue;
        if (virt2phys[v+i] != vaddr2paddr(fp,v + i) && (i == 0 || virt2phys[v+i] != virt2phys[v+i-1]+0x1000))
        {
          size_t t1 = rdtsc();
          printf("%10zu map %5zu: %20zx -> %20zx",(t1-t0)/2500000,alloccounter++,v + i,paddr);
          for (size_t j = 0; j < N_TARGET_ADDRS; ++j)
          {
            if ((target_addrs[j] & ~0x1fffff) == paddr)
            {
              printf(" (%zu)",found_target_addrs);
              phys2virtsel[paddr] = v + i;
              found_target_addrs++;
            }
          }
          printf("\n");
          if (found_target_addrs == N_TARGET_ADDRS)
          {
            printf("array start> ");
            size_t array_start = 0;
            size_t result = scanf("%zx",&array_start);
            for (size_t j = 0; j < N_TARGET_ADDRS; ++j)
            {
              size_t virt = phys2virtsel[target_addrs[j] & ~0x1fffff] | (target_addrs[j] & 0x1fffff);
              printf("%zx -> %zx -> %zx\n",target_addrs[j],virt,virt - array_start);
            }
            for (size_t j = 0; j < N_TARGET_ADDRS; ++j)
            {
              size_t virt = phys2virtsel[target_addrs[j] & ~0x1fffff] | (target_addrs[j] & 0x1fffff);
              printf("%zx\n",virt - array_start);
            }
            exit(0);
          }
          t0 = t1;
        }
        virt2phys[v+i] = vaddr2paddr(fp,v + i);
      }
    }
    if (round < 2)
      alloccounter = 0;
    oldvirt = virt;
    virt.clear();
    fseek(f,0,SEEK_SET);
  }
  free(line);
  return 0;
}
