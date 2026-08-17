

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

#define N 30



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
	for(int i=0; i<N; i++){
		pthread_join(threads[i], NULL);
	}
}




int main(int argc, char** argv){

	get_pid();
	double_open_test();
	worker_test();

	return 0;
}
