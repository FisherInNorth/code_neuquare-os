#ifndef __FDTABLE_H__
#define __FDTABLE_H__

#include "param.h"
#include "atomic/spinlock.h"
#include "atomic/ops.h"

struct file;
struct fd {
    struct file *file;
    uint flags;
};

typedef fd fd_t;
// descriptor table
struct fdtable {
    atomic_t ref;
    struct spinlock lock;
    fd_t ofile[NOFILE];
    uint max_fds; // max fd
    uint cur_fd;  // current fd
};

#endif // __FDTABLE_H__