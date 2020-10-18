#include<fcntl.h>
#include<unistd.h>
#include<err.h>
#include<string.h>

#define BUFFER_SIZE 30000

void print(int in, int out, char* buffer) {
	while(read(in, buffer, 1)) {
		write(out, buffer, 1);
	}
	memset(buffer, 0, BUFFER_SIZE);
}

int main(int argc, char* argv[]) {
	
	int i, fd;
	char buffer[BUFFER_SIZE];

	// if no args
	if(argc < 2) {
		print(STDIN_FILENO, STDOUT_FILENO, buffer);
	}

	for(i = 1; i < argc; i++) {

		// check if -
		if(strcmp(argv[i], "-") == 0) {
			print(STDIN_FILENO, STDOUT_FILENO, buffer);
			continue;
		}

		// open file
		fd = open(argv[i], O_RDONLY);

		// check if file exists
		if(fd < 0) {
			warn("%s", argv[i]);
			continue;
		}

		// write file content to stdout
		print(fd, STDOUT_FILENO, buffer);

		close(fd);
	}

	return 0;
}
