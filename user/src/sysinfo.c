#define USER
#include "stddef.h"
#include "unistd.h"
#include "stdio.h"
#include "string.h"
#include "stdlib.h"

int main()
{
    struct sysinfo info;
    if (sysinfo(&info) == -1)
        printf("error : sysinfo\n");

    printf("Uptime: %d seconds\n", info.uptime);
    printf("Total RAM: %d bytes\n", info.totalram);
    printf("Free RAM: %d bytes\n", info.freeram);
    printf("Number of processes: %d\n", info.procs);

    return 0;
}