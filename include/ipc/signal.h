#ifndef __SIGNAL_H__
#define __SIGNAL_H__
#include "common.h"
#include "atomic/ops.h"
#include "atomic/spinlock.h"
#include "lib/list.h"
#include "kernel/trap.h"

#define SIGHUP 1
#define SIGINT 2
#define SIGQUIT 3
#define SIGTERM 15
#define SIGSTOP 17

#define SIGKILL 9
#define SIGTSTP 18
#define SIGALRM 14
#define SIGSEGV 11
#define SIGCHLD 20

#define SIGCANCEL 33

typedef void __signalfn_t(int);
typedef __signalfn_t *__sighandler_t;
typedef uint64 sig_t;
#define _NSIG 64
#define valid_signal(sig) (((sig) <= _NSIG && (sig) >= 1) ? 1 : 0)

// how to process signal
#define SA_NOCLDSTOP 0x00000004
#define SA_NOCLDWAIT 0x00000020
#define SA_SIGINFO 0x00000040
#define SA_ONSTACK 0x00000001
#define SA_RESETHAND 0x00000010

#define SA_NODEFER 0x00000008
#define SA_NOMASK SA_NODEFER

#define SA_RESETHAND 0x00000010
#define SA_ONESHOT SA_RESETHAND

#define SI_USER 0      /* sent by kill, sigsend, raise */
#define SI_KERNEL 0x80 /* sent by the kernel from somewhere */

#define SIG_DFL ((__sighandler_t)0)  /* default signal handling */
#define SIG_IGN ((__sighandler_t)1)  /* ignore signal */
#define SIG_ERR ((__sighandler_t)-1) /* error return from signal */

// signal info
typedef struct {
    int si_signo;
    int si_code;
    pid_t si_pid;
} siginfo_t;

// signal sets
typedef struct {
    uint64 sig;
} sigset_t;

// the pointer to the signal handler
struct sigaction {
    __sighandler_t sa_handler;
    uint sa_flags;
    sigset_t sa_mask;
};

// signal process
struct sighand {
    spinlock_t siglock;
    atomic_t ref;
    struct sigaction action[_NSIG];
};

// pending signal queue head of proc
struct sigpending {
    struct list_head list;
    sigset_t signal;
};

// signal queue struct
struct sigqueue {
    struct list_head list;
    int flags;
    siginfo_t info;
};

// signal bit op
#define sig_empty_set(set) (memset(set, 0, sizeof(sigset_t)))
#define sig_fill_set(set) (memset(set, -1, sizeof(sigset_t)))
#define sig_add_set(set, sig) (set.sig |= 1UL << (sig - 1))
#define sig_del_set(set, sig) (set.sig &= ~(1UL << (sig - 1)))
#define sig_add_set_mask(set, mask) (set.sig |= (mask))
#define sig_del_set_mask(set, mask) (set.sig &= (~mask))
#define sig_is_member(set, n_sig) (1 & (set.sig >> (n_sig - 1)))
#define sig_gen_mask(sig) (1UL << (sig - 1))
#define sig_or(x, y) ((x) | (y))
#define sig_and(x, y) ((x) & (y))
#define sig_test_mask(set, mask) ((set.sig & mask) != 0)
#define sig_pending(t) (t.sig_pending)
#define sig_ignored(t, sig) (sig_is_member(t->blocked, sig))
#define sig_existed(t, sig) (sig_is_member(t->pending.signal, sig))
#define sig_action(t, signo) (t->sig->action[signo - 1])

typedef struct sigaltstack {
    void *ss_sp;
    int ss_flags;
    size_t ss_size;
} stack_t;

struct user_regs_struct {
    uint64 epc;
    uint64 ra;
    uint64 sp;
    uint64 gp;
    uint64 tp;
    uint64 t0;
    uint64 t1;
    uint64 t2;
    uint64 s0;
    uint64 s1;
    uint64 a0;
    uint64 a1;
    uint64 a2;
    uint64 a3;
    uint64 a4;
    uint64 a5;
    uint64 a6;
    uint64 a7;
    uint64 s2;
    uint64 s3;
    uint64 s4;
    uint64 s5;
    uint64 s6;
    uint64 s7;
    uint64 s8;
    uint64 s9;
    uint64 s10;
    uint64 s11;
    uint64 t3;
    uint64 t4;
    uint64 t5;
    uint64 t6;
};

struct sigcontext {
    struct trapframe tf;
    // struct user_regs_struct sc_regs;
    // union __riscv_fp_state sc_fpregs;
};

struct ucontext {
    // uint64 uc_flags;
    // struct ucontext *uc_link;
    // stack_t uc_stack;
    struct sigcontext uc_mcontext;
    sigset_t uc_sigmask; /* mask last for extensibility */
    sig_t sig_ing;
};

struct __riscv_mc_f_ext_state {
	unsigned int __f[32];
	unsigned int __fcsr;
};

struct __riscv_mc_d_ext_state {
	unsigned long long __f[32];
	unsigned int __fcsr;
};

struct __riscv_mc_q_ext_state {
	unsigned long long __f[64] __attribute__((aligned(16)));
	unsigned int __fcsr;
	unsigned int __reserved[3];
};

union __riscv_mc_fp_state {
	struct __riscv_mc_f_ext_state __f;
	struct __riscv_mc_d_ext_state __d;
	struct __riscv_mc_q_ext_state __q;
};

typedef unsigned long __riscv_mc_gp_state[32];

typedef struct mcontext_t {
	__riscv_mc_gp_state __gregs;
	union __riscv_mc_fp_state __fpregs;
} mcontext_t;

typedef struct __ucontext
{
	unsigned long uc_flags;
	struct __ucontext *uc_link;
	struct sigaltstack uc_stack;
	struct { unsigned long __bits[128/sizeof(long)]; } uc_sigmask;
	mcontext_t uc_mcontext;
} ucontext_t;

struct rt_sigframe {
    // struct siginfo info;
    ucontext_t uc_riscv;
    struct ucontext uc;
};

struct proc;
struct tcb;

#define SIG_BLOCK 0   /* for blocking signals */
#define SIG_UNBLOCK 1 /* for unblocking signals */
#define SIG_SETMASK 2 /* for setting the signal mask */

int signal_queue_pop(uint64 mask, struct sigpending *pending);
int signal_queue_flush(struct sigpending *queue);
void signal_info_init(sig_t sig, siginfo_t *info, int opt);
int signal_send(siginfo_t *info, struct tcb *t);
void sigpending_init(struct sigpending *sig);
int signal_handle(struct tcb *t);
int do_handle(struct tcb *t, int sig_no, struct sigaction *sig_act);
void signal_DFL(struct tcb *t, sig_t signo);
int do_sigaction(int sig, struct sigaction *act, struct sigaction *oact);
int do_sigprocmask(int how, sigset_t *set, sigset_t *oldset);
int setup_rt_frame(struct sigaction *sig, sig_t signo, sigset_t *set, struct trapframe *tf);
// int signal_frame_setup(sigset_t *set, struct trapframe *tf, struct rt_sigframe *rtf);
int signal_frame_setup(sigset_t *set, struct trapframe *tf, struct rt_sigframe *rtf, sig_t signo);
int signal_frame_restore(struct tcb *t, struct rt_sigframe *rtf);
// debug
void print_signal_mask(sigset_t sigmask);

#endif