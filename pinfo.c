#include "types.h"
#include "stat.h"
#include "user.h"
#include "proc_stat.h"
int main(int argc, char *argv[]){
	if(argc != 2){
		printf(1,"Invalid Syntax\n");
		exit();
	}
	struct proc_stat s;
	int pid=0;
	for(char *c=argv[1];*c;c++)
		pid=pid*10+((int)(*c)-(int)'0');
	printf(1,"Searching for process %d.\n", pid);
	if(getpinfo(pid,&s)==0){
		printf(1,"Proc not found\n");
		exit();
	}
	printf(1,"Process info:\nPid = %d, Runtime= %d\nNumber of runs = %d\n",s.pid,s.runtime,s.num_run);
	exit();
}