#include "common.h"
#include "proc/pcb_mm.h"
#include "lib/riscv.h"
#include "proc/pcb_life.h"
#include "kernel/trap.h"
#include "memory/memlayout.h"
#include "memory/allocator.h"
#include "memory/vm.h"
#include "debug.h"
#include "proc/tcb_life.h"
#include "proc/pcb_mm.h"
#include "memory/vma.h"

extern struct tcb thread[NTCB];
extern char trampoline[];          // trampoline.S
extern char __user_rt_sigreturn[]; // sigret.S

// Allocate a page for each process's kernel stack.
// Map it high in memory, followed by an invalid
// guard page.

void tcb_mapstacks(pagetable_t kpgtbl) {
    struct tcb *t;

    for (t = thread; t < &thread[NTCB]; t++) {
        char *pa = kmalloc(KSTACK_PAGE * PGSIZE);
        if (pa == 0)
            panic("kalloc");
        uint64 va = KSTACK((int)(t - thread));
        kvmmap(kpgtbl, va, (uint64)pa, KSTACK_PAGE * PGSIZE, PTE_R | PTE_W, COMMONPAGE);
    }
    Info("========= Information of thread kernel stack ==========\n");
    Info("number of thread : %d , pages per thread : %d, total : %d\n", NTCB, KSTACK_PAGE, NTCB * KSTACK_PAGE);
    Info("thread kernel stack init [ok]\n");
}

/* Create a user page table, with no user memory,
   but with trampoline and sigreturn pages.
*/
pagetable_t proc_pagetable() {
    pagetable_t pagetable;

    // An empty page table.
    pagetable = uvmcreate();
    if (pagetable == 0)
        return 0;

    // map the trampoline code (for system call return)
    // at the highest user virtual address.
    // only the supervisor uses it, on the way
    // to/from user space, so not PTE_U.
    if (mappages(pagetable, TRAMPOLINE, PGSIZE, (uint64)trampoline, PTE_R | PTE_X, 0) < 0) {
        freewalk(pagetable, 0);
        return 0;
    }

    /* map with PTE_U */
    if (mappages(pagetable, SIGRETURN, PGSIZE, (uint64)__user_rt_sigreturn, PTE_R | PTE_X | PTE_U, 0) < 0) {
        uvmunmap(pagetable, TRAMPOLINE, 1, 0, 0);
        freewalk(pagetable, 0);
        return 0;
    }

    return pagetable;
}

// Free a process's page table, and free the
// physical memory it refers to.
void proc_freepagetable(struct mm_struct *mm, int thread_cnt) {
    // maxoffset starts from 0
    // vmprint(mm->pagetable, 1, 0, 0, 0);
    // printfYELLOW("================");
    uvmunmap(mm->pagetable, TRAMPOLINE, 1, 0, 0);
    uvmunmap(mm->pagetable, SIGRETURN, 1, 0, 0);
    uvmunmap(mm->pagetable, USTACK_GURAD_PAGE, 1, 0, 1);
    // vmprint(mm->pagetable, 1, 0, 0, 0);
    acquire(&mm->lock);
    for (int offset = 0; offset < thread_cnt; offset++) {
        /* on-demand unmap */
        uvmunmap(mm->pagetable, TRAPFRAME - offset * PGSIZE, 1, 0, 1);
    }
    release(&mm->lock);
    // printfYELLOW("================");
    // vmprint(mm->pagetable, 1, 0, 0, 0);
    uvmfree(mm);
}

// Grow or shrink user memory by n bytes.
// Return 0 on success, -1 on failure.
int growheap(int n) {
    uint64 oldsz, newsz, sz;
    struct proc *p = proc_current();
    struct mm_struct *mm = p->mm;
    // struct mm_struct *mm = proc_current()->mm;
    sz = mm->brk;
    oldsz = mm->brk;
    newsz = mm->brk + n;

    if (newsz < mm->start_brk) {
        Warn("bad arg");
        do_exit(-1);
    }

    if (n > 0) {
        pte_t *pte;
        int level = walk(mm->pagetable, oldsz, 0, 0, &pte);
        if (pte == NULL && level != -1) {
            panic("growheap: wrong");
        }
        ASSERT(level <= 1);
        if (level == COMMONPAGE) {
            if (PGROUNDUP(oldsz) >= newsz) {
                mm->brk = newsz;
                return 0;
            }
        } else if (level == SUPERPAGE) {
            if (SUPERPG_ROUNDUP(oldsz) >= newsz) {
                mm->brk = newsz;
                return 0;
            }
        } else if (level != -1) {
            panic("growheap: wrong level");
        }

        if ((sz = uvmalloc(mm->pagetable, oldsz, newsz, PTE_W | PTE_R)) == 0) {
            return -1;
        }
    } else if (n < 0) {
        sz = uvmdealloc(mm->pagetable, oldsz, newsz);
    }

    if (mm->heapvma == NULL) {
        if (vma_map(mm, mm->start_brk, sz - mm->start_brk, PERM_READ | PERM_WRITE, VMA_HEAP) < 0) {
            panic("TODO: ERROR HANDLER");
        }
    }
    mm->brk = sz;
    mm->heapvma->size = PGROUNDUP(sz) - mm->start_brk;
    // p->rlim[RLIMIT_STACK].rlim_cur = mm->heapvma->size;
    return 0;
}

// Copy to either a user address, or kernel address,
// depending on usr_dst.
// Returns 0 on success, -1 on error.
int either_copyout(int user_dst, uint64 dst, void *src, uint64 len) {
    struct proc *p = proc_current();
    if (user_dst) {
        return copyout(p->mm->pagetable, dst, src, len);
    } else {
        memmove((char *)dst, src, len);
        return 0;
    }
}

// Copy from either a user address, or kernel address,
// depending on usr_src.
// Returns 0 on success, -1 on error.
int either_copyin(void *dst, int user_src, uint64 src, uint64 len) {
    struct proc *p = proc_current();
    if (user_src) {
        return copyin(p->mm->pagetable, dst, src, len);
    } else {
        memmove(dst, (char *)src, len);
        return 0;
    }
}