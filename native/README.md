# Program for testing for the DRAM "rowhammer" problem

See https://github.com/google/rowhammer-test - this is an adaption of the
''double_sided_rowhammer'' program from their repository.

Also see our paper ''Rowhammer.js: A Remote Software-Induced Fault Attack in JavaScript'': https://scholar.google.at/citations?view_op=view_citation&hl=de&user=JmCg4uQAAAAJ&citation_for_view=JmCg4uQAAAAJ:tOudhMTPpwUC

This tool also uses the DRAM addressing functions from ''DRAMA: Exploiting DRAM Addressing for Cross-CPU Attacks'' https://www.usenix.org/conference/usenixsecurity16/technical-sessions/presentation/pessl

## Build

Use one of the following:
```
make
make rowhammer-sandy
make rowhammer-ivy
make rowhammer-haswell
make rowhammer-skylake
```

The test should work on x86-64 Linux.

## Run

Run as follows:
```
# ./rowhammer[-architecture] [-t nsecs] [-p percent] [-c cores] [-d dimms] [-r row] [-f first_offset] [-s second_offset]
./rowhammer-skylake -c 2 -d 2
```
### Parameters
- ''-c'' the number of cores (only important with ''#define EVICTION_BASED'')
- ''-p'' percent of memory to use
- ''-d'' number of dimms (very important)
- ''-r'' loop only over the specified row
- ''-f'' only test addresses with the specified first aggressor offset
- ''-s'' only test addresses with the specified second aggressor offset

### Definitions
- ''#define PERF_COUNTERS'' to measure cache hits and misses
- ''#define MEASURE_EVICTION'' to generate histograms to verify whether eviction works
- ''#define FIND_EXPLOITABLE_BITFLIPS'' retry hammering addresses with bitflips to check for bitflips possibly exploitable from JavaScript

## Warnings

Same warnings as in the original https://github.com/google/rowhammer-test repository:

**Warning #1:** We are providing this code as-is.  You are responsible
for protecting yourself, your property and data, and others from any
risks caused by this code.  This code may cause unexpected and
undesirable behavior to occur on your machine.  This code may not
detect the vulnerability on your machine.

Be careful not to run this test on machines that contain important
data.  On machines that are susceptible to the rowhammer problem, this
test could cause bit flips that crash the machine, or worse, cause bit
flips in data that gets written back to disc.

**Warning #2:** If you find that a computer is susceptible to the
rowhammer problem, you may want to avoid using it as a multi-user
system.  Bit flips caused by row hammering breach the CPU's memory
protection.  On a machine that is susceptible to the rowhammer
problem, one process can corrupt pages used by other processes or by
the kernel.

