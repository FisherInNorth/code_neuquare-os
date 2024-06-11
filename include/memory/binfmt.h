#ifndef __BINFMT_H__
#define __BINFMT_H__

#include "common.h"
#include "lib/elf.h"

struct mm;
struct inode;

/*
 * This structure is used to hold the arguments that are used when loading binaries.
 */
struct binprm {
    const char *path;
    int argc, envpc;
    char **argv, **envp;
    struct inode *ip;
    struct mm_struct *mm;
    uint64 sp;
    uint64 size; /* current top of program(program break) */

    /* if define __DEBUG_LDSO__, the e_entry is different from elf_ex->e_entry
     * so there is need to add this field
     */
    uint64 e_entry;

    /* interpreter */
    int interp;

    /* sh interp*/
    int sh;

    Elf64_Ehdr *elf_ex;
    uint64 phvaddr;
    // Elf64_Phdr *elf_phdata;

    // uint64 e_phnu
    // uint64 e_phoff; /* AT_PHDR = e_entry + e_phoff; */
    uint64 last_bss, elf_bss;

    /* reserve for xv6_user program */
    uint64 a1, a2;

    int stack_limit;
};

struct interpreter {
    struct mm_struct *mm;
    uint64 size;
    uint64 entry;

    uint64 elf_bss, last_bss;
    int valid;
    // may need spinlock
};

int do_execve(char *path, struct binprm *bprm);
#endif // __BINFMT_H__