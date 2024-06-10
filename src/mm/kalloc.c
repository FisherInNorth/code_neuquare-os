// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "common.h"
#include "param.h"
#include "memory/memlayout.h"
#include "atomic/spinlock.h"
#include "lib/riscv.h"
#include "memory/allocator.h"
#include "memory/buddy.h"
#include "debug.h"
#include "kernel/cpu.h"
#include "atomic/ops.h"

extern char end[];
extern void alloc_fail();

atomic_t pages_cnt;
atomic_t recycling;

static inline int get_pages_cpu(struct page *page) {
    return (page - pagemeta_start) / PAGES_PER_CPU;
}

struct page *steal_mem(int cur_id, uint64 order) {
    struct page *page = NULL;
    for (int i = 0; i < NCPU; i++) {
        if (i == cur_id) {
            continue;
        }
        // TODO
        push_off();
        page = buddy_get_pages(&mempools[i], order);
        if (page != NULL) {
            return page;
        }
    }
    ASSERT(page == NULL);
    return page;
}

uint64 size_to_page_order(uint64 size) {
    uint64 order;
    uint64 page_num;
    uint64 tmp;

    order = 0;
    page_num = ROUND_UP(size, PGSIZE) / PGSIZE;
    tmp = page_num;

    while (tmp > 1) {
        tmp >>= 1;
        order += 1;
    }

    if (page_num > (1 << order)) {
        order += 1;
    }

    return order;
}

void *kmalloc(size_t size) {
    uint64 order;
    if (size <= PGSIZE) {
        order = 0;
    } else {
        order = size_to_page_order(size);
    }

    push_off();
    int id = cpuid();
    ASSERT(id >= 0 && id < NCPU);
    struct page *page = buddy_get_pages(&mempools[id], order);

    if (page == NULL) {
        page = steal_mem(id, order);
        if (page == NULL) {
            return 0;
        }
    }

    // acquire(&page->lock);
    // ASSERT(page->count == 0);
    ASSERT(atomic_read(&page->refcnt) == 0);
    atomic_set(&page->refcnt, 1);
    // page->count = 1;
    // release(&page->lock);
    void *page_ret = (void *)page_to_pa(page);
    // if (page_ret != NULL) {
    atomic_sub_return(&pages_cnt, 1 << page->order);
    // if (atomic_read(&pages_cnt) < PAGES_THRESHOLD) {
    //     alloc_fail();
    // }
    if (!atomic_read(&recycling) && atomic_read(&pages_cnt) < PAGES_THRESHOLD) {
        atomic_inc_return(&recycling);
        alloc_fail();
        atomic_dec_return(&recycling);
    }
    // if(atomic_read(&pages_cnt) < 10000) {
    //     printf("ready\n");
    // }
    // int cnt = 1<< page->order;

    // if(cnt == 4) {
    //     printfBlue("ready\n");
    // }
    // if(cnt == 1) {
    //     printfRed("prepare\n");
    // }
    // if(cnt == 2) {
    //     printf("page cnt : 2\n");
    // }
    // if(cnt == 4096) {
    //     printf("page cnt : 4096\n");
    // }
    // if(cnt == 8) {
    //     printf("page cnt : 4096\n");
    // }
    // if(cnt == 16) {
    //     printf("page cnt : 16\n");
    // }
    // if(cnt == 64) {
    //     printf("page cnt : 64\n");
    // }
    // if(cnt == 4096) {
    //     printf("alloc, page cnt : 4096\n");
    // }
    
    // if (cnt > 2)
    //     printfRed("kmalloc, page alloc : %d pages \n", cnt);
    return page_ret;
}

void *kzalloc(size_t size) {
    void *ptr;

    ptr = kmalloc(size);

    /* lack of memory */
    if (ptr == NULL)
        return NULL;

    memset(ptr, 0, size);
    return ptr;
}

/* compatible with the old kalloc call, use kmalloc instead */
void *kalloc(void) {
    int order = 0;

    push_off();
    int id = cpuid();
    // printf("%d\n", NCPU);
    ASSERT(id >= 0 && id < NCPU);
    struct page *page = buddy_get_pages(&mempools[id], order);

    if (page == NULL) {
        page = steal_mem(id, order);
        if (page == NULL) {
            return 0;
        }
    }

    // acquire(&page->lock);
    ASSERT(atomic_read(&page->refcnt) == 0);
    atomic_set(&page->refcnt, 1);
    // ASSERT(page->count == 0);
    // page->count = 1;
    // release(&page->lock);
    void *page_ret = (void *)page_to_pa(page);
    // if (page_ret != NULL) {
    atomic_sub_return(&pages_cnt, 1 << page->order);
    // }

    if (!atomic_read(&recycling) && atomic_read(&pages_cnt) < PAGES_THRESHOLD) {
        atomic_inc_return(&recycling);
        alloc_fail();
        atomic_dec_return(&recycling);
    }

    // int cnt = 1<< page->order;
    // if(cnt == 4) {
    //     printfBlue("ready\n");
    // }
    // if(cnt == 2) {
    //     printf("page cnt : 2\n");
    // }
    // if(cnt == 1) {
    //     printfRed("prepare\n");
    // }
    // if(cnt == 16) {
    //     printf("page cnt : 16\n");
    // }
    // if (cnt > 2)
    //     printfRed("kalloc, page alloc : %d pages \n", cnt);
    return page_ret;
}

void kfree(void *pa) {
    struct page *page = pa_to_page((uint64)pa);
    acquire(&page->lock);
    // ASSERT(page->count >= 1);
    // ASSERT(atomic_read(&page->refcnt) >= 1);
    if (atomic_read(&page->refcnt) < 1) {
        panic("kfree : page ref error\n");
    }

    // page->count--;
    atomic_dec_return(&page->refcnt);
    if (atomic_read(&page->refcnt) >= 1) {
        release(&page->lock);
        return;
    }
    // ASSERT(page->count == 0);
    ASSERT(atomic_read(&page->refcnt) == 0);
    release(&page->lock);

    ASSERT(page->allocated == 1);
    int id = get_pages_cpu(page);
    ASSERT(id >= 0 && id < NCPU);

    // if (page != NULL) {
    atomic_add_return(&pages_cnt, 1 << page->order);
    // }

    // int cnt = 1<< page->order;
    // if(cnt == 4096) {
    //     printfBlue("free, page cnt : 4096\n");
    // }
    
    buddy_free_pages(&mempools[id], page);
}

void share_page(uint64 pa) {
    struct page *page = pa_to_page(pa);
    // acquire(&page->lock);
    // ASSERT(page->count >= 1);
    ASSERT(atomic_read(&page->refcnt) >= 1);
    atomic_inc_return(&page->refcnt);
    // page->count++;

    // release(&page->lock);
    return;
}
