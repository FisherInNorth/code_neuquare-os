#ifndef __SBI_H__
#define __SBI_H__

#include "common.h"

#define EXTENSION_BASE 1
#define LEGACY_SET_TIMER 0x00L
#define SHUTDOWN_EXT 0x08L
#define TIMER_EXT 0x54494D45L
#define HSM_EXT 0x48534DL

#define SBI_SUCCESS 0

#define SBI_CALL(ext, funct, arg0, arg1, arg2, arg3) ({        \
    register uintptr_t a0 asm("a0") = (uintptr_t)(arg0);       \
    register uintptr_t a1 asm("a1") = (uintptr_t)(arg1);       \
    register uintptr_t a2 asm("a2") = (uintptr_t)(arg2);       \
    register uintptr_t a3 asm("a3") = (uintptr_t)(arg3);       \
    register uintptr_t a6 asm("a6") = (uintptr_t)(funct);      \
    register uintptr_t a7 asm("a7") = (uintptr_t)(ext);        \
    asm volatile("ecall"                                       \
                 : "+r"(a0), "+r"(a1)                          \
                 : "r"(a1), "r"(a2), "r"(a3), "r"(a6), "r"(a7) \
                 : "memory");                                  \
    (struct sbiret){a0, a1};                                   \
})

#define SBI_CALL_0(ext, funct) SBI_CALL(ext, funct, 0, 0, 0, 0)
#define SBI_CALL_1(ext, funct, arg0) SBI_CALL(ext, funct, arg0, 0, 0, 0)
#define SBI_CALL_2(ext, funct, arg0, arg1) SBI_CALL(ext, funct, arg0, arg1, 0, 0)
#define SBI_CALL_3(ext, funct, arg0, arg1, arg2) SBI_CALL(ext, funct, arg0, arg1, arg2, 0)
#define SBI_CALL_4(ext, funct, arg0, arg1, arg2, arg3) SBI_CALL(ext, funct, arg0, arg1, arg2, arg3)

struct sbiret {
    long error;
    long value;
};

static inline struct sbiret sbi_set_timer(uint64 stime_value) {
    // stime_value is in absolute time.
    return SBI_CALL_1(TIMER_EXT, 0, stime_value);
}

static inline struct sbiret sbi_legacy_set_timer(uint64 stime_value) {
    return SBI_CALL_1(LEGACY_SET_TIMER, 0, stime_value);
}

static inline struct sbiret sbi_shutdown() {
    return SBI_CALL_0(SHUTDOWN_EXT, 0);
}

static inline int sbi_hart_get_status(uint64 hartid) {
    struct sbiret ret;
    ret = SBI_CALL_1(HSM_EXT, 2, hartid);
    return (ret.error == 0 ? (int)ret.value : (int)ret.error);
}

// return the error code. On success, SBI_SUCCESS is returned
static inline int sbi_hart_start(uint64 hartid, uint64 start, uint64 arg) {
    return SBI_CALL_3(HSM_EXT, 0, hartid, start, arg).error;
}

#endif // __SBI_H__
