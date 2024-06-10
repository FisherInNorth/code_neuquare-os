#include "ipc/shm.h"
#include "ipc/options.h"
#include "lib/riscv.h"
#include "fs/vfs/fs.h"
#include "fs/vfs/ops.h"
#include "memory/mm.h"
#include "memory/allocator.h"
#include "errno.h"
#include "memory/vma.h"

int assist_openat(struct inode *ip, int flags, int omode, struct file **fp);

void shm_init_ns(struct ipc_namespace *ns) {
    ns->shm_ctlmax = SHMMAX;
    ns->shm_ctlall = SHMALL;
    ns->shm_ctlmni = SHMMNI;
    ns->shm_tot = 0;
    ipc_init_ids(&shm_ids(ns));
}

struct file *shmem_kernel_file_setup(const char *name, loff_t size) {
    struct inode *ip;
    struct inode *dp = fat32_sb.root; // use root as its parent?
    if ((ip = dp->i_op->icreate(dp, name, S_IFREG | S_IRWXUGO, 0, 0)) == 0) {
        // return NULL;
        panic("shmem_kernel_file_setup : create error\n");
    }
    ip->shm_flg = 1;
    ip->i_size = size;
    struct file *fp;
    assist_openat(ip, O_RDWR, 0, &fp);
    fp->is_shm_file = 1; // !!!
    return fp;
}

struct shmid_kernel *shm_lock_check(struct ipc_namespace *ns, int shmid) {
    struct kern_ipc_perm *ipcp = ipc_lock_check(&shm_ids(ns), shmid);

    // if (IS_ERR(ipcp))
    // 	return (struct shmid_kernel *)ipcp;

    return container_of(ipcp, struct shmid_kernel, shm_perm);
}

int security_shm_shmat(struct shmid_kernel *shp, char *shmaddr, int shmflg) {
    return 0;
}

int newseg(struct ipc_namespace *ns, struct ipc_params *params) {
    key_t key = params->key;
    int shmflg = params->flg;
    size_t size = params->u.size;
    int id;
    struct shmid_kernel *shp;

    // round up
    size_t numpages = (size + PGSIZE - 1) >> PGSHIFT;

    struct file *fp = NULL;
    char name[13];
    // vm_flags_t acctflag = 0;

    if (size < SHMMIN || size > ns->shm_ctlmax)
        return -EINVAL;

    if (numpages << PGSHIFT < size)
        return -ENOSPC;

    if (ns->shm_tot + numpages < ns->shm_tot || ns->shm_tot + numpages > ns->shm_ctlall)
        return -ENOSPC;

    // no memory
    shp = kzalloc(sizeof(struct shmid_kernel));
    if (unlikely(!shp))
        return -ENOMEM;

    shp->shm_perm.key = key;
    shp->shm_perm.mode = (shmflg & S_IRWXUGO);
    // shp->mlock_user = NULL;
    // 	shp->shm_perm.security = NULL;
    // error = security_shm_alloc(&shp->shm_perm);
    // if (error) {
    // 	kvfree(shp);
    // 	return error;
    // }

    snprintf(name, 13, "SYSV%08x", key);
    if (shmflg & SHM_HUGETLB) {
        panic("SHM_HUGETLB not tested\n");
        // struct hstate *hs;
        // size_t hugesize;

        // hs = hstate_sizelog((shmflg >> SHM_HUGE_SHIFT) & SHM_HUGE_MASK);
        // if (!hs) {
        // 	error = -EINVAL;
        // 	goto no_file;
        // }
        // hugesize = ALIGN(size, huge_page_size(hs));

        // /* hugetlb_file_setup applies strict accounting */
        // if (shmflg & SHM_NORESERVE)
        // 	acctflag = VM_NORESERVE;
        // file = hugetlb_file_setup(name, hugesize, acctflag,
        // 		  &shp->mlock_user, HUGETLB_SHMFS_INODE,
        // 		(shmflg >> SHM_HUGE_SHIFT) & SHM_HUGE_MASK);
    } else {
        // if  ((shmflg & SHM_NORESERVE) && sysctl_overcommit_memory != OVERCOMMIT_NEVER)
        // 	acctflag = VM_NORESERVE;
        if (shmflg & SHM_NORESERVE) {
            panic("not tested\n");
        }
        // fp = shmem_kernel_file_setup(name, size, acctflag);
        fp = shmem_kernel_file_setup(name, size);
    }
    // 	error = PTR_ERR(file);
    // 	if (IS_ERR(file))
    // 		goto no_file;

    // 	shp->shm_cprid = get_pid(task_tgid(current));
    // 	shp->shm_lprid = NULL;
    // 	shp->shm_atim = shp->shm_dtim = 0;
    // 	shp->shm_ctim = ktime_get_real_seconds();

    struct proc* p = proc_current();
    shp->shm_segsz = size;
    shp->shm_nattch = 0;
    shp->shm_file = fp;
    shp->shm_creator = p;

    // 	/* ipc_addid() locks shp upon success. */
    id = ipc_addid(&shm_ids(ns), &shp->shm_perm, ns->shm_ctlmni);
    if (id < 0)
        panic("no id");
    list_add(&shp->shm_clist, &p->sysvshm.shm_clist);

    // 	/*
    // 	 * shmid gets reported as "inode#" in /proc/pid/maps.
    // 	 * proc-ps tools use this. Changing this will break them.
    // 	 */

    fp->f_tp.f_inode->i_ino = shp->shm_perm.id;
    ns->shm_tot += numpages;
    id = shp->shm_perm.id;

    release(&shp->shm_perm.lock);
    // 	ipc_unlock_object(&shp->shm_perm);
    // 	rcu_read_unlock();
    return id;

    // no_id:
    // 	ipc_update_pid(&shp->shm_cprid, NULL);
    // 	ipc_update_pid(&shp->shm_lprid, NULL);
    // 	if (is_file_hugepages(file) && shp->mlock_user)
    // 		user_shm_unlock(size, shp->mlock_user);
    // 	fput(file);
    // 	ipc_rcu_putref(&shp->shm_perm, shm_rcu_free);
    // 	return error;
    // no_file:
    // 	call_rcu(&shp->shm_perm.rcu, shm_rcu_free);
    // 	return error;
}

int security_shm_associate(struct kern_ipc_perm *shp, int shmflg) {
    return 0;
}

int shm_more_checks(struct kern_ipc_perm *ipcp, struct ipc_params *params) {
    struct shmid_kernel *shp;

    shp = container_of(ipcp, struct shmid_kernel, shm_perm);
    if (shp->shm_segsz < params->u.size)
        return -EINVAL;

    return 0;
}

void shm_rmid(struct ipc_namespace *ns, struct shmid_kernel *s) {
    ipc_rmid(&shm_ids(ns), &s->shm_perm);
}

void security_shm_free(struct shmid_kernel *shp) {
    // struct kern_ipc_perm *isp = &shp->shm_perm;
    // isp->security = NULL;
    return;
}

void shm_destroy(struct ipc_namespace *ns, struct shmid_kernel *shp) {
    ns->shm_tot -= (shp->shm_segsz + PGSIZE - 1) >> PGSHIFT;
    shm_rmid(ns, shp);
    release(&shp->shm_perm.lock);
    // shm_unlock(shp);
    // if (!is_file_hugepages(shp->shm_file))
    // 	shmem_lock(shp->shm_file, 0, shp->mlock_user);
    // else if (shp->mlock_user)
    // 	user_shm_unlock(shp->shm_file->f_path.dentry->d_inode->i_size,
    // 					shp->mlock_user);
    // fput (shp->shm_file);

    generic_fileclose(shp->shm_file);
    // vmspace_unmap(proc_current()->mm, ((struct shm_file_data*)shp->shm_file->private_data)->user_addr, shp->shm_segsz);
    security_shm_free(shp);
    // ipc_rcu_putref(shp);
}

struct shmid_kernel *shm_lock(struct ipc_namespace *ns, int id) {
    struct kern_ipc_perm *ipcp = ipc_lock(&shm_ids(ns), id);

    // if (IS_ERR(ipcp))
    // 	return (struct shmid_kernel *)ipcp;

    return container_of(ipcp, struct shmid_kernel, shm_perm);
}