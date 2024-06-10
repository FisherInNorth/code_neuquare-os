#ifndef __TRAP_H__
#define __TRAP_H__

#include "common.h"

#define SYSCALL 8

struct context;
struct file;

// per-process data for the trap handling code in trampoline.S.
// sits in a page by itself just under the trampoline page in the
// user page table. not specially mapped in the kernel page table.
// uservec in trampoline.S saves user registers in the trapframe,
// then initializes registers from the trapframe's
// kernel_sp, kernel_hartid, kernel_satp, and jumps to kernel_trap.
// usertrapret() and userret in trampoline.S set up
// the trapframe's kernel_*, restore user registers from the
// trapframe, switch to the user page table, and enter user space.
// the trapframe includes callee-saved user registers like s0-s11 because the
// return-to-user path via usertrapret() doesn't return through
// the entire kernel call stack.
struct trapframe {
    /*   0 */ uint64 kernel_satp; // kernel page table
    /*   8 */ uint64 kernel_sp;   // top of process's kernel stack
    /*  16 */ uint64 kernel_trap; // usertrap()
    /*  24 */ uint64 epc;         // saved user program counter
    /*  32 */ uint64 hartid;      // hartid
    /*  40 */ uint64 ra;
    /*  48 */ uint64 sp;
    /*  56 */ uint64 gp;
    /*  64 */ uint64 tp;
    /*  72 */ uint64 t0;
    /*  80 */ uint64 t1;
    /*  88 */ uint64 t2;
    /*  96 */ uint64 s0;
    /* 104 */ uint64 s1;
    /* 112 */ uint64 a0;
    /* 120 */ uint64 a1;
    /* 128 */ uint64 a2;
    /* 136 */ uint64 a3;
    /* 144 */ uint64 a4;
    /* 152 */ uint64 a5;
    /* 160 */ uint64 a6;
    /* 168 */ uint64 a7;
    /* 176 */ uint64 s2;
    /* 184 */ uint64 s3;
    /* 192 */ uint64 s4;
    /* 200 */ uint64 s5;
    /* 208 */ uint64 s6;
    /* 216 */ uint64 s7;
    /* 224 */ uint64 s8;
    /* 232 */ uint64 s9;
    /* 240 */ uint64 s10;
    /* 248 */ uint64 s11;
    /* 256 */ uint64 t3;
    /* 264 */ uint64 t4;
    /* 272 */ uint64 t5;
    /* 280 */ uint64 t6;
    char resv[8];
    /* float registers below */
    /* 296 */ uint64 f0;
    /* 304 */ uint64 f1;
    /* 312 */ uint64 f2;
    /* 320 */ uint64 f3;
    /* 328 */ uint64 f4;
    /* 336 */ uint64 f5;
    /* 344 */ uint64 f6;
    /* 352 */ uint64 f7;
    /* 360 */ uint64 f8;
    /* 368 */ uint64 f9;
    /* 376 */ uint64 f10;
    /* 384 */ uint64 f11;
    /* 392 */ uint64 f12;
    /* 400 */ uint64 f13;
    /* 408 */ uint64 f14;
    /* 416 */ uint64 f15;
    /* 424 */ uint64 f16;
    /* 432 */ uint64 f17;
    /* 440 */ uint64 f18;
    /* 448 */ uint64 f19;
    /* 456 */ uint64 f20;
    /* 464 */ uint64 f21;
    /* 472 */ uint64 f22;
    /* 480 */ uint64 f23;
    /* 488 */ uint64 f24;
    /* 496 */ uint64 f25;
    /* 504 */ uint64 f26;
    /* 512 */ uint64 f27;
    /* 520 */ uint64 f28;
    /* 528 */ uint64 f29;
    /* 536 */ uint64 f30;
    /* 544 */ uint64 f31;
    /* 552 */ uint64 fcsr;
};

int copyout(pagetable_t, uint64, char *, uint64);
int copyin(pagetable_t, char *, uint64, uint64);
int copyinstr(pagetable_t, char *, uint64, uint64);
int either_copyout(int user_dst, uint64 dst, void *src, uint64 len);
int either_copyin(void *dst, int user_src, uint64 src, uint64 len);

// pagefault.c
int pagefault(uint64 cause, pagetable_t pagetable, vaddr_t stval);
void thread_usertrapret(void);

// debug
void trapframe_print(struct trapframe *tf);

#endif // __TRAP_H__