#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <err.h>

#include <stdio.h> // remove this and printf()s later

const char* getStatus(int code) {
	switch(code) {
		case 200: return "ok";
		case 201: return "created";
		case 400: return "bad request";
		case 403: return "forbidden";
		case 404: return "not found";
		case 500: return "internal server error";
	}
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

void httpresponse(char* response, int code, int length) {
	sprintf(response, "HTTP/1.1 %d %s\r\nContent-Length: %d\r\n\r\n", code, getStatus(code), length);
	send(commfd, response, strlen(response), 0);
}

int main(int argc, char* argv[]) {

	struct sockaddr_in servaddr;
	int listenfd, n;
	int servaddr_length, commfd;
	
	// check cmd arg #
	if(argc != 3) {
		printf("USAGE: ./httpserver <address> <port-number>");
	}

	// init socket
	listenfd = socket(AF_INET, SOCK_STREAM, 0);
	if(listenfd < 0) { err(1, "socket()"); }

	// init address
	memset(&servaddr, 0, sizeof(servaddr));
	servaddr.sin_family = AF_INET;
	servaddr.sin_addr.s_addr = getaddr(argv[1]);
	servaddr.sin_port = htons(atoi(argv[2]));

	// socket bind
	n = bind(listenfd, (struct sockaddr*)&servaddr, sizeof(servaddr));
	if(n < 0) { err(1, "bind()"); }

	// socket listen
	n = listen(listenfd, 10);
	if(n < 0) { err(1, "listen()"); } 

	while(1) {

		// socket accept
		servaddr_length = sizeof(servaddr);
		commfd = accept(listenfd, (struct sockaddr*)&servaddr, (socklen_t*)&servaddr_length);
		if(commfd < 0) {
			warn("accept()");
			continue;
		}

		// socket read request
		char header[10000], buffer[100], response[100];
		char* end_of_header;
		while(recv(commfd, buffer, sizeof(char), 0) > 0) {
			write(STDOUT_FILENO, buffer, sizeof(char));
			strcat(header, buffer);
			end_of_header = strstr(header, "\r\n\r\n");
			if(end_of_header != NULL) { break; }
			memset(&buffer, 0, sizeof(buffer));
		}

		// check HTTP/1.1
		char* ptrhttp = strstr(header, "HTTP/1.1");
		if(ptrhttp == NULL) {
			// 400 Bad Request
		}

		// get request type
		char* type = strtok(header, " ");
			
		// get filename
		char* filename = strtok(NULL, " ");
		filename++;

		// zero header ?
		memset(&header, 0, sizeof(header));

		// check filename length (= 10 chars)
		if(strlen(filename) != 10) {
			// 400 Bad Request
		}

		// check filename is alphanumeric
		for(char* i = filename; *i != '\0'; i++) {
			if(isalnum(*i) == 0) {
				// 400 Bad Request
			}
		}
		
		struct stat st;
		int filesize;
		if(strcmp(type, "GET") == 0) {
			if(access(filename, F_OK) == 1) {
				// 404 File Not Found
			} else {
				if(access(filename, R_OK) == -1) {
					// 403 Forbidden
				}
				stat(filename, &st);
				filesize = st.st_size;
			}
		}

		strcpy(response, "HTTP/1.1 200 OK\r\nContent-Length: 6\r\n\r\nNice\r\n");
		send(commfd, response, strlen(response), 0);

	}
	close(commfd);
}
