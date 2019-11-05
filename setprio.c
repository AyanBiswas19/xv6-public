#include "types.h"
#include "stat.h"
#include "user.h"

int main(int argc, char *argv[]){
	if(argc != 3){
		printf(1,"Invalid Syntax\n");
		exit();
	}
	int pid=0, priority=0;
	for(char *c=argv[1];*c;c++)
		pid=pid*10+((int)(*c)-(int)'0');
	for(char *c=argv[2];*c;c++)
		priority=priority*10+((int)(*c)-(int)'0');
	printf(1,"Searching for process %d.\n", pid);
	int r=setpriority(pid,priority);
	if(r==2){
		printf(1,"Invalid Priority\n");
		exit();
	}
	if(r==0){
		printf(1,"Unable to set priority.\n");
		exit();
	}
	printf(1,"Priority sucessfully set.\n");
	exit();
}