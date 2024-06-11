#include "proc/pdflush.h"
#include "proc/tcb_life.h"
#include "proc/sched.h"
#include "lib/list.h"
#include "memory/writeback.h"
#include "debug.h"

struct pdflush pdflush_control;
extern struct proc *initproc;
uint64 last_empty_time;

void pdflush_init() {
    initlock(&pdflush_control.lock, "pdflush_list_lock");
    atomic_set(&pdflush_control.nr_pdflush_threads, 0);
    INIT_LIST_HEAD(&pdflush_control.entry);
    cond_init(&pdflush_control.pdflush_cond, "pdflush_cond");

    for (int i = 0; i < MIN_PDFLUSH_THREADS; i++)
        start_one_pdflush_thread();
    // page_writeback_timer_init();
}

static int __pdflush(struct pdflush_work *my_work) {
    my_work->fn = NULL;
    my_work->who = thread_current();
    INIT_LIST_HEAD(&my_work->list);

    acquire(&pdflush_control.lock);
    atomic_inc_return(&pdflush_control.nr_pdflush_threads);
    while (1) {
        struct pdflush_work *pdf;

        // suspend this pdflush into list
        list_move(&my_work->list, &pdflush_control.entry);

        // unit is s !!!
        my_work->when_i_went_to_sleep = TIME2SEC(rdtime());

        int wait_ret = cond_wait(&pdflush_control.pdflush_cond, &pdflush_control.lock);
        if (wait_ret == 0) {
            printfRed("pdflush : error\n");
        } else {
            printfRed("pdflush : wakeup\n");
        }

        // ensure my_work is removed form list
        if (!list_empty(&my_work->list)) {
            printfRed("pdflush: bogus wakeup!\n");
            my_work->fn = NULL;
            continue;
        }
        // no function???
        if (my_work->fn == NULL) {
            printfRed("pdflush: NULL work function\n");
            continue;
        }
        release(&pdflush_control.lock);

        // start excute the assignment
        (*my_work->fn)(my_work->arg0);

        /*
         * Thread creation: For how long have there been zero available threads?
         */

        if (TIME2SEC(rdtime()) - last_empty_time > 1) { // 到最近的1s期间内没有空闲的pdflush
            /* unlocked list_empty() test is OK here */
            if (list_empty(&pdflush_control.entry)) {
                /* unlocked test is OK here */
                if (atomic_read(&pdflush_control.nr_pdflush_threads) < MAX_PDFLUSH_THREADS)
                    start_one_pdflush_thread();
            }
        }

        acquire(&pdflush_control.lock);
        my_work->fn = NULL;

        //  * Thread destruction: For how long has the sleepiest thread slept?
        if (list_empty(&pdflush_control.entry))
            continue;

        if (atomic_read(&pdflush_control.nr_pdflush_threads) <= MIN_PDFLUSH_THREADS)
            continue;

        pdf = list_last_entry(pdflush_control.entry.prev, struct pdflush_work, list); // fetch the last pdflush_work
        if (TIME2SEC(rdtime()) - pdf->when_i_went_to_sleep > 1) {                     // 最近变空闲的时间超过了1s
            /* Limit exit rate */
            pdf->when_i_went_to_sleep = TIME2SEC(rdtime());
            break;
        }
    }
    atomic_inc_return(&pdflush_control.nr_pdflush_threads);

    release(&pdflush_control.lock);

    panic("pdflush decrease\n");
    return 0;
}

static void pdflush(void) {
    // similar to thread_forkret
    release(&thread_current()->lock);
    struct pdflush_work my_work;
    __pdflush(&my_work);
}

int pdflush_operation(void (*fn)(uint64), uint64 arg0) {
    int ret = 0;
    ASSERT(fn != NULL);

    acquire(&pdflush_control.lock);
    if (list_empty(&pdflush_control.entry)) {
        release(&pdflush_control.lock);
        ret = -1;
    } else {
        struct pdflush_work *pdf;
        pdf = list_first_entry(pdflush_control.entry.next, struct pdflush_work, list); //从pdflush链表中取出第一项

        list_del_reinit(&pdf->list);
        if (list_empty(&pdflush_control.entry))
            last_empty_time = TIME2SEC(rdtime());
        pdf->fn = fn;
        pdf->arg0 = arg0;
        // wakeup thread
        thread_wakeup_atomic((void *)pdf->who);
        release(&pdflush_control.lock);
    }
    return ret;
}

void start_one_pdflush_thread() {
    struct tcb *t = NULL;
    create_thread(initproc, t, NULL, pdflush);
}