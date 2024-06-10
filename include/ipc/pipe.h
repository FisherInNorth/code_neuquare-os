#ifndef __PIPE_H__
#define __PIPE_H__

#include "common.h"
#include "atomic/spinlock.h"
#include "atomic/semaphore.h"
#include "lib/sbuf.h"

struct file;

#define PIPESIZE 512

// struct pipe {
//     struct sbuf buffer;
//     int readopen;  // read fd is still open
//     int writeopen; // write fd is still open
// };

// int pipe_alloc(struct file **f0, struct file **f1);
// void pipe_close(struct pipe *pi, int writable);
// int pipe_write(struct pipe *pi, int user_dst, uint64 addr, int n);
// int pipe_read(struct pipe *pi, int user_dst, uint64 addr, int n);

struct pipe {
    struct spinlock lock;
    char data[PIPESIZE];
    uint nread;    // number of bytes read
    uint nwrite;   // number of bytes written
    int readopen;  // read fd is still open
    int writeopen; // write fd is still open

    struct semaphore read_sem;
    struct semaphore write_sem;
};

#define PIPE_FULL(pi) (pi->nwrite == pi->nread + PIPESIZE)
#define PIPE_EMPTY(pi) (pi->nread == pi->nwrite)

#define pipereadable(p) (p->readopen)
#define pipewriteable(p) (p->writeopen)

int pipe_empty(struct pipe *p);
int pipe_full(struct pipe *p);
int pipe_alloc(struct file **f0, struct file **f1);
void pipe_close(struct pipe *pi, int writable);
int pipe_read(struct pipe *pi, int user_dst, uint64 addr, int n);
int pipe_write(struct pipe *pi, int user_dst, uint64 addr, int n);

#endif // __PIPE_H__