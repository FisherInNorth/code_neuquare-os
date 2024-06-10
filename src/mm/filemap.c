#include "lib/radix-tree.h"
#include "memory/filemap.h"
#include "atomic/spinlock.h"
#include "memory/buddy.h"
#include "memory/allocator.h"
#include "fs/mpage.h"
#include "atomic/ops.h"
#include "debug.h"
#include "kernel/trap.h"

// add
int add_to_page_cache_atomic(struct page *page, struct address_space *mapping, uint64 index) {
    page->mapping = mapping;
    page->index = index;

    // acquire(&mapping->host->tree_lock);
    int error = radix_tree_insert(&mapping->page_tree, index, page);
    if (likely(!error)) {
        // if(mapping->host->fat32_i.fname[0]=='b')
        // printfRed("index : %x\n", index);
        mapping->nrpages++;
    } else {
        panic("add_to_page_cache : error\n");
    }
    // release(&mapping->host->tree_lock);

#ifdef __DEBUG_PAGE_CACHE__
    if (!error) {
        printfMAGENTA("page_insert : fname : %s, index : %d, pa : %0x, rest memory : %d PAGES\n",
                      mapping->host->fat32_i.fname, index, page_to_pa(page), get_free_mem() / 4096);
    }
#endif
    return error;
}

// find
struct page *find_get_page_atomic(struct address_space *mapping, uint64 index, int lock) {
    struct page *page;

    // acquire(&mapping->host->tree_lock);
    page = (struct page *)radix_tree_lookup_node(&mapping->page_tree, index);
    // release(&mapping->host->tree_lock);

    if (page) {
#ifdef __DEBUG_PAGE_CACHE__
        printf("page_lookup : fname : %s, index : %d, pa : %0x\n",
               mapping->host->fat32_i.fname, index, page_to_pa(page));
#endif
        // page_cache_get(page);
        if (lock)
            acquire(&page->lock);
    }

    return page;
}

// read ahead
uint64 max_sane_readahead(uint64 nr, uint64 read_ahead, uint64 tot_nr) {
    return MIN(MIN(PGROUNDUP(nr) / PGSIZE + read_ahead, DIV_ROUND_UP(FREE_RATE(READ_AHEAD_RATE), PGSIZE)), PGROUNDUP(tot_nr) / PGSIZE);
    // don't forget /PGSIZE
}

// read using mapping
ssize_t do_generic_file_read(struct address_space *mapping, int user_dst, uint64 dst, uint off, uint n) {
    // static int read_cnt = 0;// debug
    // static int read_hit_cnt =0; // debug

    struct inode *ip = mapping->host;
    ASSERT(ip->i_size > 0);

    uint64 index = off >> PGSHIFT; // page number
    uint64 offset = PGMASK(off);   // offset in a page
    uint64 end_index = (ip->i_size - 1) >> PGSHIFT;
    uint32 isize = ip->i_size;
    uint64 read_sane_cnt = 0; // 1, 2, 3, ...

    uint64 pa;
    uint64 nr, len;

    ssize_t retval = 0;

    // // debug
    // char* buf_debug;
    // buf_debug = kzalloc(n);
    // char* buf_debug_init =  buf_debug;

    // printfRed("read content : ");

    // int first_char = 0;

    sema_wait(&ip->i_read_lock);
    while (1) {
        struct page *page;

        /* nr is the maximum number of bytes to copy from this page */
        nr = PGSIZE;
        if (index >= end_index) {
            if (index > end_index)
                goto out;
            // if index == end_index
            nr = (PGMASK((isize - 1))) + 1;
            // it is larger than offset of off
            if (nr <= offset) {
                panic("not tested\n");
                goto out;
            }
        }
        nr = nr - offset;
        /* Find the page */
        page = find_get_page_atomic(mapping, index, 0); // not acquire the lock of page
        // read_cnt++;// debug
        if (page == NULL) {
            read_sane_cnt = max_sane_readahead(n - retval, mapping->read_ahead_cnt, isize - (off + retval));
            mapping->read_ahead_end = index + read_sane_cnt - 1; // !!!
            // ASSERT(read_sane_cnt > 0);
            if (read_sane_cnt <= 0) {
                goto out;
                // panic("read_sane_cnt : error\n");
            }
#ifdef __DEBUG_PAGE_CACHE__
            printfRed("read miss : fname : %s, off : %d, n : %d, index : %d, offset : %d, read_sane_cnt : %d, read_ahead_cnt : %d, read_ahead_end : %d\n",
                      ip->fat32_i.fname, off, n, index, offset, read_sane_cnt, mapping->read_ahead_cnt, mapping->read_ahead_end);
#endif
            pa = mpage_readpages(ip, index, read_sane_cnt, 1, 0); // must read from disk, can't allocate new clusters

            // change the read_ahead_cnt dynamically
            if (index > (mapping->last_index)) {
                int ahead_tmp = mapping->read_ahead_end + mapping->read_ahead_cnt;
                if (ahead_tmp <= end_index) {
                    if (mapping->read_ahead_cnt < READ_AHEAD_PAGE_MAX_CNT) {
                        CHANGE_READ_AHEAD(mapping); // exponential growth with base 2
                    } else {
                        mapping->read_ahead_cnt += 1; // linear growth with 1
                    }
                }
            } else {
                // not increase, set cnt to zero
                mapping->read_ahead_cnt = 0;
            }
            mapping->last_index = index;
        } else {
#ifdef __DEBUG_PAGE_CACHE__
            printfGreen("read hit : fname : %s, off : %d, n : %d, index : %d, offset : %d, read_ahead_cnt : %d, read_ahead_end : %d\n",
                        ip->fat32_i.fname, off, n, index, offset, mapping->read_ahead_cnt, mapping->read_ahead_end);
#endif
            pa = page_to_pa(page);
            // read_hit_cnt ++;// debug
            // printf("read hit : %d/%d\n",read_hit_cnt, read_cnt);// debug

        }

        // similar to fat32_inode_read
        // it is illegal to read beyond isize!!! (maybe it is reasonable to fill zero)
        len = MIN(MIN(n - retval, nr), isize - offset);

        if (either_copyout(user_dst, dst, (void *)(pa + offset), len) == -1) {
            // panic("do_generic_file_read : copyout error\n");
            retval = -1;
            goto out;
        }

        // if(first_char==0){
        //     // printf("read content : %c\n", *(char*)(pa+offset));
        //     first_char=1;

        //     if(*(char*)(pa+offset)!='p') {
        //         printf("ready\n");
        //     }
        // }
        // printf("%d", *(char*)(pa+offset));

        // debug
        // memmove((void*)buf_debug, (void*)(pa+offset), len);
        // buf_debug+=len;

        // off、retval、src
        // unit is byte
        off += len;
        retval += len;
        dst += len;

        // printfRed("name : %s, retval : %d, n : %d, index : %d, end_index : %d\n",
        //         ip->fat32_i.fname, retval, n, index, end_index);
        if (retval == n) {
            break; // !!!
        }

        // index and offset
        // page_no + page_offset
        index = (off >> PGSHIFT);
        offset = PGMASK(off);
    }

out:
    // debug
    // printfRed("read content: %s\n", buf_debug_init);
    // kfree(buf_debug_init);
    // printf("\n");
    sema_signal(&ip->i_read_lock);
    return retval;
}

// write using mapping
ssize_t do_generic_file_write(struct address_space *mapping, int user_src, uint64 src, uint off, uint n) {
    // static int write_cnt = 0;// debug
    // static int write_hit_cnt =0; // debug
    // static int read_from_disk_cnt = 0;// debug
    

    struct inode *ip = mapping->host;
    ASSERT(n > 0);
    uint64 index = off >> PGSHIFT; // page number
    uint64 offset = PGMASK(off);   // offset in a page
    uint64 pa;
    uint64 nr, len;

    // uint64 isize = PGROUNDUP(ip->i_size);
    uint64 isize_offset = ip->i_size % PGSIZE;

    // // debug
    // char* buf_debug;
    // buf_debug =  (char*)kzalloc(n);
    // char* buf_debug_init =  buf_debug;

    // printfGreen("write content : ");

    // int first_char = 0;
    ssize_t retval = 0;

    // printf("write begin : \n");
    sema_wait(&ip->i_read_lock);
    while (1) {
        struct page *page;

        /* nr is the maximum number of bytes to copy from this page */
        nr = PGSIZE - offset;
        page = find_get_page_atomic(mapping, index, 0); // not acquire lock
        // write_cnt++;
        if (page == NULL) {
            int read_from_disk = -1;

            // bug!!!(in iozone)
            // if (!OUTFILE(index, isize) && NOT_FULL_PAGE(offset)) {
            if (WRITE_FULL_PAGE(n - retval) || OUT_FILE(offset, isize_offset)) {
                // if (WRITE_FULL_PAGE(retval) || OUT_FILE(offset, isize_offset)) {
                // need read page in disk
                read_from_disk = 0;
            } else {
                // panic("not tested\n");
                read_from_disk = 1;
                // read_from_disk_cnt++;
                // printf("nread from disk cnt , %d/%d, n-retval : %d, offset : %d, isize_offset : %d, isize : %d\n", read_from_disk_cnt, write_cnt, n-retval, offset, isize_offset, ip->i_size);
            }
            pa = mpage_readpages(ip, index, 1, read_from_disk, 1); // just read one page, allocate clusters if necessary
            page = pa_to_page(pa);

#ifdef __DEBUG_PAGE_CACHE__
            printfCYAN("write miss : fname : %s, off : %d, n : %d, index : %d, offset : %d, read_from_disk : %d\n",
                       ip->fat32_i.fname, off, n, index, offset, read_from_disk);
#endif
        } else {
#ifdef __DEBUG_PAGE_CACHE__
            printfBlue("write hit : fname : %s, off : %d, n : %d, index : %d, offset : %d\n",
                       ip->fat32_i.fname, off, n, index, offset);
#endif
            pa = page_to_pa(page);
            // write_hit_cnt++;
            // printf("write hit : %d/%d\n", write_hit_cnt, write_cnt);// debug     
        }

        // printf("write : %x\n", pa);

        // similar to fat32_inode_read
        len = MIN(n - retval, nr);
        if (either_copyin((void *)(pa + offset), user_src, src, len) == -1) {
            // panic("do_generic_file_write : copyin error\n");
            retval = -1;
            goto out;
        }

        // if(first_char==0){
        //     // printf("read content : %c\n", *(char*)(pa+offset));
        //     first_char=1;
        //     if(*(char*)(pa+offset)!='p') {
        //         printf("ready\n");
        //     }
        // }
        // printf("%x", *(char*)(pa+offset));

        // // debug
        // memmove((void*)buf_debug, (void*)(pa+offset), len);
        // buf_debug+=len;

        // set page dirty
        // set_page_flags(page, PG_dirty);// NOTE!!!

        acquire(&mapping->host->tree_lock);
        radix_tree_tag_set(&mapping->page_tree, index, PAGECACHE_TAG_DIRTY);// NOTE!!!
        release(&mapping->host->tree_lock);

        // put and release (don't need it, maybe?)
        // page_cache_put(page);
        // release(&page->lock);

        // off、retval、src
        // unit is byte
        off += len;
        retval += len;
        src += len;

        if (retval == n) {
            break; // !!!
        }

        // index and offset
        // page_no + page_offset
        index = off >> PGSHIFT;
        offset = PGMASK(off);
    }

    // printf("write end : \n");

out:
    // // debug
    // printfGreen("write content: %s\n", buf_debug_init);
    // kfree(buf_debug_init);
    // printf("\n");
    sema_signal(&ip->i_read_lock);
    return retval;
}
