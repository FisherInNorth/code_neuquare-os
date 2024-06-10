#include "atomic/futex.h"
#include "atomic/spinlock.h"
#include "proc/sched.h"
#include "lib/list.h"
#include "lib/queue.h"
#include "common.h"
#include "debug.h"
#include "lib/hash.h"
#include "common.h"

extern struct hash_table futex_map;

void futex_init(struct futex *fp, char *name) {
    initlock(&fp->lock, name);
    Queue_init(&fp->waiting_queue, name, TCB_WAIT_QUEUE);
}

// it is promised to be atomic
struct futex *get_futex(uint64 uaddr, int assert) {
    struct hash_node *node;

    node = hash_lookup(&futex_map, (void *)uaddr, NULL, 1, 0); // release it, not holding lock
    if (node) {
        // find it
        return (struct futex *)(node->value);
    } else {
        if (assert == 1) {
            return NULL;
        }
        // create it
        struct futex *fp = (struct futex *)kzalloc(sizeof(struct futex));
        if (fp == NULL) {
            printf("mm : %d\n", get_free_mem());
        panic("get_futex : no free space\n");
        }
        futex_init(fp, "futex");
        hash_insert(&futex_map, (void *)uaddr, (void *)fp, 0); // not holding lock
        return fp;
    }
}

// remember to release its memory
void futex_free(uint64 uaddr) {
    hash_delete(&futex_map, (void *)uaddr, 0, 1); // not holding lock, release lock
    // printfGreen("mm ++: %d\n", get_free_mem());
}

int futex_wait(uint64 uaddr, uint val, struct timespec *ts) {
    uint u_val;
    struct proc *p = proc_current();

    acquire(&p->lock);
    if (copyin(p->mm->pagetable, (char *)&u_val, uaddr, sizeof(uint)) < 0) {
        release(&p->lock);
        return -1;
    }

    if (u_val == val) {
        struct futex *fp = get_futex(uaddr, 0); // without assert
        struct tcb *t = thread_current();

        acquire(&t->lock);
        TCB_Q_changeState(t, TCB_SLEEPING);
        Queue_push_back(&fp->waiting_queue, t);

        release(&p->lock);
        if (ts != NULL) {
            t->time_out = TIMESEPC2NS((*ts));
        }
        t->wait_chan_entry = &fp->waiting_queue; // !!!!!

#ifdef __DEBUG_FUTEX__
        printfYELLOW("futex wait sleep, fp : %x, tid : %d, timeout : %d ns (%d s), uaddr %x\n", fp, t->tid, t->time_out, t->time_out / 1000 / 1000 / 1000, uaddr);
#endif
        int ret = thread_sched();
        release(&t->lock);

#ifdef __DEBUG_FUTEX__
        if (ts != NULL) {
            if (ret)
                printfMAGENTA("futex wait wakeup, tid : %d, after time out wakeup, uaddr %x\n", t->tid, uaddr);
            else
                printfBlue("futex wait wakeup, tid : %d, before time out wakeup, uaddr %x\n", t->tid, uaddr);
        }
#endif
        if (ts != NULL)
            return ret ? -1 : 0;
        else
            return 0;
    } else {
        release(&p->lock);
        return 0;
    }
}

int futex_wakeup(uint64 uaddr, int nr_wake) {
    struct proc *p = proc_current();
    acquire(&p->lock);

    // ========atomic=========
    struct futex *fp = get_futex(uaddr, 1); // with assert
    if (fp == NULL) {
        release(&p->lock);
        return 0;
    }

    struct tcb *t = NULL;
    int ret = 0;

    while (!Queue_isempty(&fp->waiting_queue) && ret < nr_wake) {
        t = (struct tcb *)Queue_provide_atomic(&fp->waiting_queue, 1); // remove it

        if (t == NULL)
            panic("futex wakeup : no waiting queue");
        if (t->state != TCB_SLEEPING) {
            printf("%s\n", fp->waiting_queue.name);
            printf("%s\n", t->state);
            panic("futex wakeup : this thread is not sleeping");
        }
        acquire(&t->lock);
        ASSERT(t->wait_chan_entry != NULL);
        t->wait_chan_entry = NULL;
        TCB_Q_changeState(t, TCB_RUNNABLE);
#ifdef __DEBUG_FUTEX__
        printfGreen("tid : %d futex wakeup tid : %d, uaddr : %x\n", thread_current()->tid, t->tid, uaddr);
#endif
        release(&t->lock);
        ret++;
    }

    if (Queue_isempty_atomic(&fp->waiting_queue)) {
#ifdef __DEBUG_FUTEX__
        printf("wakeup futex free, fp : %x, uaddr : %x\n", fp, uaddr);
#endif
        futex_free(uaddr); // !!! avoid memory leak
    }

    // ==========atomic==========
    release(&p->lock);
    return ret;
}

// 唤醒最多nr_wake个在uaddr1队列上等待的线程，其余的线程阻塞在uaddr2的队列
int futex_requeue(uint64 uaddr1, int nr_wake, uint64 uaddr2, int nr_requeue) {
    futex_wakeup(uaddr1, nr_wake);
    // int nr_wake1 = futex_wakeup(uaddr1, nr_wake);
    // #ifdef __DEBUG_FUTEX__
    // printfRed("futex_requeue : has wakeup %d threads in uaddr1 : %x\n", nr_wake1, uaddr1); // debug
    // #endif

    struct proc *p = proc_current();

    acquire(&p->lock);
    // ==========atomic=========
    struct futex *fp_old = get_futex(uaddr1, 1); // with assert
    if (fp_old == NULL) {
        release(&p->lock);
        return 0;
    }

    struct futex *fp_new = get_futex(uaddr2, 0); // without assert
    if (fp_new == NULL) {
        panic("not tested\n");
    }
    struct tcb *t = NULL;
    int ret = 0;

    while (!Queue_isempty_atomic(&fp_old->waiting_queue) && ret < nr_requeue) {
        t = (struct tcb *)Queue_provide_atomic(&fp_old->waiting_queue, 1); // remove it
        if (t == NULL)
            panic("futex requeue : no waiting queue") ;
        if (t->state != TCB_SLEEPING) {
            printf("%s\n", fp_old->waiting_queue.name);
            printf("%s\n", t->state);
            panic("futex requeue : this thread is not sleeping");
        }
        Queue_push_back_atomic(&fp_new->waiting_queue, (void *)t); // move the rest of threads to new queue
        ret++;
#ifdef __DEBUG_FUTEX__
        printfGreen("tid : %d futex requeue tid : %d from uaddr1 : %x to uaddr2 : %x\n", thread_current()->tid, t->tid, uaddr1, uaddr2);
#endif
    }
    if (Queue_isempty_atomic(&fp_old->waiting_queue)) {
#ifdef __DEBUG_FUTEX__
        printf("requeue futex free, fp : %x, uaddr : %x\n", fp_old, uaddr1);
#endif
        futex_free(uaddr1); // !!! avoid memory leak
    }

    // ========atomic========
    release(&p->lock);

    return ret;
}

// #ifdef __DEBUG_FUTEX__
// static char *futex_cmd[] = {
//     [FUTEX_WAIT] "futex_wait",
//     [FUTEX_WAKE] "futex_wake",
//     [FUTEX_REQUEUE] "futex_requeue"};
// #endif

int do_futex(uint64 uaddr, int op, uint32 val, struct timespec *ts,
             uint64 uaddr2, uint32 val2, uint32 val3) {
    int cmd = op & FUTEX_CMD_MASK;

    int ret = -1;
    switch (cmd) {
    case FUTEX_WAIT:
        ret = futex_wait(uaddr, val, ts);
        break;

    case FUTEX_WAKE:
        ret = futex_wakeup(uaddr, val);
        break;

    case FUTEX_REQUEUE:
        ret = futex_requeue(uaddr, val, uaddr2, val2); // must use val2 as a limit of requeue
        break;

    default:
        panic("do_futex : error\n");
        break;
    }

    // #ifdef __DEBUG_FUTEX__
    //         printfMAGENTA("futex, futex_cmd : %s, retval %d\n",futex_cmd[cmd], ret);
    // #endif
    return ret;
}