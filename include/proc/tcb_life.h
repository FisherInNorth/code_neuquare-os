#ifndef __TCB_LIFE_H__
#define __TCB_LIFE_H__

#include "memory/memlayout.h"
#include "memory/allocator.h"
#include "kernel/kthread.h"
#include "proc/pcb_life.h"
#include "atomic/spinlock.h"
#include "atomic/ops.h"
#include "ipc/signal.h"
#include "lib/riscv.h"
#include "lib/queue.h"
#include "lib/timer.h"
#include "lib/list.h"
#include "common.h"

#define NTCB ((NPROC) * (NTCB_PER_PROC))

// thread state
enum thread_state { TCB_UNUSED,
                    TCB_USED,
                    TCB_RUNNABLE,
                    TCB_RUNNING,
                    TCB_SLEEPING,
                    TCB_STATEMAX };

typedef enum thread_state thread_state_t;

// callback for the first scheduled of thread
typedef void (*thread_callback)(void);

// thread group
struct thread_group {
    // spinlock
    spinlock_t lock;
    // thread group id, equals to pid
    tid_t tgid;
    // thread index within the group, start from 0
    int thread_idx;
    // count of threads, start from 1
    atomic_t thread_cnt;
    // for list
    struct list_head threads;
    // group leader : main thread
    struct tcb *group_leader;
};

// thread control block
struct tcb {
    // spinlock
    spinlock_t lock;
    // thread name (debugging)
    char name[20];
    // the state of thread
    thread_state_t state;
    // the proc pointer it belongs to
    struct proc *p;
    // thread id, global
    tid_t tid;
    // offset, local
    int tidx;
    // thread : killed ?
    int killed;
    // tcb state queue
    struct list_head state_list;
    // signal
    int sig_pending_cnt;       // have signal?
    struct sighand *sig;       // signal
    sigset_t blocked;          // the blocked signal
    struct sigpending pending; // pending (private)
    sig_t sig_ing;             // processing signal
    // kernel stack, trapframe and context
    uint64 kstack;               // kernel stack
    uint64 ustack;               // user stack
    struct trapframe *trapframe; // data page for trampoline.S
    struct context context;      // swtch() here to run thread
    // thread list
    struct list_head threads;
    // thread group
    struct thread_group *tg;
    // waiting queue list
    struct list_head wait_list;
    // waiting queue entry
    struct Queue *wait_chan_entry;
    /* CLONE_CHILD_SETTID: */
    uint64 set_child_tid;
    /* CLONE_CHILD_CLEARTID: */
    uint64 clear_child_tid;
    // used for nanosleep and futex
    uint64 time_out;
};

// =============================== tid management =========================
#define alloc_tid (atomic_inc_return(&next_tid))
#define cnt_tid_inc (atomic_inc_return(&count_tid))
#define cnt_tid_dec (atomic_dec_return(&count_tid))
struct tcb *find_get_tid(tid_t tid);
struct tcb *find_get_tidx(int pid, int tidx);

// ============================== the life of a thread ====================
void tcb_init(void);
struct tcb *thread_current(void);
struct tcb *alloc_thread(thread_callback callback);
void create_thread(struct proc *p, struct tcb *t, char *name, thread_callback callback);
void tginit(struct thread_group *tg);

void thread_forkret(void);
int do_sleep_ns(struct tcb *t, struct timespec ts);
void free_thread(struct tcb *t);
int thread_killed(struct tcb *t);
void thread_setkilled(struct tcb *t);

// ============================= thread signal ==========================
void sighandinit(struct tcb *t);
void do_tkill(struct tcb *t, sig_t signo);
void thread_send_signal(struct tcb *t_cur, siginfo_t *info);

// ============================= process and thread =====================
int proc_join_thread(struct proc *p, struct tcb *t, char *name);
void proc_sendsignal_all_thread(struct proc *p, sig_t signo, int opt);

#endif