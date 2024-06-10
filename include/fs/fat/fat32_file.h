#ifndef __FAT32_FILE_H__
#define __FAT32_FILE_H__

#include "common.h"

extern struct devsw devsw[];

// 1. duplicate the file
struct file *fat32_filedup(struct file *);

// 2. read the file
ssize_t fat32_fileread(struct file *, uint64, int n);

// 3. return the state of file
int fat32_filestat(struct file *, uint64 addr);

// 4. write the file
ssize_t fat32_filewrite(struct file *, uint64, int n);

// 5. current working directory
void fat32_getcwd(char *buf);
void get_absolute_path(struct inode *ip, char *kbuf);
size_t fat32_getdents(struct inode *dp, char *buf, uint32 off, size_t len);
// size_t fat32_getdents(struct file *f, char *buf, size_t len);

#endif