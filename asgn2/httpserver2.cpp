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

#define DEFAULT_THREADS 4
#define SIZE 500

#define ERROR 1
#define NO_ERROR_YET 0

struct file_data {
    char filename[100];
    pthread_mutex_t file_mutex;
};

struct sessions {
    int commfd;
};

struct shared_data {
    pthread_mutex_t global_mutex;
    pthread_cond_t dispatcher_cond, worker_cond;
    int rflag;
    int sockfd;
    int numthreads;
    int currentrequests;
	int fileexists;
    struct sockaddr_in servaddr;
    std::vector<file_data> fdata;
    std::queue<sessions> session_queue;
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
	int errno = send(commfd, response, strlen(response), 0);
	if (errno < 0) { warn("send()"); }
}

void* worker(void* data) {
    // struct
    struct shared_data* shared = (struct shared_data*) data;
    struct stat st;

    while(1) {

        int commfd = 0;

        // lock global mutex to get data
        pthread_mutex_lock(&shared->global_mutex);
        while (shared->session_queue.empty()) {
            pthread_cond_wait(&shared->worker_cond, &shared->global_mutex);
        }
        commfd = shared->session_queue.front().commfd;
        shared->session_queue.pop();
        //pthread_cond_signal(&shared->dispatcher_cond);
        pthread_mutex_unlock(&shared->global_mutex);

        char buf[100];
		char header[5000];
		char headercpy[5000];
		char response[500];       // initialize to size of content + 10000 for the header
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


		struct file_data* filepointer;
		//int fileexists = 0;
		shared->fileexists = 0;
		int vectornum = 0;
		//pthread_mutex_lock(&shared->global_mutex);
		// find the file in the vector
        for (std::vector<file_data>::iterator i = shared->fdata.begin(); i != shared->fdata.end(); ++i) {
            if ((strcmp(filename, i->filename)) == 0) {
                filepointer = (struct file_data*) &shared->fdata[vectornum];
				shared->fileexists = 1;
            	break;
            }
            vectornum++;
        }
		pthread_mutex_lock(&shared->global_mutex);
		// if the file doesnt exist, create one and add it to the vector
		if (strcmp(type, "PUT") && (shared->fileexists == 0)) {

            struct file_data file_data;
			strcpy(file_data.filename, filename);
            pthread_mutex_init(&file_data.file_mutex, NULL);
            shared->fdata.push_back(file_data);

            filepointer = (struct file_data*) &shared->fdata.back();
			shared->fileexists = 1;
		}
		printf("file is %s\n", filepointer->filename);
		pthread_mutex_unlock(&shared->global_mutex);
        /********************************************************** GET WITHOUT REDUNDANCY **********************************************************/
        int getfd;	
		memset(&buf, 0, sizeof(buf));
		if((strcmp(type, "GET") == 0) && (errors == NO_ERROR_YET)) {
			// struct file_data* filepointer;
        	// int vectornum = 0;
        	// for (std::vector<file_data>::iterator i = shared->fdata.begin(); i != shared->fdata.end(); ++i) {
            // 	if ((strcmp(filename, i->filename)) == 0) {
            //     	filepointer = (struct file_data*) &shared->fdata[vectornum];
            //     	break;
            // 	}
            // 	vectornum++;
        	// }

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

                char *responseGet;
                responseGet = new char[500];

                pthread_mutex_lock(&filepointer->file_mutex);

				sendheader(commfd, responseGet, 200, filesize);

				// get file contents
				getfd = open(filename, O_RDONLY);
				while(read(getfd, buf, sizeof(char))) {
					send(commfd, buf, sizeof(char), 0);
					memset(&buf, 0, sizeof(buf));
				}

				close(getfd);
                pthread_mutex_unlock(&filepointer->file_mutex);

                delete[] responseGet;
			}
		}		


		/********************************************************** PUT WITHOUT REDUNDANCY **********************************************************/
		
		// handling PUT request
        int putfd, n;
		memset(&buf, 0, sizeof(buf));
		if((strcmp(type, "PUT") == 0) && (errors == NO_ERROR_YET) && (shared->rflag == 0)) {
            
			// get Content-Length
			char* ptrlength = strstr(header, "Content-Length:");
			//struct file_data* filepointer;

			if(ptrlength != NULL) {     // content length provided

				// get content if Content-Length is provided
				sscanf(ptrlength, "Content-Length: %d", &contentlength);

            	char *responsePut = new char[500];

                if(shared->fileexists == 0) {          // content length provided and file doesn't exist

                    pthread_mutex_lock(&shared->global_mutex);

                    struct file_data file_data;
					strcpy(file_data.filename, filename);
                    pthread_mutex_init(&file_data.file_mutex, NULL);
                    shared->fdata.push_back(file_data);

                    filepointer = (struct file_data*) &shared->fdata.back();
                    pthread_mutex_lock(&filepointer->file_mutex);

                    putfd = open(filename, O_CREAT | O_WRONLY | O_TRUNC, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);

				    for(int i = contentlength; i > 0; i--) {
					    n = recv(commfd, buf, sizeof(char), 0);
					    if(n < 0) { warn("recv()"); }
					    if(n == 0) { break; }
					    write(STDOUT_FILENO, buf, sizeof(char));
					    write(putfd, buf, sizeof(char));
					    memset(&buf, 0, sizeof(buf));
				    }

				    close(putfd);
				    sendheader(commfd, responsePut, 201, 0);
                    pthread_mutex_unlock(&filepointer->file_mutex);
                    pthread_mutex_unlock(&shared->global_mutex);

				    delete[] responsePut;
                }
                else {      // content length provided and file exists already

                    pthread_mutex_lock(&filepointer->file_mutex);

				    putfd = open(filename, O_CREAT | O_WRONLY | O_TRUNC, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);

				    for(int i = contentlength; i > 0; i--) {
					    n = recv(commfd, buf, sizeof(char), 0);
					    if(n < 0) { warn("recv()"); }
					    if(n == 0) { break; }
					    write(STDOUT_FILENO, buf, sizeof(char));
					    write(putfd, buf, sizeof(char));
					    memset(&buf, 0, sizeof(buf));
				    }

				    close(putfd);
				    sendheader(commfd, responsePut, 201, 0);
                    pthread_mutex_unlock(&filepointer->file_mutex);

				    delete[] responsePut;
                }

			} else {        // content length isn't provided

                char *responsePut = new char[20000];
                if (shared->fileexists == 0) {         // no content length and file doesn't exist

                    pthread_mutex_lock(&shared->global_mutex);

                    struct file_data file_data;
					strcpy(file_data.filename, filename);
                    pthread_mutex_init(&file_data.file_mutex, NULL);
                    shared->fdata.push_back(file_data);

                    filepointer = (struct file_data*) &shared->fdata.back();
                    pthread_mutex_lock(&filepointer->file_mutex);


				    putfd = open(filename, O_CREAT | O_WRONLY | O_TRUNC, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
				    sendheader(commfd, responsePut, 201, 0);

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

                    pthread_mutex_unlock(&filepointer->file_mutex);
                    pthread_mutex_unlock(&shared->global_mutex);

				    delete[] responsePut;
                }
                else {              // no content length and file exists already

                    pthread_mutex_lock(&filepointer->file_mutex);


				    putfd = open(filename, O_CREAT | O_WRONLY | O_TRUNC, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
				    sendheader(commfd, responsePut, 201, 0);

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

                    pthread_mutex_unlock(&filepointer->file_mutex);
                    delete[] responsePut;
                }

			}
		}

		/********************************************************** PUT WITH REDUNDANCY **********************************************************/

		printf("does it exist: %d\n", shared->fileexists);
		int putfdr[3] = { 0 };
		if((strcmp(type, "PUT") == 0) && (errors == NO_ERROR_YET) && (shared->rflag == 1)) {
            
			// get Content-Length
			char* ptrlength = strstr(header, "Content-Length:");
			//struct file_data* filepointer;

			if(ptrlength != NULL) {     // content length provided

				// get content if Content-Length is provided
				sscanf(ptrlength, "Content-Length: %d", &contentlength);

            	char *responsePut = new char[500];

                if(shared->fileexists == 0) {          // content length provided and file doesn't exist
					printf("file doesnt exist\n");

                    pthread_mutex_lock(&shared->global_mutex);

                    struct file_data file_data;
					strcpy(file_data.filename, filename);
                    pthread_mutex_init(&file_data.file_mutex, NULL);
                    shared->fdata.push_back(file_data);

                    filepointer = (struct file_data*) &shared->fdata.back();
                    pthread_mutex_lock(&filepointer->file_mutex);

                    char path[100];
					for(int d = 0; d < 3; d++) {
						sprintf(path, "copy%d/%s", d+1, filename);

						// create / overwrite new file in directory
						putfdr[d] = open(path, O_CREAT | O_WRONLY | O_TRUNC, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
					}

				    for(int i = contentlength; i > 0; i--) {
					    n = recv(commfd, buf, sizeof(char), 0);
					    if(n < 0) { warn("recv()"); }
					    if(n == 0) { break; }
					    write(STDOUT_FILENO, buf, sizeof(char));
					    for(int j = 0; j < 3; j++) {
							n = write(putfdr[j], buf, sizeof(char));
							if(n < 0) { err(1, "write()"); }
						}
					    memset(&buf, 0, sizeof(buf));
				    }

				    close(putfdr[0]);
					close(putfdr[1]);
					close(putfdr[2]);
				    sendheader(commfd, responsePut, 201, 0);
                    pthread_mutex_unlock(&filepointer->file_mutex);
                    pthread_mutex_unlock(&shared->global_mutex);

				    delete[] responsePut;
                }
                else {      // content length provided and file exists already

                    pthread_mutex_lock(&filepointer->file_mutex);

					char path[100];
					for(int d = 0; d < 3; d++) {
						sprintf(path, "copy%d/%s", d+1, filename);
						// create / overwrite new file in directory
						putfdr[d] = open(path, O_CREAT | O_WRONLY | O_TRUNC, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
					}

				    for(int i = contentlength; i > 0; i--) {
					    n = recv(commfd, buf, sizeof(char), 0);
					    if(n < 0) { warn("recv()"); }
					    if(n == 0) { break; }
					    write(STDOUT_FILENO, buf, sizeof(char));
					    for(int j = 0; j < 3; j++) {
							n = write(putfdr[j], buf, sizeof(char));
							if(n < 0) { err(1, "write()"); }
						}
					    memset(&buf, 0, sizeof(buf));
				    }

				    close(putfdr[0]);
					close(putfdr[1]);
					close(putfdr[2]);
				    sendheader(commfd, responsePut, 201, 0);
                    pthread_mutex_unlock(&filepointer->file_mutex);

				    delete[] responsePut;
                }

			} 
			else {        // content length isn't provided

                char *responsePut = new char[500];
                if (shared->fileexists == 0) {         // no content length and file doesn't exist

                    pthread_mutex_lock(&shared->global_mutex);

                    struct file_data file_data;
					strcpy(file_data.filename, filename);
                    pthread_mutex_init(&file_data.file_mutex, NULL);
                    shared->fdata.push_back(file_data);

                    filepointer = (struct file_data*) &shared->fdata.back();
                    pthread_mutex_lock(&filepointer->file_mutex);


				    char path[100];
					for(int d = 0; d < 3; d++) {
						sprintf(path, "copy%d/%s", d+1, filename);

						// create / overwrite new file in directory
						putfdr[d] = open(path, O_CREAT | O_WRONLY | O_TRUNC, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
					}

				    sendheader(commfd, responsePut, 201, 0);

				    while (1) {
					    int amtRcv = recv(commfd, buf, sizeof(char), 0);
					    if (amtRcv == 0) {
						    break;
					    }
					    write(STDOUT_FILENO, buf, sizeof(char));
					    for(int j = 0; j < 3; j++) {
							n = write(putfdr[j], buf, sizeof(char));
							if(n < 0) { err(1, "write()"); }
						}
					    memset(&buf, 0, sizeof(buf));
				    }

				    close(putfdr[0]);
					close(putfdr[1]);
					close(putfdr[2]);

                    pthread_mutex_unlock(&filepointer->file_mutex);
                    pthread_mutex_unlock(&shared->global_mutex);

				    delete[] responsePut;
                }
                else {              // no content length and file exists already

                    pthread_mutex_lock(&filepointer->file_mutex);


				    char path[100];
					for(int d = 0; d < 3; d++) {
						sprintf(path, "copy%d/%s", d+1, filename);

						// create / overwrite new file in directory
						putfdr[d] = open(path, O_CREAT | O_WRONLY | O_TRUNC, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
					}
				    sendheader(commfd, responsePut, 201, 0);

				    while (1) {
					    int amtRcv = recv(commfd, buf, sizeof(char), 0);
					    if (amtRcv == 0) {
						    break;
					    }
					    write(STDOUT_FILENO, buf, sizeof(char));
					    for(int j = 0; j < 3; j++) {
							n = write(putfdr[j], buf, sizeof(char));
							if(n < 0) { err(1, "write()"); }
						}
					    memset(&buf, 0, sizeof(buf));
				    }

				    close(putfdr[0]);
					close(putfdr[1]);
					close(putfdr[2]);

                    pthread_mutex_unlock(&filepointer->file_mutex);
                    delete[] responsePut;
                }

			}
		}

		// clear buffers
		memset(&header, 0, sizeof(header));
		memset(&headercpy, 0, sizeof(headercpy));
		memset(&response, 0, sizeof(response));
		
        close(commfd);

        pthread_mutex_lock(&shared->global_mutex);
        shared->currentrequests--;
        pthread_cond_signal(&shared->dispatcher_cond);
        pthread_mutex_unlock(&shared->global_mutex);

    }
}

int main(int argc, char* argv[]) {

    // parse command line using getopt()
    int opt, num_workers, redundancy;
    redundancy = 0;
    num_workers = DEFAULT_THREADS;

    while ((opt = getopt(argc, argv, "N:r")) != -1) {
        switch (opt) {
            case 'N':
                num_workers = atoi(optarg);
                break;
            case 'r':
                redundancy = 1;
                break;
        }
    }

    // bind sockets
    struct sockaddr_in servaddr;
    int listenfd, n, port;

    // init socket
    listenfd = socket(AF_INET, SOCK_STREAM, 0);
    if (listenfd < 0) { err(1, "socket()"); }

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
    n = bind(listenfd, (struct sockaddr*)&servaddr, sizeof(servaddr));
    if (n < 0) { err(1, "bind()"); }

    // socket listen
    n = listen(listenfd, 10);
    if (n < 0) { err(1, "listen()"); }

    // create struct to send to threads
    struct shared_data common_data;
    pthread_mutex_init(&common_data.global_mutex, NULL);
    pthread_cond_init(&common_data.dispatcher_cond, NULL);
    pthread_cond_init(&common_data.worker_cond, NULL);
    common_data.rflag = redundancy;
    common_data.sockfd = listenfd;
    common_data.numthreads = num_workers;
    common_data.currentrequests = 0;
    common_data.servaddr = servaddr;

    // go through each file in directory and make a lock
    struct dirent *dp;
    DIR *pdir = opendir("./");
	
	if (redundancy == 0) {
    	while ((dp = readdir(pdir)) != NULL) {

        	if (!strcmp(dp->d_name, ".") || !strcmp(dp->d_name, "..") ||
        		!strcmp(dp->d_name, "copy1") || !strcmp(dp->d_name, "copy2") ||
        		!strcmp(dp->d_name, "copy3") || !strcmp(dp->d_name, "client")) { }
        	else {
            	char file_name[100];
				strcpy(file_name, dp->d_name);
            	struct file_data filedata;
				strcpy(filedata.filename, file_name);
            	pthread_mutex_init(&filedata.file_mutex, NULL);
				common_data.fdata.push_back(filedata);
        	}
    	}

	}
	else if (redundancy == 1) {
		pdir = opendir("copy1");
        while ((dp = readdir(pdir)) != NULL) {

            if (!strcmp(dp->d_name, ".") || !strcmp(dp->d_name, "..") ||
        		!strcmp(dp->d_name, "copy1") || !strcmp(dp->d_name, "copy2") ||
        		!strcmp(dp->d_name, "copy3") || !strcmp(dp->d_name, "client")) {
                // do nothing
            }
            else {
                char* file_name = dp->d_name;

                struct file_data filedata;
				strcpy(filedata.filename, file_name);
                pthread_mutex_init(&filedata.file_mutex, NULL);
                common_data.fdata.push_back(filedata);
                printf("copy1: %s\n", file_name);
            }
        }

        pdir = opendir("copy2");
        while ((dp = readdir(pdir)) != NULL) {

            if (!strcmp(dp->d_name, ".") || !strcmp(dp->d_name, "..") ||
        		!strcmp(dp->d_name, "copy1") || !strcmp(dp->d_name, "copy2") ||
        		!strcmp(dp->d_name, "copy3") || !strcmp(dp->d_name, "client")) {    // if is it . or .. files
                // do nothing
            }
            else {      // actual files
                char* file_name = dp->d_name;

                int alreadyexists = 0;
                for (std::vector<file_data>::iterator i = common_data.fdata.begin(); i != common_data.fdata.end(); ++i) {
                    if ((strcmp(file_name, i->filename)) == 0) {        // file already has a lock
                        alreadyexists = 1;
                        break;
                    }
                }
                if (alreadyexists == 1) {
                    // do nothing
                }
                else {
                    struct file_data filedata;
                    strcpy(filedata.filename, file_name);
                    pthread_mutex_init(&filedata.file_mutex, NULL);
                    common_data.fdata.push_back(filedata);
                    printf("copy2: %s\n", file_name);
                }
            }
        }

        pdir = opendir("copy3");
        while ((dp = readdir(pdir)) != NULL) {

            if (!strcmp(dp->d_name, ".") || !strcmp(dp->d_name, "..") ||
        		!strcmp(dp->d_name, "copy1") || !strcmp(dp->d_name, "copy2") ||
        		!strcmp(dp->d_name, "copy3") || !strcmp(dp->d_name, "client")) {    // if is it . or .. files
                // do nothing
            }
            else {      // actual files
                char* file_name = dp->d_name;

                int alreadyexists = 0;
                for (std::vector<file_data>::iterator i = common_data.fdata.begin(); i != common_data.fdata.end(); ++i) {
                    if ((strcmp(file_name, i->filename)) == 0) {        // file already has a lock
                        alreadyexists = 1;
                        break;
                    }
                }
                if (alreadyexists == 1) {
                    // do nothing
                }
                else {
                    struct file_data filedata;
                    strcpy(filedata.filename, file_name);
                    pthread_mutex_init(&filedata.file_mutex, NULL);
                    common_data.fdata.push_back(filedata);
                    printf("copy3: %s\n", file_name);
                }
            }
        }

	}
    closedir(pdir);

    // create pthreads
    pthread_t worker_tid[SIZE];

    for (int i = 0; i < num_workers; ++i) {
        pthread_create(&worker_tid[i], NULL, &worker, &common_data);
    }

    // dispatcher thread
    //struct shared_data* shared = (struct shared_data*) &common_data;
    //int sockfd = shared->sockfd;

    while (1) {

        pthread_mutex_lock(&common_data.global_mutex);
        while (common_data.currentrequests >= common_data.numthreads) {
            pthread_cond_wait(&common_data.dispatcher_cond, &common_data.global_mutex);
        }
        pthread_mutex_unlock(&common_data.global_mutex);

        char waiting[] = "\nWaiting for connection...\n";
		write(STDOUT_FILENO, waiting, strlen(waiting));
		int servaddr_length = sizeof(servaddr);
		int commfd = accept(listenfd, (struct sockaddr*)&(servaddr), (socklen_t*)&servaddr_length);
		if(commfd < 0) {
			warn("accept()");
			continue;
		}
		struct sessions request;
        request.commfd = commfd;
        pthread_mutex_lock(&common_data.global_mutex);
        common_data.session_queue.push(request);
        common_data.currentrequests++;

        pthread_cond_signal(&common_data.worker_cond);
        pthread_mutex_unlock(&common_data.global_mutex);
    }

}