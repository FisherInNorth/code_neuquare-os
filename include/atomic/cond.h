#ifndef __COND_H__
#define __COND_H__

#include "lib/queue.h"

// condition variable
struct cond {
    struct Queue waiting_queue;
};

void cond_init(struct cond *cond, char *name);

int cond_wait(struct cond *cond, struct spinlock *mutex);

void cond_signal(struct cond *cond);

void cond_broadcast(struct cond *cond);

#endif