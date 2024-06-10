#ifndef __SELECT_H__
#define __SELECT_H__
#include "lib/poll.h"
#include "common.h"
#include "ipc/signal.h"

#define FD_ZERO(s)                                                        \
    do {                                                                  \
        int __i;                                                          \
        unsigned long *__b = (s)->fds_bits;                               \
        for (__i = sizeof(fd_set) / sizeof(long); __i; __i--) *__b++ = 0; \
    } while (0)

#define FD_SET(d, s) ((s)->fds_bits[(d) / (8 * sizeof(long))] |= (1UL << ((d) % (8 * sizeof(long)))))
#define FD_CLR(d, s) ((s)->fds_bits[(d) / (8 * sizeof(long))] &= ~(1UL << ((d) % (8 * sizeof(long)))))
#define FD_ISSET(d, s) !!((s)->fds_bits[(d) / (8 * sizeof(long))] & (1UL << ((d) % (8 * sizeof(long)))))

#define FD_SETSIZE 1024

#define POLLIN_SET (POLLRDNORM | POLLRDBAND | POLLIN | POLLHUP | POLLERR)
#define POLLOUT_SET (POLLWRBAND | POLLWRNORM | POLLOUT | POLLERR)
#define POLLEX_SET (POLLPRI)

typedef struct {
    uint64 fds_bits[FD_SETSIZE / 8 / sizeof(long)];
} fd_set;

int do_select(int nfds, fd_set_bits *fds, uint64 timeout);

int core_sys_select(int nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds, uint64 timeout);

long do_pselect(int nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds, struct timespec *tsp, const sigset_t *sigmask, size_t sigsetsize);

#endif