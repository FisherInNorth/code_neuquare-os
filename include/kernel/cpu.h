#ifndef __CPU_H__
#define __CPU_H__
#include "common.h"
#include "kernel/kthread.h"
#include "param.h"

struct tcb;

// Per-CPU state
struct thread_cpu {
    struct tcb *thread;     // The thread running on this cpu, or null.
    struct context context; // swtch() here to enter scheduler().
    int noff;               // Depth of push_off() nesting.
    int intena;             // Were interrupts enabled before push_off()?
};

extern struct thread_cpu t_cpus[NCPU];

// 1. get the id of cpu
int cpuid(void);
// 2. get the struct cpu
struct thread_cpu *t_mycpu(void);

#endif // __CPU_H__