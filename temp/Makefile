# Makefile

httpserver: httpserver.o
	g++ -o httpserver httpserver.o -lpthread

httpserver.o: httpserver.cpp
	g++ -c -std=c++17 -Wall -Wextra -Wpedantic -Wshadow httpserver.cpp

clean:
	rm -rf *.o httpserver