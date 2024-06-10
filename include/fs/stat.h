#ifndef __FS_STAT_H__
#define __FS_STAT_H__

// since mkfs will use kernel header file, add this condition preprocess
#ifndef USER
#include "common.h"
#endif

// type
/*
        note: below are NOT USED any more !
    */
// #define T_DIR 1    // Directory
// #define T_FILE 2   // File
// #define T_DEVICE 3 // Device

// now use these
#define S_IFMT 0170000   // bit mask for the file type bit field
#define S_IFSOCK 0140000 // socket
#define S_IFLNK 0120000  // symbolic link
#define S_IFREG 0100000  // regular file
#define S_IFBLK 0060000  // block device
#define S_IFDIR 0040000  // directory
#define S_IFCHR 0020000  // character device
#define S_IFIFO 0010000  // FIFO

#define S_ISREG(mode) (((mode)&S_IFMT) == S_IFREG)
#define S_ISDIR(mode) (((mode)&S_IFMT) == S_IFDIR)
#define S_ISCHR(mode) (((mode)&S_IFMT) == S_IFCHR)
#define S_ISBLK(mode) (((mode)&S_IFMT) == S_IFBLK)
#define S_ISFIFO(mode) (((mode)&S_IFMT) == S_IFIFO)

/* these are defined by POSIX and also present in glibc's dirent.h */
#define DT_UNKNOWN 0
#define DT_FIFO 1
#define DT_CHR 2
#define DT_DIR 4
#define DT_BLK 6
#define DT_REG 8
#define DT_LNK 10
#define DT_SOCK 12
#define DT_WHT 14

// #define S_IFMT (0xf0)
// #define S_IFIFO (0xA0)                       // 1010_0000
// #define S_IFREG (0x80)                       // 1000_0000
// #define S_IFBLK (0x60)                       // 0110_0000
// #define S_IFDIR (0x40)                       // 0100_0000
// #define S_IFCHR (0x20)                       // 0010_0000

#define S_ISUID 04000 // set-user-ID bit (see execve(2))
#define S_ISGID 02000 // set-group-ID bit (see below)
#define S_ISVTX 01000 // sticky bit (see below)

#define S_IRWXU 00700 // owner has read, write, and execute permission
#define S_IRUSR 00400 // owner has read permission
#define S_IWUSR 00200 // owner has write permission
#define S_IXUSR 00100 // owner has execute permission

#define S_IRWXG 00070 // group has read, write, and execute permission
#define S_IRGRP 00040 // group has read permission
#define S_IWGRP 00020 // group has write permission
#define S_IXGRP 00010 // group has execute permission

#define S_IRWXO 00007 // others (not in group) have read,  write,  and execute permission
#define S_IROTH 00004 // others have read permission
#define S_IWOTH 00002 // others have write permission
#define S_IXOTH 00001 // others have execute permission

#define S_IRWXUGO (S_IRWXU | S_IRWXG | S_IRWXO)
#define S_IALLUGO (S_ISUID | S_ISGID | S_ISVTX | S_IRWXUGO)
#define S_IRUGO (S_IRUSR | S_IRGRP | S_IROTH)
#define S_IWUGO (S_IWUSR | S_IWGRP | S_IWOTH)
#define S_IXUGO (S_IXUSR | S_IXGRP | S_IXOTH)

// device
#define MAJOR(rdev) (((rdev) >> 8) & 0xff)
#define MINOR(rdev) ((rdev)&0xff)
#define mkrdev(ma, mi) ((uint)(((ma)&0xff) << 8 | ((mi)&0xff)))
#define CONSOLE 1 // 终端的主设备号

typedef unsigned long int dev_t;
typedef unsigned long int ino_t;
// typedef unsigned long int nlink_t;
typedef unsigned int nlink_t;
typedef unsigned int uid_t;
typedef unsigned int gid_t;
typedef long int off_t;
typedef long int blksize_t;
typedef long int blkcnt_t;
typedef unsigned int mode_t;
typedef long int off_t;

// for oscomp
struct kstat {
    uint64 st_dev;
    uint64 st_ino;
    mode_t st_mode;
    uint32 st_nlink;
    uint32 st_uid;
    uint32 st_gid;
    uint64 st_rdev;
    unsigned long __pad;
    off_t st_size;
    uint32 st_blksize;
    int __pad2;
    uint64 st_blocks;
    long st_atime_sec;
    long st_atime_nsec;
    long st_mtime_sec;
    long st_mtime_nsec;
    long st_ctime_sec;
    long st_ctime_nsec;
    unsigned __unused[2];
};

// for inode
struct stat {
    dev_t st_dev;         /* ID of device containing file */
    ino_t st_ino;         /* Inode number */
    mode_t st_mode;       /* File type and mode */
    nlink_t st_nlink;     /* Number of hard links */
    uid_t st_uid;         /* User ID of owner */
    gid_t st_gid;         /* Group ID of owner */
    dev_t st_rdev;        /* Device ID (if special file) */
    uint16 __pad2;
    off_t st_size;        /* Total size, in bytes */
    blksize_t st_blksize; /* Block size for filesystem I/O */
    blkcnt_t st_blocks;   /* Number of 512B blocks allocated */

    /* Since Linux 2.6, the kernel supports nanosecond
        precision for the following timestamp fields.
        For the details before Linux 2.6, see NOTES. */

    struct timespec st_atim; /* Time of last access */
    struct timespec st_mtim; /* Time of last modification */
    struct timespec st_ctim; /* Time of last status change */
    unsigned __unused[2];

#define st_atime st_atim.tv_sec /* Backward compatibility */
#define st_mtime st_mtim.tv_sec
#define st_ctime st_ctim.tv_sec
};

// for file system
typedef struct {
    int val[2];
} __kernel_fsid_t;
typedef __kernel_fsid_t fsid_t;
#define MSDOS_SUPER_MAGIC 0x4d44
typedef uint64 fsblkcnt_t;
typedef uint64 fsfilcnt_t;

struct statfs {
    unsigned long f_type, f_bsize;
    fsblkcnt_t f_blocks, f_bfree, f_bavail;
    fsfilcnt_t f_files, f_ffree;
    fsid_t f_fsid;
    unsigned long f_namelen, f_frsize, f_flags, f_spare[4];
};
// f_type;     /* Type of filesystem */
// f_frsize;	/* Fragment size (since Linux 2.6) */
// f_bsize;    /* Optimal transfer block size */
// f_blocks;   /* Total data blocks in filesystem */
// f_bfree;    /* Free blocks in filesystem */
// f_files;    /* Total inodes in filesystem */
// f_ffree;    /* Free inodes in filesystem */
// f_bavail;    /* Free blocks available to unprivileged user */
// f_fsid;      /* Filesystem ID */
// f_namelen;   /* Maximum length of filenames */
// f_flags;     /* Mount flags of filesystem (since Linux 2.6.36) */
// f_spare[5];  /* Padding bytes reserved for future use */

// for utimensat
#define UTIME_NOW ((1l << 30) - 1l)
#define UTIME_OMIT ((1l << 30) - 2l)

#endif // __FS_STAT_H__
