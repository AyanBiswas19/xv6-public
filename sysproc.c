#include "types.h"
#include "x86.h"
#include "defs.h"
#include "date.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"

int
sys_fork(void)
{
  return fork();
}

int
sys_exit(void)
{
  exit();
  return 0;  // not reached
}

int
sys_wait(void)
{
  return wait();
}

int
sys_kill(void)
{
  int pid;

  if(argint(0, &pid) < 0)
    return -1;
  return kill(pid);
}

int
sys_getpid(void)
{
  return myproc()->pid;
}

int
sys_sbrk(void)
{
  int addr;
  int n;

  if(argint(0, &n) < 0)
    return -1;
  addr = myproc()->sz;
  if(growproc(n) < 0)
    return -1;
  return addr;
}

int
sys_sleep(void)
{
  int n;
  uint ticks0;

  if(argint(0, &n) < 0)
    return -1;
  acquire(&tickslock);
  ticks0 = ticks;
  while(ticks - ticks0 < n){
    if(myproc()->killed){
      release(&tickslock);
      return -1;
    }
    sleep(&ticks, &tickslock);
  }
  release(&tickslock);
  return 0;
}

// return how many clock tick interrupts have occurred
// since start.
int
sys_uptime(void)
{
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}

int
sys_halt(void)
{
  outb(0xf4, 0x00);
  return 0;
}

int 
sys_waitx(void)
{
  int *wtime, *rtime;
  argptr(0, (void*)&wtime, sizeof(int));
  argptr(1, (void*)&rtime, sizeof(int));
  return waitx(wtime,rtime);
}

int
sys_getpinfo(void)
{
  struct proc_stat *s;
  int pid;
  argint(0, &pid);
  argptr(1, (void*)&s, sizeof(struct proc_stat *));
  return getpinfo(pid,s);
}

int
sys_setpriority(void)
{
  int pid,priority;
  argint(0, &pid);
  argint(1, &priority);
  return setpriority(pid,priority);
}

int sys_getticks(void){
  return getticks();
}

int sys_printptable(void){
  return printptable();
}