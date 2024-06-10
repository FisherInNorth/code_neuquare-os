#ifndef __FMUTEX_H__
#define __FMUTEX_H__
#include "atomic/spinlock.h"
#include "lib/queue.h"
#include "common.h"

#define FUTEX_PRIVATE_FLAG 128
// 1000 0000
#define FUTEX_CLOCK_REALTIME 256
// 0001 0000 0000
#define FUTEX_CMD_MASK ~(FUTEX_PRIVATE_FLAG | FUTEX_CLOCK_REALTIME)
#define FUTEX_WAIT 0
#define FUTEX_WAKE 1
#define FUTEX_REQUEUE 3

#define FUTEX_LOCK_PI 6
#define FUTEX_WAIT_BITSET 9
#define FUTEX_WAIT_REQUEUE_PI 11
#define FUTEX_WAIT_REQUEUE_PI_PRIVATE (FUTEX_WAIT_REQUEUE_PI | FUTEX_PRIVATE_FLAG)

#define FUTEX_CMP_REQUEUE 4
#define FUTEX_CMP_REQUEUE_PI 12
#define FUTEX_WAKE_OP 5

#define FLAGS_SHARED 0x00
#define FLAGS_CLOCKRT 0x02
#define FUTEX_BITSET_MATCH_ANY 0xffffffff

struct futex {
    struct spinlock lock; // uncessary, using p->lock is ok
    struct Queue waiting_queue;
};

struct robust_list {
    struct robust_list *next;
};

struct robust_list_head {
    /*
     * The head of the list. Points back to itself if empty:
     */
    struct robust_list list;
    /*
     * This relative offset is set by user-space, it gives the kernel
     * the relative position of the futex field to examine. This way
     * we keep userspace flexible, to freely shape its data-structure,
     * without hardcoding any particular offset into the kernel:
     */
    long futex_offset;
    /*
     * The death of the thread may race with userspace setting
     * up a lock's links. So to handle this race, userspace first
     * sets this field to the address of the to-be-taken lock,
     * then does the lock acquire, and then adds itself to the
     * list, and then clears this field. Hence the kernel will
     * always have full knowledge of all locks that the thread
     * _might_ have taken. We check the owner TID in any case,
     * so only truly owned locks will be handled.
     */
    struct robust_list *list_op_pending;
};

struct futex *get_futex(uint64 uaddr, int assert);
void futex_init(struct futex *fp, char *name);
int futex_wait(uint64 uaddr, uint val, struct timespec *ts);
int futex_wakeup(uint64 uaddr, int nr_wake);
int futex_requeue(uint64 uaddr1, int nr_wake, uint64 uaddr2, int nr_requeue);

int do_futex(uint64 uaddr, int op, uint32 val, struct timespec *ts, uint64 uaddr2, uint32 val2, uint32 val3);

#endif