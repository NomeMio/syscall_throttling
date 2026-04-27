

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include "./lib/sysThrot.h"
#include <time.h>
#include <pthread.h>












int main(int argc, char** argv){
       

	


	char syscall_name[] = "__x64_sys_getpid\0";

	deregister_syscall(syscall_name);
	deregister_syscall(syscall_name);

	/*
	
	for(int i=0; i< SUPPORTED_SYSCALLS; i++){
		register_syscall(syscall_symbols[i]);
		deregister_syscall(syscall_symbols[i]);
	}

	*/


	return 0;
	
	}