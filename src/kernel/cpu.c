#include "kernel/cpu.h"
#include "lib/riscv.h"

struct thread_cpu t_cpus[NCPU];

// Must be called with interrupts disabled,
// to prevent race with process being moved
// to a different CPU.
int cpuid() {
    int id = r_tp();
    return id;
}

// Return this CPU's cpu struct.
// Interrupts must be disabled.
struct thread_cpu *
t_mycpu(void) {
    int id = cpuid();
    struct thread_cpu *c = &t_cpus[id];
    return c;
}

void hartinit() {
    w_sstatus(r_sstatus() | SSTATUS_SUM);
}