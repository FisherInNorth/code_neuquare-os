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

#endif