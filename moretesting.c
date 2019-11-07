#include "types.h"
#include "stat.h"
#include "user.h"

#define NFORK 50

int main(){
	int pid, wtime, rtime;
	printf(1,"Commencing Test:\n");
	volatile int x, y=651;
	for(int i=0; i < NFORK; i++){
		switch(pid=fork()){
			case -1:printf(1,"Unable to fork. i=%d\n", i);
			break;
			case 0: for(int j=0; j < 1000000; j++){
						y+=x*x%y;
					}
					exit();
			default:
				break;
		}
	}
	for(int i=0; i < NFORK ; i++){		
		if(waitx(&wtime,&rtime)==0)
			printf(1,"\nWaitx failed");
		else
			printf(1,"\nProcess = %d Run time = %d, Wait time = %d\n", i, rtime, wtime);
	}
	exit();
}