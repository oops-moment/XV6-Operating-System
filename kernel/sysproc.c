#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "date.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"

uint64
sys_exit(void)
{
  int n;
  if (argint(0, &n) < 0)
    return -1;
  exit(n);
  return 0; // not reached
}

// function for sigalarm
uint64 
sys_sigalarm(void)
{
    int ticks;
    uint64 handler;
    if (argint(0, &ticks) < 0 || argaddr(1, &handler) < 0) // assigning value of ticks to ticks .. tick passed as argument
    {
        return -1;
    }
    // here handler would store the address of handler functions
    myproc()->sig_alarm_flag = 0;  // initializing my proc
    myproc()->input_ticks = ticks; // setting ticks to give argument
    myproc()->count_ticks = 0;     // ticks by now intialized to 0
    myproc()->handler = handler;
    return 0;
}

uint64 
sys_sigreturn(void)
{
  struct proc *p = myproc();
  *(p->trapframe) = *(p->trapframe_temp);
  myproc()->sig_alarm_flag = 0; // once over you reset flag to 0
  int return_val=myproc()->trapframe->a0;
  return return_val;
}

uint64
sys_getpid(void)
{
  return myproc()->pid;
}

uint64
sys_fork(void)
{
  return fork();
}

uint64
sys_wait(void)
{
  uint64 p;
  if (argaddr(0, &p) < 0)
    return -1;
  return wait(p);
}

uint64
sys_sbrk(void)
{
  int addr;
  int n;

  if (argint(0, &n) < 0)
    return -1;
  addr = myproc()->sz;
  if (growproc(n) < 0)
    return -1;
  return addr;
}

uint64
sys_sleep(void)
{
  int n;
  uint ticks0;

  if (argint(0, &n) < 0)
    return -1;
  acquire(&tickslock);
  ticks0 = ticks;
  while (ticks - ticks0 < n)
  {
    if (myproc()->killed)
    {
      release(&tickslock);
      return -1;
    }
    sleep(&ticks, &tickslock);
  }
  release(&tickslock);
  return 0;
}

uint64
sys_kill(void)
{
  int pid;

  if (argint(0, &pid) < 0)
    return -1;
  return kill(pid);
}

// return how many clock tick interrupts have occurred
// since start.
uint64
sys_uptime(void)
{
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}

uint64
sys_trace()
{
  int mask;

  if (argint(0, &mask) < 0)
    return -1;
  myproc()->mask = mask;

  return 0;
}

uint64
sys_waitx(void)
{
  uint64 addr, addr1, addr2;
  uint wtime, rtime;
  if (argaddr(0, &addr) < 0)
    return -1;
  if (argaddr(1, &addr1) < 0) // user virtual memory
    return -1;
  if (argaddr(2, &addr2) < 0)
    return -1;
  int ret = waitx(addr, &wtime, &rtime);
  struct proc *p = myproc();
  if (copyout(p->pagetable, addr1, (char *)&wtime, sizeof(int)) < 0)
    return -1;
  if (copyout(p->pagetable, addr2, (char *)&rtime, sizeof(int)) < 0)
    return -1;
  return ret;
}

uint64
sys_set_priority()
{  struct proc* proc=myproc();
  int priority, pid;
  int oldpriority = 101;
  if (argint(0, &priority) < 0 || argint(1, &pid) < 0)
    return -1;

  for (struct proc *p = proc; p < &proc[NPROC]; p++)
  {
    acquire(&p->lock);

    if (p->pid == pid && priority >= 0 && priority <= 100)
    {
      p->sleepTime = 0;
      p->runTime = 0;
      oldpriority = p->process_priority;
      p->process_priority = priority;
    }

    release(&p->lock);
  }
  if (oldpriority > priority)
    yield();
  return oldpriority;
}

uint64 
sys_settickets()
{
  int ticket;
  struct proc* proc=myproc();
  if(argint(0, &ticket) < 0)
  {
    proc->tickets = 1;
  }
  else
  {
    proc->tickets = ticket;
  }

  return 0;
}
