

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
	for(int i=0; i<N; i++){
		pthread_join(threads[i], NULL);
	}
}




#define MAX_USERS 256

int main(int argc, char** argv){

	get_pid();
	double_open_test();	
	worker_test();

	int users[MAX_USERS];
	int count = get_registered_users(users, MAX_USERS);
	if (count < 0) {
		fprintf(stderr, "Failed to get registered users\n");
	} else {
		printf("Registered users (%d):\n", count);
		for (int i = 0; i < count; i++)
			printf("  UID: %d\n", users[i]);
	}

	return 0;
}
