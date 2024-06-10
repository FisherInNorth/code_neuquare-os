#ifndef __STDLIB_H__
#define __STDLIB_H__

#include "stddef.h"

void panic(char *);

#define WEXITSTATUS(s) (((s) & 0xff00) >> 8)

#ifndef assert
#define assert(f) \
    if (!(f))     \
	panic("\n --- Assert Fatal ! ---\n")
#endif

/* add */
void* malloc(uint);
void free(void*);

struct stat {
    int dev;     // File system's disk device
    uint ino;    // Inode number
    short type;  // Type of file
    short nlink; // Number of links to file
    uint64 size; // Size of file in bytes
};

int xv6_stat(const char*, struct stat*);

#endif //__STDLIB_H__
