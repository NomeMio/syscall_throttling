

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/fcntl.h>
#include "./lib/sysThrot.h"
#include <time.h>



#define DEVICE_PATH "/dev/sysThrot_dev"

int register_syscall(int syscall_id) {
	int fd = open(DEVICE_PATH, O_RDWR);
	if (fd < 0) {
		perror("Failed to open device");
		return -1;
	}

	if (ioctl(fd, sysThrot_IOC_REGISTER_SYSCALL, &syscall_id) < 0) {
		perror("Failed to send ioctl command");
		close(fd);
		return -1;
	}

	printf("Ioctl command sent successfully for registering syscall ID %d\n", syscall_id);
	close(fd);
	return 0;
}

int deregister_syscall(int syscall_id) {
	int fd = open(DEVICE_PATH, O_RDWR);
	if (fd < 0) {
		perror("Failed to open device");
		return -1;
	}

	if (ioctl(fd, sysThrot_IOC_DEREGISTER_SYSCALL, &syscall_id) < 0) {
		perror("Failed to send ioctl command");
		close(fd);
		return -1;
	}

	printf("Ioctl command sent successfully for deregistering syscall ID %d\n", syscall_id);
	close(fd);
	return 0;
}


int turn_off_monitor() {
	int fd = open(DEVICE_PATH, O_RDWR);
	if (fd < 0) {
		perror("Failed to open device");
		return -1;
	}

	if (ioctl(fd, sysThrot_IOC_TURN_OFF) < 0) {
		perror("Failed to send ioctl command");
		close(fd);
		return -1;
	}

	printf("Ioctl command sent successfully for turning off throttling\n");
	close(fd);
	return 0;
}
int turn_on_monitor() {
	int fd = open(DEVICE_PATH, O_RDWR);
	if (fd < 0) {
		perror("Failed to open device");
		return -1;
	}

	if (ioctl(fd, sysThrot_IOC_TURN_ON) < 0) {
		perror("Failed to send ioctl command");
		close(fd);
		return -1;
	}

	printf("Ioctl command sent successfully for turning on throttling\n");
	close(fd);
	return 0;
}

int register_program(const char* program_name) {
	int fd = open(DEVICE_PATH, O_RDWR);
	if (fd < 0) {
		perror("Failed to open device");
		return -1;
	}

	if (ioctl(fd, sysThrot_IOC_REGISTER_PROGRAM, program_name) < 0) {
		perror("Failed to send ioctl command");
		close(fd);
		return -1;
	}

	printf("Ioctl command sent successfully for registering program %s\n", program_name);
	close(fd);
	return 0;
}

#define GET_PID 39
#define OPEN 2

void get_pid() {
	pid_t pid = getpid();
	printf("Current PID: %d\n", pid);
}

int main(int argc, char** argv){
       

	

	char program_name[] = "a.out\0"; 
	char program_name1[] = "a22.out\0"; 
	char program_name2[] = "a33.out\0";


	register_program(program_name);
	register_program(program_name1);
	register_program(program_name2);
	int pid = getpid();
	get_pid();
	register_syscall(GET_PID); 
	for(int i=0;i<16;i++){
		get_pid();
	}

	deregister_syscall(GET_PID);
	
	return 0;
	
	}

