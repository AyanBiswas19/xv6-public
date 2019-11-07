#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "x86.h"
#include "proc.h"
#include "spinlock.h"

struct {
  struct spinlock lock;
  struct proc proc[NPROC];
} ptable;

static struct proc *initproc;

#ifdef MLFQ
static int L0notempty;
#endif

int nextpid = 1;
extern void forkret(void);
extern void trapret(void);

static void wakeup1(void *chan);

void
pinit(void)
{
  initlock(&ptable.lock, "ptable");
}

// Must be called with interrupts disabled
int
cpuid() {
  return mycpu()-cpus;
}

// Must be called with interrupts disabled to avoid the caller being
// rescheduled between reading lapicid and running through the loop.
struct cpu*
mycpu(void)
{
  int apicid, i;
  
  if(readeflags()&FL_IF)
    panic("mycpu called with interrupts enabled\n");
  
  apicid = lapicid();
  // APIC IDs are not guaranteed to be contiguous. Maybe we should have
  // a reverse map, or reserve a register to store &cpus[i].
  for (i = 0; i < ncpu; ++i) {
    if (cpus[i].apicid == apicid)
      return &cpus[i];
  }
  panic("unknown apicid\n");
}

// Disable interrupts so that we are not rescheduled
// while reading proc from the cpu structure
struct proc*
myproc(void) {
  struct cpu *c;
  struct proc *p;
  pushcli();
  c = mycpu();
  p = c->proc;
  popcli();
  return p;
}

//PAGEBREAK: 32
// Look in the process table for an UNUSED proc.
// If found, change state to EMBRYO and initialize
// state required to run in the kernel.
// Otherwise return 0.
static struct proc*
allocproc(void)
{
  struct proc *p;
  char *sp;

  acquire(&ptable.lock);

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if(p->state == UNUSED)
      goto found;

  release(&ptable.lock);
  return 0;

found:
  p->state = EMBRYO;
  p->pid = nextpid++;
  p->priority=60;
  p->current_queue=0;
  for(int i=0;i<NQUEUES;i++)
    p->ticks[i]=0;
  release(&ptable.lock);

  // Allocate kernel stack.
  if((p->kstack = kalloc()) == 0){
    p->state = UNUSED;
    return 0;
  }
  sp = p->kstack + KSTACKSIZE;

  // Leave room for trap frame.
  sp -= sizeof *p->tf;
  p->tf = (struct trapframe*)sp;

  // Set up new context to start executing at forkret,
  // which returns to trapret.
  sp -= 4;
  *(uint*)sp = (uint)trapret;

  sp -= sizeof *p->context;
  p->context = (struct context*)sp;
  memset(p->context, 0, sizeof *p->context);
  p->context->eip = (uint)forkret;

  return p;
}

//PAGEBREAK: 32
// Set up first user process.
void
userinit(void)
{
  struct proc *p;
  extern char _binary_initcode_start[], _binary_initcode_size[];

  p = allocproc();
  
  initproc = p;
  if((p->pgdir = setupkvm()) == 0)
    panic("userinit: out of memory?");
  inituvm(p->pgdir, _binary_initcode_start, (int)_binary_initcode_size);
  p->sz = PGSIZE;
  memset(p->tf, 0, sizeof(*p->tf));
  p->tf->cs = (SEG_UCODE << 3) | DPL_USER;
  p->tf->ds = (SEG_UDATA << 3) | DPL_USER;
  p->tf->es = p->tf->ds;
  p->tf->ss = p->tf->ds;
  p->tf->eflags = FL_IF;
  p->tf->esp = PGSIZE;
  p->tf->eip = 0;  // beginning of initcode.S

  safestrcpy(p->name, "initcode", sizeof(p->name));
  p->cwd = namei("/");

  // this assignment to p->state lets other cores
  // run this process. the acquire forces the above
  // writes to be visible, and the lock is also needed
  // because the assignment might not be atomic.
  acquire(&ptable.lock);

  p->state = RUNNABLE;

  release(&ptable.lock);
}

// Grow current process's memory by n bytes.
// Return 0 on success, -1 on failure.
int
growproc(int n)
{
  uint sz;
  struct proc *curproc = myproc();

  sz = curproc->sz;
  if(n > 0){
    if((sz = allocuvm(curproc->pgdir, sz, sz + n)) == 0)
      return -1;
  } else if(n < 0){
    if((sz = deallocuvm(curproc->pgdir, sz, sz + n)) == 0)
      return -1;
  }
  curproc->sz = sz;
  switchuvm(curproc);
  return 0;
}

// Create a new process copying p as the parent.
// Sets up stack to return as if from system call.
// Caller must set state of returned proc to RUNNABLE.
int
fork(void)
{
  int i, pid;
  struct proc *np;
  struct proc *curproc = myproc();

  // Allocate process.
  if((np = allocproc()) == 0){
    return -1;
  }

  // Copy process state from proc.
  if((np->pgdir = copyuvm(curproc->pgdir, curproc->sz)) == 0){
    kfree(np->kstack);
    np->kstack = 0;
    np->state = UNUSED;
    return -1;
  }
  np->sz = curproc->sz;
  np->parent = curproc;
  *np->tf = *curproc->tf;

  // Clear %eax so that fork returns 0 in the child.
  np->tf->eax = 0;

  for(i = 0; i < NOFILE; i++)
    if(curproc->ofile[i])
      np->ofile[i] = filedup(curproc->ofile[i]);
  np->cwd = idup(curproc->cwd);

  safestrcpy(np->name, curproc->name, sizeof(curproc->name));

  pid = np->pid;

  // Change: Loading time data on new process
  np->ctime=ticks;
  np->rtime=0;
  np->etime=-1;
  np->wtime=0;

  // Change: Initializing no. of runs
  np->num_run=0;

  acquire(&ptable.lock);

  np->state = RUNNABLE;

  release(&ptable.lock);

  return pid;
}

// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait() to find out it exited.
void
exit(void)
{
  struct proc *curproc = myproc();
  struct proc *p;
  int fd;

  if(curproc == initproc)
    panic("init exiting");

  // Close all open files.
  for(fd = 0; fd < NOFILE; fd++){
    if(curproc->ofile[fd]){
      fileclose(curproc->ofile[fd]);
      curproc->ofile[fd] = 0;
    }
  }

  begin_op();
  iput(curproc->cwd);
  end_op();
  curproc->cwd = 0;

  //Update times
  curproc->etime=ticks;

  acquire(&ptable.lock);

  // Parent might be sleeping in wait().
  wakeup1(curproc->parent);

  // Pass abandoned children to init.
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->parent == curproc){
      p->parent = initproc;
      if(p->state == ZOMBIE)
        wakeup1(initproc);
    }
  }

  // Jump into the scheduler, never to return.
  curproc->state = ZOMBIE;
  sched();
  panic("zombie exit");
}

// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
int
wait(void)
{
  struct proc *p;
  int havekids, pid;
  struct proc *curproc = myproc();
  
  acquire(&ptable.lock);
  for(;;){
    // Scan through table looking for exited children.
    havekids = 0;
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->parent != curproc)
        continue;
      havekids = 1;
      if(p->state == ZOMBIE){
        // Found one.
        pid = p->pid;
        kfree(p->kstack);
        p->kstack = 0;
        freevm(p->pgdir);
        p->pid = 0;
        p->parent = 0;
        p->name[0] = 0;
        p->killed = 0;
        p->state = UNUSED;
        release(&ptable.lock);
        return pid;
      }
    }

    // No point waiting if we don't have any children.
    if(!havekids || curproc->killed){
      release(&ptable.lock);
      return -1;
    }

    // Wait for children to exit.  (See wakeup1 call in proc_exit.)
    sleep(curproc, &ptable.lock);  //DOC: wait-sleep
  }
}

//PAGEBREAK: 42
// Per-CPU process scheduler.
// Each CPU calls scheduler() after setting itself up.
// Scheduler never returns.  It loops, doing:
//  - choose a process to run
//  - swtch to start running that process
//  - eventually that process transfers control
//      via swtch back to the scheduler.
void
scheduler(void)
{
  struct proc *p=0;
  struct cpu *c = mycpu();
  c->proc = 0;
  
  #ifdef MLFQ
  acquire(&ptable.lock);
  L0notempty=1;
  release(&ptable.lock);
  #endif

  for(;;){
    // Enable interrupts on this processor.
    sti();

    // Loop over process table looking for process to run.
    acquire(&ptable.lock);

    #ifdef DEFAULT
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->state != RUNNABLE)
        continue;
      c->proc = p;
      switchuvm(p);
      p->num_run++;
      p->state = RUNNING;
      swtch(&(c->scheduler), p->context);
      switchkvm();
      c->proc = 0;
    }
    #else

    #ifdef FCFS
    struct proc *earliest=0;
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->state != RUNNABLE)
        continue;
      if(earliest){
        if(p->ctime < earliest->ctime)
          earliest=p;
      }
      else
        earliest=p;
    }
    if(earliest!=0){
      p=earliest;
      c->proc = p;
      switchuvm(p);
      p->num_run++;
      p->state = RUNNING;
      swtch(&(c->scheduler), p->context);
      switchkvm();
      c->proc = 0;
    }
    #else

    #ifdef PRIORITY
    if(p==0)
      p=ptable.proc;
    struct proc *l=leftmax(p); // Left is inclusive of p
    struct proc *r=rightmax(p);
    if(!l || !r){
      p=(l==0)?r:l;
    }
    else{
      if(r->priority <= l->priority)
        p=r;
      else
        p=l;
    }
    if(p!=0){
      c->proc = p;
      switchuvm(p);
      p->num_run++;
      p->state = RUNNING;
      swtch(&(c->scheduler), p->context);
      switchkvm();
      c->proc = 0;
    }
    #else

    #ifdef MLFQ
    p=0;
    int is_empty=0;
    int k;
    for(struct proc *s=ptable.proc+2;s>=ptable.proc;s--)
      s->current_queue=0;
    //Gotta select a process
    for(int i=0;i<NQUEUES;i+=(is_empty)?1:0){
      //Iterating over the queues
      for(p=ptable.proc;p < &ptable.proc[NPROC] ; p++){
        //cprintf("i=%d\n",i);
        //If higher priority process exists we go for it, so back to first queue.
        //cprintf("Running the first queue\n");

        if(p==ptable.proc)
          is_empty=1;
        if(p->state!=RUNNABLE || p->current_queue!=i)
          continue;
        is_empty=0;
        for(k=0 ; k < pow(2,i) && p->state==RUNNABLE ; k++){
          c->proc = p;
          switchuvm(p);
          p->num_run++;
          //cprintf("executing pid %d: %s\n", p->pid, p->name);
          p->state = RUNNING;
          swtch(&(c->scheduler), p->context);
          switchkvm();
          c->proc = 0;
        }
        if(p->current_queue < NQUEUES - 1)
          p->current_queue=p->current_queue+1;

        if( i != (NQUEUES-1) && L0notempty && i!=0){
          //cprintf("Return to first queue triggered.\n" );
          is_empty=0;
          i=0;
          break;
        }
      }
    }

    #endif
    #endif
    #endif
    #endif

    release(&ptable.lock);

  }
}

// Enter scheduler.  Must hold only ptable.lock
// and have changed proc->state. Saves and restores
// intena because intena is a property of this
// kernel thread, not this CPU. It should
// be proc->intena and proc->ncli, but that would
// break in the few places where a lock is held but
// there's no process.
void
sched(void)
{
  int intena;
  struct proc *p = myproc();

  if(!holding(&ptable.lock))
    panic("sched ptable.lock");
  if(mycpu()->ncli != 1)
    panic("sched locks");
  if(p->state == RUNNING)
    panic("sched running");
  if(readeflags()&FL_IF)
    panic("sched interruptible");
  intena = mycpu()->intena;
  swtch(&p->context, mycpu()->scheduler);
  mycpu()->intena = intena;
}

// Give up the CPU for one scheduling round.
void
yield(void)
{
  acquire(&ptable.lock);  //DOC: yieldlock
  myproc()->state = RUNNABLE;
  sched();
  release(&ptable.lock);
}

// A fork child's very first scheduling by scheduler()
// will swtch here.  "Return" to user space.
void
forkret(void)
{
  static int first = 1;
  // Still holding ptable.lock from scheduler.
  release(&ptable.lock);

  if (first) {
    // Some initialization functions must be run in the context
    // of a regular process (e.g., they call sleep), and thus cannot
    // be run from main().
    first = 0;
    iinit(ROOTDEV);
    initlog(ROOTDEV);
  }

  // Return to "caller", actually trapret (see allocproc).
}

// Atomically release lock and sleep on chan.
// Reacquires lock when awakened.
void
sleep(void *chan, struct spinlock *lk)
{
  struct proc *p = myproc();
  
  if(p == 0)
    panic("sleep");

  if(lk == 0)
    panic("sleep without lk");

  // Must acquire ptable.lock in order to
  // change p->state and then call sched.
  // Once we hold ptable.lock, we can be
  // guaranteed that we won't miss any wakeup
  // (wakeup runs with ptable.lock locked),
  // so it's okay to release lk.
  if(lk != &ptable.lock){  //DOC: sleeplock0
    acquire(&ptable.lock);  //DOC: sleeplock1
    release(lk);
  }
  // Go to sleep.
  p->chan = chan;
  p->state = SLEEPING;

  sched();

  // Tidy up.
  p->chan = 0;

  // Reacquire original lock.
  if(lk != &ptable.lock){  //DOC: sleeplock2
    release(&ptable.lock);
    acquire(lk);
  }
}

//PAGEBREAK!
// Wake up all processes sleeping on chan.
// The ptable lock must be held.
static void
wakeup1(void *chan)
{
  struct proc *p;

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if(p->state == SLEEPING && p->chan == chan)
      p->state = RUNNABLE;
}

// Wake up all processes sleeping on chan.
void
wakeup(void *chan)
{
  acquire(&ptable.lock);
  wakeup1(chan);
  release(&ptable.lock);
}

// Kill the process with the given pid.
// Process won't exit until it returns
// to user space (see trap in trap.c).
int
kill(int pid)
{
  struct proc *p;

  acquire(&ptable.lock);
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->pid == pid){
      p->killed = 1;
      // Wake process from sleep if necessary.
      if(p->state == SLEEPING)
        p->state = RUNNABLE;
      release(&ptable.lock);
      return 0;
    }
  }
  release(&ptable.lock);
  return -1;
}

//PAGEBREAK: 36
// Print a process listing to console.  For debugging.
// Runs when user types ^P on console.
// No lock to avoid wedging a stuck machine further.
void
procdump(void)
{
  static char *states[] = {
  [UNUSED]    "unused",
  [EMBRYO]    "embryo",
  [SLEEPING]  "sleep ",
  [RUNNABLE]  "runble",
  [RUNNING]   "run   ",
  [ZOMBIE]    "zombie"
  };
  int i;
  struct proc *p;
  char *state;
  uint pc[10];

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->state == UNUSED)
      continue;
    if(p->state >= 0 && p->state < NELEM(states) && states[p->state])
      state = states[p->state];
    else
      state = "???";
    cprintf("%d %s %s", p->pid, state, p->name);
    //Queue printf is my addition
    #ifdef MLFQ
    cprintf(" Queue= %d",p->current_queue);
    #endif
    if(p->state == SLEEPING){
      getcallerpcs((uint*)p->context->ebp+2, pc);
      for(i=0; i<10 && pc[i] != 0; i++)
        cprintf(" %p", pc[i]);
    }
    cprintf("\n");
  }
}

void update_stats(void) {
  struct proc *p;
  acquire(&ptable.lock);
  #ifdef MLFQ
  L0notempty=0;
  #endif
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->state == RUNNING) {
        p->rtime++;
        p->ticks[p->current_queue]+=1;
    }
    else
      p->wtime++;
    #ifdef MLFQ
    if(p->wtime >= QMAXWAIT){
      p->wtime=0;
      if(p->current_queue)
        p->current_queue=p->current_queue-1;
    }
    if(p->state==RUNNABLE && p->current_queue==0)
      L0notempty=1;
    #endif
  }
  release(&ptable.lock);
}

int
waitx(int *wtime, int *rtime)
{
  struct proc *p;
  int havekids, pid;
  struct proc *curproc = myproc();
  
  acquire(&ptable.lock);
  for(;;){
    // Scan through table looking for exited children.
    havekids = 0;
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->parent != curproc)
        continue;
      havekids = 1;
      if(p->state == ZOMBIE){
        // Found one.

        //Updating times
        *wtime = p->etime - p->ctime - p->rtime;
        *rtime = p->rtime;

        pid = p->pid;
        kfree(p->kstack);
        p->kstack = 0;
        freevm(p->pgdir);
        p->pid = 0;
        p->parent = 0;
        p->name[0] = 0;
        p->killed = 0;
        p->state = UNUSED;
        release(&ptable.lock);
        return pid;
      }
    }

    // No point waiting if we don't have any children.
    if(!havekids || curproc->killed){
      release(&ptable.lock);
      return -1;
    }

    // Wait for children to exit.  (See wakeup1 call in proc_exit.)
    sleep(curproc, &ptable.lock);  //DOC: wait-sleep
  }
}

int
getpinfo(int pid, struct proc_stat *s)
{
  struct proc *p;
  acquire(&ptable.lock);
  for(p=ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->pid==pid){
      s->pid=pid;
      s->runtime=p->rtime;
      s->num_run=p->num_run;
      s->current_queue=p->current_queue;
      for(int i=0; i < NQUEUES; i++)
        s->ticks[i]=p->ticks[i];
      release(&ptable.lock);
      return 1;
    }
  }
  release(&ptable.lock);
  return 0;
}

int setpriority(int pid, int priority){
  if(!(priority>=0 && priority <=100))
    return 101;
  int yield_flag=0;
  struct proc *p;
  acquire(&ptable.lock);
  for(p=ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->pid==pid){
      if(p->priority > priority)
        yield_flag=1;
      p->priority=priority;
      release(&ptable.lock);
      if(yield_flag)
        yield();
      return 1;
    }
  }
  release(&ptable.lock);
  return 101;
}

struct proc *leftmax(struct proc *s){
  struct proc *p=0,*max=0;
  for(p=ptable.proc;p<=s;p++){
    if(p->state!=RUNNABLE)
      continue;
    if(max==0){
      max=p;
    }
    else
      if(p->priority < max->priority)
        max=p;
  }
  return max;
}

struct proc *rightmax(struct proc *s){
  struct proc *r,*max=0;
  for(r=s+1; r < &ptable.proc[NPROC];r++){
    if(r->state!=RUNNABLE)
      continue;
    if(max==0){
      max=r;
    }
    else{
      if(r->priority < max->priority)
        max=r;
    }
  }
  return max;
}

int getticks(void){
  return ticks;
}

int pow(int a, int b){
  int r=1;
  for(int i=1;i<=b;i++)
    r=r*a;
  return r;
}

// void update_queues(int Q[NQUEUES][NPROC]){
//   struct proc *p=0;
//   for(int i=0;i<NQUEUES;i++){
//     for(int j=0;i<NPROC;i++){
//       for(p=ptable.proc; p<&ptable.proc[NPROC] ; p++){
//         if(p->pid==Q[i][j])
//           break;
//       }
//       if(p==(&ptable.proc[NPROC]))
//         Q[i][j]=-1;
//     }
//   }
// }

int printptable(void){
  struct proc *p;
  for(p=ptable.proc;p<&ptable.proc[NPROC];p++){
    if(p->state==UNUSED)
      continue;
    cprintf("Process Name= %s\t Pid = %d\t", p->name, p->pid);
    #ifdef PRIORITY
    cprintf("Priority = %d\t",p->priority);
    #else
    #ifdef MLFQ
    cprintf("Current Queue = %d\t", p->current_queue);
    cprintf("Ticks in each queue:\t");
    for(int i=0; i<NQUEUES; i++){
      cprintf("%d\t",p->ticks[i]);
    }
    #endif
    #endif
    cprintf("\n");
  }
  return 1;
}