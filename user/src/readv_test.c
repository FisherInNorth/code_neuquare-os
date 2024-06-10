#define USER
#include "stddef.h"
#include "unistd.h"
#include "stdio.h"
#include "string.h"

// struct iovec {
//     void  *iov_base;    /* Starting address */
//     size_t iov_len;     /* Number of bytes to transfer */
// };
#define BUFFER_SIZE 1024
#define perror(msg)                             \
    do {                                        \
        printf("error: %s\n");                  \
    } while (0)                                 \

int main() {
    // 打开文件
    int fd = open("oscomp/text.txt", O_RDONLY);
    if (fd == -1) {
        perror("open");
        return 1;
    }

    // 创建两个缓冲区
    char buffer1[BUFFER_SIZE];
    char buffer2[BUFFER_SIZE];

    // 创建两个iovec结构体，用于指定缓冲区和长度
    struct iovec iov[2];
    iov[0].iov_base = buffer1;
    iov[0].iov_len = 6;
    iov[1].iov_base = buffer2;
    iov[1].iov_len = BUFFER_SIZE;

    // 调用readv函数读取数据
    ssize_t totalBytesRead = readv(fd, iov, 2);
    if (totalBytesRead == -1) {
        perror("readv");
        close(fd);
        return 1;
    }

    // 输出读取的数据
    printf("Total bytes read: %d\n", totalBytesRead);
    printf("Buffer 1(%d): %s\n", iov[0].iov_len, buffer1);
    printf("Buffer 2(%d): %s\n", totalBytesRead - iov[0].iov_len, buffer2);

    // 关闭文件
    close(fd);

    return 0;
}