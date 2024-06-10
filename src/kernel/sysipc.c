#include "ipc/options.h"
#include "memory/vma.h"
#include "common.h"
#include "kernel/syscall.h"
#include "proc/pcb_life.h"
#include "ipc/shm.h"
#include "fs/fcntl.h"
#include "fs/stat.h"
#include "memory/allocator.h"
#include "fs/vfs/ops.h"

static void do_IPC_INFO() {
    panic("IPC_INFO : not tested\n");
    // struct shminfo64 shminfo;
    // err = security_shm_shmctl(NULL, cmd);
    // if (err)
    //     return err;

    // memset(&shminfo, 0, sizeof(shminfo));
    // shminfo.shmmni = shminfo.shmseg = ns->shm_ctlmni;
    // shminfo.shmmax = ns->shm_ctlmax;
    // shminfo.shmall = ns->shm_ctlall;

    // shminfo.shmmin = SHMMIN;
    // if(copy_shminfo_to_user (buf, &shminfo, version))
    //     return -EFAULT;

    // down_read(&shm_ids(ns).rw_mutex);
    // err = ipc_get_maxid(&shm_ids(ns));
    // up_read(&shm_ids(ns).rw_mutex);

    // if(err<0)
    //     err = 0;
    // goto out;
}

static void do_SHM_INFO() {
    panic("SHM_INFO : not tested\n");
    // struct shm_info shm_info;
    // err = security_shm_shmctl(NULL, cmd);
    // if (err)
    //     return err;

    // memset(&shm_info, 0, sizeof(shm_info));
    // down_read(&shm_ids(ns).rw_mutex);
    // shm_info.used_ids = shm_ids(ns).in_use;
    // shm_get_stat (ns, &shm_info.shm_rss, &shm_info.shm_swp);
    // shm_info.shm_tot = ns->shm_tot;
    // shm_info.swap_attempts = 0;
    // shm_info.swap_successes = 0;
    // err = ipc_get_maxid(&shm_ids(ns));
    // up_read(&shm_ids(ns).rw_mutex);
    // if (copy_to_user(buf, &shm_info, sizeof(shm_info))) {
    //     err = -EFAULT;
    //     goto out;
    // }

    // err = err < 0 ? 0 : err;
    // goto out;
}

static void do_STAT() {
    panic("STAT : not tested\n");
    // struct shmid64_ds tbuf;
    // int result;

    // if (cmd == SHM_STAT) {
    //     shp = shm_lock(ns, shmid);
    //     if (IS_ERR(shp)) {
    //         err = PTR_ERR(shp);
    //         goto out;
    //     }
    //     result = shp->shm_perm.id;
    // } else {
    //     shp = shm_lock_check(ns, shmid);
    //     if (IS_ERR(shp)) {
    //         err = PTR_ERR(shp);
    //         goto out;
    //     }
    //     result = 0;
    // }
    // err = -EACCES;
    // if (ipcperms (&shp->shm_perm, S_IRUGO))
    //     goto out_unlock;
    // err = security_shm_shmctl(shp, cmd);
    // if (err)
    //     goto out_unlock;
    // memset(&tbuf, 0, sizeof(tbuf));
    // kernel_to_ipc64_perm(&shp->shm_perm, &tbuf.shm_perm);
    // tbuf.shm_segsz	= shp->shm_segsz;
    // tbuf.shm_atime	= shp->shm_atim;
    // tbuf.shm_dtime	= shp->shm_dtim;
    // tbuf.shm_ctime	= shp->shm_ctim;
    // tbuf.shm_cpid	= shp->shm_cprid;
    // tbuf.shm_lpid	= shp->shm_lprid;
    // tbuf.shm_nattch	= shp->shm_nattch;
    // shm_unlock(shp);
    // if(copy_shmid_to_user (buf, &tbuf, version))
    //     err = -EFAULT;
    // else
    //     err = result;
    // goto out;
}

static void do_shm_rmid(struct ipc_namespace *ns, struct kern_ipc_perm *ipcp) {
    struct shmid_kernel *shp;
    shp = container_of(ipcp, struct shmid_kernel, shm_perm);

    if (shp->shm_nattch) {
        shp->shm_perm.mode |= SHM_DEST;
        /* Do not find it any more */
        shp->shm_perm.key = IPC_PRIVATE;
        release(&shp->shm_perm.lock);
        // shm_unlock(shp);
    } else {
        shm_destroy(ns, shp);
    }
}

long do_shmat(int shmid, char *shmaddr, int shmflg, uint64 *raddr) {
    struct shmid_kernel *shp;
    uint64 addr;
    uint64 size;
    struct file *file;
    int err;
    uint64 flags;
    uint64 prot;
    int acc_mode;
    uint64 user_addr;
    struct ipc_namespace *ns;
    struct shm_file_data *sfd;
    // 	struct path path;
    fmode_t f_mode;

    err = -EINVAL;
    if (shmid < 0)
        goto out;
    else if ((addr = (uint64)shmaddr)) {
        if (addr & (SHMLBA - 1)) {
            if (shmflg & SHM_RND)
                addr &= ~(SHMLBA - 1); /* round down */
            else {
                panic("do_shmat : not tested\n");
            }
            // #ifndef __ARCH_FORCE_SHMLBA
            // 				if (addr & ~PAGE_MASK)
            // #endif
            // 					goto out;
        }
        flags = MAP_SHARED | MAP_FIXED;
    } else {
        if ((shmflg & SHM_REMAP))
            goto out;
        flags = MAP_SHARED;
    }

    if (shmflg & SHM_RDONLY) {
        prot = PROT_READ;
        acc_mode = S_IRUGO;
        f_mode = FMODE_READ;
    } else {
        prot = PROT_READ | PROT_WRITE;
        acc_mode = S_IRUGO | S_IWUGO;
        f_mode = FMODE_READ | FMODE_WRITE;
    }
    if (shmflg & SHM_EXEC) {
        prot |= PROT_EXEC;
        acc_mode |= S_IXUGO;
    }

    // 	/*
    // 	 * We cannot rely on the fs check since SYSV IPC does have an
    // 	 * additional creator id...
    // 	 */
    ns = proc_current()->ipc_ns;

    shp = shm_lock_check(ns, shmid);
    // 	if (IS_ERR(shp)) {
    // 		err = PTR_ERR(shp);
    // 		goto out;
    // 	}

    // not important???
    // err = -EACCES;
    // if (ipcperms(&shp->shm_perm, acc_mode))
    // 	goto out_unlock;

    // err = security_shm_shmat(shp, shmaddr, shmflg);
    // if (err)
    // 	goto out_unlock;

    // 	path = shp->shm_file->f_path;
    // 	path_get(&path);
    shp->shm_nattch++;
    size = shp->shm_file->f_tp.f_inode->i_size;

    // 	size = i_size_read(path.dentry->d_inode);
    shm_unlock(shp);

    err = -ENOMEM;
    if ((sfd = (struct shm_file_data *)kzalloc(sizeof(struct shm_file_data))) == 0) {
        panic("no free memory\n");
    }

    file = shp->shm_file;
    if (!file) {
        panic("occur error\n");
    }
    file->f_mode |= f_mode;

    // 	if (!sfd)
    // 		goto out_put_dentry;

    // 	file = alloc_file(&path, f_mode,
    // 			  is_file_hugepages(shp->shm_file) ?
    // 				&shm_file_operations_huge :
    // 				&shm_file_operations);
    // 	if (!file)
    // 		goto out_free;

    file->private_data = sfd;
    // file->f_mapping = shp->shm_file->f_mapping;
    sfd->id = shp->shm_perm.id;
    // sfd->ns = get_ipc_ns(ns);
    sfd->ns = ns;
    sfd->file = shp->shm_file;
    // 	sfd->vm_ops = NULL;

    struct proc *p = proc_current();
    sema_wait(&p->mm->mmap_sem);

    if (addr && !(shmflg & SHM_REMAP)) {
        panic("do_shmat : not tested\n");
        // err = -EINVAL;
        // if()
        // 		if (find_vma_intersection(current->mm, addr, addr + size))
        // 			goto invalid;
        // 		/*
        // 		 * If shm segment goes below stack, make sure there is some
        // 		 * space left for the stack to grow (at least 4 pages).
        // 		 */
        // 		if (addr < current->mm->start_stack &&
        // 		    addr > current->mm->start_stack - size - PAGE_SIZE * 5)
        // 			goto invalid;
    }

    user_addr = (uint64)do_mmap(addr, size, prot, flags, file, 0);
    if (user_addr == -1) {
        panic("error\n");
    }
    sfd->user_addr = user_addr;
    *raddr = user_addr;
    err = 0;
    // 	if (IS_ERR_VALUE(user_addr))
    // 		err = (long)user_addr;
    // invalid:
    sema_signal(&p->mm->mmap_sem);
    // generic_fileclose(file);
    // up_write(&current->mm->mmap_sem);

    // 	fput(file);

    // out_nattch:
    sema_wait(&shm_ids(ns).rwsem);

    // 	down_write(&shm_ids(ns).rw_mutex);
    shp = shm_lock(ns, shmid);
    // 	BUG_ON(IS_ERR(shp));
    shp->shm_nattch--;

    if (shp->shm_nattch == 0 && (shp->shm_perm.mode & SHM_DEST))
        shm_destroy(ns, shp);
    else
        shm_unlock(shp);
    // 	up_write(&shm_ids(ns).rw_mutex);
    sema_signal(&shm_ids(ns).rwsem);
out:
    return err;

    // out_unlock:
    // 	shm_unlock(shp);
    // 	goto out;

    // out_free:
    // 	kfree(sfd);
    // out_put_dentry:
    // 	path_put(&path);
    // 	goto out_nattch;
}

static int shmctl_down(struct ipc_namespace *ns, int shmid, int cmd, struct shmid_kernel *buf) {
    struct kern_ipc_perm *ipcp;
    struct shmid_kernel shmid_kern;
    // struct shmid_kernel *shp;
    int err;

    ipcp = ipcctl_pre_down(&shm_ids(ns), shmid, cmd, &shmid_kern.shm_perm, 0);

    // 	if (IS_ERR(ipcp))
    // 		return PTR_ERR(ipcp);

    // shp = container_of(ipcp, struct shmid_kernel, shm_perm);

    // err = security_shm_shmctl(shp, cmd);
    // 	if (err)
    // 		goto out_unlock;
    switch (cmd) {
    case IPC_RMID:
        do_shm_rmid(ns, ipcp);
        goto out_up;
    case IPC_SET:
        ipc_update_perm(&shmid_kern.shm_perm, ipcp);
        // shp->shm_ctim = get_seconds();
        break;
    default:
        err = -EINVAL;
    }
// out_unlock:
// 	shm_unlock(shp);
out_up:
    sema_signal(&shm_ids(ns).rwsem);
    // up_write(&shm_ids(ns).rw_mutex);
    return err;
}

// int shmget(key_t key, size_t size, int shmflg);
// allocates a System V shared memory segment
uint64 sys_shmget(void) {
    key_t key;
    size_t size;
    int shmflg;
    argint(0, &key);
    argulong(1, &size);
    argint(2, &shmflg);

    struct ipc_namespace *ns;
    static const struct ipc_ops shm_ops = {
        .getnew = newseg,
        .associate = security_shm_associate,
        .more_checks = shm_more_checks,
    };
    struct ipc_params shm_params;

    // extern int print_tf_flag;
    // print_tf_flag = 1;

    ns = proc_current()->ipc_ns;

    shm_params.key = key;
    shm_params.flg = shmflg;
    shm_params.u.size = size;

    return ipcget(ns, &shm_ids(ns), &shm_ops, &shm_params);
}

// int shmctl(int shmid, int cmd, struct shmid_ds *buf);
// System V shared memory control
// IPC_STAT
// IPC_SET
// IPC_RMID
uint64 sys_shmctl(void) {
    int shmid;
    int cmd;
    uint64 buf_addr;
    argint(0, &shmid);
    argint(1, &cmd);
    argaddr(2, &buf_addr);

    struct shmid_kernel buf;

    // struct shmid_kernel *shp;
    // int err, version;
    int err;
    struct ipc_namespace *ns;

    if (cmd < 0 || shmid < 0) {
        err = -EINVAL;
        goto out;
    }

    // version = ipc_parse_version(&cmd);
    struct proc *p = proc_current();
    ns = p->ipc_ns;

    switch (cmd) { /* replace with proc interface ? */
    case IPC_INFO: {
        do_IPC_INFO();
    }
    case SHM_INFO: {
        do_SHM_INFO();
    }
    case SHM_STAT:
    case IPC_STAT: {
        do_STAT();
    }
    case SHM_LOCK:
    case SHM_UNLOCK: {
        panic("shm_lock and shm_unlock not tested\n");
        // struct file *uninitialized_var(shm_file);

        // lru_add_drain_all();  /* drain pagevecs to lru lists */

        // shp = shm_lock_check(ns, shmid);
        // if (IS_ERR(shp)) {
        //     err = PTR_ERR(shp);
        //     goto out;
        // }

        // audit_ipc_obj(&(shp->shm_perm));

        // if (!capable(CAP_IPC_LOCK)) {
        //     uid_t euid = current_euid();
        //     err = -EPERM;
        //     if (euid != shp->shm_perm.uid &&
        //         euid != shp->shm_perm.cuid)
        //         goto out_unlock;
        //     if (cmd == SHM_LOCK && !rlimit(RLIMIT_MEMLOCK))
        //         goto out_unlock;
        // }

        // err = security_shm_shmctl(shp, cmd);
        // if (err)
        //     goto out_unlock;

        // if(cmd==SHM_LOCK) {
        //     struct user_struct *user = current_user();
        //     if (!is_file_hugepages(shp->shm_file)) {
        //         err = shmem_lock(shp->shm_file, 1, user);
        //         if (!err && !(shp->shm_perm.mode & SHM_LOCKED)){
        //             shp->shm_perm.mode |= SHM_LOCKED;
        //             shp->mlock_user = user;
        //         }
        //     }
        // } else if (!is_file_hugepages(shp->shm_file)) {
        //     shmem_lock(shp->shm_file, 0, shp->mlock_user);
        //     shp->shm_perm.mode &= ~SHM_LOCKED;
        //     shp->mlock_user = NULL;
        // }
        // shm_unlock(shp);
        // goto out;
    }
    case IPC_RMID:
    case IPC_SET:
        // panic("ipc rmid and set not tested\n");
        // err = shmctl_down(ns, shmid, cmd, buf, version);
        if (cmd == IPC_SET) {
            if (copyin(p->mm->pagetable, (char *)&buf, buf_addr, sizeof(buf)) < 0) {
                return -EFAULT;
            }
        }
        err = shmctl_down(ns, shmid, cmd, &buf);

        return err;
    default:
        return -EINVAL;
    }

// out_unlock:
//     shm_unlock(shp);
out:
    return err;
}

// void *shmat(int shmid, const void *shmaddr, int shmflg);
// System V shared memory operations
uint64 sys_shmat(void) {
    int shmid;
    uint64 shmaddr;
    int shmflg;
    argint(0, &shmid);
    argaddr(1, &shmaddr);
    argint(2, &shmflg);

    uint64 ret;
    long err = do_shmat(shmid, (char *)shmaddr, shmflg, &ret);
    if (err)
        return err;
    return (long)ret;
}