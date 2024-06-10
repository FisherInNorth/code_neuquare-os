#ifndef __VM_H__
#define __VM_H__

#include "common.h"
#define COMMONPAGE 0
#define SUPERPAGE 1 /* 2MB superpage */

struct mm_struct;
void kvmmap(pagetable_t kpgtbl, uint64 va, uint64 pa, uint64 sz, int perm, int lowlevel);
int walk(pagetable_t pagetable, uint64 va, int alloc, int lowlevel, pte_t **pte);
paddr_t getphyaddr(pagetable_t pagetable, vaddr_t va);
uint64 walkaddr(pagetable_t pagetable, uint64 va);
int mappages(pagetable_t pagetable, uint64 va, uint64 size, uint64 pa, int perm, int lowlevel);
pagetable_t uvmcreate(void);
vaddr_t uvmalloc(pagetable_t pagetable, vaddr_t startva, vaddr_t endva, int perm);
uint64 uvmdealloc(pagetable_t pagetable, uint64 oldsz, uint64 newsz);
int uvmcopy(struct mm_struct *srcmm, struct mm_struct *dstmm);
void uvmfree(struct mm_struct *mm);
void uvmunmap(pagetable_t pagetable, uint64 va, uint64 npages, int do_free, int on_demand);
void uvmclear(pagetable_t pagetable, uint64 va);
void freewalk(pagetable_t pagetable, int level);

int uvm_thread_stack(pagetable_t pagetable, int thread_idx);
struct trapframe *uvm_thread_trapframe(pagetable_t pagetable, int thread_idx);

/* print the pagetable */
void vmprint(pagetable_t pagetable, int isroot, int level, uint64 start, uint64 end, uint64 vabase);

#endif // __VM_H__