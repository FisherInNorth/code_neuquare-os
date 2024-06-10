#ifndef __TIMER_H__
#define __TIMER_H__

#include "lib/list.h"
#include "common.h"
#include "atomic/spinlock.h"
#include "lib/sbi.h"
#include "lib/riscv.h"
#include "memory/memlayout.h"

// #define SET_TIMER() sbi_legacy_set_timer(*(uint64 *)CLINT_MTIME + CLINT_INTERVAL)
#define SET_TIMER() sbi_legacy_set_timer(rdtime() + CLINT_INTERVAL)
#define TIME_OUT(timer_cur) (TIME2NS(rdtime()) > ((timer_cur)->expires_end))

typedef void (*timer_expire)(void *); // uint64

struct timer_entry {
    struct spinlock lock;
    struct list_head entry;
};

struct timer_list {
    struct list_head list;
    uint64 expires;
    uint64 expires_end;
    void (*function)(void *); // uint64
    void *data;               // uint64
    int count;                // for pdflush
    int cycle;                // for every clock interrupt
    int over;                 // for pselect
    uint64 interval;          // for setitimer
};

void timer_entry_init(struct timer_entry *t_entry, char *name);
void add_timer_atomic(struct timer_list *timer, uint64 expires, timer_expire function, void *data);
void delete_timer_atomic(struct timer_list *timer);
void timer_list_decrease_atomic(struct timer_entry *head);
void clockintr();

#endif