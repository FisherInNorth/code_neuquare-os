#ifndef __SOCKET_H__
#define __SOCKET_H__
#include "common.h"
#include "lib/riscv.h"
#include "atomic/spinlock.h"
#include "atomic/semaphore.h"
#include "lib/sbuf.h"

#define BUFSIZE PGSIZE

struct socket {
    // socket_state		state;
    // uint64 family;
    uint64 flags;
    uint64 src_port;
    uint64 dst_port;
    short type;

    // struct sock_buf sbuf;
    struct sbuf sbuf;
    struct file *file;
    struct socket_operations *ops;

    struct list_head pending; /* server */
    struct list_head node;    /* client */
    struct semaphore do_accept;
    int has_map;
    int used;
    // wait_queue_head_t	wait;
};

struct socket_operations {
    int (*read)(struct socket *, uint64, int);
    int (*write)(struct socket *, uint64, int);
};

void free_socket(struct socket *sock);

/* Types of sockets.  */
enum __socket_type {
    SOCK_STREAM = 1, /* Sequenced, reliable, connection-based
                   byte streams.  */
#define SOCK_STREAM SOCK_STREAM
    SOCK_DGRAM = 2, /* Connectionless, unreliable datagrams
                   of fixed maximum length.  */
#define SOCK_DGRAM SOCK_DGRAM
    SOCK_RAW = 3, /* Raw protocol interface.  */
#define SOCK_RAW SOCK_RAW
    SOCK_RDM = 4, /* Reliably-delivered messages.  */
#define SOCK_RDM SOCK_RDM
    SOCK_SEQPACKET = 5, /* Sequenced, reliable, connection-based,
                   datagrams of fixed maximum length.  */
#define SOCK_SEQPACKET SOCK_SEQPACKET
    SOCK_DCCP = 6, /* Datagram Congestion Control Protocol.  */
#define SOCK_DCCP SOCK_DCCP
    SOCK_PACKET = 10, /* Linux specific way of getting packets
                   at the dev level.  For writing rarp and
                   other similar things on the user level. */
#define SOCK_PACKET SOCK_PACKET

    /* Flags to be ORed into the type parameter of socket and socketpair and
     used for the flags parameter of paccept.  */

    SOCK_CLOEXEC = 02000000, /* Atomically set close-on-exec flag for the
                   new descriptor(s).  */
#define SOCK_CLOEXEC SOCK_CLOEXEC
    SOCK_NONBLOCK = 00004000 /* Atomically mark descriptor(s) as
                   non-blocking.  */
#define SOCK_NONBLOCK SOCK_NONBLOCK
};

#endif // __SOCKET_H__
