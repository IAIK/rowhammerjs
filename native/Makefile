all: rowhammer
clean:
	rm -f rowhammer
rowhammer: rowhammer.cc
	g++ -g -pthread -std=c++11 -O3 -o $@ $@.cc
rowhammer-ivy: rowhammer.cc
	g++ -g -pthread -std=c++11 -O3 -o $@ -DIVY $^
rowhammer-sandy: rowhammer.cc
	g++ -g -pthread -std=c++11 -O3 -o $@ -DSANDY $^
rowhammer-haswell: rowhammer.cc
	g++ -g -pthread -std=c++11 -O3 -o $@ -DHASWELL $^
rowhammer-skylake: rowhammer.cc
	g++ -g -pthread -std=c++11 -O3 -o $@ -DSKYLAKE $^

