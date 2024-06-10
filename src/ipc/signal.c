#include "proc/pcb_life.h"
#include "proc/sched.h"
#include "ipc/signal.h"
#include "memory/allocator.h"
#include "atomic/ops.h"
#include "kernel/trap.h"
#include "errno.h"
#include "debug.h"
#include "lib/list.h"

// delete signals related to the mask in the pending queue
int signal_queue_pop(uint64 mask, struct sigpending *pending) {
    ASSERT(pending != NULL);
    struct sigqueue *sig_cur;
    struct sigqueue *sig_tmp;

    if (!sig_test_mask(pending->signal, mask)) {
        printfRed("this signal is invalid\n");
        return 0;
    }

    sig_del_set_mask(pending->signal, mask);
    list_for_each_entry_safe(sig_cur, sig_tmp, &pending->list, list) {
        if (valid_signal(sig_cur->info.si_signo) && (mask & sig_gen_mask(sig_cur->info.si_signo))) {
            list_del_reinit(&sig_cur->list);
            kfree(sig_cur);
        }
    }
    return 1;
}

// delete all pending signals of queue
int signal_queue_flush(struct sigpending *pending) {
    ASSERT(pending != NULL);
    struct sigqueue *sig_cur;
    struct sigqueue *sig_tmp;
    sig_empty_set(&pending->signal);
    list_for_each_entry_safe(sig_cur, sig_tmp, &pending->list, list) {
        list_del_reinit(&sig_cur->list);
        kfree(sig_cur);
    }
    return 1;
}

// init the signal info
void signal_info_init(sig_t sig, siginfo_t *info, int opt) {
    // USER
    if (opt == 0) {
        info->si_signo = sig;
        // struct proc* p = proc_current();
        // if(p == NULL) {
        //     struct tcb * t = thread_current();
        //     printf("tid : %d ready\n",t->tid);
        // }
        // info->si_pid = p->pid;// bug!!!可能是一个空cpu，即没有进程的cpu发生时钟中断，然后发送信号
        info->si_code = SI_USER;
        // KERNEL
    } else if (opt == 1) {
        info->si_signo = sig;
        // info->si_pid = 0;
        info->si_code = SI_KERNEL;
    } else {
        panic("signal info : error\n");
    }
}

// send signal
int signal_send(siginfo_t *info, struct tcb *t) {
    ASSERT(t != NULL);
    ASSERT(info != NULL);

    // signo
    sig_t sig = info->si_signo;
    // if (sig_ignored(t, sig) || sig_existed(t, sig)) {
    //     return 0;
    // }
    if (sig_existed(t, sig)) {
        return 0;
    }

    // be killed immediately !!!
    if (sig == SIGKILL || sig == SIGSTOP || sig == SIGTERM) {
        t->killed = 1;
    }

    struct sigqueue *q;
    if ((q = (struct sigqueue *)kalloc()) == NULL) {
        printf("signal_send : no space in heap\n");
        return 0;
    }
    q->info = *info;          // !!!
    t->sig_pending_cnt++;
    INIT_LIST_HEAD(&q->list); // bug!!!

    list_add_tail(&q->list, &t->pending.list);
    sig_add_set(t->pending.signal, sig);

    return 1;
}

void sigpending_init(struct sigpending *sig) {
    sig_empty_set(&sig->signal);
    INIT_LIST_HEAD(&sig->list);
}

// signal handlle
int signal_handle(struct tcb *t) {
    if (t->sig_pending_cnt == 0)
        return 0;
    if (t->sig_ing != 0) {
#ifdef __DEBUG_SIGNAL__
        // printfRed("tid : %d is handing the signal %d\n", t->tid, t->sig_ing); // debug
#endif
    }

    struct sigqueue *sig_cur = NULL;
    struct sigqueue *sig_tmp = NULL;
    struct sigaction sig_act;

    list_for_each_entry_safe(sig_cur, sig_tmp, &t->pending.list, list) {
        int sig_no = sig_cur->info.si_signo;
        if (!valid_signal(sig_no)) { // bug!!!
            panic("signal handle : invalid signo\n");
        }
        if (sig_ignored(t, sig_no)) {
            continue;
        }
        sig_act = sig_action(t, sig_no);
        if (sig_act.sa_handler == SIG_DFL) {
            signal_DFL(t, sig_no);
            t->sig_pending_cnt--; // !!!
            // delete the signal immediately !!!
            // sig_del_set_mask(t->pending.signal, sig_gen_mask(sig_no));
            list_del_reinit(&sig_cur->list);
            kfree(sig_cur);
        } else if (sig_act.sa_handler == SIG_IGN) {
            continue;
        } else {
            do_handle(t, sig_no, &sig_act);
            t->sig_pending_cnt--; // !!!
            t->sig_ing = sig_no;
            // delete the signal immediately !!!
            // sig_del_set_mask(t->pending.signal, sig_gen_mask(sig_no));
            list_del_reinit(&sig_cur->list);
            kfree(sig_cur);
            break;
        }
    }
    return 1;
}

int do_handle(struct tcb *t, int sig_no, struct sigaction *sig_act) {
    // signal_trapframe_setup(t);
    sigset_t *oldset = &(t->blocked);

    int ret = setup_rt_frame(sig_act, sig_no, oldset, t->trapframe);
    return ret;
}

void signal_DFL(struct tcb *t, sig_t signo) {
    int cpid;
    uint64 wstatus = 0;
    switch (signo) {
    case SIGKILL:
        // case SIGSTOP:
        thread_setkilled(t);
        break;
    case SIGCHLD:
        cpid = waitpid(-1, wstatus, 0);
        printfRed("child , pid = %d exit with status : %d\n", cpid, wstatus);
        break;
    default:
        break;
    }
}

/*
 * POSIX 3.3.1.3:
 *  "Setting a signal action to SIG_IGN for a signal that is
 *   pending shall cause the pending signal to be discarded,
 *   whether or not it is blocked."
 *
 *  "Setting a signal action to SIG_DFL for a signal that is
 *   pending and whose default action is to ignore the signal
 *   (for example, SIGCHLD), shall cause the pending signal to
 *   be discarded, whether or not it is blocked"
 */
int do_sigaction(int sig, struct sigaction *act, struct sigaction *oact) {
    // struct proc *p = proc_current();
    struct tcb *t = thread_current();
    struct sigaction *k;

    if (!valid_signal(sig)) {
        return -1;
    }
    k = &t->sig->action[sig - 1];
    acquire(&t->sig->siglock);
    if (oact)
        *oact = *k;

    if (act) {
        sig_del_set_mask(act->sa_mask, sig_gen_mask(SIGKILL) | sig_gen_mask(SIGSTOP));
        *k = *act;
        // if (sig_handler_ignored(sig_handler(t, sig), sig)) {
        // 	sigemptyset(&mask);
        // 	sigaddset(&mask, sig);
        // 	rm_from_queue_full(&mask, &t->signal->shared_pending);
        // 	do {
        // 		rm_from_queue_full(&mask, &t->pending);
        // 		t = next_thread(t);
        // 	} while (t != current);
        // }
    // if(sig == SIGCANCEL)
    //     printfRed("sigaction , tid : %d, signo : %d, address : %x\n", t->tid, sig, k->sa_handler); // debug
#ifdef __DEBUG_SIGNAL__
        printfRed("sigaction , tid : %d, signo : %d, address : %x\n", t->tid, sig, k->sa_handler); // debug
#endif
    }
    release(&t->sig->siglock);
    return 0;
}

// debug
// #ifdef __DEBUG_SIGNAL__
// static char *signal_how[] = {
//     [SIG_BLOCK] "block",
//     [SIG_UNBLOCK] "unblock",
//     [SIG_SETMASK] "setmask"};
// #endif

int do_sigprocmask(int how, sigset_t *set, sigset_t *oldset) {
    struct tcb *t = thread_current();

    acquire(&t->sig->siglock);
    if (oldset)
        *oldset = t->blocked;

    int error = 0;
    switch (how) {
    case SIG_BLOCK:
        t->blocked.sig = sig_or(t->blocked.sig, set->sig);
        break;
    case SIG_UNBLOCK:
        t->blocked.sig = sig_and(t->blocked.sig, set->sig);
        break;
    case SIG_SETMASK:
        t->blocked.sig = set->sig;
        // bug like this : t->blocked = *set;
        break;
    default:
        error = -1;
    }

    // #ifdef __DEBUG_SIGNAL__
    // printfGreen("sigprocmask , tid : %d, how : %s\n", t->tid, signal_how[how]); // debug
    // if(how == SIG_SETMASK || how == SIG_UNBLOCK) {
    //     print_signal_mask(*set);
    //     print_signal_mask(t->blocked);
    // }
    // #endif
    release(&t->sig->siglock);
    return error;
}

void *get_sigframe(struct sigaction *sig, struct trapframe *tf, size_t framesize) {
    uint64 sp;
    /* Default to using normal stack */
    sp = tf->sp;
    /*
     * If we are on the alternate signal stack and would overflow it, don't.
     * Return an always-bogus address instead so we will die with SIGSEGV.
     */
    // if (on_sig_stack(sp) && !likely(on_sig_stack(sp - framesize)))
    // 	return (void __user __force *)(-1UL);

    /* This is the X/Open sanctioned signal stack switching. */
    // sp = sigsp(sp, ksig) - framesize;
    sp -= framesize;

    /* Align the stack frame. */
    sp &= ~0xfUL;

    return (void *)sp;
}

int setup_rt_frame(struct sigaction *sig, sig_t signo, sigset_t *set, struct trapframe *tf) {
    // struct proc *p = proc_current();
    struct rt_sigframe *frame;
    frame = get_sigframe(sig, tf, sizeof(*frame));
    signal_frame_setup(set, tf, frame, signo);

    tf->ra = (uint64)SIGRETURN;
    tf->epc = (uint64)sig->sa_handler;
    tf->sp = (uint64)frame;
    tf->a0 = (uint64)signo; /* a0: signal number */
    tf->a1 = 0;             // tf->a1  = (uint64)(&frame->info); /* a1: siginfo pointer */
    tf->a2 = tf->sp;        // tf->a2 = (uint64)(&frame->uc); /* a2: ucontext pointer */
    return 0;
}

int signal_frame_setup(sigset_t *set, struct trapframe *tf, struct rt_sigframe *rtf, sig_t signo) {
    // frame->uc.uc_flags = 0;
    // frame->uc.uc_link = NULL;
    // frame->uc.uc_stack.ss_sp = (void *)tf->sp;
    // frame->uc.uc_mcontext.sc_regs  ;
    // frame->uc.uc_sigmask = *set;
    struct ucontext uc;
    uc.uc_sigmask = *set;
    uc.uc_mcontext.tf = *tf;
    uc.sig_ing = signo;
    struct proc *p = proc_current();
    if (copyout(p->mm->pagetable, (uint64)&rtf->uc, (char *)&uc, sizeof(struct ucontext)))
        return -1;

    ucontext_t uc_riscv;
    memset((void *)&uc_riscv, 0, sizeof(uc_riscv));
    if (copyout(p->mm->pagetable, (uint64)&rtf->uc_riscv, (char *)&uc_riscv, sizeof(ucontext_t))) {
        return -1;
    }
    return 0;
}


int signal_frame_restore(struct tcb *t, struct rt_sigframe *rtf) {
    struct ucontext uc;
    struct proc *p = proc_current();
    if (copyin(p->mm->pagetable, (char *)&uc, (uint64)&rtf->uc, sizeof(struct ucontext)) != 0)
        return -1;
    t->blocked = uc.uc_sigmask;
    *(t->trapframe) = uc.uc_mcontext.tf;
    t->sig_ing = uc.sig_ing;

    ucontext_t uc_riscv;
    if (copyin(p->mm->pagetable, (char *)&uc_riscv, (uint64)&rtf->uc_riscv, sizeof(ucontext_t)) != 0)
        return -1;
    uint64 MC_PC = uc_riscv.uc_mcontext.__gregs[0];// for libc-test (pthread_cancel)
    if(MC_PC) {
        // printf("epc : %x\n", uc_riscv.uc_mcontext.__gregs[0]);
        t->trapframe->epc = MC_PC; 
    }
#ifdef __DEBUG_SIGNAL__
    printfRed("sigreturn , tid : %d\n", t->tid);
#endif
    return 0;
}

// debug
void print_signal_mask(sigset_t sigmask) {
    uint64 mask = 1ULL << 63;
    int leadingZeros = 1;

    for (int i = 0; i < 64; ++i) {
        if (sigmask.sig & mask) {
            printfGreen("%d ", i + 1);
            leadingZeros = 0;
        }
        mask >>= 1;
    }
    printfGreen("\n");
    if (leadingZeros) {
        // printf("None");
    }
}