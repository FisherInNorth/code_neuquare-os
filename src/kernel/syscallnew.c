#include "debug.h"
#include "common.h"
#include "proc/pcb_life.h"

// oscomp syscalls that haven't been implemented

// proc

// uid_t geteuid(void);
uint64 sys_geteuid(void) {
    return 0;
}

uint64 sys_rt_sigtimedwait(void) {
    return 0;
}