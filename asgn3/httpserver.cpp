#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <netdb.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <fcntl.h>
#include <err.h>

#define ERROR 1
#define NO_ERROR_YET 0

const char* getStatus(int code) {
	switch(code) {
		case 200: return "OK";
		case 201: return "Created";
		case 400: return "Bad Request";
		case 403: return "Forbidden";
		case 404: return "Not Found";
		case 500: return "Internal Server Error";
	}
	return NULL;
}

unsigned long getaddr(char *name) {
	unsigned long res;
	struct addrinfo hints;
	struct addrinfo* info;
	
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET;
	if (getaddrinfo(name, NULL, &hints, &info) != 0 || info == NULL) {
		char msg[] = "getaddrinfo(): address identification error\n";
		write(STDERR_FILENO, msg, strlen(msg));
		exit(1);
	}
	res = ((struct sockaddr_in*) info->ai_addr)->sin_addr.s_addr;
	freeaddrinfo(info);
	return res;
}

void sendheader(int commfd, char* response, int code, int length) {
	sprintf(response, "\nHTTP/1.1 %d %s\r\nContent-Length: %d\r\n\r\n", code, getStatus(code), length);
	int errno = send(commfd, response, strlen(response), 0);
	if (errno < 0) { warn("send()"); }
}

int main(int argc, char* argv[]) {

	struct sockaddr_in servaddr;
	struct stat st;
	int listenfd, n, port;
	int servaddr_length, commfd;
	int getfd, filesize;
	int putfd, contentlength;
	
	// check cmd arg # & get port
	if(argc == 2) {
		port = 80;
	} else if(argc == 3) {
		port = atoi(argv[2]);
	} else {
		char usage[] = "USAGE: ./httpserver <address> <port-number>\n";
		write(STDOUT_FILENO, usage, sizeof(char));
	}

	// init socket
	listenfd = socket(AF_INET, SOCK_STREAM, 0);
	if(listenfd < 0) { err(1, "socket()"); }

	// init address
	memset(&servaddr, 0, sizeof(servaddr));
	servaddr.sin_family = AF_INET;
	servaddr.sin_addr.s_addr = getaddr(argv[1]);
	servaddr.sin_port = htons(port);

	// socket bind
	n = bind(listenfd, (struct sockaddr*)&servaddr, sizeof(servaddr));
	if(n < 0) { err(1, "bind()"); }

	// socket listen
	n = listen(listenfd, 10);
	if(n < 0) { err(1, "listen()"); } 

	while (1) {

		char buf[100];
		char header[5000];
		char headercpy[5000];
		char response[500];       // initialize to size of content + 10000 for the header
		char *end_of_header, *type, *filename;
		int errors = NO_ERROR_YET;
        int bflag = 0;
        int rflag = 0;
        int r2flag = 0;
        int lflag = 0;

		// socket accept
		char waiting[] = "\nWaiting for connection...\n";
		write(STDOUT_FILENO, waiting, strlen(waiting));
		servaddr_length = sizeof(servaddr);
		commfd = accept(listenfd, (struct sockaddr*)&servaddr, (socklen_t*)&servaddr_length);
		if (commfd < 0) {
			warn("accept()");
			continue;
		}
		
		// socket read request
		while (recv(commfd, buf, sizeof(char), 0) > 0) {
			write(STDOUT_FILENO, buf, sizeof(char));
			strcat(header, buf);
			end_of_header = strstr(header, "\r\n\r\n");
			if(end_of_header != NULL) { break; }
			memset(&buf, 0, sizeof(buf));
		}
		
		// get request type
		strcpy(headercpy, header);
		type = strtok(headercpy, " ");
		
		// get filename
		filename = strtok(NULL, " ");
		filename++;
		
		// check filename length
        char *recoverypointer = strstr(header, "/r/");
        char *timestamp;

        if ((strcmp(filename, "b") == 0) && (strcmp(type, "GET") == 0)) {
            bflag = 1;
        }
        else if ((strcmp(filename, "r") == 0) && (strcmp(type, "GET") == 0)) {
            rflag = 1;
        }
        else if ((recoverypointer != NULL) && (strcmp(type, "GET") == 0)) {
            sscanf(recoverypointer, "/r/%s", &timestamp);
            for (char* i = timestamp; *i != '\0'; i++) {
                    if ((isdigit(*i) == 0) && (errors == NO_ERROR_YET)) {
                        errors = ERROR;
                        sendheader(commfd, response, 400, 0);
                    }
            }
            if (errors != ERROR) {
                r2flag = 1; 
            }
        }
        else if ((strcmp(filename, "l") == 0) && (strcmp(type, "GET") == 0)) {
            lflag = 1;
        }
		else if (strlen(filename) != 10) {
			// 400 Bad Request
			errors = ERROR;
			sendheader(commfd, response, 400, 0);
		}
		
		// check filename is alphanumeric
        if ((bflag == 0) && (rflag == 0) && (r2flag == 0) && (lflag == 0)) {
		    for (char* i = filename; *i != '\0'; i++) {
			    if ((isalnum(*i) == 0) && (errors == NO_ERROR_YET)) {

				    // 400 Bad Request
				    errors = ERROR;
				    sendheader(commfd, response, 400, 0);
			    }
		    }
        }

		// check for HTTP/1.1
		char* ptrhttp = strstr(header, "HTTP/1.1");
		if ((ptrhttp == NULL) && (errors == NO_ERROR_YET)) {

			// 400 Bad Request
			errors = ERROR;
			sendheader(commfd, response, 400, 0);
		}

		// check for other HTTP/1.1 methods
		if (((strcmp(type, "GET") != 0) && (strcmp(type, "PUT") != 0)) && (errors == NO_ERROR_YET)) {

			// 500 Internal Service Error
			errors = ERROR;
			sendheader(commfd, response, 500, 0);
        }
	
		// handling GET request	
		memset(&buf, 0, sizeof(buf));
		if ((strcmp(type, "GET") == 0) && (errors == NO_ERROR_YET)) {
			if (access(filename, F_OK) == -1) {

				// 404 File Not Found
				errors = ERROR;
				sendheader(commfd, response, 404, 0);
			} 
            else if (access(filename, R_OK) == -1) {

				// 403 Forbidden
				errors = ERROR;
				sendheader(commfd, response, 403, 0);
			} 
            else {

				// get file size
				stat(filename, &st);
				filesize = st.st_size;

				sendheader(commfd, response, 200, filesize);

				// get file contents
				getfd = open(filename, O_RDONLY);
				while(read(getfd, buf, sizeof(char))) {
					send(commfd, buf, sizeof(char), 0);
					memset(&buf, 0, sizeof(buf));
				}
				close(getfd);
			}
		}		
		
		// handling PUT request
		memset(&buf, 0, sizeof(buf));
		if ((strcmp(type, "PUT") == 0) && (errors == NO_ERROR_YET)) {
            
			// get Content-Length
			char* ptrlength = strstr(header, "Content-Length:");

			if (ptrlength != NULL) {

				// get content if Content-Length is provided
				sscanf(ptrlength, "Content-Length: %d", &contentlength);

				putfd = open(filename, O_CREAT | O_WRONLY | O_TRUNC, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);

				for (int i = contentlength; i > 0; i--) {
					n = recv(commfd, buf, sizeof(char), 0);
					if(n < 0) { warn("recv()"); }
					if(n == 0) { break; }
					write(STDOUT_FILENO, buf, sizeof(char));
					write(putfd, buf, sizeof(char));
					memset(&buf, 0, sizeof(buf));
				}

				close(putfd);

				sendheader(commfd, response, 201, 0);

			} 
            else {
				
				// // get content if Content-Length is not provided

				putfd = open(filename, O_CREAT | O_WRONLY | O_TRUNC, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
				sendheader(commfd, response, 201, 0);

				while (1) {
					int amtRcv = recv(commfd, buf, sizeof(char), 0);
					if (amtRcv == 0) {
						break;
					}
					write(STDOUT_FILENO, buf, sizeof(char));
					write(putfd, buf, sizeof(char));
					memset(&buf, 0, sizeof(buf));
				}

				close(putfd);

			}
		}

		// clear buffers
		memset(&header, 0, sizeof(header));
		memset(&headercpy, 0, sizeof(headercpy));
		memset(&response, 0, sizeof(response));
		
		// close TCP connection
		close(commfd);
	}
}