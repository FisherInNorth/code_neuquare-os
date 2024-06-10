#ifndef __SHM_H__
#define __SHM_H__
#include "common.h"
#include "lib/list.h"
#include "proc/pcb_life.h"
#include "ipc/options.h"
#include "lib/riscv.h"
#include "fs/vfs/fs.h"

/* super user shmctl commands */
#define SHM_LOCK 11
#define SHM_UNLOCK 12

// typedef long    __kernel_old_time_t;
// typedef int		__kernel_ipc_pid_t;

// semaphore
#define IPC_SEM_IDS 0
// message queue
#define IPC_MSG_IDS 1
// shared memory
#define IPC_SHM_IDS 2

#define SHMMIN 1                         /* min shared seg size (bytes) */
#define SHMMNI 4096                      /* max num of segs system wide */
#define SHMMAX (ULONG_MAX - (1UL << 24)) /* max shared seg size (bytes) */
#define SHMALL (ULONG_MAX - (1UL << 24)) /* max shm system wide (pages) */
#define SHMSEG SHMMNI                    /* max shared segs per process */

/* shm_mode upper byte flags */
#define SHM_DEST 01000       /* segment will be destroyed on last detach */
#define SHM_LOCKED 02000     /* segment will not be swapped */
#define SHM_HUGETLB 04000    /* segment will use huge TLB pages */
#define SHM_NORESERVE 010000 /* don't check for reservations */

/*
 * shmat() shmflg values
 */
#define SHM_RDONLY 010000 /* read-only access */
#define SHM_RND 020000    /* round attach address to SHMLBA boundary */
#define SHM_REMAP 040000  /* take-over region on attach */
#define SHM_EXEC 0100000  /* execution access */

/*
 * shmget() shmflg values.
 */
/* The bottom nine bits are the same as open(2) mode flags */
#define SHM_R 0400 /* or S_IRUGO */
#define SHM_W 0200 /* or S_IWUGO */
/* Bits 9 & 10 are IPC_CREAT and IPC_EXCL */
#define SHM_HUGETLB 04000    /* segment will use huge TLB pages */
#define SHM_NORESERVE 010000 /* don't check for reservations */

#define SHMLBA PGSIZE /* attach addr a multiple of this */

#define shm_unlock(shp) release(&(shp)->shm_perm.lock)

struct file;

struct shmid_kernel /* private to the kernel */
{
    struct kern_ipc_perm shm_perm;
    struct file *shm_file;
    uint64 shm_nattch;
    uint64 shm_segsz;
    // time64	shm_atim;
    // time64	shm_dtim;
    // time64	shm_ctim;
    // struct pid		*shm_cprid;
    // struct pid		*shm_lprid;
    // struct user_struct	*mlock_user;

    /* The proc created the shm object.  NULL if the proc is dead. */
    struct proc *shm_creator;
    struct list_head shm_clist; /* list by creator */
};

struct shm_file_data {
    int id;
    struct ipc_namespace *ns;
    struct file *file;
    uint64 user_addr;
    // const struct vm_operations_struct *vm_ops;
};

#define shm_file_data(file) (*((struct shm_file_data **)&(file)->private_data))

// init shared memory namespace
void shm_init_ns(struct ipc_namespace *ns);

struct file *shmem_kernel_file_setup(const char *name, loff_t size);

int newseg(struct ipc_namespace *ns, struct ipc_params *params);

int security_shm_associate(struct kern_ipc_perm *shp, int shmflg);

int shm_more_checks(struct kern_ipc_perm *ipcp, struct ipc_params *params);

struct shmid_kernel *shm_lock_check(struct ipc_namespace *ns, int shmid);

int security_shm_shmat(struct shmid_kernel *shp, char *shmaddr, int shmflg);

void shm_rmid(struct ipc_namespace *ns, struct shmid_kernel *s);

void shm_destroy(struct ipc_namespace *ns, struct shmid_kernel *shp);

void security_shm_free(struct shmid_kernel *shp);

struct shmid_kernel *shm_lock(struct ipc_namespace *ns, int id);

#endif