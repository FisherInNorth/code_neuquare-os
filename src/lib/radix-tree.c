#include "lib/radix-tree.h"
#include "memory/allocator.h"
#include "memory/buddy.h"
#include "atomic/ops.h"
#include "debug.h"

// ===================ops for tag====================
static inline void tag_set(struct radix_tree_node *node, uint32 tag,
                           int offset) {
    set_bit(offset, node->tags[tag]);
}

static inline void tag_clear(struct radix_tree_node *node, uint32 tag,
                             int offset) {
    clear_bit(offset, node->tags[tag]);
}

static inline int tag_get(struct radix_tree_node *node, uint32 tag,
                          int offset) {
    return test_bit(offset, node->tags[tag]);
}

static inline void root_tag_set(struct radix_tree_root *root, uint32 tag) {
    root->gfp_mask |= (gfp_t)(1 << (tag + __GFP_BITS_SHIFT));
}

static inline int root_tag_get(struct radix_tree_root *root, uint32 tag) {
    return (unsigned)root->gfp_mask & (1 << (tag + __GFP_BITS_SHIFT));
}

static inline void root_tag_clear(struct radix_tree_root *root, uint32 tag) {
    root->gfp_mask &= (gfp_t) ~(1 << (tag + __GFP_BITS_SHIFT));
}

static inline void root_tag_clear_all(struct radix_tree_root *root) {
    root->gfp_mask &= __GFP_BITS_MASK;
}

/*
 * Returns 1 if any slot in the node has this tag set.
 * Otherwise returns 0.
 */
static inline int any_tag_set(struct radix_tree_node *node, uint32 tag) {
    for (int idx = 0; idx < RADIX_TREE_TAG_LONGS; idx++) {
        if (node->tags[tag][idx])
            return 1;
    }
    return 0;
}

// ===================auxiliary functions====================
uint64 radix_tree_maxindex(uint height) {
    if (height > RADIX_TREE_MAX_PATH) {
        panic("radix_tree : error\n");
    }
    return (1 << (height * RADIX_TREE_MAP_SHIFT)) - 1; // at easy way to get maxindex
}

/*
 *	Set the search tag (which must be < RADIX_TREE_MAX_TAGS)
 *	corresponding to @index in the radix tree.  From
 *	the root all the way down to the leaf node.*/
void *radix_tree_tag_set(struct radix_tree_root *root, uint64 index, uint32 tag) {
    uint32 height, shift;
    struct radix_tree_node *slot;

    height = root->height;
    ASSERT(index <= radix_tree_maxindex(height));

    slot = radix_tree_indirect_to_ptr(root->rnode);
    shift = (height - 1) * RADIX_TREE_MAP_SHIFT;

    while (height > 0) {
        int offset;

        offset = (index >> shift) & RADIX_TREE_MAP_MASK;
        if (!tag_get(slot, tag, offset))
            tag_set(slot, tag, offset);
        slot = slot->slots[offset];
        ASSERT(slot != NULL);
        shift -= RADIX_TREE_MAP_SHIFT;
        height--;
    }

    /* set the root's tag bit */
    if (slot && !root_tag_get(root, tag))
        root_tag_set(root, tag);

    return slot;
}
/*
 *	Clear the search tag (which must be < RADIX_TREE_MAX_TAGS)
 *	corresponding to @index in the radix tree.  If
 *	this causes the leaf node to have no tags set then clear the tag in the
 *	next-to-leaf node, etc.*/
void *radix_tree_tag_clear(struct radix_tree_root *root, uint64 index, uint32 tag) {
    struct radix_tree_path path[RADIX_TREE_MAX_PATH + 1], *pathp = path;
    struct radix_tree_node *slot = NULL;
    uint32 height, shift;

    height = root->height;
    /*index is too large*/
    if (index > radix_tree_maxindex(height))
        goto out;

    shift = (height - 1) * RADIX_TREE_MAP_SHIFT;
    pathp->node = NULL;
    slot = radix_tree_indirect_to_ptr(root->rnode);

    while (height > 0) {
        int offset;

        if (slot == NULL) // no path
            goto out;

        offset = (index >> shift) & RADIX_TREE_MAP_MASK;
        pathp[1].offset = offset;
        pathp[1].node = slot;
        slot = slot->slots[offset];
        pathp++;
        shift -= RADIX_TREE_MAP_SHIFT;
        height--;
    }

    if (slot == NULL)
        goto out;

    while (pathp->node) {
        if (!tag_get(pathp->node, tag, pathp->offset))
            goto out;
        tag_clear(pathp->node, tag, pathp->offset);
        if (any_tag_set(pathp->node, tag))
            goto out;
        pathp--;
    }

    /* clear the root's tag bit */
    if (root_tag_get(root, tag))
        root_tag_clear(root, tag);

out:
    return slot;
}

//  * Return values:
//  *  0: tag not present or not set
//  *  1: tag set
int radix_tree_tag_get(struct radix_tree_root *root, uint64 index, uint32 tag) {
    uint32 height, shift;
    struct radix_tree_node *node;
    // int saw_unset_tag = 0;

    /* check the root's tag bit */
    if (!root_tag_get(root, tag))
        return 0;

    node = root->rnode;
    if (node == NULL)
        return 0;

    if (!radix_tree_is_indirect_ptr(node))
        return (index == 0);
    node = radix_tree_indirect_to_ptr(node);

    height = node->height;
    if (index > radix_tree_maxindex(height))
        return 0;

    shift = (height - 1) * RADIX_TREE_MAP_SHIFT;

    while (1) {
        int offset;

        if (node == NULL)
            return 0;

        offset = (index >> shift) & RADIX_TREE_MAP_MASK;

        /*
         * This is just a debug check.  Later, we can bale as soon as
         * we see an unset tag.
         */
        if (!tag_get(node, tag, offset)) {
            // saw_unset_tag = 1;
        }
        if (height == 1)
            return !!tag_get(node, tag, offset);
        node = node->slots[offset];
        shift -= RADIX_TREE_MAP_SHIFT;
        height--;
    }
}

// ===================allocate====================
struct radix_tree_node *radix_tree_node_alloc(struct radix_tree_root *root) {
    struct radix_tree_node *ret = NULL;
    ret = (struct radix_tree_node *)kzalloc(sizeof(struct radix_tree_node)); // kzalloc : with zero function
    // printfGreen("radix_tree_node_alloc, mm: %d pages\n", get_free_mem()/4096);
    if (ret == NULL) {
        panic("radix_tree_node_init : no memory\n");
    }
    ret->count = 0;
    for (int i = 0; i < RADIX_TREE_MAP_SIZE; i++)
        ret->slots[i] = NULL;
    return ret;
}

// ===================lookup====================
/*
 * is_slot == 1 : search for the slot.
 * is_slot == 0 : search for the node.
 */
static void *radix_tree_lookup_element(struct radix_tree_root *root,
                                       uint64 index, int is_slot) {
    uint32 height, shift;
    struct radix_tree_node *node, **slot;

    node = root->rnode;
    if (!radix_tree_is_indirect_ptr(node)) {
        // data item
        if (index > 0)
            return NULL;
        return is_slot ? (void *)&root->rnode : node;
    }

    node = radix_tree_indirect_to_ptr(node);

    height = node->height;
    if (index > radix_tree_maxindex(height))
        return NULL;

    shift = (height - 1) * RADIX_TREE_MAP_SHIFT;
    // height : 1 , shift : 0
    // height : 2 , shift : 6

    // similar to three level page table
    do {
        slot = (struct radix_tree_node **)(node->slots + ((index >> shift) & RADIX_TREE_MAP_MASK)); // offset mask
        node = *slot;
        if (node == NULL)
            return NULL;
        shift -= RADIX_TREE_MAP_SHIFT;
        height--;
    } while (height > 0);

    return is_slot ? (void *)slot : node;
}

// Lookup the item at the position @index in the radix tree @root
void *radix_tree_lookup_node(struct radix_tree_root *root, uint64 index) {
    return radix_tree_lookup_element(root, index, 0);
}

// Lookup the slot corresponding to the position @index in the radix tree @root.
void **radix_tree_lookup_slot(struct radix_tree_root *root, uint64 index) {
    return (void **)radix_tree_lookup_element(root, index, 1);
}

// ===================insert====================
// Insert an item into the radix tree at position @index.
int radix_tree_insert(struct radix_tree_root *root, uint64 index, void *item) {
    struct radix_tree_node *node = NULL, *slot;
    uint32 height, shift;
    int offset;
    int error;

    // it is a data item
    ASSERT(!radix_tree_is_indirect_ptr(item));

    /* Make sure the tree is high enough.  */
    if (index > radix_tree_maxindex(root->height)) {
        error = radix_tree_extend(root, index);
        if (error)
            return error;
        // if it is not high enough, we should expand it
    }

    slot = radix_tree_indirect_to_ptr(root->rnode);
    height = root->height;
    shift = (height - 1) * RADIX_TREE_MAP_SHIFT;

    offset = 0;
    while (height > 0) {
        if (slot == NULL) {
            /* Have to add a child node.  */
            if ((slot = radix_tree_node_alloc(root)) == NULL)
                return -1;
            slot->height = height;
            if (node) {
                node->slots[offset] = slot;
                node->count++;
                // add a slot
            } else {
                root->rnode = radix_tree_ptr_to_indirect(slot);
                // the initial value of node is NULL
            }
        }

        /* Go a level down */
        offset = (index >> shift) & RADIX_TREE_MAP_MASK;
        node = slot;
        slot = node->slots[offset];
        shift -= RADIX_TREE_MAP_SHIFT;
        height--;
    }

    if (slot != NULL)
        return -1;

    if (node) {
        node->count++;
        node->slots[offset] = item;
        // find the leaf, so count++ and insert item into slots
        ASSERT(!tag_get(node, 0, offset));
        ASSERT(!tag_get(node, 1, offset));
    } else {
        root->rnode = item;
        ASSERT(!root_tag_get(root, 0));
        ASSERT(!root_tag_get(root, 1));
    }

    return 0;
}

// ===================extend====================
// Extend a radix tree so it can store key @index.
int radix_tree_extend(struct radix_tree_root *root, uint64 index) {
    struct radix_tree_node *node;
    uint32 height;
    int tag;

    /* Figure out what the height should be.  */
    height = root->height + 1;
    while (index > radix_tree_maxindex(height))
        height++;

    if (root->rnode == NULL) {
        root->height = height;
        return 0;
    }

    do {
        uint32 newheight;
        if (!(node = radix_tree_node_alloc(root)))
            return -1;

        /* Increase the height.  */
        node->slots[0] = radix_tree_indirect_to_ptr(root->rnode);

        /* Propagate the aggregated tag info into the new root */
        for (tag = 0; tag < RADIX_TREE_MAX_TAGS; tag++) {
            if (root_tag_get(root, tag))
                tag_set(node, tag, 0);
        }

        newheight = root->height + 1;
        node->height = newheight;
        node->count = 1;
        node = radix_tree_ptr_to_indirect(node);
        root->rnode = node;
        root->height = newheight;
    } while (height > root->height);
    return 0;
}

// ===================shrink====================
/**
 *	radix_tree_shrink - shrink height of a radix tree to minimal
 *	@root	radix tree root
 */
void radix_tree_shrink(struct radix_tree_root *root) {
    /* try to shrink tree height */
    while (root->height > 0) {
        struct radix_tree_node *to_free = root->rnode;
        void *newptr;

        ASSERT(radix_tree_is_indirect_ptr(to_free));
        to_free = radix_tree_indirect_to_ptr(to_free);

        /*
         * The candidate node has more than one child, or its child
         * is not at the leftmost slot, we cannot shrink.
         */
        if (to_free->count != 1)
            break;
        if (!to_free->slots[0])
            break;

        newptr = to_free->slots[0];
        if (root->height > 1)
            newptr = radix_tree_ptr_to_indirect(newptr);
        root->rnode = newptr;
        root->height--;
        radix_tree_node_free(to_free);
    }
}

// ===================delete====================
// * Remove the item at @index from the radix tree rooted at @root.
// * Returns the address of the deleted item, or NULL if it was not present.
void *radix_tree_delete(struct radix_tree_root *root, uint64 index) {
    /*
     * The radix tree path needs to be one longer than the maximum path
     * since the "list" is null terminated.
     */
    struct radix_tree_path path[RADIX_TREE_MAX_PATH + 1], *pathp = path;
    struct radix_tree_node *slot = NULL;
    struct radix_tree_node *to_free;
    uint32 height, shift;
    int tag;
    int offset;

    height = root->height;
    if (index > radix_tree_maxindex(height))
        goto out;

    slot = root->rnode;
    if (height == 0) {
        root_tag_clear_all(root);
        root->rnode = NULL;
        goto out;
    }
    slot = radix_tree_indirect_to_ptr(slot);

    shift = (height - 1) * RADIX_TREE_MAP_SHIFT;
    pathp->node = NULL;

    do {
        if (slot == NULL)
            goto out;

        pathp++;
        offset = (index >> shift) & RADIX_TREE_MAP_MASK;
        pathp->offset = offset;
        pathp->node = slot;
        slot = slot->slots[offset];
        shift -= RADIX_TREE_MAP_SHIFT;
        height--;
    } while (height > 0);

    if (slot == NULL)
        goto out;

    /*
     * Clear all tags associated with the just-deleted item
     */
    for (tag = 0; tag < RADIX_TREE_MAX_TAGS; tag++) {
        if (tag_get(pathp->node, tag, pathp->offset))
            radix_tree_tag_clear(root, index, tag);
    }

    to_free = NULL;
    /* Now free the nodes we do not need anymore */
    while (pathp->node) {
        pathp->node->slots[pathp->offset] = NULL;
        pathp->node->count--;
        /*
         * Queue the node for deferred freeing after the
         * last reference to it disappears (set NULL, above).
         */
        if (to_free)
            radix_tree_node_free(to_free);

        if (pathp->node->count) {
            if (pathp->node == radix_tree_indirect_to_ptr(root->rnode))
                radix_tree_shrink(root);
            goto out;
        }

        /* Node with zero slots in use so free it */
        to_free = pathp->node;
        pathp--;
    }
    root_tag_clear_all(root);
    root->height = 0;
    root->rnode = NULL;
    if (to_free)
        radix_tree_node_free(to_free);
out:
    return slot;
}

// ===================free====================
void radix_tree_node_free(struct radix_tree_node *node) {
    tag_clear(node, 0, 0);
    tag_clear(node, 1, 0);
    node->slots[0] = NULL;
    node->count = 0;
    kfree(node);
}

// lookup a batch of items
// tag is valid , grap items
// tag isn't valid, grap items with tag
// max_items == -1 (the max of uint64), grap items without limit
// we merged the implements of __lookup and __lookup_tag
int radix_tree_lookup_batch_elements(struct radix_tree_node *slot, void *page_head, page_op function, uint64 index,
                                     uint64 max_items, uint64 *next_index, int tag) {
    uint32 nr_found = 0;
    uint32 shift, height;

    // we merge the implements of __lookup and __lookup_tag
    uint32 height_min = (tag_valid(tag)) ? 0 : 1;
    uint64 i;

    height = slot->height;
    if (height == 0)
        goto out;
    shift = (height - 1) * RADIX_TREE_MAP_SHIFT;

    while (height > height_min) {
        i = (index >> shift) & RADIX_TREE_MAP_MASK;
        for (;;) {
            // we merge the implements of __lookup and __lookup_tag
            if (tag_valid(tag) && tag_get(slot, tag, i))
                break;
            if (!tag_valid(tag) && slot->slots[i] != NULL)
                break;

            index &= ~((1UL << shift) - 1);
            index += 1UL << shift;
            if (index == 0)
                goto out; /* 32-bit wraparound */
            i++;
            if (i == RADIX_TREE_MAP_SIZE)
                goto out;
        }
        height--;

        // we merge the implements of __lookup and __lookup_tag
        if (tag_valid(tag) && height == 0) {
            break;
        }

        shift -= RADIX_TREE_MAP_SHIFT;
        slot = (slot->slots[i]);
        if (slot == NULL)
            goto out;
    }

    /* Bottom level: grab some items */
    for (i = index & RADIX_TREE_MAP_MASK; i < RADIX_TREE_MAP_SIZE; i++) {
        index++;
        if (tag_valid(tag) && !tag_get(slot, tag, i))
            continue;
        if (slot->slots[i]) {
            // function(page_head, &(slot->slots[i]), index - 1, slot->slots[i]);
            function(page_head, slot->slots[i], index - 1, slot->slots[i]);

            nr_found++;
            if (nr_found == max_items)
                goto out;
        }
    }

out:
    *next_index = index;
    return nr_found;
}

int radix_tree_general_gang_lookup_elements(struct radix_tree_root *root, void *page_head, page_op function,
                                            uint64 first_index, uint64 max_items, int tag) {
    uint64 max_index;
    struct radix_tree_node *node;
    uint64 cur_index = first_index;
    int ret;

    // tag is valid
    /* check the root's tag bit */
    if (tag_valid(tag)) {
        if (!root_tag_get(root, tag))
            return 0;
    }

    node = root->rnode;
    if (!node)
        return 0;
    if (!radix_tree_is_indirect_ptr(node)) {
        if (first_index > 0)
            return 0;
        function(page_head, node, 0, NULL);
        return 1;
    }

    node = radix_tree_indirect_to_ptr(node);
    max_index = radix_tree_maxindex(node->height);

    ret = 0;
    while (ret < max_items) {
        uint32 slots_found;
        uint64 next_index; /* Index of next search */
        if (cur_index > max_index)
            break;

        slots_found = radix_tree_lookup_batch_elements(node, page_head, function,
                                                       cur_index, max_items - ret, &next_index, tag);

        ret += slots_found;
        if (next_index == 0)
            break;

        if (unlikely(maxitems_valid(max_items)))
            break; // !!!

        cur_index = next_index;
    }

    return ret;
}

void radix_tree_free_whole_tree(struct radix_tree_node *node, uint32 max_height, uint32 height) {
#ifdef __DEBUG_PAGE_CACHE__
    uint64 pa_prev = 0;
#endif
    for (int i = 0; i < (1 << RADIX_TREE_MAP_SHIFT); i++) {
        if (node->slots[i]) {
            if (height == max_height) {
                struct page *page = (struct page *)(node->slots[i]);
                if (page->allocated == 1) { // don't forget it
                    uint64 pa = page_to_pa(page);
                    kfree((void *)pa); // must use page_to_pa
                                       // printfBlue("memory : %d PAGES\n", get_free_mem()/4096);
#ifdef __DEBUG_PAGE_CACHE__
                    pa_prev = pa;
                    printfBlue("radix tree free, pa : %x\n", pa);
#endif
                } else {
#ifdef __DEBUG_PAGE_CACHE__
                    pa_prev = pa_prev + PGSIZE;
                    printfBlue("radix tree free, pa : %x\n", pa_prev);
#endif
                    // panic("radix_tree free : error\n");
                }
            } else {
                radix_tree_free_whole_tree((struct radix_tree_node *)node->slots[i], max_height, height + 1);
            }
        }
    }
    // !!!
    kfree(node);
    // printfBlue("memory : %d PAGES\n", get_free_mem()/4096);
}
