#include "kernel/param.h"
#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

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
        return 0;
    }

    int mask_value = atoi(argv[1]);
    trace(mask_value);

    exec(argv[2], &argv[2]);
    
    exit(0);
}
