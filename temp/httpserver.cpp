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
#include <vector>
#include <dirent.h>
#include <errno.h>

#define SMALLBUF 500
#define LARGEBUF 10000

#define ERROR 1
#define NO_ERROR_YET 0

struct session {
    int commfd;
};

struct file {
    char* filename;
    pthread_mutex_t file_mutex;
};

struct shared_data {
    pthread_mutex_t sessions_mutex;
    pthread_cond_t dispatcher_cond, worker_cond;
    std::vector<file> files;
    std::queue<session> sessions;
	int rflag;
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
	sprintf(response, "\nHTTP/1.1 %d %s\r\nContent-Length: %d\r\n\r\n", code, getStatus(code), length);
	int n = send(commfd, response, strlen(response), 0);
	if (n < 0) { warn("send()"); }
}

void* worker(void* data) {
    // struct
    struct shared_data* shared = (struct shared_data*) data;
    struct stat st;

    while(1) {

        // lock global mutex to get data
        pthread_mutex_lock(&shared->sessions_mutex);
        while (shared->sessions.empty()) {
            pthread_cond_wait(&shared->worker_cond, &shared->sessions_mutex);
        }
        int commfd =  shared->sessions.front().commfd;
        shared->sessions.pop();
        pthread_mutex_unlock(&shared->sessions_mutex);

        char buf[SMALLBUF];
		char header[LARGEBUF];
		char headercpy[LARGEBUF];
		char response[LARGEBUF];       // initialize to size of content + 10000 for the header
		char *end_of_header, *type, *filename;
		int errors = NO_ERROR_YET;
        int contentlength, filesize;

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
			if((isalnum(*i) == 0) && (errors == NO_ERROR_YET)) {

				// 400 Bad Request
				errors = ERROR;
				sendheader(commfd, response, 400, 0);
			}
		}

		// check for HTTP/1.1
		char* ptrhttp = strstr(header, "HTTP/1.1");
		if((ptrhttp == NULL) && (errors == NO_ERROR_YET)) {

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
        int getfd;	
		memset(&buf, 0, sizeof(buf));
		if((strcmp(type, "GET") == 0) && (errors == NO_ERROR_YET)) {

			if(access(filename, F_OK) == -1) {

				// 404 File Not Found
				errors = ERROR;
				sendheader(commfd, response, 404, 0);
			} else if(access(filename, R_OK) == -1) {

				// 403 Forbidden
				errors = ERROR;
				sendheader(commfd, response, 403, 0);
			} else {

				pthread_mutex_lock(&shared->sessions_mutex);
				int i;
				for(i = 0; i < (int)shared->files.size(); i++) {
					if(strcmp(shared->files[i].filename, filename) == 0) { break; }
				}
				pthread_mutex_unlock(&shared->sessions_mutex);

				pthread_mutex_lock(&shared->files[i].file_mutex);

				// get file size
				stat(filename, &st);
				filesize = st.st_size;

                char *responseGet;
                responseGet = new char[LARGEBUF + filesize];

				sendheader(commfd, responseGet, 200, filesize);

				// get file contents
				getfd = open(filename, O_RDONLY);
				while(read(getfd, buf, sizeof(char))) {
					send(commfd, buf, sizeof(char), 0);
					memset(&buf, 0, sizeof(buf));
				}
				close(getfd);

                delete[] responseGet;

                pthread_mutex_unlock(&shared->files[i].file_mutex);
			}
		}		
		
		// handling PUT request
        int putfd[3], n;
		memset(&buf, 0, sizeof(buf));
		if((strcmp(type, "PUT") == 0) && (errors == NO_ERROR_YET)) {

			struct file newfile;
			newfile.filename = filename;
            
            pthread_mutex_lock(&shared->sessions_mutex);
            pthread_mutex_init(&newfile.file_mutex, 0);
			pthread_mutex_lock(&newfile.file_mutex);

			// get Content-Length
			char* ptrlength = strstr(header, "Content-Length:");

			char path[SMALLBUF];
			for(int d = 0; d < 3; d++) {
				sprintf(path, ".copy%d/%s", d+1, filename);

				// create / overwrite new file in directory
				putfd[d] = open(path, O_CREAT | O_WRONLY | O_TRUNC, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
			}

			if(ptrlength != NULL) {

				// get content if Content-Length is provided
				sscanf(ptrlength, "Content-Length: %d", &contentlength);

            	char *responsePut = new char[LARGEBUF + contentlength];

            	// 201 Created header
            	sendheader(commfd, responsePut, 201, contentlength);

            	// put specificed length of contents into file
				for(int i = 0; i < contentlength; i++) {
					n = recv(commfd, buf, sizeof(char), 0);
					if(n < 0) { warn("recv()"); }
					if(n == 0) { break; }
					for(int j = 0; j < 3; j++) {
						n = write(putfd[j], buf, sizeof(char));
						if(n < 0) { err(1, "write()"); }
					}
					memset(&buf, 0, sizeof(buf));
				}

				delete[] responsePut;
			} else {
				
				// put content into file and get Content-Length
				contentlength = recv(commfd, buf, sizeof(char), 0);
				if(contentlength < 0) { err(1, "recv()"); }
				for(int j = 0; j < 3; j++) {
					write(putfd[j], buf, contentlength);
				}

                char *responsePut = new char[LARGEBUF + contentlength];

                // 201 Created header
				sendheader(commfd, responsePut, 201, contentlength);

				delete[] responsePut;
			}

			for(int j = 0; j < 3; j++) {
				close(putfd[j]);
			}

			shared->files.push_back(newfile);
			pthread_mutex_unlock(&newfile.file_mutex);
			pthread_mutex_unlock(&shared->sessions_mutex);
		}

		// close TCP connection
		close(commfd);

		// clear buffers
		memset(&header, 0, sizeof(header));
		memset(&headercpy, 0, sizeof(headercpy));
		memset(&response, 0, sizeof(response));

		pthread_cond_signal(&shared->dispatcher_cond);
		sleep(1);
    }
}

int main(int argc, char* argv[]) {

    // parse command line using getopt()
    int numthreads = 4;
    int redundancy = 0;

    int opt;
    while ((opt = getopt(argc, argv, "N:r")) != -1) {
        switch (opt) {
            case 'N':
                numthreads = atoi(optarg);
                //printf("Option: %c\nNumber of threads: %d\n", opt, num_workers);
                break;
            case 'r':
                redundancy = 1;
                //printf("Option: %c\n", opt);
                break;
        }
    }

    // bind sockets
    struct sockaddr_in servaddr;
    int sockfd, n, port;

    // init socket
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) { err(1, "socket()"); }

    // init address
    port = 80;
    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = getaddr(argv[optind]);
    if (argv[optind+1] != NULL) {
        port = atoi(argv[optind+1]);
    }
    servaddr.sin_port = htons(port);

    // socket bind
    n = bind(sockfd, (struct sockaddr*)&servaddr, sizeof(servaddr));
    if (n < 0) { err(1, "bind()"); }

    // socket listen
    n = listen(sockfd, 10);
    if (n < 0) { err(1, "listen()"); }

    // create struct to send to threads
    struct shared_data shared;
    pthread_mutex_init(&shared.sessions_mutex, 0);
    pthread_cond_init(&shared.dispatcher_cond, 0);
    pthread_cond_init(&shared.worker_cond, 0);
    shared.rflag = redundancy;

    // go through each file in directory and make a lock
    struct dirent *dp;
    DIR *pdir = opendir("./");
    while ((dp = readdir(pdir)) != NULL) {

        if (!strcmp(dp->d_name, ".") || !strcmp(dp->d_name, "..") ||
        	!strcmp(dp->d_name, ".copy1") || !strcmp(dp->d_name, ".copy2") ||
        	!strcmp(dp->d_name, ".copy3")) { continue; }
        else {
            char* file_name = dp->d_name;
            struct file filedata;
            filedata.filename = file_name;
            shared.files.push_back(filedata);
            pthread_mutex_init(&filedata.file_mutex, 0);
        }
    }
    closedir(pdir);

    /*char directory[SMALLBUF];
    for(int i = 1; i <= 3; i++) {

    	sprintf(directory, ".copy%d", i);
    	struct dirent *dpcopy;
		DIR *pdircopy = opendir(directory);

    	if(mkdir(directory, 0777) && errno == EEXIST) {

    		chdir(directory);
    		while((dpcopy = readdir(pdircopy)) != NULL) {
	  			unlink(dpcopy->d_name);
    		}
    		chdir("..");
    	}
    	closedir(pdircopy);

		for(int j = 0; j < (int)shared.files.size(); j++) {

			if(!strcmp(shared.files[j].filename, ".copy1") ||
				!strcmp(shared.files[j].filename, ".copy2") ||
				!strcmp(shared.files[j].filename, ".copy3") ||
				!strcmp(shared.files[j].filename, ".") ||
				!strcmp(shared.files[j].filename, "..")) { continue; }

			char path[SMALLBUF];
			sprintf(path, ".copy%d/%s", i, shared.files[j].filename);

			//printf("filename: %s\n", shared.files[j].filename);
			//printf("path: %s\n", path);

			int infd = open(shared.files[j].filename, O_RDONLY);
			if(infd < 0) { err(1, "infd - open()"); }
			int outfd = open(path, O_CREAT | O_WRONLY);
			if(outfd < 0) { err(1, "outfd - open()"); }
			
			char buf[SMALLBUF];
			while(read(infd, buf, sizeof(char))) {
				write(outfd, buf, sizeof(char));
				memset(&buf, 0, sizeof(buf));
			}

			close(infd);
			close(outfd);
	    }
	}*/

	// create pthreads
    pthread_t worker_tid[SMALLBUF]; 

    for (int i = 0; i < numthreads; ++i) {
        pthread_create(&worker_tid[i], 0, &worker, &shared);
    }

    // dispatcher
    while (1) {

        pthread_mutex_lock(&shared.sessions_mutex);
        while ((int)shared.sessions.size() >= numthreads) {
            pthread_cond_wait(&shared.dispatcher_cond, &shared.sessions_mutex);
        }
        pthread_mutex_unlock(&shared.sessions_mutex);

        char waiting[] = "\nWaiting for connection...\n\n";
		write(STDOUT_FILENO, waiting, strlen(waiting));
		int servaddr_length = sizeof(servaddr);
		int commfd = accept(sockfd, (struct sockaddr*)&servaddr, (socklen_t*)&servaddr_length);
		if(commfd < 0) {
			warn("accept()");
			continue;
		}

		struct session request;
        request.commfd = commfd;

        pthread_mutex_lock(&shared.sessions_mutex);
        shared.sessions.push(request);
        pthread_cond_signal(&shared.worker_cond);
        pthread_mutex_unlock(&shared.sessions_mutex);
    }

    sleep(10);
    pthread_mutex_destroy(&shared.sessions_mutex);
    pthread_cond_destroy(&shared.dispatcher_cond);
    pthread_cond_destroy(&shared.worker_cond);
    return 0;
}