#include "kernel/trap.h"
#include "lib/sbuf.h"
#include "memory/allocator.h"
#include "debug.h"

void sbuf_init(struct sbuf *sp, uint n) {
    sp->buf = (char *)kmalloc(sizeof(char) * n);
    if (sp->buf == NULL) {
        panic("no space\n");
    }
    sp->r = 0;
    sp->w = 0;
    sp->n = n;
    initlock(&sp->lock, "pipe");
    sema_init(&sp->items, 0, "items_sem");
    sema_init(&sp->slots, n, "slots_sem");
}

void sbuf_free(struct sbuf *sp) {
    kfree(sp->buf);
}

int sbuf_empty(struct sbuf *sp) {
    acquire(&sp->lock);
    int ret = (sp->r == sp->w);
    release(&sp->lock);
    return ret;
}

int sbuf_full(struct sbuf *sp) {
    acquire(&sp->lock);
    int ret = (sp->r + sp->n == sp->w);
    release(&sp->lock);
    return ret;
}

int sbuf_insert(struct sbuf *sp, int user_dst, uint64 addr) {
    char item;
    sema_wait(&sp->slots);
    acquire(&sp->lock);
    if (either_copyin(&item, user_dst, addr, 1) == -1) {
        release(&sp->lock);
        return -1;
    }
    sp->buf[(sp->w)++ % sp->n] = item;
    release(&sp->lock);

    sema_signal(&sp->items);
    // printfGreen("insert : wakeup, items sem : %d, r : %d, w : %d\n", sp->items.value, sp->r, sp->w);
    return 0;
}

int sbuf_remove(struct sbuf *sp, int user_dst, uint64 addr) {
    char item;

    if (sp->r == sp->w && sp->w != 0) {
        return 1;
    } // bug!!!

    sema_wait(&sp->items);

    if (sp->r == sp->w) {
        return 1; // !!! bug
    }

    acquire(&sp->lock);
    item = sp->buf[(sp->r)++ % (sp->n)];
    if (either_copyout(user_dst, addr, &item, 1) == -1) {
        release(&sp->lock);
        return -1;
    }
    release(&sp->lock);
    sema_signal(&sp->slots);

    // printfRed("remove : wakeup, slots sem : %d, r : %d, w : %d\n", sp->slots.value, sp->r, sp->w);
    return 0;
}