#define USER
#include "stddef.h"
#include "unistd.h"
#include "stdio.h"
#include "string.h"
#include "stdlib.h"

#define handle_error(msg) \
               do { printf("%s\n",msg); exit(-1); } while (0)

#define BUF_SIZE 4096

int main(int argc, char *argv[]) {
    int fd, nread;
    // char buf[BUF_SIZE];
    char *buf = (char*)malloc(BUF_SIZE);
    struct linux_dirent64 *d;
    int bpos;
    unsigned char d_type;
    
    // printf("hello ls; %d\n",argc);
    fd = open(argc > 1 ? argv[1] : ".", O_RDONLY | O_DIRECTORY);
    if (fd == -1) {
      handle_error("getdents");
    }

    for (;;) {
        nread = getdents(fd, (struct linux_dirent64 *)buf, BUF_SIZE); 
        if (nread == -1)
            handle_error("getdents");

        if (nread == 0)
            break;

        // printf("--------------- nread=%d ---------------\n", nread);
        // printf("inode#    file type  d_reclen  d_off   d_name\n");
        int ctr = 0;
        for (bpos = 0; bpos < nread;) {
            d = (struct linux_dirent64 *)(buf + bpos);
            d_type = d->d_type;
/*
            printf("inode: %d      ", d->d_ino);
            printf("dtype: %d      ", d_type);
            printf("%s   ", (d_type == S_IFREG)  ? "regular" :
                             (d_type == S_IFDIR)  ? "directory" :
                            //  (d_type == DT_FIFO) ? "FIFO" :
                            //  (d_type == DT_SOCK) ? "socket" :
                            //  (d_type == DT_LNK)  ? "symlink" :
                             (d_type == T_DEVICE)  ? "dev" :
                            //  (d_type == DT_CHR)  ? "char dev" :
                                                   "???");
            printf("%d %d %s \n", d->d_reclen,
                   d->d_off, d->d_name);
*/
            switch (d_type) {
            case DT_DIR: 
                printf("\033[34;1m%s\t\033[0m",d->d_name);
                break;
            case DT_CHR: case DT_BLK: 
                printf("\033[38;5;214m%s\t\033[0m",d->d_name);
                break;
            default:
                printf("%s\t",d->d_name);
                break;
            }
            
            bpos += d->d_reclen;
            if ( ++ctr == 6 ) {
              write(STDOUT,"\n",1);
              ctr = 0;
            }

            // break;
        }
        write(STDOUT,"\n",1);
        // write(1, argv[i], strlen(argv[i]));
    }
    free(buf);
    return 0;
    exit(0);
}