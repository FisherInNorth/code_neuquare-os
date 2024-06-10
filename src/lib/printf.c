//
// formatted console output -- printf, panic.
//

#include <stdarg.h>

#include "common.h"
#include "param.h"
#include "atomic/spinlock.h"

#include "memory/memlayout.h"
#include "lib/riscv.h"
#include "driver/console.h"

#include "lib/ctype.h"
#include "debug.h"
#include "lib/sbi.h"

volatile int panicked = 0;

// lock to avoid interleaving concurrent printf's.
static struct {
    struct spinlock lock;
    int locking;
} pr;

#define ZEROPAD 1  /* pad with zero */
#define SIGN 2     /* unsigned/signed long */
#define PLUS 4     /* show plus */
#define SPACE 8    /* space if plus */
#define LEFT 16    /* left justified */
#define SPECIAL 32 /* 0x */
#define LARGE 64   /* use 'ABCDEF' instead of 'abcdef' */

#define do_div(n, base) ({ \
int __res; \
__res = ((unsigned long) n) % (unsigned) base; \
n = ((unsigned long) n) / (unsigned) base; \
__res; })

/* we use this so that we can do without the ctype library */
#define is_digit(c) ((c) >= '0' && (c) <= '9')

static int skip_atoi(const char **s) {
    int i = 0;

    while (is_digit(**s))
        i = i * 10 + *((*s)++) - '0';
    return i;
}
static void number(long num, int base, int size, int precision, int type) {
    char c, sign, tmp[66];
    const char *digits = "0123456789abcdefghijklmnopqrstuvwxyz";
    int i;

    if (type & LARGE)
        digits = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    if (type & LEFT)
        type &= ~ZEROPAD;
    if (base < 2 || base > 36)
        return;
    c = (type & ZEROPAD) ? '0' : ' ';
    sign = 0;
    if (type & SIGN) {
        if (num < 0) {
            sign = '-';
            num = -num;
            size--;
        } else if (type & PLUS) {
            sign = '+';
            size--;
        } else if (type & SPACE) {
            sign = ' ';
            size--;
        }
    }
    if (type & SPECIAL) {
        if (base == 16)
            size -= 2;
        else if (base == 8)
            size--;
    }
    i = 0;
    if (num == 0)
        tmp[i++] = '0';
    else
        while (num != 0)
            tmp[i++] = digits[do_div(num, base)];
    if (i > precision)
        precision = i;
    size -= precision;
    if (!(type & (ZEROPAD + LEFT)))
        while (size-- > 0)
            consputc(' ');
    if (sign)
        consputc(sign);
    if (type & SPECIAL) {
        if (base == 8)
            consputc('0');
        else if (base == 16) {
            consputc('0');
            consputc(digits[33]);
        }
    }
    if (!(type & LEFT)) {
        while (size-- > 0)
            consputc(c);
    }
    while (i < precision--)
        consputc('0');
    while (i-- > 0)
        consputc(tmp[i]);
    while (size-- > 0)
        consputc(' ');
}

void vprintf(const char *fmt, va_list args) {
    int len;
    unsigned long num;
    int i, base;
    char *s;

    int flags; /* flags to number() */

    int field_width; /* width of output field */
    int precision;   /* min. # of digits for integers; max
                   number of chars for from string */
    int qualifier;   /* 'l', or 'L' for integer fields */

    for (; *fmt; ++fmt) {
        if (*fmt != '%') {
            consputc(*fmt);
            continue;
        }

        /* process flags */
        flags = 0;
    repeat:
        ++fmt; /* this also skips first '%' */
        switch (*fmt) {
        case '-': flags |= LEFT; goto repeat;
        case '+': flags |= PLUS; goto repeat;
        case ' ': flags |= SPACE; goto repeat;
        case '#': flags |= SPECIAL; goto repeat;
        case '0': flags |= ZEROPAD; goto repeat;
        }

        /* get field width */
        field_width = -1;
        if (is_digit(*fmt))
            field_width = skip_atoi(&fmt);
        else if (*fmt == '*') {
            ++fmt;
            /* it's the next argument */
            field_width = va_arg(args, int);
            if (field_width < 0) {
                field_width = -field_width;
                flags |= LEFT;
            }
        }

        /* get the precision */
        precision = -1;
        if (*fmt == '.') {
            ++fmt;
            if (is_digit(*fmt))
                precision = skip_atoi(&fmt);
            else if (*fmt == '*') {
                ++fmt;
                /* it's the next argument */
                precision = va_arg(args, int);
            }
            if (precision < 0)
                precision = 0;
        }

        /* get the conversion qualifier */
        qualifier = -1;
        if (*fmt == 'l' || *fmt == 'L') {
            qualifier = *fmt;
            ++fmt;
        }

        /* default base */
        base = 10;

        switch (*fmt) {
        case 'c':
            if (!(flags & LEFT))
                while (--field_width > 0)
                    consputc(' ');
            consputc((unsigned char)va_arg(args, int));
            while (--field_width > 0)
                consputc(' ');
            continue;

        case 's':
            s = va_arg(args, char *);
            if (!s)
                s = "<NULL>";

            len = strnlen(s, precision);

            if (!(flags & LEFT))
                while (len < field_width--)
                    consputc(' ');
            for (i = 0; i < len; ++i)
                consputc(*s++);
            while (len < field_width--)
                consputc(' ');
            continue;

        case 'p':
            if (field_width == -1) {
                field_width = 2 * sizeof(void *);
                flags |= ZEROPAD;
            }
            number((unsigned long)va_arg(args, void *), 16,
                   field_width, precision, flags);
            continue;

        /* integer number formats - set up the flags and "break" */
        case 'o':
            base = 8;
            break;

        case 'X':
            flags |= LARGE;
        case 'x':
            base = 16;
            break;

        case 'd':
        case 'i':
            flags |= SIGN;
        case 'u':
            break;

        default:
            if (*fmt != '%')
                consputc('%');
            if (*fmt)
                consputc(*fmt);
            else
                --fmt;
            continue;
        }
        if (qualifier == 'l')
            num = va_arg(args, unsigned long);
        else if (flags & SIGN)
            num = va_arg(args, int);
        else
            num = va_arg(args, unsigned int);
        number(num, base, field_width, precision, flags);
    }
    return;
}

// Print to the console. only understands %d, %x, %p, %s.
void printf(char *fmt, ...) {
    // return;

    va_list ap;
    // int i, c, locking;
    int locking;
    // char *s;

    extern int debug_lock;
    // cannot debug lock when execute printf, it will cause recursive call
    debug_lock = 0;
    locking = pr.locking;
    if (locking)
        acquire(&pr.lock);
    debug_lock = 1;

    if (fmt == 0)
        panic("null fmt");

    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);

    debug_lock = 0;
    if (locking)
        release(&pr.lock);
    debug_lock = 1;
}

void panic(char *s) {
    pr.locking = 0;
    printf("panic: ");
    printf(s);
    printf("\n");
    panicked = 1; // freeze uart output from other CPUs
// extern void shutdown_writeback();
//     shutdown_writeback();
    sbi_shutdown();
    for (;;)
        ;
}

void printfinit(void) {
    initlock(&pr.lock, "pr");
    pr.locking = 1;
    Info("printf init [ok]\n");
}

void backtrace() {
    uint64 fp = r_fp();
    uint64 last_ra;
    Log("kernel backtrace");
    while (fp < PGROUNDUP(fp) && fp > PGROUNDDOWN(fp)) {
        last_ra = *(uint64 *)(fp - 8);
        fp = *(uint64 *)(fp - 16);
        printf("%p\n", last_ra);
    }
}

void Show_bytes(byte_pointer b, int len) {
    size_t i;
    printf("0x");
    for (i = 0; i < len; i++)
        printf("%X ", b[i]);
    printf("\n");
}

void printf_bin(uchar *num, int len) {
    for (int i = 0; i < len; i++) {
        unsigned char *p = (unsigned char *)&num[i];
        for (int k = 7; k >= 0; k--) {
            if (*p & (1 << k))
                printf("1");
            else
                printf("0");
        }
        printf(" ");
    }
    printf("\r\n");
}
