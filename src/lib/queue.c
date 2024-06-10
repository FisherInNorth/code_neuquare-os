#include "lib/queue.h"
#include "lib/list.h"
#include "proc/tcb_life.h"

// init
void Queue_init(Queue_t *q, char *name, enum queue_type type) {
    initlock(&q->lock, name);
    INIT_LIST_HEAD(&q->list);
    safestrcpy(q->name, name, strlen(name));
    q->type = type;
}

// is empty?
int Queue_isempty(Queue_t *q) {
    return list_empty(&(q->list));
}

// is empty (atomic)?
int Queue_isempty_atomic(Queue_t *q) {
    acquire(&q->lock);
    int empty = list_empty(&(q->list));
    release(&q->lock);
    return empty;
}

// acquire the list entry of node
struct list_head *queue_entry(void *node, enum queue_type type) {
    struct list_head *list = NULL;
    switch (type) {
    case TCB_STATE_QUEUE:
        list = &(((struct tcb *)node)->state_list);
        break;
    case PCB_STATE_QUEUE:
        list = &(((struct proc *)node)->state_list);
        break;
    case TCB_WAIT_QUEUE:
        list = &(((struct tcb *)node)->wait_list);
        break;
    default:
        panic("this type is invalid\n");
    }
    return list;
}

// get first node of queue
void *queue_first_node(Queue_t *q) {
    void *node = NULL;
    switch (q->type) {
    case TCB_STATE_QUEUE:
        node = list_first_entry(&(q->list), struct tcb, state_list);
        break;
    case PCB_STATE_QUEUE:
        node = list_first_entry(&(q->list), struct proc, state_list);
        break;
    case TCB_WAIT_QUEUE:
        node = list_first_entry(&(q->list), struct tcb, wait_list);
        break;
    default:
        panic("this type is invalid\n");
    }
    return node;
}

// push back
void Queue_push_back(Queue_t *q, void *node) {
    struct list_head *list = queue_entry(node, q->type);
    list_add_tail(list, &(q->list));
}

// push back (atomic)
void Queue_push_back_atomic(Queue_t *q, void *node) {
    acquire(&q->lock);
    struct list_head *list = queue_entry(node, q->type);
    list_add_tail(list, &(q->list));
    release(&q->lock);
}

// move it from its old waiting QUEUE
void Queue_remove(void *node, enum queue_type type) {
    struct list_head *list = queue_entry(node, type);
    list_del_reinit(list);
}

// move it from its old waiting QUEUE (atomic)
void Queue_remove_atomic(Queue_t *q, void *node) {
    acquire(&q->lock);
    struct list_head *list = queue_entry(node, q->type);
    list_del_reinit(list);
    release(&q->lock);
}

// pop the queue
void *Queue_pop(Queue_t *q, int remove) {
    if (Queue_isempty(q))
        return NULL;
    void *node = queue_first_node(q);
    // remove it from queue??
    if (remove)
        Queue_remove(node, q->type);
    return node;
}

// provide the first one of the queue (atomic)
void *Queue_provide_atomic(Queue_t *q, int remove) {
    acquire(&q->lock);
    void *t = Queue_pop(q, remove);
    release(&q->lock);
    // may return NULL
    return t;
}
