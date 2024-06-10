#ifndef __MEMLAYOUT_H__
#define __MEMLAYOUT_H__

/*
                    Sv39 Virtual Address
+--------+-------------+-------------+-------------+--------+
| (zero) | level-2 idx | level-1 idx | level-0 idx | offset |
| 63..39 |   38..30    |   29..21    |    20..12   |  11..0 |
+--------+-------------+-------------+-------------+--------+
*/

#if defined(SIFIVE_U) || defined(SIFIVE_B)
#include "platform/hifive/pml_hifive.h"
#else
#include "platform/qemu/pml.h"
#endif

/*
    The following codes are platform independent.
*/

// the kernel expects there to be RAM
// for use by the kernel and user pages
// from physical address 0x80000000 to PHYSTOP.
#define KERNBASE 0x80200000L
#define PHYSTOP (0x80000000L + 1024 * 1024 * 1024)
// 0x80a00000

// map the trampoline page to the highest address,
// in both user and kernel space.
#define TRAMPOLINE (MAXVA - PGSIZE)

#define KSTACK_PAGE 4
// map kernel stacks beneath the trampoline,
// each surrounded by invalid guard pages.
#define KSTACK(p) (TRAMPOLINE - ((p) + 1) * (KSTACK_PAGE + 1) * PGSIZE)

#ifdef __DEBUG_LDSO__
#define LDSO 0x00000000
#else
#define LDSO 0x40000000
#endif
// User memory layout.
//   text
//   original data and bss
//   expandable heap
//   ...
//   USTACK_GURAD_PAGE
//   USTACK
//   ...
//   TRAPFRAME (each thread has it's own trapframe)
//   SIGRETURN
//   TRAMPOLINE (the same page as in the kernel)

#define SIGRETURN (TRAMPOLINE - PGSIZE)

#define TRAPFRAME (SIGRETURN - PGSIZE)
#define THREAD_TRAPFRAME(id) (TRAPFRAME - (id)*PGSIZE)

#define USTACK_PAGE 10
#define USTACK (MAXVA - 512 * 10 * PGSIZE - USTACK_PAGE * PGSIZE)
#define USTACK_GURAD_PAGE (USTACK - PGSIZE)

#define TOTAL_MEM (PHYSTOP - START_MEM)
#define FREE_MEM (get_free_mem())
#define USED_MEM (FREE_MEM - TOTAL_MEM)
#define FREE_RATE(rate) (DIV_ROUND_UP(FREE_MEM, rate))

#define wmb() __asm__ __volatile__("fence w,o" \
                                   :           \
                                   :           \
                                   : "memory")
#define rmb() __asm__ __volatile__("fence i,r" \
                                   :           \
                                   :           \
                                   : "memory")

#endif // __MEMLAYOUT_H__
