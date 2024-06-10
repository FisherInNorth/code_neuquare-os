#include "common.h"
#include "kernel/trap.h"
#include "fs/vfs/fs_macro.h"
#include "proc/pcb_life.h"
#include "memory/allocator.h"

// for /dev/null and /dev/zero
int null_read(int user_dst, uint64 dst, int n) {
    // can't read any chars from /dev/null
    return 0;
}

int null_write(int user_src, uint64 src, int n) {
    // all chars disappear
    return n;
}

int zero_read(int user_dst, uint64 dst, int n) {
    // read n '\0'
    uchar *buf_zero;
    // avoid stacking overflow
    if ((buf_zero = kzalloc(n)) == NULL) {
        panic("zero_read : no free space\n");
    }
    if (either_copyout(user_dst, dst, (void *)buf_zero, n) == -1) {
        panic("zero_read either_copyout error\n");
    }
    kfree(buf_zero);
    return n;
}

int zero_write(int user_dst, uint64 src, int n) {
    // can' write any chars to /dev/zero
    return 0;
}

void null_zero_dev_init() {
    devsw[DEV_NULL].read = null_read;
    devsw[DEV_NULL].write = null_write;
    devsw[DEV_ZERO].read = zero_read;
    devsw[DEV_ZERO].write = zero_write;
    Info("null and dev init [ok]\n");
}
