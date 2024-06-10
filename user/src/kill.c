#define USER
#include "stddef.h"
#include "unistd.h"
#include "stdio.h"
#include "string.h"
#include "stddef.h"

int main(int argc, char **argv)
{
  if(argc < 2){
    fprintf(2, "usage: kill <pid>\n");
    exit(1);
  }
  for(int i=1; i<argc; i++)
    kill(atoi(argv[i]), SIGKILL);
  return 0;
}
