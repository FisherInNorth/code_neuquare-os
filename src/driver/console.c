//
// Console input and output, to the uart.
// Reads are line at a time.
// Implements special input characters:
//   newline -- end of line
//   control-h -- backspace
//   control-u -- kill line
//   control-d -- end of file
//   control-p -- print process list
//

#include <stdarg.h>

#include "common.h"
#include "param.h"
#include "atomic/spinlock.h"

#include "memory/memlayout.h"
#include "lib/riscv.h"
#include "proc/pcb_life.h"
#include "driver/uart.h"
#include "proc/pcb_mm.h"
#include "ipc/signal.h"
#include "atomic/cond.h"
#include "atomic/semaphore.h"
#include "kernel/trap.h"
#include "fs/stat.h"
#include "debug.h"

#include "termios.h"
///

struct termios term = {
    .c_iflag = ICRNL,
    .c_oflag = OPOST,
    .c_cflag = 0,
    .c_lflag = ECHO | ICANON,
    .c_line = 0,
    .c_cc = {0},
};

void uartinit(void);
#define BACKSPACE 0x100
#define C(x) ((x) - '@') // Control-x

//
// send one character to the uart.
// called by printf(), and to echo input characters,
// but not from write().
//
void consputc(int c) {
    if (c == BACKSPACE) {
        // if the user typed backspace, overwrite with a space.
        uartputc_sync('\b');
        uartputc_sync(' ');
        uartputc_sync('\b');
    } else {
        uartputc_sync(c);
    }
}

struct console {
    struct spinlock lock;

    // input
#define INPUT_BUF_SIZE 128
    char buf[INPUT_BUF_SIZE];
    uint r; // Read index
    uint w; // Write index
    uint e; // Edit index

    struct semaphore sem_r;
    struct semaphore sem_w;
} cons;

//
// user write()s to the console go here.
//
int consolewrite(int user_src, uint64 src, int n) {
    int i;

    for (i = 0; i < n; i++) {
        char c;
        if (either_copyin(&c, user_src, src + i, 1) == -1)
            break;
        uartputc(c);
    }

    return i;
}

//
// user read()s from the console go here.
// copy (up to) a whole input line to dst.
// user_dist indicates whether dst is a user
// or kernel address.
//
int consoleread(int user_dst, uint64 dst, int n) {
    uint target;
    char c;

    uint lflag = term.c_lflag;

    target = n;
    acquire(&cons.lock);
    while (n > 0) {
        // wait until interrupt handler has put some
        // input into cons.buffer.
        while (cons.r == cons.w) {
            if (proc_current()->killed) {
                release(&cons.lock);
                return -1;
            }
            release(&cons.lock);
            sema_wait(&cons.sem_r);
            acquire(&cons.lock);
        }

        c = cons.buf[cons.r++ % INPUT_BUF_SIZE];

        if ((lflag & ICANON) == 0) {
            if (either_copyout(user_dst, dst, &c, 1) == -1)
                break;
            dst++;
            --n;
            continue;
        }

        if (c == C('D')) { // end-of-file
            if (n < target) {
                // Save ^D for next time, to make sure
                // caller gets a 0-byte result.
                cons.r--;
            }
            break;
        }

        // copy the input byte to the user-space buffer.

        if (either_copyout(user_dst, dst, &c, 1) == -1)
            break;

        dst++;
        --n;

        if (c == '\n') {
            // a whole line has arrived, return to
            // the user-level read().
            break;
        }
    }
    release(&cons.lock);

    return target - n;
}

void consoleintr(char c) {
    uint16 iflag = term.c_iflag;
    uint16 lflag = term.c_lflag;
    acquire(&cons.lock);

    // not cookmode
    if ((lflag & ICANON) == 0) {
        if (c != 0 && cons.e - cons.r < INPUT_BUF_SIZE) {
            // echo back to the user.
            if (lflag & ECHO) consputc(c);

            // store for consumption by consoleread().
            cons.buf[cons.e++ % INPUT_BUF_SIZE] = c;
            cons.w = cons.e;
            sema_signal(&cons.sem_r);
        }
        release(&cons.lock);
        return;
    }

    switch (c) {
    case C('T'): // Print thread list.
        proc_thread_print();
        break;
    case C('U'): // Kill line.
        while (cons.e != cons.w && cons.buf[(cons.e - 1) % INPUT_BUF_SIZE] != '\n') {
            cons.e--;
            consputc(BACKSPACE);
        }
        break;
    case C('H'): // Backspace
    case '\x7f':
        if (cons.e != cons.w) {
            cons.e--;
            consputc(BACKSPACE);
        }
        break;
    default:
        if (c != 0 && cons.e - cons.r < INPUT_BUF_SIZE) {
            if (iflag & ICRNL)
                c = (c == '\r') ? '\n' : c; // 回车转换行

            // echo back to the user.
            if (lflag & ECHO) consputc(c);

            // store for consumption by consoleread().
            cons.buf[cons.e++ % INPUT_BUF_SIZE] = c;

            if (c == '\n' || c == C('D') || cons.e == cons.r + INPUT_BUF_SIZE) {
                // wake up consoleread() if a whole line (or end-of-file)
                // has arrived.
                cons.w = cons.e;
                sema_signal(&cons.sem_r);
            }
        }
        break;
    }

    release(&cons.lock);
}

void consoleinit(void) {
    initlock(&cons.lock, "cons");
    sema_init(&cons.sem_r, 0, "cons_sema_r");
    cons.e = cons.w = cons.r = 0;

    uartinit();

    // connect read and write system calls
    // to consoleread and consolewrite.
    devsw[CONSOLE].read = consoleread;
    devsw[CONSOLE].write = consolewrite;
    Info("uart and console init [ok]\n");
}

int consoleready() {
    acquire(&cons.lock);
    int ready = cons.w - cons.r;
    release(&cons.lock);
    return ready;
}