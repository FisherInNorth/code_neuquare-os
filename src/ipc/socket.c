#include "common.h"
#include "kernel/syscall.h"
#include "debug.h"
#include "memory/vm.h"
#include "proc/pcb_life.h"
#include "proc/tcb_life.h"
#include "kernel/trap.h"
#include "fs/vfs/ops.h"
#include "ipc/socket.h"
#include "atomic/spinlock.h"
#include "syscall_gen/syscall_num.h"
#include <stdarg.h>

/* Protocol families.  */
#define PF_UNSPEC 0      /* Unspecified.  */
#define PF_LOCAL 1       /* Local to host (pipes and file-domain).  */
#define PF_UNIX PF_LOCAL /* POSIX name for PF_LOCAL.  */
#define PF_FILE PF_LOCAL /* Another non-standard name for PF_LOCAL.  */
#define PF_INET 2        /* IP protocol family.  */

/* Address families.  */
#define AF_UNSPEC PF_UNSPEC
#define AF_LOCAL PF_LOCAL
#define AF_UNIX PF_UNIX
#define AF_FILE PF_FILE
#define AF_INET PF_INET

/* Internet address.  */
typedef uint32 in_addr_t;
#define INADDR_ANY ((in_addr_t)0x00000000)

/* Internet address. */
struct in_addr {
    uint32 s_addr; /* address in network byte order */
};

struct sockaddr_in {
    uint8 sin_family;        /* address family: AF_INET */
    uint16 sin_port;         /* port in network byte order */
    struct in_addr sin_addr; /* internet address */
};

#define SIZE 20
struct spinlock map_lock;
struct port_to_sock {
    int port;
    struct socket *sock;
} map[SIZE];

uint16 swapEndian16(uint16 value) {
    uint16 result = 0;

    result |= (value & 0xFF) << 8;
    result |= (value & 0xFF00) >> 8;

    return result;
}

struct socket *port2sock(int port) {
    if (port == 0) {
        Warn("illegal port");
        return NULL;
    }

    acquire(&map_lock);
    for (int i = 0; i < SIZE; i++) {
        if (map[i].port == port) {
            release(&map_lock);
            return map[i].sock;
        }
    }
    release(&map_lock);
    return NULL;
}

int add_mapping(int port, struct socket *sock) {
    acquire(&map_lock);
    for (int i = 0; i < SIZE; i++) {
        if (map[i].port == 0) {
            map[i].port = port;
            map[i].sock = sock;
            release(&map_lock);
            return 0;
        }
    }
    release(&map_lock);
    return -1;
}

int free_mapping(int port) {
    acquire(&map_lock);
    for (int i = 0; i < SIZE; i++) {
        if (map[i].port == port) {
            map[i].port = 0;
            release(&map_lock);
            return 0;
        }
    }
    release(&map_lock);
    return -1;
}

// struct sockaddr {
//     char sa_data[14]; /* Address data.  */
// };

atomic_t PORT = ATOMIC_INIT(1024);
#define ASSIGN_PORT atomic_inc_return(&PORT)

void info_socket(int funcid, int sockfd, struct socket *sock, ...) {
#ifndef __STRACE__
    return;
#else
    return;
    va_list ap;
    va_start(ap, sock);
    switch (funcid) {
    case SYS_sendto:
    case SYS_connect: {
        printfGreen("\n fd is %d, src_port is %d, dst_port is %d\n", sockfd, sock->src_port, sock->dst_port);
        break;
    }
    case SYS_bind: {
        printfGreen("\n fd is %d, src_port is %d", sockfd, sock->src_port);
        break;
    }
    case SYS_close: {
        printfGreen("\n src_port is %d, dst_port is %d", sockfd, sock->src_port, sock->dst_port);
        break;
    }
    case SYS_socket: {
        int type = va_arg(ap, int);
        if (type | SOCK_STREAM) {
            printfGreen("TCP socket\t");
        } else if (type | SOCK_DGRAM) {
            printfGreen("UDP socket\t");
        }
        if (type & SOCK_NONBLOCK) {
            printfGreen("NONBLOCK SOCKET\t");
        }
        break;
    }
    default:
        panic("not support");
        break;
    }
    va_end(ap);
#endif
}
//       int bind(int sockfd, const struct sockaddr *addr, socklen_t addrlen);
uint64 sys_bind(void) {
    struct trapframe *tp = thread_current()->trapframe;
    int sockfd;
    struct file *fp;
    struct socket *sock;
    const struct sockaddr_in *sa = (const struct sockaddr_in *)getphyaddr(proc_current()->mm->pagetable, tp->a1);
    if (argfd(0, &sockfd, &fp) < 0) {
        Warn("argfd failed");
        return -1;
    }
    sock = fp->f_tp.f_sock;

    // only use first 32 bit for AF_INET
    // ASSERT(addrlen == sizeof(struct sockaddr_in));

    if (sa->sin_family != AF_INET) {
        Warn("sa->sin_family != AF_INET, not support");
        return -1;
    }

    // only support localhost socket
    if (sa->sin_addr.s_addr != INADDR_ANY && sa->sin_addr.s_addr != 0x100007f) {
        Warn("sa->sin_family != INADDR_ANY, not support");
        return -1;
    }

    if (sa->sin_port == 0) {
        sock->src_port = atomic_inc_return(&PORT);
    } else {
        sock->src_port = sa->sin_port;
    }
    add_mapping(sock->src_port, sock);

    info_socket(SYS_bind, sockfd, sock);

    return 0;
}

// int getsockname(int sockfd, struct sockaddr *addr, socklen_t *addrlen);
uint64 sys_getsockname(void) {
    struct trapframe *tp = thread_current()->trapframe;
    int sockfd;
    struct file *fp;
    struct socket *sock;
    struct sockaddr_in *sa = (struct sockaddr_in *)getphyaddr(proc_current()->mm->pagetable, tp->a1);
    paddr_t *addrlen = (paddr_t *)getphyaddr(proc_current()->mm->pagetable, tp->a2);
    // uint32 addrlen = tp->a2;
    if (argfd(0, &sockfd, &fp) < 0) {
        Warn("argfd failed");
        return -1;
    }
    sock = fp->f_tp.f_sock;

    sa->sin_port = sock->src_port;
    sa->sin_addr.s_addr = INADDR_ANY;
    *addrlen = sizeof(struct sockaddr_in);
    return 0;
}

// uint64 sys_setsockopt(void) {
//     return 0;
// }

//  int listen(int sockfd, int backlog);
uint64 sys_listen(void) {
    int sockfd;
    struct file *fp;
    struct socket *sock;
    if (argfd(0, &sockfd, &fp) < 0) {
        Warn("argfd failed");
        return -1;
    }
    sock = fp->f_tp.f_sock;

    sema_init(&sock->do_accept, 0, "do_accept");
    return 0;
}

extern int create_sockfp(int type);
// int accept(int sockfd, struct sockaddr *addr, socklen_t *addrlen);
uint64 sys_accept(void) {
    int sockfd;
    struct file *fp;
    struct socket *sock;
    if (argfd(0, &sockfd, &fp) < 0) {
        Warn("argfd failed");
        return -1;
    }
    sock = fp->f_tp.f_sock;

    sema_wait(&sock->do_accept);

    // int fd = create_sockfp(sock->type);
    // struct file *fp = proc_current()->ofile[fd];
    // struct socket *new = fp->f_tp.f_sock;

    // new->dst_port = ;

    // return fd;
    return 0;
}

static void do_connect(struct socket *src_sock, struct socket *dst_sock) {
    list_add_tail(&src_sock->node, &dst_sock->pending);
    sema_signal(&dst_sock->do_accept);
    return;
}

// int connect(int sockfd, const struct sockaddr *addr, socklen_t addrlen);
uint64 sys_connect(void) {
    struct trapframe *tp = thread_current()->trapframe;
    int sockfd;
    struct file *fp;
    struct socket *sock;
    const struct sockaddr_in *sa = (const struct sockaddr_in *)getphyaddr(proc_current()->mm->pagetable, tp->a1);
    // uint32 addrlen = tp->a2;
    if (argfd(0, &sockfd, &fp) < 0) {
        Warn("argfd failed");
        return -1;
    }
    sock = fp->f_tp.f_sock;

    // if not bind before, assign a port
    if (sock->src_port == 0) {
        sock->src_port = atomic_inc_return(&PORT);
        add_mapping(sock->src_port, sock);
    }

    // int dstport = swapEndian16(sa->sin_port);
    int dstport = sa->sin_port;
    struct socket *dstsock = port2sock(dstport);
    // if (dstsock == NULL) {
    // Warn("connect failed");
    // return 0;
    // } else {
    // sock->dst_port = sa->sin_port;
    sock->dst_port = sa->sin_port;
    if (dstsock != NULL) {
        do_connect(sock, dstsock);
    }
    // }

    info_socket(SYS_connect, sockfd, sock);
    return 0;
}

struct spinlock socket_table_lock;
void init_socket_table() {
    initlock(&socket_table_lock, "socket_table");
    initlock(&map_lock, "map_lock");
    Info("socket table init [ok]\n");
}
struct socket socket_table[100];
struct socket *alloc_socket() {
    acquire(&socket_table_lock);
    for (int i = 0; i < 100; i++) {
        if (socket_table[i].used == 0) {
            memset(&socket_table[i], 0, sizeof(struct socket));
            socket_table[i].used = 1;
            INIT_LIST_HEAD(&socket_table[i].pending);
            release(&socket_table_lock);
            return &socket_table[i];
        }
    }
    release(&socket_table_lock);
    return NULL;
}

void free_socket(struct socket *sock) {
    // TODO
    // if (sock->sbuf) {
    //     sbuf_free(&sock->sbuf);
    // }
    sock->used = 0;
    if (free_mapping(sock->src_port) < 0) {
        // Warn("free failed");
    }
    info_socket(SYS_close, 0, sock);
    return;
}

extern int fdalloc(struct file *f);
// int socket(int domain, int type, int protocol);
// domain : address family ipv4 ipv6 unix
// type : features
// protocol : ipv4 ipv6 icmp raw tcp udp
uint64 sys_socket(void) {
    struct socket *sock = alloc_socket();
    struct trapframe *tp = thread_current()->trapframe;
    // int domain = tp->a0, type = tp->a1, protocol = tp->a2;

    // we only use type(TCP or UDP)
    int type = tp->a1;

    fs_t fs_type = proc_current()->cwd->fs_type;
    struct file *fp;
    int fd;
    if ((fp = filealloc(fs_type)) == 0 || (fd = fdalloc(fp)) < 0) {
        if (fp)
            generic_fileclose(fp);
        return -1;
    }
    ASSERT(fd >= 3 && fd < NOFILE);

    fp->f_type = FD_SOCKET;
    fp->f_tp.f_sock = sock;
    fp->f_count = 1;
    fp->f_flags |= (type & SOCK_CLOEXEC) ? FD_CLOEXEC : 0;
    fp->f_flags |= (type & SOCK_NONBLOCK) ? O_NONBLOCK : 0;
    info_socket(SYS_socket, fd, sock, type);

    sock->file = fp;
    sock->type = type;
    if (sock->sbuf.type == NONE) {
        sbuf_init(&sock->sbuf, 10 * PGSIZE / sizeof(char));
        sock->sbuf.type = SOCKET_SBUF;
    }
    // sock.family = domain;

    return fd;
}

int create_sockfp(int type) {
    struct socket *sock = alloc_socket();
    fs_t fs_type = proc_current()->cwd->fs_type;
    struct file *fp;
    int fd;
    if ((fp = filealloc(fs_type)) == 0 || (fd = fdalloc(fp)) < 0) {
        if (fp)
            generic_fileclose(fp);
        return -1;
    }
    ASSERT(fd >= 3 && fd < NOFILE);

    fp->f_type = FD_SOCKET;
    fp->f_tp.f_sock = sock;
    fp->f_count = 1;
    // tmp for socketpair
    fp->f_flags |= O_RDWR;
    info_socket(SYS_socket, fd, sock, type);

    sock->file = fp;
    sock->type = type;

    if (sock->sbuf.type == NONE) {
        sbuf_init(&sock->sbuf, 10 * PGSIZE / sizeof(char));
        sock->sbuf.type = SOCKET_SBUF;
    }

    return fd;
}

uint64 socket_write(struct socket *sock, vaddr_t addr, int len) {
    paddr_t buf = getphyaddr(proc_current()->mm->pagetable, addr);

    int ret = 0;
    // TODO, fix len
    for (int i = 0; i < len; i++) {
        if(sock->used == 0) {
            break;
        }
        if(sock == NULL) {
            break;
        }
        if (sbuf_full(&sock->sbuf)) {
            // Log("break");
            break;
        }
        if (sbuf_insert(&sock->sbuf, 0, buf) < 0) {
            Warn("sendto failed");
            return -1;
        }
        ret++;
    }

    return ret;
}

uint64 socket_read(struct socket *sock, vaddr_t addr, int len) {
    paddr_t buf = getphyaddr(proc_current()->mm->pagetable, addr);

    int ret = 0;
    for (int i = 0; i < len; i++) {
        if(sock->used == 0) {
            break;
        }
        if(sock == NULL) {
            break;
        }
        if (sbuf_empty(&sock->sbuf)) {
            // Log("break");
            break;
        }
        if (sbuf_remove(&sock->sbuf, 0, buf) < 0) {
            Warn("sendto failed");
            return -1;
        }
        ret++;
    }

    return ret;
}
//    ssize_t sendto(int sockfd, const void *buf, size_t len, int flags,
//                   const struct sockaddr *dest_addr, socklen_t addrlen);
uint64 sys_sendto(void) {
    struct trapframe *tp = thread_current()->trapframe;
    int sockfd;
    struct file *fp;
    struct socket *sock;
    const struct sockaddr_in *sa = (const struct sockaddr_in *)getphyaddr(proc_current()->mm->pagetable, tp->a4);
    if (argfd(0, &sockfd, &fp) < 0) {
        Warn("argfd failed");
        return -1;
    }
    sock = fp->f_tp.f_sock;

    // if it's an unassigned UDP socket, assign a port
    if (sock->src_port == 0) {
        sock->src_port = ASSIGN_PORT;
        add_mapping(sock->src_port, sock);
    }

    paddr_t addr = tp->a1;
    size_t len = tp->a2;
    sock->dst_port = sa->sin_port;
    // info_socket(SYS_sendto, sockfd, sock);

    struct socket *dstsock = port2sock(sock->dst_port);

    return socket_write(dstsock, addr, len);
}



//        ssize_t recvfrom(int sockfd, void *buf, size_t len, int flags,
//                  struct sockaddr *src_addr, socklen_t *addrlen);
uint64 sys_recvfrom(void) {
    struct trapframe *tp = thread_current()->trapframe;
    // int sockfd = tp->a0;
    // paddr_t buf = getphyaddr(proc_current()->mm->pagetable, tp->a1);
    vaddr_t addr = tp->a1;
    // size_t len = tp->a2;
    const struct sockaddr_in *sa = (const struct sockaddr_in *)getphyaddr(proc_current()->mm->pagetable, tp->a4);
    size_t len = tp->a2;

    struct socket *sock = port2sock(sa->sin_port);

    return socket_read(sock, addr, len);
}

// int setsockopt(int sockfd, int level, int optname, const void *optval, socklen_t optlen);
uint64 sys_setsockopt(void) {

    return 0;
}

// int getsockopt(int sockfd, int level, int optname, void *optval, socklen_t *optlen);
uint64 sys_getsockopt(void) {
    
    return 0;
}


// //        int pselect(int nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds,
// //                      const struct timespec *timeout, const sigset_t *sigmask);
// uint64 sys_pselect6(void) {
//     int nfds;
//     paddr_t readfds;
//     // , *writefds, *exceptfds;
//     // const struct timespec *timeout;
//     // void *sigmask;
//     argint(0, &nfds);
//     argaddr(1, &readfds);
//     readfds = getphyaddr(proc_current()->mm->pagetable, readfds);
//     // argaddr(2, writefds);
//     // argaddr(3, exceptfds);

//     extern int print_tf_flag;
//     print_tf_flag = 1;

//     for (int i = 0; i <= nfds; i++) {
//         if (FD_ISSET(i, (fd_set *)readfds)) {
//             Log("num %d fd is set", i);
//         }
//     }

//     struct socket *sock;
//     while (1) {
//         sock = proc_current()->ofile[3]->f_tp.f_sock;
//         acquire(&sock->do_accept.sem_lock);
//         if (sock->do_accept.value > 0) {
//             release(&sock->do_accept.sem_lock);
//             break;
//         }
//         release(&sock->do_accept.sem_lock);
//     }
//     return 1;
// }

// int socketpair(int domain, int type, int protocol, int sv[2]);
uint64 sys_socketpair(void) {
    struct trapframe *tp = thread_current()->trapframe;
    int type = tp->a1;
    int *sv = (int *)getphyaddr(proc_current()->mm->pagetable, tp->a3);

    int fd1 = create_sockfp(type);
    int fd2 = create_sockfp(type);
    sv[0] = fd1;
    sv[1] = fd2;
    // Log("%d %d", fd1, fd2);

    return 0;
}
