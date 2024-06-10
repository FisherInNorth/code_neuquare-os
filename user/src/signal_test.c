#define USER
#include "stddef.h"
#include "unistd.h"
#include "stdio.h"
#include "string.h"
#include "stdlib.h"

void signal(sig_t signo, __signalfn_t handler) {
    struct sigaction my_sig;
    my_sig.sa_handler = handler;
    rt_sigaction(signo, &my_sig, NULL, sizeof(sigset_t));
}

void sig_1(int sig) {
    printf("Received signal 1, exiting.\n");
    kill(getpid(), 20);
}

void sig_2(int sig) {
    printf("Received signal 2, exiting.\n");
    // kill(getpid(), 10);
    exit(0);
    kill(getpid(), 30);
}

void sig_3(int sig) {
    printf("Received signal 3, exiting.\n");

}

int main() {
    // print_pgtable();
    signal(10, sig_1);
    signal(20, sig_2);
    signal(30, sig_3);

    kill(getpid(), 10);
    // kill(getpid(), 20);
    
    return 0;
}