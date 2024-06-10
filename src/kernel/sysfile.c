//
// File-system system calls.
// this sysfile implements oscomp fs syscalls based on fat32 interface
// Mostly argument checking, since we don't trust
// user code, and calls into fat32_file.c and fat32_inode.c.
//

#include "common.h"
#include "errno.h"
#include "lib/riscv.h"
#include "param.h"
#include "debug.h"
#include "atomic/spinlock.h"
#include "proc/pcb_life.h"
#include "fs/fcntl.h"
#include "kernel/trap.h"
#include "debug.h"
#include "memory/allocator.h"
#include "ipc/pipe.h"
#include "memory/vm.h"
#include "proc/tcb_life.h"
// #include "fs/fat/fat32_mem.h"
// #include "fs/fat/fat32_file.h"
#include "fs/vfs/fs.h"
#include "fs/vfs/ops.h"
#include "fs/stat.h"
#include "fs/uio.h"
#include "kernel/syscall.h"
#include "fs/ioctl.h"

#define FILE2FD(f, proc) (((char *)(f) - (char *)(proc)->ofile) / sizeof(struct file))
// Fetch the nth word-sized system call argument as a file descriptor
// and return both the descriptor and the corresponding struct file.
// 它是线程安全的
int argfd(int n, int *pfd, struct file **pf) {
    int fd;
    struct file *f;
    struct proc *p = proc_current();
    argint(n, &fd);
    if (fd < 0 || fd >= NOFILE)
        return -1;

    // in case another thread writes after the current thead reads
    sema_wait(&p->tlock);
    if ((f = proc_current()->ofile[fd]) == 0) {
        sema_signal(&p->tlock);
        return -1;
    } else {
        if (pfd)
            *pfd = fd;
        if (pf)
            *pf = f;
    }
    sema_signal(&p->tlock);
    return 0;
}

static inline int __namecmp(const char *s, const char *t) {
    return strncmp(s, t, MAXPATH);
}

// Allocate a file descriptor for the given file.
// Takes over file reference from caller on success.
// 它是线程安全的
int fdalloc(struct file *f) {
    int fd;
    struct proc *p = proc_current();

    sema_wait(&p->tlock);
    // for (fd = 0; fd < NOFILE; fd++) {
    for (fd = 0; fd < p->max_ofile; fd++) {
        if (p->ofile[fd] == 0) {
            p->ofile[fd] = f;
            sema_signal(&p->tlock);
            return fd;
        }
    }
    sema_signal(&p->tlock);
    return -1;
}

// return ip without ip->lock held
// if name == 0, return parent
// if name != 0, guarantee ip->parent in memory
static struct inode *find_inode(char *path, int dirfd, char *name) {
    // 如果path是相对路径，则它是相对于dirfd目录而言的。
    // 如果path是相对路径，且dirfd的值为AT_FDCWD，则它是相对于当前路径而言的。
    // 如果path是绝对路径，则dirfd被忽略。
    // 一般不对 path作检查
    // 如果name字段不为null，返回的是父目录的inode节点，并填充name字段
    // printf("enter find_inode!\n");
    // ASSERT(path);
    struct inode *ip;
    struct proc *p = proc_current();
    if (*path == '/' || dirfd == AT_FDCWD) {
        // 绝对路径 || 相对于当前路径，忽略 dirfd
        // acquire(&p->tlock);
        ip = (!name) ? namei(path) : namei_parent(path, name);
        // printf("about to leave find_inode!\n");
        if (ip == 0) {
            // release(&p->tlock);
            return 0;
        } else {
            // release(&p->tlock);
            return ip;
        }
    } else {
        // path为相对于 dirfd目录的路径
        struct file *f;
        // acquire(&p->tlock);
        if (dirfd < 0 || dirfd >= NOFILE || (f = p->ofile[dirfd]) == 0) {
            // release(&p->tlock);
            // printf("about to leave find_inode!\n");
            return 0;
        }
        struct inode *oldcwd = p->cwd;
        p->cwd = f->f_tp.f_inode;
        ip = (!name) ? namei(path) : namei_parent(path, name);
        if (ip == 0) {
            // release(&p->tlock);

            p->cwd = oldcwd;
            // printf("about to leave find_inode!\n");
            return 0;
        }
        p->cwd = oldcwd;
        // release(&p->tlock);
    }

    // printf("about to leave find_inode!\n");
    return ip;
}

// 下面为inode文件分配一个打开文件表项，为进程分配一个文件描述符
int assist_openat(struct inode *ip, int flags, int omode, struct file **fp) {
    // ASSERT(ip);
    // ASSERT(ip->fs_type == FAT32);
    struct file *f;
    int fd;

    if ((f = filealloc(ip->fs_type)) == 0 || (fd = fdalloc(f)) < 0) {
        if (f)
            generic_fileclose(f);
        ip->i_op->iunlock_put(ip);
        return -EMFILE; // for libc-test
    }

    // 下面为 f 结构体填充字段
    if (S_ISCHR(ip->i_mode) || S_ISBLK(ip->i_mode)) {
        // FD_DEVICE 型
        f->f_type = FD_DEVICE;
        f->f_major = MAJOR(ip->i_rdev);
    } else { // 暂不支持 FIFO，SOCKET
        // FD_INODE 型
        f->f_type = FD_INODE;
        f->f_pos = 0;
    }
    f->f_tp.f_inode = ip;
    f->f_flags = flags; // TODO(): &
    f->f_mode = omode & 0777;
    f->f_count = 1;

    if (((flags & O_TRUNC) == O_TRUNC) && S_ISREG(ip->i_mode)) {
        ip->i_size = 0;
        f->f_pos = 0;
    } else if (((flags & O_APPEND) == O_APPEND) && S_ISREG(ip->i_mode)) {
        f->f_pos = ip->i_size + 1;
    } else {
        f->f_pos = 0;
    }

    ip->i_op->iunlock(ip);

    if (fp)
        *fp = f; // get struct file pointer

    return fd;
}

static struct inode *assist_icreate(char *path, int dirfd, uint16 type, short major, short minor) {
    struct inode *dp = NULL, *ip = NULL;
    char name[MAXPATH] = {0};
    if ((dp = find_inode(path, dirfd, name)) == 0) {
        return 0;
    }
    ASSERT(dp->i_op);
    // dp->i_op->ilock(dp); // no need to lock dp !
    ip = dp->i_op->icreate(dp, name, type, major, minor); // don't check, caller will do this
    return ip;
}

// caller should hold self->lock
static void assist_unlink(struct inode *self) {
    // 可以彻底删除 self 文件
    if (self->i_nlink < 1) {
        panic("remove: nlink < 1");
    }
    --self->i_nlink;
    self->i_op->iupdate(self); // DON'T DELETE this code
                               // fat32 don't support hard link
                               // but other file system may support
    self->i_op->iunlock_put(self);
}

// caller should hold dp lock, ip lock
static void __unlink(struct inode *dp, struct inode *ip) {
    dp->i_op->ientrydelete(dp, ip);
    // printf("ok ?\n");
    assist_unlink(ip);
    // printf("ok !!\n");

    dp->i_op->iunlock_put(dp); // bug !!!
                               // dp unlock must after ip unlink
                               // because we have inode cache!
                               // fcb delete-> hash delete -> inode unlink(3 steps should be atomic)
}

//  The  getcwd()  function  copies  an  absolute  pathname
// of the current working directory to the array pointed to by buf,
// which is of length size.
// make sure sizeof buf is big enough to hold a absolute path!
static void assist_getcwd(char *kbuf) {
    // note: kbuf must have size PATH_LONG_MAX
    size_t n;
    if (!kbuf) return;
    // ASSERT(strnlen(buf,PATH_LONG_MAX) >= PATH_LONG_MAX);
    struct proc *p = proc_current();
    // get_absolute_path(p->cwd, kbuf);
    p->cwd->i_op->ipathquery(p->cwd, kbuf); // 获取绝对路径放在kbuf中
    n = strlen(kbuf);
    if (n > 1) {
        kbuf[n - 1] = 0; // clear '/'
    }
    return;
}

static inline uint64 do_lseek(struct file *f, off_t offset, int whence) {
    // ASSERT(f);
    if (f->f_type != FD_INODE) {
        return -1;
    }
    switch (whence) {
    case SEEK_SET:
        f->f_pos = offset;
        break;
    case SEEK_CUR:
        f->f_pos += offset;
        break;
    case SEEK_END:
        f->f_pos = f->f_tp.f_inode->i_size + offset;
        break;
    default:
        panic("error type\n");
        // f->f_pos = offset;
        // break;
    }
// if(f->f_pos==-1) {
//     printf("ready\n");
// }
// debug!!!
#ifdef __DEBUG_RW__
    printfRed("lseek : pid : %d, lseek inode file %s to %d \n", proc_current()->pid, f->f_tp.f_inode->fat32_i.fname, MAX(0, f->f_pos)); // debug
#endif
    return MAX(0, f->f_pos);
}

// incomplete implement
static int do_faccess(struct inode *ip, int mode, int flags) {
    // ASSERT(flags == AT_EACCESS);
    // ip->i_op->ilock(ip);
    if (mode == F_OK) {
        // 已经存在
        // ip->i_op->iunlock_put(ip);
        return 0;
    }
    if (mode & R_OK || mode & W_OK) {
        ;
    }
    if (mode & X_OK) {
        // sorry
        ;
    }

    // ip->i_op->iunlock_put(ip);
    return 0;
}

// 如果offset不为NULL，则不会更新in_fd的pos,否则pos会更新，offset也会被赋值
static uint64 do_sendfile(struct file *rf, struct file *wf, off_t __user *poff, size_t count) {
    void *kbuf;
    struct inode *rdip, *wrip;
    off_t offset;
    ssize_t nread, nwritten;
    // bug like this :
    // kbuf = kmalloc(PGSIZE)
    // printfBlue("count : %ldkbuf = kzalloc(count)B, %ldKB, %ldMB\n",count, count/1024, count/1024/1024);
    if ((kbuf = kzalloc(count)) == 0) {
        return -1;
    }
    if (poff) {
        either_copyin(&offset, 1, (uint64)poff, sizeof(off_t));
    } else {
        offset = rf->f_pos;
    }
    rdip = rf->f_tp.f_inode;
    wrip = wf->f_tp.f_inode;
    if ((nread = rdip->i_op->iread(rdip, 0, (uint64)kbuf, offset, count)) <= 0) {
        goto bad;
    }

    if (wf->f_type == FD_PIPE) {
        // if ((nwritten = pipewrite(wf->f_tp.f_pipe, 0, (uint64)kbuf, count)) < 0) {
        //     goto bad;
        // }
        if ((nwritten = pipe_write(wf->f_tp.f_pipe, 0, (uint64)kbuf, count)) < 0) {
            goto bad;
        }
    } else if (wf->f_type == FD_INODE) {
        if ((nwritten = wrip->i_op->iwrite(wrip, 0, (uint64)kbuf, wf->f_pos, MIN(count, nread))) < 0) {
            goto bad;
        }
    } else {
        // unsupported file type
        goto bad;
    }

    offset += nread;

    if (poff) {
        either_copyout(1, (uint64)poff, &offset, sizeof(offset));
    } else {
        rf->f_pos += nread;
        wf->f_pos += nwritten;
    }
    kfree(kbuf);
    return nwritten;

bad:
    if (kbuf) {
        kfree(kbuf);
    }
    return -1;
}

static uint64 do_renameat2(struct inode *ip, int newdirfd, char *newpath, int flags) {
    char name[MAXPATH];
    struct inode *dp, *parent;
    ASSERT(flags == 0);
    struct inode *newip;
    newip = find_inode(newpath, newdirfd, 0);
    if (unlikely(ip == newip)) {
        // 指向同一个文件
        return 0;
    }

    ip->i_op->ilock(ip);
    if (likely(!newip)) {
        // 新文件不存在
        goto create;
    }

    // 新文件已存在
    // 若 ip 指向一个文件而不是目录
    if (S_ISREG(ip->i_mode)) {
        // 则 newip 若存在，不能指向目录
        newip->i_op->ilock(newip);
        if (S_ISDIR(newip->i_mode)) {
            newip->i_op->iunlock_put(newip);
            ip->i_op->iunlock_put(ip);
            return -1;
        } else {
            // 删除, 然后创建
            ASSERT(newip->parent->i_sem.value > 0);
            newip->parent->i_op->ilock(newip->parent);
            // assist_unlink(newip);    // error! do not use this
            __unlink(newip->parent, newip);
            goto create;
        }
    } else if (S_ISDIR(ip->i_mode)) {
        // 若 ip 指向目录
        // 则 newip 若存在，必须指向一个空目录
        newip->i_op->ilock(newip);
        if (!S_ISDIR(newip->i_mode) || !newip->i_op->idempty(newip)) {
            newip->i_op->iunlock_put(newip);
            ip->i_op->iunlock_put(ip);
            return -1;
        } else {
            // 删除，然后创建
            ASSERT(newip->parent->i_sem.value > 0);
            newip->parent->i_op->ilock(newip->parent);
            // assist_unlink(newip);    // error! do not use this
            __unlink(newip->parent, newip);
            goto create;
        }
    }

create:
    // 1. 拷贝目录项 entry
    if ((dp = find_inode(newpath, newdirfd, name)) == 0) {
        ip->i_op->iunlock_put(ip);
        return -1;
    }
    // ip: /A/a.txt
    // dp: /A/B

    // mv /A/a.txt /A/B/a.txt => /A/B/a.txt
    dp->i_op->ilock(dp);
    if (dp->i_op->ientrycopy(dp, ip) < 0) {
        dp->i_op->iunlock_put(dp);
        ip->i_op->iunlock_put(ip);
        return -1;
    }
    dp->i_op->iunlock_put(dp);

    // 2. 删除原目录项entry（不删除文件数据）
    ASSERT(ip->parent->i_op);
    parent = ip->parent;
    ASSERT(parent->i_sem.value > 0);
    parent->i_op->ilock(parent);
    parent->i_op->ientrydelete(parent, ip);
    parent->i_op->iunlock_put(parent);
    ip->i_op->iunlock_put(ip);

    return 0;
}

static int assist_dupfd(struct file *f) {
    int newfd;
    if ((newfd = fdalloc(f)) > 0) {
        f->f_op->dup(f);
    }
    return newfd;
}

// static int assist_getfd(struct file *f) {
// int ret;
// struct proc *p = proc_current();
// Log("%p %p %d", (char *)f, (char *)(p)->ofile, sizeof(struct file));
// ret = ((char *)(f) - (char *)((p)->ofile)) / sizeof(struct file);
// ret = FILE2FD(f, p);
// ASSERT(ret >= 0 && ret < NOFILE);
// return ret;
// }

// 不做参数检查
// 一定返回 newfd
static int assist_setfd(struct file *f, int oldfd, int newfd) {
    struct proc *p = proc_current();
    if (oldfd == newfd) {
        // do nothing
        return EINVAL;
        // return newfd;
    }
    sema_wait(&p->tlock); // 可以修改为粒度小一些的锁;可以往_file结构里加锁，或者fdtable
    if (p->ofile[newfd] == 0) {
        // not used, great!
        p->ofile[newfd] = f;
        sema_signal(&p->tlock);
    } else {
        // close and reuse
        // two steps must be atomic!
        generic_fileclose(p->ofile[newfd]);
        p->ofile[newfd] = f;
        sema_signal(&p->tlock);
    }
    // fat32_filedup(f);
    f->f_op->dup(f);
    return newfd;
}

static inline int assist_getflags(struct file *f) {
    return f->f_flags;
}

static int assist_setflag(struct file *f, int flag) {
    if (FCNTLABLE(flag)) {
        f->f_flags |= flag;
        f->f_pos = f->f_tp.f_inode->i_size + 1; // bugs for libc-test ftello_unflushed_append
        return 0;
    }
    return -1;
}

static uint64 do_fcntl(struct file *f, int cmd) {
    int ret, arg;
    switch (cmd) {
    case F_DUPFD:
        ret = assist_dupfd(f);
        break;

    case F_GETFD:
        // ret = assist_getfd(f);
        ret = f->f_flags;
        break;

    case F_SETFD:
        ret = 0;
        // f->f_flags |= FD_CLOEXEC;
        break;

    case F_GETFL:
        ret = assist_getflags(f);
        break;

    case F_SETFL:
        if (argint(2, &arg) < 0) {
            ret = -1;
        } else {
            ret = assist_setflag(f, arg);
        }
        break;

    case F_DUPFD_CLOEXEC:
        ret = assist_dupfd(f);
        // if (ret >= 0) {
        //     proc_current()->ofile[ret]->f_flags |= FD_CLOEXEC;
        // }
        break;
    default:
        ret = 0;
        break;
    }

    return ret;
}

uint64 sys_mknod(void) {
    struct inode *ip;
    char path[MAXPATH];
    int dev, mode;

    if (argstr(0, path, MAXPATH) < 0) {
        return -1;
    }
    argint(1, &mode);
    argint(2, &dev);
    if (!S_ISCHR(mode) && !S_ISBLK(mode)) {
        return -1;
    }
    // TODO(): futuer should do dev check.
    uint16 type = mode & S_IFMT;
    if ((ip = assist_icreate(path, AT_FDCWD, type, MAJOR(dev), MINOR(dev))) == 0) {
        return -1;
    }

    ip->i_op->iunlock_put(ip);
    return 0;
}

// 功能：获取当前工作目录；
// 输入：
// - char *buf：一块缓存区，用于保存当前工作目录的字符串。当buf设为NULL，由系统来分配缓存区。
// - size：buf缓存区的大小。
// 返回值：成功执行，则返回当前工作目录的字符串的指针。失败，则返回NULL。
//  The  getcwd()  function  copies  an  absolute  pathname
// of the current working directory to the array pointed to by buf,
// which is of length size.
// Maybe we need dirent ?
uint64 sys_getcwd(void) {
    uint64 buf;
    size_t size;
    struct proc *p = proc_current();
    argaddr(0, &buf);
    argulong(1, &size);

    char kbuf[MAXPATH];
    memset(kbuf, 0, MAXPATH); // important! init before use!
    // fat32_getcwd(kbuf);
    assist_getcwd(kbuf);
    if (!buf && (buf = (uint64)kalloc()) == 0) {
        return (uint64)NULL;
    }

    if (copyout(p->mm->pagetable, buf, kbuf, strnlen(kbuf, MAXPATH) + 1) < 0) { // rember add 1 for '\0'
        return (uint64)NULL;
    } else {
        return buf;
    }
}

// 功能：复制文件描述符；
// 输入：
// - fd：被复制的文件描述符。
// 返回值：成功执行，返回新的文件描述符。失败，返回-1。
uint64 sys_dup(void) {
    struct file *f;
    int fd;

    if (argfd(0, 0, &f) < 0)
        return -1;
    if ((fd = fdalloc(f)) < 0)
        return -EMFILE;

    // fat32_filedup(f);
    ASSERT(f->f_op);
    f->f_op->dup(f);
    return fd;
}

// 功能：复制文件描述符，并指定了新的文件描述符；
// 输入：
// - old：被复制的文件描述符。
// - new：新的文件描述符。
// 返回值：成功执行，返回新的文件描述符。失败，返回-1。
// Mention:
//    If the file descriptor newfd was previously open, it is silently closed before being reused.
//    The steps of closing and reusing the file descriptor newfd are performed  atomically.
uint64 sys_dup3(void) {
    struct file *f;
    int oldfd, newfd, flags;

    if (argfd(0, &oldfd, &f) < 0) {
        return -1;
    }
    argint(1, &newfd);
    if (newfd < 0 || newfd >= NOFILE) {
        return -1;
    }
    argint(2, &flags);
    ASSERT(flags == 0);

    newfd = assist_setfd(f, oldfd, newfd);
    return newfd;
}

// * 功能：挂载文件系统；
// * 输入：
//   - special: 挂载设备；
//   - dir: 挂载点；
//   - fstype: 挂载的文件系统类型；
//   - flags: 挂载参数；
//   - data: 传递给文件系统的字符串参数，可为NULL；
// * 返回值：成功返回0，失败返回-1；
// uint64 sys_mount(void) {
//     return 0;
// }

// 功能：打开或创建一个文件；
// 输入：
// - fd：文件所在目录的文件描述符。
// - filename：要打开或创建的文件名。如为绝对路径，则忽略fd。如为相对路径，且fd是AT_FDCWD，则filename是相对于当前工作目录来说的。如为相对路径，且fd是一个文件描述符，则filename是相对于fd所指向的目录来说的。
// - flags：必须包含如下访问模式的其中一种：O_RDONLY，O_WRONLY，O_RDWR。还可以包含文件创建标志和文件状态标志。
// - mode：文件的所有权描述。详见`man 7 inode `。
// 返回值：成功执行，返回新的文件描述符。失败，返回-1。
uint64 sys_openat(void) {

    char path[MAXPATH];
    int dirfd, flags, omode, fd;
    struct inode *ip;
    argint(0, &dirfd); // no need to check dirfd, because dirfd maybe AT_FDCWD(<0)
                       // find_inode() will do the check
    if (argstr(1, path, MAXPATH) < 0) {
        return -1;
    }
    argint(2, &flags);
    flags = flags & (~O_LARGEFILE); // bugs!!

    argint(3, &omode);

    // if(!strncmp(path, "/etc/localtime", 14)) {
    //     printf("ready\n");
    //     printfGreen("openat %s begin, mm: %d pages\n", path, get_free_mem()/4096);
    // }


    // 如果是要求创建文件，则调用 create
    if ((flags & O_CREAT) == O_CREAT) {
        if ((ip = assist_icreate(path, dirfd, S_IFREG, 0, 0)) == 0) {
            return -1;
        }
    } else {
        // 否则，我们先调用 find_inode 找到 path 对应的文件 inode 节点
        if ((ip = find_inode(path, dirfd, 0)) == 0) {
            return -1;
        }
        // ASSERT(ip->i_op);
        ip->i_op->ilock(ip);
        // if (ip->i_mode == S_IFDIR && flags != O_RDONLY) {
        //     fat32_inode_unlock_put(ip);
        //     return -1;
        // }

        if (((flags & O_DIRECTORY) == O_DIRECTORY) && !S_ISDIR(ip->i_mode)) {
            ip->i_op->iunlock_put(ip);
            return -1;
        }
    }

    if ((S_ISCHR(ip->i_mode) || S_ISBLK(ip->i_mode))
        && (MAJOR(ip->i_rdev) < 0 || MAJOR(ip->i_rdev) >= NDEV)) {
        ip->i_op->iunlock_put(ip);
        return -1;
    }

    fd = assist_openat(ip, flags, omode, 0);

    // printfGreen("openat %s end, mm: %d pages\n", path, get_free_mem()/4096);
    // printfRed("mm : %d pages\n", get_free_mem() / PGSIZE);
    return fd;
}

// 功能：关闭一个文件描述符；
// 输入：
// - fd：要关闭的文件描述符。
// 返回值：成功执行，返回0。失败，返回-1。
uint64 sys_close(void) {
    int fd;
    struct file *f;

    if (argfd(0, &fd, &f) < 0) {
        return -1;
    }
    proc_current()->ofile[fd] = 0;

#ifdef __DEBUG_FS__
    printfCYAN("close : filename : %s, pid %d, fd = %d\n", f->f_tp.f_inode->fat32_i.fname, proc_current()->pid, fd);
#endif

    // debug ！！！
    // printfCYAN("close begin: filename : %s, pid %d, fd = %d\n", f->f_tp.f_inode->fat32_i.fname, proc_current()->pid, fd);

    generic_fileclose(f);

    // debug ！！！
    // printfCYAN("close end: filename : %s, pid %d, fd = %d\n", f->f_tp.f_inode->fat32_i.fname, proc_current()->pid, fd);
    // printfRed("mm : %d pages\n", get_free_mem() / PGSIZE);
    return 0;
}

// 功能：从一个文件描述符中读取；
// 输入：
// - fd：要读取文件的文件描述符。
// - buf：一个缓存区，用于存放读取的内容。
// - count：要读取的字节数。
// 返回值：成功执行，返回读取的字节数。如为0，表示文件结束。错误，则返回-1。
uint64 sys_read(void) {
    struct file *f;
    int count;
    uint64 buf;

    argaddr(1, &buf);
    if (argint(2, &count) < 0) {
        return -1;
    }
    if (argfd(0, 0, &f) < 0)
        return -1;
    if (!F_READABLE(f))
        return -1;

#ifdef __DEBUG_FS__
    if (f->f_type == FD_INODE) {
        int fd;
        argint(0, &fd);
        printfMAGENTA("read : pid %d, fd = %d\n", proc_current()->pid, fd);
    }
#endif
    // static int read_cnt = 0;
    // read_cnt ++;
    // if(read_cnt%1000==0) {
    //     printf("sys_read, cnt : %d\n",read_cnt);
    // }
    // printfRed("read before, file name : %s, mm: %d pages\n", f->f_tp.f_inode->fat32_i.fname, get_free_mem()/4096);
    int retval = f->f_op->read(f, buf, count);
    // printfRed("read after, file name : %s, mm: %d pages\n", f->f_tp.f_inode->fat32_i.fname, get_free_mem()/4096);
    return retval;
}

// 功能：从一个文件描述符中写入；
// 输入：
// - fd：要写入文件的文件描述符。
// - buf：一个缓存区，用于存放要写入的内容。
// - count：要写入的字节数。
// 返回值：成功执行，返回写入的字节数。错误，则返回-1。
uint64 sys_write(void) {
    struct file *f;
    int n, fd;
    uint64 p;

    argaddr(1, &p);
    argint(2, &n);
    if (argfd(0, &fd, &f) < 0) // fd for debug
        return -1;
    if (!F_WRITEABLE(f))
        return -1;

    return f->f_op->write(f, p, n);
}

// 功能：创建文件的链接；
// 输入：
// - olddirfd：原来的文件所在目录的文件描述符。
// - oldpath：文件原来的名字。如果oldpath是相对路径，则它是相对于olddirfd目录而言的。如果oldpath是相对路径，且olddirfd的值为AT_FDCWD，则它是相对于当前路径而言的。如果oldpath是绝对路径，则olddirfd被忽略。
// - newdirfd：新文件名所在的目录。
// - newpath：文件的新名字。newpath的使用规则同oldpath。
// - flags：在2.6.18内核之前，应置为0。其它的值详见`man 2 linkat`。
// 返回值：成功执行，返回0。失败，返回-1。
uint64 sys_linkat(void) {
    char newpath[MAXPATH], oldpath[MAXPATH];
    int olddirfd, newdirfd, flags;

    if (argstr(1, oldpath, MAXPATH) < 0 || argstr(3, newpath, MAXPATH) < 0) {
        return -1;
    }
    argint(0, &olddirfd);
    argint(2, &newdirfd);
    argint(4, &flags);
    ASSERT(flags == 0);

    char oldname[MAXPATH], newname[MAXPATH];
    struct inode *oldip, *newdp;
    if ((newdp = find_inode(newpath, newdirfd, newname)) == 0) {
        // 新路径的目录不存在
        return -1;
    }
    if ((oldip = find_inode(oldpath, olddirfd, oldname)) == 0) {
        // 旧文件的inode不存在
        return -1;
    }

    // if (fat32_inode_dirlookup(newdp, newname, 0) != 0)
    if (newdp->i_op->idirlookup(newdp, newname, 0) != 0) {
        // 新文件已存在
        return 0;
    }

    // unfinished ! but will not implement
    // FAT32 don't support hard link
    // 往 newdp 中 写入一个代表 oldip 的项
    // TODO()
    // if ( fat32_dirlink(olddp, newdp) < 0) {
    //     return -1;
    // }

    return 0;
}

// 功能：移除指定文件的链接(可用于删除文件)；
// 输入：
// - dirfd：要删除的链接所在的目录。
// - path：要删除的链接的名字。如果path是相对路径，则它是相对于dirfd目录而言的。如果path是相对路径，且dirfd的值为AT_FDCWD，则它是相对于当前路径而言的。如果path是绝对路径，则dirfd被忽略。
// - flags：可设置为0或AT_REMOVEDIR。
// 返回值：成功执行，返回0。失败，返回-1。
// TODO: need to recify
uint64 sys_unlinkat(void) {
    // static int hit = 0;
    struct inode *ip, *dp;
    char name[NAME_LONG_MAX], path[MAXPATH];
    int dirfd, flags;
    argint(0, &dirfd); // don't need to check, find_inode() will do this

    argint(2, &flags);
    // ASSERT(flags == 0);
    if (argstr(1, path, MAXPATH) < 0 || __namecmp(path, "/") == 0)
        return -1;
    // printfRed("unlinkat , %s\n", path);
    // printf("unlinkat hit = %d name = %s\n",++hit,path);

    if ((dp = find_inode(path, dirfd, name)) == 0) {
        return ENOENT;
    }
    // printf("unlinkat: %d :find inode ok!\n",hit);
    if (__namecmp(name, ".") == 0 || __namecmp(name, "..") == 0) {
        //  error: cannot unlink "." or "..".
        return -1;
    }

    // printfRed("unlinkat1, mm : %d pages\n", get_free_mem() / PGSIZE);
    dp->i_op->ilock(dp);
    if ((ip = dp->i_op->idirlookup(dp, name, 0)) == 0) {
        // error: target file not found
        // printf("goto here1.\n");
        dp->i_op->iunlock_put(dp);
        return -1;
    }
    // printf("goto here2.\n");
    ip->i_op->ilock(ip);
    if ((flags == 0 && S_ISDIR(ip->i_mode))
        || (flags == AT_REMOVEDIR && !S_ISDIR(ip->i_mode))) {
        ip->i_op->iunlock_put(ip);
        dp->i_op->iunlock_put(dp);
        return -1;
    }

    if (ip->i_nlink < 1) {
        panic("unlink: nlink < 1");
    }

    if (S_ISDIR(ip->i_mode) && !ip->i_op->idempty(ip)) {
        // error: trying to unlink a non-empty directory
        // printf("ip type : 0x%x  name: %s\n", ip->i_mode, ip->fat32_i.fname);
        // printf("不会来到这里吧！\n");
        ip->i_op->iunlock_put(ip); //     bug!!!
        dp->i_op->iunlock_put(dp); //     bug!!!
        return -1;
    }

    __unlink(dp, ip);
    /*
    // --- be replaced by __unlink() ---
    dp->i_op->ientrydelete(dp, ip);
    // printf("ok ?\n");
    assist_unlink(ip);
    // printf("ok !!\n");

    dp->i_op->iunlock_put(dp); // bug !!!
                               // dp unlock must after ip unlink
                               // because we have inode cache!
                               // fcb delete-> hash delete -> inode unlink(3 steps should be atomic)
    // ---
*/
    // printfRed("unlinkat2, mm : %d pages\n", get_free_mem() / PGSIZE);
    return 0;
}

// 功能：创建目录；
// 输入：
// - dirfd：要创建的目录所在的目录的文件描述符。
// - path：要创建的目录的名称。如果path是相对路径，则它是相对于dirfd目录而言的。如果path是相对路径，且dirfd的值为AT_FDCWD，则它是相对于当前路径而言的。如果path是绝对路径，则dirfd被忽略。
// - mode：文件的所有权描述。详见`man 7 inode `。
// 返回值：成功执行，返回0。失败，返回-1。
uint64 sys_mkdirat(void) {
    char path[MAXPATH];
    int dirfd;
    mode_t mode;
    struct inode *ip;
    argint(0, &dirfd);
    if (argint(2, (int *)&mode) < 0) {
        return -1;
    }

    if (argstr(1, path, MAXPATH) < 0) {
        return -1;
    }
    if ((ip = assist_icreate(path, AT_FDCWD, S_IFDIR, 0, 0)) == 0) {
        return -1;
    }
    /*
        -> these two steps should have been necessary
        -> but fat32 can not store the message
    ip->i_mode |= (mode & 0777);
    ip->i_op->iupdate(ip);
    */

    ip->i_op->iunlock_put(ip);
    return 0;
}

/*
struct dirent {
    uint64 d_ino;	// 索引结点号
    int64 d_off;	// 到下一个dirent的偏移
    unsigned short d_reclen;	// 当前dirent的长度
    unsigned char d_type;	// 文件类型
    char d_name[];	//文件名
};
*/

// 功能：获取目录的条目;
// 输入：
// - fd：所要读取目录的文件描述符。
// - buf：一个缓存区，用于保存所读取目录的信息。
// - len：buf的大小。
// 返回值：成功执行，返回读取的字节数。当到目录结尾，则返回0。失败，则返回-1。

#define END_DIR -1
uint64 sys_getdents64(void) {
    struct file *f;
    uint64 buf; // user pointer to struct dirent
    int len;
    ssize_t nread, sz;
    char *kbuf;
    struct inode *ip;

    if (argfd(0, 0, &f) < 0) {
        return -1;
    }

    if (f->f_type != FD_INODE) {
        return -1;
    }
    ip = f->f_tp.f_inode;
    ASSERT(ip);
    ip->i_op->ilock(ip);
    if (!S_ISDIR(ip->i_mode)) {
        goto bad_ret;
    }
    if (f->f_pos == END_DIR) {
        ip->i_op->iunlock(ip);
        return 0;
    }
    ip->i_op->iunlock(ip);

    argaddr(1, &buf);
    argint(2, &len);
    if (len < 0) {
        goto bad_ret;
    }
    // sz = MAX(f->f_tp.f_inode->i_sb->cluster_size, len);
    sz = len;
    if ((kbuf = kzalloc(sz)) == 0) {
        goto bad_ret;
    }

    // if ((nread = f->f_op->readdir(f, (char *)kbuf, 0, sz)) < 0) {
    if ((nread = f->f_op->readdir(ip, (char *)kbuf, 0, sz)) < 0) {
        /*
            readdir:
            从 f->pos 的偏移开始，尽可能地读取目录项，以填充 kbuf。
            返回读取的字节数。
            会将 f->pos 后移，移动的大小等于目录项的个数
            如果目录项读完，则将 f->pos 置为 END_DIR
        */
        kfree(kbuf);
        goto bad_ret;
    }

    if (either_copyout(1, buf, kbuf, nread) < 0) {
        kfree(kbuf);
        goto bad_ret;
    }
    kfree(kbuf);

    f->f_pos = END_DIR; // !!!
    return nread;

bad_ret:
    return -1;
}

/* 一个可用但不正确的版本
uint64 sys_getdents64(void) {
    struct file *f;
    uint64 buf; // user pointer to struct dirent
    int len;
    ssize_t nread, sz;
    char *kbuf;
    struct inode *ip;

    if (argfd(0, 0, &f) < 0)
        return -1;

    if (f->f_type != FD_INODE) {
        return -1;
    }
    ip = f->f_tp.f_inode;
    ASSERT(ip);
    ip->i_op->ilock(ip);
    if (!S_ISDIR(ip->i_mode)) {
        goto bad_ret;
    }
    if (f->f_pos == ip->i_size) {
        ip->i_op->iunlock(ip);
        return 0;
    }
    argaddr(1, &buf);
    argint(2, &len);
    if (len < 0) {
        goto bad_ret;
    }
    sz = MAX(f->f_tp.f_inode->i_sb->cluster_size, len);
    if ((kbuf = kzalloc(sz)) == 0) {
        goto bad_ret;
    }
    memset(kbuf, 0, len); // !!!!
    ASSERT(ip->i_op);
    ASSERT(ip->i_op->igetdents);
    // TODO : modify offset
    if ((nread = ip->i_op->igetdents(ip, kbuf, 0, sz)) < 0) {
        kfree(kbuf);
        goto bad_ret;
    }
    len = MIN(len, nread);
    if (either_copyout(1, buf, kbuf, len) < 0) { // copy lenth may less than nread
        kfree(kbuf);
        goto bad_ret;
    }
    kfree(kbuf);
    f->f_pos = ip->i_size;
    ip->i_op->iunlock(ip);
    return nread;

bad_ret:
    ip->i_op->iunlock(ip);
    return -1;
}
*/

// 功能：获取文件状态；
// 输入：
// - fd: 文件句柄；
// - kst: 接收保存文件状态的指针；
// 返回值：成功返回0，失败返回-1；
uint64 sys_fstat(void) {
    struct file *f;
    uint64 st; // user pointer to struct kstat

    argaddr(1, &st);
    if (argfd(0, 0, &f) < 0)
        return -1;

    return f->f_op->fstat(f, st);
}

// 功能：切换工作目录；
// 输入：
// - path：需要切换到的目录。
// 返回值：成功执行，返回0。失败，返回-1。
uint64 sys_chdir(void) {
    char path[MAXPATH];
    struct inode *ip;
    struct proc *p = proc_current();

    if (argstr(0, path, MAXPATH) < 0 || (ip = namei(path)) == 0) { // bug: 修改了n_link
        return -1;
    }

    ip->i_op->ilock(ip); // bug:修改了 i_mode
    // if (ip->i_mode != S_IFDIR) {
    if (!S_ISDIR(ip->i_mode)) {
        ip->i_op->iunlock_put(ip);
        return -1;
    }

    ip->i_op->iunlock(ip);
    p->cwd = ip;
    return 0;
}

// 功能：创建管道；
// 输入：
// - fd[2]：用于保存2个文件描述符。其中，fd[0]为管道的读出端，fd[1]为管道的写入端。
// 返回值：成功执行，返回0。失败，返回-1。
uint64 sys_pipe2(void) {
    uint64 fdarray; // user pointer to array of two integers
    struct file *rf, *wf;
    int fd0, fd1;
    struct proc *p = proc_current();

    argaddr(0, &fdarray);
    // if (pipealloc(&rf, &wf) < 0) // 分配两个 pipe 文件
    //     return -1;
    if (pipe_alloc(&rf, &wf) < 0) // 分配两个 pipe 文件
        return -1;
    fd0 = -1;
    if ((fd0 = fdalloc(rf)) < 0 || (fd1 = fdalloc(wf)) < 0) { // 给当前进程分配两个文件描述符，代指那两个管道文件
        if (fd0 >= 0)
            p->ofile[fd0] = 0;
        generic_fileclose(rf);
        generic_fileclose(wf);
        return -EMFILE;
    }
    if (copyout(p->mm->pagetable, fdarray, (char *)&fd0, sizeof(fd0)) < 0
        || copyout(p->mm->pagetable, fdarray + sizeof(fd0), (char *)&fd1, sizeof(fd1)) < 0) {
        p->ofile[fd0] = 0;
        p->ofile[fd1] = 0;
        generic_fileclose(rf);
        generic_fileclose(wf);
        return -1;
    }
    return 0;
}

// pseudo implement
uint64 sys_umount2(void) {
    // ASSERT(0);
    return 0;
}

// pseudo implement
uint64 sys_mount(void) {
    // ASSERT(0);
    return 0;
}

/* busybox */

// 功能：从一个文件描述符中写入；
// 输入：
// - fd：要写入文件的文件描述符。
// - iov：一个缓存区，存放 若干个 struct iove
// - iovcnt：iov 缓冲中的结构体个数
// 返回值：成功执行，返回写入的字节数。错误，则返回-1。
uint64 sys_writev(void) {
    struct file *f;
    int iovcnt;
    ssize_t nwritten = 0;
    uint64 iov;
    void *kbuf;
    struct iovec *p;

    argaddr(1, &iov);
    if (argint(2, &iovcnt) < 0) {
        return -1;
    }
    if (iovcnt == 0) {
        return 0;
    }
    if (argfd(0, 0, &f) < 0) {
        return -1;
    }

    if (f->f_type == FD_INODE) {
        struct inode *ip;
        ip = f->f_tp.f_inode;
        ip->i_op->ilock(ip);
        if (S_ISDIR(ip->i_mode)) {
            // writev 不应该写目录
            ip->i_op->iunlock(ip);
            return -1;
        }
        ip->i_op->iunlock(ip);
    }

    int totsz = sizeof(struct iovec) * iovcnt;
    if ((kbuf = kzalloc(totsz)) == 0) {
        goto bad;
    }

    if (either_copyin(kbuf, 1, iov, totsz) < 0) {
        goto bad;
    }

    int nw = 0;
    p = (struct iovec *)kbuf;
    for (int i = 0; i != iovcnt; ++i) {
        if ((nw = f->f_op->write(f, (uint64)p->iov_base, p->iov_len)) < 0) {
            goto bad;
        }
        nwritten += nw;
        ++p;
    }

    kfree(kbuf);
    return nwritten;

bad:
    if (likely(kbuf)) {
        kfree(kbuf);
    }
    return -1;
}

// 功能：从一个文件描述符中读入；
// 输入：
// - fd：要读取文件的文件描述符。
// - iov：一个缓存区，存放 若干个 struct iove
// - iovcnt：iov 缓冲中的结构体个数
// 返回值：成功执行，返回读取的字节数。错误，则返回-1。
// struct iovec {
//     void  *iov_base;    /* Starting address */
//     size_t iov_len;     /* Number of bytes to transfer */
// };
uint64 sys_readv(void) {
    struct file *f;
    int iovcnt;
    ssize_t nread = 0;
    uint64 iov;
    void *kbuf;
    struct iovec *p;

    argaddr(1, &iov);
    if (argint(2, &iovcnt) < 0) {
        return -1;
    }
    if (argfd(0, 0, &f) < 0) {
        return -1;
    }
    int totsz = sizeof(struct iovec) * iovcnt;
    if ((kbuf = kmalloc(totsz)) == 0) {
        goto bad;
    }

    if (either_copyin(kbuf, 1, iov, totsz) < 0) {
        goto bad;
    }

    int nr = 0, filesz = f->f_tp.f_inode->i_size;

    p = (struct iovec *)kbuf;

    struct proc *p_current = proc_current();
    // special for urandom
    if (!strncmp(f->f_tp.f_inode->fat32_i.fname, "urandom", 7)) {
        for (int i = 0; i != iovcnt; i++) {
            uchar *buf_tmp;
            if ((buf_tmp = kmalloc(p->iov_len)) == NULL) {
                panic("sys_readv : no free space\n");
            }
            copyout(p_current->mm->pagetable, (uint64)p->iov_base, (void *)buf_tmp, p->iov_len);
            kfree(buf_tmp);
            nread += p->iov_len;
            ++p;
        }
    } else {
        // general process
        for (int i = 0; i != iovcnt && filesz > 0; ++i) {
            if ((nr = f->f_op->read(f, (uint64)p->iov_base, MIN(p->iov_len, filesz))) < 0) {
                goto bad;
            }
            nread += nr;
            filesz -= nr;
            ++p;
        }
    }

    kfree(kbuf);
    return nread;

bad:
    if (likely(kbuf)) {
        kfree(kbuf);
    }
    return -1;
}

// 功能：重定位文件的位置指针；
// 输入：
// - fd：要调整文件的文件描述符。
// - offset：偏移数值
// - whence：从何处起始
// 返回值：成功执行，返回偏移的字节数。错误，则返回-1。
uint64 sys_lseek(void) {
    struct file *f;
    off_t offset;
    int whence;

    arglong(1, &offset);
    //     return -1;
    // }
    // bug !!!
    // if (argint(1, (int *)&offset) < 0) {
    //     return -1;
    // }
    if (argfd(0, 0, &f) < 0) {
        return -1;
    }
    argint(2, &whence);
    return do_lseek(f, offset, whence);
}

// 功能：改变已经打开文件的属性
// 输入：
// - fd：文件所在目录的文件描述符。
// - cmd：功能描述
// - arglist:
// 返回值：成功执行，返回值依赖于cmd。错误，则返回-1。
uint64 sys_fcntl(void) {
    struct file *f;
    int cmd;
    if (argfd(0, 0, &f) < 0) {
        return -1;
    }
    argint(1, &cmd);
    return do_fcntl(f, cmd);
}

// 功能：检查进程实际用户ID和实际组ID对文件的访问权限；
// 输入：
// - dirfd：文件所在目录的文件描述符。
// - pathname：文件路径
// - mode: 测试权限，如 F_OK、R_OK、W_OK、X_OK
// - flags:
// 返回值：成功执行，返回0。错误，则返回-1。
uint64 sys_faccessat(void) {
    int dirfd, mode, flags;
    char pathname[MAXPATH];
    struct inode *ip;
    argint(0, &dirfd);
    argint(2, &mode);
    argint(3, &flags);
    if (argstr(1, pathname, MAXPATH) < 0) {
        return -1;
    }
    if ((ip = find_inode(pathname, dirfd, 0)) == 0) {
        return -1;
    }
    return do_faccess(ip, mode, flags);
}

// 功能：copies  data  between  one  file descriptor and another
// 输入：
// - out_fd: a fd for writing
// - in_fd: a fd for reading
// - *offset: 读偏移起始,如果不为NULL，则不会更新in_fd的pos,否则pos会更新，offset也会被赋值
// - count: 转移字节数
// 返回值：成功执行，返回转移字节数。错误，则返回-1。
uint64 sys_sendfile(void) {
    struct file *rf, *wf;
    off_t *poff;
    size_t count;
    if (argfd(0, 0, &wf) < 0 || argfd(1, 0, &rf) < 0) {
        return -1;
    }
    if (arglong(3, (long *)&count) < 0) {
        return -1;
    }
    if (count == 0) {
        return 0; // bug!!!
    }
    // argaddr(2,poff);
    poff = (off_t *)argraw(2);
    if (rf->f_type != FD_INODE || !F_READABLE(rf)) {
        return -1;
    }
    if (!F_WRITEABLE(wf)) {
        return -1;
    }

    // printfRed("sendfile begin, mm: %d pages\n", get_free_mem()/4096);
    // int retval = do_sendfile(rf, wf, poff, count);
    // printfRed("sendfile end,  mm: %d pages\n", get_free_mem()/4096);
    // return retval;
    return do_sendfile(rf, wf, poff, count);
}

// statfs, fstatfs - get filesystem statistics
// int statfs(const char *path, struct statfs *buf);
uint64 sys_statfs(void) {
    char buf[MAXPATH];
    uint64 ustat_addr;
    if (argstr(0, buf, MAXPATH) < 0) {
        return -1;
    }
    argaddr(1, &ustat_addr);

    struct statfs fs_stat;
    fs_stat.f_type = MSDOS_SUPER_MAGIC;
    fs_stat.f_bsize = BSIZE;
    fs_stat.f_frsize = BSIZE;
    fs_stat.f_blocks = __TotSec;
    fs_stat.f_bfree = __TotSec / 4;  // not important
    fs_stat.f_files = NINODE;
    fs_stat.f_ffree = NINODE / 4;    // not important
    fs_stat.f_bavail = __TotSec / 4; // not important
    fs_stat.f_fsid.val[0] = 2;       // not important
    fs_stat.f_namelen = NAME_LONG_MAX;
    fs_stat.f_flags = 0;             // not important
    // printfRed("%x", ustat_addr);
    struct proc *p = proc_current();
    if (copyout(p->mm->pagetable, ustat_addr, (char *)&fs_stat, sizeof(fs_stat)) < 0) { // rember add 1 for '\0'
        return -1;
    }
    return 0;
}
// static int nsec_valid(long nsec)
// {
// 	if (nsec == UTIME_OMIT || nsec == UTIME_NOW)
// 		return 1;

// 	return nsec >= 0 && nsec <= 999999999;
// }

static int utimes_common(struct inode *ip, struct timespec *times) {
    // 	int error;
    // 	struct iattr newattrs;
    // 	struct inode *inode = path->dentry->d_inode;

    // 	error = mnt_want_write(path->mnt);
    // 	if (error)
    // 		goto out;
    // if (times && times[0].ts_nsec == UTIME_NOW && times[1].ts_nsec == UTIME_NOW)
    // 	times = NULL;

    // 	newattrs.ia_valid = ATTR_CTIME | ATTR_MTIME | ATTR_ATIME;
    if (times) {
        // ignore it
        if (times[0].ts_nsec == UTIME_OMIT) {
            // 			newattrs.ia_valid &= ~ATTR_ATIME;
        } else if (times[0].ts_nsec == UTIME_NOW) {
            ip->i_atime = NS_to_S(TIME2NS(rdtime()));
            // 			newattrs.ia_atime.tv_sec = times[0].tv_sec;
            // 			newattrs.ia_atime.tv_nsec = times[0].tv_nsec;
            // 			newattrs.ia_valid |= ATTR_ATIME_SET;
        } else {
            ip->i_atime = NS_to_S(TIMESEPC2NS(times[0]));
        }

        // ignore it
        if (times[1].ts_nsec == UTIME_OMIT) {
            // 			newattrs.ia_valid &= ~ATTR_MTIME;
        } else if (times[1].ts_nsec == UTIME_NOW) {
            ip->i_mtime = NS_to_S(TIME2NS(rdtime()));
            // 			newattrs.ia_mtime.tv_sec = times[1].tv_sec;
            // 			newattrs.ia_mtime.tv_nsec = times[1].tv_nsec;
            // 			newattrs.ia_valid |= ATTR_MTIME_SET;
        } else {
            ip->i_mtime = NS_to_S(TIMESEPC2NS(times[1]));
        }
        // 		/*
        // 		 * Tell inode_change_ok(), that this is an explicit time
        // 		 * update, even if neither ATTR_ATIME_SET nor ATTR_MTIME_SET
        // 		 * were used.
        // 		 */
        // 		newattrs.ia_valid |= ATTR_TIMES_SET;
    } else {
        ip->i_atime = NS_to_S(TIME2NS(rdtime()));
        ip->i_mtime = NS_to_S(TIME2NS(rdtime()));
        // 		/*
        // 		 * If times is NULL (or both times are UTIME_NOW),
        // 		 * then we need to check permissions, because
        // 		 * inode_change_ok() won't do it.
        // 		 */
        // 		error = -EACCES;
        //                 if (IS_IMMUTABLE(inode))
        // 			goto mnt_drop_write_and_out;

        // 		if (!is_owner_or_cap(inode)) {
        // 			error = inode_permission(inode, MAY_WRITE);
        // 			if (error)
        // 				goto mnt_drop_write_and_out;
        // 		}
    }
    return 0;
    // 	mutex_lock(&inode->i_mutex);
    // 	error = notify_change(path->dentry, &newattrs);
    // 	mutex_unlock(&inode->i_mutex);

    // mnt_drop_write_and_out:
    // 	mnt_drop_write(path->mnt);
    // out:
    // 	return error;
}

long do_utimes(int dfd, char *filename, struct timespec *times, int flags) {
    int error = -EINVAL;
    // if (times && (!nsec_valid(times[0].ts_nsec) || !nsec_valid(times[1].ts_sec))) {
    // 	goto out;
    // }

    if (flags & ~AT_SYMLINK_NOFOLLOW)
        goto out;

    if (filename == NULL && dfd != AT_FDCWD) {
        struct file *file = NULL;

        if (flags & AT_SYMLINK_NOFOLLOW)
            goto out;

        argfd(0, &dfd, &file);

        error = -EBADF;
        if (!file)
            goto out;
        error = utimes_common(file->f_tp.f_inode, times);
    } else {
        // error = utimes_common(&file->f_path, times);
        struct inode *ip;
        if ((ip = find_inode(filename, dfd, 0)) == 0) {
            return -ENOTDIR;
        }
        error = utimes_common(ip, times);
    }
    // error = utimes_common(file->f_tp.f_inode, times);
    // 		fput(file);
    // } else {

    // struct path path;
    // int lookup_flags = 0;

    // if (!(flags & AT_SYMLINK_NOFOLLOW))
    // 	lookup_flags |= LOOKUP_FOLLOW;

// 		error = user_path_at(dfd, filename, lookup_flags, &path);
// 		if (error)
// 			goto out;

// 		error = utimes_common(&path, times);
// 		path_put(&path);
// }
out:
    return error;
}

// change file timestamps with nanosecond precision
// int utimensat(int dirfd, const char *pathname, const struct timespec times[2], int flags);
// times[0] specifies the new "last access time" (atime);
// times[1] specifies the new "last modification time" (mtime).
uint64 sys_utimensat(void) {
    int dirfd;
    char pathname[MAXPATH];
    uint64 times_addr;
    int flags;
    argint(0, &dirfd);
    uint64 pathaddr;
    argaddr(1, &pathaddr);
    if (pathaddr == 0 || argstr(1, pathname, MAXPATH) < 0) {
        pathname[0] = '\0';
        // return -1;
    }
    argaddr(2, &times_addr);
    argint(3, &flags);

    struct timespec tstimes[2];

    // extern int print_tf_flag;
    // print_tf_flag = 1;

    if (times_addr) {
        if (copyin(proc_current()->mm->pagetable, (char *)&tstimes, times_addr, 2 * sizeof(struct timespec)) < 0) {
            return -EFAULT;
        }
        if (tstimes[0].ts_sec == UTIME_OMIT && tstimes[1].ts_nsec == UTIME_OMIT)
            return 0;
    }

    return do_utimes(dirfd, pathname[0] ? pathname : NULL, times_addr ? tstimes : NULL, flags);

    // return -ENOENT;
    // // ASSERT(0);
    // printf("welcome to SYS_utimensat\n\n");
    // struct tcb *t = thread_current();
    // printf("tp = 0x%x\n", t->trapframe->tp);

    // // set_errno(ERRORCODE);
    // struct pthread pt;
    // either_copyin(&pt, 1, t->trapframe->tp, sizeof(pt));
    // pt.errno_val = 2;
    // pt.h_errno_val = 2;
    // either_copyout(1, t->trapframe->tp, &pt, sizeof(pt));
    // // *(int*)(t->trapframe->tp) = 2;
    // return 1;
}

// 功能：change the name or location of a file
// 输入：
// - olddirfd:
// - oldpath: 旧文件路径
// - newdirfd:
// - newpath: 新文件路径
// - flags:
// 返回值：成功执行，返回0。错误，则返回-1。
uint64 sys_renameat2(void) {
    uint flags;
    int olddirfd, newdirfd;
    struct inode *oldip;
    char oldpath[MAXPATH], newpath[MAXPATH];
    argint(0, &olddirfd);
    argint(2, &newdirfd);
    if (argstr(1, oldpath, MAXPATH) < 0) {
        return -1;
    }
    if (__namecmp("/", oldpath) == 0) {
        // 不能 mv 根目录
        return -1;
    }
    if (is_suffix(oldpath, ".") || is_suffix(oldpath, "..")) {
        // 不能重命名 . 和 ..
        return -1;
    }
    if (argstr(3, newpath, MAXPATH) < 0) {
        return -1;
    }
    if (is_suffix(newpath, ".") || is_suffix(newpath, "..")) {
        return -1;
    }
    argint(4, (int *)&flags);
    ASSERT(flags == 0);

    if ((oldip = find_inode(oldpath, olddirfd, 0)) == 0) {
        return -1;
    }

    do_renameat2(oldip, newdirfd, newpath, flags);

    return 0;
}

// 功能：control device
// 输入：
// - fd:
// - cmd:
// - arg: a user address
// 返回值：成功执行，返回0。错误，则返回-1。
uint64 sys_ioctl(void) {
    // return 0;
    int fd, ret;
    struct file *f;
    unsigned long cmd, arg;
    if (argfd(0, &fd, &f) < 0) {
        return -1;
    }
    arglong(1, (long *)&cmd);
    arglong(2, (long *)&arg);

    ret = vfs_ioctl(f, fd, cmd, arg);

    return ret;
}

// static void print_stat(struct stat* s) {
//     printf("st_dev : %x\n", s->st_dev);
//     printf("st_ino : %x\n", s->st_ino);
//     printf("st_mode : %x\n", s->st_mode);
//     printf("st_nlink : %x\n", s->st_nlink);
//     printf("st_uid : %x\n",s->st_uid);
//     printf("st_gid : %x\n", s->st_gid);
//     printf("st_rdev : %x\n", s->st_rdev);
//     printf("st_size : %x\n", s->st_size);
//     printf("st_blksize : %x\n", s->st_blksize);
//     printf("st_blocks : %x\n", s->st_blocks);
// }

// kbuf.st_dev = ip->i_dev;
// kbuf.st_ino = ip->i_ino;
// kbuf.st_mode = ip->i_mode; // not strict
// kbuf.st_nlink = 1;
// kbuf.st_uid = ip->i_uid;
// kbuf.st_gid = ip->i_gid;
// kbuf.st_rdev = ip->i_rdev;
// kbuf.st_size = ip->i_size;
// kbuf.st_blksize = ip->i_sb->s_blocksize;
// kbuf.st_blocks = ip->i_blocks * ip->i_sb->cluster_size / 512; // assuming out block is 512B

// // for libc-test stat
// kbuf.st_atim.ts_nsec = 0;
// kbuf.st_atim.ts_sec = 0;
// kbuf.st_mtim.ts_nsec = 0;
// kbuf.st_mtim.ts_sec = 0;
// kbuf.st_ctim.ts_nsec = 0;
// kbuf.st_ctim.ts_sec = 0;
// 功能：获取文件状态；
// 输入：
// - dirfd
// - pathname
// - statbuf
// - flags
// 返回值：成功返回0，失败返回-1；
uint64 sys_fstatat(void) {
    struct inode *ip;
    char pathname[MAXPATH];
    int dirfd, flags;
    uint64 statbuf;
    argint(0, &dirfd); // no need to check dirfd, because dirfd maybe AT_FDCWD(<0)
                       // find_inode() will do the check
    if (argstr(1, pathname, MAXPATH) < 0) {
        return -1;
    }
    argaddr(2, &statbuf);
    argint(3, &flags);
    // ASSERT(flags == 0);

    // TODO : for libc-test，we should add special process for /dev/null、/dev/zero
    if ((ip = find_inode(pathname, dirfd, 0)) == 0) {
        return -ENOENT;
    }

    ip->i_op->ilock(ip);

    // printf("inode name: %s\n ",ip->fat32_i.fname, ip->);
    struct stat kbuf;
    kbuf.st_dev = ip->i_dev;
    kbuf.st_ino = ip->i_ino;
    kbuf.st_mode = ip->i_mode; // not strict
    kbuf.st_nlink = ip->i_nlink;
    kbuf.st_uid = ip->i_uid;
    kbuf.st_gid = ip->i_gid;
    kbuf.st_rdev = ip->i_rdev;
    kbuf.st_size = ip->i_size;
    kbuf.st_blksize = ip->i_sb->s_blocksize;
    kbuf.st_blocks = ip->i_blocks * ip->i_sb->cluster_size / 512; // assuming out block is 512B

    // for libc-test stat
    kbuf.st_atim.ts_nsec = 0;
    kbuf.st_atim.ts_sec = 0;
    kbuf.st_mtim.ts_nsec = 0;
    kbuf.st_mtim.ts_sec = 0;
    kbuf.st_ctim.ts_nsec = 0;
    kbuf.st_ctim.ts_sec = 0;

    ip->i_op->iunlock(ip);
    // ip->i_op->iunlock_put(ip);// not nesessary for test

    // printf("name : %s\n", ip->fat32_i.fname);// debug
    // print_stat(&kbuf);// debug

    if (either_copyout(1, statbuf, &kbuf, sizeof(struct stat)) < 0) {
        return -1;
    }

    return 0;
}

// read from a file descriptor at a given offset
// ssize_t pread(int fd, void *buf, size_t count, off_t offset)
// reads up to count bytes from file descriptor fd at offset offset (from the start of the file) into the buffer starting at buf.
// The file off‐set is not changed.
uint64 sys_pread64(void) {
    int fd;
    uint64 buf;
    size_t count;
    off_t offset;

    struct file *f;
    if (argfd(0, &fd, &f) < 0) {
        return -1;
    }
    argaddr(1, &buf);
    argulong(2, &count);
    arglong(3, &offset);

    struct inode *ip = f->f_tp.f_inode;
    ip->i_op->ilock(ip);
    uint64 retval = ip->i_op->iread(ip, 1, buf, offset, count);
    ip->i_op->iunlock(ip);
    return retval;
}

// write to a file descriptor at a given offset
// ssize_t pwrite(int fd, const void *buf, size_t count, off_t offset);
// pwrite() writes up to count bytes from the buffer starting at buf to the file descriptor fd at offset offset.
// The file offset is not changed.
uint64 sys_pwrite64(void) {
    int fd;
    uint64 buf;
    size_t count;
    off_t offset;

    struct file *f;
    if (argfd(0, &fd, &f) < 0) {
        return -1;
    }
    argaddr(1, &buf);
    argulong(2, &count);
    arglong(3, &offset);

    struct inode *ip = f->f_tp.f_inode;

    ip->i_op->ilock(ip);
    uint64 retval = ip->i_op->iwrite(ip, 1, buf, offset, count);
    ip->i_op->iunlock(ip);
    return retval;
}

// synchronize cached writes to persistent storage
// void sync(void);
uint64 sys_sync(void) {
    return 0;
}

// synchronize a file's in-core state with storage device
// int fsync(int fd);
uint64 sys_fsync(void) {
    return 0;
}

// truncate a file to a specified length
// int ftruncate(int fd, off_t length);
uint64 sys_ftruncate(void) {
    return 0;
}

uint64 sys_fchmodat(void) {
    return 0;
}