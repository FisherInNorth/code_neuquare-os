#ifndef __HASH_H__
#define __HASH_H__

#include "atomic/spinlock.h"
#include "lib/list.h"
#include "param.h"

struct hash_node {
    union {
        int key_id;
        void *key_p;
        // char key_name[NAME_LONG_MAX];
        char key_name[NAME_LONG_MAX / 4];
    };
    void *value; // value
    struct list_head list;
};

struct hash_entry {
    struct list_head list;
};

enum hash_type { PID_MAP,
                 TID_MAP,
                 IPC_IDX_MAP,
                 FUTEX_MAP,
                 INODE_MAP
};

struct hash_table {
    struct spinlock lock;
    enum hash_type type;
    uint64 size;                  // table size
    struct hash_entry *hash_head; // hash entry
};

struct hash_entry *hash_get_entry(struct hash_table *table, void *key, int holding);
struct hash_node *hash_lookup(struct hash_table *table, void *key, struct hash_entry **entry, int release, int holding);
void hash_insert(struct hash_table *table, void *key, void *value, int holding);
void hash_delete(struct hash_table *table, void *key, int holding, int release);
void hash_destroy(struct hash_table *table, int free);

uint64 hash_str(char *name);
uint64 hash_val(struct hash_node *node, enum hash_type type);
uint64 hash_bool(struct hash_node *node, void *key, enum hash_type type);
void hash_assign(struct hash_node *node, void *key, enum hash_type type);

void hash_table_entry_init(struct hash_table *table);
#endif
