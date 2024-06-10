#ifndef __FCNTL_H__
#define __FCNTL_H__

// f_flags
#define O_ASYNC 020000
#define O_PATH 010000000
#define O_TMPFILE 020040000
// #define O_NDELAY O_NONBLOCK

//
#define O_LARGEFILE 0100000
#define O_ACCMODE 00000003
#define O_RDONLY 00000000
#define O_WRONLY 00000001
#define O_RDWR 00000002

#define O_CREAT 0100  /* not fcntl */
#define O_TRUNC 01000 /* not fcntl */
#define O_EXCL 0200   /* not fcntl */
#define O_NOCTTY 0400 /* not fcntl */

#define O_NONBLOCK 04000
#define O_APPEND 02000
#define O_SYNC 04010000
#define O_RSYNC 04010000
#define O_DSYNC 010000     /* used to be O_SYNC, see below */
#define O_DIRECTORY 040000 /* must be a directory */
#define O_NOFOLLOW 0100000 /* don't follow links */
// #define O_LARGEFILE 0400000 /* will be set by the kernel on every open */
#define O_DIRECT 0200000 /* direct disk access - should check with OSF/1 */
#define O_NOATIME 01000000
#define O_CLOEXEC 02000000 /* set close_on_exec */

#define FCNTLABLE(value) \
    (((value & O_NONBLOCK) == O_NONBLOCK) || ((value & O_APPEND) == O_APPEND))
// (value == O_NONBLOCK || value == O_APPEND)

#define F_WRITEABLE(fp) ((fp)->f_flags > 0 ? 1 : 0)
#define F_READABLE(fp) (((fp)->f_flags & O_WRONLY) == O_WRONLY ? 0 : 1)

// f_mode
#define IMODE_READONLY 0x01
#define IMODE_NONE 0x00

#define PROT_NONE 0x0
#define PROT_READ 0x1
#define PROT_WRITE 0x2
#define PROT_EXEC 0x4

#define AT_FDCWD -100
#define AT_REMOVEDIR 0x200
#define AT_SYMLINK_NOFOLLOW 0x100 /* Do not follow symbolic links.  */

#define F_WRITEABLE(fp) ((fp)->f_flags > 0 ? 1 : 0)
#define F_READABLE(fp) (((fp)->f_flags & O_WRONLY) == O_WRONLY ? 0 : 1)

#define FMODE_READ 0x1
#define FMODE_WRITE 0x2

// lseek
#define SEEK_SET 0 /* seek relative to beginning of file */
#define SEEK_CUR 1 /* seek relative to current file position */
#define SEEK_END 2 /* seek relative to end of file */

// fcntl
#define F_DUPFD 0    /* dup */
#define F_GETFD 1    /* get close_on_exec */
#define F_SETFD 2    /* set/clear close_on_exec */
#define F_GETFL 3    /* get file->f_flags */
#define F_SETFL 4    /* set file->f_flags */
#define FD_CLOEXEC 1 /* actually anything with low bit set goes */
#define F_DUPFD_CLOEXEC 1030

// faccess
#define F_OK 0           /* test existance */
#define R_OK 4           /* test readable */
#define W_OK 2           /* test writable */
#define X_OK 1           /* test executable */
#define AT_EACCESS 0x100 /* 使用进程的有效用户ID 和 组ID */

#endif // __FCNTL_H__
