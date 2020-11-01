# Makefile

httpserver: httpserver.o
	g++ -o httpserver httpserver.o

httpserver.o: httpserver.cpp
	g++ -c httpserver.cpp

clean:
	rm *.o httpserver
