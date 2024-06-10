#define USER
#include "stddef.h"
#include "unistd.h"
#include "stdio.h"
#include "string.h"


int main(int argc, char *argv[])
{
    // 打开文件
    int fd = open("oscomp/text.txt", O_RDONLY);
    if (fd == -1) {
        perror("open");
        return 1;
    }

    // 移动文件指针到文件末尾
    off_t offset = lseek(fd, 0, SEEK_END);
    if (offset == -1) {
        perror("lseek");
        close(fd);
        return 1;
    }

    // 输出文件大小
    printf("File size: %d bytes\n", (long)offset);
    
    
    // 关闭文件
    close(fd);

    return 0;
}
