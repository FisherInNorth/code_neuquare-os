#ifndef __ATOMIC_OPS_H__
#define __ATOMIC_OPS_H__

#include "common.h"

// 原子操作数据结构
typedef struct {
    volatile int counter;
} atomic_t;

// 初始化
#define ATOMIC_INITIALIZER \
    { 0 }
#define ATOMIC_INIT(i) \
    { (i) }

// 原子读取
/* #define WRITE_ONCE(var, val) \
     (*((volatile typeof(val) *)(&(var))) = (val)) */
#define WRITE_ONCE(var, val)          \
    do {                              \
        union {                       \
            volatile typeof(val) tmp; \
            typeof(var) result;       \
        } u = {.tmp = (val)};         \
        (var) = u.result;             \
    } while (0)
#define READ_ONCE(var) (*((volatile typeof(var) *)(&(var))))

// 原子性的读和设置
#define atomic_read(v) READ_ONCE((v)->counter)
#define atomic_set(v, i) WRITE_ONCE(((v)->counter), (i))
// #define atomic_set(v, i)

// 自增
static inline int atomic_inc_return(atomic_t *v) {
    return __sync_fetch_and_add(&v->counter, 1);
}

// 自减
static inline int atomic_dec_return(atomic_t *v) {
    return __sync_fetch_and_sub(&v->counter, 1);
}

// 增加
static inline int atomic_add_return(atomic_t *v, int i) {
    return __sync_fetch_and_add(&v->counter, i);
}

// 减少
static inline int atomic_sub_return(atomic_t *v, int i) {
    return __sync_fetch_and_sub(&v->counter, i);
}

// ==========================bit ops================================
#define __WORDSIZE 64
#define BITS_PER_LONG __WORDSIZE
#define BIT_WORD(nr) ((nr) / BITS_PER_LONG)
#define BIT_MASK(nr) (1UL << ((nr) % BITS_PER_LONG))
#if (BITS_PER_LONG == 64)
#define __AMO(op) "amo" #op ".d"
#elif (BITS_PER_LONG == 32)
#define __AMO(op) "amo" #op ".w"
#else
#error "Unexpected BITS_PER_LONG"
#endif

#define __op_bit_ord(op, mod, nr, addr, ord) \
    __asm__ __volatile__(                    \
        __AMO(op) #ord " zero, %1, %0"       \
        : "+A"(addr[BIT_WORD(nr)])           \
        : "r"(mod(BIT_MASK(nr)))             \
        : "memory");

#define __op_bit(op, mod, nr, addr) \
    __op_bit_ord(op, mod, nr, addr, )
#define __NOP(x) (x)
#define __NOT(x) (~(x))
static inline void set_bit(int nr, volatile uint64 *addr) {
    __op_bit(or, __NOP, nr, addr);
}

static inline void clear_bit(int nr, volatile uint64 *addr) {
    __op_bit(and, __NOT, nr, addr);
}

// not atomic
static inline int test_bit(int nr, const volatile void *addr) {
    return (1UL & (((const int *)addr)[nr >> 5] >> (nr & 31))) != 0UL;
}

#endif // __ATOMIC_H__