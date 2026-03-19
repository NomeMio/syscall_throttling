#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

#define CODE 333 


int main(int argc, char** argv){
       
       	syscall(1,0,0,1);		
	return 0;
}

