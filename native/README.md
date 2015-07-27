
# Program for testing for the DRAM "rowhammer" problem using eviction

See https://github.com/google/rowhammer-test - this is an adaption of the ''double_sided_rowhammer'' program from their repository.

How to run the eviction-based rowhammer test:

```
make
./double_sided_rowhammer_ivy -d 1 # -d number of dimms
# or
./double_sided_rowhammer_haswell -d 1 # -d number of dimms
```

The test should work on x86-64 Linux.

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

Additionally, if your computer is susceptible to the rowhammer bug,
disable JavaScript in your browser! Attackers could exploit this bug
through JavaScript and take control over your machine.

