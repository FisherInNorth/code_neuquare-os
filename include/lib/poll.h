#ifndef __POLL_H__
#define __POLL_H__

#include "common.h"
/*
 * How many longwords for "nr" bits?
 */
#define FDS_BITPERLONG (8 * sizeof(long))
#define FDS_LONGS(nr) (((nr) + FDS_BITPERLONG - 1) / FDS_BITPERLONG)
#define FDS_BYTES(nr) (FDS_LONGS(nr) * sizeof(long))

/* ~832 bytes of stack space used max in sys_select/sys_poll before allocating
   additional memory. */
#define MAX_STACK_ALLOC 832
#define FRONTEND_STACK_ALLOC 256
#define SELECT_STACK_ALLOC FRONTEND_STACK_ALLOC
#define POLL_STACK_ALLOC FRONTEND_STACK_ALLOC
#define WQUEUES_STACK_ALLOC (MAX_STACK_ALLOC - FRONTEND_STACK_ALLOC)
#define N_INLINE_POLL_ENTRIES (WQUEUES_STACK_ALLOC / sizeof(struct poll_table_entry))

#define DEFAULT_POLLMASK (POLLIN | POLLOUT | POLLRDNORM | POLLWRNORM)

#define __NFDBITS (8 * sizeof(uint64))

typedef struct {
    uint64 *in, *out, *ex;
    uint64 *res_in, *res_out, *res_ex;
} fd_set_bits;

static inline void zero_fd_set(uint64 nr, uint64 *fdset) {
    memset(fdset, 0, FDS_BYTES(nr));
}

// static inline void copy_fd_set(uint64 nr, fd_set* fdset_dst, fd_set* fdset_src)
// {
// 	memmove((void*)fdset_dst, (void*)fdset_src, FDS_BYTES(nr));
// }

/* Event types that can be polled for.  These bits may be set in `events'
   to indicate the interesting event types; they will appear in `revents'
   to indicate the status of the file descriptor.  */
#define POLLIN 0x001  /* There is data to read.  */
#define POLLPRI 0x002 /* There is urgent data to read.  */
#define POLLOUT 0x004 /* Writing now will not block.  */

#if defined __USE_XOPEN || defined __USE_XOPEN2K8
/* These values are defined in XPG4.2.  */
#define POLLRDNORM 0x040 /* Normal data may be read.  */
#define POLLRDBAND 0x080 /* Priority data may be read.  */
#define POLLWRNORM 0x100 /* Writing now will not block.  */
#define POLLWRBAND 0x200 /* Priority data may be written.  */
#endif

#ifdef __USE_GNU
/* These are extensions for Linux.  */
#define POLLMSG 0x400
#define POLLREMOVE 0x1000
#define POLLRDHUP 0x2000
#endif

/* Event types always implicitly polled for.  These bits need not be set in
   `events', but they will appear in `revents' to indicate the status of
   the file descriptor.  */
#define POLLERR 0x008  /* Error condition.  */
#define POLLHUP 0x010  /* Hung up.  */
#define POLLNVAL 0x020 /* Invalid polling request.  */

struct pollfd {
    int fd;        /* file descriptor */
    short events;  /* requested events */
    short revents; /* returned events */
};

#endif