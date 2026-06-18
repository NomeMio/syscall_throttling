

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
       

	
	int user=1000;

	char program_name[] = "a.out\0"; 
	char program_name1[] = "client\0"; 
	char program_name2[] = "a33.out\0";
	char syscall_name[] = "__x64_sys_getpid\0";
	register_user(user);

	//register_program(program_name);
	register_program(program_name1);
	//register_program(program_name2);

	register_syscall(syscall_name); 





	return 0;
	
	}
