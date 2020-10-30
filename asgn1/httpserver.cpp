#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <err.h>

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

		// socket read
		char request[1000], buffer[100];
		char* end_of_header;
		while(recv(commfd, buffer, sizeof(char), 0) > 0) {
			write(STDOUT_FILENO, buffer, sizeof(char));
			strcat(request, buffer);
			if((end_of_header = strstr(request, "\r\n\r\n")) != NULL) { break; }
			memset(&buffer, 0, sizeof(buffer));
		}
		memset(&header, 0, sizeof());
	}
	close(commfd);
}
