#ifndef __ALLOCATOR_H__
#define __ALLOCATOR_H__

#include "common.h"
#define PAGES_THRESHOLD 500

/* reserve this to be compatible with the old kalloc call
 * use kmalloc(PGSIZE) instead
 */
void *kalloc(void);

void kfree(void *);
void *kzalloc(size_t size);
void *kmalloc(size_t size);
void share_page(uint64 pa);

/* get available memory size */
uint64 get_free_mem();

#endif // __ALLOCATOR_H__
