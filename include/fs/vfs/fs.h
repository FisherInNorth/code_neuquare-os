#ifndef __VFS_FS_H__
#define __VFS_FS_H__

#include "common.h"
#include "param.h"
#include "atomic/spinlock.h"
#include "atomic/semaphore.h"
#include "fs/stat.h"
#include "fs/fcntl.h"
#include "fs/fat/fat32_mem.h"
#include "lib/hash.h"
#include "lib/radix-tree.h"
#include "lib/list.h"
#include "fs/mpage.h"

struct kstat;
extern struct ftable _ftable;

struct socket;
union file_type {
    struct pipe *f_pipe;   // FD_PIPE
    struct inode *f_inode; // FDINODE and FD_DEVICE
    struct socket *f_sock; // FD_SOCKET
};

typedef enum {
    FAT32 = 1,
    EXT2,
} fs_t;

struct _superblock {
    struct semaphore sem; /* binary semaphore */
    struct spinlock lock;// spinlock
    uint8 s_dev;          // device number

    uint32 s_blocksize;       // 逻辑块的数量
    uint32 sectors_per_block; // 每个逻辑块的扇区个数
    uint cluster_size;        // size of a cluster

    // uint32 s_blocksize_bits;
    uint n_sectors;   // Number of sectors
    uint sector_size; // size of a sector

    struct super_operations *s_op;
    struct inode *s_mount;
    struct inode *root;

    struct spinlock dirty_lock; // used to protect dirty list
    struct list_head s_dirty;   /* dirty inodes */

    // FAT table -> bit map
    uint64 bit_map;
    uint64 fat_table;
    
    union {
        struct fat32_sb_info fat32_sb_info;
        // struct xv6fs_sb_info xv6fs_sb;
        // void *generic_sbp;
    };
};

// abstarct everything in memory
struct file {
    type_t f_type;
    ushort f_mode;
    // uint32 f_pos; // bug
    off_t f_pos;

    ushort f_flags;
    ushort f_count;
    short f_major;

    void *private_data; // for shared memory

    int f_owner; /* pid or -pgrp where SIGIO should be sent */
    union file_type f_tp;
    const struct file_operations *f_op; // don't use pointer (bug maybe)!!!!
    // unsigned long f_version;

    int is_shm_file; // for shared memory
};

struct ftable {
    struct spinlock lock;
    struct file file[NFILE];
};

// #define NAME_MAX 10
// struct _dirent {
//     long d_ino;
//     char d_name[NAME_MAX + 1];
// };

// for index table of inode
// levl0 : level1 : level2 : level 3 = 12 : 1 : 1 : 1 is recommended
#define N_DIRECT 12                          // direct
#define N_LEVEL_ONE 50                       // level 1
#define N_LEVEL_TWO 1                        // level 2
#define N_LEVEL_THREE 1                      // level 3
#define N_INDIRECT (PGSIZE / sizeof(uint32)) // 4096/4 = 1024
#define MAX_INDEX_0 (N_DIRECT)
#define OFFSET_LEVEL_1 (N_INDIRECT)
#define OFFSET_LEVEL_2 (N_INDIRECT * N_INDIRECT)
#define OFFSET_LEVEL_3 (N_INDIRECT * N_INDIRECT * N_INDIRECT)
#define MAX_INDEX_1 (MAX_INDEX_0 + N_LEVEL_ONE * OFFSET_LEVEL_1)
#define MAX_INDEX_2 (MAX_INDEX_1 + N_LEVEL_TWO * OFFSET_LEVEL_2)
#define MAX_INDEX_3 (MAX_INDEX_2 + N_LEVEL_THREE * OFFSET_LEVEL_3)
#define IDX_SHIFT (N_INDIRECT)
#define IDX_N_SHIFT(level) (N_INDIRECT + (N_INDIRECT * (level - 1)))
#define IDX_MASK (N_INDIRECT - 1) // 10 bits : 11_1111_1111
#define IDX_N(idx, level) ((((uint64)(idx)) >> IDX_N_SHIFT(level)) & IDX_MASK)
#define IDX_OFFSET(idx) (idx & IDX_MASK)

struct index_table {
    uint32 direct[N_DIRECT];
    uint64 indirect_one[N_LEVEL_ONE];
    uint64 indirect_two[N_LEVEL_TWO];
    uint64 indirect_three[N_LEVEL_THREE];
};

// abstract datas in disk
struct inode {
    dev_t i_dev; // note: 未在磁盘中存储
    // uint32 i_ino; // 对任意给定的文件系统的唯一编号标识：由具体文件系统解释
    ino_t i_ino; // bug for libc-test
    // uint16 i_mode; // 访问权限和所有权
    mode_t i_mode; // 文件类型 + ..? + 访问权限  (4 + 3 + 9) : obey Linux
    int ref;       // Reference count
    int valid;
    // Note: fat fs does not support hard link, reserve for vfs interface
    // uint16 i_nlink; // bug!!!
    nlink_t i_nlink;
    uid_t i_uid;
    gid_t i_gid;
    uint16 i_rdev; // major、minor, 8 + 8
    uint32 i_size;
    // uint16 i_type;       // we do no use it anymore

    long i_atime;        // access time
    long i_mtime;        // modify time
    long i_ctime;        // create time
    blksize_t i_blksize; // bytes of one block
    blkcnt_t i_blocks;   // numbers of blocks

    struct semaphore i_sem;       /* binary semaphore */
    struct semaphore i_read_lock; // special for mpage_read
    struct semaphore i_writeback_lock;// special for write back and clear cache

    const struct inode_operations *i_op;
    struct _superblock *i_sb;
    struct inode *i_mount;
    // struct wait_queue *i_wait;
    struct inode *parent;

    fs_t fs_type;

    // O(1) to get inode information
    struct hash_table *i_hash;

    // speed up dirlookup
    int off_hint;

    struct spinlock i_lock;          // protecting other fields
    uint64 i_writeback;              // writing back ?
    struct list_head dirty_list;     // link with superblock s_dirty
    struct address_space *i_mapping; // used for page cache
    spinlock_t tree_lock;            /* and lock protecting radix tree */

    struct list_head list; // to speed up inode_get

    int dirty_in_parent; // need to update ??
    int create_cnt;      // for inode parent
    int create_first;    // for inode child
    int shm_flg;         // for shared memory

    // speed up fat dirlookup
    struct index_table i_table;
    union {
        struct fat32_inode_info fat32_i;
        // struct xv6inode_info xv6_i;
        // struct ext2inode_info ext2_i;
        // void *generic_ip;
    };
};

// for page cache
#define PAGECACHE_TAG_DIRTY 0
#define PAGECACHE_TAG_WRITEBACK 1
struct address_space {
    struct inode *host;               /* owner: inode*/
    struct radix_tree_root page_tree; /* radix tree(root) of all pages */
    uint64 nrpages;                   /* number of total pages */
    uint64 last_index;                // 2 4 6 8 ... read head policy
    uint64 read_ahead_cnt;            // the number of read ahead
    uint64 read_ahead_end;            // the end index of read ahead
};

struct file_operations {
    // struct file *(*alloc)(void);
    struct file *(*dup)(struct file *self);
    ssize_t (*read)(struct file *self, uint64 __user dst, int n);
    ssize_t (*write)(struct file *self, uint64 __user src, int n);
    int (*fstat)(struct file *self, uint64 __user dst);
    // int (*ioctl) (struct inode *, struct file *, unsigned int cmd, unsigned long __user arg);
    long (*ioctl)(struct file *self, unsigned int cmd, unsigned long arg);
    // size_t (*readdir)(struct file *self, char *buf, size_t len);
    size_t (*readdir)(struct inode *dp, char *buf, uint32 off, size_t len);
};

struct inode_operations {
    void (*iunlock_put)(struct inode *self);
    void (*iunlock)(struct inode *self);
    void (*iput)(struct inode *self);
    void (*ilock)(struct inode *self);
    void (*iupdate)(struct inode *self);
    struct inode *(*idup)(struct inode *self);
    void (*ipathquery)(struct inode *self, char *kbuf);
    ssize_t (*iread)(struct inode *self, int user_dst, uint64 dst, uint off, uint n);
    ssize_t (*iwrite)(struct inode *self, int user_src, uint64 src, uint off, uint n);

    // for directory inode
    struct inode *(*idirlookup)(struct inode *dself, const char *name, uint *poff);
    int (*idempty)(struct inode *dself);
    // ssize_t (*igetdents)(struct inode *dself, char *buf, size_t len);
    struct inode *(*icreate)(struct inode *dself, const char *name, uint16 type, short major, short minor);
    int (*ientrycopy)(struct inode *dself, struct inode *ip);
    int (*ientrydelete)(struct inode *dself, struct inode *ip);
};

struct linux_dirent {
    uint64 d_ino;            // 索引结点号
    int64 d_off;             // 到下一个dirent的偏移
    unsigned short d_reclen; // 当前dirent的长度
    unsigned char d_type;    // 文件类型
    char d_name[];           // 文件名
};

#endif // __VFS_FS_H__