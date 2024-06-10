#include "ipc/options.h"
#include "atomic/ops.h"
#include "memory/allocator.h"
#include "param.h"
#include "fs/stat.h"

void ipc_init_ids(struct ipc_ids *ids) {
    ids->in_use = 0;
    ids->seq = 0;
    sema_init(&ids->rwsem, 1, "ipc_ids_rwsem");
    do { union { volatile typeof((0)) tmp; typeof(((&((ids)->next_ipc_id))->counter)) result; } u = {.tmp = ((0))}; (((&((ids)->next_ipc_id))->counter)) = u.result; } while (0);
    int seq_limit = INT_MAX / SEQ_MULTIPLIER;
    if (seq_limit > USHORT_MAX)
        ids->seq_max = USHORT_MAX;
    else
        ids->seq_max = seq_limit;
    ids->key_ht = NULL;
    // rhashtable_init(&ids->key_ht, &ipc_kht_params);
    // idr_init(&ids->ipcs_idr);
    // ids->max_idx = -1;
    // ids->last_idx = -1;
    // ids->next_id = -1;
}

int ipc_buildid(int id, int seq) {
    return SEQ_MULTIPLIER * seq + id;
}

int security_ipc_permission(struct kern_ipc_perm *ipcp, short flag) {
    return 0;
}

void ipc_hashtable_init(struct ipc_ids *ids) {
    ids->key_ht = (struct hash_table *)kmalloc(sizeof(struct hash_table));
    if (ids->key_ht == NULL) {
        panic("hash table init : no free space\n");
    }
    ids->key_ht->lock = INIT_SPINLOCK(inode_hash_table);
    ids->key_ht->type = IPC_IDX_MAP;
    ids->key_ht->size = NIPCIDX; // TODO
    hash_table_entry_init(ids->key_ht);
}

int ipc_hash_insert(struct ipc_ids *ids, int id, struct kern_ipc_perm *new) {
    // don't use fat32_inode_hash_lookup!!!
    if (hash_lookup(ids->key_ht, (void *)&id, NULL, 1, 0) != NULL) { // release it, not holding it
        return -1;                                                   //!!!
    }
    hash_insert(ids->key_ht, (void *)&id, (void *)new, 0);
    return 0;
}

struct kern_ipc_perm *ipc_hash_lookup(struct ipc_ids *ids, int id) {
    struct hash_node *node = hash_lookup(ids->key_ht, (void *)&id, NULL, 0, 0); //
    // not release it(must holding lock!!!!)
    // not holding it
    if (node != NULL) {
        // find it
        struct kern_ipc_perm *search = NULL;
        search = (struct kern_ipc_perm *)(node->value);
        release(&ids->key_ht->lock);
        return search;
    }
    release(&ids->key_ht->lock); // !!!
    return NULL;
}

// ipc add id
int ipc_addid(struct ipc_ids *ids, struct kern_ipc_perm *new, int size) {
    // uid_t euid;
    // gid_t egid;
    // int idx, err;
    int idx = 0;
    int err;

    if (size > IPCMNI)
        size = IPCMNI;

    if (ids->in_use >= size)
        return -ENOSPC;

    initlock(&new->lock, "ipc_perm_lock");
    new->deleted = 0;
    // rcu_read_lock();
    acquire(&new->lock);

    // err = idr_get_new(&ids->ipcs_idr, new, &id);
    // if (err) {
    // 	spin_unlock(&new->lock);
    // 	rcu_read_unlock();
    // 	return err;
    // }
    idx = alloc_ipc_id(ids);
    if (ids->key_ht == NULL) {
        ipc_hashtable_init(ids);
    }

    err = ipc_hash_insert(ids, idx, new);
    if (err) {
        release(&new->lock);
        return err;
    }
    ids->in_use++;

    // current_euid_egid(&euid, &egid);
    // new->cuid = new->uid = euid;
    // new->gid = new->cgid = egid;

    new->seq = ids->seq++;
    if (ids->seq > ids->seq_max)
        ids->seq = 0;
    new->id = ipc_buildid(idx, new->seq);
    return idx;
}

int ipcperms(struct kern_ipc_perm *ipcp, short flag) {
    /* flag will most probably be 0 or S_...UGO from <linux/stat.h> */
    // uid_t euid = current_euid();
    // int requested_mode, granted_mode;

    // audit_ipc_obj(ipcp);
    // requested_mode = (flag >> 6) | (flag >> 3) | flag;
    // granted_mode = ipcp->mode;

    // if (euid == ipcp->cuid || euid == ipcp->uid)
    // 	granted_mode >>= 6;
    // else if (in_group_p(ipcp->cgid) || in_group_p(ipcp->gid))
    // 	granted_mode >>= 3;
    /* is there some bit set in requested_mode but not in granted_mode? */
    // if ((requested_mode & ~granted_mode & 0007) &&
    //     !capable(CAP_IPC_OWNER))
    // 	return -1;

    return security_ipc_permission(ipcp, flag);
}

int ipc_check_perms(struct kern_ipc_perm *ipcp, struct ipc_ops *ops, struct ipc_params *params) {
    int err;

    if (ipcperms(ipcp, params->flg))
        err = -EACCES;
    else {
        err = ops->associate(ipcp, params->flg);
        if (!err)
            err = ipcp->id;
    }

    return err;
}

struct kern_ipc_perm *ipc_findkey(struct ipc_ids *ids, key_t key) {
    // struct kern_ipc_perm *ipc;
    // int next_id;
    // int total;

    // for (total = 0, next_id = 0; total < ids->in_use; next_id++) {
    // ipc = idr_find(&ids->ipcs_idr, next_id);

    // if (ipc == NULL)
    // 	continue;

    // if (ipc->key != key) {
    // 	total++;
    // 	continue;
    // }

    // ipc_lock_by_ptr(ipc);
    // return ipc;
    // }

    return NULL;
}

// get new ipc
int ipcget_new(struct ipc_namespace *ns, struct ipc_ids *ids, const struct ipc_ops *ops, struct ipc_params *params) {
    int err;
    sema_wait(&ids->rwsem);
    err = ops->getnew(ns, params);
    sema_signal(&ids->rwsem);
    return err;
}

// get public ipc
int ipcget_public(struct ipc_namespace *ns, struct ipc_ids *ids, const struct ipc_ops *ops, struct ipc_params *params) {
    panic("ipcget_public : not tested");
    // struct kern_ipc_perm *ipcp;
    // int flg = params->flg;
    // int err = 0;

    // /*
    //  * Take the lock as a writer since we are potentially going to add
    //  * a new entry + read locks are not "upgradable"
    //  */
    // down_write(&ids->rwsem);
    // sema_wait(&ids->rwsem);
    // ipcp = ipc_findkey(ids, params->key);
    // if (ipcp == NULL) {
    // 	/* key not used */
    // 	if (!(flg & IPC_CREAT))
    // err = -ENOENT;
    // 	else
    // 		err = ops->getnew(ns, params);
    // } else {
    // 	/* ipc object has been locked by ipc_findkey() */

    // 	if (flg & IPC_CREAT && flg & IPC_EXCL)
    // err = -EEXIST;
    // 	else {
    // 		err = 0;
    // 		if (ops->more_checks)
    // 			err = ops->more_checks(ipcp, params);
    // 		if (!err)
    // 			/*
    // 			 * ipc_check_perms returns the IPC id on
    // 			 * success
    // 			 */
    // 			err = ipc_check_perms(ns, ipcp, ops, params);
    // 	}
    // 	ipc_unlock(ipcp);
    // }
    // sema_signal(&ids->rwsem);
    // return err;
    return 0;
}

// get ipc
int ipcget(struct ipc_namespace *ns, struct ipc_ids *ids, const struct ipc_ops *ops, struct ipc_params *params) {
    if (params->key == IPC_PRIVATE)
        return ipcget_new(ns, ids, ops, params);
    else
        return ipcget_public(ns, ids, ops, params);
}

struct kern_ipc_perm *ipc_lock(struct ipc_ids *ids, int id) {
    struct kern_ipc_perm *out;
    int lid = ipcid_to_idx(id);

    if (ids->key_ht == NULL) {
        ipc_hashtable_init(ids);
    }
    // rcu_read_lock();
    out = ipc_hash_lookup(ids, lid);
    // out = idr_find(&ids->ipcs_idr, lid);

    if (out == NULL) {
        // rcu_read_unlock();
        // return ERR_PTR(-EINVAL);
        panic("ipc hash lookup error\n");
    }
    acquire(&out->lock);
    // spin_lock(&out->lock);

    /* ipc_rmid() may have already freed the ID while ipc_lock
     * was spinning: here verify that the structure is still valid
     */
    if (out->deleted) {
        release(&out->lock);
        panic("ipc lock : error, has deleted\n");
        // 	rcu_read_unlock();
        // 	return ERR_PTR(-EINVAL);
    }
    return out;
}

struct kern_ipc_perm *ipc_lock_check(struct ipc_ids *ids, int id) {
    struct kern_ipc_perm *out;

    out = ipc_lock(ids, id);

    // if (IS_ERR(out))
    // 	return out;

    if (ipc_checkid(out, id)) {
        // ipc_unlock(out);
        // return ERR_PTR(-EIDRM);
        panic("ipc_lock_check : error\n");
    }

    return out;
}

int ipc_checkid(struct kern_ipc_perm *ipcp, int id) {
    return ipcid_to_seqx(id) != ipcp->seq;
}

struct kern_ipc_perm *ipc_obtain_object_idr(struct ipc_ids *ids, int id) {
    // struct kern_ipc_perm *out;
    // int idx = ipcid_to_idx(id);

    // out = idr_find(&ids->ipcs_idr, idx);
    // if (!out)
    // 	return ERR_PTR(-EINVAL);

    // return out;
    return NULL;
}

struct kern_ipc_perm *ipc_obtain_object_check(struct ipc_ids *ids, int id) {
    // struct kern_ipc_perm *out = ipc_obtain_object_idr(ids, id);

    // if (IS_ERR(out))
    // 	goto out;

    // if (ipc_checkid(out, id))
    // return ERR_PTR(-EINVAL);
    // out:
    // 	return out;
    return NULL;
}

struct kern_ipc_perm *ipcctl_pre_down(struct ipc_ids *ids, int id, int cmd, struct kern_ipc_perm *perm, int extra_perm) {
    struct kern_ipc_perm *ipcp;

    // 	uid_t euid;
    // int err;

    sema_wait(&ids->rwsem);
    ipcp = ipc_lock_check(ids, id);
    // 	if (IS_ERR(ipcp)) {
    // 		err = PTR_ERR(ipcp);
    // 		goto out_up;
    // 	}

    // 	audit_ipc_obj(ipcp);
    if (cmd == IPC_SET) {
        panic("ipcctl_pre_down : IPC_SET not tested\n");
    }
    // 		audit_ipc_set_perm(extra_perm, perm->uid,
    // 					 perm->gid, perm->mode);

    // 	euid = current_euid();
    // 	if (euid == ipcp->cuid ||
    // 	    euid == ipcp->uid  || capable(CAP_SYS_ADMIN))
    // 		return ipcp;

    return ipcp;

    // err = -EPERM;
    // ipc_unlock(ipcp);
    // out_up:
    // 	up_write(&ids->rw_mutex);
    // 	return ERR_PTR(err);
}

void ipc_rmid(struct ipc_ids *ids, struct kern_ipc_perm *ipcp) {
    int lid = ipcid_to_idx(ipcp->id);

    hash_delete(ids->key_ht, (void *)&lid, 0, 1); // not holding lock, release
    // idr_remove(&ids->ipcs_idr, lid);

    ids->in_use--;

    ipcp->deleted = 1;

    return;
}

void ipc_update_perm(struct kern_ipc_perm *in, struct kern_ipc_perm *out) {
    out->uid = in->uid;
    out->gid = in->gid;
    out->mode = (out->mode & ~S_IRWXUGO)
                | (in->mode & S_IRWXUGO);
}