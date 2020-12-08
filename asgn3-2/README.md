Assignment 3 \
Name: Miranda Eng & Shirley Phuong \
CSE130: Principles of Computer Systems Design \
December 10, 2020

To run httpserver: \
	&ensp;&ensp;&ensp;make \
	&ensp;&ensp;&ensp;./httpserver &lt;address&gt; &lt;port-number&gt;

Example run: \
	&ensp;&ensp;&ensp;./httpserver localhost 8080

Limitations or known issues \
	&ensp;&ensp;&ensp;None found so far

-------------------------------------------------

httpserver.cpp \
	&ensp;&ensp;&ensp;This program implements a backup and recovery enabling, single-threaded \
	&ensp;&ensp;&ensp;HTTP server that responds to GET and PUT commands to read and write \
	&ensp;&ensp;&ensp;files named by 10-characters in ASCII. In response to a GET request \
	&ensp;&ensp;&ensp;from a client, httpserver will detect a file name, read the file \
	&ensp;&ensp;&ensp;contents, write the associated HTTP response header, and, if \
	&ensp;&ensp;&ensp;appropriate, the associated content to stdout on the client side. In \
	&ensp;&ensp;&ensp;response to a PUT request from a client, httpserver will also take data \
	&ensp;&ensp;&ensp;from the request then create or overwrite the client specified file to \
	&ensp;&ensp;&ensp;put into the server directory and write the associated HTTP response \
	&ensp;&ensp;&ensp;header on the client side

DESIGN.pdf  
	&ensp;&ensp;&ensp;Describes program functionality, design (data structures, functions, \
	&ensp;&ensp;&ensp;non-trivial algorithms, etc), testing procedures \
	&ensp;&ensp;&ensp;Answers the assignment question  

Makefile  
	&ensp;&ensp;&ensp;Creates object file and executable for httpserver.cpp \
	&ensp;&ensp;&ensp;Make clean removes the object file and executable  

Readme.md  
	&ensp;&ensp;&ensp;This file. Provides a description of all files in assignment. \
	&ensp;&ensp;&ensp;Includes instructions necessary to run the code \
	&ensp;&ensp;&ensp;Includes limitations or known issues with the code