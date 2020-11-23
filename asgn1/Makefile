# Makefile

httpserver1: httpserver1.o
	g++ -o httpserver1 httpserver1.o

httpserver1.o: httpserver1.cpp
	g++ -c -std=gnu++11 -Wall -Wextra -Wpedantic -Wshadow httpserver1.cpp

clean:
	rm *.o httpserver1
