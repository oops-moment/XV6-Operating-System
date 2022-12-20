#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"

struct cpu cpus[NCPU];

struct proc proc[NPROC];

struct proc *initproc;

int i = 0;

int nextpid = 1;
struct spinlock pid_lock;

extern void forkret(void);
static void freeproc(struct proc *p);

extern char trampoline[]; // trampoline.S

// helps ensure that wakeups of wait()ing
// parents are not lost. helps obey the
// memory model when using p->parent.
// must be acquired before any p->lock.
struct spinlock wait_lock;

// Queue Utility functions
#ifdef MLFQ
struct Queue p_queue[5]; // array of struct queues of size 5

void queue_push(struct Queue *Q, struct proc *selected_process)
{
    if (Q->no_processes == NPROC) // checks is already total process are there in queue
    {
        panic("Cannot add more processes");
    }
    // pushes the element in the tail of queue
    int index = Q->tail;
    Q->tail++;
    Q->process_array[index] = selected_process;

    // Queue is full, make tail 0 to indicate queue is full
    if (Q->tail == NPROC + 1)
    {
        Q->tail = 0;
    }

    Q->no_processes++; // increment total number of processes
}

struct proc *queue_pop(struct Queue *Q)
{
    // If there are no processes in queue
    if (Q->no_processes == 0)
    {
        panic("No Process In Queue");
    }

    struct proc *return_process = Q->process_array[Q->head];
    Q->head++; // head points to next element now

    // Removing the last process from the queue
    if (Q->head == NPROC + 1)
    {
        Q->head = 0;
    }
    Q->no_processes--; // decrease the process

    return return_process;
}

#endif
// Allocate a page for each process's kernel stack.
// Map it high in memory, followed by an invalid
// guard page.
void proc_mapstacks(pagetable_t kpgtbl)
{
    struct proc *p;

    for (p = proc; p < &proc[NPROC]; p++)
    {
        char *pa = kalloc();
        if (pa == 0)
            panic("kalloc");
        uint64 va = KSTACK((int)(p - proc));
        kvmmap(kpgtbl, va, (uint64)pa, PGSIZE, PTE_R | PTE_W);
    }
}

// initialize the proc table at boot time.
void procinit(void)
{
    struct proc *p;

    initlock(&pid_lock, "nextpid");
    initlock(&wait_lock, "wait_lock");
    for (p = proc; p < &proc[NPROC]; p++)
    {
        initlock(&p->lock, "proc");
        p->kstack = KSTACK((int)(p - proc));
    }
    // initalising the array of structures
#ifdef MLFQ
    for (int i = 0; i < 5; i++)
    {
        p_queue[i].no_processes = 0;
        p_queue[i].head = 0;
        p_queue[i].tail = 0;
    }
#endif
}

// Must be called with interrupts disabled,
// to prevent race with process being moved
// to a different CPU.
int cpuid()
{
    int id = r_tp();
    return id;
}

// Return this CPU's cpu struct.
// Interrupts must be disabled.
struct cpu *
mycpu(void)
{
    int id = cpuid();
    struct cpu *c = &cpus[id];
    return c;
}

// Return the current struct proc *, or zero if none.
struct proc *
myproc(void)
{
    push_off();
    struct cpu *c = mycpu();
    struct proc *p = c->proc;
    pop_off();
    return p;
}

int allocpid()
{
    int pid;

    acquire(&pid_lock);
    pid = nextpid;
    nextpid = nextpid + 1;
    release(&pid_lock);

    return pid;
}

int lottery_total()
{
    struct proc *p = myproc();
    int total = 0;

    // Iterate over all runnable processes and get the number of tickets
    for (p = proc; p < &proc[NPROC]; p++)
    {
        if (p->state == RUNNABLE)
        {
            total += p->tickets;
        }
    }

    // Returns total number of tickets present with all processes
    return total;
}

// Look in the process table for an UNUSED proc.
// If found, initialize state required to run in the kernel,
// and return with p->lock held.
// If there are no free procs, or a memory allocation fails, return 0.
static struct proc *
allocproc(void)
{
    struct proc *p;

    for (p = proc; p < &proc[NPROC]; p++)
    {
        acquire(&p->lock);
        if (p->state == UNUSED)
        {
            goto found;
        }
        else
        {
            release(&p->lock);
        }
    }
    return 0;

found:
    p->pid = allocpid();
    p->state = USED;

    // FCFS
    p->createTime = ticks; // used to give priority to the process that was created earliest

    // PBS
    p->runTime = 0;           // ?
    p->sleepTime = 0;         // total sleep time of process
    p->totalRunTime = 0;      //?
    p->num_runs = 0;          // ?
    p->process_priority = 60; // default priority set to 60

    // LBS
    p->tickets = 1;

    // SIG_ALARM
    p->sig_alarm_flag = 0; // 1 if the sigalarm function is being called.
    p->input_ticks = 0;    // required ticks as passed  by arguments
    p->count_ticks = 0;    // ticks by now
    p->handler = 0;        // handler

// MLFQ alocating the stuff
#ifdef MLFQ

    p->queue_priority = 0; // process given highest priority 0 intially
    p->in_queue = 0;       // process not in queue rn
    p->time_quanta = 1;    // time quanta allocated
    p->nrun = 0;           // process not called yet do no_calls set to zero
    p->qitime = ticks;     // Time a process stays in a queue
    for (int i = 0; i < 5; i++)
    {
        p->qrtime[i] = 0;
    }

    i++;
#endif

    // When a process asks xv6 for more user memory, xv6 first uses kalloc to allocate physical pages
    if ((p->trapframe = (struct trapframe *)kalloc()) == 0)
    {
        freeproc(p);
        release(&p->lock);
        return 0;
    }
    if ((p->trapframe_temp = (struct trapframe *)kalloc()))
    {
        if (p->trapframe_temp == 0)
        {
            release(&p->lock);
            return 0;
        }
    }
    // An empty user page table.
    p->pagetable = proc_pagetable(p);
    if (p->pagetable == 0)
    {
        freeproc(p);
        release(&p->lock);
        return 0;
    }

    // Set up new context to start executing at forkret,
    // which returns to user space.
    memset(&p->context, 0, sizeof(p->context)); // setting upa new context
    p->context.ra = (uint64)forkret;
    p->context.sp = p->kstack + PGSIZE;

    return p;
}

// free a proc structure and the data hanging from it,
// including user pages.
// p->lock must be held.
static void
freeproc(struct proc *p)
{
    if (p->trapframe)
    {
        kfree((void *)p->trapframe);
    }
    if (p->trapframe_temp) // free all the user pages pointed
    {
        kfree((void *)p->trapframe_temp);
    }

    p->trapframe = 0;
    if (p->pagetable)
        proc_freepagetable(p->pagetable, p->sz);
    p->pagetable = 0;
    p->sz = 0;
    p->pid = 0;
    p->parent = 0;
    p->name[0] = 0;
    p->chan = 0;
    p->killed = 0;
    p->xstate = 0;
    p->state = UNUSED;
}

// Create a user page table for a given process,
// with no user memory, but with trampoline pages.
pagetable_t
proc_pagetable(struct proc *p)
{
    pagetable_t pagetable;

    // An empty page table.
    pagetable = uvmcreate();
    if (pagetable == 0)
        return 0;

    // map the trampoline code (for system call return)
    // at the highest user virtual address.
    // only the supervisor uses it, on the way
    // to/from user space, so not PTE_U.
    if (mappages(pagetable, TRAMPOLINE, PGSIZE,
                 (uint64)trampoline, PTE_R | PTE_X) < 0)
    {
        uvmfree(pagetable, 0);
        return 0;
    }

    // map the trapframe just below TRAMPOLINE, for trampoline.S.
    if (mappages(pagetable, TRAPFRAME, PGSIZE,
                 (uint64)(p->trapframe), PTE_R | PTE_W) < 0)
    {
        uvmunmap(pagetable, TRAMPOLINE, 1, 0);
        uvmfree(pagetable, 0);
        return 0;
    }

    return pagetable;
}

// Free a process's page table, and free the
// physical memory it refers to.
void proc_freepagetable(pagetable_t pagetable, uint64 sz)
{
    uvmunmap(pagetable, TRAMPOLINE, 1, 0);
    uvmunmap(pagetable, TRAPFRAME, 1, 0);
    uvmfree(pagetable, sz);
}

// a user program that calls exec("/init")
// od -t xC initcode
uchar initcode[] = {
    0x17, 0x05, 0x00, 0x00, 0x13, 0x05, 0x45, 0x02,
    0x97, 0x05, 0x00, 0x00, 0x93, 0x85, 0x35, 0x02,
    0x93, 0x08, 0x70, 0x00, 0x73, 0x00, 0x00, 0x00,
    0x93, 0x08, 0x20, 0x00, 0x73, 0x00, 0x00, 0x00,
    0xef, 0xf0, 0x9f, 0xff, 0x2f, 0x69, 0x6e, 0x69,
    0x74, 0x00, 0x00, 0x24, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00};

// Set up first user process.
void userinit(void)
{
    struct proc *p;

    p = allocproc();
    initproc = p;

    // allocate one user page and copy init's instructions
    // and data into it.
    uvminit(p->pagetable, initcode, sizeof(initcode));
    p->sz = PGSIZE;

    // prepare for the very first "return" from kernel to user.
    p->trapframe->epc = 0;     // user program counter
    p->trapframe->sp = PGSIZE; // user stack pointer

    safestrcpy(p->name, "initcode", sizeof(p->name));
    p->cwd = namei("/");

    p->state = RUNNABLE;

    release(&p->lock);
}

// Grow or shrink user memory by n bytes.
// Return 0 on success, -1 on failure.
int growproc(int n)
{
    uint sz;
    struct proc *p = myproc();

    sz = p->sz;
    if (n > 0)
    {
        if ((sz = uvmalloc(p->pagetable, sz, sz + n)) == 0)
        {
            return -1;
        }
    }
    else if (n < 0)
    {
        sz = uvmdealloc(p->pagetable, sz, sz + n);
    }
    p->sz = sz;
    return 0;
}

// Create a new process, copying the parent.
// Sets up child kernel stack to return as if from fork() system call.
int fork(void)
{
    int i, pid;
    struct proc *np;
    struct proc *p = myproc();

    // Allocate process.
    if ((np = allocproc()) == 0)
    {
        return -1;
    }

    // Copy user memory from parent to child.
    if (uvmcopy(p->pagetable, np->pagetable, p->sz) < 0)
    {
        freeproc(np);
        release(&np->lock);
        return -1;
    }
    np->sz = p->sz;

    // copy saved user registers.
    *(np->trapframe) = *(p->trapframe);

    np->mask = p->mask;

    // Cause fork to return 0 in the child.
    np->trapframe->a0 = 0;

    // increment reference counts on open file descriptors.
    for (i = 0; i < NOFILE; i++)
        if (p->ofile[i])
            np->ofile[i] = filedup(p->ofile[i]);
    np->cwd = idup(p->cwd);

    safestrcpy(np->name, p->name, sizeof(p->name));

    pid = np->pid;

    release(&np->lock);

    acquire(&wait_lock);
    np->parent = p;
    release(&wait_lock);

    acquire(&np->lock);
    np->state = RUNNABLE;
    release(&np->lock);

    return pid;
}

// Pass p's abandoned children to init.
// Caller must hold wait_lock.
void reparent(struct proc *p)
{
    struct proc *pp;

    for (pp = proc; pp < &proc[NPROC]; pp++)
    {
        if (pp->parent == p)
        {
            pp->parent = initproc;
            wakeup(initproc);
        }
    }
}

// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait().
void exit(int status)
{
    struct proc *p = myproc();

    if (p == initproc)
        panic("init exiting");

    // Close all open files.
    for (int fd = 0; fd < NOFILE; fd++)
    {
        if (p->ofile[fd])
        {
            struct file *f = p->ofile[fd];
            fileclose(f);
            p->ofile[fd] = 0;
        }
    }

    begin_op();
    iput(p->cwd);
    end_op();
    p->cwd = 0;

    acquire(&wait_lock);

    // Give any children to init.
    reparent(p);

    // Parent might be sleeping in wait().
    wakeup(p->parent);

    acquire(&p->lock);

    p->xstate = status;
    p->state = ZOMBIE;
    p->exitTime = ticks;

    release(&wait_lock);

    // Jump into the scheduler, never to return.
    sched();
    panic("zombie exit");
}

// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
int wait(uint64 addr)
{
    struct proc *np;
    int havekids, pid;
    struct proc *p = myproc();

    acquire(&wait_lock);

    for (;;)
    {
        // Scan through table looking for exited children.
        havekids = 0;
        for (np = proc; np < &proc[NPROC]; np++)
        {
            if (np->parent == p)
            {
                // make sure the child isn't still in exit() or swtch().
                acquire(&np->lock);

                havekids = 1;
                if (np->state == ZOMBIE)
                {
                    // Found one.
                    pid = np->pid;
                    if (addr != 0 && copyout(p->pagetable, addr, (char *)&np->xstate,
                                             sizeof(np->xstate)) < 0)
                    {
                        release(&np->lock);
                        release(&wait_lock);
                        return -1;
                    }
                    freeproc(np);
                    release(&np->lock);
                    release(&wait_lock);
                    return pid;
                }
                release(&np->lock);
            }
        }

        // No point waiting if we don't have any children.
        if (!havekids || p->killed)
        {
            release(&wait_lock);
            return -1;
        }

        // Wait for a child to exit.
        sleep(p, &wait_lock); // DOC: wait-sleep
    }
}

int random(int max)
{

    if (max <= 0)
    {
        return 1;
    }

    static int z1 = 12345; // 12345 for rest of zx
    static int z2 = 12345; // 12345 for rest of zx
    static int z3 = 12345; // 12345 for rest of zx
    static int z4 = 12345; // 12345 for rest of zx

    int b;
    b = (((z1 << 6) ^ z1) >> 13);
    z1 = (((z1 & 4294967294) << 18) ^ b);
    b = (((z2 << 2) ^ z2) >> 27);
    z2 = (((z2 & 4294967288) << 2) ^ b);
    b = (((z3 << 13) ^ z3) >> 21);
    z3 = (((z3 & 4294967280) << 7) ^ b);
    b = (((z4 << 3) ^ z4) >> 12);
    z4 = (((z4 & 4294967168) << 13) ^ b);

    // if we have an argument, then we can use it
    int rand = ((z1 ^ z2 ^ z3 ^ z4)) % max;

    if (rand < 0)
    {
        rand = rand * -1;
    }

    return rand;
}

// Per-CPU process scheduler.
// Each CPU calls scheduler() after setting itself up.
// Scheduler never returns.  It loops, doing:
//  - choose a process to run.
//  - swtch to start running that process.
//  - eventually that process transfers control
//    via swtch back to the scheduler.
void scheduler(void)
{
    struct proc *p;
    struct cpu *c = mycpu();

    c->proc = 0;
    int count = 0;
    for (;;)
    {

        // Avoid deadlock by ensuring that devices can interrupt.
        intr_on();

#ifdef RR
        // default scheduler no selecton required
        if (count == 0)
        {
            printf("SCHEDULER: RR\n");
        }
        count = 1;
        for (p = proc; p < &proc[NPROC]; p++)
        {
            acquire(&p->lock);
            if (p->state == RUNNABLE)
            {
                // Switch to chosen process.  It is the process's job
                // to release its lock and then reacquire it
                // before jumping back to us.
                p->state = RUNNING;
                c->proc = p;
                swtch(&c->context, &p->context);

                // Process is done running for now.
                // It should have changed its p->state before coming back.
                c->proc = 0;
            }
            release(&p->lock);
        }
#endif

#ifdef FCFS
        // printf("hi\n");
        if (count == 0)
        {
            printf("SCHEDULER: FCFS\n");
        }
        count = 1;
        struct proc *priority_process = 0;
        for (p = proc; p < &proc[NPROC]; p++)
        {
            acquire(&p->lock);
            if (p->state == RUNNABLE)
            {
                // FCFS scheduling
                // its just that which process is getting update each time a process with a lesser create
                // is encountered

                if (priority_process == 0)
                {
                    priority_process = p;
                }
                if (priority_process->createTime > p->createTime) // minimum of create time
                {
                    priority_process = p;
                }
            }
            release(&p->lock);
        }

        if (priority_process != 0)
        {
            acquire(&priority_process->lock); // lock the chosen process
            if (priority_process->state == RUNNABLE)
            {

                priority_process->state = RUNNING;              // set running state
                c->proc = priority_process;                     // set current process
                swtch(&c->context, &priority_process->context); // switch cpu's context to the chosen process context
                c->proc = 0;                                    // reset the value once the process is done executing
            }
            release(&priority_process->lock); // release the lock
        }
#endif

#ifdef PBS
        // printf("ok\n");
        if (count == 0)
        {
            printf("SCHEDULER: PBS\n");
        }
        count = 1;
        struct proc *priority_process = 0; // the process with the highest priority
        int dynamic_priority = 101;        // the highest priority possible
        for (p = proc; p < &proc[NPROC]; p++)
        {
            acquire(&p->lock); // lock the chosen process

            int nice = 5; // default nice value
            // niceness calculation
            if (p->runTime + p->sleepTime)
            {
                nice = ((10 * p->sleepTime) / (p->sleepTime + p->runTime));
            }
            int temp;
            if ((p->process_priority - nice + 5) < 100)
            {
                temp = (p->process_priority - nice + 5);
            }
            else
            {
                temp = 100;
            }

            int process_dp;
            if (temp < 0)
            {
                process_dp = 0;
            }
            else
            {
                process_dp = temp;
            }
            int check = 0;
            if (priority_process == 0)
            {
                check = 1;
            }
            else if (dynamic_priority > process_dp)
            {
                check = 1;
            }
            else if ((dynamic_priority == process_dp && priority_process->num_runs > p->num_runs))
            {
                check = 1;
            }
            else if ((dynamic_priority == process_dp &&
                      priority_process->num_runs == p->num_runs &&
                      priority_process->createTime > p->createTime))
            {
                check = 1;
            }
            if (p->state == RUNNABLE)
                if ( check == 1)
                {

                    if (priority_process)
                        release(&priority_process->lock); // release the lock of the previous process

                    dynamic_priority = process_dp; // set the dynamic priority
                    priority_process = p;          // set the process with the highest priority
                    continue;
                }
            release(&p->lock); // release the lock
        }

        if (priority_process)
        {                                                   // if there is a process with the highest priority
            priority_process->state = RUNNING;              // set the process with the highest priority to running
            priority_process->startTime = ticks;            // set the start time of the process with the highest priority
            priority_process->num_runs++;                   // increase the number of runs of the process with the highest priority
            priority_process->runTime = 0;                  // set the run time of the process with the highest priority to 0
            priority_process->sleepTime = 0;                // set the sleep time of the process with the highest priority to 0
            c->proc = priority_process;                     // set the current process to the process with the highest priority
            swtch(&c->context, &priority_process->context); // switch to the process with the highest priority
            // Process is done running for now.
            // It should have changed its p->state before coming back.
            c->proc = 0;                      // no longer running
            release(&priority_process->lock); // release the lock of the process with the highest priority
        }
#endif
#ifdef MLFQ
        // Reset priority for old processes /Aging/
        if (count == 0)
        {
            printf("SCHEDULER: MLFQ\n");
        }
        count = 1;
        for (p = proc; p < &proc[NPROC]; p++)
        {
            // if the process is runnable then check if it has exceeded it's time quanta
            // then demote that process to lower queue and change priority of process below it
            if (p->state == RUNNABLE)
            {
                // Process has been in queue for longer than 32
                // Implementing aging
                int time_in_queue = ticks - p->qitime;

                if (time_in_queue >= 32)
                {
                    p->qitime = ticks;

                    if (p->in_queue)
                    {
                        // changing the priority after demotion

                        for (int curr = p_queue->head; curr != p_queue->tail; curr = (curr + 1) % (NPROC + 1))
                        {
                            if (p_queue->process_array[curr]->pid == p->pid)
                            {
                                struct proc *temp = p_queue->process_array[curr];
                                p_queue->process_array[curr] = p_queue->process_array[(curr + 1) % (NPROC + 1)];
                                p_queue->process_array[(curr + 1) % (NPROC + 1)] = temp;
                            }
                        }

                        p->in_queue = 0;
                    }
                    // priority of demoted process decreases and it is pushed to a
                    // higher priority queue
                    if (p->queue_priority != 0)
                    {
                        p->queue_priority--;
                        // printf("%d %d %d\n", p->pid, ticks, p->queue_priority);
                    }
                }
            }
        }

        struct proc *mlfq_process = 0;

        // in this function it pushes the process into the queue
        for (p = proc; p < &proc[NPROC]; p++)
        {
            acquire(&p->lock);
            if (p->state == RUNNABLE)
            {
                if (!p->in_queue)
                {
                    queue_push(&p_queue[p->queue_priority], p);
                    p->in_queue = 1;
                }
            }
            release(&p->lock);
        }

        for (int q_no = 0; q_no < 5; q_no++)
        {
            while (p_queue[q_no].no_processes != 0)
            {
                p = queue_pop(&p_queue[q_no]);
                p->in_queue = 0;

                acquire(&p->lock);

                if (p->state == RUNNABLE)
                {
                    p->qitime = ticks;
                    mlfq_process = p;
                    break;
                }
                release(&p->lock);
            }

            if (mlfq_process != 0)
            {
                break;
            }
        }

        if (mlfq_process == 0)
        {
            continue;
        }

        mlfq_process->state = RUNNING;
        mlfq_process->time_quanta = 1 << mlfq_process->queue_priority;
        c->proc = mlfq_process;
        mlfq_process->nrun++;
        swtch(&c->context, &mlfq_process->context);
        c->proc = 0;
        mlfq_process->qitime = ticks;
        release(&mlfq_process->lock);
#endif

#ifdef LBS

        if (count == 0)
        {
            printf("SCHEDULER: LBS\n");
        }
        count = 1;
        int total_tickets = 0;
        int lottery = -1;

        for (p = proc; p < &proc[NPROC]; p++)
        {

            if (p->state != RUNNABLE)
            {
                continue;
            }
            else
            {
                total_tickets = lottery_total();
                // Assign a random lottery number to choose a process

                if (lottery < 0)
                    lottery = random(total_tickets);

                // process with higher number of tickets makes
                // lottery negative more often and thus gets more CPU cycles
                lottery = lottery - p->tickets;

                if (lottery < 0)
                    break;
            }
        }

        // schedule the process if lottery becomes negative
        if (lottery < 0)
        {
            acquire(&p->lock);
            if (p->state == RUNNABLE)
            {
                // Switch to chosen process.  It is the process's job
                // to release its lock and then reacquire it
                // before jumping back to us.
                p->state = RUNNING;
                c->proc = p;
                swtch(&c->context, &p->context);

                // Process is done running for now.
                // It should have changed its p->state before coming back.
                c->proc = 0;
            }
            release(&p->lock);
        }

#endif
    }
}

// Switch to scheduler.  Must hold only p->lock
// and have changed proc->state. Saves and restores
// intena because intena is a property of this
// kernel thread, not this CPU. It should
// be proc->intena and proc->noff, but that would
// break in the few places where a lock is held but
// there's no process.
void sched(void)
{
    int intena;
    struct proc *p = myproc();

    if (!holding(&p->lock))
        panic("sched p->lock");
    if (mycpu()->noff != 1)
        panic("sched locks");
    if (p->state == RUNNING)
        panic("sched running");
    if (intr_get())
        panic("sched interruptible");

    intena = mycpu()->intena;
    swtch(&p->context, &mycpu()->context);
    mycpu()->intena = intena;
}

// Give up the CPU for one scheduling round.
void yield(void)
{
    struct proc *p = myproc();
    acquire(&p->lock);
    p->state = RUNNABLE;
    sched();
    release(&p->lock);
}

// A fork child's very first scheduling by scheduler()
// will swtch to forkret.
void forkret(void)
{
    static int first = 1;

    // Still holding p->lock from scheduler.
    release(&myproc()->lock);

    if (first)
    {
        // File system initialization must be run in the context of a
        // regular process (e.g., because it calls sleep), and thus cannot
        // be run from main().
        first = 0;
        fsinit(ROOTDEV);
    }

    usertrapret();
}

// Atomically release lock and sleep on chan.
// Reacquires lock when awakened.
void sleep(void *chan, struct spinlock *lk)
{
    struct proc *p = myproc();

    // Must acquire p->lock in order to
    // change p->state and then call sched.
    // Once we hold p->lock, we can be
    // guaranteed that we won't miss any wakeup
    // (wakeup locks p->lock),
    // so it's okay to release lk.

    acquire(&p->lock); // DOC: sleeplock1
    release(lk);

    // Go to sleep.
    p->chan = chan;
    p->state = SLEEPING;

    sched();

    // Tidy up.
    p->chan = 0;

    // Reacquire original lock.
    release(&p->lock);
    acquire(lk);
}

// Wake up all processes sleeping on chan.
// Must be called without any p->lock.
void wakeup(void *chan)
{
    struct proc *p;

    for (p = proc; p < &proc[NPROC]; p++)
    {
        if (p != myproc())
        {
            acquire(&p->lock);
            if (p->state == SLEEPING && p->chan == chan)
            {
                p->state = RUNNABLE;
            }
            release(&p->lock);
        }
    }
}

// Kill the process with the given pid.
// The victim won't exit until it tries to return
// to user space (see usertrap() in trap.c).
int kill(int pid)
{
    struct proc *p;

    for (p = proc; p < &proc[NPROC]; p++)
    {
        acquire(&p->lock);
        if (p->pid == pid)
        {
            p->killed = 1;
            if (p->state == SLEEPING)
            {
                // Wake process from sleep().
                p->state = RUNNABLE;
            }
            release(&p->lock);
            return 0;
        }
        release(&p->lock);
    }
    return -1;
}

// Copy to either a user address, or kernel address,
// depending on usr_dst.
// Returns 0 on success, -1 on error.
int either_copyout(int user_dst, uint64 dst, void *src, uint64 len)
{
    struct proc *p = myproc();
    if (user_dst)
    {
        return copyout(p->pagetable, dst, src, len);
    }
    else
    {
        memmove((char *)dst, src, len);
        return 0;
    }
}

// Copy from either a user address, or kernel address,
// depending on usr_src.
// Returns 0 on success, -1 on error.
int either_copyin(void *dst, int user_src, uint64 src, uint64 len)
{
    struct proc *p = myproc();
    if (user_src)
    {
        return copyin(p->pagetable, dst, src, len);
    }
    else
    {
        memmove(dst, (char *)src, len);
        return 0;
    }
}

// Print a process listing to console.  For debugging.
// Runs when user types ^P on console.
// No lock to avoid wedging a stuck machine further.
void procdump(void)
{
    static char *states[] = {
        [UNUSED] "unused",
        [SLEEPING] "sleep ",
        [RUNNABLE] "runble",
        [RUNNING] "run   ",
        [ZOMBIE] "zombie"};
    struct proc *p;
    char *state;

    printf("\n");
    for (p = proc; p < &proc[NPROC]; p++)
    {
        if (p->state == UNUSED)
            continue;
        if (p->state >= 0 && p->state < NELEM(states) && states[p->state])
            state = states[p->state];
        else
            state = "???";

#if defined RR || defined FCFS
        int wtime = ticks - p->createTime - p->totalRunTime;
        printf("%d\t%s\t%d\t%d\t%d\n", p->pid, state, p->totalRunTime, wtime, p->num_runs);
#endif
#ifdef PBS
        int wtime = ticks - p->createTime - p->totalRunTime;
        printf("%d\t%d\t%s\t%d\t%d\t%d\n", p->pid, p->process_priority, state, p->totalRunTime, wtime, p->num_runs);
#endif

#ifdef LBS
        int wtime = ticks - p->createTime - p->totalRunTime;
        printf("%d\t%s\t%d\t%d\t%d\n", p->pid, state, p->totalRunTime, wtime, p->num_runs);
#endif
#ifdef MLFQ
        int wtime = ticks - p->qitime;
        printf("%d %d %s %d %d %d %d %d %d %d %d", p->pid, p->queue_priority, state, p->totalRunTime, wtime, p->nrun, p->qrtime[0], p->qrtime[1], p->qrtime[2], p->qrtime[3], p->qrtime[4]);
#endif
    }
}

void update_time()
{
    struct proc *p;
    for (p = proc; p < &proc[NPROC]; p++)
    {
        acquire(&p->lock);        // lock the process
        if (p->state == SLEEPING) // if the Process is sleeping
            p->sleepTime++;       // increment the sleepTime
        if (p->state == RUNNING)  // if the Process is running
        {
            p->runTime++;      // increment the runTime
            p->totalRunTime++; // increment the totalRunTime
#ifdef MLFQ
            // printf("%d %d %d\n", p->pid, ticks, p->queue_priority);
            p->qrtime[p->queue_priority]++;
            // printf("%d %d %d\n", p->pid, ticks, p->queue_priority);

            p->time_quanta--;
#endif
        }
        release(&p->lock); // unlock the process
    }
}

int set_priority(int new_priority, int pid)
{
    int old_priority = -1;

    struct proc *p;

    for (p = proc; p < &proc[NPROC]; p++)
    {
        acquire(&p->lock); // lock the process
        if (p->pid == pid)
        {                                       // if the process is found
            old_priority= p->process_priority;            // save the old priority
            p->process_priority = new_priority; // set the new priority
            release(&p->lock);                  // unlock the process
            if (old_priority > new_priority)               // if the new priority is lower
            {
                yield(); // yield the CPU
#
            }
            break;
        }
        release(&p->lock); // unlock the process
    }
    return old_priority;
}

int waitx(uint64 addr, uint *runTime, uint *wtime)
{
    struct proc *np;
    int havekids, pid;
    struct proc *p = myproc();

    acquire(&wait_lock);

    for (;;)
    {
        // Scan through table looking for exited children.
        havekids = 0;
        for (np = proc; np < &proc[NPROC]; np++)
        {
            if (np->parent == p)
            {
                // make sure the child isn't still in exit() or swtch().
                acquire(&np->lock);

                havekids = 1;
                if (np->state == ZOMBIE)
                {
                    // Found one.
                    pid = np->pid;
                    *runTime = np->totalRunTime;
                    *wtime = np->exitTime - np->createTime - np->totalRunTime;
                    if (addr != 0 && copyout(p->pagetable, addr, (char *)&np->xstate,
                                             sizeof(np->xstate)) < 0)
                    {
                        release(&np->lock);
                        release(&wait_lock);
                        return -1;
                    }
                    freeproc(np);
                    release(&np->lock);
                    release(&wait_lock);
                    return pid;
                }
                release(&np->lock);
            }
        }

        // No point waiting if we don't have any children.
        if (!havekids || p->killed)
        {
            release(&wait_lock);
            return -1;
        }

        // Wait for a child to exit.
        sleep(p, &wait_lock); // DOC: wait-sleep
    }
}
