# Makefile

httpserver: httpserver2.o
	g++ -o httpserver httpserver2.o -lpthread

httpserver2.o: httpserver2.cpp
	g++ -c -std=gnu++11 -Wall -Wextra -Wpedantic -Wshadow httpserver2.cpp

clean:
	rm *.o httpserver
