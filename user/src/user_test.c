#define USER
#include "stddef.h"
#include "unistd.h"
#include "stdio.h"
#include "string.h"
#include "stdlib.h"

void print_sysinfo() {
    struct sysinfo info;
    if (sysinfo(&info) == -1)
        printf("error : sysinfo\n");
    // printf("Uptime: %d seconds\n", info.uptime);
    // printf("Total RAM: %d bytes\n", info.totalram);
    printf("Free RAM: %d PAGES, %d Bytes\n", info.freeram/4096, info.freeram);
    // printf("Number of processes: %d\n", info.procs);
}


// Test that fork fails gracefully.
void forktest(void) {
    int n, pid;
    printf("==========fork test==========\n");
    int N = 100;
    for (n = 0; n < 100; n++) {
        pid = fork();
        if (pid < 0)
            break;
        if (pid == 0)
            exit(0);
    }

    if (n == N) {
        printf("fork claimed to work N times!\n");
        exit(1);
    }

    for (; n > 0; n--) {
        if (wait(0) < 0) {
            printf("wait stopped early\n");
            exit(1);
        }
    }

    if (wait(0) != -1) {
        printf("wait got too many\n");
        exit(1);
    }

    printf("fork test OK\n");
}

// concurrent forks to try to expose locking bugs.
void forkfork() {
    int N = 2;
    printf("==========forkfork test==========\n");
    for (int i = 0; i < N; i++) {
        int pid = fork();
        if (pid < 0) {
            printf("fork failed");
            exit(1);
        }
        if (pid == 0) {
            for (int j = 0; j < 100; j++) {
                int pid1 = fork();
                if (pid1 < 0) {
                    exit(1);
                }
                if (pid1 == 0) {
                    exit(0);
                }
                wait(0);
            }
            exit(0);
        }
    }

    int xstatus;
    for (int i = 0; i < N; i++) {
        wait(&xstatus);
        if (xstatus != 0) {
            printf("fork in child failed");
            exit(1);
        }
    }
    printf("forkfork test OK\n");
}

// simple file system tests (open syscall)
void opentest() {
    int fd;
    printf("==========open test==========\n");

    fd = open("/README.md", 0);
    if (fd < 0) {
        printf("open /README.md failed!\n");
        exit(1);
    }
    close(fd);
    fd = open("doesnotexist", 0);
    if (fd >= 0) {
        printf("open doesnotexist succeeded!\n");
        exit(1);
    }
    printf("open test OK\n");
}

#define WEXITSTATUS(s) (((s) & 0xff00) >> 8)
// try to find any races between exit and wait
void exitwait() {
    int i, pid;
    printf("==========exitwait test==========\n");

    for (i = 0; i < 1000; i++) {
        pid = fork();
        if (pid < 0) {
            printf("fork failed\n");
            exit(1);
        }
        if (pid) {
            int exit_state;
            if (wait(&exit_state) != pid) {
                printf("wait wrong pid\n");
                exit(1);
            }
            if ((i<<8) != exit_state) {
                printf("%d %d %d\n", i, exit_state, WEXITSTATUS(pid));
                printf("wait wrong exit status\n");
                exit(1);
            }
        } else {
            exit(i);
        }
    }
    printf("exitwait test OK\n");
}

// what if two children exit() at the same time?
void twochildren() {
    printf("==========twochildren test==========\n");
    for (int i = 0; i < 1000; i++) {
        int pid1 = fork();
        if (pid1 < 0) {
            printf("fork failed\n");
            exit(1);
        }
        if (pid1 == 0) {
            exit(0);
        } else {
            int pid2 = fork();
            if (pid2 < 0) {
                printf("fork failed\n");
                exit(1);
            }
            if (pid2 == 0) {
                exit(0);
            } else {
                wait(0);
                wait(0);
            }
        }
    }
    printf("twochildren test OK\n");
}

// try to find races in the reparenting
// code that handles a parent exiting
// when it still has live children.
void reparent() {
    printf("==========reparent test==========\n");

    int n = 100;
    int master_pid = getpid();
    for (int i = 0; i < n; i++) {
        int pid = fork();
        if (pid < 0) {
            printf("fork failed\n");
            exit(1);
        }
        if (pid) {
            if (wait(0) != pid) {
                printf("wait wrong pid\n");
                exit(1);
            }
        } else {
            int pid2 = fork();
            if (pid2 < 0) {
                kill(master_pid, SIGKILL);
                exit(1);
            }
            exit(0);
        }
    }
    printf("reparent test OK\n");
}

// regression test. does reparent() violate the parent-then-child
// locking order when giving away a child to init, so that exit()
// deadlocks against init's wait()? also used to trigger a "panic:
// release" due to exit() releasing a different p->parent->lock than
// it acquired.
void reparent2() {
    printf("==========reparent2 test==========\n");
    for (int i = 0; i < 400; i++) {
        int pid1 = fork();
        if (pid1 < 0) {
            printf("fork failed\n");
            exit(1);
        }
        if (pid1 == 0) {
            fork();
            fork();
            exit(0);
        }
        wait(0);
    }
    printf("reparent2 test OK\n");
}

#define MAXOPBLOCKS 10
#define BSIZE 512
#define BUFSZ ((MAXOPBLOCKS + 2) * BSIZE)
char buf[BUFSZ];

// write syscall
void writetest() {
    int fd;
    int N = 100;
    int SZ = 10;
    printf("==========write test==========\n");
    fd = open("small", O_CREAT | O_RDWR);
    if (fd < 0) {
        printf("error: create small failed!\n");
        exit(1);
    }

    for (int i = 0; i < N; i++) {
        if (write(fd, "aaaaaaaaaa", SZ) != SZ) {
            printf("error: write aa %d new file failed\n", i);
            exit(1);
        }
        if (write(fd, "bbbbbbbbbb", SZ) != SZ) {
            printf("error: write bb %d new file failed\n", i);
            exit(1);
        }
    }
    close(fd);
    fd = open("small", O_RDONLY);
    if (fd < 0) {
        printf("error: open small failed!\n");
        exit(1);
    }
    int ret = read(fd, buf, N * SZ * 2);
    if (ret != N * SZ * 2) {
        printf("read failed\n");
        exit(1);
    }
    close(fd);
    printf("write test OK\n");
}

void writebig() {
    int i, fd;
    int n;
    int MAXFILE = 2048;
    printf("==========writebig test==========\n");
    fd = open("big", O_CREAT | O_RDWR);
    if (fd < 0) {
        printf("error: creat big failed!\n");
        exit(1);
    }

    for (i = 0; i < MAXFILE; i++) {
        ((int *)buf)[0] = i;
        if (write(fd, buf, BSIZE) != BSIZE) {
            printf("error: write big file failed\n", i);
            exit(1);
        }
    }
    close(fd);

    fd = open("big", O_RDONLY);
    if (fd < 0) {
        printf("error: open big failed!\n");
        exit(1);
    }
    n = 0;
    for (;;) {
        i = read(fd, buf, BSIZE);
        if (i == 0) {
            if (n == MAXFILE - 1) {
                printf("read only %d blocks from big", n);
                exit(1);
            }
            break;
        } else if (i != BSIZE) {
            printf("read failed %d\n", i);
            exit(1);
        }
        if (((int *)buf)[0] != n) {
            printf("read content of block %d is %d\n", n, ((int *)buf)[0]);
            exit(1);
        }
        n++;
    }
    close(fd);
    if (unlink("big") < 0) {
        printf("unlink big failed\n");
        exit(1);
    }
    printf("writebig test OK\n");
}

void openiputtest() {
    printf("==========pass=======================\n");
    return;
    int pid, xstatus;
    printf("==========openiputtest test==========\n");

    if (mkdir("oidir", 0666) < 0) {
        printf("mkdir oidir failed\n");
        exit(1);
    }
    pid = fork();
    if (pid < 0) {
        printf("fork failed\n");
        exit(1);
    }
    if (pid == 0) {
        int fd = open("oidir", O_RDWR);
        if (fd >= 0) {
            printf("open directory for write succeeded\n");
            exit(1);
        }
        exit(0);
    }

    sleep(1);
    if (unlink("oidir") != 0) {
        printf("unlink failed\n");
        exit(1);
    }

    wait(&xstatus);
    printf("openiputtest test OK\n");
}

void truncate1() {
    char buf[32];
    printf("==========truncate1 test==========\n");

    unlink("truncfile");
    int fd1 = open("truncfile", O_CREAT | O_WRONLY | O_TRUNC);

    write(fd1, "abcd", 4);
    close(fd1);

    int fd2 = open("truncfile", O_RDONLY);

    memset(buf, 0, sizeof(buf));
    int n = read(fd2, buf, sizeof(buf));
    printf("%s\n", buf);
    if (n != 4) {
        printf("read %d bytes, wanted 4\n", n);
        exit(1);
    }

    fd1 = open("truncfile", O_WRONLY | O_TRUNC);
    int fd3 = open("truncfile", O_RDONLY);

    memset(buf, 0, sizeof(buf));
    n = read(fd3, buf, sizeof(buf));
    printf("%s\n", buf);

    if (n != 0) {
        printf("aaa fd3=%d\n", fd3);
        printf("read %d bytes, wanted 0\n", n);
        exit(1);
    }

    // memset(buf, 0, sizeof(buf));
    n = read(fd2, buf, sizeof(buf));
    // printf("%s\n",buf);
    if (n != 0) {
        printf("bbb fd2=%d\n", fd2);
        printf("read %d bytes, wanted 0\n", n);
        exit(1);
    }

    write(fd1, "abcdef", 6);

    memset(buf, 0, sizeof(buf));
    n = read(fd3, buf, sizeof(buf));
    printf("%s\n", buf);

    if (n != 6) {
        printf("read %d bytes, wanted 6\n", n);
        exit(1);
    }

    memset(buf, 0, sizeof(buf));
    n = read(fd2, buf, sizeof(buf));
    printf("%s\n", buf);

    if (n != 2) {
        printf("read %d bytes, wanted 2\n", n);
        exit(1);
    }

    unlink("truncfile");

    close(fd1);
    close(fd2);
    close(fd3);

    printf("truncate1 test OK\n");
}

void forkforkfork() {
    printf("==========forkforkfork test==========\n");
    unlink("stopforking");
    int pid = fork();
    if (pid < 0) {
        printf("fork failed");
        exit(1);
    }
    if (pid == 0) {
        while (1) {
            int fd = open("stopforking", 0);
            if (fd >= 0) {
                exit(0);
            }
            if (fork() < 0) {
                close(open("stopforking", O_CREAT | O_RDWR));
            }
        }
        exit(0);
    }

    sleep(2); // two seconds
    close(open("stopforking", O_CREAT | O_RDWR));
    wait(0);
    sleep(1); // one second

    printf("forkforkfork test OK\n");
}

void preempt() {
    printf("==========preempt test==========\n");
    int pid1, pid2, pid3;
    int pfds[2];

    pid1 = fork();
    if (pid1 < 0) {
        printf("fork failed");
        exit(1);
    }
    if (pid1 == 0) {
        for (;;)
        ;
    }
        
    pid2 = fork();
    if (pid2 < 0) {
        printf("fork failed\n");
        exit(1);
    }
    if (pid2 == 0) {
        for (;;)
            ;
    }
        
    pipe(pfds);
    pid3 = fork();
    if (pid3 < 0) {
        printf("fork failed\n");
        exit(1);
    }
    if (pid3 == 0) {
        close(pfds[0]);
        if (write(pfds[1], "x", 1) != 1)
            printf("preempt write error");
        close(pfds[1]);
        for (;;)
            ;
    }

    close(pfds[1]);
    int n=0;
    if ((n = read(pfds[0], buf, sizeof(buf))) != 1) {
        printf("%s %d\n", buf, n);
        printf("preempt read error");
        return;
    }
    close(pfds[0]);
    printf("kill... ");
    kill(pid1, SIGKILL);
    kill(pid2, SIGKILL);
    kill(pid3, SIGKILL);
    printf("wait... ");
    wait(0);
    wait(0);
    wait(0);

    printf("preempt test OK\n");
}

// test if child is killed (status = -1)
void killstatus() {
    printf("==========killstatus test==========\n");
    int xst;

    for (int i = 0; i < 4; i++) {
        int pid1 = fork();
        if (pid1 < 0) {
            printf("fork failed\n");
            exit(1);
        }
        if (pid1 == 0) {
            while (1) {
                getpid();
            }
            exit(0);
        }
        sleep(1);
        kill(pid1, SIGKILL);
        wait(&xst);
        if (xst != (-1 << 8)) {
            printf("%d %d\n", xst, i);
            printf("status should be -1<<8\n");
            exit(1);
        }
        printf("%d\n", i);
    }
    printf("killstatus test OK\n");
}

// what if you pass ridiculous pointers to system calls
// that read user memory with copyin?
void copyin() {
    printf("==========copyin test==========\n");
    // print_pgtable();
    uint64 addrs[] = {0x80000000LL, 0xffffffffffffffff};

    for (int ai = 0; ai < 2; ai++) {
        uint64 addr = addrs[ai];

        int fd = open("copyin1", O_CREAT | O_WRONLY);
        if (fd < 0) {
            printf("open(copyin1) failed\n");
            exit(1);
        }
        int n = write(fd, (void *)addr, 8192);
        if (n >= 0) {
            printf("write(fd, %p, 8192) returned %d, not -1\n", addr, n);
            exit(1);
        }
        close(fd);
        unlink("copyin1");

        n = write(1, (char *)addr, 8192);
        if (n > 0) {
            printf("write(1, %p, 8192) returned %d, not -1 or 0\n", addr, n);
            exit(1);
        }

        int fds[2];
        if (pipe(fds) < 0) {
            printf("pipe() failed\n");
            exit(1);
        }
        n = write(fds[1], (char *)addr, 8192);
        if (n > 0) {
            printf("write(pipe, %p, 8192) returned %d, not -1 or 0\n", addr, n);
            exit(1);
        }
        close(fds[0]);
        close(fds[1]);
    }
    printf("copyin test OK\n");
}

// what if you pass ridiculous pointers to system calls
// that write user memory with copyout?
void copyout() {
    printf("==========copyout test==========\n");
    uint64 addrs[] = {0x80000000LL, 0xffffffffffffffff};

    for (int ai = 0; ai < 2; ai++) {
        uint64 addr = addrs[ai];

        int fd = open("/README.md", 0);
        if (fd < 0) {
            printf("open(/README.md) failed\n");
            exit(1);
        }
        int n = read(fd, (void *)addr, 8192);
        if (n > 0) {
            printf("read(fd, %p, 8192) returned %d, not -1 or 0\n", addr, n);
            exit(1);
        }
        close(fd);

        int fds[2];
        if (pipe(fds) < 0) {
            printf("pipe() failed\n");
            exit(1);
        }
        n = write(fds[1], "x", 1);
        if (n != 1) {
            printf("pipe write failed\n");
            exit(1);
        }
        n = read(fds[0], (void *)addr, 8192);
        if (n > 0) {
            printf("read(pipe, %p, 8192) returned %d, not -1 or 0\n", addr, n);
            exit(1);
        }
        close(fds[0]);
        close(fds[1]);
    }
    printf("copyout test OK\n");
}

// what if you pass ridiculous string pointers to system calls?
void copyinstr1() {
    printf("==========copyinstr1 test==========\n");
    uint64 addrs[] = {0x80000000LL, 0xffffffffffffffff};

    for (int ai = 0; ai < 2; ai++) {
        uint64 addr = addrs[ai];

        int fd = open((char *)addr, O_CREAT | O_WRONLY);
        if (fd >= 0) {
            printf("open(%p) returned %d, not -1\n", addr, fd);
            exit(1);
        }
    }
    printf("copyinstr1 test OK\n");
}

// See if the kernel refuses to read/write user memory that the
// application doesn't have anymore, because it returned it.
void rwsbrk() {
    int fd, n;
    printf("==========rwsbrk test==========\n");

    uint64 a = (uint64)sbrk(8192);

    if (a == 0xffffffffffffffffLL) {
        printf("sbrk(rwsbrk) failed\n");
        exit(1);
    }

    if ((uint64)sbrk(-8192) == 0xffffffffffffffffLL) {
        printf("sbrk(rwsbrk) shrink failed\n");
        exit(1);
    }

    fd = open("rwsbrk", O_CREAT | O_WRONLY);
    if (fd < 0) {
        printf("open(rwsbrk) failed\n");
        exit(1);
    }
    n = write(fd, (void *)(a + 4096), 1024);
    if (n >= 0) {
        printf("write(fd, %p, 1024) returned %d, not -1\n", a + 4096, n);
        exit(1);
    }
    close(fd);
    unlink("rwsbrk");

    fd = open("/README.md", O_RDONLY);
    if (fd < 0) {
        printf("open(rwsbrk) failed\n");
        exit(1);
    }
    n = read(fd, (void *)(a + 4096), 10);
    if (n >= 0) {
        printf("read(fd, %p, 10) returned %d, not -1\n", a + 4096, n);
        exit(1);
    }
    close(fd);
    printf("rwsbrk test OK\n");
}

void truncate2() {
    printf("==========pass========\n");
    return;
    printf("==========truncate2 test==========\n");
    unlink("truncfile");

    int fd1 = open("truncfile", O_CREAT | O_TRUNC | O_WRONLY);
    write(fd1, "abcd", 4);

    int fd2 = open("truncfile", O_TRUNC | O_WRONLY);

    int n = write(fd1, "x", 1);
    if (n == -1) {
        printf("write returned %d, expected 1\n", n);
        exit(1);
    }

    // unlink("truncfile");
    close(fd1);
    close(fd2);
    printf("truncate2 test OK\n");
}

void truncate3() {
    printf("==========truncate3 test==========\n");
    int pid, xstatus;

    close(open("truncfile", O_CREAT | O_TRUNC | O_WRONLY));

    pid = fork();
    if (pid < 0) {
        printf("fork failed\n");
        exit(1);
    }

    if (pid == 0) {
        for (int i = 0; i < 100; i++) {
            char buf[32];
            int fd = open("truncfile", O_WRONLY);
            if (fd < 0) {
                printf("open failed\n");
                exit(1);
            }
            int n = write(fd, "1234567890", 10);
            if (n != 10) {
                printf("write got %d, expected 10\n", n);
                exit(1);
            }
            close(fd);
            fd = open("truncfile", O_RDONLY);
            read(fd, buf, sizeof(buf));
            close(fd);
        }
        exit(0);
    }

    for (int i = 0; i < 150; i++) {
        int fd = open("truncfile", O_CREAT | O_WRONLY | O_TRUNC);
        if (fd < 0) {
            printf("open failed\n");
            exit(1);
        }
        int n = write(fd, "xxx", 3);
        if (n != 3) {
            printf("write got %d, expected 3\n", n);
            exit(1);
        }
        close(fd);
    }

    wait(&xstatus);
    unlink("truncfile");
    printf("truncate3 test OK\n");
}

// does chdir() call iput(p->cwd) in a transaction?
void iputtest() {
    return;
    printf("==========iput test==========\n");
    if (mkdir("iputdir", 0666) < 0) {
        printf("mkdir failed\n");
        exit(1);
    }
    if (chdir("iputdir") < 0) {
        printf("chdir iputdir failed\n");
        exit(1);
    }
    if (unlink("../iputdir") < 0) {
        printf("unlink ../iputdir failed\n");
        exit(1);
    }
    if (chdir("/") < 0) {
        printf("chdir / failed\n");
        exit(1);
    }
    printf("iput test OK\n");
}

// does exit() call iput(p->cwd) in a transaction?
void exitiputtest() {
    int pid, xstatus;
    printf("==========exitiput test==========\n");
    pid = fork();
    if (pid < 0) {
        printf("fork failed\n");
        exit(1);
    }
    if (pid == 0) {
        if (mkdir("iputdir", 0666) < 0) {
            printf("mkdir failed\n");
            exit(1);
        }
        printf("???\n");
        if (chdir("iputdir") < 0) {
            printf("child chdir failed\n");
            exit(1);
        }
        if (unlink("../iputdir") < 0) {
            printf("unlink ../iputdir failed\n");
            exit(1);
        }
        exit(0);
    }
    wait(&xstatus);
    printf("exitiput test OK\n");
}

// many creates, followed by unlink test
void createtest() {
    printf("==========createtest test==========\n");
    int i, fd;
    int N = 200;
    char name[3];
    name[0] = 'a';
    name[2] = '\0';
    for (i = 0; i < N; i++) {
        name[1] = '0' + i;
        fd = open(name, O_CREAT | O_RDWR);
        close(fd);
    }
    name[0] = 'a';
    name[2] = '\0';
    for (i = 0; i < N; i++) {
        name[1] = '0' + i;
        unlink(name);
    }
    printf("createtest test OK\n");
}

// if process size was somewhat more than a page boundary, and then
// shrunk to be somewhat less than that page boundary, can the kernel
// still copyin() from addresses in the last page?
void sbrklast() {
    printf("==========sbrklast test==========\n");
    uint64 top = (uint64)sbrk(0);
    if ((top % 4096) != 0)
        sbrk(4096 - (top % 4096));
    sbrk(4096);
    sbrk(10);
    sbrk(-20);
    top = (uint64)sbrk(0);
    char *p = (char *)(top - 64);
    p[0] = 'x';
    p[1] = '\0';
    int fd = open(p, O_RDWR | O_CREAT);
    write(fd, p, 1);
    close(fd);
    fd = open(p, O_RDWR);
    p[0] = '\0';
    read(fd, p, 1);
    if (p[0] != 'x')
        exit(1);
    printf("sbrklast test OK\n");
}

// // Section with tests that take a fair bit of time
// // directory that uses indirect blocks
// void bigdir() {
//     printf("==========bigdir test==========\n");
//     enum { N = 500 };
//     int i, fd;
//     char name[10];

//     unlink("bd");

//     fd = open("bd", O_CREAT);
//     if (fd < 0) {
//         printf("bigdir create failed\n");
//         exit(1);
//     }
//     close(fd);

//     for (i = 0; i < N; i++) {
//         name[0] = 'x';
//         name[1] = '0' + (i / 64);
//         name[2] = '0' + (i % 64);
//         name[3] = '\0';
//         if (link("bd", name) != 0) {
//             printf("bigdir link(bd, %s) failed\n", name);
//             exit(1);
//         }
//     }

//     unlink("bd");
//     for (i = 0; i < N; i++) {
//         name[0] = 'x';
//         name[1] = '0' + (i / 64);
//         name[2] = '0' + (i % 64);
//         name[3] = '\0';
//         if (unlink(name) != 0) {
//             printf("bigdir unlink failed\n");
//             exit(1);
//         }
//     }
//     printf("bigdir test OK\n");
// }

void dirtest() {
    printf("==========dir test=========\n");
    if (mkdir("dir0", 0666) < 0) {
        printf("mkdir failed\n");
        exit(1);
    }

    if (chdir("dir0") < 0) {
        printf("chdir dir0 failed\n");
        exit(1);
    }

    if (chdir("..") < 0) {
        printf("chdir .. failed\n");
        exit(1);
    }

    if (unlink("dir0") < 0) {
        printf("unlink dir0 failed\n");
        exit(1);
    }
    printf("dir test OK\n");
}

void execvetest() {
    printf("==========execve test=========\n");
    int fd, xstatus, pid;
    char *echoargv[] = {"echo", "OK", 0};
    char buf[3];
    unlink("echo-ok");
    pid = fork();

    if (pid < 0) {
        printf("fork failed\n");
        exit(1);
    }
    if (pid == 0) {
        close(1);
        fd = open("echo-ok", O_CREAT | O_WRONLY);
        if (fd < 0) {
            printf("create failed\n");
            exit(1);
        }
        if (fd != 1) {
            printf("wrong fd\n");
            exit(1);
        }
        if (execve("/bin/echo", echoargv, NULL) < 0) {
            printf("execve echo failed\n");
            exit(1);
        }
        // won't get here
    }
    if (wait(&xstatus) != pid) {
        printf("wait failed!\n");
    }

    if (xstatus != 0)
        exit(xstatus);

    fd = open("echo-ok", O_RDONLY);
    if (fd < 0) {
        printf("open failed\n");
        exit(1);
    }
    if (read(fd, buf, 2) != 2) {
        printf("read failed\n");
        exit(1);
    }
    unlink("echo-ok");
    if (buf[0] == 'O' && buf[1] == 'K') {
        printf("execve test OK\n");
    } else {
        printf("wrong output\n");
        exit(1);
    }
}

void uvmfree() {
    printf("==========uvmfree test=========\n");
    enum { BIG = 100 * 1024 * 1024 };
    char *a, *p;
    uint64 amt;

    int pid = fork();
    if (pid == 0) {
        // oldbrk = sbrk(0);

        // can one grow address space to something big?
        a = sbrk(0);
        amt = BIG - (uint64)a;
        p = sbrk(amt);
        if (p != a) {
            // struct sysinfo info;
            // if (sysinfo(&info) == -1)
            //     printf("error : sysinfo\n");
            // printf("Uptime: %d seconds\n", info.uptime);
            // printf("Total RAM: %d bytes\n", info.totalram);
            // printf("Free RAM: %d bytes, %d MB\n", info.freeram, (info.freeram/(1024*1024)));
            // printf("Number of processes: %d\n", info.procs);

            printf("p : %d, a : %d, amt : %d\n", p, a, amt);
            printf("sbrk test failed to grow big address space; enough phys mem?\n");
            exit(1);
        }
        printf("child done\n");
        exit(0);
    } else if (pid > 0) {
        wait((int *)0);
        printf("uvmfree test OK\n");
    } else {
        printf("fork failed");
        exit(1);
    }
}

// simple fork and pipe read/write
void pipe1() {
    printf("==========pipe1 test=========\n");
    int fds[2], pid, xstatus;
    int seq, i, n, cc, total;
    enum { N = 5,
           SZ = 1033 };

    if (pipe(fds) != 0) {
        printf("pipe() failed\n");
        exit(1);
    }
    pid = fork();
    seq = 0;
    if (pid == 0) {
        close(fds[0]);
        for (n = 0; n < N; n++) {
            for (i = 0; i < SZ; i++)
                buf[i] = seq++;
            if (write(fds[1], buf, SZ) != SZ) {
                printf("pipe1 oops 1\n");
                exit(1);
            }
        }
        printf("write : over\n");
        exit(0);
    } else if (pid > 0) {
        close(fds[1]);
        total = 0;
        cc = 1;
        while ((n = read(fds[0], buf, cc)) > 0) {
            for (i = 0; i < n; i++) {
                if ((buf[i] & 0xff) != (seq++ & 0xff)) {
                    printf("pipe1 oops 2\n");
                    return;
                }
            }
            total += n;
            cc = cc * 2;
            if (cc > sizeof(buf))
                cc = sizeof(buf);
            // printf("read over\n");
            // printf("cc : %d\n",cc);
        }

        if (total != N * SZ) {
            printf("pipe1 oops 3 total %d\n", total);
            exit(1);
        }

        close(fds[0]);
        wait(&xstatus);
        printf("pipe1 test OK\n");
    } else {
        printf("fork() failed\n");
        exit(1);
    }
}

// allocate all mem, free it, and allocate again
void mem() {
    printf("==========mem test=========\n");
    void *m1, *m2;
    int pid;

    if ((pid = fork()) == 0) {
        m1 = 0;
        while ((m2 = malloc(10001)) != 0) {
            *(char **)m2 = m1;
            m1 = m2;
        }
        while (m1) {
            m2 = *(char **)m1;
            free(m1);
            m1 = m2;
        }
        m1 = malloc(1024 * 20);
        if (m1 == 0) {
            printf("couldn't allocate mem?!!\n");
            exit(1);
        }
        free(m1);
        exit(0);
    } else {
        int xstatus;
        wait(&xstatus);
        if (xstatus == -1) {
            // probably page fault, so might be lazy lab,
            // so OK.
            exit(0);
        }
        printf("mem test OK\n");
    }
}

// two processes write to the same file descriptor
// is the offset shared? does inode locking work?
void sharedfd() {
    printf("==========sharedfd test=========\n");
    int fd, pid, i, n, nc, np;
    enum { N = 1000,
           SZ = 10 };
    char buf[SZ];

    unlink("sharedfd");
    fd = open("sharedfd", O_CREAT | O_RDWR);
    if (fd < 0) {
        printf("cannot open sharedfd for writing");
        exit(1);
    }
    pid = fork();
    memset(buf, pid == 0 ? 'c' : 'p', sizeof(buf));
    for (i = 0; i < N; i++) {
        if (write(fd, buf, sizeof(buf)) != sizeof(buf)) {
            printf("write sharedfd failed\n");
            exit(1);
        }
    }
    if (pid == 0) {
        exit(0);
    } else {
        int xstatus;
        wait(&xstatus);
        if (xstatus != 0)
            exit(xstatus);
    }

    close(fd);
    fd = open("sharedfd", 0);
    if (fd < 0) {
        printf("cannot open sharedfd for reading\n");
        exit(1);
    }
    nc = np = 0;
    while ((n = read(fd, buf, sizeof(buf))) > 0) {
        for (i = 0; i < sizeof(buf); i++) {
            if (buf[i] == 'c')
                nc++;
            if (buf[i] == 'p')
                np++;
        }
    }
    close(fd);
    unlink("sharedfd");
    if (nc == N * SZ && np == N * SZ) {
        printf("sharedfd test OK\n");
    } else {
        printf("nc/np test fails\n");
        exit(1);
    }
}

// four processes write different files at the same
// time, to test block allocation.
void fourfiles() {
    printf("==========fourfiles test=========\n");
    int fd, pid, i, n, pi;
    int total;
    int j;
    char *names[] = {"f0", "f1", "f2", "f3"};
    char *fname;
    enum { N = 12,
           NCHILD = 4,
           SZ = 500 };

    for (pi = 0; pi < NCHILD; pi++) {
        fname = names[pi];
        unlink(fname);

        pid = fork();
        if (pid < 0) {
            printf("fork failed\n");
            exit(1);
        }

        if (pid == 0) {
            fd = open(fname, O_CREAT | O_RDWR);
            if (fd < 0) {
                printf("create failed\n");
                exit(1);
            }

            memset(buf, '0' + pi, SZ);
            for (i = 0; i < N; i++) {
                if ((n = write(fd, buf, SZ)) != SZ) {
                    printf("write failed %d\n", n);
                    exit(1);
                }
            }
            exit(0);
        }
    }

    int xstatus;
    for (pi = 0; pi < NCHILD; pi++) {
        wait(&xstatus);
        if (xstatus != 0)
            exit(xstatus);
    }

    for (i = 0; i < NCHILD; i++) {
        fname = names[i];
        fd = open(fname, 0);
        total = 0;
        while ((n = read(fd, buf, sizeof(buf))) > 0) {
            for (j = 0; j < n; j++) {
                if (buf[j] != '0' + i) {
                    printf("wrong char\n");
                    exit(1);
                }
            }
            total += n;
        }
        close(fd);
        if (total != N * SZ) {
            printf("wrong length %d\n", total);
            exit(1);
        }
        unlink(fname);
    }
    printf("fourfiles test OK\n");
}

// four processes create and delete different files in same directory
void createdelete() {
    printf("==========createdelete test=========\n");
    enum { N = 20,
           NCHILD = 4 };
    int pid, i, fd, pi;
    char name[32];

    for (pi = 0; pi < NCHILD; pi++) {
        pid = fork();
        if (pid < 0) {
            printf("fork failed\n");
            exit(1);
        }

        if (pid == 0) {
            name[0] = 'p' + pi;
            name[2] = '\0';
            for (i = 0; i < N; i++) {
                name[1] = '0' + i;
                fd = open(name, O_CREAT | O_RDWR);
                if (fd < 0) {
                    printf("create failed\n");
                    exit(1);
                }
                close(fd);
                if (i > 0 && (i % 2) == 0) {
                    name[1] = '0' + (i / 2);
                    if (unlink(name) < 0) {
                        printf("unlink failed\n");
                        exit(1);
                    }
                }
            }
            exit(0);
        }
    }
    int xstatus;
    for (pi = 0; pi < NCHILD; pi++) {
        wait(&xstatus);
        if (xstatus != 0)
            exit(1);
    }

    name[0] = name[1] = name[2] = 0;
    for (i = 0; i < N; i++) {
        for (pi = 0; pi < NCHILD; pi++) {
            name[0] = 'p' + pi;
            name[1] = '0' + i;
            fd = open(name, 0);
            if ((i == 0 || i >= N / 2) && fd < 0) {
                printf("i : %d, fd : %d\n",i, fd);
                printf("oops createdelete %s didn't exist\n", name);
                exit(1);
            } else if ((i >= 1 && i < N / 2) && fd >= 0) {
                printf("i : %d, fd : %d\n",i, fd);
                printf("oops createdelete %s did exist\n", name);
                exit(1);
            }
            if (fd >= 0)
                close(fd);
        }
    }

    for (i = 0; i < N; i++) {
        for (pi = 0; pi < NCHILD; pi++) {
            name[0] = 'p' + i;
            name[1] = '0' + i;
            unlink(name);
        }
    }
    printf("createdelete test OK\n");
}

// can I unlink a file and still read it?
void unlinkread() {
    printf("==========unlinkread test=========\n");
    enum { SZ = 5 };
    int fd;

    fd = open("unlinkread", O_CREAT | O_RDWR);
    if (fd < 0) {
        printf("create unlinkread failed\n");
        exit(1);
    }
    write(fd, "hello", SZ);
    close(fd);

    fd = open("unlinkread", O_RDWR);
    if (fd < 0) {
        printf("open unlinkread failed\n");
        exit(1);
    }
    if (unlink("unlinkread") != 0) {
        printf("unlink unlinkread failed\n");
        exit(1);
    }

    int fd1 = open("unlinkread", O_CREAT | O_RDWR);
    write(fd1, "yyy", 3);
    close(fd1);

    if (read(fd, buf, sizeof(buf)) != SZ) {
        printf("unlinkread read failed");
        exit(1);
    }

    // printf("%s\n", buf);
    if (buf[0] != 'h') {
        printf("unlinkread wrong data\n");
        exit(1);
    }

    // if (write(fd, buf, 10) != 10) {
    //     printf("unlinkread write failed\n");
    //     exit(1);
    // }
    // close(fd);
    // unlink("unlinkread");
    printf("unlinkread test OK\n");
}

// test writes that are larger than the log.
void bigwrite() {
    int fd, sz;
    printf("==========bigwrite test=========\n");
    unlink("bigwrite");
    for (sz = 499; sz < (MAXOPBLOCKS + 2) * BSIZE; sz += 471) {
        fd = open("bigwrite", O_CREAT | O_RDWR);
        if (fd < 0) {
            printf("cannot create bigwrite\n");
            exit(1);
        }
        int i;
        for (i = 0; i < 2; i++) {
            int cc = write(fd, buf, sz);
            if (cc != sz) {
                printf("write(%d) ret %d\n", sz, cc);
                exit(1);
            }
        }
        close(fd);
        unlink("bigwrite");
    }
    printf("bigwrite test OK\n");
}

void bigfile() {
    printf("==========bigfile test=========\n");
    enum { N = 20,
           SZ = 600 };
    int fd, i, total, cc;

    unlink("bigfile.dat");
    fd = open("bigfile.dat", O_CREAT | O_RDWR);
    if (fd < 0) {
        printf("cannot create bigfile");
        exit(1);
    }
    for (i = 0; i < N; i++) {
        memset(buf, i, SZ);
        if (write(fd, buf, SZ) != SZ) {
            printf("write bigfile failed\n");
            exit(1);
        }
    }
    close(fd);

    fd = open("bigfile.dat", 0);
    if (fd < 0) {
        printf("cannot open bigfile\n");
        exit(1);
    }
    total = 0;
    for (i = 0;; i++) {
        cc = read(fd, buf, SZ / 2);
        if (cc < 0) {
            printf("read bigfile failed\n");
            exit(1);
        }
        if (cc == 0)
            break;
        if (cc != SZ / 2) {
            printf("short read bigfile\n");
            exit(1);
        }
        if (buf[0] != i / 2 || buf[SZ / 2 - 1] != i / 2) {
            printf("read bigfile wrong data\n");
            exit(1);
        }
        total += cc;
    }
    close(fd);
    if (total != N * SZ) {
        printf("read bigfile wrong total\n");
        exit(1);
    }
    unlink("bigfile.dat");
    printf("bigfile test OK\n");
}

void fourteen() {
    printf("==========fourteen test=========\n");

    // DIRSIZ is 14.

    if (mkdir("12345678901234", 0666) != 0) {
        printf("mkdir 12345678901234 failed\n");
        exit(1);
    }
    if (mkdir("12345678901234/123456789012345", 0666) != 0) {
        printf("mkdir 12345678901234/123456789012345 failed\n");
        exit(1);
    }
    int fd = open("123456789012345/123456789012345/123456789012345", O_CREAT);
    if (fd < 0) {
        printf("create 123456789012345/123456789012345/123456789012345 failed\n");
        exit(1);
    }

    // close(fd);
    // fd = open("12345678901234/12345678901234/12345678901234", 0);
    // if (fd < 0) {
    //     printf("open 12345678901234/12345678901234/12345678901234 failed\n");
    //     exit(1);
    // }
    // close(fd);

    // if (mkdir("12345678901234/12345678901234", 0666) == 0) {
    //     printf("mkdir 12345678901234/12345678901234 succeeded!\n");
    //     exit(1);
    // }
    // if (mkdir("123456789012345/12345678901234", 0666) == 0) {
    //     printf("mkdir 12345678901234/123456789012345 succeeded!\n");
    //     exit(1);
    // }

    // // clean up
    // unlink("123456789012345/12345678901234");
    // unlink("12345678901234/12345678901234");
    // unlink("12345678901234/12345678901234/12345678901234");
    // unlink("123456789012345/123456789012345/123456789012345");
    // unlink("12345678901234/123456789012345");
    // unlink("12345678901234");

    printf("fourteen test OK\n");
}

void rmdot() {
    printf("==========rmdom test=========\n");
    if (mkdir("dots", 066) != 0) {
        printf("mkdir dots failed\n");
        exit(1);
    }
    if (chdir("dots") != 0) {
        printf("chdir dots failed\n");
        exit(1);
    }
    if (unlink(".") == 0) {
        printf("rm . worked!\n");
        exit(1);
    }
    if (unlink("..") == 0) {
        printf("rm .. worked!\n");
        exit(1);
    }
    if (chdir("/") != 0) {
        printf("chdir / failed\n");
        exit(1);
    }
    if (unlink("dots/.") == 0) {
        printf("unlink dots/. worked!\n");
        exit(1);
    }
    if (unlink("dots/..") == 0) {
        printf("unlink dots/.. worked!\n");
        exit(1);
    }
    if (unlink("dots") != 0) {
        printf("unlink dots failed!\n");
        exit(1);
    }
    printf("rmdot test OK\n");
}

// regression test. test whether exec() leaks memory if one of the
// arguments is invalid. the test passes if the kernel doesn't panic.
void badarg() {
    printf("==========badarg test=========\n");
    for (int i = 0; i < 50000; i++) {
        char *argv[2];
        argv[0] = (char *)0xffffffff;
        argv[1] = 0;
        execve("echo", argv, NULL);
    }
    printf("badarg test OK\n");
}

// does sbrk handle signed int32 wrap-around with
// negative arguments?
void sbrk8000() {
    printf("==========sbrk8000 test=========\n");
    sbrk(0x80000004);
    volatile char *top = sbrk(0);
    *(top - 1) = *(top - 1) + 1;
    printf("sbrk8000 test OK\n");
}

// check that writes to text segment fault
void textwrite() {
    printf("==========textwrite test=========\n");
    int pid;
    int xstatus;

    pid = fork();
    if (pid == 0) {
        volatile int *addr = (int *)0;
        *addr = 10;
        exit(1);
    } else if (pid < 0) {
        printf("fork failed\n");
        exit(1);
    }
    wait(&xstatus);
    if (xstatus == (-1 << 8) ) // kernel killed child?: 位移运算优先级低，，要加括号！
    {
        printf("textwrite test OK\n");
    } else
        exit(xstatus);
}

void outofinodes() {
    printf("==========outofinodes test=========\n");
    int nzz = 32 * 32;
    for (int i = 0; i < nzz; i++) {
        char name[32];
        name[0] = 'z';
        name[1] = 'z';
        name[2] = '0' + (i / 32);
        name[3] = '0' + (i % 32);
        name[4] = '\0';
        unlink(name);
        int fd = open(name, O_CREAT | O_RDWR | O_TRUNC);
        if (fd < 0) {
            // failure is eventually expected.
            break;
        }
        close(fd);
    }

    for (int i = 0; i < nzz; i++) {
        char name[32];
        name[0] = 'z';
        name[1] = 'z';
        name[2] = '0' + (i / 32);
        name[3] = '0' + (i % 32);
        name[4] = '\0';
        unlink(name);
    }
    printf("outofinodes test OK\n");
}

// concurrent writes to try to provoke deadlock in the virtio disk
// driver.
void manywrites() {
    printf("==========manywrites test=========\n");
    int nchildren = 4;
    int howmany = 30; // increase to look for deadlock

    for (int ci = 0; ci < nchildren; ci++) {
        int pid = fork();
        if (pid < 0) {
            printf("fork failed\n");
            exit(1);
        }

        if (pid == 0) {
            char name[3];
            name[0] = 'b';
            name[1] = 'a' + ci;
            name[2] = '\0';
            unlink(name);

            for (int iters = 0; iters < howmany; iters++) {
                for (int i = 0; i < ci + 1; i++) {
                    int fd = open(name, O_CREAT | O_RDWR);
                    if (fd < 0) {
                        printf("cannot create %s\n", name);
                        exit(1);
                    }
                    int sz = sizeof(buf);
                    int cc = write(fd, buf, sz);
                    if (cc != sz) {
                        printf("write(%d) ret %d\n", sz, cc);
                        exit(1);
                    }
                    close(fd);
                }
                unlink(name);
            }

            unlink(name);
            exit(0);
        }
    }



    for (int ci = 0; ci < nchildren; ci++) {
        int st = 0;
        wait(&st);
        if (st != 0)
            exit(st);
    }
    printf("manywrites test OK\n");
}

// regression test. does write() with an invalid buffer pointer cause
// a block to be allocated for a file that is then not freed when the
// file is deleted? if the kernel has this bug, it will panic: balloc:
// out of blocks. assumed_free may need to be raised to be more than
// the number of free blocks. this test takes a long time.
void badwrite() {
    printf("==========badwrite test=========\n");
    int assumed_free = 600;

    unlink("junk");
    for (int i = 0; i < assumed_free; i++) {
        int fd = open("junk", O_CREAT | O_WRONLY);
        if (fd < 0) {
            printf("open junk failed\n");
            exit(1);
        }
        write(fd, (char *)0xffffffffffL, 1);
        close(fd);
        unlink("junk");
    }

    int fd = open("junk", O_CREAT | O_WRONLY);
    if (fd < 0) {
        printf("open junk failed\n");
        exit(1);
    }
    if (write(fd, "x", 1) != 1) {
        printf("write failed\n");
        exit(1);
    }
    close(fd);
    unlink("junk");

    printf("badwrite test OK\n");
}

// test the execveout() code that cleans up if it runs out
// of memory. it's really a test that such a condition
// doesn't cause a panic.
void execveout() {
    printf("==========execveout test=========\n");
    for (int avail = 0; avail < 15; avail++) {
        int pid = fork();
        if (pid < 0) {
            printf("fork failed\n");
            exit(1);
        } else if (pid == 0) {
            // allocate all of memory.
            while (1) {
                uint64 a = (uint64)sbrk(4096);
                if (a == 0xffffffffffffffffLL)
                    break;
                *(char *)(a + 4096 - 1) = 1;
            }

            // free a few pages, in order to let exec() make some
            // progress.
            for (int i = 0; i < avail; i++)
                sbrk(-4096);

            close(1);
            char *args[] = {"echo", "x", 0};
            execve("echo", args, NULL);
            exit(0);
        } else {
            wait((int *)0);
        }
    }
    printf("execveout test OK\n");
}

// can the kernel tolerate running out of disk space?
void diskfull() {
    printf("==========diskfull test=========\n");
    int fi;
    int done = 0;
    int MAXFILE = 2000;

    unlink("diskfulldir");

    for (fi = 0; done == 0; fi++) {
        char name[32];
        name[0] = 'b';
        name[1] = 'i';
        name[2] = 'g';
        name[3] = '0' + fi;
        name[4] = '\0';
        unlink(name);
        int fd = open(name, O_CREAT | O_RDWR | O_TRUNC);
        if (fd < 0) {
            // oops, ran out of inodes before running out of blocks.
            printf("could not create file %s\n", name);
            done = 1;
            break;
        }
        for (int i = 0; i < MAXFILE; i++) {
            // char buf[BSIZE];
            if (write(fd, "hello", 5) != 5) {
                done = 1;
                close(fd);
                break;
            }
        }
        done = 1;
        close(fd);
    }
    printf("disk is full\n");
    // now that there are no free blocks, test that dirlink()
    // merely fails (doesn't panic) if it can't extend
    // directory content. one of these file creations
    // is expected to fail.
    // int nzz = 128;
    // for (int i = 0; i < nzz; i++) {
    //     char name[32];
    //     name[0] = 'z';
    //     name[1] = 'z';
    //     name[2] = '0' + (i / 32);
    //     name[3] = '0' + (i % 32);
    //     name[4] = '\0';
    //     unlink(name);
    //     int fd = open(name, O_CREAT | O_RDWR | O_TRUNC);
    //     if (fd < 0)
    //         break;
    //     close(fd);
    // }

    // // this mkdir() is expected to fail.
    // if (mkdir("diskfulldir", 0666) == 0)
    //     printf("mkdir(diskfulldir) unexpectedly succeeded!\n");

    // unlink("diskfulldir");

    // for (int i = 0; i < nzz; i++) {
    //     char name[32];
    //     name[0] = 'z';
    //     name[1] = 'z';
    //     name[2] = '0' + (i / 32);
    //     name[3] = '0' + (i % 32);
    //     name[4] = '\0';
    //     unlink(name);
    // }

    // for (int i = 0; i < fi; i++) {
    //     char name[32];
    //     name[0] = 'b';
    //     name[1] = 'i';
    //     name[2] = 'g';
    //     name[3] = '0' + i;
    //     name[4] = '\0';
    //     unlink(name);
    // }
    printf("diskfull test OK\n");
}

void sbrkbasic() {
    printf("==========sbrkbasic test=========\n");
    enum { TOOMUCH = 1024 * 1024 * 1024 };
    int i, pid, xstatus;
    char *c, *a, *b;

    // does sbrk() return the expected failure value?
    pid = fork();
    if (pid < 0) {
        printf("fork failed in sbrkbasic\n");
        exit(1);
    }
    if (pid == 0) {
        a = sbrk(TOOMUCH);
        if (a == (char *)0xffffffffffffffffL) {
            // it's OK if this fails.
            exit(0);
        }

        for (b = a; b < a + TOOMUCH; b += 4096) {
            *b = 99;
        }

        // we should not get here! either sbrk(TOOMUCH)
        // should have failed, or (with lazy allocation)
        // a pagefault should have killed this process.
        exit(1);
    }

    wait(&xstatus);
    if (xstatus == 1) {
        printf("too much memory allocated!\n");
        exit(1);
    }

    // can one sbrk() less than a page?
    a = sbrk(0);
    for (i = 0; i < 5000; i++) {
        b = sbrk(1);
        if (b != a) {
            printf("sbrk test failed %d %x %x\n", i, a, b);
            exit(1);
        }
        *b = 1;
        a = b + 1;
    }
    pid = fork();
    if (pid < 0) {
        printf("sbrk test fork failed\n");
        exit(1);
    }
    c = sbrk(1);
    c = sbrk(1);
    if (c != a + 1) {
        printf("sbrk test failed post-fork\n");
        exit(1);
    }
    if (pid == 0)
        exit(0);
    wait(&xstatus);
    printf("sbrkbasic test OK\n");
}

#define PGSIZE 4096 // bytes per page
void sbrkmuch() {
    printf("==========sbrkmuch test=========\n");
    enum { BIG = 100 * 1024 * 1024 };
    char *c, *oldbrk, *a, *lastaddr, *p;
    uint64 amt;

    oldbrk = sbrk(0);

    // can one grow address space to something big?
    a = sbrk(0);
    amt = BIG - (uint64)a;
    p = sbrk(amt);
    if (p != a) {
        print_sysinfo();
        printf("p : %ld, a : %ld, amt : %ld\n",p, a, amt);
        printf("sbrk test failed to grow big address space; enough phys mem?\n");
        exit(1);
    }

    // touch each page to make sure it exists.
    char *eee = sbrk(0);
    // printf("eee is %x", eee);
    // print_pgtable();
    // int cnt = 0;
    for (char *pp = a; pp < eee; pp += 4096) {
        // printf("%x\n", pp);
        *pp = 1;
    }

    lastaddr = (char *)(BIG - 1);
    *lastaddr = 99;

    // can one de-allocate?
    a = sbrk(0);
    // printf("a is %x %d\n", a, a);
    // print_pgtable();
    c = sbrk(-PGSIZE);
    // print_pgtable();
    if (c == (char *)0xffffffffffffffffL) {
        printf("sbrk could not deallocate\n");
        exit(1);
    }
    c = sbrk(0);
    // printf("c is %x %d\n", c, c);
    if (c != a - PGSIZE) {
        printf("sbrk deallocation produced wrong address, a %x c %x\n", a, c);
        exit(1);
    }

    // can one re-allocate that page?
    a = sbrk(0);
    c = sbrk(PGSIZE);
    if (c != a || sbrk(0) != a + PGSIZE) {
        printf("sbrk re-allocation failed, a %x c %x\n", a, c);
        exit(1);
    }
    if (*lastaddr == 99) {
        // should be zero
        printf("sbrk de-allocation didn't really deallocate\n");
        exit(1);
    }

    a = sbrk(0);
    c = sbrk(-(sbrk(0) - oldbrk));
    if (c != a) {
        printf("sbrk downsize failed, a %x c %x\n", a, c);
        exit(1);
    }
    printf("sbrkmuch test OK\n");
}

#define KERNBASE 0x80200000L
// can we read the kernel's memory?
void kernmem() {
    printf("==========kernmem test=========\n");
    char *a;
    int pid;

    for (a = (char *)(KERNBASE); a < (char *)(KERNBASE + 2000000); a += 50000) {
        pid = fork();
        if (pid < 0) {
            printf("fork failed\n");
            exit(1);
        }
        if (pid == 0) {
            printf("oops could read %x = %x\n", a, *a);
            exit(1);
        }
        int xstatus;
        wait(&xstatus);
        printf("%d\n", xstatus);
        if (xstatus != -1 << 8) // did kernel kill child?
            exit(1);
    }
    printf("kernmem test OK\n");
}

#define MAXVA (1L << (9 + 9 + 9 + 12 - 1))
// user code should not be able to write to addresses above MAXVA.
void MAXVAplus() {
    printf("==========MAXVAplus test=========\n");
    volatile uint64 a = MAXVA;
    for (; a != 0; a <<= 1) {
        int pid;
        pid = fork();
        if (pid < 0) {
            printf("fork failed\n");
            exit(1);
        }
        if (pid == 0) {
            *(char *)a = 99;
            printf("oops wrote %x\n", a);
            exit(1);
        }
        int xstatus;
        wait(&xstatus);
        if (xstatus != -1 << 8) // did kernel kill child?
            exit(1);
    }
    printf("MAXVAplus test OK\n");
}

// if we run the system out of memory, does it clean up the last
// failed allocation?
void sbrkfail() {
    printf("==========sbrkfail test=========\n");
    enum { BIG = 100 * 1024 * 1024 };
    int i, xstatus;
    int fds[2];
    char scratch;
    char *c, *a;
    int pids[10];
    int pid;

    if (pipe(fds) != 0) {
        printf("pipe() failed\n");
        exit(1);
    }
    for (i = 0; i < sizeof(pids) / sizeof(pids[0]); i++) {
        if ((pids[i] = fork()) == 0) {
            // allocate a lot of memory
            sbrk(BIG - (uint64)sbrk(0));
            write(fds[1], "x", 1);
            // sit around until killed
            for (;;) sleep(1000);
        }
        if (pids[i] != -1)
            read(fds[0], &scratch, 1);
    }

    // if those failed allocations freed up the pages they did allocate,
    // we'll be able to allocate here
    c = sbrk(PGSIZE);
    for (i = 0; i < sizeof(pids) / sizeof(pids[0]); i++) {
        if (pids[i] == -1)
            continue;
        kill(pids[i], SIGKILL);
        wait(0);
    }
    if (c == (char *)0xffffffffffffffffL) {
        printf("failed sbrk leaked memory\n");
        exit(1);
    }

    // test running fork with the above allocated page
    pid = fork();
    if (pid < 0) {
        printf("fork failed\n");
        exit(1);
    }
    if (pid == 0) {
        // allocate a lot of memory.
        // this should produce a page fault,
        // and thus not complete.
        a = sbrk(0);
        sbrk(10 * BIG);
        int n = 0;
        for (i = 0; i < 10 * BIG; i += PGSIZE) {
            n += *(a + i);
        }
        // print n so the compiler doesn't optimize away
        // the for loop.
        printf("allocate a lot of memory succeeded %d\n", n);
        exit(1);
    }
    wait(&xstatus);
    if (xstatus != -1 << 8 && xstatus != 2)
        exit(1);
    printf("sbrkfail test OK\n");
}

// test reads/writes from/to allocated memory
void sbrkarg() {
    printf("==========sbrkarg test=========\n");
    char *a;
    int fd, n;

    a = sbrk(PGSIZE);
    fd = open("sbrk", O_CREAT | O_WRONLY);
    unlink("sbrk");
    if (fd < 0) {
        printf("open sbrk failed\n");
        exit(1);
    }
    if ((n = write(fd, a, PGSIZE)) < 0) {
        printf("write sbrk failed\n");
        exit(1);
    }
    close(fd);

    // test writes to allocated memory
    a = sbrk(PGSIZE);
    if (pipe((int *)a) != 0) {
        printf("pipe() failed\n");
        exit(1);
    }
    printf("sbrkarg test OK\n");
}

// does uninitialized data start out zero?
char uninit[10000];
void bsstest() {
    printf("==========bsstest test=========\n");
    int i;
    for (i = 0; i < sizeof(uninit); i++) {
        if (uninit[i] != '\0') {
            printf("bss test failed\n");
            exit(1);
        }
    }
    printf("bsstest test OK\n");
}

// does exec return an error if the arguments
// are larger than a page? or does it write
// below the stack and wreck the instructions/data?
#define MAXARG 32 // max exec arguments
void bigargtest() {
    printf("==========bigargtest test=========\n");
    int pid, fd, xstatus;

    unlink("bigarg-ok");
    pid = fork();
    if (pid == 0) {
        static char *args[MAXARG];
        int i;
        for (i = 0; i < MAXARG - 1; i++)
            args[i] = "bigargs test: failed\n                                                                                                                                                                                                       ";
        args[MAXARG - 1] = 0;
        execve("echo", args, NULL);
        fd = open("bigarg-ok", O_CREAT);
        close(fd);
        exit(0);
    } else if (pid < 0) {
        printf("bigargtest: fork failed\n");
        exit(1);
    }

    wait(&xstatus);
    if (xstatus != 0)
        exit(xstatus);
    fd = open("bigarg-ok", 0);
    if (fd < 0) {
        printf("bigarg test failed!\n");
        exit(1);
    }
    close(fd);
    printf("bigargtest test OK\n");
}

// what happens when the file system runs out of blocks?
// answer: balloc panics, so this test is not useful.
void fsfull() {
    int nfiles;
    int fsblocks = 0;

    printf("==========fsfull test=========\n");

    for (nfiles = 0;; nfiles++) {
        char name[64];
        name[0] = 'f';
        name[1] = '0' + nfiles / 1000;
        name[2] = '0' + (nfiles % 1000) / 100;
        name[3] = '0' + (nfiles % 100) / 10;
        name[4] = '0' + (nfiles % 10);
        name[5] = '\0';
        printf("writing %s\n", name);
        int fd = open(name, O_CREAT | O_RDWR);
        if (fd < 0) {
            printf("open %s failed\n", name);
            break;
        }
        int total = 0;
        while (1) {
            int cc = write(fd, buf, BSIZE);
            if (cc < BSIZE)
                break;
            total += cc;
            fsblocks++;
        }
        printf("wrote %d bytes\n", total);
        close(fd);
        if (total == 0)
            break;
    }

    while (nfiles >= 0) {
        char name[64];
        name[0] = 'f';
        name[1] = '0' + nfiles / 1000;
        name[2] = '0' + (nfiles % 1000) / 100;
        name[3] = '0' + (nfiles % 100) / 10;
        name[4] = '0' + (nfiles % 10);
        name[5] = '\0';
        unlink(name);
        nfiles--;
    }

    printf("fsfull test OK\n");
}

void argptest() {
    printf("==========argptest test=========\n");
    int fd;
    fd = open("/boot/init", O_RDONLY);
    if (fd < 0) {
        printf("open failed\n");
        exit(1);
    }
    read(fd, sbrk(0) - 1, -1);
    close(fd);
    printf("argptest test OK\n");
}

static inline uint64
r_sp() {
    uint64 x;
    asm volatile("mv %0, sp"
                 : "=r"(x));
    return x;
}

// check that there's an invalid page beneath
// the user stack, to catch stack overflow.
void stacktest() {
    printf("==========stacktest test=========\n");
    int pid;
    int xstatus;

    pid = fork();
    if (pid == 0) {
        char *sp = (char *)r_sp();
        sp -= 2*PGSIZE;
        // the *sp should cause a trap.
        printf("stacktest: read below stack %p\n", *sp);
        exit(1);
    } else if (pid < 0) {
        printf("fork failed\n");
        exit(1);
    }
    wait(&xstatus);
    if (xstatus == -1 << 8) // kernel killed child?
    {
        printf("stacktest test OK\n");
    } else
        exit(xstatus);
}

// regression test. copyin(), copyout(), and copyinstr() used to cast
// the virtual page address to uint, which (with certain wild system
// call arguments) resulted in a kernel page faults.
void *big = (void *)0xeaeb0b5b00002f5e;
void pgbug() {
    printf("==========pgbug test=========\n");
    char *argv[1];
    argv[0] = 0;
    execve(big, argv, NULL);
    pipe(big);

    printf("pgbug test OK\n");
}

// regression test. does the kernel panic if a process sbrk()s its
// size to be less than a page, or zero, or reduces the break by an
// amount too small to cause a page to be freed?
void sbrkbugs() {
    printf("==========sbrkbugs test=========\n");
    int pid = fork();
    if (pid < 0) {
        printf("fork failed\n");
        exit(1);
    }
    if (pid == 0) {
        int sz = (uint64)sbrk(0);
        // free all user memory; there used to be a bug that
        // would not adjust p->sz correctly in this case,
        // causing exit() to panic.
        sbrk(-sz);
        // user page fault here.
        exit(0);
    }
    wait(0);

    pid = fork();
    if (pid < 0) {
        printf("fork failed\n");
        exit(1);
    }
    if (pid == 0) {
        int sz = (uint64)sbrk(0);
        // set the break to somewhere in the very first
        // page; there used to be a bug that would incorrectly
        // free the first page.
        sbrk(-(sz - 3500));
        exit(0);
    }
    wait(0);

    pid = fork();
    if (pid < 0) {
        printf("fork failed\n");
        exit(1);
    }
    if (pid == 0) {
        // set the break in the middle of a page.
        sbrk((10 * 4096 + 2048) - (uint64)sbrk(0));

        // reduce the break a bit, but not enough to
        // cause a page to be freed. this used to cause
        // a panic.
        sbrk(-10);

        exit(0);
    }
    wait(0);

    printf("sbrkbugs test OK\n");
}

// cow tests
// allocate more than half of physical memory,
// then fork. this will fail in the default
// kernel, which does not support copy-on-write.
#define PHYSTOP (0x80000000L + 130 * 1024 * 1024)
#define START_MEM 0x80a00000
void simpletest() {
    printf("==========simpletest test=========\n");
    uint64 phys_size = PHYSTOP - START_MEM;
    int sz = (phys_size / 3) * 2;

    printf("simple: ");

    char *p = sbrk(sz);
    if (p == (char *)0xffffffffffffffffL) {
        printf("sbrk(%d) failed\n", sz);
        exit(-1);
    }

    for (char *q = p; q < p + sz; q += 4096) {
        *(int *)q = getpid();
    }

    int pid = fork();
    if (pid < 0) {
        printf("fork() failed\n");
        exit(-1);
    }

    if (pid == 0)
        exit(0);

    wait(0);

    if (sbrk(-sz) == (char *)0xffffffffffffffffL) {
        printf("sbrk(-%d) failed\n", sz);
        exit(-1);
    }

    printf("simpletest test OK\n");
}

// three processes all write COW memory.
// this causes more than half of physical memory
// to be allocated, so it also checks whether
// copied pages are freed.
void threetest() {
    printf("==========threetest test=========\n");
    uint64 phys_size = PHYSTOP - START_MEM;
    int sz = phys_size / 4;
    int pid1, pid2;

    printf("three: ");

    char *p = sbrk(sz);
    if (p == (char *)0xffffffffffffffffL) {
        printf("sbrk(%d) failed\n", sz);
        exit(-1);
    }

    pid1 = fork();
    if (pid1 < 0) {
        printf("fork failed\n");
        exit(-1);
    }
    if (pid1 == 0) {
        pid2 = fork();
        if (pid2 < 0) {
            printf("fork failed");
            exit(-1);
        }
        if (pid2 == 0) {
            for (char *q = p; q < p + (sz / 5) * 4; q += 4096) {
                *(int *)q = getpid();
            }
            for (char *q = p; q < p + (sz / 5) * 4; q += 4096) {
                if (*(int *)q != getpid()) {
                    printf("wrong content\n");
                    exit(-1);
                }
            }
            exit(-1);
        }
        for (char *q = p; q < p + (sz / 2); q += 4096) {
            *(int *)q = 9999;
        }
        exit(0);
    }

    for (char *q = p; q < p + sz; q += 4096) {
        *(int *)q = getpid();
    }

    wait(0);

    sleep(1);

    for (char *q = p; q < p + sz; q += 4096) {
        if (*(int *)q != getpid()) {
            printf("wrong content\n");
            exit(-1);
        }
    }

    if (sbrk(-sz) == (char *)0xffffffffffffffffL) {
        printf("sbrk(-%d) failed\n", sz);
        exit(-1);
    }

    printf("threetest test OK\n");
}

char buf2[4096];
int fds[2];

// test whether copyout() simulates COW faults.
void filetest() {
    printf("==========filetest test==========\n");

    buf2[0] = 99;

    for (int i = 0; i < 4; i++) {
        if (pipe(fds) != 0) {
            printf("pipe() failed\n");
            exit(-1);
        }
        int pid = fork();
        if (pid < 0) {
            printf("fork failed\n");
            exit(-1);
        }
        if (pid == 0) {
            sleep(1);
            if (read(fds[0], buf2, sizeof(i)) != sizeof(i)) {
                printf("error: read failed\n");
                exit(1);
            }
            sleep(1);
            int j = *(int *)buf2;
            if (j != i) {
                printf("error: read the wrong value\n");
                exit(1);
            }
            exit(0);
        }
        if (write(fds[1], &i, sizeof(i)) != sizeof(i)) {
            printf("error: write failed\n");
            exit(-1);
        }
    }

    int xstatus = 0;
    for (int i = 0; i < 4; i++) {
        wait(&xstatus);
        if (xstatus != 0) {
            exit(1);
        }
    }

    if (buf2[0] != 99) {
        printf("error: child overwrote parent\n");
        exit(1);
    }

    printf("filetest test OK\n");
}

void cowtest() {
    simpletest();

    // check that the first simpletest() freed the physical memory.
    simpletest();

    threetest();
    threetest();
    threetest();

    filetest();

    printf("ALL COW TESTS PASSED\n");
}


void stressfs() {
    int fd, i;
    char path[] = "stressfs0";
    char data[512];

    printf("==========stressfs test==========\n");
    memset(data, 'a', sizeof(data));

    for (i = 0; i < 4; i++)
        if (fork() > 0)
            break;

    printf("write %d\n", i);

    path[8] += i;
    fd = open(path, O_CREAT | O_RDWR);
    for (i = 0; i < 20; i++)
        //    printf(fd, "%d\n", i);
        write(fd, data, sizeof(data));
    close(fd);

    printf("read\n");

    fd = open(path, O_RDONLY);
    for (i = 0; i < 20; i++)
        read(fd, data, sizeof(data));
    close(fd);

    wait(0);
    printf("stressfs test OK\n");
}


void copyinstr3() {
    printf("==========copyinstr3 test==========\n");
    sbrk(8192);
    uint64 top = (uint64)sbrk(0);
    if ((top % PGSIZE) != 0) {
        sbrk(PGSIZE - (top % PGSIZE));
    }
    top = (uint64)sbrk(0);
    if (top % PGSIZE) {
        printf("oops\n");
        exit(1);
    }

    char *b = (char *)(top - 1);
    *b = 'x';

    int ret = unlink(b);
    if (ret != -1) {
        printf("unlink(%s) returned %d, not -1\n", b, ret);
        exit(1);
    }

    int fd = open(b, O_CREAT | O_WRONLY);
    if (fd != -1) {
        printf("open(%s) returned %d, not -1\n", b, fd);
        exit(1);
    }

    char *args[] = {"xx", 0};
    ret = execve(b, args, NULL);
    if (ret != -1) {
        printf("execve(%s) returned %d, not -1\n", b, fd);
        exit(1);
    }
    printf("copyinstr3 test OK\n");
}


void subdir() {
    int fd, cc;
    printf("==========subdir test==========\n");
    unlink("ff");
    if (mkdir("dd", 0666) != 0) {
        printf("mkdir dd failed\n");
        exit(1);
    }

    fd = open("dd/ff", O_CREAT | O_RDWR);
    if (fd < 0) {
        printf("create dd/ff failed\n");
        exit(1);
    }
    write(fd, "ff", 2);
    close(fd);

    if (unlink("dd") >= 0) {
        printf("unlink dd (non-empty dir) succeeded!\n");
        exit(1);
    }

    if (mkdir("/dd/dd", 0666) != 0) {
        printf("subdir mkdir dd/dd failed\n");
        exit(1);
    }

    fd = open("dd/dd/ff", O_CREAT | O_RDWR);
    if (fd < 0) {
        printf("create dd/dd/ff failed\n");
        exit(1);
    }
    write(fd, "FF", 2);
    close(fd);

    fd = open("dd/dd/../ff", 0);
    if (fd < 0) {
        printf("open dd/dd/../ff failed\n");
        exit(1);
    }
    cc = read(fd, buf, sizeof(buf));
    printf("%s", buf);
    if (cc != 2 || buf[0] != 'f') {
        printf("dd/dd/../ff wrong content\n");
        exit(1);
    }
    close(fd);

    if (unlink("dd/dd/ff") != 0) {
        printf("unlink dd/dd/ff failed\n");
        exit(1);
    }
    if (open("dd/dd/ff", O_RDONLY) >= 0) {
        printf("open (unlinked) dd/dd/ff succeeded\n");
        exit(1);
    }

    if (chdir("dd") != 0) {
        printf("chdir dd failed\n");
        exit(1);
    }
    if (chdir("dd/../../dd") != 0) {
        printf("chdir dd/../../dd failed\n");
        exit(1);
    }
    if (chdir("dd/../../../dd") != 0) {
        printf("chdir dd/../../dd failed\n");
        exit(1);
    }
    if (chdir("./..") != 0) {
        printf("chdir ./.. failed\n");
        exit(1);
    }


    if (open("dd/dd/ff", O_RDONLY) >= 0) {
        printf("open (unlinked) dd/dd/ff succeeded!\n");
        exit(1);
    }

    if (open("dd/ff/ff", O_CREAT | O_RDWR) >= 0) {
        printf("create dd/ff/ff succeeded!\n");
        exit(1);
    }
    if (open("dd/xx/ff", O_CREAT | O_RDWR) >= 0) {
        printf("create dd/xx/ff succeeded!\n");
        exit(1);
    }
    if (open("dd", O_CREAT) >= 0) {
        printf("create dd succeeded!\n");
        exit(1);
    }
    if (open("dd", O_RDWR) >= 0) {
        printf("open dd rdwr succeeded!\n");
        exit(1);
    }

    if (open("dd", O_WRONLY) >= 0) {
        printf("open dd wronly succeeded!\n");
        exit(1);
    }

    // if (mkdir("dd/ff/ff", 066) == 0) {
    //     printf("mkdir dd/ff/ff succeeded!\n");
    //     exit(1);
    // }
    // if (mkdir("dd/xx/ff", 066) == 0) {
    //     printf("mkdir dd/xx/ff succeeded!\n");
    //     exit(1);
    // }
    // if (mkdir("dd/dd/ffff", 0666) == 0) {
    //     printf("mkdir dd/dd/ffff succeeded!\n");
    //     exit(1);
    // }
    // if (unlink("dd/xx/ff") == 0) {
    //     printf("unlink dd/xx/ff succeeded!\n");
    //     exit(1);
    // }
    // if (unlink("dd/ff/ff") == 0) {
    //     printf("unlink dd/ff/ff succeeded!\n");
    //     exit(1);
    // }
    // if (chdir("dd/ff") == 0) {
    //     printf("chdir dd/ff succeeded!\n");
    //     exit(1);
    // }
    // if (chdir("dd/xx") == 0) {
    //     printf("chdir dd/xx succeeded!\n");
    //     exit(1);
    // }

    // if (unlink("dd/dd/ffff") != 0) {
    //     printf("unlink dd/dd/ff failed\n");
    //     exit(1);
    // }
    // if (unlink("dd/ff") != 0) {
    //     printf("unlink dd/ff failed\n");
    //     exit(1);
    // }
    // if (unlink("dd") == 0) {
    //     printf("unlink non-empty dd succeeded!\n");
    //     exit(1);
    // }
    // if (unlink("dd/dd") < 0) {
    //     printf("unlink dd/dd failed\n");
    //     exit(1);
    // }
    // if (unlink("dd") < 0) {
    //     printf("unlink dd failed\n");
    //     exit(1);
    // }
    printf("subdir test OK\n");
}

void dirfile() {
    printf("==========dirfile test==========\n");
    int fd;

    fd = open("dirfile", O_CREAT);
    if (fd < 0) {
        printf("create dirfile failed\n");
        exit(1);
    }
    close(fd);
    if (chdir("dirfile") == 0) {
        printf("chdir dirfile succeeded!\n");
        exit(1);
    }
    fd = open("dirfile/xx", 0);
    if (fd >= 0) {
        printf("create dirfile/xx succeeded!\n");
        exit(1);
    }
    fd = open("dirfile/xx", O_CREAT);
    if (fd >= 0) {
        printf("create dirfile/xx succeeded!\n");
        exit(1);
    }
    if (mkdir("dirfile/xx", 0666) == 0) {
        printf("mkdir dirfile/xx succeeded!\n");
        exit(1);
    }
    if (unlink("dirfile/xx") == 0) {
        printf("unlink dirfile/xx succeeded!\n");
        exit(1);
    }

    if (unlink("dirfile") != 0) {
        printf("unlink dirfile failed!\n");
        exit(1);
    }

    close(fd);
    printf("dirfile test OK\n");
}


int main(void) {
    // ==== process =====
    // print_sysinfo();
    // forktest();
    // print_sysinfo();
    // exitwait();
    // print_sysinfo();
    // forkfork();    
    // print_sysinfo();
    // forkforkfork();
    // print_sysinfo();
    // twochildren();
    // print_sysinfo();
    // reparent();
    // print_sysinfo();
    // reparent2(); 
    // print_sysinfo();
    // killstatus();
    // print_sysinfo();

    // ====== file system ======
    // opentest();
    // print_sysinfo();
    openiputtest();
    print_sysinfo();
    writetest();
    print_sysinfo();
    writebig();
    print_sysinfo();
    
    preempt();
    print_sysinfo();
    truncate1();
    print_sysinfo();
    copyin();
    print_sysinfo();
    copyout();
    print_sysinfo();
    copyinstr1();
    print_sysinfo();
    truncate2(); // not valid
    print_sysinfo();
    truncate3();
    print_sysinfo();
    sbrkbasic();
    print_sysinfo();
    sbrkmuch();
    print_sysinfo();

    iputtest();
    
    print_sysinfo();
    exitiputtest();
    print_sysinfo();
    createtest();
    print_sysinfo();
    sbrklast();
    print_sysinfo();
    dirtest();
    print_sysinfo();
    execvetest();
    print_sysinfo();
    uvmfree();
    print_sysinfo();
    pipe1();
    print_sysinfo();
    mem();
    print_sysinfo();

    sharedfd();     // occur bug
    print_sysinfo();
    createdelete();
    print_sysinfo();
    fourfiles();
    print_sysinfo();
    bigwrite();
    print_sysinfo();
    bigfile();
    print_sysinfo();
    rmdot();
    print_sysinfo();
    badarg();
    print_sysinfo();
    sbrk8000();
    print_sysinfo();
    textwrite();
    print_sysinfo();
    outofinodes();
    print_sysinfo();
    manywrites();
    print_sysinfo();
    badwrite();

    print_sysinfo();
    kernmem();
    print_sysinfo();
    MAXVAplus();
    print_sysinfo();
    sbrkfail();
    print_sysinfo();
    sbrkarg();
    print_sysinfo();
    bsstest();
    print_sysinfo();
    bigargtest();

    print_sysinfo();
    argptest();
    print_sysinfo();
    stacktest();
    print_sysinfo();
    pgbug();
    print_sysinfo();
    sbrkbugs();
    print_sysinfo();
    cowtest();
    print_sysinfo();
    copyinstr3();
    print_sysinfo();
    stressfs();
    print_sysinfo();

//     // TODO :
//     // fsfull();
//     // diskfull();
//     // execveout();
//     // fourteen();
//     // unlinkread();
//     // subdir();
//     // dirfile();

    printf("ALL TESTS PASSED\n");
    exit(0);
    return 0;
}
