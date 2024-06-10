#include "common.h"
#include "debug.h"

void *
memset(void *dst, int c, uint n) {
    char *cdst = (char *)dst;
    int i;
    for (i = 0; i < n; i++) {
        cdst[i] = c;
    }
    return dst;
}

int memcmp(const void *v1, const void *v2, uint n) {
    const uchar *s1, *s2;

    s1 = v1;
    s2 = v2;
    while (n-- > 0) {
        if (*s1 != *s2)
            return *s1 - *s2;
        s1++, s2++;
    }

    return 0;
}

void *
memmove(void *dst, const void *src, uint n) {
    const char *s;
    char *d;

    if (n == 0)
        return dst;

    s = src;
    d = dst;
    if (s < d && s + n > d) {
        s += n;
        d += n;
        while (n-- > 0) {
            *--d = *--s;
        }

    } else
        while (n-- > 0) {
            if (*d == 0x88200000 || *s == 0x88200000) {
                Log("ready\n");
            }
            *d++ = *s++;
        }

    return dst;
}

// memcpy exists to placate GCC.  Use memmove.
void *
memcpy(void *dst, const void *src, uint n) {
    return memmove(dst, src, n);
}

int strncmp(const char *p, const char *q, uint n) {
    while (n > 0 && *p && *p == *q)
        n--, p++, q++;
    if (n == 0)
        return 0;
    return (uchar)*p - (uchar)*q;
}

char *
strncpy(char *s, const char *t, int n) {
    char *os;

    os = s;
    while (n-- > 0 && (*s++ = *t++) != 0)
        ;
    while (n-- > 0)
        *s++ = 0;
    return os;
}

// Like strncpy but guaranteed to NUL-terminate.
char *
safestrcpy(char *s, const char *t, int n) {
    char *os;

    os = s;
    if (n <= 0)
        return os;
    while (n-- > 0 && (*s++ = *t++) != 0)
        ;
    *s = 0;
    return os;
}

int strlen(const char *s) {
    int n;

    for (n = 0; s[n]; n++)
        ;
    return n;
}

size_t strnlen(const char *s, size_t count) {
    const char *sc;

    for (sc = s; *sc != '\0' && count--; ++sc)
        /* nothing */;
    return sc - s;
}

#include "lib/ctype.h"
void str_toupper(char *str) {
    if (str != NULL) {
        while (*str != '\0') {
            *str = toupper(*str);
            str++;
        }
    }
}

void str_tolower(char *str) {
    if (str != NULL) {
        while (*str != '\0') {
            *str = tolower(*str);
            str++;
        }
    }
}
char *strchr(const char *str, int c) {
    while (*str != '\0') {
        if (*str == (char)c) {
            return (char *)str;
        }
        str++;
    }
    if (c == '\0') {
        return (char *)str;
    }
    return NULL;
}

int str_split(const char *str, const char ch, char *str1, char *str2) {
    char *p = strchr(str, ch);
    if (p == NULL) {
        return -1;
    }
    strncpy(str1, str, p - str);
    strncpy(str2, p + 1, strlen(str) - 1 - (p - str));

    return 1;
}

char *strcat(char *dest, const char *src) {
    char *p = dest;
    while (*p) {
        ++p;
    }
    while (*src) {
        *p++ = *src++;
    }
    *p = '\0';
    return dest;
}

// 查找子串 needle 在 haystack 中首次出现的位置
char *strstr(const char *haystack, const char *needle) {
    if (*needle == '\0') {
        return (char *)haystack; // 空子串在任何字符串中都存在
    }

    while (*haystack != '\0') {
        const char *h = haystack;
        const char *n = needle;

        while (*h == *n && *n != '\0') {
            h++;
            n++;
        }

        if (*n == '\0') {
            return (char *)haystack; // 找到子串
        }

        haystack++;
    }

    return NULL; // 未找到子串
}

int strcmp(const char *l, const char *r) {
    for (; *l == *r && *l; l++, r++)
        ;
    return *(unsigned char *)l - *(unsigned char *)r;
}

int is_suffix(const char *str, const char *suffix) {
    size_t str_len = strlen(str);
    size_t suffix_len = strlen(suffix);

    if (suffix_len > str_len) {
        return 0; // 子串比字符串更长，不可能是结尾
    }

    const char *str_end = str + (str_len - suffix_len);
    return strcmp(str_end, suffix) == 0;
}