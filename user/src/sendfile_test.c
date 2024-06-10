#define USER
#include "stddef.h"
#include "unistd.h"
#include "stdio.h"
#include "string.h"


int main(int argc, char *argv[])
{
    const char* source_file = "oscomp/text.txt";
    const char* destination_file = "oscomp/destination.txt";

    // 打开源文件
    int source_fd = open(source_file, O_RDONLY);
    if (source_fd == -1) {
        perror("无法打开源文件");
        return 1;
    }

    // 创建目标文件
    int destination_fd = open(destination_file, O_WRONLY | O_CREAT | O_TRUNC);
    if (destination_fd == -1) {
        perror("无法创建目标文件");
        return 1;
    }

    // 获取源文件的大小
    struct kstat stat_buf;
    if (fstat(source_fd, &stat_buf) == -1) {
        perror("无法获取源文件大小");
        return 1;
    }
    off_t offset = 0;

    // 使用sendfile将数据从源文件复制到目标文件
    ssize_t bytes_sent = sendfile(destination_fd, source_fd, &offset, stat_buf.st_size);
    if (bytes_sent == -1) {
        perror("sendfile调用失败");
        return 1;
    }

    printf("已成功复制 %d 字节的数据\n", bytes_sent);

    // 关闭文件描述符
    close(source_fd);
    close(destination_fd);

    return 0;
}
