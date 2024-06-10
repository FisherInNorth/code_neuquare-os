#ifndef __SPINLOCK_H__
#define __SPINLOCK_H__

#include "common.h"

struct thread_cpu;

// Mutual exclusion lock.
struct spinlock {
    uint locked; // Is the lock held?

    // For debugging:
    char *name;             // Name of lock.
    struct thread_cpu *cpu; // The cpu holding the lock.
#ifdef __LOCKTRACE__
    int debug;
#endif
};

typedef struct spinlock spinlock_t;
#define INIT_SPINLOCK(NAME)                     \
    (spinlock_t) {                              \
        .locked = 0, .name = #NAME, .cpu = NULL \
    }

#define acquire(lock) wrap_acquire(__FILE__, __LINE__, (lock))
#define release(lock) wrap_release(__FILE__, __LINE__, (lock))
void wrap_acquire(char *file, int line, struct spinlock *lock);
void wrap_release(char *file, int line, struct spinlock *lock);

int holding(struct spinlock *);
void initlock(struct spinlock *, char *);
void push_off(void);
void pop_off(void);
int atomic_read4(int *addr);

#endif // __SPINLOCK_H__
