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

#define SIZE 10000

#define ERROR 1
#define NO_ERROR 0

struct session {
	int sockfd, commfd;
	struct sockaddr_in servaddr;
	pthread_t tid;
}

vector<session> sessions;
pthread_mutex_t sessions_mutex;
pthread_mutex_t files_mutex;
pthread_cond_t prod_cond, cons_cond;

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
	sprintf(response, "HTTP/1.1 %d %s\r\nContent-Length: %d\r\n\r\n", code, getStatus(code), length);
	int errno = send(commfd, response, strlen(response), 0);
	if(errno < 0) { warn("send()"); }
}

void* dispatcher(void* arg) {
	session* client = (session*)arg;
	int sockfd = client->sockfd;

	while(1) {
		// socket accept
		char waiting[] = "Waiting for connection...\n";
		write(STDOUT_FILENO, waiting, strlen(waiting));
		servaddr_length = sizeof(client->servaddr);
		client->commfd = accept(sockfd, (struct sockaddr*)&(client->servaddr), (socklen_t*)&servaddr_length);
		if(client->commfd < 0) {
			warn("accept()");
			continue;
		}
		
		pthread_mutex_lock(&sessions_mutex);
		sessions.push_back(client);
		if(sessions.size() >= 4) {
			// if queue is full, wait
			pthread_cond_wait(&(client->prod_cond), &sessions_mutex);
		}
		// tell waiting consumers they can now work
		pthread_cond_signal(&client->cons_cond);
		pthread_mutex_unlock(&sessions_mutex);
	}
}

void* worker(void* arg) {
	session* client = (session*)arg;

	char buf[SIZE];
	char header[SIZE];
	char headercpy[SIZE];
	char response[SIZE];
	char *end_of_header, *type, *filename;
	int errors = NO_ERROR;

	while(1) {

		pthread_mutex_lock(&sessions_mutex);
		while(client.empty()) {
			// wait if buffer is empty
			pthread_cond_wait(&client->cons_cond, &sessions_mutex);
		}
		// tell waiting producers they can now dispatch
		pthread_cond_signal(&client->prod_cond);
		pthread_mutex_unlock(&sessions_mutex);

		// socket read request
		while(recv(commfd, buf, sizeof(char), 0) > 0) {
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
		if(strlen(filename) != 10) {

			// 400 Bad Request
			errors = ERROR;
			sendheader(commfd, response, 400, 0);
		}
		
		// check filename is alphanumeric
		for(char* i = filename; *i != '\0'; i++) {
			if((isalnum(*i) == 0) && (errors == NO_ERROR)) {

				// 400 Bad Request
				errors = ERROR;
				sendheader(commfd, response, 400, 0);
			}
		}

		// check for HTTP/1.1
		char* ptrhttp = strstr(header, "HTTP/1.1");
		if((ptrhttp == NULL) && (errors == NO_ERROR)) {

			// 400 Bad Request
			errors = ERROR;
			sendheader(commfd, response, 400, 0);
		}

		// restrict HTTP/1.1 methods to GET & PUT
		if (((strcmp(type, "GET") != 0) && (strcmp(type, "PUT") != 0)) && (errors == NO_ERROR)) {

			// 500 Internal Service Error
			errors = ERROR;
			sendheader(commfd, response, 500, 0);
		}

		// handling GET request	
		if((strcmp(type, "GET") == 0) && (errors == NO_ERROR)) {
			if(access(filename, F_OK) == -1) {

				// 404 File Not Found
				errors = ERROR;
				sendheader(commfd, response, 404, 0);
			} else if(access(filename, R_OK) == -1) {

				// 403 Forbidden
				errors = ERROR;
				sendheader(commfd, response, 403, 0);
			} else {
				
				// get file size
				stat(filename, &st);
				filesize = st.st_size;

				char *responseGet = new char[SIZE + filesize];

				// 200 OK header
				sendheader(commfd, responseGet, 200, filesize);

				// send file contents
				getfd = open(filename, O_RDONLY);
				while(read(getfd, buf, sizeof(char)) > 0) {
					send(commfd, buf, sizeof(char), 0);
					memset(&buf, 0, sizeof(buf));
				}
				close(getfd);

				delete[] responseGet;
			}
		}		
		
		// handling PUT request
		if((strcmp(type, "PUT") == 0) && (errors == NO_ERROR)) {
	        
			// get Content-Length
			char* ptrlength = strstr(header, "Content-Length:");

			// create / overwrite new file in directory
			putfd = open(filename, O_CREAT | O_WRONLY | O_TRUNC, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);

			if(ptrlength != NULL) {

				// get content if Content-Length is provided
				sscanf(ptrlength, "Content-Length: %d", &contentlength);
				
				char *responsePut = new char[SIZE + contentlength];

				// 201 Created header
				sendheader(commfd, responsePut, 201, contentlength);

				// put specified length of contents into file
				for(int i = 0; i < contentlength; i++) {
					errno = recv(commfd, buf, sizeof(char), 0);
					if(errno < 0) { err(1, "recv()"); }
					if(errno == 0) { break; }
					write(putfd, buf, sizeof(char));
					memset(&buf, 0, sizeof(char));
				}

				delete[] responsePut;
			} else {
			
				// put content into file and get Content-Length
				contentlength = recv(commfd, buf, sizeof(buf), 0);
				if(contentlength < 0) { err(1, "recv()"); }
				write(putfd, buf, contentlength);

				char *responsePut = new char[SIZE + contentlength];
				
				// 201 Created header
				sendheader(commfd, responsePut, 201, contentlength);

				delete[] responsePut;
			}
			close(putfd);
		}
	}

	// clear buffers
	memset(&buf, 0, sizeof(buf));
	memset(&header, 0, sizeof(header));
	memset(&headercpy, 0, sizeof(headercpy));
	memset(&response, 0, sizeof(response));

	// close TCP connection
	close(commfd);

	// remove client from queue
	pthread_mutex_lock(&sessions_mutex);
	for(int i = 0; i < sessions.size(); i++) {
		if(sessions[i]->commfd == client->commfd) {
			sessions.erase(sessions.begin() + i);
			break;
		}
	}
	pthread_mutex_unlock(&sessions_mutex);
	delete client;

	return(0);
}

int main(int argc, char* argv[]) {

	struct sockaddr_in servaddr;
	struct stat st;
	int numthreads;
	int listenfd, errno, port;
	int servaddr_length, commfd;
	int getfd, filesize;
	int putfd, contentlength;
	session *client = new session;
	
	// check cmd arg # & get port
	if(argc == 2) {
		port = 80;
	} else if(argc == 3) {
		port = atoi(argv[2]);
	} else {
		char usage[] = "USAGE: ./httpserver <address> <port-number> [-N -r]\n";
		write(STDOUT_FILENO, usage, sizeof(usage));
		exit(0);
	}

	// check for # of threads
	if(strcmp(argv[3], "-N") == 0) {
		numthreads = atoi(argv[4]);
	} else {
		numthreads = 4; // default
	}

	// init socket
	client->sockfd = socket(AF_INET, SOCK_STREAM, 0);
	if(listenfd < 0) { err(1, "socket()"); }

	// init address
	memset(&servaddr, 0, sizeof(servaddr));
	servaddr.sin_family = AF_INET;
	servaddr.sin_addr.s_addr = getaddr(argv[1]);
	servaddr.sin_port = htons(port);

	// socket bind
	errno = bind(listenfd, (struct sockaddr*)&servaddr, sizeof(servaddr));
	if(errno < 0) { err(1, "bind()"); }

	// socket listen
	errno = listen(listenfd, 10);
	if(errno < 0) { err(1, "listen()"); }

	pthread_mutex_init(&sessions_mutex, 0);
	pthread_mutex_init(&files_mutex, 0);

	pthread_create(&(client->tid), 0, dispatcher, (void*)&client);

	pthread_t worker_tid[numthreads];
	for(int i = 0; i < numthreads; i++) {
		pthread_create(&worker_tid[i], 0, worker, (void*)&client);
	}

	sleep(10);
	pthread_mutex_destroy(&sessions_mutex);
	pthread_cond_destroy(&(client->prod_cond));
	pthread_cond_destroy(&(client->cons_cond));

	exit(0);
}
