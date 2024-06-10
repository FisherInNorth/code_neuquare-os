#ifndef __SCHED_H__
#define __SCHED_H__
#include "proc/pcb_life.h"
#include "proc/tcb_life.h"
#include "lib/queue.h"

struct proc;
struct tcb;

void PCB_Q_ALL_INIT(void);
void PCB_Q_changeState(struct proc *, enum procstate);

void TCB_Q_ALL_INIT(void);
void TCB_Q_changeState(struct tcb *t, enum thread_state state_new);

void thread_wakeup_atomic(void *t);
void thread_wakeup(struct tcb *t);
void thread_yield(void);

int thread_sched(void);
void thread_scheduler(void) __attribute__((noreturn));

// switch to context of scheduler
void swtch(struct context *, struct context *);

#endif