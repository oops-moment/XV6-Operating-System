// #include "types.h"
// #include "param.h"
// #include "memlayout.h"
// #include "riscv.h"
// #include "spinlock.h"
// #include "proc.h"
// #include "defs.h"

// struct spinlock tickslock;
// uint ticks;
// #ifdef MLFQ
// extern struct Queue p_queue[5];
// #endif
// extern char trampoline[], uservec[], userret[];

// // in kernelvec.S, calls kerneltrap().
// void kernelvec();

// extern int devintr();

// void trapinit(void)
// {
//   initlock(&tickslock, "time");
// }

// // set up to take exceptions and traps while in the kernel.
// void trapinithart(void)
// {
//   w_stvec((uint64)kernelvec);
// }

// //
// // handle an interrupt, exception, or system call from user space.
// // called from trampoline.S
// //
// void usertrap(void)
// {
//   int which_dev = 0;

//   if ((r_sstatus() & SSTATUS_SPP) != 0)
//     panic("usertrap: not from user mode");

//   // send interrupts and exceptions to kerneltrap(),
//   // since we're now in the kernel.
//   w_stvec((uint64)kernelvec);

//   struct proc *p = myproc();

//   // save user program counter.
//   p->trapframe->epc = r_sepc();

//   if (r_scause() == 8)
//   {
//     // system call

//     if (p->killed)
//       exit(-1);

//     // sepc points to the ecall instruction,
//     // but we want to return to the next instruction.
//     p->trapframe->epc += 4;

//     // an interrupt will change sstatus &c registers,
//     // so don't enable until done with those registers.
//     intr_on();

//     syscall();
//   }
//   else if (r_scause() == 0xf)
//   {
//     if (cowfault(p->pagetable, r_stval()) < 0)
//     {
//       p->killed = 1;
//     }
//   }
//   else if ((which_dev = devintr()) != 0)
//   {
//     if (which_dev == 2) // checks if it is the timer interrupt
//     {
//       p->count_ticks++;       // increment the value of ticks
//       if (p->input_ticks > 0) // check if ticks by now are less than the total desired ticks
//       {
//         if (p->count_ticks >= p->input_ticks)
//         {
//           if (p->sig_alarm_flag == 0)
//           {
//             p->count_ticks = 0;                     // setting now_ticks to 1
//             p->sig_alarm_flag = 1;                  // if sigalarm is called then set its flag to 1
//             *(p->trapframe_temp) = *(p->trapframe); // rather than copying each and every parameter just point to memeory location
//             p->trapframe->epc = p->handler;         // copy the address of handler function
//           }
//         }
//       }
//     }
//   }
//     else
//     {
//       printf("usertrap(): unexpected scause %p pid=%d\n", r_scause(), p->pid);
//       printf("            sepc=%p stval=%p\n", r_sepc(), r_stval());
//       p->killed = 1;
//     }

//     if (p->killed)
//       exit(-1);
// // #ifdef RR // disabling interrupt for FCFS and PBS
// //   // printf("ello\n");
// //  if (which_dev == 2)
// //     yield();

// // #endif
// #ifdef MLFQ
//     if (which_dev == 2 && myproc() && myproc()->state == RUNNING)
//     {
//       struct proc *p = myproc();
//       if (p->time_quanta <= 0)
//       {
//         p->queue_priority = p->queue_priority + 1 != 5 ? p->queue_priority + 1 : p->queue_priority;
//         yield();
//       }
//       for (int i = 0; i < p->queue_priority; i++)
//       {
//         if (p_queue[i].no_processes)
//         {
//           yield();
//         }
//       }
//     }
// #endif
// #ifdef LBS // disabling interrupt for FCFS and PBS
//     // printf("hello\n");
//     if (which_dev == 2)
//       yield();
// #endif
// // give up the CPU if this is a timer interrupt.
// #if defined RR
//     if (which_dev == 2) // checks if it is the timer interrupt
//     {
//       p->count_ticks++;       // increment the value of ticks
//       if (p->input_ticks > 0) // check if ticks by now are less than the total desired ticks
//       {
//         if (p->count_ticks >= p->input_ticks)
//         {
//           if (p->sig_alarm_flag == 0)
//           {
//             p->count_ticks = 0;                     // setting now_ticks to 1
//             p->sig_alarm_flag = 1;                  // if sigalarm is called then set its flag to 1
//             *(p->trapframe_temp) = *(p->trapframe); // rather than copying each and every parameter just point to memeory location
//             p->trapframe->epc = p->handler;         // copy the address of handler function
//           }
//         }
//       }
//       yield();
//     }

// #endif

//     usertrapret();
//   }

//   // return to user space
//   //
//   int cowfault(pagetable_t pagetable, uint64 va)
//   {
//     if (va >= MAXVA)
//     {
//       return -1;
//     }
//     pte_t *pte = walk(pagetable, va, 0);
//     if (pte == 0)
//       return -1; // if  page table could not be found

//     if ((*pte & PTE_U) == 0 || (*pte & PTE_V) == 0)
//       return -1; // if address is invalid

//     // so now we need a pointer to copy the page
//     uint64 pa1 = PTE2PA(*pte);
//     // here this is pa2 allocated memory
//     uint64 pa2 = (uint64)kalloc();
//     if (pa2 == 0)
//     {
//       // printf("Cow KAlloc failed\n");
//       return -1;
//     }
//     // we want to free a a page when there are zero page tables that refers to it
//     memmove((void *)pa2, (void *)pa1, 4096); // points the page
//     // kfree((void*)pa1);  // you free the page
//     *pte = PA2PTE(pa2) | PTE_V | PTE_U | PTE_R | PTE_W | PTE_X; // creating a copy
//     kfree((void *)pa1);
//     // *pte &= ~PTE_C;
//     return 0;
//   }
//   void usertrapret(void)
//   {
//     struct proc *p = myproc();

//     // we're about to switch the destination of traps from
//     // kerneltrap() to usertrap(), so turn off interrupts until
//     // we're back in user space, where usertrap() is correct.
//     intr_off();

//     // send syscalls, interrupts, and exceptions to trampoline.S
//     w_stvec(TRAMPOLINE + (uservec - trampoline));

//     // set up trapframe values that uservec will need when
//     // the process next re-enters the kernel.
//     p->trapframe->kernel_satp = r_satp();         // kernel page table
//     p->trapframe->kernel_sp = p->kstack + PGSIZE; // process's kernel stack
//     p->trapframe->kernel_trap = (uint64)usertrap;
//     p->trapframe->kernel_hartid = r_tp(); // hartid for cpuid()

//     // set up the registers that trampoline.S's sret will use
//     // to get to user space.

//     // set S Previous Privilege mode to User.
//     unsigned long x = r_sstatus();
//     x &= ~SSTATUS_SPP; // clear SPP to 0 for user mode
//     x |= SSTATUS_SPIE; // enable interrupts in user mode
//     w_sstatus(x);

//     // set S Exception Program Counter to the saved user pc.
//     w_sepc(p->trapframe->epc);

//     // tell trampoline.S the user page table to switch to.
//     uint64 satp = MAKE_SATP(p->pagetable);

//     // jump to trampoline.S at the top of memory, which
//     // switches to the user page table, restores user registers,
//     // and switches to user mode with sret.
//     uint64 fn = TRAMPOLINE + (userret - trampoline);
//     ((void (*)(uint64, uint64))fn)(TRAPFRAME, satp);
//   }

//   // interrupts and exceptions from kernel code go here via kernelvec,
//   // on whatever the current kernel stack is.
//   void kerneltrap()
//   {
//     int which_dev = 0;
//     uint64 sepc = r_sepc();
//     uint64 sstatus = r_sstatus();
//     uint64 scause = r_scause();

//     if ((sstatus & SSTATUS_SPP) == 0)
//       panic("kerneltrap: not from supervisor mode");
//     if (intr_get() != 0)
//       panic("kerneltrap: interrupts enabled");

//     if ((which_dev = devintr()) == 0)
//     {
//       printf("scause %p\n", scause);
//       printf("sepc=%p stval=%p\n", r_sepc(), r_stval());
//       panic("kerneltrap");
//     }

//     if (which_dev == 2 && myproc() != 0 && myproc()->state == RUNNING)
//     {
// #ifdef RR
//       // give up the CPU if this is a timer interrupt.

//       yield();
// #endif

// #ifdef LBS
//       // give up the CPU if this is a timer interrupt.

//       yield();
// #endif

// #ifdef MLFQ

//       {
//         struct proc *p = myproc();
//         if (p->time_quanta <= 0)
//         {
//           p->queue_priority = p->queue_priority + 1 != 5 ? p->queue_priority + 1 : p->queue_priority;
//           yield();
//         }
//         for (int i = 0; i < p->queue_priority; i++)
//         {
//           if (p_queue[i].no_processes)
//           {
//             yield();
//           }
//         }
//       }
// #endif
//     }
//     // the yield() may have caused some traps to occur,
//     // so restore trap registers for use by kernelvec.S's sepc instruction.
//     w_sepc(sepc);
//     w_sstatus(sstatus);
//   }

//   void clockintr()
//   {
//     acquire(&tickslock);
//     ticks++;
//     update_time();
//     wakeup(&ticks);
//     release(&tickslock);
//   }

//   // check if it's an external interrupt or software interrupt,
//   // and handle it.
//   // returns 2 if timer interrupt,
//   // 1 if other device,
//   // 0 if not recognized.
//   int devintr()
//   {
//     uint64 scause = r_scause();

//     if ((scause & 0x8000000000000000L) &&
//         (scause & 0xff) == 9)
//     {
//       // this is a supervisor external interrupt, via PLIC.

//       // irq indicates which device interrupted.
//       int irq = plic_claim();

//       if (irq == UART0_IRQ)
//       {
//         uartintr();
//       }
//       else if (irq == VIRTIO0_IRQ)
//       {
//         virtio_disk_intr();
//       }
//       else if (irq)
//       {
//         printf("unexpected interrupt irq=%d\n", irq);
//       }

//       // the PLIC allows each device to raise at most one
//       // interrupt at a time; tell the PLIC the device is
//       // now allowed to interrupt again.
//       if (irq)
//         plic_complete(irq);

//       return 1;
//     }
//     else if (scause == 0x8000000000000001L)
//     {
//       // software interrupt from a machine-mode timer interrupt,
//       // forwarded by timervec in kernelvec.S.

//       if (cpuid() == 0)
//       {
//         clockintr();
//       }

//       // acknowledge the software interrupt by clearing
//       // the SSIP bit in sip.
//       w_sip(r_sip() & ~2);

//       return 2;
//     }
//     else
//     {
//       return 0;
//     }
//   }
#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"

struct spinlock tickslock;
uint ticks;
#ifdef MLFQ
extern struct Queue p_queue[5];
#endif
extern char trampoline[], uservec[], userret[];

// in kernelvec.S, calls kerneltrap().
void kernelvec();

extern int devintr();

void trapinit(void)
{
  initlock(&tickslock, "time");
}

// set up to take exceptions and traps while in the kernel.
void trapinithart(void)
{
  w_stvec((uint64)kernelvec);
}

//
// handle an interrupt, exception, or system call from user space.
// called from trampoline.S
//
void usertrap(void)
{
  int which_dev = 0;

  if ((r_sstatus() & SSTATUS_SPP) != 0)
    panic("usertrap: not from user mode");

  // send interrupts and exceptions to kerneltrap(),
  // since we're now in the kernel.
  w_stvec((uint64)kernelvec);

  struct proc *p = myproc();

  // save user program counter.
  p->trapframe->epc = r_sepc();

  if (r_scause() == 8)
  {
    // system call

    if (p->killed)
      exit(-1);

    // sepc points to the ecall instruction,
    // but we want to return to the next instruction.
    p->trapframe->epc += 4;

    // an interrupt will change sstatus &c registers,
    // so don't enable until done with those registers.
    intr_on();

    syscall();
  }
  else if (r_scause() == 0xf)
  {
    if (cowfault(p->pagetable, r_stval()) < 0)
    {
      p->killed = 1;
    }
  }
  else if ((which_dev = devintr()) != 0)
  {
    if (which_dev == 2) // checks if it is the timer interrupt
    {
      p->count_ticks++;       // increment the value of ticks
      if (p->input_ticks > 0) // check if ticks by now are less than the total desired ticks
      {
        if (p->count_ticks >= p->input_ticks)
        {
          if (p->sig_alarm_flag == 0)
          {
            p->count_ticks = 0;                     // setting now_ticks to 1
            p->sig_alarm_flag = 1;                  // if sigalarm is called then set its flag to 1
            *(p->trapframe_temp) = *(p->trapframe); // rather than copying each and every parameter just point to memeory location
            p->trapframe->epc = p->handler;         // copy the address of handler function
          }
        }
      }
      yield();
    }
    // ok
  }
  else
  {
    printf("usertrap(): unexpected scause %p pid=%d\n", r_scause(), p->pid);
    printf("            sepc=%p stval=%p\n", r_sepc(), r_stval());
    p->killed = 1;
  }

  if (p->killed)
    exit(-1);

#if defined RR || LBS // disabling interrupt for FCFS and PBS
                      // printf("ello\n");
  if (which_dev == 2)
    yield();
#endif

#ifdef MLFQ
  if (which_dev == 2 && myproc() && myproc()->state == RUNNING)
  {
    struct proc *p = myproc();
    if (p->time_quanta <= 0)
    {
      p->queue_priority = p->queue_priority + 1 != 5 ? p->queue_priority + 1 : p->queue_priority;
      yield();
    }
    for (int i = 0; i < p->queue_priority; i++)
    {
      if (p_queue[i].no_processes)
      {
        yield();
      }
    }
  }
#endif
  usertrapret();
}

// return to user space
//
int cowfault(pagetable_t pagetable, uint64 va)
{
  if (va >= MAXVA)
  {
    return -1;
  }
  pte_t *pte = walk(pagetable, va, 0);
  if (pte == 0)
    return -1; // if  page table could not be found

  if ((*pte & PTE_U) == 0 || (*pte & PTE_V) == 0)
    return -1; // if address is invalid

  // so now we need a pointer to copy the page
  uint64 pa1 = PTE2PA(*pte);
  // here this is pa2 allocated memory
  uint64 pa2 = (uint64)kalloc();
  if (pa2 == 0)
  {
    // printf("Cow KAlloc failed\n");
    return -1;
  }
  // we want to free a a page when there are zero page tables that refers to it
  memmove((void *)pa2, (void *)pa1, 4096); // points the page
  // kfree((void*)pa1);  // you free the page
  *pte = PA2PTE(pa2) | PTE_V | PTE_U | PTE_R | PTE_W | PTE_X; // creating a copy
  kfree((void *)pa1);
  // *pte &= ~PTE_C;
  return 0;
}
void usertrapret(void)
{
  struct proc *p = myproc();

  // we're about to switch the destination of traps from
  // kerneltrap() to usertrap(), so turn off interrupts until
  // we're back in user space, where usertrap() is correct.
  intr_off();

  // send syscalls, interrupts, and exceptions to trampoline.S
  w_stvec(TRAMPOLINE + (uservec - trampoline));

  // set up trapframe values that uservec will need when
  // the process next re-enters the kernel.
  p->trapframe->kernel_satp = r_satp();         // kernel page table
  p->trapframe->kernel_sp = p->kstack + PGSIZE; // process's kernel stack
  p->trapframe->kernel_trap = (uint64)usertrap;
  p->trapframe->kernel_hartid = r_tp(); // hartid for cpuid()

  // set up the registers that trampoline.S's sret will use
  // to get to user space.

  // set S Previous Privilege mode to User.
  unsigned long x = r_sstatus();
  x &= ~SSTATUS_SPP; // clear SPP to 0 for user mode
  x |= SSTATUS_SPIE; // enable interrupts in user mode
  w_sstatus(x);

  // set S Exception Program Counter to the saved user pc.
  w_sepc(p->trapframe->epc);

  // tell trampoline.S the user page table to switch to.
  uint64 satp = MAKE_SATP(p->pagetable);

  // jump to trampoline.S at the top of memory, which
  // switches to the user page table, restores user registers,
  // and switches to user mode with sret.
  uint64 fn = TRAMPOLINE + (userret - trampoline);
  ((void (*)(uint64, uint64))fn)(TRAPFRAME, satp);
}

// interrupts and exceptions from kernel code go here via kernelvec,
// on whatever the current kernel stack is.
void kerneltrap()
{
  int which_dev = 0;
  uint64 sepc = r_sepc();
  uint64 sstatus = r_sstatus();
  uint64 scause = r_scause();

  if ((sstatus & SSTATUS_SPP) == 0)
    panic("kerneltrap: not from supervisor mode");
  if (intr_get() != 0)
    panic("kerneltrap: interrupts enabled");

  if ((which_dev = devintr()) == 0)
  {
    printf("scause %p\n", scause);
    printf("sepc=%p stval=%p\n", r_sepc(), r_stval());
    panic("kerneltrap");
  }

  if (which_dev == 2 && myproc() != 0 && myproc()->state == RUNNING)
  {
#if defined RR || LBS
    // give up the CPU if this is a timer interrupt.
    // if (which_dev == 2 && myproc() != 0 && myproc()->state == RUNNING)
    yield();
#endif

#ifdef MLFQ
    {
      struct proc *p = myproc();
      if (p->time_quanta <= 0)
      {
        p->queue_priority = p->queue_priority + 1 != 5 ? p->queue_priority + 1 : p->queue_priority;
        yield();
      }
      for (int i = 0; i < p->queue_priority; i++)
      {
        if (p_queue[i].no_processes)
        {
          yield();
        }
      }
    }
#endif
  }
  // the yield() may have caused some traps to occur,
  // so restore trap registers for use by kernelvec.S's sepc instruction.
  w_sepc(sepc);
  w_sstatus(sstatus);
}

void clockintr()
{
  acquire(&tickslock);
  ticks++;
  update_time();
  wakeup(&ticks);
  release(&tickslock);
}

// check if it's an external interrupt or software interrupt,
// and handle it.
// returns 2 if timer interrupt,
// 1 if other device,
// 0 if not recognized.
int devintr()
{
  uint64 scause = r_scause();

  if ((scause & 0x8000000000000000L) &&
      (scause & 0xff) == 9)
  {
    // this is a supervisor external interrupt, via PLIC.

    // irq indicates which device interrupted.
    int irq = plic_claim();

    if (irq == UART0_IRQ)
    {
      uartintr();
    }
    else if (irq == VIRTIO0_IRQ)
    {
      virtio_disk_intr();
    }
    else if (irq)
    {
      printf("unexpected interrupt irq=%d\n", irq);
    }

    // the PLIC allows each device to raise at most one
    // interrupt at a time; tell the PLIC the device is
    // now allowed to interrupt again.
    if (irq)
      plic_complete(irq);

    return 1;
  }
  else if (scause == 0x8000000000000001L)
  {
    // software interrupt from a machine-mode timer interrupt,
    // forwarded by timervec in kernelvec.S.

    if (cpuid() == 0)
    {
      clockintr();
    }

    // acknowledge the software interrupt by clearing
    // the SSIP bit in sip.
    w_sip(r_sip() & ~2);

    return 2;
  }
  else
  {
    return 0;
  }
}