

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





void get_pid() {
	pid_t pid = getpid();
	printf("Current PID: %d\n", pid);
}

#define N 50



void* worker_job(void *arg) {
	int pid = getpid();
	return NULL;
}	

void worker_test(){
	pthread_t threads[N];
	for(int i=0; i<N; i++){
		if(pthread_create(&threads[i], NULL, worker_job, NULL) != 0){
			perror("Failed to create thread");
			exit(EXIT_FAILURE);
		}
	}
	//sleep(2);
	//turn_off_monitor();
	for(int i=0; i<N; i++){
		pthread_join(threads[i], NULL);
	}
}




int main(int argc, char** argv){
       

	

	char program_name[] = "a.out\0"; 
	char program_name1[] = "client\0"; 
	char program_name2[] = "a33.out\0";
	char syscall_name[] = "__x64_sys_getpid\0";

	register_program(program_name);
	register_program(program_name1);
	register_program(program_name2);
	int pid = getpid();
	get_pid();
	register_syscall(syscall_name); 
	register_syscall(syscall_name); 


	worker_test();
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

