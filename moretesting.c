#include "types.h"
#include "stat.h"
#include "user.h"

#define NFORK 25

int main(){
	int pid, wtime, rtime;
	int trtime=0,twtime=0;
	printf(1,"Commencing Test:\n");
	volatile int x, y, j, k;
	for(int i=0; i < NFORK; i++){
		switch(pid=fork()){
			case -1:printf(1,"Unable to fork. i=%d\n", i);
			break;
			case 0: for(k=0; k < 10; k++)
						for(j=0; j < 1000000; j++){
							y+= (x*x*y+x)%100007;
						}
					exit();
			default:
				break;
		}
	}
	for(int i=0; i < NFORK ; i++){		
		if(waitx(&wtime,&rtime)==0)
			printf(1,"\nWaitx failed");
		//else
			//printf(1,"\nProcess = %d Run time = %d, Wait time = %d\n", i, rtime, wtime);
		trtime+=rtime;
		twtime+=wtime;
	}
	trtime/=NFORK;
	twtime/=NFORK;
	printf(1,"Average Run Time = %d, Wait Time = %d\n",trtime,twtime );
	exit();
}