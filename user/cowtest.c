// tests for copy-on-write fork()

#include "kernel/types.h"
#include "kernel/memlayout.h"
#include "user/user.h"

// allocate more than half of physical memory, then fork.
// fails in the default kernel (with no cow)
void simpletest()
{
    uint64 physical_size = PHYSTOP - KERNBASE;
    int size = (physical_size / 3) * 2;

    printf("simple test: ");

    char *p = sbrk(size);
    if (p == (char *)0xffffffffffffffffL)
    {
        printf("sbrk(%d) failed\n", size);
        exit(-1);
    }

    for (char *q = p; q < p + size; q += 4096)
    {
        *(int *)q = getpid();
    }

    int pid = fork();
    if (pid < 0)
    {
        printf("fork() failed\n");
        exit(-1);
    }

    if (pid == 0)
        exit(0);

    wait(0);

    if (sbrk(-size) == (char *)0xffffffffffffffffL)
    {
        printf("sbrk(-%d) failed\n", size);
        exit(-1);
    }
    else
        printf("test passed\n");
}

// three processes all write to COW memory.
// this causes more than half of physical memory to be allocated
// then checks if copied pages are freed.
void threetest()
{
    uint64 physical_size = PHYSTOP - KERNBASE;
    int size = physical_size / 4;
    int pid1, pid2;

    printf("three test: ");

    char *p = sbrk(size);
    if (p == (char *)0xffffffffffffffffL)
    {
        printf("sbrk(%d) failed\n", size);
        exit(-1);
    }

    pid1 = fork();  // fork the first process
    if (pid1 < 0)
    {
        printf("error: fork failed\n");
        exit(-1);
    }
    if (pid1 == 0)
    {
        pid2 = fork();      // fork the second process
        if (pid2 < 0)
        {
            printf("error: fork failed");
            exit(-1);
        }
        if (pid2 == 0)
        {
            for (char *q = p; q < p + (size / 5) * 4; q += 4096)
            {
                *(int *)q = getpid();
            }
            for (char *q = p; q < p + (size / 5) * 4; q += 4096)
            {
                if (*(int *)q != getpid())
                {
                    printf("error: wrong content\n");
                    exit(-1);
                }
            }
            exit(-1);
        }
        for (char *q = p; q < p + (size / 2); q += 4096)
        {
            *(int *)q = 9999;
        }
        exit(0);
    }

    for (char *q = p; q < p + size; q += 4096)
    {
        *(int *)q = getpid();
    }

    wait(0);

    sleep(1);

    for (char *q = p; q < p + size; q += 4096)
    {
        if (*(int *)q != getpid())
        {
            printf("error: wrong content\n");
            exit(-1);
        }
    }

    if (sbrk(-size) == (char *)0xffffffffffffffffL)
    {
        printf("sbrk(-%d) failed\n", size);
        exit(-1);
    }

    printf("test passed\n");
}

char junk1[4096];
int fds[2];
char junk2[4096];
char buf[4096];
char junk3[4096];

// test whether copyout() simulates COW faults.
void filetest()
{
    printf("file test: ");

    buf[0] = 99;

    for (int i = 0; i < 4; i++)
    {
        if (pipe(fds) != 0)
        {
            printf("error: piping failed\n");
            exit(-1);
        }
        int pid = fork();
        if (pid < 0)
        {
            printf("error: forking failed\n");
            exit(-1);
        }
        if (pid == 0)
        {
            sleep(1);
            if (read(fds[0], buf, sizeof(i)) != sizeof(i))
            {
                printf("error: read failed\n");
                exit(1);
            }
            sleep(1);
            int j = *(int *)buf;
            if (j != i)
            {
                printf("error: read the wrong value\n");
                exit(1);
            }
            exit(0);
        }
        if (write(fds[1], &i, sizeof(i)) != sizeof(i))
        {
            printf("error: write failed\n");
            exit(-1);
        }
    }

    int xstatus = 0;
    for (int i = 0; i < 4; i++)
    {
        wait(&xstatus);
        if (xstatus != 0)
        {
            exit(1);
        }
    }

    if (buf[0] != 99)
    {
        printf("error: child overwrote parent\n");
        exit(1);
    }

    printf("test passed\n");
}

int main(int argc, char *argv[])
{
    simpletest();

    // check that the first simpletest() freed the physical memory.
    simpletest();

    threetest();
    threetest();
    threetest();

    filetest();

    printf("ALL COW TESTS PASSED\n");

    exit(0);
}