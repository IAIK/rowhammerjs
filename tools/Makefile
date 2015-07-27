all: watch_firefox
clean:
	rm -f watch_firefox
watch_firefox: watch_firefox.cc
	g++ -g -pthread -std=c++11 -O3 -o $@ $@.cc
