#include "debug.h"
#include "common.h"
#include "proc/pcb_mm.h"
#include "kernel/trap.h"
#include "proc/sched.h"
#include "lib/riscv.h"
#include "lib/sbi.h"
#include "memory/buddy.h"
#include "debug.h"
#include "fs/vfs/fs.h"
#include "fs/bio.h"
#include "kernel/syscall.h"
#include "lib/ctype.h"
#include "lib/timer.h"
#include "lib/resource.h"
#include "debug.h"
#include "ipc/signal.h"
#include "memory/vm.h"
#include "memory/allocator.h"
#include "kernel/syscall.h"

extern atomic_t ticks;
extern struct cond cond_ticks;

struct tms {
    long tms_utime;
    long tms_stime;
    long tms_cutime;
    long tms_cstime;
};

struct utsname {
    char sysname[65];
    char nodename[65];
    char release[65];
    char version[65];
    char machine[65];
    char domainname[65];
};

struct utsname sys_ut = {
    "LostWakeup OS",
    "oskernel",
    .release = "5.0",
    .version = "0.0.0",
    "RISCV",
    "www.hdu.lostwakeup.edu.cn",
};

/*
 * 功能：获取进程时间；
 * 输入：tms结构体指针，用于获取保存当前进程的运行时间数据；
 * 返回值：成功返回已经过去的滴答数，失败返回-1;
 */
// struct tms *tms;s
uint64 sys_times(void) {
    uint64 addr;
    argaddr(0, &addr);

    // struct proc *p = proc_current();
    struct tms tms_buf;
    // tms_buf.tms_stime = p->tms_stime;

    tms_buf.tms_stime = 0;
    tms_buf.tms_utime = 0;
    tms_buf.tms_cstime = 0;
    tms_buf.tms_cutime = 0;

    // TODO: utime and stime
    if (either_copyout(1, addr, &tms_buf, sizeof(tms_buf)) == -1)
        return -1;

    return atomic_read(&ticks);
}
/*
 * 功能：打印系统信息；
 * 输入：utsname结构体指针用于获得系统信息数据；
 * 返回值：成功返回0，失败返回-1;
 */
// struct utsname *uts;
uint64 sys_uname(void) {
    uint64 addr;
    uint64 ret = 0;
    argaddr(0, &addr);

    if (either_copyout(1, addr, &sys_ut, sizeof(sys_ut)) == -1)
        ret = -1;

    return ret;
}

/*
 * 功能：让出调度器；
 * 输入：系统调用ID；
 * 返回值：成功返回0，失败返回-1;
 */
//
uint64 sys_sched_yield(void) {
    thread_yield();
    return 0;
}

/*
 * 功能：获取时间；
 * 输入： timespec结构体指针用于获得时间值；
 * int gettimeofday(struct timeval *tv, struct timezone *tz);
 * 返回值：成功返回0，失败返回-1;
 */
uint64 sys_gettimeofday(void) {
    uint64 addr;
    argaddr(0, &addr);
    uint64 time = rdtime();
    struct timeval tv_buf = TIME2TIMEVAL(time);
    // Log("%ld", tv_buf.tv_sec);
    // Log("%ld", tv_buf.tv_usec);
    if (copyout(proc_current()->mm->pagetable, addr, (char *)&tv_buf, sizeof(tv_buf)) < 0) {
        return -1;
    }
    return 0;
}

/*
 * 功能：执行线程睡眠，sleep()库函数基于此系统调用；
 * 输入：睡眠的时间间隔；
 * int nanosleep(const struct timespec *req, struct timespec *rem);
 * 返回值：成功返回0，失败返回-1;
 */
uint64 sys_nanosleep(void) {
    /* NOTE:currently, we do not support rem! */
// #if defined (SIFIVE_U) || defined (SIFIVE_B)
//     return 0;
// #endif
    // uint64 req;
    // argaddr(0, &req);

    // struct timespec ts_buf;
    // if (copyin(proc_current()->mm->pagetable, (char *)&ts_buf, req, sizeof(ts_buf)) == -1) {
    //     return -1;
    // }

    // do_sleep_ns(thread_current(), ts_buf);
    return 0;
}

extern void shutdown_writeback(void);
uint64 sys_shutdown() {
    // syscall_count_analysis();
    shutdown_writeback();
    
    // printfGreen("mm: %d pages when shutdown\n", get_free_mem()/4096);
    sbi_shutdown();

    panic("shutdown: can not reach here");
}

typedef int clockid_t;
#define CLOCK_REALTIME 0
// int clock_gettime(clockid_t clockid, struct timespec *tp);
uint64 sys_clock_gettime(void) {
    int clockid;
    uint64 tp;
    struct timespec ts_buf;
    argint(0, &clockid);
    argaddr(1, &tp);
    uint64 time = rdtime();
    ts_buf = TIME2TIMESPEC(time);

    if (copyout(proc_current()->mm->pagetable, tp, (char *)&ts_buf, sizeof(ts_buf)) < 0) {
        return -1;
    }
    // extern int print_tf_flag;
    // print_tf_flag = 1;

    return 0;
}

struct sysinfo {
    long uptime; /* Seconds since boot */
    // unsigned long loads[3];  /* 1, 5, and 15 minute load averages */
    unsigned long totalram; /* Total usable main memory size */
    unsigned long freeram;  /* Available memory size */
    // unsigned long sharedram; /* Amount of shared memory */
    // unsigned long bufferram; /* Memory used by buffers */
    // unsigned long totalswap; /* Total swap space size */
    // unsigned long freeswap;  /* Swap space still available */
    unsigned short procs; /* Number of current processes */
    // unsigned long totalhigh; /* Total high memory size */
    // unsigned long freehigh;  /* Available high memory size */
    // unsigned int mem_unit;   /* Memory unit size in bytes */
    // char _f[20-2*sizeof(long)-sizeof(int)]; /* Padding to 64 bytes */
};

// int sysinfo(struct sysinfo *info);
uint64 sys_sysinfo(void) {
    uint64 info;
    argaddr(0, &info);

    long time_s = TIME2SEC(rdtime());
    struct sysinfo info_buf;
    info_buf.uptime = time_s;
    info_buf.freeram = get_free_mem();
    info_buf.totalram = PHYSTOP - START_MEM; // 120MB
    info_buf.procs = get_current_procs();

    if (copyout(proc_current()->mm->pagetable, info, (char *)&info_buf, sizeof(info_buf)) < 0) {
        return -1;
    }

    return 0;
}

// void syslog(int priority, const char *format, ...);
uint64 sys_syslog(void) {
    int priority;
    // char* format;
    argint(0, &priority);
    // argaddr(1, format);
    // Log(format);

    return 0;
}

/* inefficient, use for debug only! */
static void print_rawstr(const char *c, size_t len) {
    for (int i = 0; i < len; i++) {
        if (*(c + i) == ' ') {
            printf(" ");
        } else if (isalnum(*(c + i)) || *(c + i) == '.' || *(c + i) == '~' || *(c + i) == '_') {
            printf("%c", *(c + i));
        } else {
            printf(" ");
        }
    }
}

static void print_short_dir(const dirent_s_t *buf) {
    printf("(short) ");
    printf("name: ", buf->DIR_Name);
    print_rawstr((char *)buf->DIR_Name, FAT_SFN_LENGTH);
    printf("  ");
    printf("attr: %#x\t", buf->DIR_Attr);
    printf("dev: %d\t", buf->DIR_Dev);
    printf("filesize %d\n", buf->DIR_FileSize);
    // printf("create_time_tenth %d\t", buf->DIR_CrtTimeTenth);
    // printf("create_time %d\n", buf->DIR_CrtTime);
    // printf("crea");
}

static void print_long_dir(const dirent_l_t *buf) {
    printf("(long) ");
    printf("name1: ");
    print_rawstr((char *)buf->LDIR_Name1, 10);
    printf("  ");
    printf("name2: ");
    print_rawstr((char *)buf->LDIR_Name2, 12);
    printf("  ");
    printf("name3: ");
    print_rawstr((char *)buf->LDIR_Name3, 4);
    printf("  ");
    printf("attr %#x\n", buf->LDIR_Attr);
}

void print_rawfile(struct file *f, int fd, int printdir);
void sys_print_rawfile(void) {
    int fd;
    int printdir;
    struct file *f;

    printfGreen("==============\n");
    if (argfd(0, &fd, &f) < 0) {
        printfGreen("file doesn't open!\n");
        return;
    }
    argint(1, &printdir);
    print_rawfile(f, fd, printdir);
}

void print_rawfile(struct file *f, int fd, int printdir) {
    ASSERT(f->f_type == FD_INODE);
    int cnt = 0;
    int pos = 0;
    uint64 iter_c_n = f->f_tp.f_inode->fat32_i.cluster_start;

    printfGreen("fd is %d\n", fd);
    struct inode *ip = f->f_tp.f_inode;
    if (!S_ISDIR(ip->i_mode) && printdir == 1) {
        printfGreen("the file is not a directory\n");
        return;
    }
    int off = 0;
    // print logistic clu.no and address(in fat32.img)
    while (!ISEOF(iter_c_n)) {
        uint64 addr = (iter_c_n - fat32_sb.fat32_sb_info.root_cluster_s) * __BPB_BytsPerSec * __BPB_SecPerClus + FSIMG_STARTADDR;
        printfGreen("cluster no: %d\t address: %p \toffset %d(%#p)\n", cnt++, addr, pos, pos);
        if (printdir == 1) {
            int first_sector = FirstSectorofCluster(iter_c_n);
            int init_s_n = LOGISTIC_S_NUM(pos);
            struct buffer_head *bp;
            for (int s = init_s_n; s < __BPB_SecPerClus; s++) {
                bp = bread(ip->i_dev, first_sector + s);
                for (int i = 0; i < 512 && i < ip->i_size; i += 32) {
                    dirent_s_t *tmp = (dirent_s_t *)(bp->data + i);
                    printf("%x ", off++);
                    if (LONG_NAME_BOOL(tmp->DIR_Attr)) {
                        print_long_dir((dirent_l_t *)(bp->data + i));
                    } else {
                        print_short_dir((dirent_s_t *)(bp->data + i));
                    }
                }
                printfRed("===Sector %d end===\n", s);
                brelse(bp);
            }
        }
        pos += __BPB_BytsPerSec * __BPB_SecPerClus;
        iter_c_n = fat32_next_cluster(iter_c_n);
    }
    printfGreen("file size is %d(%#p)\n", f->f_tp.f_inode->i_size, f->f_tp.f_inode->i_size);
    printfGreen("==============\n");
    return;
}

// RLIMIT_NOFILE and RLIMIT_STACK
int do_prlimit(struct proc *p, uint32 resource, struct rlimit *new_rlim, struct rlimit *old_rlim) {
    // 	struct rlimit *rlim;
    int retval = 0;

    if (resource >= RLIM_NLIMITS)
        return -EINVAL;

    // if (new_rlim) {
    // 	if (new_rlim->rlim_cur > new_rlim->rlim_max)
    // 		return -EINVAL;
    // 	if (resource == RLIMIT_NOFILE && new_rlim->rlim_max > sysctl_nr_open)
    // 		return -EPERM;
    // }
    struct rlimit *rlim = p->rlim + resource;
    if (!retval) {
        if (old_rlim)
            *old_rlim = *rlim;
        if (new_rlim) {
            *rlim = *new_rlim;
            switch (resource) {
            case RLIMIT_NOFILE:
                p->max_ofile = new_rlim->rlim_max;
                p->cur_ofile = new_rlim->rlim_cur;
                break;
            case RLIMIT_STACK:
                // panic("stack not tested\n");
                break;
            default:
                panic("RLIMIT : not tested\n");
                break;
            }
        }
    }
    // if (res == RLIMIT_STACK) {
    //     if (oldrl) {
    //         rl.rlim_cur = p->mm->ustack->len;
    //         rl.rlim_max = 30 * PGSIZE;
    //         if (copyout(oldrl, (char*)&rl, sizeof(struct rlimit)) < 0) {
    //             return -1;
    //         }
    //     }

    //     if (newrl) {
    //         if (copy_from_user(&rl, newrl, sizeof(struct rlimit)) < 0)
    //             return -1;
    //         if (mmap_ext_stack(p->mm, rl.rlim_cur) < 0)
    //             return -1;
    //     }
    // } else if (res == RLIMIT_NOFILE) {
    //     if (oldrl) {
    //         rl.rlim_cur = p->fdtable->max_nfd;
    //         rl.rlim_max = p->fdtable->max_nfd;
    //         if (copyout(oldrl, (char*)&rl, sizeof(struct rlimit)) < 0) {
    //             return -1;
    //         }
    //     }

    //     if (newrl) {
    //         if (copy_from_user(&rl, newrl, sizeof(struct rlimit)) < 0)
    //             return -1;

    //         return fdtbl_setmaxnfd(p->fdtable, rl.rlim_cur);
    //     }
    // } else {
    //     debug("ukres %d", res);
    //     return -1;
    // }

    // 	if (new_rlim) {
    // 		if (new_rlim->rlim_cur > new_rlim->rlim_max)
    // 			return -EINVAL;
    // 		if (resource == RLIMIT_NOFILE &&
    // 				new_rlim->rlim_max > sysctl_nr_open)
    // 			return -EPERM;
    // 	}

    // 	/* protect tsk->signal and tsk->sighand from disappearing */
    // 	read_lock(&tasklist_lock);
    // 	if (!tsk->sighand) {
    // 		retval = -ESRCH;
    // 		goto out;
    // 	}

    // 	rlim = tsk->signal->rlim + resource;
    // 	task_lock(tsk->group_leader);
    // 	if (new_rlim) {
    // 		/* Keep the capable check against init_user_ns until
    // 		   cgroups can contain all limits */
    // 		if (new_rlim->rlim_max > rlim->rlim_max &&
    // 				!capable(CAP_SYS_RESOURCE))
    // 			retval = -EPERM;
    // 		if (!retval)
    // 			retval = security_task_setrlimit(tsk, resource, new_rlim);
    // 	}
    // 	if (!retval) {
    // 		if (old_rlim)
    // 			*old_rlim = *rlim;
    // 		if (new_rlim)
    // 			*rlim = *new_rlim;
    // 	}
    // 	task_unlock(tsk->group_leader);

    // 	/*
    // 	 * RLIMIT_CPU handling. Arm the posix CPU timer if the limit is not
    // 	 * infite. In case of RLIM_INFINITY the posix CPU timer code
    // 	 * ignores the rlimit.
    // 	 */
    // 	 if (!retval && new_rlim && resource == RLIMIT_CPU &&
    // 	     new_rlim->rlim_cur != RLIM_INFINITY &&
    // 	     IS_ENABLED(CONFIG_POSIX_TIMERS))
    // 		update_rlimit_cpu(tsk, new_rlim->rlim_cur);
    // out:
    // read_unlock(&tasklist_lock);
    return retval;
}

static inline int rlim64_is_infinity(__u64 rlim64) {
    return rlim64 == RLIM64_INFINITY;
}

static void rlim_to_rlim64(const struct rlimit *rlim, struct rlimit64 *rlim64) {
    if (rlim->rlim_cur == RLIM_INFINITY)
        rlim64->rlim_cur = RLIM64_INFINITY;
    else
        rlim64->rlim_cur = rlim->rlim_cur;
    if (rlim->rlim_max == RLIM_INFINITY)
        rlim64->rlim_max = RLIM64_INFINITY;
    else
        rlim64->rlim_max = rlim->rlim_max;
}

static void rlim64_to_rlim(const struct rlimit64 *rlim64, struct rlimit *rlim) {
    if (rlim64_is_infinity(rlim64->rlim_cur))
        rlim->rlim_cur = RLIM_INFINITY;
    else
        rlim->rlim_cur = (unsigned long)rlim64->rlim_cur;
    if (rlim64_is_infinity(rlim64->rlim_max))
        rlim->rlim_max = RLIM_INFINITY;
    else
        rlim->rlim_max = (unsigned long)rlim64->rlim_max;
}

// int prlimit(pid_t pid, int resource, const struct rlimit *new_limit, struct rlimit *old_limit);
uint64 sys_prlimit64(void) {
    pid_t pid;
    int resource;
    uint64 new_limit_addr;
    uint64 old_limit_addr;
    struct rlimit new_limit, old_limit;
    struct rlimit64 new64_limit, old64_limit;

    argint(0, &pid);
    argint(1, &resource);
    argaddr(2, &new_limit_addr);
    argaddr(3, &old_limit_addr);

    // struct task_struct *tsk;
    struct proc *p;
    // unsigned int checkflags = 0;
    int ret;

    if (new_limit_addr) {
        if (copyin(proc_current()->mm->pagetable, (char *)&new64_limit, new_limit_addr, sizeof(new64_limit)) < 0) {
            return -EFAULT;
        }
        rlim64_to_rlim(&new64_limit, &new_limit);
    }
    // rcu_read_lock();
    p = pid ? find_get_pid(pid) : proc_current();
    if (!p) {
        panic("proc get error\n");
    }
    acquire(&p->lock);

    ret = do_prlimit(p, resource, new_limit_addr ? &new_limit : NULL, old_limit_addr ? &old_limit : NULL);

    if (!ret && old_limit_addr) {
        rlim_to_rlim64(&old_limit, &old64_limit);
        if (copyout(proc_current()->mm->pagetable, old_limit_addr, (char *)&old64_limit, sizeof(old64_limit)) < 0) {
            ret = -EFAULT;
        }
    }

    release(&p->lock);
    // put_task_struct(tsk);
    return ret;
}

/*
 * Returns true if the timeval is in canonical form
 */
#define USEC_PER_SEC 1000000
#define timeval_valid(t) (((t)->tv_sec >= 0) && (((unsigned long)(t)->tv_usec) < USEC_PER_SEC))

void setitimer_REAL_callback(void *ptr) {
    struct proc *p = (struct proc *)ptr;
    sig_t signo = SIGALRM;

// printfCYAN("kill : kill proc %d, signo = %d\n", p->pid, signo); // debug
#ifdef __DEBUG_SIGNAL__
    printf("send SIGALRM(14) signal to pid : %d\n", p->pid);
#endif
    proc_sendsignal_all_thread(p, signo, 1);
}

int do_setitimer(int which, struct itimerval *value, struct itimerval *ovalue) {
    // 	struct task_struct *tsk = current;
    // 	struct hrtimer *timer;
    // 	ktime_t expires;
    // struct tcb* t = thread_current();
    struct proc *p = proc_current();
    struct timer_list *timer;
    /*
     * Validate the timevals in value.
     */
    if (!timeval_valid(&value->it_value) || !timeval_valid(&value->it_interval))
        return -EINVAL;

    switch (which) {
    case ITIMER_REAL:
        // This timer counts down in real (i.e., wall clock) time.  At each expiration, a SIGALRM signal is generated.
        // printfRed("real not tested\n");
        // again:
        // 		spin_lock_irq(&tsk->sighand->siglock);
        // timer = &t->real_timer;
        timer = &p->real_timer;
        if (ovalue) {
            ovalue->it_value = TIME2TIMEVAL(timer->expires);
            ovalue->it_interval = TIME2TIMEVAL(timer->interval);
        }
        uint64 base = TIMEVAL2NS(value->it_value);
        uint64 interval = TIMEVAL2NS(value->it_interval);
#ifdef __DEBUG_SIGNAL__
        printfMAGENTA("setitimer , pid : %d, base : %ld(ns)/%ld(s), interval : %ld(ns)/%ld(s)", p->pid, base, NS_to_S(base), interval, NS_to_S(interval));
#endif

        if (base == 0) {
            // delete_timer_atomic(&p->real_timer);// bug ???
            return 0;
        }
        timer->count = interval ? -1 : 0; // bug!!!
        // bug like this : timer->count = interval ? 0 : -1;
        timer->interval = interval;

        int rate = 1;
        // uint64 time_out = S_to_NS(dirty_writeback_cycle);
        add_timer_atomic(timer, base * rate, setitimer_REAL_callback, (void *)p);
        // 		if (ovalue) {
        // 			ovalue->it_value = itimer_get_remtime(timer);
        // 			ovalue->it_interval
        // 				= ktime_to_timeval(tsk->signal->it_real_incr);
        // 		}
        // 		/* We are sharing ->siglock with it_real_fn() */
        // 		if (hrtimer_try_to_cancel(timer) < 0) {
        // 			spin_unlock_irq(&tsk->sighand->siglock);
        // 			goto again;
        // 		}
        // 		expires = timeval_to_ktime(value->it_value);
        // 		if (expires.tv64 != 0) {
        // 			tsk->signal->it_real_incr =
        // 				timeval_to_ktime(value->it_interval);
        // 			hrtimer_start(timer, expires, HRTIMER_MODE_REL);
        // 		} else
        // 			tsk->signal->it_real_incr.tv64 = 0;
        // 		trace_itimer_state(ITIMER_REAL, value, 0);
        // 		spin_unlock_irq(&tsk->sighand->siglock);
        break;
    case ITIMER_VIRTUAL:
        printfRed("virtual not tested\n");
        // 		set_cpu_itimer(tsk, CPUCLOCK_VIRT, value, ovalue);
        // 		break;
    case ITIMER_PROF:
        printfRed("prof not tested\n");
        // 		set_cpu_itimer(tsk, CPUCLOCK_PROF, value, ovalue);
        break;
    default:
        return -EINVAL;
    }
    return 0;
}

// int setitimer(int which, const struct itimerval *new_value, struct itimerval *old_value);
// set value of an interval timer
uint64 sys_setitimer(void) {
    int which;
    uint64 new_value_addr;
    uint64 old_value_addr;
    struct itimerval new_value;
    struct itimerval old_value;
    argint(0, &which);
    argaddr(1, &new_value_addr);
    argaddr(2, &old_value_addr);

    int error;
    if (new_value_addr) {
        if (copyin(proc_current()->mm->pagetable, (char *)&new_value, new_value_addr, sizeof(new_value)) < 0) {
            return -EFAULT;
        }
    } else {
        memset((char *)&new_value, 0, sizeof(new_value));
    }
    error = do_setitimer(which, &new_value, old_value_addr ? &old_value : NULL);
    if (error || !old_value_addr)
        return error;
    if (copyout(proc_current()->mm->pagetable, old_value_addr, (char *)&old_value, sizeof(old_value)) < 0) {
        return -EFAULT;
    }
    return 0;
}

// int clock_getres(clockid_t clockid, struct timespec *res);
uint64 sys_clock_getres(void) {
    clockid_t clockid;
    uint64 res_addr;
    argint(0, &clockid);
    argaddr(1, &res_addr);

    struct timespec res;
    int error = 0;

    switch (clockid) {
    case CLOCK_MONOTONIC:
        // not important !
        res.ts_sec = 0;
        res.ts_nsec = 1;
        break;

    default:
        panic("clock_getres : not tested\n");
        break;
    }

    struct proc *p = proc_current();
    if (!error && res_addr && copyout(p->mm->pagetable, res_addr, (char *)&res, sizeof(res)) < 0) error = -EFAULT;

    return error;
}

// high-resolution sleep with specifiable clock
// int clock_nanosleep(clockid_t clockid, int flags, const struct timespec *request, struct timespec *remain);
uint64 sys_clock_nanosleep(void) {
    clockid_t clockid;
    int flags;
    uint64 request_addr;
    uint64 remain_addr;
    struct timespec request;
    argint(0, &clockid);
    argint(1, &flags);
    argaddr(2, &request_addr);
    argaddr(3, &remain_addr);

    if (copyin(proc_current()->mm->pagetable, (char *)&request, request_addr, sizeof(request)) < 0)
        return -EFAULT;

    if (remain_addr) {
        panic("clock_nanosleep, not tested\n");
    }
    struct tcb *t = thread_current();

    if (flags == 0) {
        do_sleep_ns(t, request);
    } else if (flags == TIMER_ABSTIME) {
        uint64 request_ns = TIMESEPC2NS(request);
        uint64 now_ns = TIME2NS(rdtime());
        if (now_ns >= request_ns) {
            return 0;
        } else {
            acquire(&cond_ticks.waiting_queue.lock);
            t->time_out = request_ns - now_ns;
            cond_wait(&cond_ticks, &cond_ticks.waiting_queue.lock);
            release(&cond_ticks.waiting_queue.lock);
        }
    }

    switch (clockid) {
    case CLOCK_MONOTONIC:
        break;
    // case CLOCK_REALTIME:
    //     break;
    // case CLOCK_TAI:
    //     break;
    default:
        panic("clock_nanosleep, clockid not tested\n");
        break;
    }

    return 0;
}

// int getrusage(int who, struct rusage *usage);
uint64 sys_getrusage(void) {
    int who;
    uint64 usage;
    paddr_t pa;
    struct proc *p = proc_current();

    argint(0, &who);
    argaddr(1, &usage);

    pa = getphyaddr(proc_current()->mm->pagetable, usage);
    switch (who) {
    case RUSAGE_SELF: {
        struct timeval utime = TIME2TIMEVAL(p->utime);
        struct timeval stime = TIME2TIMEVAL(p->stime);
        memmove((void *)(&((struct rusage *)pa)->ru_utime), (const void *)&utime, sizeof(struct timeval));
        memmove((void *)(&((struct rusage *)pa)->ru_stime), (const void *)&stime, sizeof(struct timeval));
        break;
    }
    default:
        Warn("not support");
        return -1;
    }

    return 0;
}

uint64 sys_umask(void) {
    return 0x777;
}

uint64 sys_msync(void) {
    return 0;
}
uint64 sys_readlinkat(void) {
    return 0;
}
//    int madvise(void *addr, size_t length, int advice);
uint64 sys_madvise(void) {
    return 0;
}

uint64 sys_setpgid(void) {
    return 0;
}

uint64 sys_getpgid(void) {
    return 0;
}

uint64 sys_prctl(void) {
    return 0;
}