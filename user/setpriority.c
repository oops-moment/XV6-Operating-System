#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/param.h"
#include "user/user.h"
#include "kernel/fs.h"
#include "kernel/fcntl.h"

int check_valid(int no_arguments)
{
    if (no_arguments < 3)
    {
        printf("TOO LESS ARGUMENtS\n");
        return -1;
    }
    return 1;
}

int main(int argc, char *argv[])
{

    int check = check_valid(argc);

    if (check == -1)
    {
        return -1;
    }

    int priority = atoi(argv[1]);
    int pid = atoi(argv[2]);

    int new_priority = set_priority(priority, pid);

    if (new_priority < 0)
    {
        printf("Invalid Priority\n");
        return -1;
    }

    else if (new_priority == 101)
    {
        printf("Invalid pid\n");
        return -1;
    }

    return 0;
}