#define USER
#include "stddef.h"
#include "unistd.h"
#include "stdio.h"
#include "string.h"

// struct iovec {
//     void  *iov_base;    /* Starting address */
//     size_t iov_len;     /* Number of bytes to transfer */
// };
int main(int argc, char *argv[])
{
    char *str0 = "hello ";
    char *str1 = "world\n";
    struct iovec iov[2];
    ssize_t nwritten;

    iov[0].iov_base = str0;
    iov[0].iov_len = strlen(str0);
    iov[1].iov_base = str1;
    iov[1].iov_len = strlen(str1);

    nwritten = writev(1, (const struct iovec*)iov, 2);
    printf("%d\n",nwritten);

    

    return 0;
}
