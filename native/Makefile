all: rowhammer
clean:
	rm -f rowhammer
rowhammer: rowhammer.cc
	g++ -g -pthread -std=c++11 -O3 -o $@ $@.cc

