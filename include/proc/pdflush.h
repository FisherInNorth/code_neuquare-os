#ifndef __PDFLUSH_H__
#define __PDFLUSH_H__

#include "common.h"
#include "lib/list.h"
#include "atomic/spinlock.h"
#include "atomic/ops.h"
#include "atomic/cond.h"

#define MIN_PDFLUSH_THREADS 2
#define MAX_PDFLUSH_THREADS 4

struct pdflush {
    struct list_head entry;
    struct spinlock lock; // protect list
    atomic_t nr_pdflush_threads;
    struct cond pdflush_cond;
};

struct pdflush_work {
    struct tcb *who;
    void (*fn)(uint64);
    uint64 arg0;
    struct list_head list;
    uint64 when_i_went_to_sleep;
};

void pdflush_init();
int pdflush_operation(void (*fn)(uint64), uint64 arg0);
void start_one_pdflush_thread();

#endif