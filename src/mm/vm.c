#include "param.h"
#include "common.h"
#include "memory/memlayout.h"
#include "memory/vma.h"
#include "lib/riscv.h"
#include "memory/vm.h"
#include "debug.h"
#include "memory/allocator.h"
#include "proc/pcb_life.h"
#include "proc/pcb_mm.h"
#include "lib/list.h"
#include "memory/buddy.h"
#include "memory/pagefault.h"
#include "kernel/cpu.h"
#include "platform/hifive/uart_hifive.h"
#include "platform/hifive/dma_hifive.h"

/*
 * the kernel's page table.
 */
pagetable_t kernel_pagetable;
extern char etext[];      // kernel.ld sets this to end of kernel code.
extern char trampoline[]; // trampoline.S

// Make a direct-map page table for the kernel.
pagetable_t
kvmmake(void) {
    pagetable_t kpgtbl = (pagetable_t)kzalloc(PGSIZE);

#if defined(VIRT)
    // CLINT_MTIME
    // map in kernel pagetable, so we can access it in s-mode
    kvmmap(kpgtbl, CLINT_MTIME, CLINT_MTIME, PGSIZE, PTE_R, COMMONPAGE);
    // uart registers
    kvmmap(kpgtbl, UART0, UART0, PGSIZE, PTE_R | PTE_W, COMMONPAGE);
    // virtio mmio disk interface
    kvmmap(kpgtbl, VIRTIO0, VIRTIO0, PGSIZE, PTE_R | PTE_W, COMMONPAGE);
    // PLIC
    kvmmap(kpgtbl, PLIC, PLIC, 0x400000, PTE_R | PTE_W, SUPERPAGE);
#elif defined(SIFIVE_U) || defined(SIFIVE_B)
    // a temporary version <<==
    kvmmap(kpgtbl, CLINT_MTIME, CLINT_MTIME, PGSIZE, PTE_R, COMMONPAGE);
    // // uart registers
    kvmmap(kpgtbl, UART0_BASE, UART0_BASE, PGSIZE, PTE_R | PTE_W, COMMONPAGE);
    // dma
    kvmmap(kpgtbl, DMA_BASE, DMA_BASE, 0x100000, PTE_R | PTE_W, COMMONPAGE);
    // plic
    kvmmap(kpgtbl, PLIC_BASE, PLIC_BASE, 0x400000, PTE_R | PTE_W, SUPERPAGE);
    // a rough handler
#define QSPI_2_BASE ((unsigned int)0x10050000)
    kvmmap(kpgtbl, QSPI_2_BASE, QSPI_2_BASE, PGSIZE, PTE_R | PTE_W, COMMONPAGE);
#endif

    // map kernel text executable and read-only.
    vaddr_t super_aligned_sz = SUPERPG_DOWN((uint64)etext - KERNBASE);
    if (super_aligned_sz != 0) {
        kvmmap(kpgtbl, KERNBASE, KERNBASE, super_aligned_sz, PTE_R | PTE_X, SUPERPAGE);
    }
    kvmmap(kpgtbl, KERNBASE + super_aligned_sz, KERNBASE + super_aligned_sz, (uint64)etext - KERNBASE - super_aligned_sz, PTE_R | PTE_X, COMMONPAGE);

    // map kernel data and the physical RAM we'll make use of.
    super_aligned_sz = SUPERPG_DOWN(PHYSTOP - (uint64)etext);
    kvmmap(kpgtbl, (uint64)etext, (uint64)etext, PHYSTOP - (uint64)etext - super_aligned_sz, PTE_R | PTE_W, COMMONPAGE);
    kvmmap(kpgtbl, SUPERPG_ROUNDUP((uint64)etext), SUPERPG_ROUNDUP((uint64)etext), super_aligned_sz, PTE_R | PTE_W, SUPERPAGE);

    // map the trampoline for trap entry/exit to
    // the highest virtual address in the kernel.
    kvmmap(kpgtbl, TRAMPOLINE, (uint64)trampoline, PGSIZE, PTE_R | PTE_X, COMMONPAGE);

    // allocate and map a kernel stack for each process.
    tcb_mapstacks(kpgtbl);

    // debug user program
    kvmmap(kpgtbl, 0, START_MEM, PGSIZE * 1000, PTE_R | PTE_W, 0);

    // vmprint(kpgtbl, 1, 0, 0, 0);
    return kpgtbl;
}

// Initialize the one kernel_pagetable
void kvminit(void) {
    kernel_pagetable = kvmmake();
    Info("kernel pagetable init [ok]\n");
}

// Switch h/w page table register to the kernel's page table,
// and enable paging.
void kvminithart() {
    // wait for any previous writes to the page table memory to finish.
    sfence_vma();

    w_satp(MAKE_SATP(kernel_pagetable));

    // flush stale entries from the TLB.
    sfence_vma();
    Info("cpu %d, paging is enable !!!\n", cpuid());
}

// Return the address of the PTE in page table pagetable
// that corresponds to virtual address va.  If alloc!=0,
// create any required page-table pages.
//
// The risc-v Sv39 scheme has three levels of page-table
// pages. A page-table page contains 512 64-bit PTEs.
// A 64-bit virtual address is split into five fields:
//   39..63 -- must be zero.
//   30..38 -- 9 bits of level-2 index.
//   21..29 -- 9 bits of level-1 index.
//   12..20 -- 9 bits of level-0 index.
//    0..11 -- 12 bits of byte offset within the page.
#define LEVELS 3
/* return the leaf pte's level, set (pte_t *)*pte to this pte's address at the same time */
int walk(pagetable_t pagetable, uint64 va, int alloc, int lowlevel, pte_t **pte) {
    vaddr_t max = MAXVA;
    if (va >= MAXVA) {
        Log("%p", max);
        panic("walk");
    }

    for (int level = LEVELS - 1; level > lowlevel; level--) {
        pte_t *pte_tmp = &pagetable[PN(level, va)];
        if (*pte_tmp & PTE_V) {
            // find a superpage leaf PTE
            if ((*pte_tmp & PTE_R) || (*pte_tmp & PTE_X)) {
                *pte = pte_tmp;
                /* assert: only support 2MB leaf-pte */
                ASSERT(level == 1);
                return level;
            }
            pagetable = (pagetable_t)PTE2PA(*pte_tmp);
        } else {
            if (!alloc || (pagetable = (pde_t *)kzalloc(PGSIZE)) == 0) {
                *pte = 0;
                return -1;
            }
            *pte_tmp = PA2PTE(pagetable) | PTE_V;
        }
    }
    *pte = (pte_t *)&pagetable[PN(lowlevel, va)];
    return 0;
}

/* since the kernel only has direct mapping, add this func to make things easy
 * getphyaddr will return the physical address of the va
 * return 0 if the va is not in the pagetable
 */
paddr_t getphyaddr(pagetable_t pagetable, vaddr_t va) {
    vaddr_t aligned_va;
    paddr_t aligned_pa;
    aligned_va = PGROUNDDOWN(va);
    aligned_pa = walkaddr(pagetable, va);
    if (aligned_pa == 0) {
        return 0;
    }
    return aligned_pa + (va - aligned_va);
}

// Look up a virtual address, return the physical address,
// or 0 if not mapped.
// Can only be used to look up user pages.
uint64 walkaddr(pagetable_t pagetable, uint64 va) {
    pte_t *pte;
    uint64 pa;

    if (va >= MAXVA)
        return 0;

    int level = walk(pagetable, va, 0, 0, &pte);
    ASSERT(level <= 1);
    if (pte == 0)
        return 0;
    if ((*pte & PTE_V) == 0)
        return 0;
    if ((*pte & PTE_U) == 0)
        return 0;
    pa = PTE2PA(*pte);
    if (level == COMMONPAGE) {
        return pa;
    } else if (level == SUPERPAGE) {
        return pa + (PGROUNDDOWN(va) - SUPERPG_DOWN(va));
    } else {
        panic("can not reach here");
    }
}

// add a mapping to the kernel page table.
// only used when booting.
// does not flush TLB or enable paging.
void kvmmap(pagetable_t kpgtbl, uint64 va, uint64 pa, uint64 sz, int perm, int lowlevel) {
    if (mappages(kpgtbl, va, sz, pa, perm, lowlevel) != 0)
        panic("kvmmap");
}

// Create PTEs for virtual addresses starting at va that refer to
// physical addresses starting at pa. va and size might not
// be page-aligned. Returns 0 on success, -1 if walk() couldn't
// allocate a needed page-table page.
int mappages(pagetable_t pagetable, uint64 va, uint64 size, uint64 pa, int perm, int lowlevel) {
    uint64 a, last;
    pte_t *pte;

    if (size == 0)
        panic("mappages: size");

    uint64 pgsize = 0;
    switch (lowlevel) {
    case 0: {
        pgsize = PGSIZE;
        a = PGROUNDDOWN(va);
        last = PGROUNDDOWN(va + size - 1);
        break;
    }
    case 1: {
        pgsize = SUPERPGSIZE;
        /* if this is a superpage mapping, the va needs to be superpage aligned */
        ASSERT(va % SUPERPGSIZE == 0);
        a = SUPERPG_DOWN(va);
        last = SUPERPG_DOWN(va + size - 1);
        break;
    }
    default: panic("mappages: not support"); break;
    }

    for (;;) {
        walk(pagetable, a, 1, lowlevel, &pte);
        if (pte == 0) {
            return -1;
        }
        if (*pte & PTE_V) {
            vmprint(pagetable, 1, 0, 0, 0, 0);
            // Log("remap va is %x", *pte);
            Log("remap pte is %x", *pte);
            Log("remap pa is %x", PTE2PA(*pte));
            panic("mappages: remap");
        }
        *pte = PA2PTE(pa) | perm | PTE_V;
        if (a == last)
            break;
        a += pgsize;
        pa += pgsize;
    }
    return 0;
}

// Remove npages of mappings starting from va. va must be
// page-aligned. The mappings must exist.
// Optionally free the physical memory.
/* on_demand option: use for on-demand mapping, only unmap the mapping pages
                     and skip the unmapping pages */
void uvmunmap(pagetable_t pagetable, uint64 va, uint64 npages, int do_free, int on_demand) {
    uint64 a;
    pte_t *pte;

    if ((va % PGSIZE) != 0)
        panic("uvmunmap: not aligned");

    uint64 endva = va + npages * PGSIZE;

    for (a = va; a < endva; a += PGSIZE) {
        int level = walk(pagetable, a, 0, 0, &pte);
        if (pte == 0) {
            if (on_demand == 1) {
                continue;
            }
            panic("uvmunmap: walk");
        }
        if ((*pte & PTE_V) == 0) {
            if (on_demand == 1) {
                continue;
            }
            vmprint(pagetable, 1, 0, 0, 0, 0);
            printf("va is %x\n", va);
            panic("uvmunmap: not mapped");
        }
        if (PTE_FLAGS(*pte) == PTE_V)
            panic("uvmunmap: not a leaf");

        ASSERT(level <= 1);

        if (do_free) {
            uint64 pa = PTE2PA(*pte);
            kfree((void *)pa);
        }
        uint64 pte_flags = PTE_FLAGS(*pte);
        *pte = 0;

        if (level == SUPERPAGE) {
            if (a != SUPERPG_DOWN(a) && a == va) {
                uvmalloc(pagetable, SUPERPG_DOWN(a), a, pte_flags);
                // vmprint(pagetable, 1, 0, 0, 0);
            }
            a += (SUPERPGSIZE - PGSIZE);
        }
    }
}

// create an empty user page table.
// returns 0 if out of memory.
pagetable_t uvmcreate() {
    pagetable_t pagetable;
    pagetable = (pagetable_t)kzalloc(PGSIZE);
    if (pagetable == 0)
        return 0;
    return pagetable;
}

// Allocate PTEs and physical memory to grow process from oldsz to
// newsz, which need not be page aligned.  Returns new size or 0 on error.
vaddr_t uvmalloc(pagetable_t pagetable, vaddr_t startva, vaddr_t endva, int perm) {
    char *mem;

    if (endva < startva)
        return startva;

    // 外层有对super/normal page 的判断!

    uint64 aligned_sz = PGROUNDUP(startva);
    uint64 super_aligned_sz = SUPERPG_ROUNDUP(startva);
    uint64 min = (endva <= super_aligned_sz ? endva : super_aligned_sz);

    for (uint64 a = aligned_sz; a < min; a += PGSIZE) {
        mem = kzalloc(PGSIZE);
        if (mem == 0) {
            uvmdealloc(pagetable, a, startva);
            return 0;
        }
        if (mappages(pagetable, a, PGSIZE, (uint64)mem, PTE_R | PTE_U | perm, COMMONPAGE) != 0) {
            kfree(mem);
            uvmdealloc(pagetable, a, startva);
            return 0;
        }
    }

    if (endva <= super_aligned_sz) {
        ASSERT(min == endva);
        return endva;
    } else {
        uint64 addr;
        uint64 newsz_down = SUPERPG_DOWN(endva);
        for (addr = super_aligned_sz; addr < newsz_down; addr += SUPERPGSIZE) {
            mem = kzalloc(SUPERPGSIZE);
            if (mem == 0) {
                // vmprint(pagetable, 1, 0, 0, 0);
                uvmdealloc(pagetable, addr, startva);
                return 0;
            }
            if (mappages(pagetable, addr, SUPERPGSIZE, (uint64)mem, PTE_R | PTE_U | perm, SUPERPAGE) != 0) {
                kfree(mem);
                uvmdealloc(pagetable, addr, startva);
                return 0;
            }
        }

        for (addr = newsz_down; addr < endva; addr += PGSIZE) {
            mem = kzalloc(PGSIZE);
            if (mem == 0) {
                uvmdealloc(pagetable, addr, startva);
                return 0;
            }
            if (mappages(pagetable, addr, PGSIZE, (uint64)mem, PTE_R | PTE_U | perm, COMMONPAGE) != 0) {
                kfree(mem);
                uvmdealloc(pagetable, addr, startva);
                return 0;
            }
        }
    }

    return endva;
}

// Deallocate user pages to bring the process size from oldsz to
// newsz.  oldsz and newsz need not be page-aligned, nor does newsz
// need to be less than oldsz.  oldsz can be larger than the actual
// process size.  Returns the new process size.
uint64 uvmdealloc(pagetable_t pagetable, uint64 oldsz, uint64 newsz) {
    if (newsz >= oldsz)
        return oldsz;

    if (PGROUNDUP(newsz) < PGROUNDUP(oldsz)) {
        int npages = (PGROUNDUP(oldsz) - PGROUNDUP(newsz)) / PGSIZE;
        uvmunmap(pagetable, PGROUNDUP(newsz), npages, 1, 0);
    }

    return newsz;
}

// Recursively free page-table pages.
// All leaf mappings must already have been removed.
void freewalk(pagetable_t pagetable, int level) {
    // there are 2^9 = 512 PTEs in a page table.
    for (int i = 0; i < 512; i++) {
        // if (level == 0 && i == 1) {
        //     /* used for libc.so */
        //     continue;
        // }
        pte_t pte = pagetable[i];
        if ((pte & PTE_V) && (pte & (PTE_R | PTE_W | PTE_X)) == 0) {
            // this PTE points to a lower-level page table.
            uint64 child = PTE2PA(pte);
            freewalk((pagetable_t)child, level + 1);
            pagetable[i] = 0;
        } else if (pte & PTE_V) {
#ifdef __DEBUG_LDSO__
            continue;
#else
            vmprint(pagetable, 1, 0, 0, 0, 0);
            panic("freewalk: leaf");
#endif
        }
    }
    if (pagetable) // bug!!!
        kfree((void *)pagetable);
}

// Free all pages in vmas,
// then free page-table pages.
void uvmfree(struct mm_struct *mm) {
    free_all_vmas(mm);
    // print_vma(&mm->head_vma);
    freewalk(mm->pagetable, 0);
}

// Given a parent process's page table, copy
// its memory into a child's page table.
// Copies both the page table and the
// physical memory.
// returns 0 on success, -1 on failure.
// frees any allocated pages on failure.
int uvmcopy(struct mm_struct *srcmm, struct mm_struct *dstmm) {
    struct vma *pos;
    list_for_each_entry(pos, &srcmm->head_vma, node) {
        vaddr_t startva, endva;
        paddr_t pa;
        pte_t *pte;
        uint flags;
        int level;

        /* for STACK VMA, copy both the pagetable and the physical memory */
        if (pos->type == VMA_STACK) {
            // ASSERT(pos->size == USTACK_PAGE * PGSIZE);
            // Log("stack size is %d", pos->size / PGSIZE);
            for (uint64 offset = 0; offset < pos->size; offset += PGSIZE) {
                level = walk(srcmm->pagetable, pos->startva + offset, 0, 0, &pte);
                // ASSERT(level <= 1 && level >= 0);
                if (!(level <= 1 && level >= 0)) {
                    panic("uvmcopy : level error\n");
                }
                pa = PTE2PA(*pte);

                paddr_t new = (paddr_t)kzalloc(PGSIZE);
                if (new == 0) {
                    Warn("uvmcopy: no free mem");
                    return -1;
                }
                memmove((void *)new, (void *)pa, PGSIZE);
                if (mappages(dstmm->pagetable, pos->startva + offset, PGSIZE, new, PTE_W | PTE_R | PTE_U, COMMONPAGE) != 0) {
                    panic("uvmcopy: map failed");
                    return -1;
                }
                // Log("stack %p", pos->startva + offset);
            }
            continue;
        }

        /* for other vmas, copy pagetable only */
        startva = pos->startva;
        endva = startva + pos->size;
        ASSERT(startva % PGSIZE == 0 && endva % PGSIZE == 0);
        for (vaddr_t i = startva; i < endva; i += PGSIZE) {
            level = walk(srcmm->pagetable, i, 0, 0, &pte);
            if (pte == NULL || *pte == 0) {
                continue;
            }
            if (!(level <= 1 && level >= 0)) {
                panic("uvmcopy : level error\n");
            }

            if (pos->type != VMA_FILE || !(pos->perm & PERM_SHARED)) {
                // if (pos->type != VMA_FILE) {
                if ((*pte & PTE_W) == 0 && (*pte & PTE_SHARE) == 0) {
                    *pte = *pte | PTE_READONLY;
                }
                /* shared page */
                if ((*pte & PTE_W) == 0 && (*pte & PTE_READONLY) != 0) {
                    *pte = *pte | PTE_READONLY;
                }
                *pte = *pte | PTE_SHARE;
                *pte = *pte & ~PTE_W;
            }

            pa = PTE2PA(*pte);
            flags = PTE_FLAGS(*pte);

            if (level == SUPERPAGE) {
                /* level == 1 ~ map superpage */
                ASSERT(i == SUPERPG_DOWN(i));
                if (mappages(dstmm->pagetable, i, SUPERPGSIZE, pa, flags, SUPERPAGE) != 0) {
                    goto err;
                }
                i += (SUPERPGSIZE - PGSIZE);
            } else if (level == COMMONPAGE) {
                /* level == 0 ~ map common page */
                if (mappages(dstmm->pagetable, i, PGSIZE, pa, flags, COMMONPAGE) != 0) {
                    goto err;
                }
            }
            /* call share_page after mappages success! */
            share_page(pa);
        }
    }
    return 0;

err:
    panic("TODO: error handler");
    // uvmunmap(new, 0, i / PGSIZE, 0, 0);
    return -1;
}

// mark a PTE invalid for user access.
// used by exec for the user stack guard page.
void uvmclear(pagetable_t pagetable, uint64 va) {
    pte_t *pte;

    walk(pagetable, va, 0, 0, &pte);
    if (pte == 0)
        panic("uvmclear");
    *pte &= ~PTE_U;
}

// Copy from kernel to user.
// Copy len bytes from src to virtual address dstva in a given page table.
// Return 0 on success, -1 on error.
int copyout(pagetable_t pagetable, uint64 dstva, char *src, uint64 len) {
    uint64 n, va0, pa0;

    while (len > 0) {
        va0 = PGROUNDDOWN(dstva);
        // kernel has the right to write all RAM without PAGE FAULT
        // so need to check if this write is legal(PTE_W == 1 in PTE)
        // if not, call pagefault
        pte_t *pte;
        int flags;
        /* since walk will panic if va0 > maxva, so we have to handle this error before walk */
        if (va0 >= MAXVA) {
            return -1;
        }
        walk(pagetable, va0, 0, 0, &pte);
        if (pte == NULL || (*pte == 0)) {
            if (pagefault(STORE_PAGEFAULT, pagetable, dstva) < 0) {
                return -1;
            }
        }
        flags = PTE_FLAGS(*pte);
        if ((flags & PTE_W) == 0 && is_a_cow_page(flags)) {
            if (pagefault(STORE_PAGEFAULT, pagetable, dstva) < 0) {
                return -1;
            }
        }

        pa0 = walkaddr(pagetable, va0);
        if (pa0 == 0)
            return -1;
        n = PGSIZE - (dstva - va0);
        if (n > len)
            n = len;
        memmove((void *)(pa0 + (dstva - va0)), src, n);

        len -= n;
        src += n;
        dstva = va0 + PGSIZE;
    }
    return 0;
}

// Copy from user to kernel.
// Copy len bytes to dst from virtual address srcva in a given page table.
// Return 0 on success, -1 on error.
int copyin(pagetable_t pagetable, char *dst, uint64 srcva, uint64 len) {
    uint64 n, va0, pa0;

    while (len > 0) {
        va0 = PGROUNDDOWN(srcva);
        // if (va0 == 0x32407000) {
        //     vmprint(pagetable, 1, 0, 0x32406000, 0);
        // }
        pa0 = walkaddr(pagetable, va0);
        if (pa0 == 0)
            return -1;
        n = PGSIZE - (srcva - va0);
        if (n > len)
            n = len;
        memmove(dst, (void *)(pa0 + (srcva - va0)), n);

        len -= n;
        dst += n;
        srcva = va0 + PGSIZE;
    }
    return 0;
}

// Copy a null-terminated string from user to kernel.
// Copy bytes to dst from virtual address srcva in a given page table,
// until a '\0', or max.
// Return 0 on success, -1 on error.
int copyinstr(pagetable_t pagetable, char *dst, uint64 srcva, uint64 max) {
    uint64 n, va0, pa0;
    int got_null = 0;

    while (got_null == 0 && max > 0) {
        va0 = PGROUNDDOWN(srcva);
        pa0 = walkaddr(pagetable, va0);
        if (pa0 == 0)
            return -1;
        n = PGSIZE - (srcva - va0);
        if (n > max)
            n = max;

        char *p = (char *)(pa0 + (srcva - va0));
        while (n > 0) {
            if (*p == '\0') {
                *dst = '\0';
                got_null = 1;
                break;
            } else {
                *dst = *p;
            }
            --n;
            --max;
            p++;
            dst++;
        }

        srcva = va0 + PGSIZE;
    }
    if (got_null) {
        return 0;
    } else {
        return -1;
    }
}

/* vpn ~ virtual page number */
void vmprint_indent(int level, int vpn) {
    switch (level) {
    case 0: printf(" ..%d: ", vpn); break;
    case 1: printf(" .. ..%d: ", vpn); break;
    case 2: printf(" .. .. ..%d: ", vpn); break;
    default: panic("should not reach here");
    }
}

void vmprint(pagetable_t pagetable, int isroot, int level, uint64 start, uint64 end, uint64 vabase) {
    if (end == 0) {
        end = MAXVA;
    }
    pte_t pte;
    if (isroot) {
        printf("page table %p\n", pagetable);
    }

    uint64 vagap;
    switch (level) {
    case 0: vagap = SUPERPGSIZE * 512; break;
    case 1: vagap = SUPERPGSIZE; break;
    case 2: vagap = PGSIZE; break;
    default: panic("wrong argument");
    }

    for (int i = 0; i < 512; i++) {
        pte = pagetable[i];
        if (pte & PTE_V) {
            if (vabase + i * vagap >= start && vabase + i * vagap < end) {
                vmprint_indent(level, i);
            }
            if ((pte & (PTE_W | PTE_X | PTE_R)) == 0) {
                // not a leaf-pte
                if (vabase + i * vagap >= start && vabase + i * vagap < end) {
                    printf("pte %p pa %p\n", pte, PTE2PA(pte));
                }
                vmprint((pagetable_t)PTE2PA(pte), 0, level + 1, start, end, vabase + i * vagap);
            } else {
                // a leaf-pte
                vaddr_t curva = vabase + i * vagap;
                if (curva >= start && curva < end) {
                    printf("leaf pte %p pa %p ", pte, PTE2PA(pte));
                    PTE("RSW %d%d A %d D %d U %d X %d W %d R %d  va is %p  ",
                        (pte & PTE_READONLY) > 0, (pte & PTE_SHARE) > 0,
                        (pte & PTE_D) > 0, (pte & PTE_A) > 0,
                        (pte & PTE_U) > 0, (pte & PTE_X) > 0,
                        (pte & PTE_W) > 0, (pte & PTE_R) > 0,
                        curva);
                    if (curva == SIGRETURN) {
                        PTE("SIGRETURN");
                    } else if (curva == TRAMPOLINE) {
                        PTE("TRAMPOLINE");
                    } else if (curva == TRAPFRAME) {
                        PTE("TRAPFRAME0");
                    }
                    printf("\n");
                }
            }
        }
    }
}

int uvm_thread_stack(pagetable_t pagetable, int ustack_page) {
    /* for guard page */
    paddr_t pa = (paddr_t)kzalloc(PGSIZE);
    if (pa == 0) {
        Warn("no free mem for user stack");
        return -1;
    }

    // /* guard page: page with PTE_R | PTE_W , but not PTE_U */
    // if (mappages(pagetable, USTACK_GURAD_PAGE, PGSIZE, pa, PTE_R | PTE_W, COMMONPAGE) < 0) {
    //     return -1;
    // }

    vaddr_t stackdown = USTACK;
    if (uvmalloc(pagetable, stackdown, stackdown + ustack_page * PGSIZE, PTE_W | PTE_R) == 0) {
        return -1;
    }
    return 0;
}

// return pa when success, else return NULL
struct trapframe *uvm_thread_trapframe(pagetable_t pagetable, int thread_idx) {
    paddr_t pa = (paddr_t)kzalloc(PGSIZE);

    if (mappages(pagetable, TRAPFRAME - thread_idx * PGSIZE, PGSIZE, pa, PTE_R | PTE_W, 0) < 0) {
        kfree((void *)pa);
        return NULL;
    }

    return (struct trapframe *)pa;
}
