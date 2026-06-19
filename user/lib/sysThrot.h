#ifndef USER_SYS_THROT_H
#define USER_SYS_THROT_H

#include "./syscalls.h"
#define sysThrot_IOC_MAGIC '-'


static const char *syscall_symbols[];
#define TYPE_OF_DATA_PASSED_TO_IOCTL_USER int*
#define TYPE_OF_DATA_PASSED_TO_IOCTL_PROGRAM char *
#define TYPE_OF_DATA_PASSED_TO_IOCTL_SYSCALL int *


#define sysThrot_IOC_REGISTER_USER _IOW(sysThrot_IOC_MAGIC, 1, TYPE_OF_DATA_PASSED_TO_IOCTL_USER)
#define sysThrot_IOC_DEREGISTER_USER _IOW(sysThrot_IOC_MAGIC, 2, TYPE_OF_DATA_PASSED_TO_IOCTL_USER)
#define sysThrot_IOC_REGISTER_PROGRAM _IOW(sysThrot_IOC_MAGIC, 3, TYPE_OF_DATA_PASSED_TO_IOCTL_PROGRAM)
#define sysThrot_IOC_DEREGISTER_PROGRAM _IOW(sysThrot_IOC_MAGIC, 4, TYPE_OF_DATA_PASSED_TO_IOCTL_PROGRAM)
#define sysThrot_IOC_REGISTER_SYSCALL _IOW(sysThrot_IOC_MAGIC, 5, TYPE_OF_DATA_PASSED_TO_IOCTL_SYSCALL)
#define sysThrot_IOC_DEREGISTER_SYSCALL _IOW(sysThrot_IOC_MAGIC, 6, TYPE_OF_DATA_PASSED_TO_IOCTL_SYSCALL)
#define sysThrot_IOC_TURN_ON _IO(sysThrot_IOC_MAGIC, 7)
#define sysThrot_IOC_TURN_OFF _IO(sysThrot_IOC_MAGIC, 8)

#define DEVICE_PATH "/dev/sysThrot_dev"


int find_syscall_id(const char* syscall_name) {
    for (int i = 0; i < SUPPORTED_SYSCALLS; i++) {
        if (strcmp(syscall_symbols[i], syscall_name) == 0) {
            return i;
        }
    }
    return -1; 
}


int register_syscall(char *syscall) {
    int syscall_id = find_syscall_id(syscall);
    if (syscall_id == -1) {
        fprintf(stderr, "Syscall %s not found in supported syscalls\n", syscall);
        return -1;
    }
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

int deregister_syscall(char *syscall) {
    int syscall_id = find_syscall_id(syscall);
    if (syscall_id == -1) {
        fprintf(stderr, "Syscall %s not found in supported syscalls\n", syscall);
        return -1;
    }
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

int register_user(int user_id) {
	int fd = open(DEVICE_PATH, O_RDWR);
	if (fd < 0) {
		perror("Failed to open device");
		return -1;
	}

	if (ioctl(fd, sysThrot_IOC_REGISTER_USER, &user_id) < 0) {
		perror("Failed to send ioctl command");
		close(fd);
		return -1;
	}

	printf("Ioctl command sent successfully for registering user ID %d\n", user_id);
	close(fd);
	return 0;
}

int deregister_user(int user_id) {
	int fd = open(DEVICE_PATH, O_RDWR);
	if (fd < 0) {
		perror("Failed to open device");
		return -1;
	}

	if (ioctl(fd, sysThrot_IOC_DEREGISTER_USER, &user_id) < 0) {
		perror("Failed to send ioctl command");
		close(fd);
		return -1;
	}

	printf("Ioctl command sent successfully for deregistering user ID %d\n", user_id);
	close(fd);
	return 0;
}

int deregister_program(const char* program_name) {
	int fd = open(DEVICE_PATH, O_RDWR);
	if (fd < 0) {
		perror("Failed to open device");
		return -1;
	}

	if (ioctl(fd, sysThrot_IOC_DEREGISTER_PROGRAM, program_name) < 0) {
		perror("Failed to send ioctl command");
		close(fd);
		return -1;
	}

	printf("Ioctl command sent successfully for deregistering program %s\n", program_name);
	close(fd);
	return 0;
}

int double_open_test() {
	int fd1 = open(DEVICE_PATH, O_RDWR);
	if (fd1 < 0) {
		perror("Failed to open device the first time");
		return -1;
	}

	int fd2 = open(DEVICE_PATH, O_RDWR);
	if (fd2 < 0) {
		perror("Failed to open device the second time");
		close(fd1);
		return -1;
	}

	printf("Device opened successfully twice (fd1: %d, fd2: %d)\n", fd1, fd2);
	close(fd1);
	close(fd2);
	return 0;
}



#endif