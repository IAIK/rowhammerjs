all: double_sided_rowhammer_haswell double_sided_rowhammer_ivy
clean:
	rm -f double_sided_rowhammer_haswell double_sided_rowhammer_ivy
double_sided_rowhammer_haswell: double_sided_rowhammer_haswell.cc
	g++ -g -pthread -std=c++11 -O3 -o $@ $@.cc
double_sided_rowhammer_ivy: double_sided_rowhammer_ivy.cc
	g++ -g -pthread -std=c++11 -O3 -o $@ $@.cc

