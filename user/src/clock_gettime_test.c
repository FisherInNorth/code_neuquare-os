#define USER
#include "stddef.h"
#include "unistd.h"
#include "stdio.h"
#include "string.h"
#include "stdlib.h"


#define SECS_IN_DAY (24 * 60 * 60)
#define CLOCK_REALTIME 0


static void displayClock(clockid_t clock, const char *name)
{
    struct timespec ts;

    if (clock_gettime(clock, &ts) == -1) {
        exit(EXIT_FAILURE);
    }
    printf("%s: %d.%d\n", name, (intmax_t) ts.ts_sec, ts.ts_nsec / 1000000);

    long days = ts.ts_sec / SECS_IN_DAY;
    if (days > 0)
        printf("%d days + ", days);

    printf("%dh %dm %ds",
            (int) (ts.ts_sec % SECS_IN_DAY) / 3600,
            (int) (ts.ts_sec % 3600) / 60,
            (int) ts.ts_sec % 60);
    printf("\n");
}

int main(int argc, char *argv[])
{

    displayClock(CLOCK_REALTIME, "CLOCK_REALTIME");
    // displayClock(CLOCK_MONOTONIC, "CLOCK_MONOTONIC");
    // displayClock(CLOCK_BOOTTIME, "CLOCK_BOOTTIME");
    exit(EXIT_SUCCESS);
    return 0;
}