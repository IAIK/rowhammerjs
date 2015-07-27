
# Program for testing for the DRAM "rowhammer" problem using eviction

See https://github.com/google/rowhammer-test - this is an adaption of the
''double_sided_rowhammer'' program from their repository.

## How to run the native eviction-based rowhammer test

```
cd native
make
./double_sided_rowhammer_ivy -d 1 # -d number of dimms
# or
./double_sided_rowhammer_haswell -d 1 # -d number of dimms
```

The test should work on x86-64 Linux.

If you have found a reproducible bitflip, look for the ''Print this for the
watch_firefox tool'' comment.

## Find the array indices for specific physical addresses

Edit tools/watch_firefox.cc to contain the addresses from your native
eviction-based rowhammer test.

Start firefox with rowhammer.html

```
cd tools
make
./watch_firefox
```

The program outputs physical address mappings and the time since
the last allocation. Based on this and the virtual address printed you
can determine where the array starts.

Allocate memory in a large array in JavaScript.
If using ''rowhammer.html'': Click the ''Allocate'' button.

If you have much noise before you press the button, just restart
''watch_firefox'' and try again.

As soon as it has found the indices it asks you to enter the virtual address
of the array start. This is not yet automated.

The program prints the array indices to use in JavaScript.
In case of ''rowhammer.html'' just copy them into the editbox and click ''Parse''.
Then you can start Hammering.

## Rowhammer.html / Rowhammer.js
In the ''javascript'' folder your right now only find the Rowhammer.js version
for Haswell CPUs with a 16-way L3 cache and no L4 cache. It will probably not
work on other CPUs without modifications.

Open ''rowhammer.html'' in a browser, paste the hammering array indices in the
editbox (you can use the ''watch_firefox'' program for this).

You can modify ''rowhammer.js'' while the page is still loaded and click the
''Refresh'' button to only reload the ''rowhammer.js'' file. This way you keep
the array and the array indices and you can experiment with different settings
while not having to search for the array indices anew.

## JavaScript only variant
Not yet public.

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

Additionally, if your computer is susceptible to the rowhammer bug,
disable JavaScript in your browser! Attackers could exploit this bug
through JavaScript and take control over your machine.

