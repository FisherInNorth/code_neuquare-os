#include "atomic/ops.h"
#include "atomic/spinlock.h"
#include "atomic/cond.h"
#include "atomic/futex.h"
#include "memory/memlayout.h"
#include "memory/allocator.h"
#include "memory/vm.h"
#include "memory/vma.h"
#include "memory/binfmt.h"
#include "kernel/trap.h"
#include "kernel/cpu.h"
#include "proc/pcb_life.h"
#include "proc/sched.h"
#include "proc/pcb_life.h"
#include "proc/pcb_mm.h"
#include "proc/pdflush.h"
#include "proc/options.h"
#include "ipc/signal.h"
#include "fs/stat.h"
#include "fs/vfs/fs.h"
#include "fs/vfs/ops.h"
#include "fs/fat/fat32_file.h"
#include "lib/hash.h"
#include "lib/queue.h"
#include "lib/riscv.h"
#include "lib/list.h"
#include "common.h"
#include "param.h"
#include "debug.h"
#include "test.h"

extern Queue_t unused_p_q, used_p_q, zombie_p_q;
extern Queue_t unused_t_q, runnable_t_q, sleeping_t_q;
extern Queue_t *STATES[PCB_STATEMAX];
extern struct tcb thread[NTCB];
extern struct hash_table pid_map;

struct proc proc[NPROC];
struct proc *initproc;
char proc_lock_name[NPROC][10];
atomic_t next_pid;
atomic_t count_pid;

// initialize the proc table.
void proc_init(void) {
    struct proc *p;
    atomic_set(&next_pid, 1);
    atomic_set(&count_pid, 0);

    PCB_Q_ALL_INIT();

    for (int i = 0; i < NPROC; i++) {
        p = proc + i;
        sprintf(proc_lock_name[i], "proc_%d", i);
        initlock(&p->lock, proc_lock_name[i]);
        sema_init(&p->tlock, 1, "sem_ofile");
        p->state = PCB_UNUSED;
        Queue_push_back_atomic(&unused_p_q, p);
    }
    Info("========= Information of proc table and tcb table ==========\n");
    Info("number of proc : %d\n", NPROC);
    Info("proc table init [ok]\n");
    return;
}

// Return the current struct tcb *, or zero if none.
struct proc *proc_current(void) {
    return thread_current()->p;
}

void deleteChild(struct proc *parent, struct proc *child) {
    if (nosibling(child)) {
        parent->first_child = NULL;
    } else {
        struct proc *firstchild = firstchild(parent);
        if (child == firstchild) {
            parent->first_child = nextsibling(firstchild);
        }
        list_del_reinit(&child->sibling_list);
    }
}

void appendChild(struct proc *parent, struct proc *child) {
    if (nochildren(parent)) {
        parent->first_child = child;
    } else {
        list_add_tail(&child->sibling_list, &(firstchild(parent)->sibling_list));
    }
}

// allocate a new proc (return with lock)
struct proc *alloc_proc(void) {
    struct proc *p;

    // fetch a unused proc from unused queue
    p = (struct proc *)Queue_provide_atomic(&unused_p_q, 1);
    if (p == NULL)
        return 0;

    // return with lock
    acquire(&p->lock);

    // allocate pid
    p->pid = alloc_pid;
    cnt_pid_inc;

    // proc family
    p->first_child = NULL;
    INIT_LIST_HEAD(&p->sibling_list);

    // allocate memory
    // slob TODO
    p->mm = alloc_mm();
    if (p->mm == NULL) {
        Warn("fix error handler!");
        free_proc(p);
        release(&p->lock);
        return 0;
    }

    // thread group (list head) sets to NULL
    if ((p->tg = (struct thread_group *)kalloc()) == 0) {
        free_proc(p);
        release(&p->lock);
        return 0;
    }
    tginit(p->tg);

    // ipc namespace
    if ((p->ipc_ns = (struct ipc_namespace *)kalloc()) == 0) {
        free_proc(p);
        release(&p->lock);
        return 0;
    }

    // shared memory
    shm_init_ns(p->ipc_ns);

    // for prlimit
    proc_prlimit_init(p);

    // setitimer
    p->real_timer.expires = 0;
    p->real_timer.interval = 0;
    p->stub_time = rdtime();
    p->utime = 0;
    p->stime = 0;

    // bug!!!
    INIT_LIST_HEAD(&p->real_timer.list);

    // bug!!!
    INIT_LIST_HEAD(&p->sysvshm.shm_clist);

    // state
    PCB_Q_changeState(p, PCB_USED);

    // semaphore for waitpid
    sema_init(&p->sem_wait_chan_parent, 0, "wait_parent");
    sema_init(&p->sem_wait_chan_self, 0, "wait_self");

    // map <pid, p>
    hash_insert(&pid_map, (void *)&(p->pid), (void *)p, 0); // not holding it
    return p;
}

// create a proc with a group leader thread
struct proc *create_proc() {
    struct tcb *t = NULL;
    struct proc *p = NULL;

    if ((p = alloc_proc()) == 0) {
        return 0;
    }

    if ((t = alloc_thread(thread_forkret)) == 0) {
        free_proc(p);
        return 0;
    }

    proc_join_thread(p, t, NULL);

    release(&t->lock);

    return p;
}

// free a existed proc
void free_proc(struct proc *p) {
    // free_mm will write back, must release the lock of p?
    release(&p->lock); // bug for iozone
    free_mm(p->mm, p->tg->thread_idx);
    acquire(&p->lock); // bug for iozone

    if (p->tg)
        kfree((void *)p->tg);
    p->tg = 0;
    if (p->ipc_ns) {
        // bug!!!
        if(shm_ids(p->ipc_ns).key_ht) {
            hash_destroy(shm_ids(p->ipc_ns).key_ht, 1);// free hash table
        }
        kfree((void *)p->ipc_ns);
    }
    p->ipc_ns = 0;

    // shared memory
    if(!list_empty(&p->sysvshm.shm_clist)) {
        struct shmid_kernel* shmid_cur = NULL;
        struct shmid_kernel* shmid_tmp = NULL;
        list_for_each_entry_safe(shmid_cur, shmid_tmp, &p->sysvshm.shm_clist, shm_clist) {
            kfree(shmid_cur);
            kfree(shmid_cur->shm_file->private_data);
        }
    }

    // delete <pid, t>
    hash_delete(&pid_map, (void *)&p->pid, 0, 1); // not holding lock , release lock

    cnt_pid_dec;
    p->pid = 0;
    p->parent = 0;
    p->name[0] = 0;
    p->killed = 0;
    p->exit_state = 0;

    PCB_Q_changeState(p, PCB_UNUSED);
}

struct file console;
void userinit(void) {
    struct proc *p = NULL;

    p = create_proc();

    safestrcpy(p->name, "/init", 10);
    safestrcpy(p->tg->group_leader->name, "/init-0", 10);

    p->mm->brk = 0;

    initproc = p;

    TCB_Q_changeState(p->tg->group_leader, TCB_RUNNABLE);
    release(&p->lock);

    return;
}

// init proc based on FAT32 file system
void init_ret(void) {
    extern struct _superblock fat32_sb;
    fat32_fs_mount(ROOTDEV, &fat32_sb); // initialize fat32 superblock obj and root inode obj.
    proc_current()->cwd = fat32_sb.root->i_op->idup(fat32_sb.root);
#ifdef SUBMIT
    Info("======== submit-init return ========\n");
    printfGreen("The initial Memory before execve init : %d pages\n", get_free_mem()/4096);
    return;
#else
    struct binprm bprm;
    memset(&bprm, 0, sizeof(bprm));
    
    proc_current()->tg->group_leader->trapframe->a0 = do_execve("/boot/init", &bprm);
#endif
}

// find the proc we search using hash map
inline struct proc *find_get_pid(pid_t pid) {
    struct hash_node *node = hash_lookup(&pid_map, (void *)&pid, NULL, 1, 0); // realese it, not holiding it
    return node != NULL ? (struct proc *)(node->value) : NULL;
}

// A fork child's very first scheduling by scheduler()
// will swtch to forkret.
void thread_forkret(void) {
    // Still holding p->lock from scheduler.
    release(&thread_current()->lock);
    if (thread_current() == initproc->tg->group_leader) {
        init_ret();
    }
    // printfRed("tid : %d , name : %s forkret\n", thread_current()->tid, thread_current()->name);// debug
    
    // trapframe_print(thread_current()->trapframe);// debug
    proc_current()->last_in = proc_current()->last_out = rdtime();
    thread_usertrapret();
}

int do_clone(uint64 flags, vaddr_t stack, uint64 ptid, uint64 tls, uint64 ctid) {

    // printfGreen("clone start, mm: %d pages\n", get_free_mem()/4096);
    int pid;
    struct proc *p = proc_current();
    struct proc *np = NULL;
    struct tcb *t = NULL;

    // extern int print_tf_flag;
    // print_tf_flag = 1;
    // print_clone_flags(flags);
    // #ifdef __STRACE__
    //     print_clone_flags(flags);
    // #endif
    if (flags & CLONE_THREAD) {
        if ((t = alloc_thread(thread_forkret)) == 0) {
            return -1;
        }
        // extern int print_tf_flag;
        // print_tf_flag = 1;
        if (proc_join_thread(p, t, NULL) < 0) {
            free_thread(t);
        }
#ifdef __DEBUG_THREAD__
        printfRed("clone a thread, pid : %d, tid : %d\n", p->pid, t->tid);
#endif
    } else {
        // Allocate process.
        if ((np = create_proc()) == 0) {
            // proc_thread_print();
            return -1;
        }
        t = np->tg->group_leader; // !!!
    }

    // print_clone_flags(flags);
    // ==============create thread for proc=======================
    // copy saved user registers.
    *(t->trapframe) = *(p->tg->group_leader->trapframe);

    // Cause fork to return 0 in the child.
    t->trapframe->a0 = 0;

    // set the tls (Thread-local Storage，TLS)
    // RISC-V使用TP寄存器
    if (flags & CLONE_SETTLS) {
        t->trapframe->tp = tls;
    }

    if (flags & CLONE_CHILD_SETTID) {
        if (copyout(p->mm->pagetable, ctid, (char *)&t->tid, sizeof(tid_t)) < 0)
            return -1;
    }

    if (flags & CLONE_CHILD_CLEARTID) {
        // when the child exits,  Clear (zero)
        t->clear_child_tid = ctid;
    }

    if (stack) {
        t->trapframe->sp = stack;
    }

    if (flags & CLONE_SIGHAND) {
        t->sig = p->tg->group_leader->sig;
        atomic_inc_return(&t->sig->ref); // !!! bug
        // TODO : sigmask??
    } else {
        sighandinit(t);
    }

    // store the parent pid
    if (flags & CLONE_PARENT_SETTID) {
        if (copyout(p->mm->pagetable, ptid, (char *)&t->tid, sizeof(tid_t)) == -1)
            return -1;
    }

    // for pthread create
    if (np == NULL) {
        // acquire(&t->lock);
        TCB_Q_changeState(t, TCB_RUNNABLE);
        release(&t->lock);
        return t->tid;
    }

    // ==============create proc with group leader=======================
    acquire(&p->lock);
    /* Copy vma */
    if (vmacopy(p->mm, np->mm) < 0) {
        free_proc(np);
        release(&p->lock);
        release(&np->lock);
        return -1;
    }

    // Copy user memory from parent to child.
    if (flags & CLONE_VM) {
        np->mm->pagetable = p->mm->pagetable;
    } else {
        if (uvmcopy(p->mm, np->mm) < 0) {
            free_proc(np);
            release(&p->lock);
            release(&np->lock);
            return -1;
        }
    }

    np->mm->start_brk = p->mm->start_brk;
    np->mm->brk = p->mm->brk;
    release(&p->lock);
    // increment reference counts on open file descriptors.
    if (flags & CLONE_FILES) {
    } else {
        for (int i = 0; i < NOFILE; i++)
            if (p->ofile[i])
                // np->ofile[i] = fat32_filedup(p->ofile[i]);
                np->ofile[i] = p->ofile[i]->f_op->dup(p->ofile[i]);
            // else 
            //     break;// to speed up?
        // TODO : clone a completely same fdtable   >> not necessary
    }

    // if (flags & CLONE_NEWIPC) {
    // } else {
    //     np->ipc_ns = p->ipc_ns;
    // }

    // TODO : vfs inode cmd  >> Done
    np->cwd = p->cwd->i_op->idup(p->cwd);

    safestrcpy(np->name, p->name, sizeof(p->name));

    pid = np->pid;
    np->parent = p;

    acquire(&p->lock);
    appendChild(p, np);
    release(&p->lock);
    release(&np->lock);

#ifdef __DEBUG_PROC__
    printfRed("clone : %d -> %d\n", p->pid, np->pid); // debug
#endif

    // printfGreen("clone end, mm: %d pages\n", get_free_mem()/4096);
    acquire(&t->lock);
    TCB_Q_changeState(t, TCB_RUNNABLE);
    release(&t->lock);


    return pid;
}

int exit_process(int status) {
    struct proc *p = proc_current();
    if (p == initproc)
        panic("init exiting");

    // private to proc, no need to acquire
    for (int fd = 0; fd < NOFILE; fd++) {
        if (p->ofile[fd]) {
            struct file *f = p->ofile[fd];
            generic_fileclose(f);
            p->ofile[fd] = 0;
        }
    }
    fat32_inode_put(p->cwd);
    p->cwd = 0;

    // !!! bug
    delete_timer_atomic(&p->real_timer);

    // Give any children to init.
    reparent(p);

    acquire(&p->lock);
    PCB_Q_changeState(p, PCB_ZOMBIE);
    p->exit_state = status << 8;
    sema_signal(&p->parent->sem_wait_chan_parent);
    sema_signal(&p->sem_wait_chan_self);

#ifdef __DEBUG_PROC__
    printfGreen("exit : %d has exited\n", p->pid);                // debug
    printfGreen("exit : %d wakeup %d\n", p->pid, p->parent->pid); // debug
#endif
    return 0;
}

void exit_wakeup(struct proc *p, struct tcb *t) {
    // bug !!!
    if (t->clear_child_tid) {
        int val = 0;
        if (copyout(p->mm->pagetable, t->clear_child_tid, (char *)&val, sizeof(val)))
            panic("exit error\n");
        futex_wakeup(t->clear_child_tid, 1);
    }
}

void do_exit(int status) {
    struct proc *p = proc_current();
    struct tcb *t = thread_current();
    struct thread_group *tg = p->tg;

    exit_wakeup(p, t);
    // !!!! =======atomic=======
    int last_thread = 0;
    if (!(atomic_dec_return(&tg->thread_cnt) - 1)) {
        exit_process(status);
        last_thread = 1;
    } else {
    }
    acquire(&tg->lock);
    list_del_reinit(&t->threads);
    release(&tg->lock);

#ifdef __DEBUG_THREAD__
    printfMAGENTA("exit a thread start, pid : %d, tid : %d\n", t->p->pid, t->tid);
#endif
    acquire(&t->lock);
    free_thread(t);
#ifdef __DEBUG_THREAD__
    printfMAGENTA("exit a thread end, pid : %d, tid : %d\n", t->p->pid, t->tid);
#endif
    if (last_thread) {
        release(&p->lock);
    }
    // !!!! =======atomic=======
    thread_sched();
    panic("do_exit should never return");
    return;
}

int waitpid(pid_t pid, uint64 status, int options) {
    struct proc *p = proc_current();
    if (pid < -1)
        pid = -pid;

    ASSERT(pid != 0);

    if (nochildren(p)) {
#ifdef __DEBUG_PROC__
        printf("wait : %d hasn't children\n", p->pid); // debug
#endif
        return -1;
    }

    if (proc_killed(p)) {
#ifdef __DEBUG_PROC__
        printf("wait : %d has been killed\n", p->pid); // debug
#endif
        return -1;
    }
    while (1) {
        sema_wait(&p->sem_wait_chan_parent);
#ifdef __DEBUG_PROC__
        printfBlue("wait : %d wakeup\n", p->pid); // debug
#endif
        struct proc *p_child = NULL;
        struct proc *p_tmp = NULL;
        struct proc *p_first = firstchild(p);
        int flag = 1;
        list_for_each_entry_safe_given_first(p_child, p_tmp, p_first, sibling_list, flag) {
            // shell won't exit !!!
            if (pid > 0 && p_child->pid == pid) {
                sema_wait(&p_child->sem_wait_chan_self);
#ifdef __DEBUG_PROC__
                printfBlue("wait : %d wakeup self %d\n", p->pid, p_child->pid); // debug
#endif
            }
            acquire(&p_child->lock);
            if (p_child->state == PCB_ZOMBIE) {
                // if(p==initproc)
                //     printfRed("唤醒,pid : %d, %d\n",p_child->pid, ++cnt_wakeup); // debug
                // ASSERT(p_child->pid!=SHELL_PID);
                pid = p_child->pid;
                if (status != 0 && copyout(p->mm->pagetable, status, (char *)&(p_child->exit_state), sizeof(p_child->exit_state)) < 0) {
                    release(&p_child->lock);
                    return -1;
                }
                // ASSERT(list_empty(&p_child->tg->threads)); // !!!
                free_proc(p_child);

                acquire(&p->lock);
                deleteChild(p, p_child);
                release(&p->lock);

                release(&p_child->lock);

#ifdef __DEBUG_PROC__
                printfBlue("wait : %d delete %d\n", p->pid, pid); // debug
#endif
                return pid;
            }
            release(&p_child->lock);
        }
        printf("%d\n", p->pid);
        // printf("%d\n", p->sem_wait_chan_parent.value);
        // if(p==initproc) {
        //     procChildrenChain(initproc);
        // }
        panic("waitpid : invalid wakeup for semaphore!");
    }
}

// Pass p's abandoned children to init.
// Caller must hold wait_lock.
void reparent(struct proc *p) {
    struct proc *p_child = NULL;
#ifdef __DEBUG_PROC__
    printf("reparent : %d is going to exit and reparent its children\n", p->pid); // debug
#endif
    if (!nochildren(p)) {
        struct proc *p_first_c = firstchild(p);
        struct proc *p_tmp = NULL;
        int flag = 1;

        list_for_each_entry_safe_given_first(p_child, p_tmp, p_first_c, sibling_list, flag) {
            acquire(&p_child->lock);

            acquire(&p->lock);
            deleteChild(p, p_child);
            release(&p->lock);
            // maybe the lock of initproc can be removed

            acquire(&initproc->lock);
            appendChild(initproc, p_child);
            release(&initproc->lock);

            p_child->parent = initproc;
            if (p_child->state == PCB_ZOMBIE) {
                sema_signal(&initproc->sem_wait_chan_parent); // !!!!!
#ifdef __DEBUG_PROC__
                printfBWhite("reparent : zombie %d has exited\n", p_child->pid); // debug
                printfBWhite("reparent : zombie %d wakeup 1\n", p_child->pid);   // debug
#endif
            }
            release(&p_child->lock);

#ifdef __DEBUG_PROC__
            printf("reparent : %d reparent %d -> 1\n", p->pid, p_child->pid); // debug
#endif
            if (p->first_child == NULL) {
                break; // !!!!
            }
        }
        // procChildrenChain(initproc);
        ASSERT(nochildren(p));
    } else {
#ifdef __DEBUG_PROC__
        printf("reparent : %d has no children\n", p->pid); // debug
#endif
    }
}

void proc_setkilled(struct proc *p) {
    acquire(&p->lock);
    p->killed = 1;
    release(&p->lock);
}

int proc_killed(struct proc *p) {
    int k;

    acquire(&p->lock);
    k = p->killed;
    release(&p->lock);
    return k;
}

uint8 get_current_procs() {
    // TODO : add lock to proc table??(maybe)
    uint8 procs = 0;
    struct proc *p;
    for (p = proc; p < &proc[NPROC]; p++) {
        acquire(&p->lock);
        if (p->state != PCB_UNUSED) {
            procs++;
        }
        release(&p->lock);
    }
    return procs;
}

// debug
static char *PCB_states[] = {
    [PCB_UNUSED] "pcb_unused",
    [PCB_USED] "pcb_used",
    [PCB_ZOMBIE] "pcb_zombie"};
static char *TCB_states[] = {
    [TCB_UNUSED] "tcb_unused",
    [TCB_USED] "tcb_used",
    [TCB_RUNNABLE] "tcb_runnable",
    [TCB_RUNNING] "tcb_running",
    [TCB_SLEEPING] "tcb_sleeping"};

void printProcessTree(struct proc *p, int indent) {
    if (p->state == PCB_UNUSED) {
        return;
    }

    for (int i = 0; i < indent * 4; i++) {
        printf(" ");
    }

    printf("pid %d %s %s\n", p->pid, PCB_states[p->state], p->name);

    struct tcb *t_cur = NULL;
    acquire(&p->tg->lock);
    list_for_each_entry(t_cur, &p->tg->threads, threads) {
        acquire(&t_cur->lock);
        for (int i = 0; i < (indent + 1) * 4; i++) {
            printf(" ");
        }
        printf("└─tid %d %s %s\n", t_cur->tid, TCB_states[t_cur->state], t_cur->name);
        release(&t_cur->lock);
    }
    release(&p->tg->lock);
    printf("\n");

    if (nochildren(p)) {
        return;
    }
    struct proc *p_child = NULL;
    struct proc *p_tmp = NULL;
    struct proc *p_first = firstchild(p);
    int flag = 1;
    list_for_each_entry_safe_given_first(p_child, p_tmp, p_first, sibling_list, flag) {
        acquire(&p_child->lock);
        printProcessTree(p_child, indent + 1);
        release(&p_child->lock);
    }
}

void proc_prlimit_init(struct proc *p) {
    struct rlimit *rlim = p->rlim;
    struct rlimit *rlim_tmp = NULL;
    // RLIMIT_NPROC
    rlim_tmp = rlim + RLIMIT_NOFILE;
    rlim_tmp->rlim_max = NOFILE;
    rlim_tmp->rlim_cur = NOFILE;
    p->max_ofile = NOFILE;

    // PLIMIT_STACK
    rlim_tmp = rlim + RLIMIT_STACK;
    rlim_tmp->rlim_max = USTACK_PAGE;
    rlim_tmp->rlim_cur = 0;
}

// This  system  call  is equivalent to _exit(2) except that it terminates not only the calling thread, but
// all threads in the calling process's thread group.
void do_exit_group(struct proc *p) {
    struct tcb *caller = thread_current();
    struct thread_group *tg = p->tg;
    struct tcb *t_cur = NULL;
    struct tcb *t_tmp = NULL;
    acquire(&p->tg->lock);
    list_for_each_entry_safe(t_cur, t_tmp, &tg->threads, threads) {
        if (t_cur == caller)
            continue;
        acquire(&t_cur->lock); // maybe necessary?
        list_del_reinit(&t_cur->threads);
        atomic_dec_return(&tg->thread_cnt);

        exit_wakeup(p, t_cur); // !!! bug
        free_thread(t_cur);
        release(&t_cur->lock);
    }
    release(&p->tg->lock);
}

// debug
void proc_thread_print(void) {
    printf("\n");
    printProcessTree(initproc, 0);
}

void print_clone_flags(int flags) {
    if (flags & CSIGNAL)
        printfRed("CSIGNAL is set.\n");
    if (flags & CLONE_VM)
        printfRed("CLONE_VM is set.\n");
    if (flags & CLONE_FS)
        printfRed("CLONE_FS is set.\n");
    if (flags & CLONE_FILES)
        printfRed("CLONE_FILES is set.\n");
    if (flags & CLONE_SIGHAND)
        printfRed("CLONE_SIGHAND is set.\n");
    if (flags & CLONE_PTRACE)
        printfRed("CLONE_PTRACE is set.\n");
    if (flags & CLONE_VFORK)
        printfRed("CLONE_VFORK is set.\n");
    if (flags & CLONE_PARENT)
        printfRed("CLONE_PARENT is set.\n");
    if (flags & CLONE_THREAD)
        printfRed("CLONE_THREAD is set.\n");
    if (flags & CLONE_NEWNS)
        printfRed("CLONE_NEWNS is set.\n");
    if (flags & CLONE_SYSVSEM)
        printfRed("CLONE_SYSVSEM is set.\n");
    if (flags & CLONE_SETTLS)
        printfRed("CLONE_SETTLS is set.\n");
    if (flags & CLONE_PARENT_SETTID)
        printfRed("CLONE_PARENT_SETTID is set.\n");
    if (flags & CLONE_CHILD_CLEARTID)
        printfRed("CLONE_CHILD_CLEARTID is set.\n");
    if (flags & CLONE_DETACHED)
        printfRed("CLONE_DETACHED is set.\n");
    if (flags & CLONE_UNTRACED)
        printfRed("CLONE_UNTRACED is set.\n");
    if (flags & CLONE_CHILD_SETTID)
        printfRed("CLONE_CHILD_SETTID is set.\n");
    if (flags & CLONE_STOPPED)
        printfRed("CLONE_STOPPED is set.\n");
    if (flags & CLONE_NEWUTS)
        printfRed("CLONE_NEWUTS is set.\n");
    if (flags & CLONE_NEWIPC)
        printfRed("CLONE_NEWIPC is set.\n");
}

// print children of proc p
void procChildrenChain(struct proc *p) {
    char tmp_str[1000];
    int len = 0;
    len += sprintf(tmp_str, "=======debug======\n");
    len += sprintf(tmp_str + len, "proc : %d\n", p->pid, p->name);
    struct proc *p_pos = NULL;
    struct proc *p_first = firstchild(p);
    if (p_first == NULL) {
        len += sprintf(tmp_str + len, "no children!!!\n");
    } else {
        len += sprintf(tmp_str + len, "%d", p_first->pid, p_first->name);
        list_for_each_entry(p_pos, &p_first->sibling_list, sibling_list) {
            len += sprintf(tmp_str + len, "->%d", p_pos->pid, p_pos->name);
        }
    }
    printf("%s\n", tmp_str);
}

uchar initcode[] = {
#include "initcode.h"
};

void uvminit(struct mm_struct *mm, uchar *src, uint sz) {
    char *mem;
    pagetable_t pagetable = mm->pagetable;
    // vmprint(pagetable, 1, 0, 0, 0, 0);

    if (sz > 4 * PGSIZE)
        panic("init: more than 4 pages");

    // program
    for (int i = 0; i < 4; i++) {
        mem = kzalloc(PGSIZE);
        mappages(pagetable, 0 + i * PGSIZE, PGSIZE, (uint64)mem, PTE_W | PTE_R | PTE_X | PTE_U, COMMONPAGE);
        memmove(mem, src + PGSIZE * i, (PGSIZE > (sz - i * PGSIZE) ? (sz - i * PGSIZE) : PGSIZE));
    }

    if (vma_map(mm, 0, 4 * PGSIZE, PERM_READ | PERM_WRITE, VMA_TEXT) < 0) {
        panic("uvminit: vma_map failed");
    }

    // print_vma(&mm->head_vma);
    // stack
    uvm_thread_stack(pagetable, 10);
    if (vma_map(mm, USTACK, 10 * PGSIZE, PERM_READ | PERM_WRITE, VMA_STACK) < 0) {
        panic("uvminit: vma_map failed");
    }

    // vmprint(pagetable, 1, 0, 0, 0, 0);
}

void oscomp_init(void) {
    struct proc *p;
    struct tcb *t;
    p = create_proc();
    ASSERT(p != NULL);
    t = p->tg->group_leader;
    ASSERT(t != NULL);
    initproc = p;

    // printf("sizeof code = %p\n", sizeof(initcode));
    uvminit(p->mm, initcode, sizeof(initcode));

    // prepare for the very first "return" from kernel to user.
    t->trapframe->epc = 0;
    t->trapframe->sp = USTACK + 5 * PGSIZE;

    safestrcpy(p->name, "/init", 10);
    // acquire(&t->lock);
    TCB_Q_changeState(t, TCB_RUNNABLE);
    // release(&t->lock);
    release(&p->lock);
    Info("========== init finished! ==========\n");
    return;
}