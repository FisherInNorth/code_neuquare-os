#define USER
#include "stddef.h"
#include "unistd.h"
#include "stdio.h"
#include "string.h"


int main(int argc, char *argv[])
{
    const char* oldpath = "oscomp/text.txt";
    const char* newpath = "text.txt";

    // 打开源文件
    // int olddirfd, newdirfd;
    // if ( (olddirfd = open("oscomp", O_RDONLY)) < 0) {
    //     perror("无法打开源文件");
    //     return 1;
    // }
    // if ( (newdirfd = open("/", O_RDONLY)) < 0) {
    //     perror("无法打开源文件");
    //     return 1;
    // }
    if ( renameat2(AT_FDCWD, oldpath, AT_FDCWD, newpath, 0) < 0) {
        perror("renameat2 失败！");
        return 1;
    } else {
        printf("success.\n");
        printf("old path: %s\n", oldpath);
        printf("new path: %s\n", newpath);
    }

    int fd;
    if ( (fd = open(newpath, O_RDONLY)) < 0) {
        perror("无法打开新文件");
        return 1; 
    }
    char buf[200];
    read(fd, buf, sizeof(buf));
    write(STDOUT_FILENO,buf,sizeof(buf));

    return 0;
}
