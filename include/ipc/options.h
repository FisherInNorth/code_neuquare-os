#ifndef __IPC_OPTIONS_H__
#define __IPC_OPTIONS_H__
#include "common.h"
#include "lib/hash.h"
#include "atomic/semaphore.h"
#include "atomic/spinlock.h"
#include "atomic/ops.h"
#include "errno.h"

typedef int __kernel_key_t;
typedef unsigned int __kernel_uid_t;
typedef unsigned int __kernel_gid_t;
typedef unsigned int __kernel_mode_t;

#define IPC_PRIVATE ((__kernel_key_t)0)

#define IPC_64 0x0100

#define ipc_parse_version(cmd) IPC_64

#define IPCMNI_SHIFT 15
#define ipcmni_seq_shift() IPCMNI_SHIFT
#define IPCMNI_IDX_MASK ((1 << IPCMNI_SHIFT) - 1)
#define ipcid_to_idx(id) ((id)&IPCMNI_IDX_MASK)
#define ipcid_to_seqx(id) ((id) >> ipcmni_seq_shift())

// atomic instruction to allocate ipc_id
#define init_ipc_id(ipc_ids) (atomic_set(&((ipc_ids)->next_ipc_id), 0))
#define alloc_ipc_id(ipc_ids) (atomic_inc_return(&((ipc_ids)->next_ipc_id)))


// for seq_max
#define IPCMNI 32768
#define SEQ_MULTIPLIER (IPCMNI)
#define INT_MAX ((int)(~0U >> 1))
#define USHORT_MAX ((uint16)(~0U))

// ipc 权限
struct kern_ipc_perm {
    __kernel_key_t key;
    struct spinlock lock;
    int deleted;
    int id;
    __kernel_uid_t uid;
    __kernel_gid_t gid;
    // __kernel_uid_t	cuid;
    // __kernel_gid_t	cgid;
    __kernel_mode_t mode;
    uint16 seq;
};

// ipc 参数
struct ipc_params {
    key_t key;
    int flg;
    union {
        size_t size; /* for shared memories */
        int nsems;   /* for semaphores */
    } u;
};

// ipc 对象
struct ipc_ids {
    int in_use;
    uint16 seq;
    uint16 seq_max;

    struct semaphore rwsem;

    // simplify idr
    atomic_t next_ipc_id;
    // lookup hash table
    struct hash_table *key_ht;

    // struct idr ipcs_idr;
    // int max_idx;
    // int last_idx;	/* For wrap around detection */
    // int next_id;
};

// ipc namespace
struct ipc_namespace {
    // refcount_t	count;
    struct ipc_ids ids[3];

    // int		sem_ctls[4];
    // int		used_sems;

    // unsigned int	msg_ctlmax;
    // unsigned int	msg_ctlmnb;
    // unsigned int	msg_ctlmni;
    // atomic_t	msg_bytes;
    // atomic_t	msg_hdrs;

    size_t shm_ctlmax;
    size_t shm_ctlall;
    unsigned long shm_tot;
    int shm_ctlmni;
    // /*
    //  * Defines whether IPC_RMID is forced for _all_ shm segments regardless
    //  * of shmctl()
    //  */
    // int		shm_rmid_forced;

    // struct notifier_block ipcns_nb;

    // /* The kern_mount of the mqueuefs sb.  We take a ref on it */
    // struct vfsmount	*mq_mnt;

    // /* # queues in this ns, protected by mq_lock */
    // unsigned int    mq_queues_count;

    // /* next fields are set through sysctl */
    // unsigned int    mq_queues_max;   /* initialized to DFLT_QUEUESMAX */
    // unsigned int    mq_msg_max;      /* initialized to DFLT_MSGMAX */
    // unsigned int    mq_msgsize_max;  /* initialized to DFLT_MSGSIZEMAX */
    // unsigned int    mq_msg_default;
    // unsigned int    mq_msgsize_default;

    // /* user_ns which owns the ipc ns */
    // struct user_namespace *user_ns;
    // struct ucounts *ucounts;

    // struct llist_node mnt_llist;

    // struct ns_common ns;
};

#define shm_ids(ns) ((ns)->ids[IPC_SHM_IDS])

// ipc operations
struct ipc_ops {
    int (*getnew)(struct ipc_namespace *, struct ipc_params *);
    int (*associate)(struct kern_ipc_perm *, int);
    int (*more_checks)(struct kern_ipc_perm *, struct ipc_params *);
};

/* resource get request flags */
#define IPC_CREAT 00001000  /* create if key is nonexistent */
#define IPC_EXCL 00002000   /* fail if key exists */
#define IPC_NOWAIT 00004000 /* return error on wait */

/* ipcs ctl commands */
#define SHM_STAT 13
#define SHM_INFO 14
#define SHM_STAT_ANY 15

#define SEQ_MULTIPLIER (IPCMNI)

/* Override IPC ownership checks */
#define CAP_IPC_OWNER 15

// for shm cmd
#define IPC_RMID 0 /* remove resource */
#define IPC_SET 1  /* set ipc_perm options */
#define IPC_STAT 2 /* get ipc_perm options */
#define IPC_INFO 3 /* see ipcs */

void ipc_init_ids(struct ipc_ids *ids);

int ipc_buildid(int id, int seq);

int security_ipc_permission(struct kern_ipc_perm *ipcp, short flag);

int ipc_addid(struct ipc_ids *ids, struct kern_ipc_perm *new, int size);

int ipcperms(struct kern_ipc_perm *ipcp, short flag);

int ipc_check_perms(struct kern_ipc_perm *ipcp, struct ipc_ops *ops, struct ipc_params *params);

struct kern_ipc_perm *ipc_findkey(struct ipc_ids *ids, key_t key);

int ipcget_new(struct ipc_namespace *ns, struct ipc_ids *ids, const struct ipc_ops *ops, struct ipc_params *params);

int ipcget_public(struct ipc_namespace *ns, struct ipc_ids *ids, const struct ipc_ops *ops, struct ipc_params *params);

int ipcget(struct ipc_namespace *ns, struct ipc_ids *ids, const struct ipc_ops *ops, struct ipc_params *params);

int ipc_checkid(struct kern_ipc_perm *ipcp, int id);

struct kern_ipc_perm *ipc_obtain_object_idr(struct ipc_ids *ids, int id);

struct kern_ipc_perm *ipc_obtain_object_check(struct ipc_ids *ids, int id);

struct kern_ipc_perm *ipc_lock_check(struct ipc_ids *ids, int id);

void ipc_hashtable_init(struct ipc_ids *ids);

int ipc_hash_insert(struct ipc_ids *ids, int id, struct kern_ipc_perm *new);

struct kern_ipc_perm *ipc_hash_lookup(struct ipc_ids *ids, int id);

struct kern_ipc_perm *ipcctl_pre_down(struct ipc_ids *ids, int id, int cmd, struct kern_ipc_perm *perm, int extra_perm);

void ipc_rmid(struct ipc_ids *ids, struct kern_ipc_perm *ipcp);

void ipc_update_perm(struct kern_ipc_perm *in, struct kern_ipc_perm *out);

struct kern_ipc_perm *ipc_lock(struct ipc_ids *ids, int id);

#endif