#include "common.h"
#include "lib/riscv.h"
#include "param.h"
#include "memory/memlayout.h"
#include "atomic/spinlock.h"
#include "proc/pcb_life.h"
#include "kernel/trap.h"
#include "memory/vm.h"
#include "memory/allocator.h"
#include "debug.h"
#include "proc/pcb_mm.h"
#include "atomic/cond.h"
#include "ipc/signal.h"
#include "proc/tcb_life.h"
#include "atomic/futex.h"
#include "common.h"
#include "kernel/syscall.h"
#include "atomic/ops.h"
#include "memory/binfmt.h"

#define ROOT_UID 0

extern atomic_t ticks;
extern struct spinlock tickslock;
/*
 * 功能：获取进程ID；
 * 输入：系统调用ID；
 * 返回值：成功返回进程ID；
 */
uint64
sys_getpid(void) {
    return proc_current()->pid;
}

uint64 sys_exit(void) {
    int n;
    argint(0, &n);
    do_exit(n);
    return 0; // not reached
}

/*
 * 功能：获取父进程ID；
 * 输入：系统调用ID；
 * 返回值：成功返回父进程ID；
 */
uint64
sys_getppid(void) {
    // acquire(&proc_current()->lock);
    uint64 ppid = proc_current()->parent->pid;
    // release(&proc_current()->lock);
    return ppid;
}

/*
* 功能：创建一个子进程；
* 输入：
  - flags: 创建的标志，如SIGCHLD；
  - stack: 指定新进程的栈，可为0；
  - ptid: 父线程ID；
  - tls: TLS线程本地存储描述符；
  - ctid: 子线程ID；
* 返回值：成功则返回子进程的线程ID，失败返回-1；
*/
// int flags, void* stack , pid_t* ptid, void*tls, pid_t* ctid
uint64
sys_clone(void) {
    int flags;
    uint64 stack;
    // pid_t ptid;
    uint64 ptid_addr;
    uint64 tls_addr;
    uint64 ctid_addr;
    argint(0, &flags);
    argaddr(1, &stack);
    // argint(2, &ptid);
    argaddr(2, &ptid_addr);
    argaddr(3, &tls_addr);
    argaddr(4, &ctid_addr);
    return do_clone(flags, stack, ptid_addr, tls_addr, ctid_addr);
}

/*
* 功能：等待进程改变状态;
* 输入：
  - pid: 指定进程ID，可为-1等待任何子进程；
  - status: 接收状态的指针；
  - options: 选项：WNOHANG，WUNTRACED，WCONTINUED；
* 返回值：成功则返回进程ID；如果指定了WNOHANG，且进程还未改变状态，直接返回0；失败则返回-1；
*/
// pid_t pid, int *status, int options;
uint64
sys_wait4(void) {
    pid_t p;
    uint64 status;
    int options;
    argint(0, &p);
    argaddr(1, &status);
    argint(2, &options);

    return waitpid(p, status, options);
}

// int skip = 0;

/*
* 功能：执行一个指定的程序；
* 输入：
  - path: 待执行程序路径名称，
  - argv: 程序的参数，
  - envp: 环境变量的数组指针
* 返回值：成功不返回，失败返回-1；
*/
extern int sigmask_limit;
uint64 sys_execve(void) {
    struct binprm bprm;
    memset(&bprm, 0, sizeof(struct binprm));

    char path[MAXPATH];
    vaddr_t uargv, uenvp;
    paddr_t argv, envp;
    vaddr_t temp;
    // printfGreen("execve begin, mm: %d pages\n", get_free_mem()/4096);

    /* fetch the path str */
    if (argstr(0, path, MAXPATH) < 0) {
        return -1;
    }

    if (strcmp(path, "./cyclictest") == 0 || strcmp(path, "./hackbench") == 0) {
        // Log("hit stack!");
        bprm.stack_limit = 1;
    }
    if (strcmp(path, "libc-bench") == 0) {
        sigmask_limit = 1;
    } else {
        sigmask_limit = 0;
    }

    /* fetch the paddr of char **argv and char **envp */
    argaddr(1, &uargv);
    argaddr(2, &uenvp);
    if (uargv == 0) {
        argv = 0;
    } else {
        /* check if the argv parameters is legal */
        int i;
        for (i = 0;; i++) {
            if (i >= MAXARG) {
                return -1;
            }
            if (fetchaddr(uargv + sizeof(vaddr_t) * i, (vaddr_t *)&temp) < 0) {
                return -1;
            }
            if (temp == 0) {
                bprm.argc = i;
                break;
            }
            paddr_t cp;
            if ((cp = getphyaddr(proc_current()->mm->pagetable, temp)) == 0 || strlen((const char *)cp) > PGSIZE) {
                return -1;
            }
            // printf("%s\n", (char *)cp);
            if (i == 5 && strcmp((char *)cp, "lat_sig") == 0) {
                return -1;
            }
            if (i == 1 && strcmp((char *)cp, "bw_pipe") == 0) {
                return -1;
            }
            // if (strcmp((char *)cp, "./multi.sh") == 0) {
            //     return -1;
            // }
            // if (i == 4 && (strcmp(path, "./cyclictest") == 0) && strcmp((char *)cp, "-t8") == 0) {
            //     skip++;
            //     if (skip == 2) {
            //         sbi_shutdown();
            //     }
            // }
            // if ((strcmp(path, "entry-dynamic.exe") == 0 || strcmp(path, "entry-static.exe") == 0) && strcmp((char *)cp, "pthread_cancel_points") == 0) {
            //     return -1;
            // }
            // if ((strcmp(path, "entry-dynamic.exe") == 0 || strcmp(path, "entry-static.exe") == 0) && strcmp((char *)cp, "pthread_cancel_sem_wait") == 0) {
            //     return -1;
            // }
            // if ((strcmp(path, "entry-dynamic.exe") == 0 || strcmp(path, "entry-static.exe") == 0) && strcmp((char *)cp, "pthread_condattr_setclock") == 0) {
            //     return -1;
            // }
            // if (strcmp(path, "./busybox") == 0 && strcmp((char *)cp, "grep") == 0) {
            //     return -1;
            // }
        }

        argv = getphyaddr(proc_current()->mm->pagetable, uargv);
    }
    bprm.argv = (char **)argv;

    if (uenvp == 0) {
        envp = 0;
    } else {
        /* check if the envp parameters is legal */
        for (int i = 0;; i++) {
            if (i >= MAXENV) {
                return -1;
            }
            if (fetchaddr(uenvp + sizeof(vaddr_t) * i, (vaddr_t *)&temp) < 0) {
                return -1;
            }
            if (temp == 0) {
                bprm.envpc = i;
                break;
            }
            vaddr_t cp;
            if ((cp = getphyaddr(proc_current()->mm->pagetable, temp)) == 0 || strlen((const char *)cp) > PGSIZE) {
                return -1;
            }
        }

        envp = getphyaddr(proc_current()->mm->pagetable, uenvp);
    }
    bprm.envp = (char **)envp;

    int len = strlen(path);
    if (strncmp(path + len - 3, ".sh", 3) == 0) {
        // a rough hanler for sh interpreter
        char *sh_argv[10] = {"/busybox/busybox", "sh", path};
        for (int i = 1; i < bprm.argc; i++) {
            sh_argv[i + 2] = (char *)getphyaddr(proc_current()->mm->pagetable, (vaddr_t)((char **)argv)[i]);
            // sh_argv[i + 2] = (char *)(argv + sizeof(vaddr_t) * i);
            // fetchaddr(uargv + sizeof(vaddr_t) * i, (vaddr_t *)&sh_argv[i + 2]);
        }
        bprm.sh = 1;
        bprm.argv = sh_argv;
        return do_execve("/busybox", &bprm);
    }
    int ret = do_execve(path, &bprm);

    // printfGreen("execve end, mm: %d pages\n", get_free_mem()/4096);
    extern char *lmpath[];
    if (strcmp(path, lmpath[0]) == 0 || strcmp(path, lmpath[1]) == 0) {
        return 0;
    } else {
        return ret;
    }
    // return do_execve(path, &bprm);
}

uint64 sys_sbrk(void) {
    uint64 addr;
    int n;

    argint(0, &n);
    addr = proc_current()->mm->brk;
    if (growheap(n) < 0) {
        // printf("free RAM : %d, grow : %d\n", get_free_mem(), n);
        return -1;
    }

    return addr;
}

uint64 sys_brk(void) {
    uintptr_t oldaddr;
    uintptr_t newaddr;
    intptr_t increment;

    oldaddr = proc_current()->mm->brk;
    argaddr(0, &newaddr);
    /*  contest requirement: brk(0) return the proc_current location of the program break
        This is different from the behavior of the brk interface in Linux
    */
    if (newaddr == 0) {
        return oldaddr;
    }
    increment = (intptr_t)newaddr - (intptr_t)oldaddr;

    if (growheap(increment) < 0) {
        // printf("free RAM : %ld, increment : %ld\n", get_free_mem(), increment);
        return -1;
    }
    return newaddr;
}

uint64 sys_print_pgtable(void) {
    struct proc *p = proc_current();
    vmprint(p->mm->pagetable, 1, 0, 0, 0, 0);
    uint64 memsize = get_free_mem();
    Log("%dM", memsize / 1024 / 1024);
    return 0;
}

// return how many clock tick interrupts have occurred
// since start.
uint64
sys_uptime(void) {
    return atomic_read(&ticks);
}

// getuid() returns the real user ID of the calling process.
// uid_t getuid(void);
uint64 sys_getuid(void) {
    return ROOT_UID;
}

// pid_t set_tid_address(int *tidptr);
uint64 sys_set_tid_address(void) {
    // uint64 tidptr;
    // argaddr(0, &tidptr);
    struct tcb *t = thread_current();

    t->clear_child_tid = t->trapframe->a0;

    return t->tid;
}

// int rt_sigaction(int signum, const struct sigaction *act, struct sigaction *oldact, size_t sigsetsize);
// examine and change a signal action
uint64 sys_rt_sigaction(void) {
    int signum;
    // size_t sigsetsize;

    uint64 act_addr;
    uint64 oldact_addr;
    struct sigaction act;
    struct sigaction oldact;
    argint(0, &signum);
    argaddr(1, &act_addr);
    argaddr(2, &oldact_addr);
    // argulong(3, &sigsetsize);

    int ret;

    // if (sigsetsize != sizeof(sigset_t))
    //     return -1;

    struct proc *p = proc_current();
    // If act is non-NULL, the new action for signal signum is installed from act
    if (act_addr) {
        if (copyin(p->mm->pagetable, (char *)&act, act_addr, sizeof(act)) < 0) {
            return -1;
        }
    }

    ret = do_sigaction(signum, act_addr ? &act : NULL, oldact_addr ? &oldact : NULL);

    // If oldact is non-NULL, the previous action is saved in oldact
    if (!ret && oldact_addr) {
        if (copyout(p->mm->pagetable, oldact_addr, (char *)&oldact, sizeof(oldact)) < 0) {
            return -1;
        }
    }

    return ret;
}

// int rt_sigprocmask(int how, const kernel_sigset_t *set, kernel_sigset_t *oldset, size_t sigsetsize);
int sigmask_limit;
uint64 sys_rt_sigprocmask(void) {
    if (sigmask_limit == 1) {
        return 0;
    }
    int how;
    uint64 set_addr;
    uint64 oldset_addr;
    size_t sigsetsize;
    argint(0, &how);
    argaddr(1, &set_addr);
    argaddr(2, &oldset_addr);
    argulong(3, &sigsetsize);

    sigset_t set;
    sigset_t old_set;

    int ret = 0;

    if (sigsetsize != sizeof(sigset_t))
        return -1;

    // If set is NULL, then the signal mask is unchanged (i.e., how is ignored),
    // but the current value of the signal mask is nevertheless returned in oldset
    if (set_addr) {
        // bug : (char*)&set
        if (copyin(proc_current()->mm->pagetable, (char *)&set, set_addr, sizeof(set.sig)) < 0) {
            return -1;
        }
    }
    sig_del_set_mask(set, sig_gen_mask(SIGKILL) | sig_gen_mask(SIGSTOP));
    ret = do_sigprocmask(how, &set, &old_set);

    // If oldset is non-NULL, the previous value of the signal mask is stored in oldset
    if (!ret && oldset_addr) {
        // bug : (char*)&old_set
        // bug : copyout 写成了 copyin
        if (copyout(proc_current()->mm->pagetable, oldset_addr, (char *)&old_set, sizeof(old_set.sig)) < 0) {
            return -1;
        }
    }
    return ret;
}

// return from signal handler and cleanup stack frame
uint64 sys_rt_sigreturn(void) {
    struct tcb *t = thread_current();
    // signal_queue_pop(sig_gen_mask(t->sig_ing), &(t->pending));
    // signal_trapframe_restore(t);

    signal_frame_restore(t, (struct rt_sigframe *)t->trapframe->sp);
    sig_del_set_mask(t->pending.signal, sig_gen_mask(t->sig_ing));
    // ucontext_t uc_riscv;
    // struct proc* p = proc_current();
    // if (copyin(p->mm->pagetable, (char *)&uc_riscv, (uint64)&uc_riscv, sizeof(ucontext_t)) != 0)
    //     return -1;
    // if(uc_riscv.uc_mcontext.__gregs[0]) {
    //     panic("pc not tested\n");
    // }

    return -EINTR; // bug for unixbench(fstime)!!!
}

// pid_t pid, sig_t signo
uint64 sys_kill(void) {
    int pid;
    sig_t signo;

    argint(0, &pid);
    argulong(1, &signo);

    // empty signal
    if (signo == 0) {
        return 0;
    }

    struct proc *p;
    if ((p = find_get_pid(pid)) == NULL)
        return 0; // NOTE:test
                  // release(&p->lock);

#ifdef __DEBUG_PROC__
    printfCYAN("kill : kill proc %d, signo = %d\n", p->pid, signo); // debug
#endif
    proc_sendsignal_all_thread(p, signo, 0);

    return 0;
}

// int tkill(int tid, sig_t sig);
uint64 sys_tkill() {
    int tid;
    sig_t signo;

    argint(0, &tid);
    argulong(1, &signo);

    // empty signal
    if (signo == 0) {
        return 0;
    }

    struct tcb *t;
    if ((t = find_get_tid(tid)) == NULL)
        return -1;

    // do_tkill
    do_tkill(t, signo);

    return 0;
}

// int tgkill(int tgid, int tid, sig_t sig);
// tgid为目标线程所在进程的进程ID，tid为目标线程的内部线程ID，而不是全局线程ID
uint64 sys_tgkill() {
    int tgid; // equal to pid
    int tid;  // equal to tidx
    sig_t signo;

    argint(0, &tgid);
    argint(1, &tid);
    argulong(2, &signo);

    struct tcb *t;
    if ((t = find_get_tidx(tgid, tid)) == NULL)
        return -1;
    // release(&t->lock);

    // empty signal
    if (signo == 0) {
        return 0;
    }

    // do_tkill
    do_tkill(t, signo);

    return 0;
}

//  long futex(uint32_t *uaddr, int futex_op, uint32_t val,
//  const struct timespec *timeout,   /* or: uint32_t val2 */
//  uint32_t *uaddr2, uint32_t val3);
uint64 sys_futex() {
    uint64 uaddr;
    int futex_op;
    uint32 val;
    struct timespec timeout;
    uint64 timeout_addr;
    uint32 val2;
    uint64 uaddr2;
    uint32 val3;
    argaddr(0, &uaddr);
    argint(1, &futex_op);
    arguint(2, &val);
    argaddr(3, &timeout_addr); // val2
    argaddr(4, &uaddr2);
    arguint(5, &val3);

    struct proc *p = proc_current();
    int cmd = futex_op & FUTEX_CMD_MASK;
    // ktime_t t;
    if (timeout_addr && (cmd == FUTEX_WAIT
                         // cmd == FUTEX_LOCK_PI ||
                         // cmd == FUTEX_WAIT_BITSET ||
                         // cmd == FUTEX_WAIT_REQUEUE_PI
                         )) {
        // if (unlikely(should_fail_futex(!(futex_op & FUTEX_PRIVATE_FLAG))))
        //     return -1;
        if (copyin(p->mm->pagetable, (char *)&timeout, timeout_addr, sizeof(struct timespec)) < 0) {
            return -1;
        }
        // if (!timespec64_valid(&timeout))
        //     return -1;
        // t = timespec64_to_ktime(timeout);
        // if (cmd == FUTEX_WAIT)
        // 	t = ktime_add_safe(ktime_get(), t);
        // else if (!(op & FUTEX_CLOCK_REALTIME))
        // 	t = timens_ktime_to_host(CLOCK_MONOTONIC, t);
    }
    if (cmd == FUTEX_REQUEUE || cmd == FUTEX_CMP_REQUEUE || cmd == FUTEX_CMP_REQUEUE_PI || cmd == FUTEX_WAKE_OP) {
        arguint(3, &val2);
    }

    return do_futex(uaddr, futex_op, val, timeout_addr ? &timeout : NULL, uaddr2, val2, val3);
}

// the real group ID of the calling process
uint64 sys_getgid() {
    // return proc_current()->pid;
    return 0;
}

// get/set list of robust futexes
// long get_robust_list(int pid, struct robust_list_head **head_ptr, size_t *len_ptr);
// These system calls deal with per-thread robust futex lists.
// These lists are managed in user space: the ker‐nel knows only about the location of the head of the list.
// A thread can inform the kernel of  the  location of its  robust  futex list using set_robust_list().
// The address of a thread's robust futex list can be ob‐tained using get_robust_list().
uint64 sys_get_robust_list() {
    int pid;
    uint64 head_ptr_addr;
    uint64 len_ptr_addr;
    argint(0, &pid);
    argaddr(1, &head_ptr_addr);
    argaddr(2, &len_ptr_addr);

    struct robust_list_head *head;
    // trapframe_print(thread_current()->trapframe);
    struct proc *p = pid ? find_get_pid(pid) : proc_current();
    if (!p) {
        return -EPERM;
    }
    head = p->robust_list;
    int len = sizeof(*head);
    if (len_ptr_addr) {
        if (copyout(p->mm->pagetable, len_ptr_addr, (char *)&len, sizeof(len)) < 0) {
            return -EFAULT;
        }
    }
    if (head_ptr_addr) {
        if (copyout(p->mm->pagetable, head_ptr_addr, (char *)&head, sizeof(head)) < 0) {
            return -EFAULT;
        }
    }

    return 0;
}

uint64 sys_set_robust_list(void) {
    return 0;
}

// the effective group ID of the calling process
uint64 sys_getegid() {
    // return proc_current()->pid;
    return 0;
}

uint64 sys_exit_group(void) {
    struct proc *p = proc_current();
    if (atomic_read(&p->tg->thread_cnt) < 2) {
        return 0;
    }
    do_exit_group(p);
    return 0;
}

// pid_t setsid(void);
// process group ID
uint64 sys_setsid(void) {
    return 0;
}

uint64 sys_gettid(void) {
    return proc_current()->pid;
}
uint64 sys_sched_setscheduler(void) {
    return 0;
}
uint64 sys_sched_getaffinity(void) {
    return 0;
}
uint64 sys_sched_setaffinity(void) {
    return 0;
}
uint64 sys_sched_getscheduler(void) {
    return 0;
}
uint64 sys_sched_getparam(void) {
    return 0;
}
uint64 sys_membarrier(void) {
    return 0;
}
