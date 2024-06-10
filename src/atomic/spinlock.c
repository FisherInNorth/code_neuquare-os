// Mutual exclusion spin locks.

#include "common.h"
#include "param.h"
#include "memory/memlayout.h"
#include "atomic/spinlock.h"
#include "lib/riscv.h"
#include "proc/pcb_life.h"
#include "kernel/cpu.h"
#include "debug.h"

// Read a shared 32-bit value without holding a lock
int atomic_read4(int *addr) {
    uint32 val;
    __atomic_load(addr, &val, __ATOMIC_SEQ_CST);
    return val;
}

void _acquire(struct spinlock *lk);
void _release(struct spinlock *lk);
#define DEBUG_LOCK_NUM 1
#define DEBUG_LOCK_BLACKLIST 10
// cannot use to debug pr(printf's lock)!!!
char *debug_lockname[DEBUG_LOCK_NUM] = {
    "inode_table",
};

char *blacklist[DEBUG_LOCK_BLACKLIST] = {
    "buddy_phy_mem_pools_lock",
    "TCB_RUNNABLE",
    "buffer",
    "uart",
    "bcache",
    "uart_tx_r_sem",
    "proc_0",
    "proc_1",
    // "proc_2",
    "timer_entry",
    "cons",
};

int all = 1;

void initlock(struct spinlock *lk, char *name) {
    lk->name = name;
    lk->locked = 0;
    lk->cpu = 0;
#ifdef __LOCKTRACE__
    lk->debug = 0;
    if (name == NULL) {
        return;
    }
    for (int i = 0; i < DEBUG_LOCK_BLACKLIST; i++) {
        if (strncmp(blacklist[i], name, sizeof(blacklist[i])) == 0) {
            return;
        }
    }
    if (all == 1) {
        lk->debug = 1;
    }
    for (int i = 0; i < DEBUG_LOCK_NUM; i++) {
        if (strncmp(debug_lockname[i], name, sizeof(debug_lockname[i])) == 0) {
            lk->debug = 1;
            break;
        }
    }
#endif
}

void wrap_acquire(char *file, int line, struct spinlock *lock) {
#ifdef __LOCKTRACE__
    extern int debug_lock;
    if (debug_lock == 1 && (lock->debug == 1)) {
        DEBUG_ACQUIRE("%s:%d, acquire lock %s\t\n", file, line, lock->name);
    }
#endif
    _acquire(lock);
}

void wrap_release(char *file, int line, struct spinlock *lock) {
#ifdef __LOCKTRACE__
    extern int debug_lock;
    if (debug_lock == 1 && lock->debug == 1) {
        DEBUG_RELEASE("%s:%d, release lock %s\t\n", file, line, lock->name);
    }
#endif
    _release(lock);
}

// Acquire the lock.
// Loops (spins) until the lock is acquired.
// void _acquire(struct spinlock *lk) {
//     push_off(); // disable interrupts to avoid deadlock.
//     // Log("%s\n",lk->name);// debug
//     if (holding(lk)) {
//         printf("%s\n", lk->name);
//         panic("acquire");
//     }

//     // On RISC-V, sync_lock_test_and_set turns into an atomic swap:
//     //   a5 = 1
//     //   s1 = &lk->locked
//     //   amoswap.w.aq a5, a5, (s1)
//     while (__sync_lock_test_and_set(&lk->locked, 1) != 0)
//         ;

//     // Tell the C compiler and the processor to not move loads or stores
//     // past this point, to ensure that the critical section's memory
//     // references happen strictly after the lock is acquired.
//     // On RISC-V, this emits a fence instruction.
//     __sync_synchronize();

//     // Record info about lock acquisition for holding() and debugging.
//     lk->cpu = t_mycpu();
// }

void _acquire(struct spinlock *lk) {
    push_off(); // disable interrupts to avoid deadlock.
    // Log("%s\n",lk->name);// debug
    if (holding(lk)) {
        printf("%s\n", lk->name);
        panic("acquire");
    }

    // On RISC-V, sync_lock_test_and_set turns into an atomic swap:
    //   a5 = 1
    //   s1 = &lk->locked
    //   amoswap.w.aq a5, a5, (s1)
    while (__sync_lock_test_and_set(&lk->locked, 1) != 0)
        ;

    // Tell the C compiler and the processor to not move loads or stores
    // past this point, to ensure that the critical section's memory
    // references happen strictly after the lock is acquired.
    // On RISC-V, this emits a fence instruction.
    __sync_synchronize();

    // Record info about lock acquisition for holding() and debugging.
    lk->cpu = t_mycpu();
}

// Release the lock.
// void _release(struct spinlock *lk) {
//     if (!holding(lk)) {
//         printf("%s ", lk->name);
//         panic("release\n");
//     }
//     lk->cpu = 0;

//     // Tell the C compiler and the CPU to not move loads or stores
//     // past this point, to ensure that all the stores in the critical
//     // section are visible to other CPUs before the lock is released,
//     // and that loads in the critical section occur strictly before
//     // the lock is released.
//     // On RISC-V, this emits a fence instruction.
//     __sync_synchronize();

//     // Release the lock, equivalent to lk->locked = 0.
//     // This code doesn't use a C assignment, since the C standard
//     // implies that an assignment might be implemented with
//     // multiple store instructions.
//     // On RISC-V, sync_lock_release turns into an atomic swap:
//     //   s1 = &lk->locked
//     //   amoswap.w zero, zero, (s1)
//     __sync_lock_release(&lk->locked);

//     pop_off();
// }

// Release the lock.
void _release(struct spinlock *lk) {
    if (!holding(lk)) {
        printf("%s ", lk->name);
        panic("release\n");
    }
    lk->cpu = 0;

    // Tell the C compiler and the CPU to not move loads or stores
    // past this point, to ensure that all the stores in the critical
    // section are visible to other CPUs before the lock is released,
    // and that loads in the critical section occur strictly before
    // the lock is released.
    // On RISC-V, this emits a fence instruction.
    __sync_synchronize();

    // Release the lock, equivalent to lk->locked = 0.
    // This code doesn't use a C assignment, since the C standard
    // implies that an assignment might be implemented with
    // multiple store instructions.
    // On RISC-V, sync_lock_release turns into an atomic swap:
    //   s1 = &lk->locked
    //   amoswap.w zero, zero, (s1)
    __sync_lock_release(&lk->locked);

    pop_off();
}

// Check whether this cpu is holding the lock.
// Interrupts must be off.
int holding(struct spinlock *lk) {
    int r;
    if (lk == NULL) {
        panic("???\n");
    }

    // printf("%d, %x\n", lk->locked, lk->cpu);

    r = (lk->locked && lk->cpu == t_mycpu());
    return r;
}

// push_off/pop_off are like intr_off()/intr_on() except that they are matched:
// it takes two pop_off()s to undo two push_off()s.  Also, if interrupts
// are initially off, then push_off, pop_off leaves them off.

void push_off(void) {
    int old = intr_get();

    intr_off();
    if (t_mycpu()->noff == 0)
        t_mycpu()->intena = old;
    t_mycpu()->noff += 1;
}

void pop_off(void) {
    struct thread_cpu *c = t_mycpu();
    if (intr_get())
        panic("pop_off - interruptible");
    if (c->noff < 1)
        panic("pop_off");
    c->noff -= 1;
    if (c->noff == 0 && c->intena)
        intr_on();
}
