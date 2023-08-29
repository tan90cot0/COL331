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
  p->policy = -1;  //Set to EDF
   //cprintf("policy set to -1 default\n");
  p->elapsed_time = 0;
  p->arrival_time = 0;
  p->ticksproc = 0;
  p->deadline = 0;
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
  //cprintf("allocated init \n");
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
  p->deadline = 100000;
  p->exec_time = 100000;
  // this assignment to p->state lets other cores
  // run this process. the acquire forces the above
  // writes to be visible, and the lock is also needed
  // because the assignment might not be atomic.
  acquire(&ptable.lock);

  p->state = RUNNABLE;

  release(&ptable.lock);
  //cprintf("init init finished\n");
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
  np->policy = curproc->policy;
  pid = np->pid;

  acquire(&ptable.lock);

  np->state = RUNNABLE;
  release(&ptable.lock);
  //cprintf("Fored process pid %d from pid %d\n", pid, curproc->pid); 
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
   {cprintf("panicked because exiting process the initproc?");
    panic("init exiting");}

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

  acquire(&ptable.lock);
  curproc->state = ZOMBIE;
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
  //curproc->state = ZOMBIE;
  //cprintf("exit checpoint 1\n");
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
  struct proc *p;
  struct cpu *c = mycpu();
  c->proc = 0;
  
  struct proc *minP = 0;  //will set it to the appropriate val later, anyways

  
  for(;;){
    // Enable interrupts on this processor.
    sti();
    acquire(&ptable.lock);   
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    	if(p->state != RUNNABLE){continue;}
    	
    	
    	if(p->policy == -1){
    	        //cprintf("check\n"); //execute on the spot
    		minP = p;
    	}
    	else if(p->policy == 0){//EDF proc exists, traverse list to find min deadline 
                //cprintf("state %d pid %d\n", p->state, p->pid);
                int minDeadline = 214783647;
                struct proc *q;
                for(q = ptable.proc; q<&ptable.proc[NPROC]; q++){
                	if(q->state != RUNNABLE){continue;}
                	if(q->policy != 0){continue;}
                	
                	//minDeadline initailised to infinity, first condition always true for first EDF proc
                	if(q->deadline < minDeadline){minDeadline = q->deadline;minP = q;}
                	else if(q->deadline == minDeadline){if((q->pid) < (minP->pid)){minP = q;}}                
                
                }
        }
        else if(p->policy == 1){//RM process exists, traverse list to find max weight min pid
                //cprintf("p with pid %d with policy %d, state %d\n",p->pid, p->policy, p->state);
                int minWeight = 4;
                struct proc *q;
                for(q = ptable.proc; q<&ptable.proc[NPROC]; q++){
                	if(q->state != RUNNABLE){continue;}
                	if(q->policy != 1){continue;}
                	
                	//cprintf("rm iterates through pid %d with policy %d, state %d\n",q->pid, q->policy, q->state);
                	//minWeight initailised to 4, first condition always true for first RM proc
                	if(rateToWeight(q->rate) < minWeight){minP = q; minWeight = rateToWeight(q->rate);}
      			else if(rateToWeight(q->rate) == minWeight && ((q->pid) < (minP->pid))){minP = q;}
     			
                }
                //cprintf("rm picks minP with pid %d with policy %d, state %d\n",minP->pid, minP->policy, minP->state);
        }
      	if(minP->state!= ZOMBIE){
      		//cprintf("Process state %d pid %d\n", minP->state, minP->pid);
      	//cprintf("Sched picks process pid %d with policy %d\n",minP->pid, minP->policy);
      		c->proc = minP;
      		switchuvm(minP);
      		minP->state = RUNNING;
      		swtch(&(c->scheduler), minP->context);
      		switchkvm();
      		c->proc = 0;
      		}    
       
   }//inner for loop
   release(&ptable.lock);     
  }//infinite for loop
}//function ki last bracket



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
  //if(p->policy == -1){
  //cprintf("sched checpoint 1 p state %d, pid %d\n", p->state, p->pid);
  swtch(&p->context, mycpu()->scheduler);
  //cprintf("sched checpoint 2\n");
  //}
  //else if(p->policy == 0){
  	//cprintf("we here, sched edf branch \n");
  	//swtch(&p->context, mycpu()->schedulerEDF);
  	//cprintf("swtch to schedulerEDF success");}
  //else{cprintf("Policy = %d \n", p->policy);}
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

// A  child's very first scheduling by scheduler()
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
    if(p->state == SLEEPING){
      getcallerpcs((uint*)p->context->ebp+2, pc);
      for(i=0; i<10 && pc[i] != 0; i++)
        cprintf(" %p", pc[i]);
    }
    cprintf("\n");
  }
}

int
sched_policy(int pid, int policy)
{
  struct proc *p;
  int i = 0;
  acquire(&ptable.lock);
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    //cprintf("loop main p ki pid = %d\n", p->pid);
    //if(p->policy == 0){p->deadline -= p->elapsed_time;}//baaki processes ka elapsed time unki dead
    if(p->pid == pid){
      	release(&ptable.lock);
      	p->policy = policy;
      	p->arrival_time = (int)ticks;
      	
      	if(policy==0){
      		p->deadline += p->arrival_time;
      		i = isSchedEDF(p);
      		}
        if(policy==1){
    		i = isSchedRM(p);    
        	}
      	//cprintf("altered policy of pid %d to %d, deadline = %d\n",p->pid, (p->policy), p->deadline);
      	//release(&ptable.lock);
      	return i;
      	}
    }     
    //-1 for default, 0 for EDF, 1 for RM
    release(&ptable.lock);
    return -22;		
}

int
exec_time(int pid, int exec_t)
{
  struct proc *p;
  acquire(&ptable.lock);
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->pid == pid){
      p->exec_time = exec_t;
      //exec-time of the relevant process set
      //cprintf("altered exec_time of pid %d to %d\n",p->pid, (p->exec_time));
      release(&ptable.lock);
      return 0;
    }
  }
  release(&ptable.lock);
  return -22;
}

int
deadline(int pid, int deadlin)
{
  struct proc *p;
  acquire(&ptable.lock);
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->pid == pid){
      p->deadline = deadlin;
      //cprintf("altered deadline of pid %d to %d\n",p->pid, (p->deadline));
      //deadline set, if needed
      release(&ptable.lock);
      return 0;
    }
  }
  release(&ptable.lock);
  return -22;
}

int
rate(int pid, int rte)
{
  struct proc *p;
  acquire(&ptable.lock);
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->pid == pid){
      p->rate = rte;
      //rate set, if needed
      release(&ptable.lock);
      return 0;
    }
  }
  release(&ptable.lock);
  return -22;
}

int
rateToWeight(int rate)
{
	if(rate <1){return -1;}
	else if(rate < 11){return 3;}
	else if(rate < 21){return 2;}
	else if(rate <31){return 1;}
	else{return -1;}
}

int isSchedEDF(struct proc *pmaybe){
  int num = 0;
  int denim = 1;
  struct proc *p;
  acquire(&ptable.lock);
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
         if(p->policy == 0 && p->pid!=0 &&p->killed ==0){
                //cprintf("pid %d policy %d exectime %d deadline %d\n",p->pid, p->policy, p->exec_time, p->deadline);
		num = num*((p->deadline)-(p->arrival_time)) + denim*(p->exec_time);
		denim = denim*((p->deadline)-(p->arrival_time));		         
          }
 	} // num/den is sigma(ei/pi)
  release(&ptable.lock);
        //cprintf("num %d denim %d\n", num, denim);
	if(num > denim){
	 //cprintf("Process pid %d isn't schedulable\n", pmaybe->pid);
	 pmaybe->killed = 1;
	 pmaybe->state = RUNNABLE;
	 return -22;}
	else{return 0;}
}


int isSchedRM(struct proc *pmaybe){
  int scheds[64] = {1000000, 828427,779763,756828,743492,734772,728627,724062,720538,717735,715452,713557,711959,710593,
  			709412, 708381, 707472, 706666,705946,705298,704713,704182,703698,703254,702846,702469,702121,701798,701497,701217,
  			700955,700709,700478,700261,700056,699863,699681,699508,699343,699188,699040,698898,698764,698636,698513,698396,698284,
  			698176,698073,697974,697879,697788,697700,697615,697533,697455,697379,697306,697235,697166,697100,697036,696974,696914};
  int num = 0;
  int tot = 0;
  struct proc *p;
  acquire(&ptable.lock);
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
         if(p->policy == 1 && p->killed == 0){
         	num+=1;
         	tot += 10000*(p->exec_time)*(p->rate);	         
          }
 	} // num/den is sigma(ei/pi)
  release(&ptable.lock);
  //cprintf(" tot %d sched %d process pid %d\n", tot, scheds[num-1], pmaybe->pid);
  if(num > 64){if (tot > 693147){
  			//cprintf("maybe here?\n");
  			pmaybe->killed = 1;
  			pmaybe->state = RUNNABLE;
  			return -22;} 
  		else{return 0;}}
  else if(tot > scheds[num-1]){
        //cprintf("unschedulable\n");
  	pmaybe->killed = 1;
  	pmaybe->state = RUNNABLE;
  	return -22;}
  else{return 0;}
}
