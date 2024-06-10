#ifndef __WRITEBACK_H__
#define __WRITEBACK_H__
#include "common.h"
#include "fs/vfs/fs.h"

#define MAX_WRITEBACK_PAGES 1024
#define dirty_writeback_cycle 5 // seconds
// #define PAGES_THRESHOLD 10000

int sync_inode(struct inode *ip);
void wakeup_bdflush(void *nr_pages);
void writeback_inodes(uint64 nr_to_write);
void page_writeback_timer_init(void);

#endif