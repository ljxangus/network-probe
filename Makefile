CC=g++
CFLAGS=-D_GLIBCXX_USE_NANOSLEEP -lrt -pthread -std=c++0x

all: test

test:
	$(CC) $(CFLAGS) NetProbe.cpp -o NetProbe
clean:
	rm test
