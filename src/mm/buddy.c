#include "memory/buddy.h"
#include "common.h"
#include "memory/memlayout.h"
#include "lib/list.h"
#include "lib/riscv.h"
#include "debug.h"
#include "atomic/ops.h"

extern atomic_t pages_cnt;
extern atomic_t recycling;

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.
uint64 npages;

struct phys_mem_pool mempools[NCPU];
static struct page *merge_page(struct phys_mem_pool *pool, struct page *page);
static struct page *split_page(struct phys_mem_pool *pool, uint64 order, struct page *page);
void init_buddy(struct phys_mem_pool *pool, struct page *start_page, uint64 start_addr, uint64 page_num);

// uint64 get_free_mem();// debug
static inline uint64 page_to_offset(struct phys_mem_pool *pool, struct page *page) {
    return (page - pool->page_metadata) * PGSIZE;
}

static struct page *offset_to_page(struct phys_mem_pool *pool, uint64 offset) {
    ASSERT(offset % PGSIZE == 0);
    return (offset / PGSIZE + pool->page_metadata);
}

static struct page *get_buddy(struct phys_mem_pool *pool, struct page *page) {
    ASSERT(page->order < BUDDY_MAX_ORDER);
    uint64 this_off = page_to_offset(pool, page);
    uint64 buddy_off = this_off ^ (1UL << (page->order + 12));

    /* Check whether the buddy belongs to pool */
    if (buddy_off >= pool->mem_size) {
        return NULL;
    }

    return offset_to_page(pool, (uint64)buddy_off);
}

struct page *pagemeta_start;
void mm_init() {
    // readonly! can not modify!
    pagemeta_start = (struct page *)PGROUNDUP((uint64)end);
    Info("=========Information of RAM==========\n");
    Info("pagemeta_start: %x\n", pagemeta_start);
    Info("NPAGES: %d\n", NPAGES);
    atomic_set(&pages_cnt, NPAGES);
    atomic_set(&recycling, 0);
    Info("PAGES PER CPU: %d\n", PAGES_PER_CPU);
    for (int i = 0; i < NCPU; i++) {
        init_buddy(&mempools[i],
                   (struct page *)PGROUNDUP((uint64)end) + i * PAGES_PER_CPU,
                   (uint64)START_MEM + i * PAGES_PER_CPU * PGSIZE,
                   PAGES_PER_CPU);
    }
    Info("buddy system init [ok]\n");
}

static int cur = 0;
void init_buddy(struct phys_mem_pool *pool, struct page *start_page, uint64 start_addr, uint64 page_num) {
    // int mem_size = PGSIZE * page_num;
    // Log("%d mem size: %dM", cur, mem_size / 1024 / 1024);
    // Log("%d start_mem: %#x", cur, start_addr);
    // Log("%d end mem: %#x", cur, start_addr + mem_size);
    // Log("%d page_num %d", cur, page_num);
    Info("=========Information of memory for CPU %d==========\n", cur);
    Info("%d pagemeta start: %x\n", cur, start_page);
    Info("%d pagemeta end: %x\n", cur, (uint64)start_page + page_num * sizeof(struct page));
    ASSERT((uint64)start_page + page_num * sizeof(struct page) < START_MEM);
    pool->start_addr = start_addr;
    pool->page_metadata = start_page;
    pool->mem_size = PGSIZE * page_num;

    // Log("start_page_metadata: %#x", pool->page_metadata);
    // Log("start_addr: %#x", pool->start_addr);
    cur++;

    /* Init the spinlock */
    initlock(&pool->lock, "buddy_phy_mem_pools_lock");

    /* Init the free lists */
    for (int order = 0; order <= BUDDY_MAX_ORDER; ++order) {
        INIT_LIST_HEAD(&pool->freelists[order].lists);
        pool->freelists[order].num = 0;
    }

    /* Clear the page_metadata area. */
    memset(pool->page_metadata, 0, page_num * sizeof(struct page));

    /* Init the page_metadata area. */
    struct page *page;
    for (int page_idx = 0; page_idx < page_num; ++page_idx) {
        page = start_page + page_idx;
        page->allocated = 1;
        page->order = 0;
        initlock(&page->lock, "page_lock");
    }

    /* Put each physical memory page into the free lists. */
    for (int page_idx = 0; page_idx < page_num; ++page_idx) {
        page = start_page + page_idx;
        buddy_free_pages(pool, page);
    }
    // Log("finish initialization");

    /* make sure the buddy_free_pages works correctly */
    uint64 memsize = 0;
    for (int i = 0; i <= BUDDY_MAX_ORDER; i++) {
        // Log("%d order chunks num: %d", i, pool->freelists[i].num);
        memsize += pool->freelists[i].num * PGSIZE * (1 << i);
    }
    Info("memsize: %u MB\n", memsize / 1024 / 1024);
    ASSERT(memsize == pool->mem_size);
    return;
}

struct page *buddy_get_pages(struct phys_mem_pool *pool, uint64 order) {
    // kalloc() will call push_off, so use pop_off here to prevent long time interrupt off
    pop_off();
    // printf("memory %d PAGES, %d Bytes\n", get_free_mem()/4096, get_free_mem());
    ASSERT(order <= BUDDY_MAX_ORDER);
    struct page *page = NULL;
    struct list_head *lists;

    acquire(&pool->lock);
    for (int i = order; i <= BUDDY_MAX_ORDER; i++) {
        lists = &pool->freelists[i].lists;
        if (!list_empty(lists)) {
            page = list_first_entry(lists, struct page, list);
            list_del(&page->list);
            pool->freelists[page->order].num--;
            break;
        }
    }

    if (page == NULL) {
        // Log("there is no 2^%d mem!", order);
        release(&pool->lock);
        return NULL;
    }

    if (page->order > order) {
        page = split_page(pool, order, page);
    }
    page->allocated = 1;
    ASSERT(page->order == order);
    release(&pool->lock);
    return page;
}

static struct page *split_page(struct phys_mem_pool *pool, uint64 order, struct page *page) {
    ASSERT(page->order > order);

    while (page->order > order) {
        page->order--;
        struct page *buddy = get_buddy(pool, page);
        ASSERT(buddy->allocated == 0);
        buddy->order = page->order;

        list_add(&buddy->list, &pool->freelists[buddy->order].lists);
        pool->freelists[buddy->order].num++;
    }
    return page;
}

void buddy_free_pages(struct phys_mem_pool *pool, struct page *page) {
    acquire(&pool->lock);
    page->allocated = 0;

    if (page->order < BUDDY_MAX_ORDER) {
        page = merge_page(pool, page);
    }

    struct list_head *list = &pool->freelists[page->order].lists;
    list_add(&page->list, list);
    pool->freelists[page->order].num++;
    release(&pool->lock);
}

static struct page *merge_page(struct phys_mem_pool *pool, struct page *page) {
    if (page->order == BUDDY_MAX_ORDER) {
        return page;
    }

    static int max = 0;

    struct page *buddy = get_buddy(pool, page);
    if (buddy == NULL) {
        // this page doesn't have buddy
        return page;
    }

    ASSERT(buddy != NULL);
    if (buddy->allocated == 0 && buddy->order == page->order) {
        // get the buddy page out of the list
        list_del(&buddy->list);
        pool->freelists[buddy->order].num--;

        // merge buddy with current page
        struct page *merge = ((uint64)buddy < (uint64)page ? buddy : page);
        merge->order++;
        if (merge->order > max) {
            max = merge->order;
            // Log("%d", max);
        }
        return merge_page(pool, merge);
    } else {
        return page;
    }
}

uint64 get_free_mem() {
    uint64 memsize = 0;
    for (int cpu = 0; cpu < NCPU; cpu++) {
        struct phys_mem_pool *pool = &mempools[cpu];
        acquire(&pool->lock);
        for (int i = 0; i <= BUDDY_MAX_ORDER; i++) {
            // Log("%d order chunks num: %d", i, pool->freelists[i].num);
            memsize += pool->freelists[i].num * PGSIZE * (1 << i);
        }
        release(&pool->lock);
    }
    return memsize;
}