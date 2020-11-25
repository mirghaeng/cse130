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

struct file_data {
    char* filename;
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

void* dispatcher(void* data) {
    // struct
    struct shared_data* shared = (struct shared_data*) data;
    int sockfd = shared->sockfd;

    while (1) {

        pthread_mutex_lock(&shared->global_mutex);
        while (shared->session_queue.size() >= shared->numthreads) {
            pthread_cond_wait(&shared->dispatcher_cond, &shared->global_mutex);
        }

        char waiting[] = "\nWaiting for connection...\n";
		write(STDOUT_FILENO, waiting, strlen(waiting));
		int servaddr_length = sizeof(shared->servaddr);
		int commfd = accept(sockfd, (struct sockaddr*)&(shared->servaddr), (socklen_t*)&servaddr_length);
		if(commfd < 0) {
			warn("accept()");
			continue;
		}

        struct sessions request;
        request.commfd = commfd;
        shared->session_queue.push(request);
        pthread_cond_signal(&shared->worker_cond);
        pthread_mutex_unlock(&shared->global_mutex);
    }
}

void* worker(void* data) {
    // struct
    struct shared_data* shared = (struct shared_data*) data;

    while(1) {

        // lock global mutex to get data
        pthread_mutex_lock(&shared->global_mutex);
        while (shared->session_queue.empty()) {
            pthread_cond_wait(&shared->worker_cond, &shared->global_mutex);
        }
        int local_commfd =  shared->session_queue.front().commfd;
        shared->session_queue.pop();
        pthread_cond_signal(&shared->dispatcher_cond);
        pthread_mutex_unlock(&shared->global_mutex);
        
        // parse header
        // check errors

        // lock mutex
        // get
        // put
        // condition signal
        // unlock mutex
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

    // testing
    //printf("The address is: %s\nThe port is: %d\nThreads: %d\n", argv[optind], port, num_workers);

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
    common_data.servaddr = servaddr;

    // go through each file in directory and make a lock
    struct dirent *dp;
    DIR *pdir;
    pdir = opendir("./");
    while ((dp = readdir(pdir)) != NULL) {

        if (!strcmp(dp->d_name, ".") || !strcmp(dp->d_name, "..")) {
            // do nothing
        }
        else {
            char* file_name = dp->d_name;
            //printf("File: %s\n", file_name);

            struct file_data filedata;
            filedata.filename = file_name;
            pthread_mutex_init(&filedata.file_mutex, NULL);
            common_data.fdata.push_back(filedata);
        }

    }
    printf("# of files: %lu\n", common_data.fdata.size());

    // create pthreads
    pthread_t dispatch_tid[SIZE], worker_tid[SIZE];

    pthread_create(&dispatch_tid[0], NULL, &dispatcher, &common_data);
    for (int i = 0; i < num_workers; ++i) {
        pthread_create(&worker_tid[i], NULL, &worker, &common_data);
    }

    sleep(10);
    pthread_mutex_destroy(&common_data.global_mutex);
    pthread_cond_destroy(&common_data.dispatcher_cond);
    pthread_cond_destroy(&common_data.worker_cond);
    return 0;
}