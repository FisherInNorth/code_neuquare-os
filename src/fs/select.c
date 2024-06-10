#include "common.h"
#include "kernel/syscall.h"
#include "fs/select.h"
#include "ipc/signal.h"
#include "proc/tcb_life.h"
#include "proc/pcb_life.h"
#include "lib/poll.h"
#include "errno.h"
#include "memory/allocator.h"
#include "debug.h"
#include "ipc/pipe.h"
#include "proc/pcb_life.h"
#include "driver/console.h"

extern struct cond cond_ticks;

int do_select(int nfds, fd_set_bits *fds, uint64 timeout) {
    int retval;

    uint64 time_now = TIME2NS(rdtime());
    struct proc *p = proc_current();
    retval = 0;
    for (;;) {
        uint64 *rinp, *routp, *rexp, *inp, *outp, *exp;

        inp = fds->in;
        outp = fds->out;
        exp = fds->ex;
        rinp = fds->res_in;
        routp = fds->res_out;
        rexp = fds->res_ex;

        for (int i = 0; i < nfds; ++rinp, ++routp, ++rexp) {
            uint64 in, out, ex;
            uint64 bit = 1;
            uint64 res_in = 0, res_out = 0, res_ex = 0;
            struct file *file = NULL;

            in = *inp++;
            out = *outp++;
            ex = *exp++;
            uint64 all_bits = in | out | ex;

            if (all_bits == 0) {
                i += __NFDBITS;
                continue;
            }
            for (int j = 0; j < __NFDBITS; ++j, ++i, bit <<= 1) {
                // 			int fput_needed;
                if (i >= nfds) {
                    panic("i>=nfds : not tested\n");
                    break;
                }
                if (!(bit & all_bits))
                    continue;
                file = p->ofile[i];
                if (file) {
                    switch (file->f_type) {
                    case FD_INODE:
                        break;
                    case FD_PIPE: {
                    }
                    default:
                        Warn("TYPE is %d", file->f_type);
                        panic("this type not tested\n");
                    }
                    if (in & bit) {
                        res_in |= bit;
                        retval++;
                    }
                    if (out & bit) {
                        res_out |= bit;
                        retval++;
                    }
                    if (ex & bit) {
                        res_ex |= bit;
                        retval++;
                    }
                }
            }
            if (res_in)
                *rinp = res_in;
            if (res_out)
                *routp = res_out;
            if (res_ex)
                *rexp = res_ex;
        }

        // 	wait = NULL;
        if (retval || (TIME2NS(rdtime()) > time_now + timeout)) {
            // printf("ready\n");
            // printfGreen("select , pid : %d exit\n", proc_current()->pid);// debug
            break;
        } else {
            // TODO : set a waiting queue
        }
    }

    return retval;
}

int core_sys_select(int nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds, uint64 timeout) {
    fd_set_bits fds;
    void *bits;
    int ret;
    // 	int ret, max_fds;
    uint32 size;
    // 	struct fdtable *fdt;
    // 	/* Allocate small arguments on the stack to save memory and be faster */
    long stack_fds[SELECT_STACK_ALLOC / sizeof(long)];

    ret = -EINVAL;
    if (nfds > NOFILE) {
        panic("nfds > NOFILE, not tested\n");
    }

    // 	if (nfds < 0)
    // 		goto out_nofds;

    // 	/* max_fds can increase, so grab it once to avoid race */
    // 	rcu_read_lock();
    // 	fdt = files_fdtable(current->files);
    // 	max_fds = fdt->max_fds;
    // 	rcu_read_unlock();
    // 	if (nfds > max_fds)
    // 		nfds = max_fds;

    /*
     * We need 6 bitmaps (in/out/ex for both incoming and outgoing),
     * since we used fdset we need to allocate memory in units of
     * long-words.
     */

    size = FDS_BYTES(nfds);
    bits = stack_fds;
    if (size > sizeof(stack_fds) / 6) {
        /* Not enough space in on-stack array; must use kmalloc */
        ret = -ENOMEM;
        bits = kmalloc(6 * size);
        if (!bits)
            panic("no memory\n");
        // goto out_nofds;
    }
    fds.in = bits;
    fds.out = bits + size;
    fds.ex = bits + 2 * size;
    fds.res_in = bits + 3 * size;
    fds.res_out = bits + 4 * size;
    fds.res_ex = bits + 5 * size;

    // if ((ret = get_fd_set(nfds, readfds, fds.in)) || (ret = get_fd_set(nfds, writefds, fds.out)) || (ret = get_fd_set(nfds, exceptfds, fds.ex)))
    // 	// goto out;
    // 	panic("core_sys_select : get_fd_set error\n");
    // copy_fd_set(nfds, fds.in, readfds);
    // copy_fd_set(nfds, fds.out, writefds);
    // copy_fd_set(nfds, fds.ex, exceptfds);
    fds.in = (uint64 *)readfds;
    fds.out = (uint64 *)writefds;
    fds.ex = (uint64 *)exceptfds;
    zero_fd_set(nfds, fds.res_in);
    zero_fd_set(nfds, fds.res_out);
    zero_fd_set(nfds, fds.res_ex);

    ret = do_select(nfds, &fds, timeout);
    if (ret < 0) {
        panic("ret < 0, not tested\n");
    }
    // 	if (ret < 0)
    // 		goto out;
    // 	if (!ret) {
    // 		ret = -ERESTARTNOHAND;
    // 		if (signal_pending(current))
    // 			goto out;
    // 		ret = 0;
    // 	}
    // if (set_fd_set(nfds, readfds, fds.res_in) || set_fd_set(nfds, writefds, fds.res_out) || set_fd_set(nfds, exceptfds, fds.res_ex))
    //     // ret = -EFAULT;
    // 	panic("core_sys_select : set_fd_set error\n");
    return ret;

    // out:
    // 	if (bits != stack_fds)
    // 		kfree(bits);
    // out_nofds:
    // 	return ret;
}

int poll_select_copy_remaining(uint64 timeout, void *p, int timeval, int ret) {
    // 	struct timespec rts;
    // 	struct timeval rtv;

    // 	if (!p)
    // 		return ret;

    // 	if (current->personality & STICKY_TIMEOUTS)
    // 		goto sticky;

    // 	/* No update for zero timeout */
    // 	if (!end_time->tv_sec && !end_time->tv_nsec)
    // 		return ret;

    // 	ktime_get_ts(&rts);
    // 	rts = timespec_sub(*end_time, rts);
    // 	if (rts.tv_sec < 0)
    // 		rts.tv_sec = rts.tv_nsec = 0;

    // 	if (timeval) {
    // 		rtv.tv_sec = rts.tv_sec;
    // 		rtv.tv_usec = rts.tv_nsec / NSEC_PER_USEC;

    // 		if (!copy_to_user(p, &rtv, sizeof(rtv)))
    // 			return ret;

    // 	} else if (!copy_to_user(p, &rts, sizeof(rts)))
    // 		return ret;

    // 	/*
    // 	 * If an application puts its timeval in read-only memory, we
    // 	 * don't want the Linux-specific update to the timeval to
    // 	 * cause a fault after the select has completed
    // 	 * successfully. However, because we're not updating the
    // 	 * timeval, we can't restart the system call.
    // 	 */

    // sticky:
    // 	if (ret == -ERESTARTNOHAND)
    // 		ret = -EINTR;
    // 	return ret;
    return 0;
}

long do_pselect(int nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds, struct timespec *tsp, const sigset_t *sigmask, size_t sigsetsize) {
    int ret;

    uint64 time_out = (tsp != NULL ? TIMESEPC2NS((*tsp)) : 0);

    ret = core_sys_select(nfds, readfds, writefds, exceptfds, time_out);

    // if ()

    return ret;
}

// select, pselect, FD_CLR, FD_ISSET, FD_SET, FD_ZERO - synchronous I/O multiplexing
// int pselect(int nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds, const struct timespec *timeout, const sigset_t *sigmask);
uint64 sys_pselect6(void) {
    int nfds;
    uint64 readfds_addr, writefds_addr, exceptfds_addr, timeout_addr;
    // uint64 sigmask_addr;
    fd_set readfds, writefds, exceptfds;
    struct timespec timeout;
    // sigset_t sigmask;
    argint(0, &nfds);
    argaddr(1, &readfds_addr);
    argaddr(2, &writefds_addr);
    argaddr(3, &exceptfds_addr);
    argaddr(4, &timeout_addr);
    // argaddr(5, &sigmask_addr);
    struct proc *p = proc_current();

    if (readfds_addr && (copyin(p->mm->pagetable, (char *)&readfds, readfds_addr, sizeof(readfds))) < 0) return -1;
    if (writefds_addr && (copyin(p->mm->pagetable, (char *)&writefds, writefds_addr, sizeof(writefds))) < 0) return -1;
    if (exceptfds_addr && (copyin(p->mm->pagetable, (char *)&exceptfds, exceptfds_addr, sizeof(exceptfds)) < 0)) return -1;

    if (timeout_addr) {
        if (copyin(p->mm->pagetable, (char *)&timeout, timeout_addr, sizeof(timeout)) < 0) return -1;
        // panic("timeout not tested\n");
    }
    uint64 time_out = (timeout_addr != 0 ? TIMESEPC2NS(timeout) : 0);
    // printfRed("time_out : %ld\n", time_out);

    // size_t sigsetsize = 0;
    // if (sigmask_addr) {
    //     if (copyin(p->mm->pagetable, (char *)&sigmask, sigmask_addr, sizeof(sigmask)) < 0) return -1;
    // }
    // int ret = do_pselect(nfds, &readfds, &writefds, &exceptfds, timeout_addr ? &timeout : NULL, &sigmask, sigsetsize);

    int ret = 0;

    struct file *fp;
    while (1) {
        if (readfds_addr) {
            for (int i = 0; i < nfds; i++) {
                fp = p->ofile[i];
                if (fp) {
                    switch (fp->f_type) {
                    case FD_INODE:
                        break;
                    case FD_PIPE: {
                        if (!pipereadable(fp->f_tp.f_pipe) || pipe_empty(fp->f_tp.f_pipe)) {
                            FD_CLR(i, &readfds);
                            break;
                        }
                    }
                    case FD_DEVICE:
                    case FD_SOCKET: {
                        ret++;
                        break;
                    }
                    default:
                        Warn("TYPE is %d", fp->f_type);
                        panic("this type not tested\n");
                    }
                }
            }
        }

        if (writefds_addr) {
            for (int i = 0; i < nfds; i++) {
                fp = p->ofile[i];
                if (fp) {
                    switch (fp->f_type) {
                    case FD_INODE:
                        break;
                    case FD_PIPE: {
                        if (!pipewriteable(fp->f_tp.f_pipe) || pipe_full(fp->f_tp.f_pipe)) {
                            FD_CLR(i, &writefds);
                            break;
                        }
                    }
                    case FD_DEVICE:
                    case FD_SOCKET: {
                        ret++;
                        break;
                    }
                    default:
                        Warn("TYPE is %d", fp->f_type);
                        panic("this type not tested\n");
                    }
                }
            }
        }

        if (exceptfds_addr) {
            for (int i = 0; i < nfds; i++) {
                FD_CLR(i, &exceptfds);
            }
        }

        // timeout
        // 	wait = NULL;
        uint64 time_now = TIME2NS(rdtime());
        if (ret || (TIME2NS(rdtime()) > time_now + time_out)) {
            // printf("ready\n");
            // printfGreen("select , pid : %d exit\n", proc_current()->pid);// debug
            break;
        } else {
            // acquire(&cond_ticks.waiting_queue.lock);
            // struct tcb* t = thread_current();
            // t->time_out = time_out;
            // cond_wait(&cond_ticks, &cond_ticks.waiting_queue.lock);
            // release(&cond_ticks.waiting_queue.lock);
            // break;
        }
    }

    if (readfds_addr && (copyout(p->mm->pagetable, readfds_addr, (char *)&readfds, sizeof(readfds)) < 0)) return -1;
    if (writefds_addr && (copyout(p->mm->pagetable, writefds_addr, (char *)&writefds, sizeof(writefds)) < 0)) return -1;
    if (exceptfds_addr && (copyout(p->mm->pagetable, exceptfds_addr, (char *)&exceptfds, sizeof(exceptfds)) < 0)) return -1;

    return ret;
}


#define TICK_GRANULARITY 10L // ms
#define SEC2TICK(sec) ((sec)*1000 / TICK_GRANULARITY)
#define MS2TICK(ms) ((ms) / TICK_GRANULARITY)
#define US2TICK(us) ((us)*TICK_GRANULARITY / 1000)
#define NS2TICK(ns) ((ns) / 1000 / 1000 / TICK_GRANULARITY)
static inline uint64 ts2ticks(struct timespec *ts) {
    return ts ? SEC2TICK(ts->ts_sec) + NS2TICK(ts->ts_nsec) : 0;
}

uint64 sys_ppoll(void) {
    uint64 pfdaddr;
    int nfds;
    uint64 tsaddr;
    uint64 sigmaskaddr;
    struct proc *p = proc_current();

    argaddr(0, &pfdaddr);
    argint(1, &nfds);
    argaddr(2, &tsaddr);
    argaddr(3, &sigmaskaddr);

    ASSERT(nfds == 1);
    // printf("ts addr is %#lx\n", tsaddr);

    struct pollfd pfd;

    if (copyin(p->mm->pagetable, (char *)&pfd, pfdaddr, sizeof(pfd)) < 0)
        return -1;

    ASSERT(pfd.events == POLLIN);
    ASSERT(pfd.fd == 0);

    struct file *f;
    f = p->ofile[pfd.fd];

    // TODO
    // argfd(, pfd.fd, , f);

    struct timespec ts;
    if (tsaddr && copyin(p->mm->pagetable, (char *)&ts, tsaddr, sizeof(struct timespec)) < 0)
        return -1;

    // uint64 timeout = TIMESEPC2NS(ts);
    uint64 timeout = tsaddr ? ts2ticks(&ts) : -1;

    while (1) {
        switch (f->f_type) {
        case FD_DEVICE:
            if (consoleready()) goto ret;
            break;
        case FD_PIPE:
            return 1;
        default:
            panic("error");
        }

        if (timeout == -1) continue;

        // uint64 time_now = TIME2NS(rdtime());
        // if ((TIME2NS(rdtime()) > time_now + timeout)) {
        //     // printf("ready\n");
        //     // printfGreen("select , pid : %d exit\n", proc_current()->pid);// debug
        //     break;
        // }

        if (timeout) {
            // Log("hit");
            extern struct cond cond_ticks;
            acquire(&cond_ticks.waiting_queue.lock);
            cond_wait(&cond_ticks, &cond_ticks.waiting_queue.lock);
            release(&cond_ticks.waiting_queue.lock);
            timeout--;
        } else {
            break;
        }
    }
ret:
    if (timeout == 0) return 0;

    pfd.revents = pfd.events;
    if (copyout(p->mm->pagetable, pfdaddr, (char *)&pfd, sizeof(pfd)) < 0)
        return -1;

    return 1;
}