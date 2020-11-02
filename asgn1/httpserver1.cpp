#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <netdb.h>
#include <string.h>
#include <err.h>
#include <stdio.h>
#include <ctype.h>

#define SIZE 100
#define BAD_REQUEST 0
#define GET 1
#define PUT 2

/*
  getaddr returns the numerical representation of the address
  identified by *name* as required for an IPv4 address represented
  in a struct sockaddr_in.
 */

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

int main(int argc, char *argv[]) {

    struct sockaddr_in servaddr;
    int listen_fd, n;
    int servaddr_length;

    if (argc != 3) {
        printf("USAGE: ./httpserver <address> <port-number>");
    }

    struct stat st;

    // init socket
    listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) err(1, "socket()");


    // init address
    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = getaddr(argv[1]);
    servaddr.sin_port = htons(atoi(argv[2]));
  
    n = bind(listen_fd, (struct sockaddr*) &servaddr, sizeof(servaddr));
    if(n < 0) err(1, "bind()");

    n = listen(listen_fd, 500);
    if(n < 0) err(1, "listen()");

  while(1) {
    char waiting[] = "waiting for connection\n";
    write(STDOUT_FILENO, waiting, strlen(waiting));
    int comm_fd = accept(listen_fd, NULL, NULL);
    if (comm_fd < 0) {
      warn("accept()");
      continue;
    }

    char buf[SIZE];
    char header[10000];
    char header2[10000];
    char body[10000];
    char response[10000];

    char *request;
    char *fileName;

    char* end_of_header;

	while (recv(comm_fd, buf, sizeof(char), 0) > 0) {
		write(STDOUT_FILENO, buf, sizeof(char));
        strcat(header, buf);
        end_of_header = strstr(header, "\r\n\r\n");
        if (end_of_header != NULL) { break; }
		memset(&buf, 0, sizeof(buf));
    }

    // get request type
    strcpy(header2, header);
    request = strtok(header2, " ");
    printf("%s\n", request);

    // get file name
    fileName = strtok(NULL, " ");
    fileName += 1;
    printf("%s\n", fileName);

    // check if file name is 10 characters
    int fileLength = strlen(fileName);
    printf("%d\n", fileLength);
    if (fileLength != 10) {
        // 400 Bad Request
        printf("%s\n", "File length is not 10");
        goto sendResponse;
    }
    // check if file name is alphanumeric
    char *i;
    for (i = fileName; *i != '\0'; i++) {
        if (isalnum(*i) == 0) {
            // 400 Bad Request
            printf("%s\n", "File name is not alphanumeric");
            goto sendResponse;
        }
    }

    // check for HTTP/1.1
    char *ret;
    ret = strstr(header, "HTTP/1.1");
    if (ret == NULL) {
        // 400 Bad Request
        printf("%s\n", "HTTP/1.1 not found");
        goto sendResponse;
    }

    int fileSize;
    if (strcmp(request, "GET") == 0) {
        if (access(fileName, F_OK) != -1) {
            printf("%s\n", "The file exists");
            if (access(fileName, R_OK) != -1) {
                // 403 Forbidden
                printf("Read permissions not granted");
                goto sendResponse;
            }

            // read file and write to response;
            stat(fileName, &st);
            fileSize = st.st_size;
            printf("%d\n", fileSize);
        }
        else {
            printf("%s\n", "The file doesn't exist");
            goto sendResponse;
        }
    }


    // only for PUT
    // while (recv(comm_fd, buf, sizeof(char), 0) > 0) {
    //     write(STDOUT_FILENO, buf, sizeof(char));
    //     strcat(body, buf);
    //     memset(&buf, 0, sizeof(buf));
    // }



    sendResponse:
        strcpy(response, "HTTP/1.1 200 OK\r\nContent-Length: 7\r\n\r\nhello\r\n");
        send(comm_fd, response, strlen(response), 0);


    memset(&header, 0, sizeof(header));
    //free(request);
    close(comm_fd);
  }
}
