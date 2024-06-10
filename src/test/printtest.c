#include "common.h"

void snprintf_test() {
    char buf[100];
    memset(buf, 0, sizeof(buf));

    printf("sprintf test: %d,%s", sprintf(buf, "%d%c%c\n", 1, 'a', 'b'), buf);
    memset(buf, 0, sizeof(buf));

    // common test
    printf("snprintf test: %d,%s", snprintf(buf, 10, "test\n"), buf);
    memset(buf, 0, sizeof(buf));

    // test %d
    printf("snprintf test: %d,%s", snprintf(buf, 10, "%d\n", 1234), buf);
    memset(buf, 0, sizeof(buf));

    // test %0d
    printf("snprintf test: %d,%s", snprintf(buf, 10, "%0d\n", 1234), buf);
    memset(buf, 0, sizeof(buf));

    // test %010d
    printf("snprintf test: %d,%s", snprintf(buf, 10, "%010d\n", 1234), buf);
    memset(buf, 0, sizeof(buf));
    printf("snprintf test: %d,%s", snprintf(buf, 10, "%010d\n", -1234), buf);
    memset(buf, 0, sizeof(buf));

    // test %p
    void _entry();
    printf("snprintf test: %d,%s", snprintf(buf, 20, "%0p\n", _entry), buf);
    memset(buf, 0, sizeof(buf));

    // test %s
    printf("snprintf test: %d,%s", snprintf(buf, 20, "%s\n", "snprintf_test"), buf);
    memset(buf, 0, sizeof(buf));

    // test %x
    printf("snprintf test: %d,%s", snprintf(buf, 20, "%#x\n", 16), buf);
    memset(buf, 0, sizeof(buf));
    printf("snprintf test: %d,%s", snprintf(buf, 20, "%#010X\n", 16), buf);
    memset(buf, 0, sizeof(buf));

    // test: size < string_length
    printf("snprintf test: %d,%s", snprintf(buf, 10, "%s\n", "0123456789abcdefghijklmn\n"), buf);
    printf("\n");
    memset(buf, 0, sizeof(buf));
    printf("snprintf test: %d,%s", snprintf(buf, 10, "%4d\n", 12345), buf);
    memset(buf, 0, sizeof(buf));

    // test: size == 0
    printf("snprintf test: %d,%s", snprintf(buf, 0, "%s\n", "0123456789abcdefghijklmn\n"), buf);
    printf("\n");
    memset(buf, 0, sizeof(buf));
}

void printf_test() {
    printf("printf_test: test\n");
    printf("printf_test: %d\n", 1234);
    printf("printf_test: %10d\n", 1234);
    printf("printf_test: %010d\n", 1234);
    printf("printf_test: %d\n", -1234);
    printf("printf_test: %d\n", 0);
    printf("printf_test: %s\n", "string test");
    printf("printf_test: %x\n", 16);
    printf("printf_test: %5x\n", 16);
    printf("printf_test: %05x\n", 16);
    printf("printf_test: %#05x\n", 16);
    printf("printf_test: %#05X\n", 16);
    printf("printf_test: %c\n", 'c');
    printf("printf_test: %10c\n", 'c');

    char *str = "abcdefg";
    void _entry();
    printf("printf_test: %s\n", str);
    printf("printf_test: %p\n", _entry);
    printf("printf_test: %5p\n", _entry);
    printf("printf_test: %10p\n", _entry);
    printf("printf_test: %20p\n", _entry);
    printf("printf_test: %020p\n", _entry);
}