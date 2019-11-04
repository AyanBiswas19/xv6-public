#include "types.h"
#include "stat.h"
#include "user.h"

int main(int argc, char *argv[]){
	if(argc < 2){
		printf(1,"\nInvalid Syntax\n");
		exit();
	}
	int pid;
	switch(pid=fork()){
		case -1:printf(1,"fork failed");
				break;
		case 0:exec(argv[1],argv+1);
			   exit();
		default:break;
	}
	int rtime,wtime;
	if(waitx(&wtime,&rtime)==0)
		printf(1,"\nWaitx failed");
	else
		printf(1,"\nRun time = %d, Wait time = %d\n", rtime, wtime);
	exit();
}