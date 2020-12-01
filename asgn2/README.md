Assignment 2 \
Name: Miranda Eng & Shirley Phuong \
CSE130: Principles of Computer Systems Design \
November 30, 2020

To run httpserver: \
	&ensp;&ensp;&ensp;make \
	&ensp;&ensp;&ensp;./httpserver &lt;address&gt; &lt;port-number&gt; [options] \
	&ensp;&ensp;&ensp;options: \
	&ensp;&ensp;&ensp;&ensp;&ensp;&ensp;-N to set number of threads \
	&ensp;&ensp;&ensp;&ensp;&ensp;&ensp;-r to enable redundancy

Example run:  
	&ensp;&ensp;&ensp;./httpserver localhost 8080 \
	&ensp;&ensp;&ensp;./httpserver localhost 8080 -N 3 \
	&ensp;&ensp;&ensp;./httpserver localhost 8080 -r \
	&ensp;&ensp;&ensp;./httpserver localhost 8080 -N 3 -r

Limitations or known issues \
	&ensp;&ensp;&ensp;None found so far

-------------------------------------------------

httpserver.cpp \
	&ensp;&ensp;&ensp;This program implements a multi-threaded HTTP server that responds \
	&ensp;&ensp;&ensp;to GET and PUT commands to read and write files named by 10-characters \
	&ensp;&ensp;&ensp;in ASCII. A user can specify number of threads and/or to enable redundancy. \
	&ensp;&ensp;&ensp;If no number of threads is specified, the program will use 4 threads. \
	&ensp;&ensp;&ensp;If redundancy is enabled, the program will only work with the files in \
	&ensp;&ensp;&ensp;copy1, copy2, and copy3 so there will be 3 copies of each file.\
	&ensp;&ensp;&ensp;In response to a GET request from a client, httpserver will \
	&ensp;&ensp;&ensp;detect a file name, read the file contents, write the associated HTTP \
	&ensp;&ensp;&ensp;response header, and, if appropriate, the associated content to stdout \
	&ensp;&ensp;&ensp;on the client side. In response to a PUT request from a client, \
	&ensp;&ensp;&ensp;httpserver will also take data from the request then create or \
	&ensp;&ensp;&ensp;overwrite the client specified file to put into the server directory \
	&ensp;&ensp;&ensp;and write the associated HTTP response header on the client side


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