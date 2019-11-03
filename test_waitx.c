#include "types.h"
#include "stat.h"
#include "user.h"

int main(){
	printf(1,"Starting testing\n");
	int pid;
	switch(pid=fork()){
		case -1:printf(1,"fork failed");
				break;
		case 0:for(int i=0; i< 100; i++)
			   	printf(1,"%d",i);
			   sleep(100);
		default:break;
	}
	int rtime,wtime;
	if(waitx(&wtime,&rtime)==0)
		printf(1,"\nWaitx failed");
	else
		printf(1,"Run time = %d, Wait time = %d\n", rtime, wtime);
	printf(1,"Testing done.\n");
	return 0;
}