#include "common.h"
#include "atomic/spinlock.h"
#include "kernel/trap.h"
#include "debug.h"
#include "param.h"
#include "proc/pcb_life.h"
#include "ipc/pipe.h"
#include "fs/bio.h"
#include "fs/stat.h"
#include "fs/vfs/fs.h"
#include "fs/vfs/ops.h"
#include "fs/fcntl.h"
#include "fs/fat/fat32_disk.h"
#include "fs/fat/fat32_mem.h"
#include "fs/fat/fat32_stack.h"
#include "fs/fat/fat32_file.h"
#include "memory/allocator.h"

extern uint64 socket_write(struct socket *sock, vaddr_t addr, int len);
extern uint64 socket_read(struct socket *sock, vaddr_t addr, int len);
int pid_debug_1 = 9;
int pid_debug_2 = 11;

// #define _O_READ              (~O_WRONLY)
// #define _O_WRITE             (O_WRONLY | O_RDWR | O_CREATE |)
void fileinit(void) {
    initlock(&_ftable.lock, "_ftable");
}

// Increment ref count for file f.
// 语义：将 f 指向的 file 文件的引用次数自增，并返回该指针
// 实现：给 _ftable 加锁后，f->f_count++
struct file *fat32_filedup(struct file *f) {
    acquire(&_ftable.lock);
    if (f->f_count < 1)
        panic("filedup");
    f->f_count++;
    release(&_ftable.lock);
    return f;
}

// Get metadata about file f.
// addr is a user virtual address, pointing to a struct stat.
// 语义：获取文件 f 的相关属性，写入 addr 指向的用户空间
int fat32_filestat(struct file *f, uint64 addr) {
    struct proc *p = proc_current();
    struct kstat st;
    // ASSERT(sizeof(st) == 128);
    // printf("KERNEL: sizeof kstat = %d\n",sizeof(st));
    memset(&st, 0, sizeof(st)); // avoid leak kernel data to user

    if (f->f_type == FD_INODE || f->f_type == FD_DEVICE) {
        fat32_inode_lock(f->f_tp.f_inode);
        // fat32_inode_load_from_disk(f->f_tp.f_inode);

        fat32_inode_stati(f->f_tp.f_inode, &st);
        fat32_inode_unlock(f->f_tp.f_inode);
        if (copyout(p->mm->pagetable, addr, (char *)&st, sizeof(st)) < 0)
            return -1;
        return 0;
    }
    return -1;
}

// Read from file f.
// addr is a user virtual address.
// 语义：读取文件 f ，从 偏移量 f->f_pos 起始，读取 n 个字节到 addr 指向的用户空间
ssize_t fat32_fileread(struct file *f, uint64 addr, int n) {
#ifdef __DEBUG_PIPE__   
    static int pipe_r_cnt = 0;// debug
#endif
    int r = 0;

    if (F_READABLE(f) == 0)
        return -1;

    if (f->f_type == FD_PIPE) {
        r = pipe_read(f->f_tp.f_pipe, 1, addr, n);
#ifdef __DEBUG_PIPE__
        pipe_r_cnt += 1;
        if(pipe_r_cnt % 1000==0)
        printfGreen("read pipe : pid : %d, read %d chars -> pipe file (%d) starting from %d\n", proc_current()->pid, r, f->f_tp.f_pipe, f->f_tp.f_pipe->nread - r);
#endif
    } else if (f->f_type == FD_DEVICE) {
        if (f->f_major < 0 || f->f_major >= NDEV || !devsw[f->f_major].read)
            return -1;
        r = devsw[f->f_major].read(1, addr, n);
    } else if (f->f_type == FD_INODE) {
        n = MIN(n, f->f_tp.f_inode->i_size);
        fat32_inode_lock(f->f_tp.f_inode);

        if ((r = fat32_inode_read(f->f_tp.f_inode, 1, addr, f->f_pos, n)) > 0)
            f->f_pos += r;
        fat32_inode_unlock(f->f_tp.f_inode);

        // debug!!!
        // if (r < 0)
        //     printfMAGENTA("read : error, reading chars of inode file %s starting from %d \n", f->f_tp.f_inode->fat32_i.fname, f->f_pos);
        // else
        //     printfMAGENTA("read : read %d chars of inode file %s starting from %d \n", r, f->f_tp.f_inode->fat32_i.fname, f->f_pos - r);

#ifdef __DEBUG_RW__
        // if(proc_current()->pid == pid_debug_1 || proc_current()->pid == pid_debug_2) {
        if (r < 0)
            printfMAGENTA("read inode : pid : %d, error, reading chars of inode file %s starting from %d \n", proc_current()->pid, f->f_tp.f_inode->fat32_i.fname, f->f_pos);
        else
            printfMAGENTA("read inode : pid : %d, read %d chars of inode file %s starting from %d \n", proc_current()->pid, r, f->f_tp.f_inode->fat32_i.fname, f->f_pos - r);
            // }
#endif
    } else if (f->f_type == FD_SOCKET) {
        r = socket_read(f->f_tp.f_sock, addr, n);
    } else {
        panic("fileread");
    }

    return r;
}

// Write to file f.
// addr is a user virtual address.
// 语义：写文件 f ，从 f->f_pos开始，把用户空间 addr 起始的 n 个字节的内容写入文件 f
ssize_t fat32_filewrite(struct file *f, uint64 addr, int n) {
#ifdef __DEBUG_PIPE__   
    static int pipe_w_cnt = 0;// debug
#endif
    int r, ret = 0;

    if (F_WRITEABLE(f) == 0)
        return -1;

    if (f->f_type == FD_PIPE) {
        ret = pipe_write(f->f_tp.f_pipe, 1, addr, n);
        // debug!!
#ifdef __DEBUG_PIPE__
        pipe_w_cnt += 1;
        if(pipe_w_cnt % 1000==0)
        printfYELLOW("write pipe: pid : %d, write %d chars -> pipe file (%d) starting from %d\n", proc_current()->pid, ret, f->f_tp.f_pipe, f->f_tp.f_pipe->nwrite - ret);
#endif
    } else if (f->f_type == FD_DEVICE) {
        // special for dev_cpu_dma_latency?
        if (f->f_major == DEV_CPU_DMA_LATENCY) {
            return n;
        }
        if (f->f_major < 0 || f->f_major >= NDEV || !devsw[f->f_major].write)
            return -1;
        ret = devsw[f->f_major].write(1, addr, n);
    } else if (f->f_type == FD_INODE) {
        // write a few blocks at a time to avoid exceeding
        // the maximum log transaction size, including
        // i-node, indirect block, allocation blocks,
        // and 2 blocks of slop for non-aligned writes.
        // this really belongs lower down, since writei()
        // might be writing a device like the console.
        // int max = ((MAXOPBLOCKS - 1 - 1 - 2) / 2) * BSIZE;
        int i = 0;
        while (i < n) {
            int n1 = n - i;
            // if (n1 > max)
            //     n1 = max;
            // begin_op();
            fat32_inode_lock(f->f_tp.f_inode);
            // fat32_inode_load_from_disk(f->f_tp.f_inode);
            if ((r = fat32_inode_write(f->f_tp.f_inode, 1, addr + i, f->f_pos, n1)) > 0)
                f->f_pos += r;
            fat32_inode_unlock(f->f_tp.f_inode);
            // end_op();

            if (r != n1) {
                // error from writei
                break;
            }
            i += r;
        }
        ret = (i == n ? n : -1);

        // debug!!!
        // if (ret < 0)
        //     printfBlue("write : error writing chars -> inode file %s starting from %d\n", f->f_tp.f_inode->fat32_i.fname, f->f_pos);
        // else
        //     printfBlue("write : pid : %d, write %d chars -> inode file %s starting from %d\n", proc_current()->pid, i, f->f_tp.f_inode->fat32_i.fname, f->f_pos - i);
        // }
        // if (i != n) {
        //     printf("ready\n");
        // }

#ifdef __DEBUG_RW__
        // if(proc_current()->pid == pid_debug_1 || proc_current()->pid == pid_debug_2) {
        if (ret < 0)
            printfBlue("write inode: pid : %d,  error writing chars -> inode file %s starting from %d\n", proc_current()->pid, f->f_tp.f_inode->fat32_i.fname, f->f_pos);
        else
            printfBlue("write inode: pid : %d, write %d chars -> inode file %s starting from %d\n", proc_current()->pid, i, f->f_tp.f_inode->fat32_i.fname, f->f_pos - i);
            // }
#endif
    } else if (f->f_type == FD_SOCKET) {
        ret = socket_write(f->f_tp.f_sock, addr, n);
    } else {
        // panic("filewrite");
        if (!f->is_shm_file) {
            panic("file write\n");
        }
    }

    return ret;
}

// 查询 ip 指向的 inode 文件的绝对路径
// 不做参数检查
// buf 最后以 / 结尾
// 感觉目录项得有，不然inode缓存一下子就被污染了
void get_absolute_path(struct inode *ip, char *buf) {
    if (ip->fat32_i.fname[0] != '/') {
        get_absolute_path(ip->parent, buf);
    } else {
        ;
    }

    size_t n0 = strlen(buf), n1 = strlen(ip->fat32_i.fname);
    strncpy(buf + n0, ip->fat32_i.fname, n1);
    if (ip->fat32_i.fname[0] != '/') {
        safestrcpy(buf + n0 + n1, "/", 2);
    }

    return;
}

//  The  getcwd()  function  copies  an  absolute  pathname
// of the current working directory to the array pointed to by buf,
// which is of length size.
// make sure sizeof buf is big enough to hold a absolute path!
void fat32_getcwd(char *buf) {
    // note: buf must have size PATH_LONG_MAX
    if (!buf) return;
    // ASSERT(strnlen(buf,PATH_LONG_MAX) >= PATH_LONG_MAX);
    struct proc *p = proc_current();
    get_absolute_path(p->cwd, buf);
    size_t n = strlen(buf);
    if (n > 1) {
        buf[n - 1] = 0; // clear '/'
    }
    return;
}

// 将 dp 目录下的所有目录项解析成 struct _dirent，填入 buf 中
// len 为 buf 的最大长度
// 不用检查参数
// 返回读取的字节数
// 可用但不正确
size_t fat32_getdents(struct inode *dp, char *buf, uint32 off, size_t len) {
    struct trav_control tc;
    tc.kbuf = buf;
    tc.start_off = off;
    tc.end_off = off + len;
    tc.ops = GETDENTS_OP;
    ssize_t nreads = 0;
    tc.retval = (void *)&nreads;
    // don't forget it
    if (dp->i_hash == NULL) {
        fat32_inode_hash_init(dp);
    }
    fat32_inode_general_trav(dp, &tc, fat32_inode_travel_fcb_handler);
    return nreads;
}
// size_t fat32_getdents(struct file *f, char *buf, size_t len) {
//     struct buffer_head *bp;
//     struct inode *dp, *ip_buf;
//     char buf_tmp[NAME_LONG_MAX + 30];
//     struct __dirent *dirent_buf = (struct __dirent *)buf_tmp;

//     ssize_t nread = 0;
//     char name_buf[NAME_LONG_MAX];
//     memset(name_buf, 0, sizeof(name_buf));
//     Stack_t fcb_stack;
//     stack_init(&fcb_stack);

//     dp = f->f_tp.f_inode;
//     fat32_inode_lock(dp);
//     FAT_entry_t iter_c_n = dp->fat32_i.cluster_start;

//     int first_sector;
//     uint off = 0;
//     uint cnt = 0;
//     int fname_len = 0;

//     if (dp->i_hash == NULL) {
//         fat32_inode_hash_init(dp);
//     }

//     int idx = 0;
//     // FAT seek cluster chains
//     while (!ISEOF(iter_c_n)) {
//         first_sector = FirstSectorofCluster(iter_c_n);
//         // sectors in a cluster
//         for (int s = 0; s < (dp->i_sb->sectors_per_block); s++) {
//             // uint sec_pos = DEBUG_SECTOR(dp, first_sector + s); // debug
//             // printf("%d\n",sec_pos); // debug
//             bp = bread(dp->i_dev, first_sector + s);
//             dirent_s_t *fcb_s = (dirent_s_t *)(bp->data);
//             dirent_l_t *fcb_l = (dirent_l_t *)(bp->data);
//             idx = 0;
//             // FCB in a sector
//             while (idx < FCB_PER_BLOCK) {
//                 // long dirctory item push into the stack
//                 if (NAME0_FREE_ALL(fcb_s[idx].DIR_Name[0])) {
//                     brelse(bp);
//                     goto finish;
//                 }
//                 while ((LONG_NAME_BOOL(fcb_l[idx].LDIR_Attr)) && idx < FCB_PER_BLOCK) {
//                     stack_push(&fcb_stack, fcb_l[idx++]);
//                     off++;
//                 }
//                 // pop stack
//                 if (!LONG_NAME_BOOL(fcb_l[idx].LDIR_Attr) && !NAME0_FREE_BOOL(fcb_s[idx].DIR_Name[0]) && idx < FCB_PER_BLOCK) {
//                     memset(name_buf, 0, sizeof(name_buf));
//                     ushort long_valid = fat32_longname_popstack(&fcb_stack, fcb_s[idx].DIR_Name, name_buf);

//                     // if (fat32_namecmp(name_buf, "console.dev") == 0) {
//                     //     Log("ready\n");
//                     // }
//                     // if long directory is invalid
//                     if (!long_valid) {
//                         fat32_short_name_parser(fcb_s[idx], name_buf);
//                     }
//                     // speciall judgement for the first long directory in the data region
//                     cnt++;

//                     uint ino = SECTOR_TO_FATINUM(first_sector + s, idx);

//                     // insert cache into the hash table
//                     int ret = fat32_inode_hash_insert(dp, name_buf, ino, off);
//                     if (ret == 0) {
//                         // printfGreen("getdents : insert : %s\n", name_buf); //debug
//                     }

//                     // get a pos for inode
//                     ip_buf = fat32_inode_get(dp->i_dev, ino, name_buf, off);
//                     ip_buf->parent = dp;
//                     ip_buf->i_nlink = 1;
//                     brelse(bp); // !!!!

//                     fat32_inode_lock(ip_buf);

//                     dirent_buf->d_ino = ip_buf->i_ino;
//                     dirent_buf->d_off = cnt;
//                     // dirent_buf->d_type = (ip_buf->i_mode & S_IFMT) >> 8; // error, not use
//                     dirent_buf->d_type = __IMODE_TO_DTYPE(ip_buf->i_mode);
//                     fname_len = strlen(ip_buf->fat32_i.fname);
//                     safestrcpy(dirent_buf->d_name, name_buf, fname_len); // !!!!!

//                     dirent_buf->d_name[fname_len] = '\0';

//                     dirent_buf->d_reclen = dirent_len(dirent_buf);
//                     // memmove((void *)(buf + nread), (void *)&dirent_buf, sizeof(dirent_buf));
//                     // 只从 f->f_pos 读起
//                     if (cnt >= f->f_pos) {
//                         if (len >= dirent_buf->d_reclen) {
//                             memmove((void *)(buf + nread), (void *)dirent_buf, dirent_buf->d_reclen);
//                             ++f->f_pos;
//                             nread += dirent_buf->d_reclen;
//                             len -= dirent_buf->d_reclen;
//                         } else {
//                             // 用户的缓存区用尽
//                             fat32_inode_unlock(dp);
//                             return nread;
//                         }
//                     }

//                     fat32_inode_unlock_put(ip_buf);

//                     bp = bread(dp->i_dev, first_sector + s);
//                 }
//                 idx++;
//                 off++;
//             }
//             brelse(bp);
//         }
//         iter_c_n = fat32_next_cluster(iter_c_n);
//     }

//     stack_free(&fcb_stack);
//     // save hint
//     // dp->idx_hint = idx;
//     dp->off_hint = off;
//     fat32_inode_unlock(dp);

//     f->f_pos = -1;
//     return nread;
// finish:
//     // save hint
//     // dp->idx_hint = idx;
//     dp->off_hint = off;
//     fat32_inode_unlock(dp);
//     stack_free(&fcb_stack);
//     f->f_pos = -1;
//     return nread;
// }

// 一个可用但不正确的版本
// ssize_t fat32_getdents(struct inode *dp, char *buf, size_t len) {
//     if (!DIR_BOOL((dp->fat32_i.Attr)))
//         panic("getdents : not DIR");
//     struct buffer_head *bp;
//     struct inode *ip_buf;
//     char buf_tmp[NAME_LONG_MAX + 30];
//     struct __dirent *dirent_buf = (struct __dirent *)buf_tmp;

//     ssize_t nread = 0;
//     char name_buf[NAME_LONG_MAX];
//     memset(name_buf, 0, sizeof(name_buf));
//     Stack_t fcb_stack;
//     stack_init(&fcb_stack);
//     FAT_entry_t iter_c_n = dp->fat32_i.cluster_start;

//     int first_sector;
//     uint off = 0;
//     uint cnt = 0;
//     int fname_len = 0;

//     if (dp->i_hash == NULL) {
//         fat32_inode_hash_init(dp);
//     }

//     int idx = 0;
//     // FAT seek cluster chains
//     while (!ISEOF(iter_c_n)) {
//         first_sector = FirstSectorofCluster(iter_c_n);
//         // sectors in a cluster
//         for (int s = 0; s < (dp->i_sb->sectors_per_block); s++) {
//             // uint sec_pos = DEBUG_SECTOR(dp, first_sector + s); // debug
//             // printf("%d\n",sec_pos); // debug
//             bp = bread(dp->i_dev, first_sector + s);
//             dirent_s_t *fcb_s = (dirent_s_t *)(bp->data);
//             dirent_l_t *fcb_l = (dirent_l_t *)(bp->data);
//             idx = 0;
//             // FCB in a sector
//             while (idx < FCB_PER_BLOCK) {
//                 // long dirctory item push into the stack
//                 if (NAME0_FREE_ALL(fcb_s[idx].DIR_Name[0])) {
//                     brelse(bp);
//                     goto finish;
//                 }
//                 while ((LONG_NAME_BOOL(fcb_l[idx].LDIR_Attr)) && idx < FCB_PER_BLOCK) {
//                     stack_push(&fcb_stack, fcb_l[idx++]);
//                     off++;
//                 }
//                 // pop stack
//                 if (!LONG_NAME_BOOL(fcb_l[idx].LDIR_Attr) && !NAME0_FREE_BOOL(fcb_s[idx].DIR_Name[0])) {
//                     memset(name_buf, 0, sizeof(name_buf));
//                     ushort long_valid = fat32_longname_popstack(&fcb_stack, fcb_s[idx].DIR_Name, name_buf);

//                     // if (fat32_namecmp(name_buf, "console.dev") == 0) {
//                     //     Log("ready\n");
//                     // }
//                     // if long directory is invalid
//                     if (!long_valid) {
//                         fat32_short_name_parser(fcb_s[idx], name_buf);
//                     }
//                     // speciall judgement for the first long directory in the data region
//                     cnt++;

//                     uint ino = SECTOR_TO_FATINUM(first_sector + s, idx);

//                     // insert cache into the hash table
//                     int ret = fat32_inode_hash_insert(dp, name_buf, ino, off);
//                     if (ret == 0) {
//                         // printfGreen("getdents : insert : %s\n", name_buf); //debug
//                     }

//                     // get a pos for inode
//                     ip_buf = fat32_inode_get(dp->i_dev, ino, name_buf, off);
//                     ip_buf->parent = dp;
//                     ip_buf->i_nlink = 1;
//                     brelse(bp); // !!!!

//                     fat32_inode_lock(ip_buf);

//                     dirent_buf->d_ino = ip_buf->i_ino;
//                     dirent_buf->d_off = cnt;
//                     // dirent_buf->d_type = (ip_buf->i_mode & S_IFMT) >> 8; // error, not use
//                     dirent_buf->d_type = __IMODE_TO_DTYPE(ip_buf->i_mode);
//                     fname_len = strlen(ip_buf->fat32_i.fname);
//                     safestrcpy(dirent_buf->d_name, name_buf, fname_len); // !!!!!

//                     dirent_buf->d_name[fname_len] = '\0';

//                     dirent_buf->d_reclen = dirent_len(dirent_buf);
//                     // memmove((void *)(buf + nread), (void *)&dirent_buf, sizeof(dirent_buf));
//                     memmove((void *)(buf + nread), (void *)dirent_buf, dirent_buf->d_reclen);

//                     nread += dirent_buf->d_reclen;
//                     fat32_inode_unlock_put(ip_buf);

//                     bp = bread(dp->i_dev, first_sector + s);
//                 }
//                 idx++;
//                 off++;
//             }
//             brelse(bp);
//         }
//         iter_c_n = fat32_next_cluster(iter_c_n);
//     }

//     stack_free(&fcb_stack);
//     // save hint
//     // dp->idx_hint = idx;
//     dp->off_hint = off;
//     return nread;
// finish:
//     // save hint
//     // dp->idx_hint = idx;
//     dp->off_hint = off;
//     stack_free(&fcb_stack);
//     return nread;
// }

// 往 dp 目录文件中 写入 代表 ip 的 fcb
// 成功返回 0，失败返回 -1
int fat32_dirlink(struct inode *dp, struct inode *ip) {
    return 0;
}
