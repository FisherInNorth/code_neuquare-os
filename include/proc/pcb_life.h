#ifndef __PCB_LIFE_H__
#define __PCB_LIFE_H__

#include "atomic/spinlock.h"
#include "kernel/kthread.h"
#include "atomic/semaphore.h"
#include "memory/mm.h"
#include "ipc/signal.h"
#include "ipc/shm.h"
#include "lib/resource.h"
#include "lib/timer.h"
#include "lib/list.h"
#include "common.h"
#include "param.h"

// #define INIT_PID 1
// #define SHELL_PID 2

struct file;
struct inode;
struct rlimit;

// process state
enum procstate { PCB_UNUSED,
                 PCB_USED,
                 PCB_ZOMBIE,
                 PCB_STATEMAX };

// shared memory
struct sysv_shm {
    struct list_head shm_clist;
};

// process
struct proc {
    // the spinlock protecting proc
    struct spinlock lock;
    // process name (debugging)
    char name[20];
    // process id
    pid_t pid;
    // process state
    enum procstate state;
    // exit status to be returned to parent's wait
    int exit_state;
    // if non-zero, have been killed
    int killed;
    // memory management
    struct mm_struct *mm;
    // open files table
    struct file *ofile[NOFILE];
    int max_ofile;
    int cur_ofile;
    // current directory
    struct inode *cwd;
    // proc state queue
    struct list_head state_list; // its state queue
    // proc children and siblings
    struct proc *parent;           // parent
    struct proc *first_child;      // its first child!!!! (one direction)
    struct list_head sibling_list; // its sibling
    // thread group
    struct thread_group *tg;
    // for clone
    pid_t ctid;
    // thread lock
    struct semaphore tlock;
    // ipc name space
    struct ipc_namespace *ipc_ns;
    // system V shared memory
    struct sysv_shm sysvshm;
    // for prlimit
    struct rlimit rlim[RLIM_NLIMITS];
    // for waitpid
    struct semaphore sem_wait_chan_parent;
    struct semaphore sem_wait_chan_self;
    // for setitimer
    struct timer_list real_timer;
    // for futex
    struct robust_list_head *robust_list;

    uint64 utime, stime, last_in, last_out;
    uint64 stub_time;
    // // signal
    // int sig_pending_cnt;                   // have signal?
    // struct sighand *sig;        // signal
    // sigset_t blocked;                 // the blocked signal
    // struct sigpending pending;        // pending (private)

    // system mode time and user mode time
    // long tms_stime;   // system mode time(ticks)
    // long tms_utime;   // user mode time(ticks)
    // long create_time; // create time(ticks)
    // long enter_time;  // enter kernel time(ticks)
};

// ======================= pid management ==========================
#define alloc_pid (atomic_inc_return(&next_pid))
#define cnt_pid_inc (atomic_inc_return(&count_pid))
#define cnt_pid_dec (atomic_dec_return(&count_pid))
struct proc *find_get_pid(pid_t pid);

// ======================= process family tree =====================
#define nochildren(p) (p->first_child == NULL)
#define nosibling(p) (list_empty(&p->sibling_list))
#define firstchild(p) (p->first_child)
#define nextsibling(p) (list_first_entry(&(p->sibling_list), struct proc, sibling_list))

// ======================= process management =======================
void proc_init(void);
void init_ret(void);
struct proc *proc_current(void);
struct proc *alloc_proc(void);
struct proc *create_proc();
void free_proc(struct proc *p);
void proc_setkilled(struct proc *p);
int proc_killed(struct proc *p);

void deleteChild(struct proc *parent, struct proc *child);
void appendChild(struct proc *parent, struct proc *child);
void proc_prlimit_init(struct proc *p);

// ======================= the life of a process =====================
int do_clone(uint64 flags, vaddr_t stack, uint64 ptid, uint64 tls, uint64 ctid);
void exit_wakeup(struct proc *p, struct tcb *t); // for futex
void do_exit_group(struct proc *p);
void do_exit(int status);
int waitpid(pid_t pid, uint64 status, int options);
void reparent(struct proc *p);

// ========================== debug ===========================
void print_clone_flags(int flags);
void procChildrenChain(struct proc *p);
void proc_thread_print(void);
uint8 get_current_procs();

#endif