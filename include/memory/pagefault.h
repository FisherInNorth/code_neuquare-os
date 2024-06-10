#ifndef __PAGEFAULT_H__
#define __PAGEFAULT_H__

#include "common.h"
#define INSTUCTION_PAGEFAULT 12
#define LOAD_PAGEFAULT 13
#define STORE_PAGEFAULT 15

#define PAGEFAULT(format, ...) printf("[PAGEFAULT]: " format "\n", ##__VA_ARGS__);
#define CHECK_PERM(cause, vma) (((cause) == STORE_PAGEFAULT && (vma->perm & PERM_WRITE))  \
                                || ((cause) == LOAD_PAGEFAULT && (vma->perm & PERM_READ)) \
                                || ((cause) == INSTUCTION_PAGEFAULT && (vma->perm & PERM_EXEC)))

/* copy-on write */
int cow(pte_t *pte, int level, paddr_t pa, int flags);
int is_a_cow_page(int flags);

int pagefault(uint64 cause, pagetable_t pagetable, vaddr_t stval);

#endif // __PAGEFAULT_H__