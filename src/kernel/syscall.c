#include "debug.h"
#include "common.h"
#include "param.h"
#include "memory/memlayout.h"
#include "lib/riscv.h"
#include "atomic/spinlock.h"
#include "proc/pcb_life.h"
#include "proc/tcb_life.h"
#include "kernel/trap.h"
#include "syscall_gen/syscall_num.h"
#include "debug.h"
#include "kernel/syscall.h"

extern atomic_t pages_cnt;

// #define __STRACE__
// Fetch the uint64 at addr from the current process.
#define INSTACK(addr) ((addr) >= USTACK && (addr) + sizeof(uint64) < USTACK + USTACK_PAGE * PGSIZE)
int fetchaddr(vaddr_t addr, uint64 *ip) {
    struct proc *p = proc_current();
    // if ((addr >= p->mm->brk || addr + sizeof(uint64) > p->mm->brk) && !INSTACK(addr)) // both tests needed, in case of overflow
    //     return -1;
    if (copyin(p->mm->pagetable, (char *)ip, addr, sizeof(*ip)) != 0)
        return -1;
    return 0;
}

// Fetch the nul-terminated string at addr from the current process.
// Returns length of string, not including nul, or -1 for error.
int fetchstr(uint64 addr, char *buf, int max) {
    struct proc *p = proc_current();
    if (copyinstr(p->mm->pagetable, buf, addr, max) < 0)
        return -1;
    return strlen(buf);
}

uint64 argraw(int n) {
    // struct proc *p = proc_current();
    struct tcb *t = thread_current();
    switch (n) {
    case 0:
        return t->trapframe->a0;
    case 1:
        return t->trapframe->a1;
    case 2:
        return t->trapframe->a2;
    case 3:
        return t->trapframe->a3;
    case 4:
        return t->trapframe->a4;
    case 5:
        return t->trapframe->a5;
    }
    panic("argraw");
    return -1;
}

int arglist(uint64 argv[], int s, int n) {
    ASSERT(s + n <= 6);
    struct trapframe *trapframe = thread_current()->trapframe;
    uint64 kbuf[6] = {trapframe->a0, trapframe->a1, trapframe->a2, trapframe->a3,
                      trapframe->a4, trapframe->a5};
    for (int i = 0; i != n; ++i) {
        argv[i] = kbuf[s + i];
    }
    return 0;
}

// Fetch the nth 32-bit system call argument.
int argint(int n, int *ip) {
    *ip = argraw(n);
    if (*ip < 0)
        return -1;
    else
        return 0;
}

int arguint(int n, uint *ip) {
    *ip = argraw(n);
    if (*ip < 0)
        return -1;
    else
        return 0;
}

// int arglong(int n, uint64 *ip) {
//     *ip = argraw(n);
//     if (*ip < 0)
//         return -1;
//     else
//         return 0;
// }

void argulong(int n, unsigned long *ulip) {
    *ulip = argraw(n);
}

int arglong(int n, long *lip) {
    *lip = argraw(n);
    if (*lip < 0)
        return -1;
    else
        return 0;
}

// Retrieve an argument as a pointer.
// Doesn't check for legality, since
// copyin/copyout will do that.
int argaddr(int n, uint64 *ip) {
    *ip = argraw(n);
    if (*ip < 0)
        return -1;
    else
        return 0;
}

// Fetch the nth word-sized system call argument as a null-terminated string.
// Copies into buf, at most max.
// Returns string length if OK (including nul), -1 if error.
int argstr(int n, char *buf, int max) {
    uint64 addr;
    argaddr(n, &addr);
    return fetchstr(addr, buf, max);
}

// Prototypes for the functions that handle system calls.
#include "syscall_gen/syscall_def.h"

// An array mapping syscall numbers from syscall.h
// to the function that handles the system call.
static uint64 (*syscalls[])(void) = {
#include "syscall_gen/syscall_func.h"
};

int syscall_cnt[] = {
#include "syscall_gen/syscall_cnt.h"
};

uint64 syscall_time[] = {
#include "syscall_gen/syscall_cnt.h"
};

int syscall_mm[] = {
#include "syscall_gen/syscall_cnt.h"
};

char *syscall_str[] = {
#include "syscall_gen/syscall_str.h"
};

void syscall_count_analysis(void) {
    int len_syscall = NELEM(syscall_cnt);
    for (int i = 0; i < len_syscall; i++) {
        if (syscall_str[i] != NULL && syscall_cnt[i] != 0)
            // printf("%s , cnt : %d, time : %ld ns, time per syscall : %ld, mm : %d pages\n", syscall_str[i], syscall_cnt[i], syscall_time[i], syscall_time[i]/syscall_cnt[i], syscall_mm[i]);
            printf("time per syscall : %-10ld, total time : %-10ld, total cnt : %-10ld, syscall name : %s, mm : %d pages\n", syscall_time[i]/syscall_cnt[i], syscall_time[i], syscall_cnt[i] ,syscall_str[i], syscall_mm[i]);
    }
}

#ifdef __STRACE__
struct syscall_info {
    const char *name;
    int num;
    // s/p/d/...
    char type[8]; /* reserve a space for \0 */
    char return_type;
};

static struct syscall_info info[] = {
    /* pid_t getpid(void); */
    [SYS_getpid] { "getpid", 0, },
    // int exit(int) __attribute__((noreturn));
    [SYS_exit] { "exit", 1, "d" },
    /* int execve(const char *pathname, char *const argv[], char *const envp[]); */
    [SYS_execve] { "execve", 3, "spp" },
    // char* sbrk(int);
    [SYS_sbrk] { "sbrk", 1, "d", 'p' },
    /* int close(int fd); */
    [SYS_close] { "close", 1, "d" },
    /* ssize_t read(int fd, void *buf, size_t count); */
    [SYS_read] { "read", 3, "dpu" },
    // int open(const char*, int);
    [SYS_openat] { "openat", 4, "dsxx" },
    /* int getdents64(unsigned int fd, struct linux_dirent64 *dirp,
                    unsigned int count); */
    [SYS_getdents64] { "getdents64", 3, "upu" },
    // int write(int, const void*, int);
    [SYS_write] { "write", 3, "dpd" },
    //    int brk(void *addr);
    [SYS_brk] { "brk", 1, "p", 'p' },
    // void *mmap(void *addr, size_t length, int prot, int flags,
    //         int fd, off_t offset);
    [SYS_mmap] { "mmap", 6, "puxxdu", 'p' },
    //    int munmap(void *addr, size_t length);
    [SYS_munmap] { "munmap", 2, "px", 'd' },
    // long set_tid_address(int *tidptr);
    [SYS_set_tid_address] { "set_tid_address", 1, "p", 'd' },
    // int mprotect(void *addr, size_t len, int prot);
    [SYS_mprotect] { "mprotect", 3, "pux", 'd' },
    // int ioctl(int fd, unsigned long request, ...);
    [SYS_ioctl] { "ioctl", 2, "du", 'd' },
    // pid_t wait4(pid_t pid, int *wstatus, int options, struct rusage *rusage);
    [SYS_wait4] { "wait4", 4, "dpxp" },
    // void exit_group(int status);
    [SYS_exit_group] { "exit_group", 1, "d", 'd' },
    // int rt_sigprocmask(int how, const kernel_sigset_t *set,
    //  kernel_sigset_t *oldset, size_t sigsetsize);
    [SYS_rt_sigprocmask] { "rt_sigprocmask", 4, "dppd", 'd' },
    //        int sigaction(int signum, const struct sigaction *act,
    //                 struct sigaction *oldact);
    [SYS_rt_sigaction] { "rt_sigaction", 3, "dpp", 'd' },
    // pid_t getppid(void);
    [SYS_getppid] { "getppid", 0, },
    // int uname(struct utsname *buf);
    [SYS_uname] { "uname", 1, "p" },
    // char *getcwd(char *buf, size_t size);
    [SYS_getcwd] { "getcwd", 2, "pd", 's' },
    // int fcntl(int fd, int cmd, ... /* arg */ );
    [SYS_fcntl] { "fcntl", 2, "dd", 'd' },
    // ssize_t writev(int fd, const struct iovec *iov, int iovcnt);
    [SYS_writev] { "writev", 3, "dpd", 'u' },
    //        int fstatat(int dirfd, const char *pathname, struct stat *statbuf, int flags);
    [SYS_fstatat] { "fstatat", 4, "dspx", 'd' },
    //            long clone(unsigned long flags, void *stack, int *parent_tid, unsigned long tls, int *child_tid);
    // int flags, void* stack , pid_t* ptid, void*tls, pid_t* ctid
    [SYS_clone] { "clone", 5, "xpppp", 'd' },
    //          int pipe2(int pipefd[2], int flags);
    [SYS_pipe2] { "pipe2", 2, "pd", 'd' },
    //          int dup(int);
    [SYS_dup] { "dup", 1, "d" },
    //          int dup3(int oldfd, int newfd, int flags);
    [SYS_dup3] { "dup3", 3, "ddd", 'd' },
    // pid_t gettid(void);
    [SYS_gettid] { "gettid", 0, },
    // int clock_gettime(clockid_t clk_id, struct timespec *tp);
    [SYS_clock_gettime] { "clock_gettime", 2, "dp" },
    [SYS_sendfile] { "sendfile", 4, "dddd" },

    // int socket(int domain, int type, int protocol);
    [SYS_socket] { "socket", 3, "ddd", 'd' },
    //        int setsockopt(int sockfd, int level, int optname, const void *optval, socklen_t optlen);
    [SYS_setsockopt] { "setsockopt", 5, "dddpu", 'd' },
    // int listen(int sockfd, int backlog);
    [SYS_listen] { "listen", 2, "dd", 'd' },
    //       int bind(int sockfd, const struct sockaddr *addr, socklen_t addrlen);
    [SYS_bind] { "bind", 3, "dpu", 'd' },
    // int pselect(int nfds, fd_set *readfds, fd_set *writefds,
    // fd_set *exceptfds, const struct timespec *timeout, const sigset_t *sigmask);
    [SYS_pselect6] { "pselect6", 6, "dppppp", 'd' },
    // int getsockname(int sockfd, struct sockaddr *addr, socklen_t *addrlen);
    [SYS_getsockname] { "getsockname", 3, "dpp", 'd' },
    // ssize_t sendto(int sockfd, const void *buf, size_t len, int flags, const struct sockaddr *dest_addr, socklen_t addrlen);
    [SYS_sendto] { "sendto", 6, "dpddpd", 'd' },
    // ssize_t recvfrom(int sockfd, void *buf, size_t len, int flags, struct sockaddr *src_addr, socklen_t *addrlen);
    [SYS_recvfrom] { "recvfrom", 6, "dpddpp" },
    // int connect(int sockfd, const struct sockaddr *addr, socklen_t addrlen);
    [SYS_connect] { "connect", 3, "dpd", 'd' },
    //  int getsockopt(int sockfd, int level, int optname, void *optval, socklen_t *optlen);
    [SYS_getsockopt] { "getsockopt", 5, "dddpp", 'd' },
    // int accept(int sockfd, struct sockaddr *addr, socklen_t *addrlen);
    [SYS_accept] { "accept", 3, "dpp" },

    [SYS_lseek] { "lseek", 3, "dld" },
    [SYS_prlimit64] { "prlimit64", 4, "ddpp" },
    [SYS_readv] { "readv", 3, "dpd" },
    [SYS_clock_nanosleep] { "clock_nanosleep", 4, "ddpp" },
    [SYS_socketpair] { "socketpair", 4, "dddd" },
    [SYS_get_robust_list] { "get_robust_list", 3, "dpp" },
    // // int fork(void);
    // [SYS_fork] { "fork", 0, },
    // // int wait(int*);
    // [SYS_wait] { "wait", 1, "p" },
    // // int pipe(int*);
    // [SYS_pipe] { "pipe", 1, "p" },
    // int kill(pid_t pid, int sig);
    [SYS_kill] { "kill", 2, "dd" },
    // // int fstat(int fd, struct stat*);
    // [SYS_fstat] { "fstat", 2, "dp" },
    // // int chdir(const char*);
    // [SYS_chdir] { "chdir", 1, "s" },
    // // int sleep(int);
    // [SYS_sleep] { "sleep", 1, "d" },
    // // int uptime(void);
    // [SYS_uptime] { "uptime", 0 },
    // // int mknod(const char*, short, short);
    // [SYS_mknod] { "mknod", 3, "sdd" },
    // // int unlink(const char*);,
    [SYS_unlinkat] { "unlinkat", 3, "dsd" },
    [SYS_clock_gettime] { "clock_gettime", 2, "dp" },
    [SYS_fstat] { "fstat", 2, "dp" },
    [SYS_chdir] { "chdir", 1, "s" },
    [SYS_shmget] { "shmget", 3, "dld" },
    [SYS_shmctl] { "shmctl", 3, "ddp" },
    [SYS_shmat] { "shmat", 3, "dpd" },
    [SYS_sync] { "sync", 0 },
    [SYS_fsync] { "fsync", 1, "d" },
    [SYS_ftruncate] { "ftruncate", 2, "dl" },
    [SYS_utimensat] { "utimensat", 4, "dspd" },
    [SYS_setitimer] { "setitimer", 3, "dpp" },
    [SYS_umask] { "umask", 1, "d" },
    [SYS_sched_getaffinity] { "sched_getaffinity", 3, "ddp" },
    [SYS_sched_setaffinity] { "shced_setaffinity", 3, "ddp" },
    [SYS_sched_getscheduler] { "sched_getscheduler", 3, "ddp" },
    [SYS_sched_getparam] { "sched_getparam", 2, "dp" },
    [SYS_sched_setscheduler] { "sched_setscheduler", 3, "ddp" },
    [SYS_clock_getres] { "clock_getres", 2, "dp" },
    [SYS_nanosleep] { "nanosleep", 2, "pp" },
    [SYS_futex] { "futex", 6, "pddppd" },
    [SYS_tkill] { "tkill", 2, "dd" },
    [SYS_membarrier] { "membarrier", 3, "ddd" },
    [SYS_clock_nanosleep] { "clock_nanosleep", 4, "ddpp" },
    // int link(const char*, const char*);
    // [SYS_link] { "link", 2, "ss" },
    // // int mkdir(const char*);
    // [SYS_mkdir] { "mkdir", 1, "s" },
    //  int getrusage(int who, struct rusage *usage);
    [SYS_getrusage] { "getrusage", 2, "dp" },
    // int socketpair(int domain, int type, int protocol, int sv[2]);
    [SYS_socketpair] { "socketpair", 4, "dddp" },
    // ssize_t readlink(const char *pathname, char *buf, size_t bufsiz);
    [SYS_readlinkat] { "readlinkat", 3, "spd" },
    // int kill(pid_t pid, int sig);
    [SYS_kill] { "kill", 2, "dd" },
    // int msync(void *addr, size_t length, int flags);
    [SYS_msync] { "msync", 3, "pdd" },
    // int mkdirat(int dirfd, const char *pathname, mode_t mode);
    [SYS_mkdirat] { "mkdirat", 3, "dsu" },
    [SYS_pread64] { "pread64", 4, "dpdd" },
    [SYS_pwrite64] { "pwrite64", 4, "dpdd" }};

// static int syscall_filter[] = {
//     [SYS_read] 1,
//     [SYS_write] 1,
//     [1050] 0
// };

#define STRACE_TARGET_NUM 1
// cannot use to debug pr(printf's lock)!!!
char *strace_proc_name[STRACE_TARGET_NUM] = {
    "ls",
};

int is_strace_target(int num) {
    /* trace all proc except sh and init */
    if (proc_current()->pid > 2) {
        if (num == SYS_execve) {
            return 1;
        } else {
            return 0;
        }
        // if (num == SYS_getuid) {
        //     return 0;
        // }
        // if (num == SYS_kill || num == SYS_rt_sigreturn) {
        //     return 0;
        // }
        // if (num == SYS_read || num == SYS_write || num == SYS_lseek || num == SYS_pselect6 || num == SYS_clock_gettime || num == SYS_getrusage) {
        //     return 0;
        // }
        // if (num == SYS_writev || num == SYS_readv) {
        //     return 0;
        // }
        // if(num == SYS_clock_gettime || num == SYS_getrusage || num == SYS_pselect6) {
        //     return 0;
        // }
        // printfYELLOW("syscall num is %d\n", num);
        // return 1;
        // if (num == SYS_clock_gettime || num == SYS_nanosleep || num == SYS_clock_nanosleep) {
        //     return 0;
        // }
        // if (num == SYS_read || num == SYS_write) {
        //     return 0;
        // }
        // if (num == SYS_kill || num == SYS_wait4) {
        //     return 1;
        // } else {
        //     return 0;
        // }
        // if(num == SYS_msync || num == SYS_mmap || num == SYS_munmap || num == SYS_kill || num == SYS_pselect6 || num == SYS_getrusage || num == SYS_clock_gettime) {
        //     return 0;
        // }
        // if(num==)
        //     return 1;
        // else
        //     return 0;
        // if(num == SYS_execve || num == SYS_sendfile) {
        //     return 1;
        // } else {
        //     return 0;
        // }
        return 1;
    } else {
        return 0;
        // if(syscall_filter[num]) {
        //     return 0;
        // }
        // else {
        //     return 1;
        // }
    }
    // for (int i = 0; i < STRACE_TARGET_NUM; i++) {
    //     if (strncmp(proc_current()->name, strace_proc_name[i], sizeof(strace_proc_name[i])) == 0) {
    //         return 1;
    //     }
    // }
    // return 0;
}

#endif

void syscall(void) {
    // static int mm_prev = 0;
    int num;
#ifdef __STRACE__
    /* a0 use both in argument and return value, so need to preserve it when open STRACE */
    uint64 a0;
    struct proc *p = proc_current();
#endif

    struct tcb *t = thread_current();

    num = t->trapframe->a7;
    // printfYELLOW("syscall num is %d\n", num);
    if (num >= 0 && num < NELEM(syscalls) && syscalls[num]) {
        syscall_cnt[num]++;
        // Use num to lookup the system call function for num, call it,
        // and store its return value in p->trapframe->a0
#ifdef __STRACE__
        a0 = t->trapframe->a0;
        if (is_strace_target(num)) {
            STRACE("%d.%d : syscall %s(", p->pid, t->tidx, info[num].name);
            for (int i = 0; i < info[num].num; i++) {
                uint64 argument;
                switch (i) {
                case 0: argument = a0; break;
                case 1: argument = t->trapframe->a1; break;
                case 2: argument = t->trapframe->a2; break;
                case 3: argument = t->trapframe->a3; break;
                case 4: argument = t->trapframe->a4; break;
                case 5: argument = t->trapframe->a5; break;
                case 6: argument = t->trapframe->a6; break;
                default: panic("could not reach here"); break;
                }
                switch (info[num].type[i]) {
                case 's': {
                    char buf[100];
                    copyinstr(p->mm->pagetable, buf, argument, 100);
                    STRACE("%s, ", buf);
                    break;
                }
                case 'd': STRACE("%d, ", argument); break;
                case 'p': STRACE("%p, ", argument); break;
                case 'u': STRACE("%u, ", argument); break;
                case 'l': STRACE("%ld, ", argument); break;
                case 'x': STRACE("%#x, ", argument); break;
                default: STRACE("\\, "); break;
                }
            }
        }
#endif
        // int pages_before = atomic_read(&pages_cnt);
        // uint64 time_before = rdtime();
        t->trapframe->a0 = syscalls[num]();
        // uint64 time_after = rdtime();
        // int pages_after = atomic_read(&pages_cnt);
        // syscall_mm[num] += (pages_after - pages_before);
        // uint64 time = TIME2NS((time_after - time_before));
        // syscall_time[num] += time;
        // if (pages_after != mm_prev)
        // printfRed("%s, mm : %d\n", syscall_str[num], pages_after);
        // mm_prev = pages_after;

#ifdef __STRACE__
        if (is_strace_target(num)) {
            switch (info[num].return_type) {
            case 'p': STRACE(") -> %#x\n", t->trapframe->a0); break;
            case 'u': STRACE(") -> %u\n", t->trapframe->a0); break;
            case 's': {
                char str[100];
                fetchstr(t->trapframe->a0, str, 100);
                STRACE(") -> %s\n", str);
                break;
            }
            case 'c': STRACE(") -> %c\n", t->trapframe->a0); break;
            default: STRACE(") -> %d\n", t->trapframe->a0); break;
            }
        }
#endif
    } else {
        printf("tid : %d name : %s: unknown sys call %d\n",
               t->tid, t->name, num);
        t->trapframe->a0 = 0;
    }
}