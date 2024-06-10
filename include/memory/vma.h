#ifndef __VMA_H__
#define __VMA_H__

#include "common.h"
#include "lib/list.h"
#include "memory/mm.h"
#include "lib/list.h"

#define NVMA 3000

// mmap
#define MAP_FILE 0
#define MAP_SHARED 0x01
#define MAP_PRIVATE 0x02
#define MAP_FIXED 0x10     /* Interpret addr exactly.  */
#define MAP_ANONYMOUS 0x20 /* Don't use a file.  */

// return (void *)0xfffff...ff to indicate fail
#define MAP_FAILED ((void *)-1)

struct proc;

/* permission */
#define PERM_READ (1 << 0)  /* same as PROT_READ */
#define PERM_WRITE (1 << 1) /* same as PROT_WRITE */
#define PERM_EXEC (1 << 2)  /* same as PROT_EXEC */
#define PERM_SHARED (1 << 3)
// #define PERM_KERNEL_PGTABLE (1 << 6)

typedef enum {
    VMA_STACK,
    VMA_HEAP,
    VMA_TEXT,
    VMA_FILE,
    VMA_ANON, /* anonymous */
    VMA_INTERP,
} vmatype;

/* virtual memory area */
struct vma {
    vmatype type;
    struct list_head node;
    vaddr_t startva;
    size_t size;
    uint32 perm;

    // temp
    int used;

    /* for VMA_FILE */
    // int fd;
    uint64 offset;
    struct file *vm_file;
};

extern struct vma vmas[NVMA];
int vma_map_file(struct mm_struct *mm, uint64 va, size_t len, uint64 perm, uint64 type, off_t offset, struct file *fp);
int vma_map(struct mm_struct *mm, uint64 va, size_t len, uint64 perm, uint64 type);
int vmspace_unmap(struct mm_struct *mm, vaddr_t va, size_t len);

struct vma *find_vma_for_va(struct mm_struct *mm, vaddr_t addr);
vaddr_t find_mapping_space(struct mm_struct *mm, vaddr_t start, size_t size);
int split_vma(struct mm_struct *mm, struct vma *vma, unsigned long addr, int new_below);
int vmacopy(struct mm_struct *srcmm, struct mm_struct *dstmm);
void free_all_vmas(struct mm_struct *mm);
void print_vma(struct list_head *head_vma);

// for mmap
void del_vma_from_vmspace(struct list_head *vma_head, struct vma *vma);
void *do_mmap(vaddr_t addr, size_t length, int prot, int flags, struct file *fp, off_t offset);

#endif // __VMA_H__