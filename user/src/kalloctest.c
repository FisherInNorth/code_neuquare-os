#define USER
#include "stddef.h"
#include "unistd.h"
#include "stdio.h"
#include "string.h"
// #include "memory/memlayout.h"
// #include "memory/buddy.h"

#define NCHILD 2
#define N 100000
#define SZ 4096

// void test1(void);
void test2(void);
void test3(void);
char buf[SZ];

int
main(int argc, char *argv[])
{
//   test1();
  test2();
  test3();
  exit(0);
  return 0;
}

// int ntas(int print)
// {
//   int n;
//   char *c;

//   if (statistics(buf, SZ) <= 0) {
//     fprintf(2, "ntas: no stats\n");
//   }
//   c = strchr(buf, '=');
//   n = atoi(c+2);
//   if(print)
//     printf("%s", buf);
//   return n;
// }

// // Test concurrent kallocs and kfrees
// void test1(void)
// {
//   void *a, *a1;
//   int n, m;
//   printf("start test1\n");  
//   m = ntas(0);
//   for(int i = 0; i < NCHILD; i++){
//     int pid = fork();
//     if(pid < 0){
//       printf("fork failed");
//       exit(-1);
//     }
//     if(pid == 0){
//       for(i = 0; i < N; i++) {
//         a = sbrk(4096);
//         *(int *)(a+4) = 1;
//         a1 = sbrk(-4096);
//         if (a1 != a + 4096) {
//           printf("wrong sbrk\n");
//           exit(-1);
//         }
//       }
//       exit(-1);
//     }
//   }

//   for(int i = 0; i < NCHILD; i++){
//     wait(0);
//   }
//   printf("test1 results:\n");
//   n = ntas(1);
//   if(n-m < 10) 
//     printf("test1 OK\n");
//   else
//     printf("test1 FAIL\n");
// }

//
// countfree() from usertests.c
//
int
countfree()
{
  uint64 sz0 = (uint64)sbrk(0);
  int n = 0;

  while(1){
    uint64 a = (uint64) sbrk(4096);
    if(a == 0xffffffffffffffff){
      break;
    }
    // modify the memory to make sure it's really allocated.
    *(char *)(a + 4096 - 1) = 1;
    n += 1;
  }
  sbrk(-((uint64)sbrk(0) - sz0));
  return n;
}

#define PHYSTOP (0x80000000L + 130 * 1024 * 1024)
#define START_MEM 0x80a00000
#define PGSIZE 4096 // bytes per page
// Test stealing
void test2() {
  int free0 = countfree();
  int free1;
  int n = (PHYSTOP-START_MEM)/PGSIZE;
  printf("start test2\n");  
  printf("total free number of pages: %dMB (out of %dMB)\n", free0 / 256, n / 256);
  if(n - free0 > 1000) {
    printf("test2 FAILED: cannot allocate enough memory");
    exit(-1);
  }
  for (int i = 0; i < 50; i++) {
    free1 = countfree();
    if(i % 10 == 9)
      printf(".");
    if(free1 != free0) {
      printf("test2 FAIL: losing pages\n");
      exit(-1);
    }
  }
  printf("\ntest2 OK\n");  
}

// Test concurrent kalloc/kfree and stealing
void test3(void)
{
  int free0 = countfree();
  printf("cur free number %d", free0 / 256);
  void *a, *a1;
  printf("start test3\n");  
  for(int i = 0; i < NCHILD; i++){
    int pid = fork();
    if(pid < 0){
      printf("fork failed");
      exit(-1);
    }
    if(pid == 0){
      if (i == 0) {
        for(i = 0; i < N; i++) {
          a = sbrk(4096);
          if ((uint64)a == -1) {
            break;
          }
          *(int *)(a+4) = 1;
          a1 = sbrk(-4096);
          if (a1 != a + 4096) {
            printf("wrong sbrk\n");
            exit(-1);
          }
        }
        printf("child done %d\n", i);
        exit(0);
      } else {
        countfree();
        printf("child done %d\n", i);
        exit(0);
      }
    }
  }

  for(int i = 0; i < NCHILD; i++){
    wait(0);
  }
  printf("test3 OK\n");
}
