#include "common.h"
#include "memory/vma.h"
#include "atomic/spinlock.h"
#include "memory/allocator.h"
#include "proc/pcb_life.h"
#include "proc/tcb_life.h"
#include "lib/riscv.h"
#include "lib/list.h"
#include "debug.h"
#include "memory/vm.h"
#include "memory/mm.h"
#include "fs/fat/fat32_file.h"
#include "fs/vfs/fs.h"
#include "fs/vfs/ops.h"

struct vma vmas[NVMA];
struct spinlock vmas_lock;

static struct vma *vma_map_range(struct mm_struct *mm, uint64 va, size_t len, uint64 perm, uint64 type);
void vmas_init() {
    initlock(&vmas_lock, "vmas_lock");
    memset(vmas, 0, sizeof(vmas));
    Info("vma init [ok]\n");
}

static struct vma *alloc_vma(void) {
    // 1. slab allocator
    // 2. fine grained lock
    acquire(&vmas_lock);
    for (int i = 0; i < NVMA; i++) {
        if (vmas[i].used == 0) {
            vmas[i].used = 1;
            release(&vmas_lock);
            return &vmas[i];
        }
    }
    release(&vmas_lock);
    return 0;
}

void free_vma(struct vma *vma) {
    acquire(&vmas_lock);
    vma->used = 0;
    release(&vmas_lock);
}

/*
 * Returns 0 when no intersection detected.
 */
static int check_vma_intersect(struct list_head *vma_head, struct vma *checked_vma) {
    vaddr_t checked_start, start;
    vaddr_t checked_end, end;
    struct vma *pos;

    checked_start = checked_vma->startva;
    checked_end = checked_start + checked_vma->size - 1;

    list_for_each_entry(pos, vma_head, node) {
        start = pos->startva;
        end = pos->startva + pos->size;
        if ((checked_start >= start && checked_start < end) || (checked_end >= start && checked_end < end)) {
            return 1;
        }
    }
    return 0;
}

static int add_vma_to_vmspace(struct list_head *head, struct vma *vma) {
    // printfYELLOW("===============\n");
    // print_vma(head);
    if (check_vma_intersect(head, vma) != 0) {
        // print_vma(head);
        Log("add_vma_to_vmspace: vma overlap\n");
        ASSERT(0);
        return -1;
    }

    list_add(&vma->node, head);
    return 0;
}

static int is_vma_in_vmspace(struct list_head *vma_head, struct vma *vma) {
    struct vma *pos;

    list_for_each_entry(pos, vma_head, node) {
        if (pos == vma) {
            return 1;
        }
    }

    return 0;
}

void del_vma_from_vmspace(struct list_head *vma_head, struct vma *vma) {
    if (is_vma_in_vmspace(vma_head, vma)) {
        list_del(&(vma->node));
    } else {
        ASSERT(0);
    }
    free_vma(vma);
}

void print_rawfile(struct file *f, int fd, int printdir);
int vma_map_file(struct mm_struct *mm, uint64 va, size_t len, uint64 perm, uint64 type, off_t offset, struct file *fp) {
    struct vma *vma;
    /* the file isn't writable and perm has PERM_WRITE is illegal
       but if the PERM_SHARED is not set(means PERM_PRIVATE), then it's ok */
    if (!F_WRITEABLE(fp) && ((perm & PERM_WRITE) && (perm & PERM_SHARED))) {
        return -1;
    }
    if ((vma = vma_map_range(mm, va, len, perm, type)) == NULL) {
        return -1;
    }
    // print_vma(&mm->head_vma);
    // vma->fd = fd;
    vma->offset = offset;
    vma->vm_file = fp;
    fat32_filedup(vma->vm_file);
    // print_rawfile(vma->vm_file, 0, 0);
    return 0;
}

int vma_map(struct mm_struct *mm, uint64 va, size_t len, uint64 perm, uint64 type) {
    // Log("%p %p %#x", va, va + len, perm);
    struct vma *vma;
    if ((vma = vma_map_range(mm, va, len, perm, type)) == NULL) {
        return -1;
    } else {
        if (type == VMA_HEAP) {
            mm->heapvma = vma;
        }
        return 0;
    }
}

static struct vma *vma_map_range(struct mm_struct *mm, uint64 va, size_t len, uint64 perm, uint64 type) {
    struct vma *vma;
    vma = alloc_vma();
    if (vma == NULL) {
        return 0;
    }

    // not support
    ASSERT(va % PGSIZE == 0);

    vma->startva = PGROUNDDOWN(va);
    if (len < PGSIZE) {
        len = PGSIZE;
    }
    vma->size = PGROUNDUP(len);
    vma->perm = perm;
    vma->type = type;

    if (add_vma_to_vmspace(&mm->head_vma, vma) < 0) {
        goto free;
    }
    return vma;

free:
    free_vma(vma);
    return 0;
}

static void writeback(pagetable_t pagetable, struct file *fp, vaddr_t start, size_t len) {
    ASSERT(start % PGSIZE == 0);
    ASSERT(fp != NULL);

    pte_t *pte;
    vaddr_t endva = start + len;
    for (vaddr_t addr = start; addr < endva; addr += PGSIZE) {
        walk(pagetable, addr, 0, 0, &pte);
        if (pte == NULL || (*pte & PTE_V) == 0) {
            continue;
        }
        /* only writeback dirty pages(pages with PTE_D) */
        if (PTE_FLAGS(*pte) & PTE_D) {
            fat32_filewrite(fp, addr, (PGSIZE > (endva - addr) ? (endva - addr) : PGSIZE));
        }
    }
}

int vmspace_unmap(struct mm_struct *mm, vaddr_t va, size_t len) {
    struct vma *vma;
    vaddr_t start;
    size_t size;

    vma = find_vma_for_va(mm, va);
    if (!vma) {
        Warn("vmspace_unmap: va is not in vmas");
        return -1;
    }

    start = vma->startva;
    size = vma->size;

    if ((va != start)) {
        Log("we only support unmap at the start now.\n");
        return -1;
    }

    // ASSERT(len % PGSIZE == 0);
    size_t origin_len = len;
    len = PGROUNDUP(len);

    if (vma->type == VMA_FILE) {
        /* if the perm has PERM_SHREAD, call writeback */
        if ((vma->perm & PERM_SHARED) && (vma->perm & PERM_WRITE)) {
            // if(start == 0x32407000) {
            // vmprint(mm->pagetable, 1, 0, 0x32406000, 0);
            // print_vma(&mm->head_vma);
            writeback(mm->pagetable, vma->vm_file, start, origin_len);
            // }
        }
    }

    if (size > len) {
        /* unmap part of the vma */
        vma->startva += len;
        vma->size -= len;
        uvmunmap(mm->pagetable, start, PGROUNDUP(size) / PGSIZE, 1, 1);
        return 0;
    }

    del_vma_from_vmspace(&mm->head_vma, vma);

    // Note: non-leaf pte still not recycle
    uvmunmap(mm->pagetable, start, PGROUNDUP(size) / PGSIZE, 1, 1);

    if (size < len) {
        // print_vma(&mm->head_vma);
        // size < len: in case the vma has split before
        vmspace_unmap(mm, va + size, len - size);
    }

    return 0;
}

struct vma *find_vma_for_va(struct mm_struct *mm, vaddr_t addr) {
    struct vma *pos;
    vaddr_t start, end;
    list_for_each_entry(pos, &mm->head_vma, node) {
        start = pos->startva;
        end = pos->startva + pos->size;
        if (addr >= start && addr < end) {
            return pos;
        }
    }
    return NULL;
}

#define MMAP_START 0x30000000
vaddr_t find_mapping_space(struct mm_struct *mm, vaddr_t start, size_t size) {
    struct vma *pos;
    vaddr_t max = MMAP_START;
    list_for_each_entry(pos, &mm->head_vma, node) {
        if (pos->type == VMA_INTERP) {
            continue;
        }
        if (pos->type == VMA_STACK) {
            continue;
        }
        vaddr_t endva = pos->startva + pos->size;
        if (max < endva) {
            max = endva;
        }
    }
    ASSERT(max % PGSIZE == 0);

    // assert code: make sure the max address is not in pagetable(not mapping)
    pte_t *pte;
    int ret;
    // acquire(&mm->lock);
    ret = walk(mm->pagetable, max, 0, 0, &pte);
    // release(&mm->lock);
    // Log("%p", max);
    // vmprint(mm->pagetable, 1, 0, 0, 0);
    // ASSERT(ret == -1 || *pte == 0);
    if (!(ret == -1 || *pte == 0)) {
        panic("this page not map???\n");
    }
    return max;
}

void sys_print_vma() {
    struct mm_struct *mm = proc_current()->mm;
    print_vma(&mm->head_vma);
}

void print_vma(struct list_head *head_vma) {
    struct vma *pos;
    // VMA("%s vmas:\n", proc_current()->name);
    printfYELLOW("=====================\n");
    list_for_each_entry(pos, head_vma, node) {
        VMA("%#p-%#p %dKB\t", pos->startva, pos->startva + pos->size, pos->size / 1024);
        if (pos->perm & PERM_READ) {
            VMA("r");
        } else {
            VMA("-");
        }
        if (pos->perm & PERM_WRITE) {
            VMA("w");
        } else {
            VMA("-");
        }
        if (pos->perm & PERM_EXEC) {
            VMA("x");
        } else {
            VMA("-");
        }
        if (pos->perm & PERM_SHARED) {
            VMA("s");
        } else {
            VMA("p");
        }
        switch (pos->type) {
        case VMA_TEXT: VMA("  VMA_TEXT  "); break;
        case VMA_STACK: VMA("  VMA_STACK  "); break;
        case VMA_HEAP: VMA("  VMA_HEAP  "); break;
        case VMA_FILE:
            VMA("  VMA_FILE  ");
            VMA("%p", pos->offset);
            break;
        case VMA_ANON: VMA("  VMA_ANON  "); break;
        case VMA_INTERP: VMA("  libc.so  "); break;
        default: panic("no such vma type");
        }
        VMA("\n");
    }
}

int vmacopy(struct mm_struct *srcmm, struct mm_struct *dstmm) {
    struct vma *pos;
    list_for_each_entry(pos, &srcmm->head_vma, node) {
        if (pos->type == VMA_FILE) {
            // int vma_map_file(struct proc *p, uint64 va, size_t len, uint64 perm, uint64 type,
            //                  int fd, off_t offset, struct file *fp) {
            if (vma_map_file(dstmm, pos->startva, pos->size, pos->perm, pos->type,
                             pos->offset, pos->vm_file)
                < 0) {
                return -1;
            }
        } else {
            if (vma_map(dstmm, pos->startva, pos->size, pos->perm, pos->type) < 0) {
                return -1;
            }
            // panic("not support");
        }
    }
    return 0;
}

void free_all_vmas(struct mm_struct *mm) {
    struct vma *pos_cur;
    struct vma *pos_tmp;
    // vmprint(mm->pagetable, 1, 0, 0, 0);
    // print_vma(&mm->head_vma);
    list_for_each_entry_safe(pos_cur, pos_tmp, &mm->head_vma, node) {
        // Warn("%p~%p", pos->startva, pos->size);
        // print_vma(&mm->head_vma);
        // if (pos_cur->type == VMA_INTERP) {
        //     continue;
        // }
        if (pos_cur->type == VMA_HEAP && pos_cur->size == 0) {
            del_vma_from_vmspace(&mm->head_vma, pos_cur);
            continue;
        }
        if (vmspace_unmap(mm, pos_cur->startva, pos_cur->size) < 0) {
            panic("free_all_vmas: unmap failed");
        }
    }
}

/*
 * Split a vma into two pieces at address 'addr', a new vma is allocated
 * either for the first part or the tail.
 */
int split_vma(struct mm_struct *mm, struct vma *vma, unsigned long addr, int new_below) {
    struct vma *new;

    new = alloc_vma();
    if (!new) {
        // TODO, SLOB!!!!!
        // Warn("split_vma: no free mem");
        return -1;
    }

    /* most fields are the same, copy all */
    *new = *vma;

    if (new_below) {
        vma->size = vma->startva + vma->size - addr;
        vma->startva = addr;
        vma->offset += (addr - new->startva);
        new->size = addr - new->startva;
    } else {
        new->startva = addr;
        new->size = vma->startva + vma->size - addr;
        vma->size = addr - vma->startva;
        new->offset += (addr - vma->startva);
    }
    // Log("%p %p", new->startva, new->startva + new->size);
    // Log("%p %p", vma->startva, vma->startva + vma->size);

    if (new->vm_file)
        fat32_filedup(new->vm_file);

    if (add_vma_to_vmspace(&mm->head_vma, new) < 0) {
        free_vma(new);
        Warn("split_vma: add_vma_to_vmspace failed");
        return -1;
    }
    return 0;
}