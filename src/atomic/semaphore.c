#include "proc/pcb_life.h"
#include "atomic/cond.h"
#include "atomic/semaphore.h"

void sema_init(sem *S, int value, char *name) {
    S->value = value;
    S->wakeup = 0;
    initlock(&S->sem_lock, name);
    cond_init(&S->sem_cond, name);
}

void sema_wait(sem *S) {
    acquire(&S->sem_lock);
    S->value--;
    if (S->value < 0) {
        do {
            cond_wait(&S->sem_cond, &S->sem_lock);
        } while (S->wakeup == 0);
        S->wakeup--;
    }
    release(&S->sem_lock);
}

void sema_signal(sem *S) {
    acquire(&S->sem_lock);
    S->value++;
    if (S->value <= 0) {
        S->wakeup++;
        cond_signal(&S->sem_cond);
    }
    release(&S->sem_lock);
}