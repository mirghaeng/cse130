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

#define DEFAULT_THREADS 4

struct file_data {
    char* filename;
    pthread_mutex_t file_mutex;
};

struct sessions {
    int sockfd, commfd;
	struct sockaddr_in servaddr;
	pthread_t thread;
};

struct shared_data {
    pthread_mutex_t global_mutex;
    pthread_cond_t dispatcher_cond, worker_cond;
    int redundancy;
    std::vector<file_data> fdata;
    std::queue<sessions> session_queue;
};

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

void* dispatcher(void* data) {
    // struct

    // while (1)
        // lock mutex
        // while (queue is full)
            // wait
        // accept connection
        // put connection in queue
        // condition signal
        // unlock mutex
}

void* worker(void* data) {
    // struct

    // while(1)
        // while (queue is empty)
            // wait
        
        // parse header
        // check errors

        // lock mutex
        // get
        // put
        // condition signal
        // unlock mutex
}

int main(int argc, char* argv[]) {

    // parse command line using getopt()
    int opt, num_workers, redundancy, workers_given;

    while (opt = getopt(argc, argv, "N:r") != -1) {
        switch (opt) {
            case 'N':
                num_workers = atoi(optarg);
                workers_given = 1;
                printf("Option: %c\nNumber of threads: %d\n", opt, num_workers);
                break;
            case 'r':
                redundancy = 1;
                printf("Option: %c\n", opt);
                break;
        }
    }

    if (workers_given != 1) {
        // set to default: 4 threads
        num_workers = DEFAULT_THREADS;
    }

    // bind sockets
    struct sockaddr_in servaddr;
    int listenfd, n, port, servaddr_length;

    // init socket
    listenfd = socket(AF_INET, SOCK_STREAM, 0);
    if (listenfd < 0) { err(1, "socket()"); }

    // init address
    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = getaddr(argv[optind]);
    char *portString = argv[optind+1];
    int port;
    if (portString == NULL) {
        port = 80;
    }
    else {
        port = htons(*portString);
    }
    servaddr.sin_port = htons(port);

    // socket bind
    int n;
    n = bind(listenfd, (struct sockaddr*)&servaddr, sizeof(servaddr));
    if (n < 0) { err(1, "bind()"); }

    // socket listen
    n = listen(listenfd, 10);
    if (n < 0) { err(1, "listen()"); }

    // create mutexes for files
    struct shared_data common_data;
    pthread_t dispatch_tid[1], worker_tid[num_workers];


    // create pthreads
    for (int i = 0; i < num_workers; i++) {
        pthread_create(&worker_tid[i], NULL, &worker, &common_data);
    }
    pthread_create(&dispatch_tid[0], NULL, &dispatcher, &common_data);
}