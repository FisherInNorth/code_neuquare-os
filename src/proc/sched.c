#include "kernel/cpu.h"
#include "kernel/trap.h"
#include "atomic/spinlock.h"
#include "proc/sched.h"
#include "proc/pcb_life.h"
#include "proc/tcb_life.h"
#include "lib/riscv.h"
#include "lib/queue.h"
#include "debug.h"
#include "common.h"
#include "lib/timer.h"

Queue_t unused_p_q, used_p_q, zombie_p_q;
Queue_t *STATES[PCB_STATEMAX] = {
    [PCB_UNUSED] & unused_p_q,
    [PCB_USED] & used_p_q,
    [PCB_ZOMBIE] & zombie_p_q};

Queue_t unused_t_q, used_t_q, runnable_t_q, sleeping_t_q, zombie_t_q;
Queue_t *T_STATES[TCB_STATEMAX] = {
    [TCB_UNUSED] & unused_t_q,
    [TCB_USED] & used_t_q,
    [TCB_RUNNABLE] & runnable_t_q,
    [TCB_SLEEPING] & sleeping_t_q};

extern struct proc proc[NPROC];
extern struct tcb thread[NTCB];

void PCB_Q_ALL_INIT() {
    Queue_init(&unused_p_q, "PCB_UNUSED", PCB_STATE_QUEUE);
    Queue_init(&used_p_q, "PCB_USED", PCB_STATE_QUEUE);
    Queue_init(&zombie_p_q, "PCB_ZOMBIE", PCB_STATE_QUEUE);
}

void TCB_Q_ALL_INIT() {
    Queue_init(&unused_t_q, "TCB_UNUSED", TCB_STATE_QUEUE);
    Queue_init(&used_t_q, "TCB_USED", TCB_STATE_QUEUE);
    Queue_init(&runnable_t_q, "TCB_RUNNABLE", TCB_STATE_QUEUE);
    Queue_init(&sleeping_t_q, "TCB_SLEEPING", TCB_STATE_QUEUE);
}

void PCB_Q_changeState(struct proc *p, enum procstate state_new) {
    Queue_t *pcb_q_new = STATES[state_new];
    Queue_t *pcb_q_old = STATES[p->state];
    Queue_remove_atomic(pcb_q_old, (void *)p);

    Queue_push_back_atomic(pcb_q_new, (void *)p);

    p->state = state_new;
    return;
}

void TCB_Q_changeState(struct tcb *t, enum thread_state state_new) {
    Queue_t *tcb_q_new = T_STATES[state_new];
    Queue_t *tcb_q_old = T_STATES[t->state];

    if (t->state != TCB_RUNNING) {
        Queue_remove_atomic(tcb_q_old, (void *)t);
    } else {
        Queue_remove((void *)t, TCB_STATE_QUEUE);
    }
    Queue_push_back_atomic(tcb_q_new, (void *)t);

    // if (t->tid == 4 && state_new == TCB_SLEEPING) {
    //     printfGreen("4 ready\n");
    // }
    // if (t->tid == 6 && state_new == TCB_SLEEPING) {
    //     printfGreen("6 ready\n");
    // }

    t->state = state_new;
    return;
}

void thread_yield(void) {
    struct tcb *t = thread_current();
    acquire(&t->lock);

    TCB_Q_changeState(t, TCB_RUNNABLE);

    thread_sched();
    release(&t->lock);
}

// holding lock
void thread_wakeup(struct tcb *t) {
    ASSERT(t->wait_chan_entry != NULL);
    Queue_remove_atomic(t->wait_chan_entry, (void *)t);
    ASSERT(t->state == TCB_SLEEPING);
    t->wait_chan_entry = NULL;
    TCB_Q_changeState(t, TCB_RUNNABLE);
}

// it is essential !!!
void thread_wakeup_atomic(void *t) {
    struct tcb *thread = (struct tcb *)t;
#ifdef __DEBUG_FUTEX__
    printfCYAN("timer : thread_wakeup_atomic tid : %d\n", thread->tid);
#endif

    acquire(&thread->lock);
    ASSERT(thread->wait_chan_entry != NULL);
    Queue_remove_atomic(thread->wait_chan_entry, (void *)thread);
    ASSERT(thread->state == TCB_SLEEPING);
    thread->wait_chan_entry = NULL;
    TCB_Q_changeState(t, TCB_RUNNABLE);
    release(&thread->lock);
}

// return the rest of expires of timer
int thread_sched(void) {
    int intena;
    struct tcb *thread = thread_current();
    // if (!holding(&thread->lock))
    //     panic("sched thread->lock");
    if (t_mycpu()->noff != 1) {
        panic("sched locks");
    }
    if (thread->state == TCB_RUNNING)
        panic("sched running");
    if (intr_get())
        panic("sched interruptible");

    intena = t_mycpu()->intena;

    // set timer for thread
    int set_timer = thread->time_out; // !!!
    struct timer_list timer;
    timer.expires = 0;
    timer.count = 1;                 // only once
    if (set_timer != 0) {
        INIT_LIST_HEAD(&timer.list); // bug!!!
        add_timer_atomic(&timer, thread->time_out, thread_wakeup_atomic, (void *)thread);
    }

    swtch(&thread->context, &t_mycpu()->context);
    t_mycpu()->intena = intena;

    if (set_timer != 0) {
        thread->time_out = 0; // bug !!!
        delete_timer_atomic(&timer);
    }

    return timer.expires == 0; // if it is 0, is is reasonable
}

void thread_scheduler(void) {
    struct tcb *t;
    struct thread_cpu *c = t_mycpu();

    c->thread = 0;
    for (;;) {
        // Avoid deadlock by ensuring that devices can interrupt.
        intr_on();
        t = (struct tcb *)Queue_provide_atomic(&runnable_t_q, 1); // remove it
        if (t == NULL)
            continue;

        acquire(&t->lock);
        t->state = TCB_RUNNING;
        c->thread = t;
        swtch(&c->context, &t->context);
        c->thread = 0;
        release(&t->lock);
    }
}