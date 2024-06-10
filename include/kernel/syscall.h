#ifndef __SYSCALL_H__
#define __SYSCALL_H__
#include "common.h"

struct file;
// syscall.c
uint64 argraw(int);
int argint(int, int *);
void argulong(int, unsigned long *);
int arguint(int n, uint *ip);
int arglong(int, long *);
int argfd(int n, int *pfd, struct file **pf);
int argstr(int, char *, int);
int argaddr(int, uint64 *);
int arglist(uint64 argv[], int s, int n); // do not use
// int arglong(int, uint64 *);
int fetchstr(uint64, char *, int);
int fetchaddr(uint64, uint64 *);
void syscall_count_analysis();
void syscall();


#endif