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
#include <fcntl.h>

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
    char fileContent[10000];

    //char contentLength[100];
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
    //printf("%s\n", request);

    // get file name
    fileName = strtok(NULL, " ");
    fileName += 1;
    //printf("%s\n", fileName);

    // check if file name is 10 characters
    int fileLength = strlen(fileName);
    //printf("%d\n", fileLength);
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

    // GET requests
    int fileSize;
    int fd;
    if (strcmp(request, "GET") == 0) {
        if (access(fileName, F_OK) != -1) {
            printf("%s\n", "The file exists");

            if (access(fileName, R_OK) == -1) {
                // 403 Forbidden
                printf("Read permissions not granted");
                goto sendResponse;
            }
            
            stat(fileName, &st);
            fileSize = st.st_size;
            //printf("%d\n", fileSize);

            // read the file
            fd = open(fileName, O_RDONLY);
            while (read(fd, buf, 1)) {
                strcat(fileContent, buf);
                memset(&buf, 0, sizeof(buf));

            }

            // send response back
            sprintf(response, "HTTP/1.1 200 OK\r\nContent-Length: %d\r\n\r\n", fileSize);
            strcat(response, fileContent);
            send(comm_fd, response, strlen(response), 0);
            close(fd);
            goto sentResponseAlready;

        }
        else {
            // 404 File Not Found
            printf("%s\n", "The file doesn't exist");
            goto sendResponse;
        }
    }


    // PUT requests
    int put_fd;
    if (strcmp(request, "PUT") == 0) {
        // get content length and body
        char* ret2;
        int contentLength, j;
        ret2 = strstr(header, "Content-Length");

        if (ret2 != NULL) {
            sscanf(ret2, "Content-Length: %d", &contentLength);
            j = contentLength;

            // get body if content length is provided
            memset(&buf, 0, sizeof(buf));
            while (j > 0) {
                int n = recv(comm_fd, buf, sizeof(char), 0);
                write(STDOUT_FILENO, buf, sizeof(char));
                strcat(body, buf);
		        memset(&buf, 0, sizeof(buf));
                j--;
            }
        }
        else {
            // get body if no content length provided
            while (recv(comm_fd, buf, sizeof(char), 0) > 0) {
                write(STDOUT_FILENO, buf, sizeof(char));
                strcat(body, buf);
		        memset(&buf, 0, sizeof(buf));
            }
        }

        //printf("%s\n", body);
        // read body and write to fileName
        put_fd = open(fileName, O_CREAT | O_WRONLY | O_TRUNC);

        int bodyLength = strlen(body);
        //printf("Body length %d\n", bodyLength);
        write(put_fd, body, bodyLength);

        sprintf(response, "HTTP/1.1 201 Created\r\nContent-Length: 0\r\n\r\n");
        send(comm_fd, response, strlen(response), 0);
        close(put_fd);
        goto sentResponseAlready;
    }

    sendResponse:
        strcpy(response, "HTTP/1.1 200 OK\r\nContent-Length: 7\r\n\r\nhello\r\n");
        send(comm_fd, response, strlen(response), 0);

    sentResponseAlready:

    memset(&header, 0, sizeof(header));
    memset(&response, 0, sizeof(response));
    close(comm_fd);
  }
}
