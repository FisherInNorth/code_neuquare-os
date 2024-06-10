#include "common.h"
#include "memory/vma.h"
#include "lib/riscv.h"
#include "kernel/trap.h"
#include "proc/pcb_life.h"
#include "memory/vm.h"
#include "memory/allocator.h"
#include "fs/vfs/fs.h"
#include "debug.h"
#include "memory/mm.h"
#include "memory/pagefault.h"


static uint32 perm_vma2pte(uint32 vma_perm) {
    uint32 pte_perm = 0;
    if (vma_perm & PERM_READ) {
        pte_perm |= PTE_R;
    }
    if (vma_perm & PERM_WRITE) {
        pte_perm |= PTE_W;
    }
    if (vma_perm & PERM_EXEC) {
        pte_perm |= PTE_X;
    }
    return pte_perm;
}

int is_a_cow_page(int flags) {
    /* write to an unshared page is illegal */
    if ((flags & PTE_SHARE) == 0) {
        PAGEFAULT("cow: try to write a readonly page");
        return 0;
    }

    /* write to readonly shared page is illegal */
    if ((flags & PTE_READONLY) > 0) {
        PAGEFAULT("cow: try to write a readonly shared page");
        return 0;
    }
    return 1;
}

int pagefault(uint64 cause, pagetable_t pagetable, vaddr_t stval) {
    /* the va exceed the MAXVA is illegal */
    if (PGROUNDDOWN(stval) >= MAXVA) {
        PAGEFAULT("exceed the MAXVA");
        return -1;
    }

    struct vma *vma = find_vma_for_va(proc_current()->mm, stval);
    // if (stval > 0x3000b0000) {
    //     Log("hit");
    // }
    if (vma != NULL) {
        if (!CHECK_PERM(cause, vma)) {
            print_vma(&proc_current()->mm->head_vma);
            PAGEFAULT("permission checked failed");
            return -1;
        }

        pte_t *pte;
        uint64 pa;
        uint flags;
        int level;
        level = walk(pagetable, stval, 0, 0, &pte);
        if (pte == NULL || (*pte == 0)) {
            uvmalloc(pagetable, PGROUNDDOWN(stval), PGROUNDUP(stval + 1), perm_vma2pte(vma->perm));
            if (vma->type == VMA_FILE) {
                paddr_t pa = walkaddr(pagetable, stval);

                fat32_inode_lock(vma->vm_file->f_tp.f_inode);
                fat32_inode_read(vma->vm_file->f_tp.f_inode, 0, pa, vma->offset + PGROUNDDOWN(stval) - vma->startva, PGSIZE);
                fat32_inode_unlock(vma->vm_file->f_tp.f_inode);
            }
        } else {
            pa = PTE2PA(*pte);
            flags = PTE_FLAGS(*pte);
            ASSERT(flags & PTE_V);
            /* copy-on-write handler */
            if (is_a_cow_page(flags)) {
                return cow(pte, level, pa, flags);
            } else {
                return -1;
            }
        }

    } else {
        print_vma(&proc_current()->mm->head_vma);
        PAGEFAULT("va is not in the vmas");
        return -1;
    }

    return 0;
}

int cow(pte_t *pte, int level, paddr_t pa, int flags) {
    void *mem;
    if (level == SUPERPAGE) {
        // 2MB superpage
        if ((mem = kmalloc(SUPERPGSIZE)) == 0) {
            return -1;
        }
        memmove(mem, (void *)pa, SUPERPGSIZE);
    } else if (level == COMMONPAGE) {
        // common page
        if ((mem = kmalloc(PGSIZE)) == 0) {
            return -1;
        }
        memmove(mem, (void *)pa, PGSIZE);
    } else {
        PAGEFAULT("the level of leaf pte is wrong");
        return -1;
    }

    *pte = PA2PTE((uint64)mem) | flags | PTE_W;
    kfree((void *)pa);
    return 0;
}