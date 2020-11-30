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
#include <pthread.h>
#include <queue>

#define SIZE 10000

#define ERROR 1
#define NO_ERROR 0

struct session {
	int commfd;
};

struct file {
	char* filename;
	pthread_mutex_t file_mutex;
};

struct shared_data {
	int numthreads;
	int sockfd;
	struct sockaddr_in servaddr;
	std::queue<session*> sessions;
	std::vector<file*> files;
	pthread_mutex_t sessions_mutex;
	pthread_cond_t dispatcher_cond, worker_cond;
};

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
	shared_data* client = (shared_data*)arg;
	int sockfd = client->sockfd;

	while(1) {

		pthread_mutex_lock(&(client->sessions_mutex));
		while((int)client->sessions.size() >= client->numthreads) {
			// if queue is full, wait
			pthread_cond_wait(&(client->dispatcher_cond), &(client->sessions_mutex));
		}
		pthread_mutex_unlock(&(client->sessions_mutex));

		session* ses = new session;

		// socket accept
		char waiting[] = "Waiting for connection...\n";
		write(STDOUT_FILENO, waiting, strlen(waiting));
		int servaddr_length = sizeof(client->servaddr);
		ses->commfd = accept(sockfd, (struct sockaddr*)&(client->servaddr), (socklen_t*)&servaddr_length);
		if(ses->commfd < 0) {
			warn("accept()");
			continue;
		}
		
		pthread_mutex_lock(&(client->sessions_mutex));
		client->sessions.push(ses);
		// tell waiting consumers they can now work
		pthread_cond_signal(&(client->worker_cond));
		pthread_mutex_unlock(&(client->sessions_mutex));
	}
}

void* worker(void* arg) {
	shared_data* client = (shared_data*)arg;

	struct stat st;
	int getfd, filesize;
	int putfd, contentlength;
	int errno;
	char buf[SIZE];
	char header[SIZE];
	char headercpy[SIZE];
	char response[SIZE];
	char *end_of_header, *type, *filename;
	int errors = NO_ERROR;

	while(1) {

		pthread_mutex_lock(&(client->sessions_mutex));
		while(client->sessions.empty()) {
			// wait if buffer is empty
			pthread_cond_wait(&(client->worker_cond), &(client->sessions_mutex));
		}
		session* ses = client->sessions.front();
		int commfd = ses->commfd;
		client->sessions.pop();
		delete ses;
		pthread_mutex_unlock(&(client->sessions_mutex));

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
		
		printf("%s\n", filename);
		int l = strlen(filename);
		printf("%d\n", l);

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
				contentlength = recv(ses->commfd, buf, sizeof(buf), 0);
				if(contentlength < 0) { err(1, "recv()"); }
				write(putfd, buf, contentlength);

				char *responsePut = new char[SIZE + contentlength];
				
				// 201 Created header
				sendheader(commfd, responsePut, 201, contentlength);

				delete[] responsePut;
			}

			// close putfd
			close(putfd);
		}

		// close TCP connection
		close(ses->commfd);

		// clear buffers
		memset(&buf, 0, sizeof(buf));
		memset(&header, 0, sizeof(header));
		memset(&headercpy, 0, sizeof(headercpy));
		memset(&response, 0, sizeof(response));

		// tell waiting producers they can now dispatch
		pthread_cond_signal(&(client->dispatcher_cond));
	}
}

int main(int argc, char* argv[]) {

	struct sockaddr_in servaddr;
	int opt;
	int errno, port;
	shared_data *client = new shared_data;
	client->numthreads = 4; //default
	//int redundancy;

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

    while((opt = getopt(argc, argv, "N:r")) != -1) {
        switch (opt) {
            case 'N':
                client->numthreads = atoi(optarg);
                break;
            case 'r':
                //redundancy = 1;
                printf("Option: %c\n", opt);
                break;
        }
    }

	// init socket
	client->sockfd = socket(AF_INET, SOCK_STREAM, 0);
	if(client->sockfd < 0) { err(1, "socket()"); }

	// init address
	memset(&servaddr, 0, sizeof(servaddr));
	servaddr.sin_family = AF_INET;
	servaddr.sin_addr.s_addr = getaddr(argv[1]);
	servaddr.sin_port = htons(port);

	// socket bind
	errno = bind(client->sockfd, (struct sockaddr*)&servaddr, sizeof(servaddr));
	if(errno < 0) { err(1, "bind()"); }

	// socket listen
	errno = listen(client->sockfd, 10);
	if(errno < 0) { err(1, "listen()"); }

	// initialize session mutex
	pthread_mutex_init(&client->sessions_mutex, 0);

	// create threads
	pthread_t dispatcher_tid[1];
	pthread_t worker_tid[SIZE];
	pthread_create(&dispatcher_tid[0], 0, dispatcher, (void*)&client);
	for(int i = 0; i < client->numthreads; i++) {
		pthread_create(&worker_tid[i], 0, worker, (void*)&client);
	}

	sleep(10);
	pthread_mutex_destroy(&(client->sessions_mutex));
	pthread_cond_destroy(&(client->dispatcher_cond));
	pthread_cond_destroy(&(client->worker_cond));

	exit(0);
}
