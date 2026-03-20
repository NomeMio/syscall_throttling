

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/fcntl.h>
#include "./lib/sysThrot.h"


int main(int argc, char** argv){
       
    int fd = open("/dev/sysThrot_dev", O_RDWR);
	if (fd < 0) {
		perror("Failed to open device");
		return EXIT_FAILURE;
	}

	int user_id = 2; // Example user ID
	if (ioctl(fd, sysThrot_IOC_REGISTER_SYSCALL, &user_id) < 0) {
		perror("Failed to send ioctl command");
		close(fd);
		return EXIT_FAILURE;
	}

	printf("Ioctl command sent successfully for user ID %d\n", user_id);
	close(fd);
	return 0;
}

