#ifndef __PCB_MM_H__
#define __PCB_MM_H__

#include "common.h"

struct tcb;
struct mm_struct;
void tcb_mapstacks(pagetable_t);
pagetable_t proc_pagetable();
int thread_trapframe(struct tcb *t, int still);
void proc_freepagetable(struct mm_struct *mm, int maxoffset);
int growheap(int);

#endif