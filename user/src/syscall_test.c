#define USER
#include "stddef.h"
#include "unistd.h"
#include "stdio.h"
#include "string.h"

int main() {
    int fd;
    fd = open(".", O_DIRECTORY);
    if (fd < 0) {
        return 0;
    } else {
        print_pgtable(fd, 1);
    }
    return 0;
}

