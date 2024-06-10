#ifndef __STRING_H__
#define __STRING_H__

#include "stddef.h"

int isspace(int c);
int isdigit(int c);
int atoi(const char *s);
void *memset(void *dest, int c, size_t n);
int strcmp(const char *l, const char *r);
size_t strlen(const char *);
size_t strnlen(const char *s, size_t n);
char *strncpy(char *restrict d, const char *restrict s, size_t n);
int strncmp(const char *_l, const char *_r, size_t n);

/* add */
char* strchr(const char*, char c);
char* strcpy(char*, const char*);
void *memmove(void*, const void*, int);
int memcmp(const void *, const void *, uint);
void *memcpy(void *, const void *, uint);
char* strcat(char* dest, const char* src);

#endif // __STRING_H__
