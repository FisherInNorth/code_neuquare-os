#include "kernel/cpu.h"
#include "proc/sched.h"
#include "proc/tcb_life.h"
#include "kernel/trap.h"
#include "lib/list.h"
#include "debug.h"
#include "lib/hash.h"
#include "lib/queue.h"
#include "lib/timer.h"
#include "proc/options.h"
#include "memory/vm.h"

extern Queue_t unused_t_q, runnable_t_q, sleeping_t_q, zombie_t_q;
extern Queue_t *STATES[TCB_STATEMAX];
extern struct hash_table tid_map;
extern struct proc *initproc;
extern struct cond cond_ticks;

struct tcb thread[NTCB];
char tcb_lock_name[NTCB][10];

atomic_t next_tid;
atomic_t count_tid;

// tcb init
void tcb_init(void) {
    struct tcb *t;
    atomic_set(&next_tid, 1);
    atomic_set(&count_tid, 0);

    TCB_Q_ALL_INIT();
    for (int i = 0; i < NTCB; i++) {
        t = thread + i;
        initlock(&t->lock, tcb_lock_name[i]); // init its spinlock

        t->state = TCB_UNUSED;
        t->kstack = KSTACK((int)(t - thread));
        Queue_push_back(&unused_t_q, t);
    }
    Info("thread table init [ok]\n");
    return;
}

// the kernel thread in current cpu
struct tcb *thread_current(void) {
    push_off();
    struct thread_cpu *c = t_mycpu();
    struct tcb *thread = c->thread;
    pop_off();
    return thread;
}

// allocate a new kernel thread
struct tcb *alloc_thread(thread_callback callback) {
    struct tcb *t;

    t = (struct tcb *)Queue_provide_atomic(&unused_t_q, 1); // remove it from the queue
    if (t == NULL)
        return 0;
    acquire(&t->lock);

    // spinlock and threads list head
    INIT_LIST_HEAD(&t->threads);

    t->tid = alloc_tid;
    cnt_tid_inc;

    // signal
    t->sig_pending_cnt = 0;
    sig_empty_set(&t->blocked);
    sigpending_init(&(t->pending));

    // Set up new context to start executing at forkret, which returns to user space.
    memset(&t->context, 0, sizeof(t->context));
    t->context.ra = (uint64)callback;
    t->context.sp = t->kstack + KSTACK_PAGE * PGSIZE;

    // chage state of TCB
    TCB_Q_changeState(t, TCB_USED);

    // map <tid, t>
    hash_insert(&tid_map, (void *)&t->tid, (void *)t, 0); // not holding it

    // timeout for timer
    t->time_out = 0;

    // for clone
    t->set_child_tid = 0;
    t->clear_child_tid = 0;
    return t;
}

// free a thread
void free_thread(struct tcb *t) {
    // free & unmap tramframe
    acquire(&t->p->mm->lock);
    if (t->trapframe)
        uvmunmap(t->p->mm->pagetable, THREAD_TRAPFRAME(t->tidx), 1, 1, 1);
    else
        uvmunmap(t->p->mm->pagetable, THREAD_TRAPFRAME(t->tidx), 1, 0, 1);
    release(&t->p->mm->lock);

    // bug!
    if (t->wait_chan_entry != NULL) {
        // Queue_remove_atomic(thread->wait_chan_entry, (void *)thread);
        ASSERT(thread->state == TCB_SLEEPING);
        thread->wait_chan_entry = NULL;
    }
    // bug!
    if (t->sig) {
        // !!! for shared
        int ref = atomic_dec_return(&t->sig->ref) - 1;
        if (ref == 0) {
            kfree((void *)t->sig);
        }
        t->sig = NULL;
    }

    // delete <tid, t>
    hash_delete(&tid_map, (void *)&t->tid, 0, 1); // not holding lock, release lock

    cnt_tid_dec;

    t->tid = 0;
    t->tidx = 0;
    t->trapframe = 0;
    t->name[0] = 0;
    // t->exit_status = 0;
    t->p = 0;
    t->sig_pending_cnt = 0;
    t->sig_ing = 0;
    memset(&t->context, 0, sizeof(t->context));

    signal_queue_flush(&t->pending); // !!!
    t->killed = 0;                   // !!! bug qwq

    TCB_Q_changeState(t, TCB_UNUSED);
}

// if map trapframe failed, return -1
int proc_join_thread(struct proc *p, struct tcb *t, char *name) {
    struct thread_group *tg = p->tg;

    atomic_inc_return(&tg->thread_cnt);
    acquire(&tg->lock);
    if (tg->group_leader == NULL) {
        tg->group_leader = t;
    }
    list_add_tail(&t->threads, &p->tg->threads);
    tg->tgid = p->pid;
    t->tidx = tg->thread_idx++;
    t->p = p;
    release(&p->tg->lock);

    acquire(&t->p->mm->lock);
    // Log("thread idx is %d, within group %d", t->tidx, p->pid);
    if ((t->trapframe = uvm_thread_trapframe(p->mm->pagetable, t->tidx)) == 0) {   
        release(&t->p->mm->lock);
        return -1;
    }
    release(&t->p->mm->lock);

    // vmprint(p->mm->pagetable, 0, 0, MAXVA - 512 * PGSIZE, 0);
    if (name == NULL) {
        char name_tmp[20];
        snprintf(name_tmp, 20, "%s-%d", p->name, t->tidx);
        strncpy(t->name, name_tmp, 20);
    } else {
        strncpy(t->name, name, 20);
    }


    return 0;
}

// send signal to all threads of proc p
void proc_sendsignal_all_thread(struct proc *p, sig_t signo, int opt) {
    struct tcb *t_cur = NULL;
    struct tcb *t_tmp = NULL;
    siginfo_t info;
    acquire(&p->tg->lock);
    list_for_each_entry_safe(t_cur, t_tmp, &p->tg->threads, threads) {
        signal_info_init(signo, &info, opt);

        acquire(&t_cur->lock);
        thread_send_signal(t_cur, &info);
        release(&t_cur->lock);
    }
    release(&p->tg->lock);
    if (signo == SIGKILL || signo == SIGSTOP) {
        proc_setkilled(p);
    }
}

// set killed state for thread
void thread_setkilled(struct tcb *t) {
    acquire(&t->lock);
    t->killed = 1;
    release(&t->lock);
}

// is thread killed ?
int thread_killed(struct tcb *t) {
    int k;

    acquire(&t->lock);
    k = t->killed;
    release(&t->lock);
    return k;
}

void tginit(struct thread_group *tg) {
    initlock(&tg->lock, "thread group lock");
    tg->group_leader = NULL;
    atomic_set(&tg->thread_cnt, 0);
    tg->thread_idx = 0;
    INIT_LIST_HEAD(&tg->threads);
}

void sighandinit(struct tcb *t) {
    if ((t->sig = (struct sighand *)kzalloc(sizeof(struct sighand))) == 0) {
        panic("no space for sighand\n");
    }
    initlock(&t->sig->siglock, "signal handler lock");
    atomic_set(&(t->sig->ref), 1);
    // memset the signal handler???
}

// send signal to thread (wakeup tcb sleeping)
void thread_send_signal(struct tcb *t_cur, siginfo_t *info) {
    signal_send(info, t_cur);

    if (t_cur->state == TCB_SLEEPING) {
        thread_wakeup(t_cur);
    }
    // wakeup thread sleeping of proc p
    // if (info->si_signo == SIGKILL || info->si_signo == SIGSTOP) {
    //     if (t_cur->state == TCB_SLEEPING) {
    //         Queue_remove_atomic(&cond_ticks.waiting_queue, (void *)t_cur); // bug
    //         TCB_Q_changeState(t_cur, TCB_RUNNABLE);
    //     }
    // }
#ifdef __DEBUG_PROC__
    printfCYAN("tkill : kill thread %d, signo = %d\n", t_cur->tid, info->si_signo); // debug
#endif
    switch(info->si_signo) {
        case SIGKILL:  
        case SIGSTOP:
            t_cur->killed = 1;
            break;
        default:
            break;
    }
    
    return;
}

// find the tcb* given tid using hash map
struct tcb *find_get_tid(tid_t tid) {
    struct hash_node *node = hash_lookup(&tid_map, (void *)&tid, NULL, 1, 0); // release it, not holding it
    return node != NULL ? (struct tcb *)(node->value) : NULL;
}

// find tcb given pid and tidx
struct tcb *find_get_tidx(int pid, int tidx) {
    // find proc given pid
    struct proc *p;
    if ((p = find_get_pid(pid)) == NULL)
        return NULL;

    // find thread given tidx
    struct tcb *t_cur = NULL;
    struct tcb *t_tmp = NULL;
    acquire(&p->tg->lock);
    list_for_each_entry_safe(t_cur, t_tmp, &p->tg->threads, threads) {
        acquire(&t_cur->lock);
        // tidx from 0, but tid == 0 stands for invalid thread
        if (t_cur->tidx + 1 == tidx) {
            release(&p->tg->lock);
            return t_cur;
        }
        release(&t_cur->lock);
    }
    release(&p->tg->lock);

    return NULL;
}

void do_tkill(struct tcb *t, sig_t signo) {
    siginfo_t info;
    signal_info_init(signo, &info, 0);
    acquire(&t->lock);
    thread_send_signal(t, &info);
    release(&t->lock);
#ifdef __DEBUG_SIGNAL__
    printfCYAN("tkill , tid : %d, signo : %d\n", t->tid, signo);
#endif
}

int do_sleep_ns(struct tcb *t, struct timespec ts) {
//     uint64 interval_ns = TIMESEPC2NS(ts);

//     acquire(&cond_ticks.waiting_queue.lock);
//     t->time_out = interval_ns;
// // printf("from do_sleep_ns: hart = %d\n",cpuid());
// // printf("t-time_out = %d\n",interval_ns);
//     int wait_ret = cond_wait(&cond_ticks, &cond_ticks.waiting_queue.lock);
//     release(&cond_ticks.waiting_queue.lock);
//     return wait_ret;
    return 0;
}

// create thread valid inkernel space
void create_thread(struct proc *p, struct tcb *t, char *name, thread_callback callback) {
    ASSERT(p != NULL);

    if ((t = alloc_thread(callback)) == 0) {
        panic("no free thread\n");
    }

    proc_join_thread(p, t, name);

    TCB_Q_changeState(t, TCB_RUNNABLE);
    release(&t->lock);
}