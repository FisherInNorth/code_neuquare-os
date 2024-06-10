#include "memory/allocator.h"
#include "atomic/spinlock.h"
#include "atomic/futex.h"
#include "lib/hash.h"
#include "debug.h"

// global hash table
struct hash_table pid_map = {.lock = INIT_SPINLOCK(pid_hash_table),
                             .type = PID_MAP,
                             .size = NPROC};
struct hash_table tid_map = {.lock = INIT_SPINLOCK(tid_hash_table),
                             .type = TID_MAP,
                             .size = NTCB};
struct hash_table futex_map = {.lock = INIT_SPINLOCK(futex_hash_table),
                               .type = FUTEX_MAP,
                               .size = FUTEX_NUM};
struct futex;

// find the table entry given the table，type and key
struct hash_entry *hash_get_entry(struct hash_table *table, void *key, int holding) {
    uint64 hash_val = 0;
    struct hash_entry *entry = NULL;

    switch (table->type) {
    case PID_MAP:
    case TID_MAP:
    case IPC_IDX_MAP:
        hash_val = *(int *)key % table->size;
        break;
        // hash_val = *(int *)key % table->size;
        // break;
    case FUTEX_MAP:
        hash_val = (uint64)key % table->size;
        break;
    case INODE_MAP:
        hash_val = hash_str((char *)key) % table->size;
        break;
    default:
        panic("hash_get_entry : this type is invalid\n");
    }
    entry = table->hash_head + hash_val;
    if (!holding)
        acquire(&table->lock);
    return entry;
}

// lookup the hash table
// release : release its lock?
struct hash_node *hash_lookup(struct hash_table *table, void *key, struct hash_entry **entry, int release, int holding) {
    struct hash_entry *_entry = hash_get_entry(table, key, holding);

    if (entry)
        *entry = _entry;
    struct hash_node *node_cur = NULL;
    struct hash_node *node_tmp = NULL;
    list_for_each_entry_safe(node_cur, node_tmp, &_entry->list, list) {
        if (hash_bool(node_cur, key, table->type)) {
            if (release)
                release(&table->lock);
            return node_cur;
        }
    }
    if (release)
        release(&table->lock);
    return NULL;
}

// insert the hash node into the table
void hash_insert(struct hash_table *table, void *key, void *value, int holding) {
    struct hash_entry *entry = NULL;
    struct hash_node *node = hash_lookup(table, key, &entry, 0, holding); // not release it

    struct hash_node *node_new;
    if (node == NULL) {
        node_new = (struct hash_node *)kmalloc(sizeof(struct hash_node));
        hash_assign(node_new, key, table->type);
        node_new->value = value;
        INIT_LIST_HEAD(&node_new->list);
        list_add_tail(&node_new->list, &(entry->list));
    } else {
        if (table->type == INODE_MAP || table->type == FUTEX_MAP) {
            kfree(node->value); // !!!
        }
        node->value = value;
    }
    release(&table->lock);
}

// delete the inode given the key
void hash_delete(struct hash_table *table, void *key, int holding, int release) {
    struct hash_node *node = hash_lookup(table, key, NULL, 0, holding); // not release it

    if (node != NULL) {
        list_del_reinit(&node->list);

        if (table->type == INODE_MAP || table->type == FUTEX_MAP) {
            // if(table->type == FUTEX_MAP) {
            //     struct futex* fp = (struct futex*)(node->value);
            //     release(&fp->lock);
            // }
            kfree(node->value); // !!!
            // printfGreen("hash_delete : node->value, mm ++: %d pages\n", get_free_mem() / 4096);
        }
        kfree(node);
        // printfGreen("hash_delete : node, mm ++: %d pages\n", get_free_mem() / 4096);
    } else {
        // printfRed("hash delete : this key doesn't existed\n");
    }
    if (release)
        release(&table->lock);
}

// destroy hash table
// free : free this table global ?
void hash_destroy(struct hash_table *table, int free) {
    // printfGreen("inode destory(after) : free RAM: %d\n", get_free_mem());
    acquire(&table->lock);
    struct hash_node *node_cur = NULL;
    struct hash_node *node_tmp = NULL;
    for (int i = 0; i < table->size; i++) {
        list_for_each_entry_safe(node_cur, node_tmp, &table->hash_head[i].list, list) {
            if (table->type == INODE_MAP || table->type == FUTEX_MAP)
                kfree(node_cur->value); // !!!
            kfree(node_cur);    
        }
    }
    release(&table->lock);

    if (free) {
        kfree(table);
    }
    // printfGreen("hash_destroy, mm ++: %d pages\n", get_free_mem() / 4096);
    // printfGreen("inode destory(after) : free RAM: %d\n", get_free_mem());
}

// hash value given a string
uint64 hash_str(char *name) {
    uint64 hash_val = 0;
    size_t len = strlen(name);
    for (size_t i = 0; i < len; i++) {
        hash_val = hash_val * 31 + (uint64)name[i];
    }
    return hash_val;
}

// choose the key of hash node given type of hash table
uint64 hash_val(struct hash_node *node, enum hash_type type) {
    uint64 hash_val = 0;
    switch (type) {
    case PID_MAP:
    case TID_MAP:
    case IPC_IDX_MAP:
        hash_val = node->key_id;
        break;
    case FUTEX_MAP:
        hash_val = (uint64)node->key_p;
    case INODE_MAP:
        hash_val = hash_str(node->key_name);
        break;
    default:
        panic("hash_val : this type is invalid\n");
    }
    return hash_val;
}

// judge the key of hash node given key and type of hash table
uint64 hash_bool(struct hash_node *node, void *key, enum hash_type type) {
    uint64 ret = 0;
    switch (type) {
    case PID_MAP:
    case TID_MAP:
    case IPC_IDX_MAP:
        ret = (node->key_id == *(int *)key);
        break;
    case FUTEX_MAP:
        ret = ((uint64)node->key_p == (uint64)key);
        break;
    case INODE_MAP:
        ret = (hash_str(node->key_name) == hash_str((char *)key));
        break;
    default:
        panic("hash_bool : this type is invalid\n");
    }
    return ret;
}

// assign the value of hash node given key and table type
void hash_assign(struct hash_node *node, void *key, enum hash_type type) {
    switch (type) {
    case PID_MAP:
    case TID_MAP:
    case IPC_IDX_MAP:
        node->key_id = *(int *)key;
        break;
    case FUTEX_MAP:
        node->key_p = key;
        break;
    case INODE_MAP:
        safestrcpy(node->key_name, (char *)key, strlen((char *)key));
        break;
    default:
        panic("hash_assign : this type is invalid\n");
    }
}

// init every hash table entry
void hash_table_entry_init(struct hash_table *table) {
    // printfBlue("inode init(before) : free RAM: %d\n", get_free_mem());
    table->hash_head = (struct hash_entry *)kzalloc(table->size * sizeof(struct hash_entry));
    // printfMAGENTA("hash_table_entry_init, mm-- : %d pages\n", get_free_mem() / 4096);
    // printfBlue("inode init(after) : free RAM: %d\n", get_free_mem());
    for (int i = 0; i < table->size; i++) {
        INIT_LIST_HEAD(&table->hash_head[i].list);
    }
}

#define MAP_SIZE(map) (sizeof(map) + sizeof(map.hash_head))
// init all global hash tables
void hash_tables_init() {
    hash_table_entry_init(&pid_map);
    hash_table_entry_init(&tid_map);
    hash_table_entry_init(&futex_map);
    Info("========= Information of global hash table ==========\n");
    Info("pid_map size : %d B\n", MAP_SIZE(pid_map));
    Info("tid_map size : %d B\n", MAP_SIZE(tid_map));
    Info("futex_map size : %d B\n", MAP_SIZE(futex_map));
    Info("hash table, size = %d\n", sizeof(struct hash_table));
    Info("hash node, size = %d\n", sizeof(struct hash_node));
    Info("global hash table init [ok]\n");

}