#include "common.h"
#include "debug.h"
#include "atomic/spinlock.h"
#include "memory/allocator.h"
#include "proc/pcb_life.h"
#include "fs/bio.h"
#include "fs/stat.h"
#include "fs/vfs/fs.h"
#include "fs/vfs/ops.h"
#include "fs/fat/fat32_stack.h"
#include "fs/fat/fat32_mem.h"
#include "fs/fat/fat32_disk.h"
#include "kernel/trap.h"
#include "lib/ctype.h"
#include "lib/hash.h"
#include "fs/mpage.h"
#include "atomic/ops.h"
#include "memory/filemap.h"
#include "lib/radix-tree.h"
#include "memory/writeback.h"
#include "lib/list.h"
#include "atomic/semaphore.h"

// debug
// int cache_cnt;
// int hit_cnt;
// int dirlookup_cnt;

extern struct file_operations fat32_fop;
extern struct inode_operations fat32_iop;

struct _superblock fat32_sb;

struct inode_table_t {
    spinlock_t lock;
    // struct semaphore lock;
    struct list_head entry;           // free list
    struct inode inode_entry[NINODE]; // array
} inode_table;

// init the global inode table
void inode_table_init() {
    INIT_LIST_HEAD(&inode_table.entry);
    struct inode *entry;
    initlock(&inode_table.lock, "inode_table"); // !!!!
    // sema_init(&inode_table.lock, 1, "inode_table_lock");
    for (entry = inode_table.inode_entry; entry < &inode_table.inode_entry[NINODE]; entry++) {
        memset(entry, 0, sizeof(struct inode));
        sema_init(&entry->i_sem, 1, "inode_entry_sem");
        sema_init(&entry->i_read_lock, 1, "read_lock");
        // sema_init(&entry->i_writeback_lock, 1, "i_writeback_lock");
        initlock(&entry->i_lock, "inode_entry_lock");
        initlock(&entry->tree_lock, "inode_radix_tree_lock");
        INIT_LIST_HEAD(&entry->dirty_list);
        INIT_LIST_HEAD(&entry->list); // !!! to speed up inode_get
        list_add_tail(&entry->list, &inode_table.entry);
    }
    Info("========= Information of inode table ==========\n");
    Info("number of inode : %d\n", NINODE);
    Info("inode table init [ok]\n");
}

// caller must pass a valid pointer !
static uint8 __inode_update_to_fatdev(struct inode *ip) {
    uint8 DIR_Dev;
    uint16 i_rdev = ip->i_rdev;
    // uint16 i_mode = ip->i_mode;
    uint8 type = (ip->i_mode & S_IFMT) >> 8;
    ASSERT(MAJOR(i_rdev) < 4);
    // DIR_Dev: 4 + 2 + 2
    DIR_Dev = type | ((MAJOR(i_rdev) & 0x3) << 2) | (MINOR(i_rdev) & 0x3);
    return DIR_Dev;
}

inline static uint16 __imode_from_fcb(dirent_s_t *pfcb) {
    ASSERT(pfcb);
    uint16 mode;
    if (pfcb->DIR_Dev) {
        mode = FATDEV_TO_ITYPE(pfcb->DIR_Dev);
    } else {
        mode = DIR_BOOL(pfcb->DIR_Attr) ? S_IFDIR : S_IFREG;
    }
    mode |= 0777; // a sloppy handle : make it rwx able
    return mode;
}

inline static int __get_blocks(int size) {
    int q = size / __CLUSTER_SIZE;
    int r = size & (__CLUSTER_SIZE - 1);
    return (q + (r != 0));
}

struct inode *fat32_root_inode_init(struct _superblock *sb) {
    // root inode initialization
    struct inode *root_ip = (struct inode *)kalloc();
    sema_init(&root_ip->i_sem, 1, "fat_root_inode");
    sema_init(&root_ip->i_read_lock, 1, "read_root_inode");
    // sema_init(&root_ip->i_writeback_lock, 1, "writebakc_root_inode");
    root_ip->i_dev = sb->s_dev;
    // root_ip->i_mode = IMODE_NONE;
    // set root inode num to 0 (this is no longer used)
    root_ip->i_ino = ROOT_INO; // offset in parent << 32 | cluster_start!!!
    root_ip->ref = 0;
    root_ip->valid = 1;
    // file size
    root_ip->i_mount = root_ip;
    root_ip->i_sb = sb;
    root_ip->i_nlink = 1;
    root_ip->i_op = get_inodeops[FAT32]();
    root_ip->fs_type = FAT32;

    // the parent of the root is itself
    root_ip->parent = root_ip;
    root_ip->fat32_i.cluster_start = 2;
    root_ip->fat32_i.parent_off = 0;
    root_ip->fat32_i.fname[0] = '/';
    root_ip->fat32_i.fname[1] = '\0';

    // root inode doesn't have these fields, so set these to 0
    root_ip->i_atime = 0;
    root_ip->i_mtime = 0;
    root_ip->i_ctime = 0;

    root_ip->fat32_i.cluster_cnt = fat32_fat_travel(root_ip, 0);
    root_ip->i_size = DIRLENGTH(root_ip);
    root_ip->i_blksize = __get_blocks(root_ip->i_size);
    root_ip->i_sb = sb;
    root_ip->fat32_i.DIR_FileSize = 0;
    // root_ip->i_mode = S_IFDIR | 0666;
    root_ip->i_mode = S_IFDIR | 0777;
    DIR_SET(root_ip->fat32_i.Attr);

    // inode hash table
    root_ip->i_hash = NULL;

    // speed up dirlookup using hint
    root_ip->off_hint = 0;

    // mapping for root
    root_ip->i_mapping = NULL;

    // dirty and dirty_list
    INIT_LIST_HEAD(&root_ip->dirty_list);

    // is dirty in parent ?
    root_ip->dirty_in_parent = 0;

    // for inode create
    root_ip->create_cnt = 0;
    root_ip->create_first = 0;

    initlock(&root_ip->tree_lock, "radix_tree_lock_root");
    ASSERT(root_ip->fat32_i.cluster_start == 2);

    return root_ip;
}

// num is a logical cluster number(within a file)
// start from 1
// num == 0 ~ return count of clusters, set the inode->fat32_i.cluster_end at the same time
// num != 0 ~ return the num'th physical cluster num
uint32 fat32_fat_travel(struct inode *ip, uint num) {
    FAT_entry_t iter_c_n = ip->fat32_i.cluster_start;
    int cnt = 0;
    int prev = 0;
    while (!ISEOF(iter_c_n) && (num > ++cnt || num == 0)) {
        prev = iter_c_n;
        fat32_ctl_index_table(ip, cnt, iter_c_n); // add cluster_num into index table!
        // iter_c_n = fat32_next_cluster(iter_c_n);
        iter_c_n = fat32_fat_cache_get(iter_c_n);
    }

    if (num == 0) {
        ip->fat32_i.cluster_end = prev;
        return cnt;
    } else {
        if (num > cnt)
            return prev;
        return iter_c_n;
    }
}

// find the next cluster of current cluster
uint fat32_next_cluster(uint cluster_cur) {
    // ASSERT(cluster_cur >= 2 && cluster_cur < FAT_CLUSTER_MAX);
    if (!(cluster_cur >= 2 && cluster_cur <= FAT_CLUSTER_MAX)) {
        printfRed("cluster_cur : %d(%x)\n", cluster_cur, cluster_cur);
        panic("fat32_next_cluster, cluster_cur error\n");
    }
    struct buffer_head *bp;
    uint sector_num = ThisFATEntSecNum(cluster_cur);
    bp = bread(fat32_sb.s_dev, sector_num);
    FAT_entry_t fat_next = FAT32ClusEntryVal(bp->data, cluster_cur);
    brelse(bp);
    // if (!(fat_next >= 2 && fat_next < FAT_CLUSTER_MAX) && fat_next != EOC) {
    //     printfRed("fat_next : %d(%x)\n", fat_next, fat_next);
    //     panic("fat_next error\n");
    // }
    return fat_next;
}

// allocate a free cluster
FAT_entry_t fat32_cluster_alloc(uint dev) {
    // sema_wait(&fat32_sb.sem);
    acquire(&fat32_sb.lock);
    if (!fat32_sb.fat32_sb_info.free_count) {
        panic("no disk space!!!\n");
    }
    FAT_entry_t free_num = fat32_sb.fat32_sb_info.nxt_free;
    fat32_sb.fat32_sb_info.free_count--;
    // if(fat32_sb.fat32_sb_info.free_count%1000==0) {
    // printfRed("free num --: %d\n", fat32_sb.fat32_sb_info.free_count);
    // }

    // FAT_entry_t tmp;
    // if((tmp = fat32_next_cluster(fat32_sb.fat32_sb_info.nxt_free))) {

    // }
    int hint;
    FAT_entry_t fat_next;
retry:
    hint = fat32_sb.fat32_sb_info.hint_valid ? fat32_sb.fat32_sb_info.nxt_free : 0;
    // fat_next = fat32_fat_alloc(hint);
    fat_next = fat32_bitmap_alloc(&fat32_sb, hint);
    fat32_sb.fat32_sb_info.nxt_free = fat_next + 1; // !!!
    if (fat32_sb.fat32_sb_info.nxt_free >= FAT_CLUSTER_MAX) {
        fat32_sb.fat32_sb_info.nxt_free = 3;
    }
    fat32_sb.fat32_sb_info.hint_valid = 1; // using hint!
    if (fat_next == 0)
        goto retry;

    free_num = fat_next;                                              // bug !!! 
    // printfRed("fat cluster : %x\n", free_num); // debug!
    // the first sector

    // int first_sector = FirstSectorofCluster(fat32_sb.fat32_sb_info.nxt_free);
    // struct buffer_head* bp = bread(dev, first_sector);
    // if (NAME0_FREE_ALL((bp->data)[0]) && fat32_sb.fat32_sb_info.nxt_free < FAT_CLUSTER_MAX - 1) {
    //     // next free hit
    //     brelse(bp); // !!!!

    //     // FAT_entry_t tmp;
    //     // if((tmp = fat32_next_cluster(fat32_sb.fat32_sb_info.nxt_free))) {
    //     //     // printf("next cluster : %d", tmp);
    //     //     // panic("alloc error\n");
    //     // }
    //     fat32_fat_set(fat32_sb.fat32_sb_info.nxt_free, EOC);
    //     // printfRed("hint : fat cluster : %x, value : %x\n", fat32_sb.fat32_sb_info.nxt_free, fat32_next_cluster(fat32_sb.fat32_sb_info.nxt_free)); // debug!
    //     fat32_sb.fat32_sb_info.nxt_free++;
    // } else {
    //     // start from the begin of fat
    //     brelse(bp); // !!!!
    //     int hint = fat32_sb.fat32_sb_info.hint_valid ? fat32_sb.fat32_sb_info.nxt_free : 0;
    //     uint fat_next = fat32_fat_alloc(hint);
    //     if (fat_next == 0)
    //         panic("no more space");
    //     fat32_sb.fat32_sb_info.nxt_free = (fat_next + 1) % (FAT_CLUSTER_MAX); // !!!
    //     if (fat32_sb.fat32_sb_info.nxt_free == 0) {
    //         fat32_sb.fat32_sb_info.nxt_free = 3;
    //     }
    //     fat32_sb.fat32_sb_info.hint_valid = 1; // using hint!
    //     free_num = fat_next;                   // bug !!!
    // }

    fat32_sb.fat32_sb_info.dirty = 1; // sync in put
    // sema_signal(&fat32_sb.sem);

    fat32_fat_cache_set(free_num, EOC);
    release(&fat32_sb.lock);

    // fat32_fat_set(free_num, EOC);// don't forget it

    // zero cluster (maybe unnecessary)
    // fat32_zero_cluster(free_num);
    return free_num;
}

// it is not useful for comp test
void fat32_update_fsinfo(uint dev) {
    sema_wait(&fat32_sb.sem);
    if (!fat32_sb.fat32_sb_info.dirty) {
        sema_signal(&fat32_sb.sem);
        return;
    }
    // update fsinfo
    struct buffer_head *bp;
    fsinfo_t *fsinfo_tmp;
    bp = bread(dev, SECTOR_FSINFO);
    fsinfo_tmp = (fsinfo_t *)(bp->data);
    fsinfo_tmp->Free_Count = fat32_sb.fat32_sb_info.free_count;
    fsinfo_tmp->Nxt_Free = fat32_sb.fat32_sb_info.nxt_free;
    bwrite(bp);
    brelse(bp);

    // update fsinfo backup
    bp = bread(dev, SECTOR_FSINFO_BACKUP);
    fsinfo_tmp = (fsinfo_t *)(bp->data);
    fsinfo_tmp->Free_Count = fat32_sb.fat32_sb_info.free_count;
    fsinfo_tmp->Nxt_Free = fat32_sb.fat32_sb_info.nxt_free;
    bwrite(bp);
    brelse(bp);

    fat32_sb.fat32_sb_info.dirty = 0; // !!!
    sema_signal(&fat32_sb.sem);
}

// allocate a new fat entry
uint fat32_fat_alloc(FAT_entry_t hint) {
    // using hint speed up
    struct buffer_head *bp;
    int c = hint;
    // cluster 0 and cluster 1 is reserved, cluster 2 is for root
    int s_init = c % FAT_PER_SECTOR;
    int sec = FAT_BASE + c / FAT_PER_SECTOR;
    while (c < FAT_CLUSTER_MAX) {
        bp = bread(fat32_sb.s_dev, sec);
        FAT_entry_t *fats = (FAT_entry_t *)(bp->data);
        for (int s = s_init; s < FAT_PER_SECTOR; s++) {
            if (fats[s] == FREE_MASK) {
                brelse(bp); // !!!!
                fat32_fat_set(c, EOC);
                // printfRed("not hint : fat cluster : %x, value : %x\n", c, fat32_next_cluster(c)); // debug!
                return c;
            }
            c++;
            if (c > FAT_CLUSTER_MAX) {
                brelse(bp);
                return 0;
            }
        }
        s_init = 0;
        sec++;
        brelse(bp);
    }
    return 0;
}

// set fat to value
// NOTE : we don't update fat region 2 (it is unnecessary)
void fat32_fat_set(uint cluster, uint value) {
    if (!(cluster >= 2 && cluster <= FAT_CLUSTER_MAX)) {
        printfRed("cluster : %d(%x)\n", cluster, cluster);
        panic("fat32_fat_set, cluster error\n");
    }
    if (!(value >= 2 && value <= FAT_CLUSTER_MAX) && value != EOC && value != FREE_MASK) {
        printfRed("value : %d(%x)\n", value, value);
        panic("fat32_fat_set, value error\n");
    }

    struct buffer_head *bp;
    uint sector_num = ThisFATEntSecNum(cluster);
    bp = bread(fat32_sb.s_dev, sector_num);
    SetFAT32ClusEntryVal(bp->data, cluster, value);
    bwrite(bp);
    brelse(bp);
}

// move search cursor(<cluster, sector, offset>) given off
uint32 fat32_cursor_to_offset(struct inode *ip, uint off, FAT_entry_t *c_start, int *init_s_n, int *init_s_offset) {
    uint32 C_NUM_off = LOGISTIC_C_NUM(off) + 1;
    // find the target cluster of off
    // *c_start = fat32_fat_travel(ip, C_NUM_off);
    *c_start = fat32_ctl_index_table(ip, C_NUM_off, 0);
    if (ISEOF(*c_start)) {
        *c_start = ip->fat32_i.cluster_end;
    }
    while (C_NUM_off > ip->fat32_i.cluster_cnt) {
        FAT_entry_t fat_new = fat32_cluster_alloc(ROOTDEV);
        // fat32_fat_set(*c_start, fat_new);
        fat32_fat_cache_set(*c_start, fat_new);// using fat table in memory
        *c_start = fat_new;
        ip->fat32_i.cluster_cnt++;
        ip->fat32_i.cluster_end = fat_new;
        fat32_ctl_index_table(ip, ip->fat32_i.cluster_cnt, fat_new); // add fat_new into index table!
    }
    *init_s_n = LOGISTIC_S_NUM(off);
    *init_s_offset = LOGISTIC_S_OFFSET(off);
    return C_NUM_off;
}

// allocate an index page to fill cluster number
uint64 fat32_page_alloc(int n) {
    uint64 idx_page;
    if ((idx_page = (uint64)kzalloc(PGSIZE * n)) == 0) {
        panic("page alloc : no enough memory\n");
    }
    return idx_page;
}

#define INDEX_LOOKUP(p) (p ? p : EOC)
static uint32 get_index_val(uint32 *idx_table, uint32 idx_entry, uint32 cluster_num) {
    int alloc = (cluster_num == 0 ? 0 : 1);
    // it is ok to use direct page
    if (alloc) {
        // allocate it?
        idx_table[idx_entry] = cluster_num;
        return cluster_num;
    } else {
        // lookup it?
        return INDEX_LOOKUP(idx_table[idx_entry]);
    }
}

// logistic_num -> cluster_num
// if cluster_num != 0, create <logistic_num,cluster_num> mapping
// else lookup the cluster_num of l_num
// l_num starts from 1
// if not cached, return EOC
uint32 fat32_ctl_index_table(struct inode *ip, uint32 l_num, uint32 cluster_num) {
    ASSERT(l_num >= 1 && l_num <= MAX_INDEX_3);
    // if(ip->fat32_i.cluster_cnt)
    //     l_num = MIN(l_num, ip->fat32_i.cluster_cnt);// avoid exceed cluster_cnt
    uint32 idx = l_num - 1;
    struct index_table *itp = &ip->i_table;
    if (l_num <= MAX_INDEX_0) {
        // level 0
        return get_index_val(itp->direct, idx, cluster_num);
    } else if (l_num <= MAX_INDEX_1) {
        // level 1
        idx -= MAX_INDEX_0;
        uint64 table_offset = idx / OFFSET_LEVEL_1;
        idx = idx % OFFSET_LEVEL_1;
        uint64 idx_table = itp->indirect_one[table_offset];
        if (idx_table == 0) {
            idx_table = fat32_page_alloc(1); // one page
            itp->indirect_one[table_offset] = idx_table;
        }
        return get_index_val((uint32 *)idx_table, IDX_OFFSET(idx), cluster_num);
    } else {
        // TODO : level 2 and level 3
        panic("lookup not tested\n");
    }
    return 0;
}

// free index table
void fat32_free_index_table(struct inode *ip) {
    struct index_table *itp = &ip->i_table;
    uint32 cluster_cnt = ip->fat32_i.cluster_cnt;
    if (cluster_cnt == 0) {
        // for device file
        // sema_signal(&ip->i_sem);
        return;
    }
    for (int idx = 0; idx < N_DIRECT; idx++) {
        if (!itp->direct[idx])
            break;
        itp->direct[idx] = 0;
    }
    for (int table_offet = 0; table_offet < N_LEVEL_ONE; table_offet++) {
        if (!itp->indirect_one[table_offet])
            break;
        kfree((void *)itp->indirect_one[table_offet]);
        itp->indirect_one[table_offet] = 0;
    }
}

// Read data from fa32 inode.
ssize_t fat32_inode_read(struct inode *ip, int user_dst, uint64 dst, uint off, uint n) {
    // int need_lock = 0;
    // if (ip->locked == 0) {
    //     need_lock = 1;
    //     sema_wait(&ip->i_sem);
    // printfRed("read %s not using lock???\n",ip->fat32_i.fname);
    // }
    int fileSize = ip->i_size;

    // 特判合法
    if (off > fileSize || off + n < off)
        return 0;
    // clip it
    if (off + n > fileSize)
        n = fileSize - off;

    if (n == 0) {
        return 0;
    }

    // init the i_mapping
    if (ip->i_mapping == NULL) {
        fat32_i_mapping_init(ip);
    }

    // using mapping to speed up read
    int ret = do_generic_file_read(ip->i_mapping, user_dst, dst, off, n);

    // if(need_lock) {
    //     sema_signal(&ip->i_sem);
    // sema_signal();
    // }
    return ret;
}

// Write data to fat32 inode
// 写 inode 文件，从偏移量 off 起， 写 src 的 n 个字节的内容
ssize_t fat32_inode_write(struct inode *ip, int user_src, uint64 src, uint off, uint n) {
    // int need_lock = 0;
    // if (ip->i_sem.value == 1) {
    //     need_lock = 1;
    //     sema_wait(&ip->i_sem);
    //     // printfRed("write %s not using lock???\n", ip->fat32_i.fname);
    // }
    int fileSize = ip->i_size;
    if (off + n < off)
        return -1;

    // init the i_mapping
    if (ip->i_mapping == NULL) {
        fat32_i_mapping_init(ip);
    }

    int tot = do_generic_file_write(ip->i_mapping, user_src, src, off, n);
    if (tot == -1) {
        return -1;
    }

    // add it into dirty list !!!
    acquire(&ip->i_sb->dirty_lock);
    if (list_empty(&ip->dirty_list)) {
#ifdef __DEBUG_PAGE_CACHE__
        printfCYAN("file %s is dirty\n", ip->fat32_i.fname);
#endif
        list_add_tail(&ip->dirty_list, &ip->i_sb->s_dirty);
    }
    release(&ip->i_sb->dirty_lock);

    // don't forget it!!!
    if (off + n > fileSize) {
        if (S_ISREG(ip->i_mode))
            ip->i_size = off + tot;
        else
            ip->i_size = CEIL_DIVIDE(off + tot, ip->i_sb->cluster_size) * (ip->i_sb->cluster_size);
        ip->i_blocks = __get_blocks(ip->i_size); // bug!!!
        // fat32_inode_update(ip);
        ip->dirty_in_parent = 1;
#ifdef __DEBUG_PAGE_CACHE__
        printfCYAN("file %s is dirty in parent\n", ip->fat32_i.fname);
#endif
    }
    // if(need_lock) {
    //     sema_signal(&ip->i_sem);
    // }

    return tot;
}

// duplicate
struct inode *fat32_inode_dup(struct inode *ip) {
    // sema_wait(&inode_table.lock);
    acquire(&inode_table.lock);
    ip->ref++;
    // sema_signal(&inode_table.lock);
    release(&inode_table.lock);
    return ip;
}
// TODO():等待合并
// get a inode , move it from disk to memory
struct inode *fat32_inode_get(uint dev, struct inode *dp, const char *name, uint parentoff) {
    // int get_cnt = 0;// debug
    struct inode *ip = NULL, *empty = NULL;
    acquire(&inode_table.lock);
    // sema_wait(&inode_table.lock);

    // Is the fat32 inode already in the table?
    empty = 0;
    // ===== using array =====
    for (ip = inode_table.inode_entry; ip < &inode_table.inode_entry[NINODE]; ip++) {
        // get_cnt++;
        if (ip->ref > 0 && ip->i_nlink == 0) {
            fat32_inode_lock(ip);
            release(&inode_table.lock);
            // sema_signal(&inode_table.lock);
            fat32_inode_trunc(ip);
            fat32_inode_unlock(ip);
            acquire(&inode_table.lock);
            ip->ref = 0;
        }
        if (ip->ref > 0 && ip->i_dev == dev && ip->parent == dp && ip->fat32_i.parent_off == parentoff && ip->i_nlink != 0 && !strcmp(ip->fat32_i.fname, name)) {
            // bug : i_nlink!!
            ip->ref++;
            release(&inode_table.lock);
            // sema_signal(&inode_table.lock);
            // printfMAGENTA("inode get, hit : %d\n", get_cnt);// debug
            return ip;
        }
        // bug !!!
        if (!list_empty(&ip->dirty_list)) {
            continue;
        }
        if (empty == 0 && ip->ref == 0) // Remember empty slot.
            empty = ip;
    }
    // ===== using list =====
    // struct inode* ip_cur = NULL;
    // struct inode* ip_tmp = NULL;
    // list_for_each_entry_safe(ip_cur, ip_tmp, &inode_table.entry, list) {

    //     list_del_reinit(&ip_cur->list);
    // }
    // printfBlue("inode get, not hit : %d\n", get_cnt);// debug
    // Recycle an fat32 entry.
    if (empty == 0) {
        printf("mm : %d\n", get_free_mem());
        panic("fat32_inode_get: no space");
    }

    // init the inode pointer
    ip = empty;
    ip->i_sb = &fat32_sb;
    ip->i_dev = dev;
    // ip->i_ino = inum;
    ip->ref = 1;
    ip->valid = 0;
    ip->fat32_i.parent_off = parentoff; // very important!!!
    ip->i_op = get_inodeops[FAT32]();
    ip->fs_type = FAT32;

    // hash table
    ip->i_hash = NULL;

    // speed up dirlookup using hint
    ip->off_hint = 0;

    if (!list_empty(&ip->dirty_list)) {
        panic("fat32_inode_get : get dirty inode\n");
    }
    // ASSERT(list_empty(&ip->dirty_list));

    // i_mapping set NULL (bug!!)
    ip->i_mapping = NULL;

    ip->i_nlink = 1;

    ip->dirty_in_parent = 0; // !!!

    // full name of file
    safestrcpy(ip->fat32_i.fname, name, strlen(name));

    // for inode create
    ip->create_cnt = 0;
    ip->create_first = 0;

    release(&inode_table.lock);
    // sema_signal(&inode_table.lock);

    // printfGreen("get new, filename : %s\n", ip->fat32_i.fname); // debug
    // printfGreen("mm: %d pages\n", get_free_mem()/4096); 
    return ip;
}

// 往 dp 中写入代表 ip 磁盘块信息的fcb
// caller should hold dp->lock, ip->lock
// 成功返回 0，失败返回 -1
// similar to delete
int fat32_fcb_copy(struct inode *dp, struct inode *ip) {
    // get fcb_char
    int str_len = strlen(ip->fat32_i.fname);
    int off = ip->fat32_i.parent_off * 32;                   // unit of parent_off is 32 bytes
    ASSERT(off > 0);
    int long_dir_len = CEIL_DIVIDE(str_len, FAT_LFN_LENGTH); // 上取整
    int fcb_char_len = (long_dir_len + 1) * sizeof(dirent_l_t);

    uchar *fcb_char = kzalloc(fcb_char_len);
    // read
    int nread = fat32_inode_read(ip->parent, 0, (uint64)fcb_char, off, fcb_char_len);
    ASSERT(nread == fcb_char_len);

    // write
    uint off_write = fat32_dir_fcb_insert_offset(dp, fcb_char_len) * 32; // unit of offset is 32 Bytes
    uint nwrite = fat32_inode_write(dp, 0, (uint64)fcb_char, off_write, fcb_char_len);
    ASSERT(nwrite == fcb_char_len);

    kfree(fcb_char);

    return 0;
}

// 获取fat32 inode的锁 并加载 磁盘中的short dirent to mem
void fat32_inode_lock(struct inode *ip) {
    // static int hit = 0;
    if (ip == 0 || ip->ref < 1) {
        printfRed("ip : %s, ref : %d\n", ip->fat32_i.fname, ip->ref);
        panic("inode lock");
    }
    // Hint ： 如果发现卡住了，很有可能是两次获取同一把锁
    // printf("lock: %d : try to lock %s sem.value = %d\n",++hit, ip->fat32_i.fname, ip->i_sem.value);
    sema_wait(&ip->i_sem);
    // printf("lock: %s locked !! sem.value = %d\n",ip->fat32_i.fname, ip->i_sem.value);

    if (ip->valid == 0) {
        fat32_inode_load_from_disk(ip);
    }
    return;
}

int fat32_inode_load_from_disk(struct inode *ip) {
    uint16 mode;
    // =====using i_mapping_read=====
    uchar bp[32];                          // pay attention to stack overflow!!!
    memset(bp, 0, sizeof(bp));             // !!!
    int off = ip->fat32_i.parent_off * 32; // the size of short entry or long entry is 32B

    if (ip->parent->valid == 0) {
        panic("error");
    }

    // sema_wait(&ip->parent->i_sem);
    int ret = fat32_inode_read(ip->parent, 0, (uint64)bp, off, 32); // read fcb using its parent, rather than itself!!!
    // sema_signal(&ip->parent->i_sem);

    ASSERT(ret == 32);
    dirent_s_t *dirent_s_tmp = (dirent_s_t *)bp;
    // bug like this :     dirent_s_t *dirent_s_tmp = (dirent_s_t *)bp + sector_offset;

    ip->fat32_i.Attr = dirent_s_tmp->DIR_Attr;
    if (ip->fat32_i.Attr == 0) {
        printf("ip type:0x%x name:%s\n", ip->i_mode, ip->fat32_i.fname);
        panic("fat32_inode_lock: no Attr");
    }

    // cluster_start and ino
    ip->fat32_i.cluster_start = DIR_FIRST_CLUS(dirent_s_tmp->DIR_FstClusHI, dirent_s_tmp->DIR_FstClusLO);
    ip->i_ino = UNIQUE_INO(ip->fat32_i.parent_off, ip->fat32_i.cluster_start); // offset in parent << 32 | cluster_start

    if (ip->fat32_i.cluster_start == fat32_sb.fat32_sb_info.root_cluster_s && fat32_namecmp(ip->fat32_i.fname, "/")) {
        return -1;
    }
    // !!!
    if (ip->fat32_i.cluster_start != 0) {
        ip->fat32_i.cluster_cnt = fat32_fat_travel(ip, 0);
        // printfRed("start , filename : %s, cluster cnt : %d \n",ip->fat32_i.fname, ip->fat32_i.cluster_cnt);// debug
    } else if (!S_ISCHR(ip->i_mode) && !S_ISBLK(ip->i_mode)) {
        // printfRed("i_mode = 0x%x\n", ip->i_mode);
        // printfRed("the cluster_start of the file %s is zero\n", ip->fat32_i.fname);
    }

    ip->fat32_i.DIR_CrtTimeTenth = dirent_s_tmp->DIR_CrtTimeTenth;
    ip->fat32_i.DIR_CrtTime = dirent_s_tmp->DIR_CrtTime;
    ip->fat32_i.DIR_CrtDate = dirent_s_tmp->DIR_CrtDate;
    ip->fat32_i.DIR_LstAccDate = dirent_s_tmp->DIR_LstAccDate;
    ip->fat32_i.DIR_WrtTime = dirent_s_tmp->DIR_WrtTime;
    ip->fat32_i.DIR_CrtDate = dirent_s_tmp->DIR_CrtDate;
    ip->fat32_i.DIR_FileSize = dirent_s_tmp->DIR_FileSize;

    // ip->i_mode = FATDEV_TO_ITYPE(dirent_s_tmp->DIR_Dev);
    // ip->i_mode = READONLY_GET(dirent_s_tmp->DIR_Attr);

    ip->i_rdev = FATDEV_TO_IRDEV(dirent_s_tmp->DIR_Dev);
    mode = FATDEV_TO_ITYPE(dirent_s_tmp->DIR_Dev);
    if (S_ISCHR(mode) || S_ISBLK(mode)) { // bug!! 不能用 i_rdev 判断，新创建的设备还没写入设备号！
        // a device file
        ip->i_size = 0;
        ip->i_mode = mode;
        ip->i_blocks = 0;
    } else if (DIR_BOOL(ip->fat32_i.Attr)) {
        // a directory
        ip->i_mode = S_IFDIR;
        ip->i_size = DIRLENGTH(ip);
        ip->i_blocks = __get_blocks(ip->i_size);
    } else { // 暂不支持 FIFO 和 SOCKET 文件
        ip->i_mode = S_IFREG;
        ip->i_size = ip->fat32_i.DIR_FileSize;
        ip->i_blocks = __get_blocks(ip->i_size);
    }
    ip->i_blksize = __CLUSTER_SIZE;
    ip->i_mode |= 0777; // a sloppy handle : make it rwx able
    ip->valid = 1;

    return 1;
}

// 释放fat32 inode的锁
void fat32_inode_unlock(struct inode *ip) {
    // if (ip == 0 || !holdingsleep(&ip->i_sem) || ip->ref < 1)
    if (ip == 0 || ip->ref < 1) {
        printf("ip : %d, ip->ref : %d\n", ip, ip->ref);
        panic("fat32 unlock");
    }
    sema_signal(&ip->i_sem);
    // printf("unlock: %s release !! sem.value = %d\n",ip->fat32_i.fname, ip->i_sem.value);
}

// fat32 inode put : trunc and update
// void fat32_inode_put(struct inode *ip) {
//     // printfGreen("mm: %d pages\n", get_free_mem()/4096);
//     // if (ip->fat32_i.fname[0] != '/') {
//     //     printfMAGENTA("inode_put , name : %s, ref : %d\n", ip->fat32_i.fname, ip->ref);
//     // }
//     // return ;
//     acquire(&inode_table.lock);
//     // debug
//     // if (ip->fat32_i.fname[0] != '/') {
//     //     printfMAGENTA("inode_put , name : %s, ref : %d\n", ip->fat32_i.fname, ip->ref);
//     // }
//     // acquire();
//     // sema_wait(&inode_table.lock);
//     int put_parent = 0;
//     int unlock_parent = 0;
//     int put_self = 0;
//     if (ip->ref == 1) {
//         // if (ip->fat32_i.fname[0] != '/') {
//         //     printfMAGENTA("inode_put , name : %s, ref : %d\n", ip->fat32_i.fname, ip->ref);
//         // }
//         put_self = 1;
//         // printfGreen("end , filename : %s, cluster cnt : %d \n",ip->fat32_i.fname, ip->fat32_i.cluster_cnt);// debug
//         // destory hash table
//         fat32_inode_hash_destroy(ip);

//         // write back dirty pages of inode
//         fat32_i_mapping_writeback(ip);

//         // destory i_mapping
//         fat32_i_mapping_destroy(ip);

//         // special for inode_create
//         if (ip->create_first) {
//             release(&inode_table.lock); // !!! bug for iozone
//             fat32_inode_lock(ip->parent);
//             acquire(&inode_table.lock); // !!! bug for iozone

//             ASSERT(ip->parent->valid);
//             ASSERT(ip->parent->create_cnt);
//             ip->parent->create_cnt--;
//             if (!ip->parent->create_cnt) {
//                 put_parent = 1;
//             }
//             unlock_parent = 1;
//         }

//         // if (ip->fat32_i.fname[0] == 'S') {
//         //     printfRed("inode_put , name : %s, ref : %d\n", ip->fat32_i.fname, ip->ref);
//         // }

//         // ip->valid = 0;
//         // delete file, if it's nlink == 0
//         if (ip->valid && ip->i_nlink == 0) {
//             fat32_inode_lock(ip);
//             release(&inode_table.lock);

//             fat32_inode_trunc(ip);
//             // ip->dirty_in_parent = 1;
//             // fat32_inode_update(ip);

//             fat32_inode_unlock(ip);
//             acquire(&inode_table.lock);
//         } else {
//             // free index table
//             sema_wait(&ip->i_sem);
//             fat32_free_index_table(ip);
//             sema_signal(&ip->i_sem);
//         }
//     }

//     ip->ref--;
//     release(&inode_table.lock);

//     // sema_signal(&inode_table.lock);
//     // special for inode_create
//     if (unlock_parent) {
//         fat32_inode_unlock(ip->parent);
//     }
//     if (put_parent) {
//         fat32_inode_put(ip->parent);
//     }
//     if (put_self) {
//         // update fsinfo (in fact, it is uncessary for comp)
//         fat32_update_fsinfo(ROOTDEV);
//     }
// }

void fat32_inode_put(struct inode *ip) {
    // int put_parent = 0;
    // int unlock_parent = 0;
    acquire(&inode_table.lock);
    if (ip->valid && ip->i_nlink == 0) {
        // sema_wait(&ip->i_writeback_lock);
        // destory hash table
        fat32_inode_hash_destroy(ip);

        // write back dirty pages of inode
        fat32_i_mapping_writeback(ip);
        // list_del_reinit(&ip->dirty_list);// !!!

        // destory i_mapping
        fat32_i_mapping_destroy(ip);
        // sema_signal(&ip->i_writeback_lock);

        // // truncate inode
        // fat32_inode_lock(ip);
        // release(&inode_table.lock);
        // fat32_inode_trunc(ip);

        // fat32_inode_unlock(ip);
        // acquire(&inode_table.lock);
        // ip->ref = 0;
        // printfRed("unlink, filename : %s\n", ip->fat32_i.fname);// debug
        // printfRed("mm: %d pages\n", get_free_mem()/4096);
    }
    release(&inode_table.lock);
}

// unlock and put
void fat32_inode_unlock_put(struct inode *ip) {
    fat32_inode_unlock(ip);
    // printf("lock: %s unlocked !! sem.value = %d\n",ip->fat32_i.fname, ip->i_sem.value);// debug
    fat32_inode_put(ip);
}

// truncate the fat32 inode
void fat32_inode_trunc(struct inode *ip) {
    FAT_entry_t iter_c_n = ip->fat32_i.cluster_start;

    uint32 l_num = 1;
    // truncate the fat chain
    while (!ISEOF(iter_c_n)) {
        // FAT_entry_t fat_next = fat32_next_cluster(iter_c_n);
        // printfGreen("plus : cluster %x ++\n", iter_c_n); // debug!
        FAT_entry_t fat_next = fat32_ctl_index_table(ip, l_num + 1, 0); // lookup;
        // fat32_fat_set(iter_c_n, FREE_MASK);                             // bug like this : fat32_fat_set(iter_c_n, EOC);
        fat32_fat_cache_set(iter_c_n, FREE_MASK);// using fat table in memory
        fat32_bitmap_op(&fat32_sb, iter_c_n, 0);// clear
        fat32_fat_cache_set(iter_c_n, FREE_MASK);// set to FREE
        iter_c_n = fat_next;
        l_num++;
    }
    // free index table
    fat32_free_index_table(ip);

    // necessary!!!
    // sema_wait(&fat32_sb.sem);
    acquire(&fat32_sb.lock);
    fat32_sb.fat32_sb_info.free_count += ip->fat32_i.cluster_cnt;
    // fat32_sb.fat32_sb_info.nxt_free = ip->fat32_i.cluster_start;// !!!???
    // printfGreen("free num ++ : %d\n", fat32_sb.fat32_sb_info.free_count);// debug
    // sema_signal(&fat32_sb.sem);
    release(&fat32_sb.lock);

    ip->fat32_i.cluster_start = 0;
    ip->fat32_i.cluster_end = 0;
    ip->fat32_i.cluster_cnt = 0;
    ip->fat32_i.DIR_FileSize = 0;
    ip->fat32_i.Attr = 0;
    ip->i_rdev = 0;
    ip->i_mode = IMODE_NONE;
    ip->i_size = 0;
    ip->i_blocks = 0;
    ip->fat32_i.parent_off = -1; // ???

    ip->i_hash = NULL;           // !!!

    // speed up dirlookup
    ip->off_hint = 0;
}

// update
void fat32_inode_update(struct inode *ip) {
    if (ip->i_ino == ROOT_INO) {
        return;
    }

    uchar bp[32];                          // pay attention to stack overflow!!!
    memset(bp, 0, sizeof(bp));             // !!!
    int off = ip->fat32_i.parent_off * 32; // the size of short entry or long entry is 32B

    if (ip->parent->valid == 0) {
        panic("error");
    }
    // sema_wait(&ip->parent->i_sem);
    int ret = fat32_inode_read(ip->parent, 0, (uint64)bp, off, 32); // read fcb using its parent, rather than itself!!!
    // sema_signal(&ip->parent->i_sem);

    ASSERT(ret == 32);

    dirent_s_t *dirent_s_tmp = (dirent_s_t *)bp;
    dirent_s_tmp->DIR_Attr = ip->fat32_i.Attr;
    dirent_s_tmp->DIR_LstAccDate = ip->fat32_i.DIR_LstAccDate;
    dirent_s_tmp->DIR_WrtDate = ip->fat32_i.DIR_WrtDate;
    dirent_s_tmp->DIR_WrtTime = ip->fat32_i.DIR_WrtTime;
    dirent_s_tmp->DIR_FstClusHI = DIR_FIRST_HIGH(ip->fat32_i.cluster_start);
    dirent_s_tmp->DIR_FstClusLO = DIR_FIRST_LOW(ip->fat32_i.cluster_start);

    if (S_ISDIR(ip->i_mode) || S_ISCHR(ip->i_mode) || S_ISBLK(ip->i_mode)) {
        dirent_s_tmp->DIR_FileSize = 0;
    } else {
        dirent_s_tmp->DIR_FileSize = ip->i_size;
    }

    dirent_s_tmp->DIR_Dev = __inode_update_to_fatdev(ip);

    if (ip->parent->valid == 0) {
        panic("error");
    }
    // sema_wait(&ip->parent->i_sem);
    fat32_inode_write(ip->parent, 0, (uint64)bp, off, 32);
    // sema_signal(&ip->parent->i_sem);
}

int fat32_filter_longname(dirent_l_t *dirent_l_tmp, char *ret_name) {
    int idx = 0;
    for (int i = 0; i < 5; i++) {
        ret_name[idx++] = LONG_NAME_CHAR_MASK(dirent_l_tmp->LDIR_Name1[i]);
        if (!LONG_NAME_CHAR_VALID(dirent_l_tmp->LDIR_Name1[i]))
            return idx;
    }
    for (int i = 0; i < 6; i++) {
        ret_name[idx++] = LONG_NAME_CHAR_MASK(dirent_l_tmp->LDIR_Name2[i]);
        if (!LONG_NAME_CHAR_VALID(dirent_l_tmp->LDIR_Name2[i]))
            return idx;
    }
    for (int i = 0; i < 2; i++) {
        ret_name[idx++] = LONG_NAME_CHAR_MASK(dirent_l_tmp->LDIR_Name3[i]);
        if (!LONG_NAME_CHAR_VALID(dirent_l_tmp->LDIR_Name3[i]))
            return idx;
    }
    return idx;
}

// pop the stack
ushort fat32_longname_popstack(Stack_t *fcb_stack, uchar *fcb_s_name, char *name_buf) {
    int name_idx = 0;
    ushort long_valid = 0;
    dirent_l_t fcb_l_tmp;
    if (stack_is_empty(fcb_stack)) {
        return 0;
    }
    uchar cnt = 1;
    // reverse the stack to check every long directory entry
    while (!stack_is_empty(fcb_stack)) {
        fcb_l_tmp = stack_pop(fcb_stack);
        if (!stack_is_empty(fcb_stack) && cnt != fcb_l_tmp.LDIR_Ord) {
            return 0;
        } else {
            cnt++;
        }
        uchar checksum = ChkSum(fcb_s_name);
        if (fcb_l_tmp.LDIR_Chksum != checksum) {
            panic("check sum error");
        }
        char l_tmp[14];
        memset(l_tmp, 0, sizeof(l_tmp));
        int l_tmp_len = fat32_filter_longname(&fcb_l_tmp, l_tmp);
        for (int i = 0; i < l_tmp_len; i++) {
            name_buf[name_idx++] = l_tmp[i];
        }
    }
    name_buf[name_idx] = '\0';
    long_valid = LAST_LONG_ENTRY_BOOL(fcb_l_tmp.LDIR_Ord) ? 1 : 0;
    return long_valid;
}

// checksum
uchar ChkSum(uchar *pFcbName) {
    uchar Sum = 0;
    for (short FcbNameLen = 11; FcbNameLen != 0; FcbNameLen--) {
        Sum = ((Sum & 1) ? 0x80 : 0) + (Sum >> 1) + *pFcbName++;
    }
    return Sum;
}

// dirlookup
struct inode *fat32_inode_dirlookup(struct inode *dp, const char *name, uint *poff) {
    // printf("dirlookup: dp %s sem.value %d\n",dp->fat32_i.fname,dp->i_sem.value); //debug
    if (!DIR_BOOL((dp->fat32_i.Attr)))
        panic("dirlookup not DIR");
    struct inode *ip_search = NULL;

    // dirlookup_cnt++; // debug
    if (dp->i_hash == NULL) {
        fat32_inode_hash_init(dp);
    } else {
        //  speed up dirlookfat32_inode_hash_lookupup using hash table
        ip_search = fat32_inode_hash_lookup(dp, name);
        if (ip_search != NULL) {
            // printfRed("cache, %d/%d\n", ++cache_cnt, dirlookup_cnt);// debug
            // printfBlue("hit , name is %s\n",name); // debug
            return ip_search;
        }
    }

    // speed up dirlookup using off hint
    int off_hit = dp->off_hint;
    ip_search = fat32_inode_dirlookup_with_hint(dp, name, poff);
    // printfGreen("off_hit, %d\n", off_hit);
    if (ip_search != NULL) {
        // printfBlue("hit, %d/%d\n", ++hit_cnt, dirlookup_cnt);
        return ip_search;
    } else {
        if (off_hit != 0) {
            // printfBlue("hit, %d/%d, off_hit, %d\n", ++hit_cnt, dirlookup_cnt, off_hit);
        }
        return 0;
    }
}

// dirlookup optimization (dirlookup with hint)
struct inode *fat32_inode_dirlookup_with_hint(struct inode *dp, const char *name, uint *poff) {
    struct trav_control tc;
    tc.start_off = dp->off_hint * 32;
    tc.end_off = dp->i_size;
    tc.kbuf = NULL;
    tc.ops = DIRLOOKUP_OP;
    safestrcpy(tc.name_search, name, strlen(name));
    tc.poff = poff;
    tc.retval = NULL;
    fat32_inode_general_trav(dp, &tc, fat32_inode_travel_fcb_handler);
    return tc.retval;
}

// return a pointer to a new inode with lock on
// no need to lock dp before call this func
struct inode *fat32_inode_create(struct inode *dp, const char *name, uint16 type, short major, short minor) {
    struct inode *ip = NULL;

    ASSERT(dp);
    fat32_inode_lock(dp);
    // have existed?
    if ((ip = fat32_inode_dirlookup(dp, name, 0)) != 0) {
        // fat32_inode_unlock_put(dp);
        fat32_inode_unlock(dp);
        fat32_inode_lock(ip);

        if ((type == (ip->i_mode & S_IFMT)) || (ip->shm_flg)) {
            dp->create_cnt++;
            ip->create_first = 1;
            return ip;
        }
        fat32_inode_unlock_put(ip);

        return 0;
    }

    // haven't exited
    if ((ip = fat32_inode_alloc(dp, name, type)) == 0) {
        fat32_inode_unlock_put(dp);
        return 0;
    }

    fat32_inode_lock(ip);
    // ip->i_nlink = 1;
    // ip->i_mode = type;   // type is only type!!
    if (S_ISCHR(type) || S_ISBLK(type)) {
        ip->i_rdev = mkrdev(major, minor);
    } else {
        ip->i_rdev = 0;
    }
    fat32_inode_update(ip); // !!!! very important
    // bug like this :  ip->dirty_in_parent = 1;

    // if (type == S_IFDIR) { // Create . and .. entries.
    if (S_ISDIR(type)) { // Create . and .. entries.
        // // No ip->nlink++ for ".": avoid cyclic ref count.
        // if (fat32_inode_dirlink(ip, ".") < 0 || fat32_inode_dirlink(ip, "..") < 0)
        //     goto fail;
        // TODO : dirlink

        // direntory . and .. , write them to the disk
        uchar fcb_dot_char[64];
        memset(fcb_dot_char, 0, sizeof(fcb_dot_char));
        fat32_fcb_init(ip, (const uchar *)".", S_IFDIR, (char *)fcb_dot_char);
        uint tot = fat32_inode_write(ip, 0, (uint64)fcb_dot_char, 0, 32);
        ASSERT(tot == 32);

        uchar fcb_dotdot_char[64];
        memset(fcb_dotdot_char, 0, sizeof(fcb_dotdot_char));
        fat32_fcb_init(ip, (const uchar *)"..", S_IFDIR, (char *)fcb_dotdot_char);
        tot = fat32_inode_write(ip, 0, (uint64)fcb_dotdot_char, 32, 32);
        ASSERT(tot == 32);
    }

    // fat32_inode_update(dp);
    // fat32_inode_unlock_put(dp);
    fat32_inode_unlock(dp);
    dp->create_cnt++;
    ip->create_first = 1;

    return ip;
}

// allocate a new inode
struct inode *fat32_inode_alloc(struct inode *dp, const char *name, uint16 type) {
    uchar *fcb_char = kzalloc(FCB_MAX_LENGTH);
    // printfMAGENTA("fcb_char, mm-- : %d pages\n", get_free_mem() / 4096);

    int fcb_cnt = fat32_fcb_init(dp, (const uchar *)name, type, (char *)fcb_char);
    uint offset = fat32_dir_fcb_insert_offset(dp, fcb_cnt); // unit is 32Bytes
    uint tot = fat32_inode_write(dp, 0, (uint64)fcb_char, offset * sizeof(dirent_l_t), fcb_cnt * sizeof(dirent_l_t));
    kfree(fcb_char);

    ASSERT(tot == fcb_cnt * sizeof(dirent_l_t));

    struct inode *ip_new;

    uint off = offset + fcb_cnt - 1;

    // speed up dirlookup using hash table
    if (dp->i_hash == NULL) {
        fat32_inode_hash_init(dp);
    }

    int ret = fat32_inode_hash_insert(dp, name, NULL, off);
    if (ret == 0) {
        // printfGreen("alloc : insert : %s\n", name); //debug
    }

    ip_new = fat32_inode_get(dp->i_dev, dp, name, off);
    ip_new->parent = dp;
    dp->off_hint = off + 1; // don't use off, but the next one

#ifdef __DEBUG_INODE__
    printfRed("inode alloc : pid %d, filename : %s, off : %d (%x)\n", proc_current()->pid, ip_new->fat32_i.fname, off, off);
#endif
    return ip_new;
}

// fcb init
// 为 long_name 初始化若干个 dirent_l_t 和一个dirent_s_t，写入 fcb_char中，返回需要的 fcb 总数
int fat32_fcb_init(struct inode *ip_parent, const uchar *long_name, uint16 type, char *fcb_char) {
    uchar attr;
    dirent_s_t dirent_s_cur;
    memset((void *)&dirent_s_cur, 0, sizeof(dirent_s_cur));

    if (S_ISDIR(type))
        DIR_SET(attr);
    else
        NONE_DIR_SET(attr);

    dirent_s_cur.DIR_Attr = attr;
    dirent_s_cur.DIR_Dev = ITYPE_TO_FATDEV(type);
    uint long_idx = -1;
    // . and ..
    if (!fat32_namecmp((const char *)long_name, ".") || !fat32_namecmp((const char *)long_name, "..")) {
        strncpy((char *)dirent_s_cur.DIR_Name, (const char *)long_name, strlen((const char *)long_name));
        dirent_s_cur.DIR_FileSize = 0;
        dirent_s_cur.DIR_FstClusHI = DIR_FIRST_HIGH(ip_parent->fat32_i.cluster_start);
        dirent_s_cur.DIR_FstClusLO = DIR_FIRST_LOW(ip_parent->fat32_i.cluster_start);
        memmove(fcb_char, &dirent_s_cur, sizeof(dirent_s_cur));
        return 1;
    }

    int ret_cnt = 0;
    char file_name[NAME_LONG_MAX];
    char file_ext[4];
    /*short dirent*/
    int name_len = strlen((const char *)long_name);

    // 数据文件
    if (!DIR_BOOL(attr)) {
        if (str_split((const char *)long_name, '.', file_name, file_ext) == -1) {
            // printfRed("it is a file without extname\n");
            strncpy(file_name, (char *)long_name, 8);
            dirent_s_cur.DIR_Name[8] = 0x20;
            dirent_s_cur.DIR_Name[9] = 0x20;
            dirent_s_cur.DIR_Name[10] = 0x20;
        } else {
            str_toupper(file_ext);
            strncpy((char *)dirent_s_cur.DIR_Name + 8, file_ext, 3); // extend name
            if (strlen(file_ext) == 2) {
                dirent_s_cur.DIR_Name[10] = 0x20;
            }
            if (strlen(file_ext) == 1) {
                dirent_s_cur.DIR_Name[10] = 0x20;
                dirent_s_cur.DIR_Name[9] = 0x20;
            }
        }

        str_toupper(file_name);
        strncpy((char *)dirent_s_cur.DIR_Name, (char *)file_name, 8);
        if (strlen((char *)long_name) > 8) {
            long_idx = fat32_find_same_name_cnt(ip_parent, file_name);
            dirent_s_cur.DIR_Name[6] = '~';
            dirent_s_cur.DIR_Name[7] = long_idx + 0x31;
        }
    } else {
        // 目录文件
        strncpy(file_name, (char *)long_name, 11);

        str_toupper(file_name);
        strncpy((char *)dirent_s_cur.DIR_Name, file_name, 8);
        if (strlen((const char *)long_name) > 8) {
            long_idx = fat32_find_same_name_cnt(ip_parent, file_name);

            // strncpy(dirent_s_cur.DIR_Name + 8, file_name + (name_len - 3), 3); // last three char
            dirent_s_cur.DIR_Name[6] = '~';
            dirent_s_cur.DIR_Name[7] = long_idx + 0x31;
            memset(dirent_s_cur.DIR_Name + 8, 0x20, 3);
        }
    }
    dirent_s_cur.DIR_FileSize = 0;

    uint first_c;
    if (S_ISCHR(type) || S_ISBLK(type)) {
        // 设备文件不需要分配磁盘块
        first_c = 0; // for debug; should be zero
    } else {
        first_c = fat32_cluster_alloc(ip_parent->i_dev);
    }

    dirent_s_cur.DIR_FstClusHI = DIR_FIRST_HIGH(first_c);
    dirent_s_cur.DIR_FstClusLO = DIR_FIRST_LOW(first_c);

    /*push long dirent into stack*/
    Stack_t fcb_stack;
    stack_init(&fcb_stack);
    uchar checksum = ChkSum(dirent_s_cur.DIR_Name);
#ifdef __DEBUG_INODE__
    printfGreen("inode init : pid %d, %s, checksum = %x \n", proc_current()->pid, long_name, checksum);
#endif
    int char_idx = 0;
    // every long name entry
    int ord_max = CEIL_DIVIDE(name_len, FAT_LFN_LENGTH);
    for (int i = 1; i <= ord_max; i++) {
        dirent_l_t dirent_l_cur;
        memset((void *)&(dirent_l_cur), 0xFF, sizeof(dirent_l_cur));
        if (char_idx == name_len)
            break;

        // order
        if (i == ord_max)
            dirent_l_cur.LDIR_Ord = LAST_LONG_ENTRY_SET(i);
        else
            dirent_l_cur.LDIR_Ord = i;

        // Name
        int end_flag = 0;
        for (int i = 0; i < 5 && !end_flag; i++) {
            dirent_l_cur.LDIR_Name1[i] = LONG_NAME_CHAR_SET(long_name[char_idx]);
            if (!LONG_NAME_CHAR_VALID(long_name[char_idx]))
                end_flag = 1;
            char_idx++;
        }
        for (int i = 0; i < 6 && !end_flag; i++) {
            dirent_l_cur.LDIR_Name2[i] = LONG_NAME_CHAR_SET(long_name[char_idx]);
            if (!LONG_NAME_CHAR_VALID(long_name[char_idx]))
                end_flag = 1;
            char_idx++;
        }
        for (int i = 0; i < 2 && !end_flag; i++) {
            dirent_l_cur.LDIR_Name3[i] = LONG_NAME_CHAR_SET(long_name[char_idx]);
            if (!LONG_NAME_CHAR_VALID(long_name[char_idx]))
                end_flag = 1;
            char_idx++;
        }

        // Attr  and  Type
        dirent_l_cur.LDIR_Attr = ATTR_LONG_NAME;
        dirent_l_cur.LDIR_Type = 0;

        // check sum
        dirent_l_cur.LDIR_Chksum = checksum;

        // must set to zero
        dirent_l_cur.LDIR_Nlinks = 0;
        stack_push(&fcb_stack, dirent_l_cur);
    }

    // pop the stack
    dirent_l_t fcb_l_tmp;
    while (!stack_is_empty(&fcb_stack)) {
        fcb_l_tmp = stack_pop(&fcb_stack);
        memmove(fcb_char, &fcb_l_tmp, sizeof(fcb_l_tmp));
        fcb_char = fcb_char + sizeof(fcb_l_tmp);
        ret_cnt++;
    }
    // the first long dirent
    fcb_l_tmp.LDIR_Nlinks = 1;
    memmove(fcb_char, &dirent_s_cur, sizeof(dirent_s_cur));
    stack_free(&fcb_stack);

    return ret_cnt + 1;
    // TODO: 获取当前时间和日期，还有TimeTenth
}

// find the same prefix and same extend name !!!
int fat32_find_same_name_cnt(struct inode *dp, char *name) {
    struct trav_control tc;
    tc.start_off = 0;
    tc.end_off = dp->i_size;
    tc.kbuf = NULL;
    safestrcpy(tc.name_search, name, strlen(name));
    int cnt = 0;
    tc.retval = (void *)&cnt;
    fat32_inode_general_trav(dp, &tc, fat32_find_same_name_cnt_handler);
    return cnt;
}

// 获取fcb的插入位置(可以插入到碎片中)
// 在目录节点中找到能插入 fcb_cnt_req 个 fcb 的启始偏移位置，并返回它
int fat32_dir_fcb_insert_offset(struct inode *dp, uchar fcb_cnt_req) {
    struct trav_control tc;
    tc.start_off = 0;
    tc.end_off = dp->i_size;
    tc.kbuf = NULL;
    uint offset_ret_final = 0;
    tc.retval = (void *)&offset_ret_final;
    tc.fcb_cnt_req = fcb_cnt_req;                         // don't forget it
    tc.offset_ret_base = dp->i_size / sizeof(dirent_l_t); // we use page cache now, we can't write according to cluster_cnt
    // the data in page cache doesn't write back timely, so useing i_size is OK
    // bug like this: tc.offset_ret_base = DIRLENGTH(dp) / sizeof(dirent_l_t);

    tc.fcb_free_cnt = 0;

    fat32_inode_general_trav(dp, &tc, fat32_dir_fcb_insert_offset_handler);
    return offset_ret_final;
}

// 一个目录除了 .. 和 . 是否为空？
int fat32_isdirempty(struct inode *dp) {
    struct trav_control tc;
    tc.start_off = 0;
    tc.end_off = dp->i_size;
    tc.kbuf = NULL;
    int judge = 1;
    tc.retval = (void *)&judge;
    fat32_inode_general_trav(dp, &tc, fat32_isdirempty_handler);
    return judge;
}

// information of fat32 inode
void fat32_inode_stati(struct inode *ip, struct kstat *st) {
    // ASSERT(ip && st);
    st->st_atime_sec = ip->i_atime;
    st->st_atime_nsec = ip->i_atime * 1000000000;
    st->st_blksize = ip->i_blksize;
    st->st_blocks = ip->i_blocks;
    st->st_ctime_sec = ip->i_ctime;
    st->st_ctime_nsec = ip->i_ctime * 1000000000;
    st->st_dev = ip->i_dev;
    st->st_gid = ip->i_gid;
    st->st_ino = ip->i_ino;
    st->st_mode = ip->i_mode;
    st->st_mtime_sec = ip->i_mtime;
    st->st_mtime_nsec = ip->i_mtime * 1000000000;
    st->st_nlink = ip->i_nlink;
    st->st_rdev = ip->i_rdev;
    st->st_size = ip->i_size;
    st->st_uid = ip->i_uid;
    // ip->fat32_i.cluster_cnt;
    return;
}

// delete inode given its parent dp
// 将 dp 中代表 ip 的 fcb删除
// 成功返回 0，失败返回 -1
int fat32_fcb_delete(struct inode *dp, struct inode *ip) {
    int str_len = strlen(ip->fat32_i.fname);
    int off = ip->fat32_i.parent_off;
    ASSERT(off > 0);
    int long_dir_len = CEIL_DIVIDE(str_len, FAT_LFN_LENGTH); // 上取整
    char fcb_char[FCB_MAX_LENGTH];
    memset(fcb_char, 0, sizeof(fcb_char));
    for (int i = 0; i < long_dir_len + 1; i++)
        fcb_char[i * 32] = 0xE5;
    // ASSERT(off - long_dir_len > 0);
    ASSERT(off - long_dir_len >= 0); // !!! bug , off-long_dir_len should >=0
#ifdef __DEBUG_INODE__
    printf("inode delete : pid %d, %s, off = %d (%x), long_dir_len = %d\n", proc_current()->pid, ip->fat32_i.fname, off, off, long_dir_len);
#endif
    uint tot = fat32_inode_write(dp, 0, (uint64)fcb_char, (off - long_dir_len) * sizeof(dirent_s_t), (long_dir_len + 1) * sizeof(dirent_s_t));
    ASSERT(tot == (long_dir_len + 1) * sizeof(dirent_l_t));

    hash_delete(dp->i_hash, (void *)ip->fat32_i.fname, 0, 1); // not holding lock, release it
    return 0;
}

void fat32_short_name_parser(dirent_s_t dirent_s, char *name_buf) {
    int len_name = 0;
    for (int i = 0; i < 8; i++) {
        if (dirent_s.DIR_Name[i] != 0x20) {
            name_buf[len_name++] = dirent_s.DIR_Name[i];
        }
    }
    if (dirent_s.DIR_Name[8] != 0x20) {
        name_buf[len_name++] = '.';
        for (int i = 8; i < 11; i++) {
            if (dirent_s.DIR_Name[i] != 0x20) {
                name_buf[len_name++] = dirent_s.DIR_Name[i];
            }
        }
    }
    name_buf[len_name] = '\0';
}

void fat32_inode_hash_init(struct inode *dp) {
    dp->i_hash = (struct hash_table *)kmalloc(sizeof(struct hash_table));
    // printfMAGENTA("fat32_inode_hash_init, mm-- : %d pages\n", get_free_mem() / PGSIZE);
    if (dp->i_hash == NULL) {
        panic("hash table init : no free space\n");
    }
    dp->i_hash->lock = INIT_SPINLOCK(inode_hash_table);
    dp->i_hash->type = INODE_MAP;
    dp->i_hash->size = NINODE;
    hash_table_entry_init(dp->i_hash);
}

// int fat32_inode_hash_insert(struct inode *dp, const char *name, uint ino, uint off) {
int fat32_inode_hash_insert(struct inode *dp, const char *name, struct inode *ip, uint off) {
    // don't use fat32_inode_hash_lookup!!!
    if (hash_lookup(dp->i_hash, (void *)name, NULL, 1, 0) != NULL) { // release it, not holding lock
        return -1;                                                   //!!!
    }
    struct inode_cache *c = (struct inode_cache *)kmalloc(sizeof(struct inode_cache));

    // printfMAGENTA("fat32_inode_hash_insert, mm --: %d pages\n", get_free_mem() / PGSIZE);

    if (c == NULL) {
        panic("fat32_inode_hash_insert : no free space\n");
    }
    // c->ino = ino;
    c->ip = ip;
    c->off = off;

    hash_insert(dp->i_hash, (void *)name, (void *)c, 0); // not holding it
    return 0;
}

// using hash table to speed up dirlookup
struct inode *fat32_inode_hash_lookup(struct inode *dp, const char *name) {
    struct hash_node *node = hash_lookup(dp->i_hash, (void *)name, NULL, 0, 0);
    // not release it(must holding lock!!!!)
    // not holding lock

    if (node != NULL) {
        // find it
        struct inode *ip_search = NULL;
        struct inode_cache *cache = (struct inode_cache *)(node->value);
        // int ino = cache->ino;
        // struct inode *ip = cache->ip;
        int off = cache->off;

        release(&dp->i_hash->lock);
        if (cache != NULL) {
            // printfBlue("hit : name %s, ino, %d, off, %x\n", name ,cache->ino, cache->off);
            ip_search = fat32_inode_get(dp->i_dev, dp, name, off);
            ip_search->parent = dp;
        }

        return ip_search;
    }

    release(&dp->i_hash->lock); // !!!

    return NULL;
}

void fat32_inode_hash_destroy(struct inode *ip) {
    if (ip->i_hash != NULL) {
        hash_destroy(ip->i_hash, 1); // free it
        ip->i_hash = NULL;           // !!!

        // speed up dirlookup using hit
        ip->off_hint = 0;
    }
}

// similar to inode_read
// we need to fill the bio using off and n
// The unit of off is byte
// The unit of n is byte
int fat32_get_block(struct inode *ip, struct bio *bio_p, uint off, uint n, int alloc) {
    // need alignment
    ASSERT(!PGMASK(off));
    // suppose it is an integer multiple of BSIZE
    ASSERT(n % __BPB_BytsPerSec == 0);
    ASSERT(n > 0);
    ASSERT(off >= 0);
    ASSERT(list_empty(&bio_p->list_entry));

    FAT_entry_t iter_c_n;
    int init_s_n;
    int init_s_offset;
    // move cursor
    uint32 l_num = fat32_cursor_to_offset(ip, off, &iter_c_n, &init_s_n, &init_s_offset); // get logistic_num. start from 1
    ASSERT(init_s_offset == 0);
    // The unit of cur_s_n and tot_s_n is sector(512 B, if the size of block is 512B)
    uint cur_s_n = 0;
    uint tot_s_n = CEIL_DIVIDE(n, __BPB_BytsPerSec);
    uint b_per_c_n = ip->i_sb->sectors_per_block;

    struct bio_vec *vec_cur = NULL;
    while (!ISEOF(iter_c_n) && cur_s_n < tot_s_n) {
        int first_sector = FirstSectorofCluster(iter_c_n);

        if (vec_cur == NULL || !CLUSTER_ADJACENT(vec_cur, first_sector)) {
            if ((vec_cur = (struct bio_vec *)kzalloc(sizeof(struct bio_vec))) == NULL) {
                panic("fat_get_block : no free memory\n");
            }
            // printfMAGENTA("fat32_get_block: bio_vec alloc, mm-- : %d pages\n", get_free_mem() / 4096);

            sema_init(&vec_cur->sem_disk_done, 0, "bio_disk_done"); // !!! don't forget it
            INIT_LIST_HEAD(&vec_cur->list);                         // !!! don't forget it
            list_add_tail(&vec_cur->list, &bio_p->list_entry);
            // bug like this :  list_add_tail(&bio_p->list_entry, &vec_cur->list);
        }

        // m = MIN(BSIZE - init_s_offset, n - tot);
        uint blocks_n = MIN(b_per_c_n - init_s_n, tot_s_n - cur_s_n);
        if (vec_cur->blockno_start == 0) {
            vec_cur->blockno_start = first_sector + init_s_n;
            vec_cur->block_len = blocks_n;
        } else {
            vec_cur->block_len += blocks_n;
        }

        cur_s_n += blocks_n;

        if (cur_s_n == tot_s_n)
            break; // don't forget it

        init_s_n = 0;
        init_s_offset = 0;

        if (alloc) {
            // write it, we need to append new cluster if necessary
            // FAT_entry_t next = fat32_next_cluster(iter_c_n);
            FAT_entry_t next = fat32_ctl_index_table(ip, l_num + 1, 0); // lookup
            if (ISEOF(next)) {
                FAT_entry_t fat_new = fat32_cluster_alloc(ROOTDEV);
                // fat32_fat_set(iter_c_n, fat_new);
                fat32_fat_cache_set(iter_c_n, fat_new);
                // printfRed("fat cluster : %x, value : %x\n",iter_c_n, fat32_next_cluster(iter_c_n));// debug!!
                iter_c_n = fat_new;
                ip->fat32_i.cluster_cnt++;
                ip->fat32_i.cluster_end = fat_new;
                fat32_ctl_index_table(ip, ip->fat32_i.cluster_cnt, fat_new); // add fat_new into index table!
            } else {
                iter_c_n = next;
            }
        } else {
            // FAT_entry_t tmp = iter_c_n;// debug
            // iter_c_n = fat32_next_cluster(tmp);// debug
            // read it, we don't need to append new cluster
            // iter_c_n = fat32_next_cluster(iter_c_n);
            iter_c_n = fat32_ctl_index_table(ip, l_num + 1, 0); // lookup
        }
        l_num++;                                                // !!!
    }
    // bio_print(bio_p); // debug
#ifdef __DEBUG_PAGE_CACHE__
    bio_print(bio_p); // debug
#endif
    return cur_s_n;
}

// destory
// similar to mpage_writepage
void fat32_i_mapping_destroy(struct inode *ip) {
    struct address_space *mapping = ip->i_mapping;
    acquire(&ip->tree_lock);     // !!!
    if (mapping == NULL) {
        release(&ip->tree_lock); // !!!
        return;
    }

    struct radix_tree_node *node;
    node = mapping->page_tree.rnode;
    if (!node) {
        release(&ip->tree_lock); // !!!
        return;
    }
    if (!radix_tree_is_indirect_ptr(node)) {
        kfree((void *)page_to_pa((struct page *)node));
    } else {
        node = radix_tree_indirect_to_ptr(node);
#ifdef __DEBUG_PAGE_CACHE__
        printfBlue("radix tree free, file : %s, \n", ip->fat32_i.fname);
#endif
        radix_tree_free_whole_tree(node, mapping->page_tree.height, 1);
    }

    kfree(mapping);
    ip->i_mapping = NULL;
    // printfGreen("fat32_i_mapping_destroy, mm ++: %d pages\n", get_free_mem() / 4096);

    release(&ip->tree_lock); // !!!

#ifdef __DEBUG_PAGE_CACHE__
    printfBlue("i_mapping destory , file : %s \n", ip->fat32_i.fname);
#endif
}

// init i_mapping
void fat32_i_mapping_init(struct inode *ip) {
    struct address_space *mapping = kzalloc(sizeof(struct address_space));
    // printfMAGENTA("fat32_i_mapping_init, mm-- : %d pages\n", get_free_mem() / 4096);
    ip->i_mapping = mapping;
    mapping->host = ip; // !!!
    mapping->nrpages = 0;
    INIT_RADIX_TREE(&mapping->page_tree, GFP_FS);
    mapping->last_index = 0;
    mapping->read_ahead_cnt = 0;
    mapping->read_ahead_end = 0;

#ifdef __DEBUG_PAGE_CACHE__
    printfCYAN("fat32_i_mapping_init, file : %s\n", ip->fat32_i.fname);
#endif
}

void fat32_i_mapping_writeback(struct inode *ip) {
    // atomic !!!
    // sema_wait(&ip->i_sem); // !!!! bug , must acquire this lock
    if (!list_empty_atomic(&ip->dirty_list, &ip->i_lock)) {
        // release(&inode_table.lock);


        release(&inode_table.lock);
        int ret = sync_inode(ip);
        acquire(&inode_table.lock);

        // remove inode from dity list
        // acquire(&ip->i_sb->dirty_lock);
        if (ret == 0) {
            list_del_reinit(&ip->dirty_list);
            ip->i_writeback = 0;
#ifdef __DEBUG_PAGE_CACHE__
            printfCYAN("file %s has written back\n", ip->fat32_i.fname);
#endif
        }
        // release(&ip->i_sb->dirty_lock);
    }
    // sema_signal(&ip->i_sem);
}

// do_general_travel
void fat32_inode_general_trav(struct inode *dp, struct trav_control *tc, trav_handler fn) {
    // must a directory!!!
    if (!DIR_BOOL((dp->fat32_i.Attr)))
        panic("not DIR\n");
    tc->dp = dp;

    // name_buf and fcb_stack only for dirlookup and getdents
    if (tc->ops == DIRLOOKUP_OP || tc->ops == GETDENTS_OP) {
        Stack_t fcb_stack;
        char name_buf[NAME_LONG_MAX];
        memset(name_buf, 0, sizeof(name_buf));
        stack_init(&fcb_stack);
        tc->fcb_stack = &fcb_stack;
        tc->name_buf = name_buf;
    }

    // off and sz
    int start_off = tc->start_off;
    int end_off = tc->end_off;
    int sz = end_off - start_off;

    int free_flag = 0;
    // read all blocks of this file
    if (tc->kbuf == NULL) {
        char *kbuf;
        if ((kbuf = kzalloc(sz)) == 0) {
            panic("fat32_inode_general_trav : no enough memory\n");
        }
        // printfMAGENTA("tc->kbuf, mm-- : %d pages\n", get_free_mem() / 4096);
        tc->kbuf = kbuf;
        free_flag = 1;
    }
    fat32_inode_read(dp, 0, (uint64)tc->kbuf, start_off, sz);

    // find it or stop
    tc->stop = 0;

    // check every fcb
    for (tc->idx = 0; start_off + tc->idx * 32 < end_off; tc->idx++) {
        tc->off = ((tc->start_off) >> 5) + (tc->idx); // start_off/32 + idx
        fn(tc);
        if (tc->stop) {
            break;
        }
    }

    // don't forget kfree
    if (free_flag) {
        kfree(tc->kbuf);
        // printfGreen("fat32_inode_general_trav : kbuf, mm ++: %d pages\n", get_free_mem() / PGSIZE);
    }

    // over
    if (tc->ops == DIRLOOKUP_OP || tc->ops == GETDENTS_OP) {
        stack_free(tc->fcb_stack);
        if (!tc->stop) {
            tc->retval = NULL;
            tc->dp->off_hint = 0;
        }
    }

    return;
}

// TODO : change these functions to staic inline!!!
// for dirlookup and getdents
// think short entry and long entry as fcb
// 0 : sucess
// -1 : fail
void fat32_inode_travel_fcb_handler(struct trav_control *tc) {
    dirent_s_t *fcb_s = (dirent_s_t *)(tc->kbuf) + tc->idx;
    dirent_l_t *fcb_l = (dirent_l_t *)(tc->kbuf) + tc->idx;
    tc->fcb_s = *fcb_s;
    // bug like this :     dirent_l_t *fcb_l = (dirent_l_t *)(tc->kbuf + tc->idx);
    // all free, return directly
    if (NAME0_FREE_ALL(fcb_s->DIR_Name[0])) {
        tc->stop = 1;
        return;
    }
    // only this fcb is free
    if (NAME0_FREE_ONLY(fcb_s->DIR_Name[0])) {
        if (!stack_is_empty(tc->fcb_stack)) {
            panic("there is something wrong with disk\n");
        }
        return;
    }
    // long_entry
    if (LONG_NAME_BOOL(fcb_l->LDIR_Attr)) {
        stack_push(tc->fcb_stack, *fcb_l);
    }
    // short entry
    else {
        // pop long entry stack to get full name of file
        memset(tc->name_buf, 0, sizeof(tc->name_buf));
        ushort long_valid = fat32_longname_popstack(tc->fcb_stack, fcb_s->DIR_Name, tc->name_buf);
        // if the long directory is invalid, we use name of short entry
        if (!long_valid) {
            fat32_short_name_parser(*fcb_s, tc->name_buf);
        }
        // insert cache into the hash table (except . and ..)
        if (tc->name_buf[0] != '.') {
            int ret = fat32_inode_hash_insert(tc->dp, tc->name_buf, NULL, tc->off); // only idx is changing!!!
            if (ret == 0) {
                // printfGreen("dirlookup : insert : %s, looking for %s\n", name_buf, name); //debug
            } else if (ret == -1) {
                // printfGreen("dirlookup : insert , name %s has existed, looking for %s\n", name_buf, name); //debug
            }
        }
        // ino
        uint32 cluster_start = DIR_FIRST_CLUS(fcb_s->DIR_FstClusHI, fcb_s->DIR_FstClusLO);
        tc->i_ino = UNIQUE_INO(tc->off, cluster_start);
        // apply operations
        switch (tc->ops) {
        case DIRLOOKUP_OP:
            fat32_inode_dirlookup_handler(tc);
            break;
        case GETDENTS_OP:
            fat32_inode_getdents_handler(tc);
            break;
        default:
            panic("fat32_inode_travel_using_stack : error\n");
        }
    }
    return;
}

// for dirlookup
void fat32_inode_dirlookup_handler(struct trav_control *tc) {
    //  search for?
    if (fat32_namecmp(tc->name_search, tc->name_buf) == 0) {
        // inode matches path element
        if (tc->poff)
            *tc->poff = tc->off;

        struct inode *ip_search;
        ip_search = fat32_inode_get(tc->dp->i_dev, tc->dp, tc->name_search, tc->off);
        ip_search->parent = tc->dp;

        // save hint
        tc->dp->off_hint = tc->off + 1;

        // it is time to return
        tc->retval = ip_search;
        tc->stop = 1;
    }
}

// for getdents
void fat32_inode_getdents_handler(struct trav_control *tc) {
    char buf_tmp[NAME_LONG_MAX + 30];
    struct __dirent *dirent_buf = (struct __dirent *)buf_tmp;

    dirent_buf->d_off = ++tc->file_idx; // start from 1

    // handle i_mode
    uint16 mode = __imode_from_fcb(&(tc->fcb_s));
    dirent_buf->d_type = (mode & S_IFMT) >> 8;
    dirent_buf->d_type = __IMODE_TO_DTYPE(mode);

    int fname_len = strlen(tc->name_buf);
    safestrcpy(dirent_buf->d_name, tc->name_buf, fname_len); // !!!!!
    dirent_buf->d_name[fname_len] = '\0';
    dirent_buf->d_reclen = dirent_len(dirent_buf);
    // memmove((void *)(buf + nread), (void *)&dirent_buf, sizeof(dirent_buf));
    memmove((void *)(tc->kbuf + *(ssize_t *)tc->retval), (void *)dirent_buf, dirent_buf->d_reclen);
    *(ssize_t *)tc->retval += dirent_buf->d_reclen;
}

// find insert offset
void fat32_dir_fcb_insert_offset_handler(struct trav_control *tc) {
    dirent_s_t *fcb_s = (dirent_s_t *)(tc->kbuf) + tc->idx;
    // bug like this : (dirent_s_t *)(tc->kbuf + tc->idx);

    if (!NAME0_FREE_BOOL(fcb_s->DIR_Name[0])) {
        tc->fcb_free_cnt = 0;
        *(int *)tc->retval = tc->offset_ret_base;
    } else {
        if (tc->fcb_free_cnt == 0) {
            *(int *)tc->retval = tc->start_off / 32 + tc->idx; // remember /32
        }
        tc->fcb_free_cnt++;
        if (tc->fcb_free_cnt == tc->fcb_cnt_req) {
            tc->stop = 1;
            return;
        }
    }
    return;
}

// is empty?
void fat32_isdirempty_handler(struct trav_control *tc) {
    dirent_s_t *fcb_s = (dirent_s_t *)(tc->kbuf) + tc->idx;
    // bug like this : (dirent_s_t *)(tc->kbuf + tc->idx);
    // not free
    if (!NAME0_FREE_BOOL(fcb_s->DIR_Name[0])) {
        if (fat32_namecmp((char *)fcb_s->DIR_Name, ".") && fat32_namecmp((char *)fcb_s->DIR_Name, "..")) {
            tc->stop = 1;
            *(int *)tc->retval = 0; // default is 1
        }
    }
}

// get count of files with the same prefix(short entry name)
void fat32_find_same_name_cnt_handler(struct trav_control *tc) {
    dirent_s_t *fcb_s = (dirent_s_t *)(tc->kbuf) + tc->idx;
    // bug like this : (dirent_s_t *)(tc->kbuf + tc->idx);

    if (NAME0_FREE_ALL(fcb_s->DIR_Name[0])) {
        tc->stop = 1;
        return;
    }

    if (!LONG_NAME_BOOL(fcb_s->DIR_Attr)) {
        // is we search for?
        // extend name should be matched!!!
        if (!strncmp((char *)fcb_s->DIR_Name, tc->name_search, 6) && fcb_s->DIR_Name[6] == '~') {
            *(int *)tc->retval += 1; // initial value is 0
        }
    }
}

void alloc_fail(void) {
    printfGreen("mm: %d pages before alloc fail\n", get_free_mem()/4096);

    acquire(&inode_table.lock);
    struct inode *ip = NULL;

    for (ip = inode_table.inode_entry; ip < &inode_table.inode_entry[NINODE]; ip++) {
        if (ip->ref) {
            // printfBlue("file name : %s recycle, ref : %d\n", ip->fat32_i.fname, ip->ref);

            // release(&inode_table.lock);
            // sema_wait(&ip->i_writeback_lock);
            // ==== atomic ====
            // write back dirty pages of inode
            // fat32_i_mapping_writeback(ip);

            // destory i_mapping
            if (list_empty_atomic(&ip->dirty_list, &ip->i_lock)) {
                fat32_i_mapping_destroy(ip);
            }
            // acquire(&inode_table.lock);

            // destory hash table
            fat32_inode_hash_destroy(ip);

            // // free index table
            // fat32_free_index_table(ip);

            // sema_signal(&ip->i_writeback_lock);
            // ==== atomic ====
        }
    }
    if (list_empty_atomic(&fat32_sb.root->dirty_list, &fat32_sb.root->i_lock)) {
        fat32_i_mapping_destroy(fat32_sb.root);
    }
    fat32_inode_hash_destroy(fat32_sb.root);
    // // free index table
    // fat32_free_index_table(ip);

    printfGreen("mm: %d pages after alloc fail\n", get_free_mem()/4096);

    release(&inode_table.lock);


}

void shutdown_writeback(void) {
    printfGreen("mm: %d pages before writeback\n", get_free_mem()/4096);

    acquire(&inode_table.lock);
    struct inode *ip = NULL;

    for (ip = inode_table.inode_entry; ip < &inode_table.inode_entry[NINODE]; ip++) {
        if (ip->ref) {
            // printfBlue("file name : %s recycle, ref : %d\n", ip->fat32_i.fname, ip->ref);

            // release(&inode_table.lock);
            // sema_wait(&ip->i_writeback_lock);
            // ==== atomic ====
            // write back dirty pages of inode
            fat32_i_mapping_writeback(ip);

            // destory i_mapping
            // if (list_empty_atomic(&ip->dirty_list, &ip->i_lock)) {
            fat32_i_mapping_destroy(ip);
            // }
            // acquire(&inode_table.lock);

            // destory hash table
            fat32_inode_hash_destroy(ip);

            // // free index table
            fat32_free_index_table(ip);

            // sema_signal(&ip->i_writeback_lock);
            // ==== atomic ====
        }
    }
    // if (list_empty_atomic(&fat32_sb.root->dirty_list, &fat32_sb.root->i_lock)) {
    fat32_i_mapping_writeback(fat32_sb.root);

    fat32_i_mapping_destroy(fat32_sb.root);
    // }
    fat32_inode_hash_destroy(fat32_sb.root);

    fat32_free_index_table(fat32_sb.root);
    // // free index table
    // fat32_free_index_table(ip);

    printfGreen("mm: %d pages after writeback\n", get_free_mem()/4096);
    release(&inode_table.lock);
    fat32_fat_bitmap_writeback(ROOTDEV, &fat32_sb);

}

// get time string
// int fat32_time_parser(uint16 *time_in, char *str, int ms) {
//     uint CreateTimeMillisecond;
//     uint TimeSecond = time_in->second_per_2 << 1;
//     uint TimeMinute = time_in->minute;
//     uint TimeHour = time_in->hour;

//     if (ms) {
//         CreateTimeMillisecond = (uint)(ms)*10; // 计算毫秒数
//         sprintf(str, "%d:%02d:%02d.%03d", TimeHour, TimeMinute, TimeSecond, CreateTimeMillisecond);
//     } else {
//         sprintf(str, "%d:%02d:%02d", TimeHour, TimeMinute, TimeSecond);
//     }

//     return 1;
// }

// get date string
// int fat32_date_parser(uint16 *date_in, char *str) {
//     uint TimeDay = date_in->day;
//     uint TimeMonth = date_in->month;
//     uint TimeYear = date_in->year + 1980;

//     sprintf(str, "%04d-%02d-%02d", TimeYear, TimeMonth, TimeDay);

//     return 1;
// }

// get time now
// uint16 fat32_inode_get_time(int *ms) {
//     // TODO
//     // uint16 time_ret;
//     // uint64 count;

//     // asm volatile("rdtime %0" : "=r"(count));
//     // // second and its reminder
//     // uint64 tick_per_second = 10000000;   // 时钟频率为 32.768 kHz
//     // uint64 seconds = count / tick_per_second;
//     // uint64 remainder = count % tick_per_second;

//     // // hour minute second
//     // uint64 total_seconds = (uint32)seconds;
//     // uint64 sec_per_hour = 3600;
//     // uint64 sec_per_minute = 60;

//     // time_ret.hour = total_seconds / sec_per_hour;
//     // time_ret.minute = (total_seconds / sec_per_minute) % 60;
//     // time_ret.second_per_2 = (total_seconds % 60)>>1;
//     // if(ms)
//     // {
//     //     // million second
//     //     uint64 tick_per_ms = tick_per_second / 1000;
//     //     *ms = remainder / tick_per_ms;
//     // }
//     // return time_ret;

//     return TODO();
// }

// get date now
// uint16 fat32_inode_get_date() {
//     // TODO
// }
// // zero cluster
// void fat32_zero_cluster(uint64 c_num) {
//     struct buffer_head *bp;
//     int first_sector;
//     first_sector = FirstSectorofCluster(c_num);
//     for (int s = 0; s < (fat32_sb.sectors_per_block); s++) {
//         bp = bread(fat32_sb.s_dev, first_sector + s);
//         memset(bp->data, 0, BSIZE);
//         bwrite(bp);
//         brelse(bp);
//     }
//     return;
// }
// // direntory link (hard link)
// int fat32_inode_dirlink(struct inode *dp, char *name) {
//     // int off;
//     struct inode *ip;

//     // Check that name is not present.
//     if ((ip = fat32_inode_dirlookup(dp, name, 0)) != 0) {
//         fat32_inode_put(ip);
//         return -1;
//     }
//     ip->i_nlink++;
//     fat32_inode_put(ip);
//     return 0;
// }
