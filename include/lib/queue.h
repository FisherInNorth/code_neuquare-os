#ifndef __QUEUE_H__
#define __QUEUE_H__

#include "atomic/spinlock.h"
#include "lib/list.h"

struct tcb;
struct proc;

enum queue_type { TCB_STATE_QUEUE,
                  PCB_STATE_QUEUE,
                  TCB_WAIT_QUEUE,
                  INODE_FREE_QUEUE };

struct Queue {
    struct spinlock lock;
    struct list_head list;
    char name[30]; // the name of queue
    enum queue_type type;
};

typedef struct Queue Queue_t;

// init
void Queue_init(Queue_t *q, char *name, enum queue_type type);

// is empty?
int Queue_isempty(Queue_t *q);

// is empty (atomic)?
int Queue_isempty_atomic(Queue_t *q);

// acquire the list entry of node
struct list_head *queue_entry(void *node, enum queue_type type);

// acquire the first node of queue given type of queue
void *queue_first_node(Queue_t *q);

// push back
void Queue_push_back(Queue_t *q, void *node);

// push back (atomic)
void Queue_push_back_atomic(Queue_t *q, void *node);

// move it from its old Queue
void Queue_remove(void *node, enum queue_type type);

// move it from its old Queue (atomic)
void Queue_remove_atomic(Queue_t *q, void *node);

// pop the queue
void *Queue_pop(Queue_t *q, int remove);

// provide the first one of the queue (atomic)
void *Queue_provide_atomic(Queue_t *q, int remove);

#endif