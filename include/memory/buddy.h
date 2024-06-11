#ifndef __BUDDY_H__
#define __BUDDY_H__

#include "common.h"
#include "atomic/ops.h"
#include "lib/list.h"
#include "atomic/spinlock.h"
#include "param.h"
#include "lib/riscv.h"
#include "memory/memlayout.h"
#include "fs/vfs/fs.h"
#include "debug.h"

/*
    +---------------------------+ <-- rust-sbi jump to 0x80200000
    |     kernel img region     |
    +---------------------------+ <-- kernel end
    |    page_metadata region   |
    +---------------------------+ <-- START_MEM
    |          MEMORY           |
    |           ...             |
    +---------------------------+ <-- PHYSTOP
*/

/* configuration option */
/* Note: to support 2MB superpage, (PHYSTOP - START_MEM) need to be (NCPU * 2MB) aligned! */
#define START_MEM 0x80800000
#define BUDDY_MAX_ORDER 13

#define NPAGES (((PHYSTOP)-START_MEM) / (PGSIZE))
#define PAGES_PER_CPU (NPAGES / NCPU)
extern struct page *pagemeta_start;

// page status
#define PG_locked 0x01
#define PG_dirty 0x02

// chage the refcnt of page (atomic)
#define page_cache_get(page) (atomic_inc_return(&page->refcnt))
#define page_cache_put(page) (atomic_dec_return(&page->refcnt))

struct page {
    // use for buddy system
    int allocated;
    int order;
    struct list_head list;

    // use for copy-on-write
    struct spinlock lock;
    atomic_t refcnt;

    // for i-mapping
    uint64 flags;
    struct address_space *mapping;
    // pagecache index
    uint64 index;
};

struct free_list {
    struct list_head lists;
    int num;
};

struct phys_mem_pool {
    uint64 start_addr;
    uint64 mem_size;
    /*
     * The start virtual address (for used in kernel) of
     * the metadata area of this pool.
     */
    struct page *page_metadata;

    struct spinlock lock;

    /* The free list of different free-memory-chunk orders. */
    struct free_list freelists[BUDDY_MAX_ORDER + 1];
};
extern struct phys_mem_pool mempools[NCPU];

void buddy_free_pages(struct phys_mem_pool *pool, struct page *page);
struct page *buddy_get_pages(struct phys_mem_pool *pool, uint64 order);

static inline void set_page_flags(struct page *page, uint64 flags) {
    set_bit(flags, &page->flags);
}

static inline void clear_page_flags(struct page *page, uint64 flags) {
    clear_bit(flags, &page->flags);
}

static inline uint64 page_to_pa(struct page *page) {
    return (page - pagemeta_start) * PGSIZE + START_MEM;
}
static inline struct page *pa_to_page(uint64 pa) {
    ASSERT((pa - START_MEM) % PGSIZE == 0);
    return ((pa - START_MEM) / PGSIZE + pagemeta_start);
}

#endif // __BUDDY_H__