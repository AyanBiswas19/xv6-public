#include "types.h"
#include "user.h"
#include "stat.h"

//This is a dummy program for writing various dummy stuff.

int main(int arc, char *argv[]){
	exec(argv[1],argv+1);
	printf(1,"Unable to exec.\n");
	exit();
}