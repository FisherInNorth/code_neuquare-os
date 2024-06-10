#ifndef __STDDEF_H__
#define __STDDEF_H__

/* Represents true-or-false values */
typedef int bool;

/* Explicitly-sized versions of integer types */
typedef char int8;
typedef unsigned char uint8;
typedef short int16;
typedef unsigned short uint16;
typedef int int32;
typedef unsigned int uint32;
typedef long long int64;
typedef unsigned long long uint64;
typedef unsigned int uint;

#define STDIN_FILENO 0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2
#define ULONG_MAX (0xffffffffffffffffULL)
#define LONG_MAX (0x7fffffffffffffffLL)
#define INTMAX_MAX LONG_MAX
#define UINT_MAX (0xffffffffU)
#define INT_MAX (0x7fffffff)
#define UCHAR_MAX (0xffU)
#define CHAR_MAX (0x7f)

/* *
 * Pointers and addresses are 32 bits long.
 * We use pointer types to represent addresses,
 * uintptr_t to represent the numerical values of addresses.
 * */
#if __riscv_xlen == 64
typedef int64 intptr_t;
typedef uint64 uintptr_t;
#elif __riscv_xlen == 32
typedef int32_t intptr_t;
typedef uint32 uintptr_t;
#endif

/* size_t is used for memory object sizes */
typedef uintptr_t size_t;
typedef intptr_t ssize_t;

typedef int pid_t;

#define NULL ((void *)0)

#define SIGCHLD 17

#define va_start(ap, last) (__builtin_va_start(ap, last))
#define va_arg(ap, type) (__builtin_va_arg(ap, type))
#define va_end(ap) (__builtin_va_end(ap))
#define va_copy(d, s) (__builtin_va_copy(d, s))
typedef __builtin_va_list va_list;

#define O_ASYNC      020000
#define O_PATH    010000000
#define O_TMPFILE 020040000
// #define O_NDELAY O_NONBLOCK

//
#define O_LARGEFILE 0100000
#define O_ACCMODE 00000003
#define O_RDONLY 00000000
#define O_WRONLY 00000001
#define O_RDWR 00000002

#define O_CREAT        0100   /* not fcntl */
#define O_TRUNC       01000   /* not fcntl */
#define O_EXCL         0200    /* not fcntl */
#define O_NOCTTY       0400 /* not fcntl */

#define O_NONBLOCK    04000
#define O_APPEND      02000
#define O_SYNC     04010000
#define O_RSYNC    04010000
#define O_DSYNC      010000      /* used to be O_SYNC, see below */
#define O_DIRECTORY  040000 /* must be a directory */
#define O_NOFOLLOW  0100000   /* don't follow links */
// #define O_LARGEFILE 0400000 /* will be set by the kernel on every open */
#define O_DIRECT    0200000   /* direct disk access - should check with OSF/1 */
#define O_NOATIME  01000000
#define O_CLOEXEC  02000000  /* set close_on_exec */

#define DIR 0x040000
#define FILE 0x100000

#define AT_FDCWD -100

typedef struct
{
    uint64 sec;  // 自 Unix 纪元起的秒数
    uint64 usec; // 微秒数
} TimeVal;

typedef struct
{
    uint64 dev;    // 文件所在磁盘驱动器号，不考虑
    uint64 ino;    // inode 文件所在 inode 编号
    uint32 mode;   // 文件类型
    uint32 nlink;  // 硬链接数量，初始为1
    uint64 pad[7]; // 无需考虑，为了兼容性设计
} Stat;

typedef unsigned int mode_t;
typedef long int off_t;

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

struct linux_dirent64 {
    uint64 d_ino;
    int64 d_off;
    unsigned short d_reclen;
    unsigned char d_type;
    char d_name[];
};

// for mmap
#define PROT_NONE 0
#define PROT_READ 1
#define PROT_WRITE 2
#define PROT_EXEC 4
#define PROT_GROWSDOWN 0X01000000
#define PROT_GROWSUP 0X02000000

#define MAP_FILE 0
#define MAP_SHARED 0x01
#define MAP_PRIVATE 0X02
#define MAP_FAILED ((void *)-1)

// add
typedef unsigned int uint;
typedef unsigned short ushort;
typedef unsigned char uchar;
typedef int clockid_t;

struct timespec {
    uint64 ts_sec;  /* Seconds */
    uint64 ts_nsec; /* Nanoseconds */
};

#define EXIT_FAILURE 1 /* Failing exit status.  */
#define EXIT_SUCCESS 0 /* Successful exit status.  */
typedef long int intmax_t;

struct sysinfo {
    long uptime; /* Seconds since boot */
    // unsigned long loads[3];  /* 1, 5, and 15 minute load averages */
    unsigned long totalram; /* Total usable main memory size */
    unsigned long freeram;  /* Available memory size */
    // unsigned long sharedram; /* Amount of shared memory */
    // unsigned long bufferram; /* Memory used by buffers */
    // unsigned long totalswap; /* Total swap space size */
    // unsigned long freeswap;  /* Swap space still available */
    unsigned short procs; /* Number of current processes */
    // unsigned long totalhigh; /* Total high memory size */
    // unsigned long freehigh;  /* Available high memory size */
    // unsigned int mem_unit;   /* Memory unit size in bytes */
    // char _f[20-2*sizeof(long)-sizeof(int)]; /* Padding to 64 bytes */
};

typedef uint64 sig_t;
typedef void (*sighandler_t)(int);

// signal sets
typedef struct {
    uint64 sig;
} sigset_t;

typedef void __signalfn_t(int);
typedef __signalfn_t *__sighandler_t;

struct sigaction {
    __sighandler_t sa_handler;
    uint sa_flags;
    sigset_t sa_mask;
};

#define SIGINT 2
#define SIGKILL 9

typedef long int off_t;

struct iovec {
    void *iov_base; /* Starting address */
    size_t iov_len; /* Number of bytes to transfer */
};

// struct statfs {
//     __fsword_t f_type;    /* Type of filesystem (see below) */
//     __fsword_t f_bsize;   /* Optimal transfer block size */
//     fsblkcnt_t f_blocks;  /* Total data blocks in filesystem */
//     fsblkcnt_t f_bfree;   /* Free blocks in filesystem */
//     fsblkcnt_t f_bavail;  /* Free blocks available to
//                                         unprivileged user */
//     fsfilcnt_t f_files;   /* Total file nodes in filesystem */
//     fsfilcnt_t f_ffree;   /* Free file nodes in filesystem */
//     fsid_t f_fsid;        /* Filesystem ID */
//     __fsword_t f_namelen; /* Maximum length of filenames */
//     __fsword_t f_frsize;  /* Fragment size (since Linux 2.6) */
//     __fsword_t f_flags;   /* Mount flags of filesystem
//                                         (since Linux 2.6.36) */
//     __fsword_t f_spare[xxx];
//     /* Padding bytes reserved for future use */
// };

#endif // __STDDEF_H__
