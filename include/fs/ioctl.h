#ifndef __IOCTL_H__
#define __IOCTL_H__

struct file;
int vfs_ioctl(struct file *, unsigned int, unsigned int, unsigned long);

#endif // __IOCTL_H__