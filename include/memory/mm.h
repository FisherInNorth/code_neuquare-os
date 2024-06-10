#ifndef __MM_H__
#define __MM_H__

#include "common.h"
#include "lib/list.h"
#include "atomic/semaphore.h"

typedef unsigned long vm_flags_t;
#define VM_NORESERVE 0x00200000 /* should the VM suppress accounting */

struct vma;
struct mm_struct {
    struct list_head head_vma;
    pagetable_t pagetable; // User page table

    paddr_t start_brk, brk; /* program break */
    struct vma *heapvma;

    struct semaphore mmap_sem;
    struct spinlock lock;
};

struct mm_struct *alloc_mm();
void free_mm(struct mm_struct *mm, int thread_idx);

#endif // __MM_H__