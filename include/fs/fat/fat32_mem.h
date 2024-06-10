#ifndef __FAT32_MEM_H__
#define __FAT32_MEM_H__

#include "common.h"
#include "fat32_disk.h"
#include "fat32_stack.h"
#include "fs/stat.h"
#include "lib/hash.h"
#include "fs/bio.h"

struct inode;

// Oscomp
struct fat_dirent_buf {
    uint64 d_ino;            // 索引结点号
    int64 d_off;             // 到下一个dirent的偏移
    unsigned short d_reclen; // 当前dirent的长度
    unsigned char d_type;    // 文件类型
    char d_name[];           // 文件名
};

// fat32 super block information
struct fat32_sb_info {
    // read-only
    uint fatbase;        // FAT base sector
    uint n_fats;         // Number of FATs (1 or 2)
    uint n_sectors_fat;  // Number of sectors per FAT
    uint root_cluster_s; // Root directory base cluster (start)

    // FSINFO ~ may modify
    uint free_count;
    uint nxt_free;

    // help fsinfo
    int hint_valid;

    // dirty
    int dirty;
};

// fat32 inode information
struct fat32_inode_info {
    // on-disk structure
    char fname[NAME_LONG_MAX];
    uchar Attr;             // directory attribute
    uchar DIR_CrtTimeTenth; // create time
    uint16 DIR_CrtTime;     // create time, 2 bytes
    uint16 DIR_CrtDate;     // create date, 2 bytes
    uint16 DIR_LstAccDate;  // last access date, 2 bytes
    // (DIR_FstClusHI << 16) | (DIR_FstClusLO)
    uint32 cluster_start; // start num
    uint16 DIR_WrtTime;   // Last modification (write) time.
    uint16 DIR_WrtDate;   // Last modification (write) date.
    uint32 DIR_FileSize;  // file size (bytes)

    // in memory structure
    uint32 cluster_end; // end num
    uint64 cluster_cnt; // number of clusters
    uint32 parent_off;  // offset in parent clusters
};

struct __dirent {
    uint64 d_ino;            // 索引结点号
    int64 d_off;             // 到下一个dirent的偏移 (start from 1)
    unsigned short d_reclen; // 当前dirent的长度
    unsigned char d_type;    // 文件类型
    char d_name[];           // 文件名
};

struct inode_cache {
    // uint32 ino;
    struct inode *ip;
    uint32 off;
};

struct trav_control {
    // general
    struct inode *dp;
    char *kbuf;
    int idx;
    char name_search[50]; // the length of file may exceed 50
    int start_off;        // unit is 1 Byte
    int end_off;          // unit is 1 Byte
    uint32 off;           // unit is 32 Bytes

    void *retval;
    uchar stop;

    // options
    int ops;

    // using stack
    Stack_t *fcb_stack;
    char *name_buf;

    // dirlookup
    uint *poff;
    struct inode *ip_search;

    // getdents
    int file_idx;
    uint32 i_ino;
    dirent_s_t fcb_s; // can it be a pointer?

    // insert_off
    int fcb_cnt_req;
    int fcb_free_cnt;
    int offset_ret_base;
};

typedef void (*trav_handler)(struct trav_control *);

#define DIRLOOKUP_OP 1
#define GETDENTS_OP 2
#define dirent_len(dirent) (sizeof(dirent->d_ino) + sizeof(dirent->d_off) + sizeof(dirent->d_type) + sizeof(dirent->d_reclen) + strlen(dirent->d_name) + 1)

// ==================== part I : the management of inode ====================
// init the root fat32 inode
struct inode *fat32_root_inode_init(struct _superblock *sb);

// get information about inodes
void fat32_inode_stati(struct inode *ip, struct kstat *st);

// lock the fat32 inode
void fat32_inode_lock(struct inode *ip);

// unlock the fat32 inode
void fat32_inode_unlock(struct inode *ip);

// put the fat32 inode
void fat32_inode_put(struct inode *ip);

// unlock and put the fat32 inode
void fat32_inode_unlock_put(struct inode *ip);

// dup a existed fat32 inode
struct inode *fat32_inode_dup(struct inode *ip);

// find a existed or new fat32 inode
struct inode *fat32_inode_get(uint dev, struct inode *dp, const char *name, uint parentoff);

// load inode from disk
int fat32_inode_load_from_disk(struct inode *ip);

// create a fat32 inode for device
struct inode *fat32_inode_create(struct inode *dp, const char *name, uint16 type, short major, short minor); // now use this

// allocate the fat32 inode
struct inode *fat32_inode_alloc(struct inode *dp, const char *name, uint16 type);

// init the fat32 fcb (short entry + long entry)
int fat32_fcb_init(struct inode *, const uchar *, uint16, char *);

// the right fcb insert offset
int fat32_dir_fcb_insert_offset(struct inode *, uchar);

// update the fat32 inode in the disk
void fat32_inode_update(struct inode *);

// copy ip to dp(only add fcbs in dp)
int fat32_fcb_copy(struct inode *dp, struct inode *ip);

// truncate the fat32 inode
void fat32_inode_trunc(struct inode *ip);

// delete fat32 inode (short entry + long entry)
int fat32_fcb_delete(struct inode *dp, struct inode *ip);

// ==================== part II : the management of BPB、FSINFO and FAT table ====================
// allocate a new fat entry
uint fat32_fat_alloc(FAT_entry_t hint);

// set the fat entry to given value
void fat32_fat_set(uint cluster, uint value);

// traverse the fat32 chain
uint32 fat32_fat_travel(struct inode *ip, uint num);

// allocate a new cluster
FAT_entry_t fat32_cluster_alloc(uint dev);

// return the next cluster number
uint fat32_next_cluster(uint cluster_cur);

// update fsinfo
void fat32_update_fsinfo(uint dev);

// allocate a page to fill cluster num
uint64 fat32_page_alloc(int n);

// lookup or create index table to find the cluster_num of logistic_num
uint32 fat32_ctl_index_table(struct inode *ip, uint32 l_num, uint32 cluster_num);

// free index table
void fat32_free_index_table(struct inode *ip);

// ==================== part III : special for long entry and short entry ====================
// reverse the dirent_l to get the long name
ushort fat32_longname_popstack(Stack_t *fcb_stack, uchar *fcb_s_name, char *name_buf);

// cat the Name1, Name2 and Name3 of dirent_l
int fat32_filter_longname(dirent_l_t *dirent_l_tmp, char *ret_name);

// the check sum of dirent_l
uchar ChkSum(uchar *pFcbName);

// the number of files with the same name prefix
int fat32_find_same_name_cnt(struct inode *dp, char *name);

// short name parser
void fat32_short_name_parser(dirent_s_t dirent_l, char *name_buf);

// ==================== part IV : the management of data region ====================
// direcotry is empty?
int fat32_isdirempty(struct inode *dp);

// lookup inode given its name and inode of its parent
struct inode *fat32_inode_dirlookup(struct inode *dp, const char *name, uint *poff);

// dirlookup with hint
struct inode *fat32_inode_dirlookup_with_hint(struct inode *dp, const char *name, uint *poff);

// inode read
ssize_t fat32_inode_read(struct inode *ip, int user_dst, uint64 dst, uint off, uint n);

// inode write
ssize_t fat32_inode_write(struct inode *ip, int user_src, uint64 src, uint off, uint n);

// ==================== part V : the management of blocks ====================
// move cursor
uint32 fat32_cursor_to_offset(struct inode *ip, uint off, FAT_entry_t *c_start, int *init_s_n, int *init_s_offset);

// get bio of blocks
int fat32_get_block(struct inode *ip, struct bio *bio_p, uint off, uint n, int alloc);

// ==================== part VI : general interface of travel、read and write ====================
// general_travel
void fat32_inode_general_trav(struct inode *dp, struct trav_control *tc, trav_handler fn);

// for dirlookup and getdents
void fat32_inode_travel_fcb_handler(struct trav_control *tc);

// for dirlookup
void fat32_inode_dirlookup_handler(struct trav_control *tc);

// for getdents
void fat32_inode_getdents_handler(struct trav_control *tc);

// for fat32_dir_fcb_insert_offset
void fat32_dir_fcb_insert_offset_handler(struct trav_control *tc);

// for fat32_isdirempty
void fat32_isdirempty_handler(struct trav_control *tc);

// for fat32_find_same_name_cnt
void fat32_find_same_name_cnt_handler(struct trav_control *tc);

// ==================== part VII : hash table for speeding up dirlookup ====================
// init the hash table of inode
void fat32_inode_hash_init(struct inode *dp);

// insert inode hash table
int fat32_inode_hash_insert(struct inode *dp, const char *name, struct inode *ip, uint off);

// lookup the hash table of inode
struct inode *fat32_inode_hash_lookup(struct inode *dp, const char *name);

// delete hash table of inode
void fat32_inode_hash_destroy(struct inode *dp);

// ==================== part VIII ： the management of i_mapping ===========================
// init i_mapping
void fat32_i_mapping_init(struct inode *ip);

// i_mapping writeback
void fat32_i_mapping_writeback(struct inode *ip);

// destory i_mapping
void fat32_i_mapping_destroy(struct inode *ip);

// ignore it, a rough process
void alloc_fail(void);

void shutdown_writeback(void);
// ======================= abandon， may be ============================
// timer to string
// int fat32_time_parser(uint16 *, char *, int);

// date to string
// int fat32_date_parser(uint16 *, char *);

// acquire the time now
// uint16 fat32_inode_get_time(int *);

// acquire the date now
// uint16 fat32_inode_get_date();

// zero the cluster given cluster num
// void fat32_zero_cluster(uint64 c_num);

#endif