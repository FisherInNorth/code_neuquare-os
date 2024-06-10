#ifndef __RADIX_TREE_H__
#define __RADIX_TREE_H__
#include "common.h"
#include "atomic/ops.h"
#include "lib/list.h"

#define RADIX_TREE_MAP_SHIFT 6
#define RADIX_TREE_MAP_SIZE (1UL << RADIX_TREE_MAP_SHIFT) // 1<<6 = 64
#define RADIX_TREE_MAX_TAGS 2
#define RADIX_TREE_TAG_LONGS ((RADIX_TREE_MAP_SIZE + BITS_PER_LONG - 1) / BITS_PER_LONG)
// (64+32-1)/32 = 95/32 = 2
#define RADIX_TREE_INDEX_BITS (8 /* CHAR_BIT */ * sizeof(uint64))
#define RADIX_TREE_MAX_PATH (DIV_ROUND_UP(RADIX_TREE_INDEX_BITS, RADIX_TREE_MAP_SHIFT))
// 64/6 上取整
// height   ||   max index
// 0             0
// 1             2^6-1
// 2             2^12-1
// 3             2^18-1
// ?             2^64-1
// ? = ⌈64 / 6⌉
#define RADIX_TREE_INDIRECT_PTR 1
// 1 : indirent node, 0 : data item
#define RADIX_TREE_MAP_MASK (RADIX_TREE_MAP_SIZE - 1) // 1<<6-1 = 64 -1

typedef unsigned int gfp_t;
#define __GFP_BITS_SHIFT 22 /* Room for 22 __GFP_FOO bits */
#define __GFP_BITS_MASK ((gfp_t)((1 << __GFP_BITS_SHIFT) - 1))

// tag is valid ???
#define tag_valid(tag) ((tag != -1))
#define tag_invalid (-1)
// grap all ???
#define maxitems_valid(maxitems) ((maxitems != UINT64_MAX))
#define maxitems_invald UINT64_MAX

#define GFP_FS ((gfp_t)0x80u) /* Can call down to low-level FS? */
#define RADIX_TREE_INIT(mask) \
    {                         \
        .height = 0,          \
        .gfp_mask = (mask),   \
        .rnode = NULL,        \
    }

#define RADIX_TREE(name, mask) \
    struct radix_tree_root name = RADIX_TREE_INIT(mask)

#define INIT_RADIX_TREE(root, mask) \
    do {                            \
        (root)->height = 0;         \
        (root)->gfp_mask = (mask);  \
        (root)->rnode = NULL;       \
    } while (0)

// Hint : in order to distinguish data item and radix_tree_node pointer
// * An indirect pointer (root->rnode pointing to a radix_tree_node, rather
// * than a data item) is signalled by the low bit set in the root->rnode pointer.
static inline int radix_tree_is_indirect_ptr(void *ptr) {
    return (int)((uint64)ptr & RADIX_TREE_INDIRECT_PTR);
}

static inline void *radix_tree_indirect_to_ptr(void *ptr) {
    return (void *)((uint64)ptr & ~RADIX_TREE_INDIRECT_PTR);
}

static inline void *radix_tree_ptr_to_indirect(void *ptr) {
    return (void *)((uint64)ptr | RADIX_TREE_INDIRECT_PTR);
}

// root of radix tree
struct radix_tree_root {
    uint32 height;                 // height of radix tree
    struct radix_tree_node *rnode; // root node pointer
    gfp_t gfp_mask;                // same as Linux
};

// node of radix tree
struct radix_tree_node {
    uint32 height;                                          // 叶子节点到树根的高度
    uint32 count;                                           // 当前节点的子节点个数，叶子节点的 count=0
    void *slots[RADIX_TREE_MAP_SIZE];                       // 每个slot对应一个子节点
    uint64 tags[RADIX_TREE_MAX_TAGS][RADIX_TREE_TAG_LONGS]; // 标签数组，用于存储各个元素的标记信息
};

// help for radix search
struct radix_tree_path {
    struct radix_tree_node *node; // 遍历路径上的当前节点
    int offset;                   // 当前节点在父节点中对应的槽位（slot）索引
};

// list or array insert?
typedef void (*page_op)(void *, void *, uint64, void *); // uint64 -> page index

// ops for tag
void *radix_tree_tag_set(struct radix_tree_root *root, uint64 index, uint32 tag);
void *radix_tree_tag_clear(struct radix_tree_root *root, uint64 index, uint32 tag);
int radix_tree_tag_get(struct radix_tree_root *root, uint64 index, uint32 tag);
// auxiliary functions
uint64 radix_tree_maxindex(uint height);
// allocate
struct radix_tree_node *radix_tree_node_alloc(struct radix_tree_root *root);
// search
void *radix_tree_lookup_node(struct radix_tree_root *root, uint64 index);
void **radix_tree_lookup_slot(struct radix_tree_root *root, uint64 index);
// insert
int radix_tree_insert(struct radix_tree_root *root, uint64 index, void *item);
// extend
int radix_tree_extend(struct radix_tree_root *root, uint64 index);
// shrink
void radix_tree_shrink(struct radix_tree_root *root);
// delete
void *radix_tree_delete(struct radix_tree_root *root, uint64 index);
// free
void radix_tree_node_free(struct radix_tree_node *node);

// lookup with batchsize(max_items)
// tag == -1 , grap item
// tag != -1 , grap item with tag
int radix_tree_lookup_batch_elements(struct radix_tree_node *slot, void *page_head, page_op function, uint64 index,
                                     uint64 max_items, uint64 *next_index, int tag);

// we merged radix_tree_gang_lookup、radix_tree_gang_lookup_slot、
// radix_tree_gang_lookup_tag and radix_tree_gang_lookup_tag_slot.
// tag == -1 , grap item
// tag != -1 , grap item with tag
int radix_tree_general_gang_lookup_elements(struct radix_tree_root *root, void *page_head, page_op function,
                                            uint64 first_index, uint64 max_items, int tag);
// free the whole tree
void radix_tree_free_whole_tree(struct radix_tree_node *node, uint32 max_height, uint32 height);

#endif