#include "proc/pcb_life.h"
#include "proc/tcb_life.h"
#include "proc/sched.h"
#include "lib/queue.h"
#include "atomic/cond.h"
#include "kernel/cpu.h"
#include "debug.h"

extern struct proc proc[NPROC];
extern struct tcb thread[NTCB];

extern Queue_t unused_p_q, used_p_q, zombie_p_q;
extern Queue_t unused_t_q, runnable_t_q, sleeping_t_q;

// init
void cond_init(struct cond *cond, char *name) {
    Queue_init(&cond->waiting_queue, name, TCB_WAIT_QUEUE);
}

// wait
int cond_wait(struct cond *cond, struct spinlock *mutex) {
    struct tcb *t = thread_current();

    acquire(&t->lock);

    TCB_Q_changeState(t, TCB_SLEEPING);

    Queue_push_back(&cond->waiting_queue, (void *)t);

    t->wait_chan_entry = &cond->waiting_queue; // !!!

    // TODO : modify it to futex(ref to linux)
    release(mutex);

    int ret = thread_sched();
    // ==========special for signal==============
    int killed = t->killed;
    release(&t->lock);
    if (killed) {
        do_exit(-1);
    }
    // ==========special for signal ==============
    // Reacquire original lock.
    acquire(mutex);

    return ret;
}

// just signal a object!!!
void cond_signal(struct cond *cond) {
    struct tcb *t;

    if (!Queue_isempty_atomic(&cond->waiting_queue)) {
        t = (struct tcb *)Queue_provide_atomic(&cond->waiting_queue, 1); // remove it
        acquire(&t->lock);
        if (t == NULL)
            panic("cond signal : this cond has no object waiting queue");
        if (t->state != TCB_SLEEPING) {
            printf("%s\n", t->state);
            panic("cond signal : this thread is not sleeping");
        }
        ASSERT(t->wait_chan_entry != NULL);
        t->wait_chan_entry = NULL;
        TCB_Q_changeState(t, TCB_RUNNABLE);
        release(&t->lock);
    }
}

// signal all object!!!
void cond_broadcast(struct cond *cond) {
    struct tcb *t;
    while (!Queue_isempty_atomic(&cond->waiting_queue)) {
        t = (struct tcb *)Queue_provide_atomic(&cond->waiting_queue, 1); // remove it

        acquire(&t->lock);
        if (t == NULL)
            panic("cond signal : this cond has no object waiting queue");
        if (t->state != TCB_SLEEPING) {
            printf("%s\n", cond->waiting_queue.name);
            printf("%s\n", t->state);
            panic("cond broadcast : this thread is not sleeping");
        }
        ASSERT(t->wait_chan_entry != NULL);
        t->wait_chan_entry = NULL;
        TCB_Q_changeState(t, TCB_RUNNABLE);
        release(&t->lock);
    }
}