Assignment 1 \
Name: Miranda Eng & Tiffany Phuong \
CSE130: Principles of Computer Systems Design \
November 2, 2020 \

To run httpserver: \
	make \
	./httpserver <address> <port-number> \

Example run: \
	./httpserver localhost 8080 \

Limitations or known issues \
	None found so far \

-------------------------------------------------

httpserver.cpp \
	This program implements a single-threaded HTTP server that responds to GET and PUT commands to read and write files named by 10-characters in ASCII. In response to a GET request from a client, httpserver will detect a file name, read the file contents, write the associated HTTP response header, and, if appropriate, the associated content to stdout on the client side. In response to a PUT request from a client, httpserver will also take data from the request then create or overwrite the client specified file to put into the server directory and write the associated HTTP response header on the client side \

Makefile \
	Creates object file and executable for httpserver.cpp \
	Make clean removes the object file and executable \

Readme.md \
	This file. Provides a description of all files in assignment 1 \
	Includes instructions necessary to run the code \
	Includes limitations or known issues with the code \
