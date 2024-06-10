#include "debug.h"
#include "ipc/pipe.h"
#include "proc/pcb_life.h"
#include "fs/fcntl.h"
#include "fs/vfs/fs.h"
#include "fs/vfs/ops.h"
#include "fs/fat/fat32_file.h"
#include "fs/fat/fat32_mem.h"
#include "fs/ext2/ext2_file.h"
#include "ipc/socket.h"

struct devsw devsw[NDEV];
struct ftable _ftable;

// == file layer ==
struct file *filealloc(fs_t type) {
    // Allocate a file structure.
    // 语义：从内存中的 _ftable 中寻找一个空闲的 file 项，并返回指向该 file 的指针
    ASSERT(type == FAT32);
    if (type < 0) {
        // error: ilegal file system type
        return 0;
    }
    struct file *f;
    acquire(&_ftable.lock);
    for (f = _ftable.file; f < _ftable.file + NFILE; f++) {
        if (f->f_count == 0) {
            f->f_count = 1;
            // ASSERT(proc_current()->cwd->fs_type == FAT32);
            // f->f_op = get_fileops[proc_current()->cwd->fs_type]();
            f->f_op = get_fileops[type]();

            release(&_ftable.lock);
            return f;
        }
    }
    release(&_ftable.lock);
    return 0;
}

void generic_fileclose(struct file *f) {
    // Close file f.  (Decrement ref count, close when reaches 0.)
    // 语义：自减 f 指向的file的引用次数，如果为0，则关闭
    // 对于管道文件，调用pipeclose
    // 否则，调用iput归还inode节点
    struct file ff;

    acquire(&_ftable.lock);
    if (f->f_count < 1)
        panic("generic_fileclose");
    if (--f->f_count > 0) {
        release(&_ftable.lock);
        return;
    }
    ff = *f;
    f->f_count = 0;
    f->f_type = FD_NONE;
    release(&_ftable.lock);

    if (ff.f_type == FD_PIPE) {
        int wrable = F_WRITEABLE(&ff);
        // pipeclose(ff.f_tp.f_pipe, wrable);
        pipe_close(ff.f_tp.f_pipe, wrable);
    } else if (ff.f_type == FD_INODE || ff.f_type == FD_DEVICE) {
        ff.f_tp.f_inode->i_op->iput(ff.f_tp.f_inode);
    } else if (ff.f_type == FD_SOCKET) {
        free_socket(ff.f_tp.f_sock);
    }
}

static inline const struct file_operations *get_fat32_fileops(void) {
    // Meyer's singleton
    static const struct file_operations fops_instance = {
        .dup = fat32_filedup,
        .read = fat32_fileread,
        .write = fat32_filewrite,
        .fstat = fat32_filestat,
        .readdir = fat32_getdents,
    };

    return &fops_instance;
}

static inline const struct file_operations *get_ext2_fileops(void) {
    ASSERT(0);
    return NULL;
}

// Not to be moved upward
const struct file_operations *(*get_fileops[])(void) = {
    [FAT32] get_fat32_fileops,
    [EXT2] get_ext2_fileops,
};

// == inode layer ==
static char *skepelem(char *path, char *name);
static struct inode *inode_namex(char *path, int nameeparent, char *name);

static char *skepelem(char *path, char *name) {
    // Examples:
    //   skepelem("a/bb/c", name) = "bb/c", setting name = "a"
    //   skepelem("///a//bb", name) = "bb", setting name = "a"
    //   skepelem("a", name) = "", setting name = "a"
    //   skepelem("", name) = skepelem("////", name) = 0

    //   skepelem("./mnt", name) = "", setting name = "mnt"
    //   skepelem("../mnt", name) = "", setting name = "mnt"
    //   skepelem("..", name) = "", setting name = 0
    char *s;
    int len;

    // while (*path == '/' || *path == '.')
    while (*path == '/')
        path++;
    if (*path == 0)
        return 0;
    s = path;
    while (*path != '/' && *path != 0)
        path++;
    len = path - s;
    if (len >= MAXPATH)
        memmove(name, s, MAXPATH);
    else {
        memmove(name, s, len);
        name[len] = 0;
    }
    while (*path == '/')
        path++;
    return path;
}

// return ip without ip->lock held, guarantee inode in memory
// if nameparent =0, we guarantee ip->parent also in memory
static struct inode *inode_namex(char *path, int nameeparent, char *name) {
    // printf("enter inode_namex!\n");
    struct inode *ip = NULL, *next = NULL, *cwd = proc_current()->cwd;
    // ASSERT(cwd);
    if (*path == '/') {
        // ASSERT(cwd->i_sb);
        // ASSERT(cwd->i_sb->root);
        struct inode *rip = cwd->i_sb->root;
        ip = rip->i_op->idup(rip);
    } else if (strncmp(path, "..", 2) == 0) {
        ip = cwd->parent->i_op->idup(cwd->parent);
    } else {
        ip = cwd->i_op->idup(cwd);
    }

    while ((path = skepelem(path, name)) != 0) {
        // printf("path:%s name:%s\n",path,name);
        // printf("namex: ip : %s ",ip->fat32_i.fname);
        // printf("sem.value %d\n",ip->i_sem.value);
        ip->i_op->ilock(ip);
        // printf("namex: LOCK 1 ok!\n");
        // printf("2\n");
        // printf("name %s i_mode 0x%x\n",ip->fat32_i.fname, ip->i_mode);
        if (!S_ISDIR(ip->i_mode)) {
            ip->i_op->iunlock_put(ip);
            // printf("3\n");
            return 0;
        }
        if (nameeparent && *path == '\0') {
            // Stop one level early.
            ip->i_op->iunlock(ip);
            // printf("ip %s sem.value: %d  unlocked~\n",ip->fat32_i.fname, ip->i_sem.value);
            // printf("4\n");
            return ip;
        }
        // printf("dirlook up\n");
        // if ((next = ip->i_op->idirlookup(ip, name, 0)) == 0) {
        //     ip->i_op->iunlock_put(ip);
        //     return 0;
        // }

        if (strncmp(name, "..", 2) == 0) {
            next = fat32_inode_dup(ip->parent);
        } else if (strncmp(name, ".", 1) == 0) {
            next = fat32_inode_dup(ip);
        } else {
            if ((next = fat32_inode_dirlookup(ip, name, 0)) == 0) {
                fat32_inode_unlock_put(ip);
                return 0;
            }
        }

        // printf("dirlook up ok!\n");

        ip->i_op->iunlock(ip);
        // printf("ip %s sem.value: %d  unlocked~\n",ip->fat32_i.fname, ip->i_sem.value);
        // if (likely(*path != '\0')) {
        //     ip->i_op->iput(ip);
        // }
        ip = next;
    }
    // printf("inode_namex got ip!\n");

    // ASSERT(ip->i_op);
    if (nameeparent) {
        ip->i_op->iput(ip);
        return 0;
    }

    // ASSERT(ip->parent->i_op);
    return ip;
}

struct inode *namei(char *path) {
    char name[NAME_LONG_MAX];
    return inode_namex(path, 0, name);
}

struct inode *namei_parent(char *path, char *name) {
    return inode_namex(path, 1, name);
}

// translate ip->mode to POSIX dirent.d_type(user level)
unsigned char __IMODE_TO_DTYPE(uint16 mode) {
    unsigned char dtype = DT_UNKNOWN;
    switch (mode & S_IFMT) {
    case S_IFREG: dtype = DT_REG; break;
    case S_IFDIR: dtype = DT_DIR; break;
    case S_IFCHR: dtype = DT_CHR; break;
    case S_IFBLK: dtype = DT_BLK; break;
    case S_IFIFO: dtype = DT_FIFO; break;
    case S_IFSOCK: dtype = DT_SOCK; break;
    default:
        break;
    }
    return dtype;
}

static inline const struct inode_operations *get_fat32_iops(void) {
    static const struct inode_operations iops_instance = {
        .iunlock_put = fat32_inode_unlock_put,
        .iunlock = fat32_inode_unlock,
        .iput = fat32_inode_put,
        .ilock = fat32_inode_lock,
        .iupdate = fat32_inode_update,
        .idirlookup = fat32_inode_dirlookup,
        .idempty = fat32_isdirempty,
        // .igetdents = fat32_getdents,
        .idup = fat32_inode_dup,
        .icreate = fat32_inode_create,
        .ipathquery = get_absolute_path,
        .iread = fat32_inode_read,
        .iwrite = fat32_inode_write,
        .ientrycopy = fat32_fcb_copy,
        .ientrydelete = fat32_fcb_delete,
    };

    return &iops_instance;
}

static inline const struct inode_operations *get_ext2_iops(void) {
    ASSERT(0);
    return NULL;
}

// Not to be moved upward
const struct inode_operations *(*get_inodeops[])(void) = {
    [FAT32] get_fat32_iops,
    [EXT2] get_ext2_iops,
};