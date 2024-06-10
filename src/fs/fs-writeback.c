#include "atomic/spinlock.h"
#include "memory/writeback.h"
#include "common.h"
#include "lib/list.h"
#include "fs/vfs/fs.h"
#include "debug.h"
#include "fs/mpage.h"

extern struct _superblock fat32_sb;

int sync_inode(struct inode *ip) {
    acquire(&ip->i_lock);
    if (ip->i_writeback) {
        release(&ip->i_lock);
        return -1;
    }
    ip->i_writeback = 1;
    release(&ip->i_lock);

    // page write back (all)
    mpage_writepage(ip, 1); // allocate if necessary

    // update fcb in parent
    if (ip->dirty_in_parent) {
        fat32_inode_update(ip);
    }

    return 0;
}

void writeback_inodes(uint64 nr_to_write) {
    // nr_to_write is not important
    struct inode *ip_cur = NULL;
    struct inode *ip_tmp = NULL;

    acquire(&fat32_sb.dirty_lock);
    list_for_each_entry_safe(ip_cur, ip_tmp, &fat32_sb.s_dirty, dirty_list) {
        release(&fat32_sb.dirty_lock);

        sema_wait(&ip_cur->i_sem);// important ??? maybe
        int ret = sync_inode(ip_cur);
        sema_signal(&ip_cur->i_sem);

        acquire(&fat32_sb.dirty_lock);
        if (ret == 0) {
            list_del_reinit(&ip_cur->dirty_list);
            ip_cur->i_writeback = 0;
        }
    }
    release(&fat32_sb.dirty_lock);
}
