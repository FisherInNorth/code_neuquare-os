#ifndef __FILEMAP_H__
#define __FILEMAP_H__
#include "fs/vfs/fs.h"
#include "memory/buddy.h"

#define READ_AHEAD_RATE 20
#define READ_AHEAD_PAGE_MAX_CNT 8
#define WRITE_FULL_PAGE(rest_val) (rest_val >= PGSIZE)
#define OUT_FILE(offset_cur, offset_tot) ((offset_cur > offset_tot))

#define CHANGE_READ_AHEAD(mapping) (mapping->read_ahead_cnt = ((mapping->read_ahead_cnt) == 0) ? 1 : mapping->read_ahead_cnt * 2)

int add_to_page_cache_atomic(struct page *page, struct address_space *mapping, uint64 index);
struct page *find_get_page_atomic(struct address_space *mapping, uint64 index, int lock);
uint64 max_sane_readahead(uint64 nr, uint64 read_ahead, uint64 tot_nr);
ssize_t do_generic_file_read(struct address_space *mapping, int user_src, uint64 src, uint off, uint n);
ssize_t do_generic_file_write(struct address_space *mapping, int user_src, uint64 src, uint off, uint n);

#endif