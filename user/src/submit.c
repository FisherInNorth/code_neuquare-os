#define USER
#include "stddef.h"
#include "unistd.h"
#include "stdio.h"
#include "string.h"

// char *argv[] = {"/busybox/busybox", "sh", 0};
char *envp[] = {
    "PATH=/",
    "LD_LIBRARY_PATH=/",
    "ENOUGH=3000",
    "TIMING_O=7",
    "LOOP_O=0",
    NULL,
};

#define CONSOLE 1
#define DEV_NULL 2
#define DEV_ZERO 3
#define DEV_RTC 3
#define DEV_CPU_DMA_LATENCY 0
#define DEV_URANDOM 4
#define AT_FDCWD -100

#define CHECK(c, ...) ((c) ? 1 : (printf(#c "fail" __VA_ARGS__), exit(-1)))

void runtest();


int main(void) {
    print_vma();
    mkdir("/dev", 0666);
    if (openat(AT_FDCWD, "/dev/tty", O_RDWR) < 0) {
        mknod("/dev/tty", S_IFCHR, CONSOLE << 8);
        openat(AT_FDCWD, "/dev/tty", O_RDWR);
    }

    dup(0); // stdout
    dup(0); // stderr

    // after we create tty, we can use printf
    printf("submit start\n");
    // CHECK(0 + 0, "%s\n", "test");
    CHECK(mkdir("/proc", 0666) == 0);
    CHECK(mkdir("/var", 0666) == 0);
    CHECK(mkdir("/var/tmp", 0666) == 0);
    CHECK(mkdir("/var/tmp/lmbench", 0666) == 0);
    CHECK(mkdir("/proc/mounts", 0666) == 0);
    CHECK(openat(AT_FDCWD, "/proc/meminfo", O_RDWR | O_CREAT) > 0);
    CHECK(mkdir("/tmp", 0666) == 0);
    CHECK(mknod("/dev/null", S_IFCHR, DEV_NULL << 8) == 0);
    CHECK(mknod("/dev/zero", S_IFCHR, DEV_ZERO << 8) == 0);
    CHECK(mknod("/dev/cpu_dma_latency", S_IFCHR, DEV_CPU_DMA_LATENCY << 8) == 0);
    CHECK(mkdir("/dev/shm", 0666) == 0);
    CHECK(mkdir("/dev/misc", 0666) == 0);
    CHECK(mknod("/dev/misc/rtc", S_IFCHR, DEV_RTC << 8) == 0);

    printf("ready to run test\n");
    runtest();
    shutdown();
    return 0;
}

char *testpath[] = {
    "./time-test",
    "busybox_testcode.sh",
    "libctest_testcode.sh",
    "iozone_testcode.sh",
    "lua_testcode.sh",
    "libc-bench",
    "unixbench_testcode.sh",
    "lmbench_testcode.sh",
    "cyclictest_testcode.sh",
};

// char *testpath[] = {"./cyclictest_testcode.sh", "libc-bench"};
// char *testpath[] = {"libctest_testcode.sh"};
// char *testpath[] = {"unixbench_testcode.sh"};

// number of elements in fixed-size array
#define NELEM(x) (sizeof(x) / sizeof((x)[0]))

void runtest() {
    int pid;
    
    // while(1) {
    for (int i = 0; i < NELEM(testpath); i++) {
        pid = fork();
        if (pid < 0) {
            printf("fork failed\n");
            exit(-1);
        }
        if (pid == 0) {
            execve(testpath[i], NULL, envp);
            printf("never reach here");
            exit(-1);
        } else {
            while (1) {
                if (wait(0) < 0) {
                    break;
                }
            }
        }
    }
    // }
}
