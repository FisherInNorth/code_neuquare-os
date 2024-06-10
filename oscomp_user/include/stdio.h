#ifndef __STDIO_H__
#define __STDIO_H__

#define STDIN 0
#define STDOUT 1
#define STDERR 2

#define S_IFMT (0xf0)
#define S_IFIFO (0xA0)                       // 1010_0000
#define S_IFREG (0x80)                       // 1000_0000
#define S_IFBLK (0x60)                       // 0110_0000
#define S_IFDIR (0x40)                       // 0100_0000
#define S_IFCHR (0x20)                       // 0010_0000
#define S_ISREG(t) (((t)&S_IFMT) == S_IFREG) // ip->i_type
#define S_ISDIR(t) (((t)&S_IFMT) == S_IFDIR)
#define S_ISCHR(t) (((t)&S_IFMT) == S_IFCHR)
#define S_ISBLK(t) (((t)&S_IFMT) == S_IFBLK)
#define S_ISFIFO(t) (((t)&S_IFMT) == S_IFIFO)

#define DT_UNKNOWN	0
#define DT_FIFO		1
#define DT_CHR		2
#define DT_DIR		4
#define DT_BLK		6
#define DT_REG		8
#define DT_LNK		10
#define DT_SOCK		12
#define DT_WHT		14


//#define TEST_START(x) puts(x)
#define TEST_START(x) puts("========== START ");puts(x);puts(" ==========\n");
#define TEST_END(x) puts("========== END ");puts(x);puts(" ==========\n");

#define stdin STDIN
#define stdout STDOUT
#define stderr STDERR

#define va_start(ap, last) (__builtin_va_start(ap, last))
#define va_arg(ap, type) (__builtin_va_arg(ap, type))
#define va_end(ap) (__builtin_va_end(ap))
#define va_copy(d, s) (__builtin_va_copy(d, s))

typedef __builtin_va_list va_list;
typedef unsigned long int uintmax_t;
typedef long int intmax_t;

int getchar();
int putchar(int);
int puts(const char *s);
void printf(const char *fmt, ...);

#endif // __STDIO_H__
