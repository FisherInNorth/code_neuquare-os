#ifndef __VFS_OPS_H__
#define __VFS_OPS_H__

#include "fs.h"

// file layer
struct file *filealloc(fs_t);
void generic_fileclose(struct file *);
extern const struct file_operations *(*get_fileops[])(void);

// pathname layer
struct inode *namei(char *path);
struct inode *namei_parent(char *path, char *name);

// inode layer
unsigned char __IMODE_TO_DTYPE(uint16 mode);
extern const struct inode_operations *(*get_inodeops[])(void);

#endif // __VFS_OPS_H__