#include "common.h"
#include "ipc/pipe.h"
#include "lib/riscv.h"
#include "param.h"
#include "atomic/spinlock.h"
#include "proc/pcb_life.h"
#include "kernel/trap.h"
#include "memory/allocator.h"
#include "atomic/cond.h"
#include "ipc/signal.h"
#include "fs/vfs/fs.h"
#include "fs/vfs/ops.h"
#include "lib/sbuf.h"
#include "debug.h"
#include "errno.h"

int pipe_alloc(struct file **f0, struct file **f1) {
    struct pipe *pi;
    fs_t type = proc_current()->cwd->fs_type;
    pi = 0;
    *f0 = *f1 = 0;
    if ((*f0 = filealloc(type)) == 0 || (*f1 = filealloc(type)) == 0)
        goto bad;
    if ((pi = (struct pipe *)kalloc()) == 0)
        goto bad;
    pi->readopen = 1;
    pi->writeopen = 1;
    pi->nwrite = 0;
    pi->nread = 0;
    initlock(&pi->lock, "pipe");

    sema_init(&pi->read_sem, 0, "read_sem");
    sema_init(&pi->write_sem, 0, "write_sem");

    (*f0)->f_type = FD_PIPE;
    (*f0)->f_flags = O_RDONLY;
    (*f0)->f_tp.f_pipe = pi;
    (*f1)->f_type = FD_PIPE;
    (*f1)->f_flags = O_WRONLY;
    (*f1)->f_tp.f_pipe = pi;
    return 0;

bad:
    if (pi)
        kfree((char *)pi);
    if (*f0)
        generic_fileclose(*f0);
    if (*f1)
        generic_fileclose(*f1);
    return -EMFILE;
}

void pipe_close(struct pipe *pi, int writable) {
    acquire(&pi->lock);
    if (writable) {
        pi->writeopen = 0;
        sema_signal(&pi->read_sem);
    } else {
        pi->readopen = 0;
        sema_signal(&pi->write_sem);
    }
    if (pi->readopen == 0 && pi->writeopen == 0) {
        release(&pi->lock);
        kfree((char *)pi);
    } else
        release(&pi->lock);
}

int pipe_write(struct pipe *pi, int user_dst, uint64 addr, int n) {
    int i = 0;
    struct proc *pr = proc_current();

    acquire(&pi->lock);
    while (i < n) {
        if (pi->readopen == 0 || proc_killed(pr)) {
            release(&pi->lock);
            return -1;
        }
        if (PIPE_FULL(pi)) {
            sema_signal(&pi->read_sem);
            release(&pi->lock);
            sema_wait(&pi->write_sem);
            acquire(&pi->lock);
        } else {
            char ch;
            if (either_copyin(&ch, user_dst, addr + i, 1) == -1)
                break;
            pi->data[pi->nwrite++ % PIPESIZE] = ch;
            i++;
            // int write_idx = pi->nwrite % PIPESIZE;
            // int read_idx = pi->nwrite % PIPESIZE;
            // int cpy_len = PIPESIZE - (write_idx - read_idx);
            // if (either_copyin(pi->data + write_idx, user_dst, addr + cpy_len, cpy_len) == -1)
            //     break;
            // pi->nwrite += cpy_len;
            // i+= cpy_len;
        }
    }
    sema_signal(&pi->read_sem);
    release(&pi->lock);

    return i;
}

int pipe_read(struct pipe *pi, int user_dst, uint64 addr, int n) {
    int i;
    struct proc *pr = proc_current();
    char ch;

    acquire(&pi->lock);
    while (PIPE_EMPTY(pi) && pi->writeopen) {
        if (proc_killed(pr)) {
            release(&pi->lock);
            return -1;
        }
        release(&pi->lock);
        sema_wait(&pi->read_sem);
        acquire(&pi->lock);
    }
    for (i = 0; i < n; i++) {
        if (PIPE_EMPTY(pi))
            break;
        ch = pi->data[pi->nread % PIPESIZE];
        if (either_copyout(user_dst, addr + i, &ch, 1) == -1)
            break;
        ++pi->nread;
    }
    sema_signal(&pi->write_sem);
    release(&pi->lock);
    return i;
}

// int pipe_alloc(struct file **f0, struct file **f1) {
//     struct pipe *pi = 0;
//     fs_t type = proc_current()->cwd->fs_type;
//     *f0 = *f1 = 0;
//     if ((*f0 = filealloc(type)) == 0 || (*f1 = filealloc(type)) == 0)
//         goto bad;
//     if ((pi = (struct pipe *)kalloc()) == 0)
//         goto bad;

//     sbuf_init(&pi->buffer, PIPESIZE);

//     pi->readopen = 1;
//     pi->writeopen = 1;

//     (*f0)->f_type = FD_PIPE;
//     (*f0)->f_flags = O_RDONLY;
//     (*f0)->f_tp.f_pipe = pi;
//     (*f1)->f_type = FD_PIPE;
//     (*f1)->f_flags = O_WRONLY;
//     (*f1)->f_tp.f_pipe = pi;
//     return 0;
// bad:
//     if (pi)
//         kfree((char *)pi);
//     if (*f0)
//         generic_fileclose(*f0);
//     if (*f1)
//         generic_fileclose(*f1);
//     return -EMFILE;
// }

// void pipe_close(struct pipe *pi, int writable) {
//     acquire(&pi->buffer.lock);
//     if (writable) {
//         pi->writeopen = 0;
//         sema_signal(&pi->buffer.items); // !!! bug
//     } else {
//         pi->readopen = 0;
//         sema_signal(&pi->buffer.slots); // !!! bug
//     }
//     if (pi->readopen == 0 && pi->writeopen == 0) {
//         release(&pi->buffer.lock);
//         sbuf_free(&pi->buffer); // !!!
//         kfree((char *)pi);
//     } else {
//         release(&pi->buffer.lock);
//     }
// }

// int pipe_write(struct pipe *pi, int user_dst, uint64 addr, int n) {
//     struct proc *pr = proc_current();
//     int i;

//     for (i = 0; i < n; i++) {
//         // acquire(&pi->buffer.lock);
//         if (proc_killed(pr) || pi->readopen == 0) {
//             // release(&pi->buffer.lock);
//             return -1;
//         }
//         // release(&pi->buffer.lock);
//         int ret = sbuf_insert(&pi->buffer, user_dst, addr + i);
//         if (ret == -1) {
//             return -1;
//         }
//     }

//     return i;
// }

// int pipe_read(struct pipe *pi, int user_dst, uint64 addr, int n) {
//     int i;
//     struct proc *pr = proc_current();

//     for (i = 0; i < n; i++) {
//         if (proc_killed(pr)) {
//             return -1;
//         }
//         // acquire(&pi->buffer.lock);
//         if (pi->writeopen == 0 && pi->buffer.r == pi->buffer.w) { // !!! bug
//             // release(&pi->buffer.lock);
//             break;
//         }
//         // release(&pi->buffer.lock);

//         int ret = sbuf_remove(&pi->buffer, user_dst, addr + i);
//         if (ret == -1) {
//             return -1;
//         } else if (ret == 1) {
//             break;
//         }
//     }
//     return i;
// }
int pipe_empty(struct pipe *p) {
    acquire(&p->lock);
    int ret = (p->nread == p->nwrite);
    release(&p->lock);
    return ret;
}

int pipe_full(struct pipe *p) {
    acquire(&p->lock);
    int ret = ((p->nread + PIPESIZE) == p->nwrite);
    release(&p->lock);
    return ret;
}
