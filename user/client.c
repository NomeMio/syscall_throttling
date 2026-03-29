

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

	

	char program_name[] = "a.out\0"; 
	if (ioctl(fd, sysThrot_IOC_REGISTER_PROGRAM, &program_name) < 0) {
		perror("Failed to send ioctl command");
		
	}
	char program_name2[] = "a2.out\0"; 
	if (ioctl(fd, sysThrot_IOC_REGISTER_PROGRAM, &program_name2) < 0) {
		perror("Failed to send ioctl command");
	
	}
	char program_name3[] = "test\0"; 
	if (ioctl(fd, sysThrot_IOC_REGISTER_PROGRAM, &program_name3) < 0) {
		perror("Failed to send ioctl command");

	}

int user_id = 2; // Example user ID
	if (ioctl(fd, sysThrot_IOC_REGISTER_SYSCALL, &user_id) < 0) {
		perror("Failed to send ioctl command");
		close(fd);
		return EXIT_FAILURE;
	}
close(fd);

	printf("Ioctl command sent successfully for registering ID %d\n", user_id);

   fd = open("/dev/sysThrot_dev", O_RDWR);
	if (fd < 0) {
		perror("Failed to open device");
		return EXIT_FAILURE;
	}
	if (ioctl(fd, sysThrot_IOC_DEREGISTER_SYSCALL, &user_id) < 0) {
		perror("Failed to send ioctl command");
		close(fd);
		return EXIT_FAILURE;
	}
	printf("Ioctl command sent successfully for deriegister ID %d\n", user_id);



	close(fd);

	
	fd = open("/dev/sysThrot_dev", O_RDWR);
	if (fd < 0) {
		perror("Failed to open device");
		return EXIT_FAILURE;
	}
	if (ioctl(fd, sysThrot_IOC_DEREGISTER_SYSCALL, &user_id) < 0) {
		perror("Failed to send ioctl command");
		close(fd);
		return EXIT_FAILURE;
	}
	printf("Ioctl command sent successfully for deriegister ID %d\n", user_id);



	close(fd);

	return 0;
	}

