#include "common.h"
#include "kernel/trap.h"
#include "proc/pcb_life.h"
#include "debug.h"
#include "memory/vma.h"
#include "fs/fcntl.h"
#include "memory/vm.h"
#include "lib/riscv.h"
#include "fs/vfs/fs.h"
#include "kernel/syscall.h"
#include "atomic/spinlock.h"
#include "proc/tcb_life.h"

/* int munmap(void *addr, size_t length); */
uint64 sys_munmap(void) {
    vaddr_t addr;
    size_t length;
    argaddr(0, &addr);
    argulong(1, &length);
    struct proc *p = proc_current();
    struct tcb *t = thread_current();

    // print_vma(&p->mm->head_vma);
    // struct vma *v1 = find_vma_for_va(p->mm, t->ustack);

    struct vma *v2 = find_vma_for_va(p->mm, addr);
    // TODO: fix
    if ((strcmp(p->name, "entry-dynamic.exe") == 0 || strcmp(p->name, "entry-static.exe") == 0) && t->tidx != 0 && v2->type == VMA_ANON) {
        // Log("ustack hit");
        return 0;
    }

    acquire(&p->mm->lock);
    if (vmspace_unmap(p->mm, addr, length) != 0) {
        release(&p->mm->lock);
        return -1;
    }
    release(&p->mm->lock);
    return 0;
}

static uint64 mkperm(int prot, int flags) {
    uint64 perm = 0;
    if (flags & MAP_SHARED) {
        perm |= PERM_SHARED;
    }
    return (perm | prot);
}

void *do_mmap(vaddr_t addr, size_t length, int prot, int flags, struct file *fp, off_t offset) {
    struct mm_struct *mm = proc_current()->mm;
    // sema_wait(&mm->mmap_sem);
    vaddr_t mapva = 0;
    if (addr == 0) {
        // acquire(&mm->lock);
        mapva = find_mapping_space(mm, addr, length);
        // release(&mm->lock);
    } else {
        if ((flags & MAP_FIXED) == 0) {
            Warn("mmap: not support");
            // sema_signal(&mm->mmap_sem);
            return MAP_FAILED;
        }

        uint64 start, end;
        start = addr;
        end = addr + length;
        struct vma *vma;
        if ((vma = find_vma_for_va(mm, addr)) != NULL) {
            // print_vma(&mm->head_vma);
            if (start != vma->startva) {
                if (split_vma(mm, vma, start, 1) < 0) {
                    // sema_signal(&mm->mmap_sem);
                    return MAP_FAILED;
                }
            }

            if (end != vma->startva + vma->size) {
                if (split_vma(mm, vma, end, 0) < 0) {
                    // sema_signal(&mm->mmap_sem);
                    return MAP_FAILED;
                }
            }

            // print_vma(&mm->head_vma);
            if (vma != NULL) {
                del_vma_from_vmspace(&mm->head_vma, vma);
            }
            mapva = addr;
            // offset = ;
            // print_vma(&mm->head_vma);
            // if (walkaddr(mm->pagetable, mapva) != 0) {
            //     Warn("the addr %p is already mapped", addr);
            //     return MAP_FAILED;
            // }
        }
    }
    if (flags & MAP_ANONYMOUS || fp == NULL) {
    // if (fp == NULL) {
        if (vma_map(mm, mapva, length, mkperm(prot, flags), VMA_ANON) < 0) {
            // sema_signal(&mm->mmap_sem);
            return MAP_FAILED;
        }
    } else {
        if (vma_map_file(mm, mapva, length, mkperm(prot, flags), VMA_FILE, offset, fp) < 0) {
            // sema_signal(&mm->mmap_sem);
            return MAP_FAILED;
        }
    }
    // print_vma(&mm->head_vma);

    // print_vma(&mm->head_vma);
    // sema_signal(&mm->mmap_sem);
    return (void *)mapva;
}

// return type!!!
// 与声明不兼容不会出错
/* void *mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset); */
void *sys_mmap(void) {
    vaddr_t addr;
    size_t length;
    int prot;
    int flags;
    int fd;
    off_t offset;
    struct file *fp;

    argaddr(0, &addr);
    argulong(1, &length);
    argint(2, &prot);
    argint(3, &flags);
    if (argfd(4, &fd, &fp) < 0) {
        fp = NULL;
        if ((flags & MAP_ANONYMOUS) == 0) {
            Log("hit");
        }
    }
    arglong(5, &offset);

    if (offset != 0) {
        Warn("mmap: not support");
        return MAP_FAILED;
    }
    struct mm_struct *m = proc_current()->mm;
    // sema_wait(&m->mmap_sem);
    acquire(&m->lock);
    void *retval = do_mmap(addr, length, prot, flags, fp, offset);
    release(&m->lock);
    // sema_signal(&m->mmap_sem);
    return retval;
}

/* int mprotect(void *addr, size_t len, int prot); */
uint64 sys_mprotect(void) {
    vaddr_t start;
    size_t len;
    int prot;
    argaddr(0, &start);
    argulong(1, &len);
    argint(2, &prot);

    if (!len)
        return 0;
    len = PGROUNDUP(len);
    uint64 end = start + len;
    if (end < start)
        return -1;

    if (prot & ~(PROT_READ | PROT_WRITE | PROT_EXEC))
        return -1;

    struct vma *vma = find_vma_for_va(proc_current()->mm, start);
    if (vma == NULL) {
        return -1;
    }

    struct mm_struct *mm = proc_current()->mm;
    // print_vma(&mm->head_vma);
    if (start != vma->startva) {
        if (split_vma(mm, vma, start, 1) < 0) {
            return -1;
        }
    }

    if (end != vma->startva + vma->size) {
        if (split_vma(mm, vma, end, 0) < 0) {
            return -1;
        }
    }
    // printfYELLOW("==========================");
    // print_vma(&mm->head_vma);

    vma->perm = prot;
    return 0;
}