#include "common.h"
#include "param.h"
#include "memory/memlayout.h"
#include "lib/riscv.h"
#include "atomic/spinlock.h"
#include "proc/pcb_life.h"
#include "debug.h"
#include "memory/vm.h"
#include "kernel/trap.h"
#include "proc/pcb_mm.h"
#include "fs/vfs/ops.h"
#include "fs/fat/fat32_file.h"
#include "fs/fat/fat32_mem.h"
#include "proc/tcb_life.h"
#include "lib/auxv.h"
#include "lib/ctype.h"
#include "memory/vma.h"
#include "lib/elf.h"
#include "memory/binfmt.h"

static int map_interpreter(struct mm_struct *mm);
static int load_elf_interp(char *path);
static Elf64_Ehdr *load_elf_ehdr(struct binprm *bprm);
static Elf64_Phdr *load_elf_phdrs(const Elf64_Ehdr *elf_ex, struct inode *ip);
static int load_program(struct binprm *bprm, Elf64_Phdr *elf_phdata);
static uint64 START = 0;

struct interpreter ldso;
void print_ustack(pagetable_t pagetable, uint64 stacktop);
char *lmpath[] = {"//lmbench_all", "lmbench_all"};

#define AUX_CNT 38

int flags2perm(int flags) {
    int perm = 0;
    if (flags & 0x1)
        perm = PTE_X;
    if (flags & 0x2)
        perm |= PTE_W;
    if (flags & 0x4)
        perm |= PTE_R;
    return perm;
}

int flags2vmaperm(int flags) {
    int perm = 0;
    if (flags & 0x1)
        perm = PERM_EXEC;
    if (flags & 0x2)
        perm |= PERM_WRITE;
    if (flags & 0x4)
        perm |= PERM_READ;
    return perm;
}

#define ELF_PAGEOFFSET(_v) ((_v) & (PGSIZE - 1))
static int padzero(uint64 elf_bss) {
    uint64 nbyte;

    nbyte = ELF_PAGEOFFSET(elf_bss);
    if (nbyte) {
        nbyte = PGSIZE - nbyte;
        paddr_t pa = getphyaddr(ldso.mm->pagetable, elf_bss);
        memset((void *)pa, 0, nbyte);
    }
    return 0;
}

int clear_bss(uint64 elf_bss, uint64 last_bss) {
    if (padzero(elf_bss)) {
        return -1;
    }

    elf_bss = PGROUNDUP(elf_bss);
    last_bss = PGROUNDUP(last_bss);
    paddr_t pa;
    for (uint64 i = elf_bss; i < last_bss; i += PGSIZE) {
        pa = walkaddr(ldso.mm->pagetable, i);
        memset((void *)pa, 0, PGSIZE);
    }
    // after mapping contious phy mem, use these:
    // if (last_bss > elf_bss) {
    // paddr_t elf_bss_pa = getphyaddr(ldso.mm->pagetable, elf_bss);
    // paddr_t last_bss_pa = getphyaddr(ldso.mm->pagetable, last_bss - 1);
    // memset((void *)elf_bss_pa, 0, last_bss_pa - elf_bss_pa);
    // }

    return 0;
}

static int load_elf_interp(char *path) {
    // if (ldso.valid == 1) {
    //     clear_bss(ldso.elf_bss, ldso.last_bss);
    //     return 0;
    // }

    struct binprm bprm;
    Elf64_Ehdr *elf_ex;
    Elf64_Phdr *elf_phdata; /* ph poiner */
    struct inode *ip = NULL;

    struct mm_struct *mm;
    mm = ldso.mm = bprm.mm = alloc_mm();
    if (mm == NULL) {
        Warn("alloc_mm failed");
        goto bad;
    }

    if ((ip = namei(path)) == 0) {
        Warn("path not found!");
        goto bad;
    }
    bprm.ip = ip;

    ip->i_op->ilock(ip);
    if ((elf_ex = load_elf_ehdr(&bprm)) == NULL) {
        Warn("load_elf_ehdr failed");
        goto bad;
    }
    bprm.elf_ex = elf_ex;

    if ((elf_phdata = load_elf_phdrs(elf_ex, ip)) == NULL) {
        Warn("load_elf_phdr failed");
        goto bad;
    }

    if (load_program(&bprm, elf_phdata) < 0) {
        Warn("load_program failed");
        goto bad;
    }
    ldso.last_bss = bprm.last_bss;
    ldso.elf_bss = bprm.elf_bss;

    ip->i_op->iunlock_put(ip);
    ip = 0;
    ldso.size = bprm.size;
    ldso.entry = elf_ex->e_entry;
    ldso.valid = 1;
    // vmprint(ldso.pagetable, 1, 0, 0, 0);
    ldso.last_bss = bprm.last_bss;
    ldso.elf_bss = bprm.elf_bss;

    return 0;

bad:
    // TODO: error handler
    // proc_freepagetable(mm->pagetable, sz, 0);
    // free_mm(mm);
    ip->i_op->iunlock_put(ip);
    return -1;
}

static int map_interpreter(struct mm_struct *mm) {
    ASSERT(ldso.valid == 1);

    pagetable_t ldso_pagetable = ldso.mm->pagetable;
    pagetable_t src_pagetable = mm->pagetable;
    vaddr_t ldva = LDSO;
    paddr_t ldpa;
    pte_t *pte;
    int flags;
    for (vaddr_t i = 0; i < ldso.size; i += PGSIZE, ldva += PGSIZE) {
        walk(ldso_pagetable, i, 0, 0, &pte);
        flags = PTE_FLAGS(*pte);
        ldpa = PTE2PA(*pte);
        if (mappages(src_pagetable, ldva, PGSIZE, ldpa, flags, 0) < 0) {
            Warn("mappages failed");
            return -1;
        }
    }

    struct vma *pos;
    list_for_each_entry(pos, &(ldso.mm->head_vma), node) {
        if (vma_map(mm, pos->startva + LDSO, pos->size, pos->perm, VMA_INTERP) < 0) {
            return -1;
        }
    }

    return 0;
}

// Load a program segment into pagetable at virtual address va.
// va must be page-aligned
// and the pages from va to va+sz must already be mapped.
// Returns 0 on success, -1 on failure.
static int
loadseg(pagetable_t pagetable, uint64 va, struct inode *ip, uint offset, uint sz) {
    uint i, n;
    uint64 pa;

    for (i = 0; i < sz; i += PGSIZE) {
        pa = walkaddr(pagetable, va + i);
        if (pa == 0)
            panic("loadseg: address should exist");
        if (sz - i < PGSIZE)
            n = sz - i;
        else
            n = PGSIZE;
        // TODO, replace with elf_read
        if (fat32_inode_read(ip, 0, (uint64)pa, offset + i, n) != n)
            return -1;
    }

    return 0;
}

vaddr_t p_argc, p_envp, p_argv, p_auxv;
/* return argc, or -1 to indicate error */
static int ustack_init(struct proc *p, pagetable_t pagetable, struct binprm *bprm, char *const argv[], char *const envp[], int ustack_page) {
    paddr_t spp = getphyaddr(pagetable, USTACK + ustack_page * PGSIZE - 1) + 1;
    paddr_t stacktop = spp;
    paddr_t stackbase = spp - ustack_page * PGSIZE;
#define SPP2SP (USTACK + ustack_page * PGSIZE - (stacktop - spp)) /* only use this macro in this func! */
    // Log("%p %p", spp, stackbase);

    /*       Initial Process Stack (follows SYSTEM V ABI)
        +---------------------------+ <-- High Address
        |     Information block     |
        +---------------------------+
        |         Unspecified       |
        +---------------------------+
        |    Null aux vector entry  |
        +---------------------------+
        |  Auxiliary vector entries |
        +---------------------------+
        |            0              |
        +---------------------------+
        |           ...             |
        |        envp pointers      |
        |           ...             |
        +---------------------------+
        |            0              |
        +---------------------------+
        |           ...             |
        |        argv pointers      |
        |           ...             |
        +---------------------------+
        |           argc            |
        +---------------------------+ <-- Low Address
        |           ...             |
        +---------------------------+
    */

    uint64 argc = 0, envpc = 0;
    vaddr_t argv_addr[MAXARG], envp_addr[MAXENV];
    paddr_t cp;
    /* Information block */
    if (argv != 0) {
        if (bprm->sh == 1) {
            for (; argv[argc]; argc++) {
                cp = (paddr_t)argv[argc];
                spp -= strlen((const char *)cp) + 1;
                spp -= spp % 8;
                if (spp < stackbase)
                    return -1;
                memmove((void *)spp, (void *)cp, strlen((const char *)cp) + 1);
                argv_addr[argc] = SPP2SP;
            }
        } else {
            for (; argv[argc]; argc++) {
                if (argc >= MAXARG)
                    return -1;
                if ((cp = getphyaddr(p->mm->pagetable, (vaddr_t)argv[argc])) == 0) {
                    return -1;
                }
                spp -= strlen((const char *)cp) + 1;
                spp -= spp % 8;
                if (spp < stackbase)
                    return -1;
                memmove((void *)spp, (void *)cp, strlen((const char *)cp) + 1);
                argv_addr[argc] = SPP2SP;
            }
        }
    }
    argv_addr[argc] = 0;

    if (envp != 0) {
        for (; envp[envpc]; envpc++) {
            if (envpc >= MAXENV)
                return -1;
            if ((cp = getphyaddr(p->mm->pagetable, (vaddr_t)envp[envpc])) == 0) {
                return -1;
            }
            spp -= strlen((const char *)cp) + 1;
            spp -= spp % 8;
            if (spp < stackbase)
                return -1;
            memmove((void *)spp, (void *)cp, strlen((const char *)cp) + 1);
            envp_addr[envpc] = SPP2SP;
        }
    }
    envp_addr[envpc] = 0;

    ASSERT(spp % 8 == 0);
    // Log("%d %d", sp % 16, (uint64)(argc + envpc + 2 + 1) % 2);
    /* to make sp 16-bit aligned */
    if ((spp % 16 != 0 && (uint64)(argc + envpc + 1 + AUX_CNT) % 2 == 0)
        || ((spp % 16 == 0) && (uint64)(argc + envpc + 2 + 1) % 2 == 1)) {
        // Log("aligned");
        spp -= 8;
    }

    /* auxiliary vectors */
    uint64 auxv[AUX_CNT * 2] = {0};
    for (int i = 0; i < AUX_CNT; i++) {
        if (i + 1 > AT_RANDOM) {
            break;
        }
        // if (i == AT_EXECFN) {
        //     continue;
        // }
        auxv[i * 2] = i + 1;
    }
    auxv[AT_PAGESZ * 2 - 1] = PGSIZE;
    // if (bprm->interp) {
    if (bprm->interp || (strcmp(bprm->path, lmpath[0]) == 0)) {
        auxv[AT_BASE * 2 - 1] = LDSO;
#ifdef __DEBUG_LDSO__
        auxv[AT_PHDR * 2 - 1] = 0x20000000 + bprm->phvaddr;
#else
        if (strcmp(bprm->path, lmpath[0]) == 0) {
            auxv[AT_PHDR * 2 - 1] = bprm->elf_ex->e_phoff;
        } else {
            auxv[AT_PHDR * 2 - 1] = bprm->phvaddr;
        }
#endif
        auxv[AT_PHNUM * 2 - 1] = bprm->elf_ex->e_phnum;
        auxv[AT_PHENT * 2 - 1] = bprm->elf_ex->e_phentsize;
#ifdef __DEBUG_LDSO__
        auxv[AT_ENTRY * 2 - 1] = bprm->e_entry + 0x20000000;
#else
        auxv[AT_ENTRY * 2 - 1] = bprm->e_entry;
#endif
        // auxv[AT_EXECFN * 2 - 1] = 0;
    }
    // uint64 random[2] = {0xea0dad5a44586952, 0x5a1fa5497a4a283d};
    // memmove((void *)&auxv[AT_RANDOM * 2 - 1], random, 16);
    auxv[AT_RANDOM * 2 - 1] = SPP2SP;

    // char *s = "RISC-V64";
    // memmove((void *)&auxv[AT_PLATFORM * 2 - 1], (void *)s, sizeof(s));

    spp -= AUX_CNT * 16 + 8; /* reserved 8bits for null auxv entry */
    if (spp < stackbase) {
        return -1;
    }
    p_auxv = SPP2SP;
    memmove((void *)spp, (void *)auxv, AUX_CNT * 16);

    /* push the array of envp[] pointers */
    spp -= (envpc + 1) * sizeof(uint64);
    spp -= spp % 8;
    p_envp = SPP2SP;
    // Log("envp %p", sp);
    if (spp < stackbase)
        return -1;
    memmove((void *)spp, (void *)envp_addr, (envpc + 1) * sizeof(uint64));
    bprm->a2 = SPP2SP;

    /* push the array of argv[] pointers */
    spp -= (argc + 1) * sizeof(uint64);
    spp -= spp % 8;
    p_argv = SPP2SP;
    // Log("argv %p", sp);
    if (spp < stackbase)
        return -1;
    memmove((void *)spp, (void *)argv_addr, (argc + 1) * sizeof(uint64));
    bprm->a1 = SPP2SP;
    spp -= 8;
    p_argc = SPP2SP;
    memmove((void *)spp, (void *)&argc, sizeof(uint64));

    bprm->sp = SPP2SP; // initial stack pointer
    // Log("sp is %p", sp);
    // print_ustack(pagetable, USTACK + USTACK_PAGE * PGSIZE);
    return argc;
}

void print_ustack(pagetable_t pagetable, uint64 stacktop) {
    char *pa = (char *)getphyaddr(pagetable, stacktop - 1) + 1;
    // Log("pa is %p", pa);
    /* just print the first 100 8bits of the ustack */
    for (int i = 8; i < 150 * 8; i += 8) {
        if (i % 16 == 0) {
            printfGreen("aligned -> ");
        } else {
            printfGreen("           ");
        }

        if (stacktop - i == p_argc || stacktop - i == p_argv || stacktop - i == p_envp || stacktop - i == p_auxv) {
            printfBlue("%#p:", stacktop - i);
        } else {
            printfGreen("%#p:", stacktop - i);
        }

        for (int j = 0; j < 8; j++) {
            char c = (char)*(paddr_t *)(pa - i + j);
            if ((int)c > 0x20 && (int)c <= 0x7e) {
                printfGreen("%c", c);
            }
        }
        printf("\t");
        for (int j = 0; j < 8; j++) {
            printf("%02x ", (uint8) * (paddr_t *)(pa - i + j));
        }
        printf("\n");
    }
}

static int elf_read(struct inode *ip, void *buf, uint64 size, uint64 offset) {
    uint64 read;

    // ip->i_op->ilock(ip);
    read = ip->i_op->iread(ip, 0, (uint64)buf, offset, size);
    if (unlikely(read != size)) {
        ip->i_op->iunlock(ip);
        return -1;
    }
    return 0;
    // ip->i_op->iunlock(ip);
}

static Elf64_Ehdr *load_elf_ehdr(struct binprm *bprm) {
    struct inode *ip = bprm->ip;
    Elf64_Ehdr *elf_ex = kmalloc(sizeof(Elf64_Ehdr));

    if (elf_read(ip, elf_ex, sizeof(Elf64_Ehdr), 0) < 0) {
        goto out;
    }

    // CHECK
    if (memcmp(elf_ex->e_ident, ELFMAG, SELFMAG) != 0)
        goto out;
    if (elf_ex->e_type != ET_EXEC && elf_ex->e_type != ET_DYN)
        goto out;

    return elf_ex;
out:
    kfree(elf_ex);
    return NULL;
}

static Elf64_Phdr *load_elf_phdrs(const Elf64_Ehdr *elf_ex, struct inode *ip) {
    Elf64_Phdr *elf_phdata = NULL;
    int retval;
    unsigned int size;

    if (elf_ex->e_phentsize != sizeof(Elf64_Phdr))
        return NULL;

    /* Sanity check the number of program headers... */
    /* ...and their total size. */
    size = sizeof(Elf64_Phdr) * elf_ex->e_phnum;
    if (size == 0 || size > PGSIZE)
        return NULL;

    elf_phdata = kmalloc(size);
    if (!elf_phdata) {
        Warn("load_elf_phdrs: no free mem");
        return NULL;
    }

    /* Read in the program headers */
    retval = elf_read(ip, elf_phdata, size, elf_ex->e_phoff);
    if (retval < 0) {
        goto out;
    }

    return elf_phdata;

out:
    kfree(elf_phdata);
    return NULL;
}

/* support misaligned va load */
static int load_program(struct binprm *bprm, Elf64_Phdr *elf_phdata) {
    Elf64_Ehdr *elf_ex = bprm->elf_ex;
    Elf64_Phdr *elf_phpnt = elf_phdata;
    struct mm_struct *mm = bprm->mm;
    struct inode *ip = bprm->ip;
    uint64 sz = 0;
    uint64 last_bss = 0, elf_bss = 0;
    // int bss_prot;

    for (int i = 0; i < elf_ex->e_phnum; i++, elf_phpnt++) {
        if (elf_phpnt->p_type != PT_LOAD)
            continue;
        if (elf_phpnt->p_memsz < elf_phpnt->p_filesz)
            return -1;
        if (elf_phpnt->p_vaddr + elf_phpnt->p_memsz < elf_phpnt->p_vaddr)
            return -1;

        /* offset: start offset in misaligned page
           size: read size of misaligned page
        */
        uint64 offset = 0, size = 0;
        vaddr_t vaddrdown = PGROUNDDOWN(elf_phpnt->p_vaddr);
        if (elf_phpnt->p_vaddr % PGSIZE != 0) {
            // Warn("%p", vaddrdown);
            paddr_t pa = (paddr_t)kmalloc(PGSIZE);
#ifdef __DEBUG_LDSO__
            if (mappages(mm->pagetable, START + vaddrdown, PGSIZE, pa, flags2perm(elf_phpnt->p_flags) | PTE_U, COMMONPAGE) < 0) {
#else
            if (mappages(mm->pagetable, vaddrdown, PGSIZE, pa, flags2perm(elf_phpnt->p_flags) | PTE_U, COMMONPAGE) < 0) {
#endif
                Warn("misaligned load mappages failed");
                kfree((void *)pa);
                return -1;
            }
            offset = elf_phpnt->p_vaddr - vaddrdown;
            size = PGROUNDUP(elf_phpnt->p_vaddr) - elf_phpnt->p_vaddr;
            if (ip->i_op->iread(ip, 0, (uint64)pa + offset, elf_phpnt->p_offset, size) != size) {
                return -1;
            }
            // Log("entry is %p", elf.entry);
        } else {
            ASSERT(vaddrdown == elf_phpnt->p_vaddr);
        }
        uint64 sz1;
        ASSERT((elf_phpnt->p_vaddr + size) % PGSIZE == 0);
        // Log("\nstart end:%p %p", ph.vaddr + size, ph.vaddr + ph.memsz);
#ifdef __DEBUG_LDSO__
        if ((sz1 = uvmalloc(mm->pagetable, START + elf_phpnt->p_vaddr + size, START + elf_phpnt->p_vaddr + elf_phpnt->p_memsz, flags2perm(elf_phpnt->p_flags))) == 0)
#else
        if ((sz1 = uvmalloc(mm->pagetable, elf_phpnt->p_vaddr + size, elf_phpnt->p_vaddr + elf_phpnt->p_memsz, flags2perm(elf_phpnt->p_flags))) == 0)
#endif
            return -1;
        // vmprint(mm->pagetable, 1, 0, 0, 0);
        sz = sz1;
#ifdef __DEBUG_LDSO__
        if (loadseg(mm->pagetable, elf_phpnt->p_vaddr + size + START, ip, elf_phpnt->p_offset + size, elf_phpnt->p_filesz - size) < 0)
            return -1;
#else
        if (loadseg(mm->pagetable, elf_phpnt->p_vaddr + size, ip, elf_phpnt->p_offset + size, elf_phpnt->p_filesz - size) < 0)
            return -1;
#endif

        vaddr_t vaddrup = PGROUNDUP(elf_phpnt->p_vaddr + elf_phpnt->p_memsz);
#ifdef __DEBUG_LDSO__
        if (vma_map(mm, START + vaddrdown, vaddrup - vaddrdown, flags2vmaperm(elf_phpnt->p_flags), VMA_TEXT) < 0) {
#else
        if (vma_map(mm, vaddrdown, vaddrup - vaddrdown, flags2vmaperm(elf_phpnt->p_flags), VMA_TEXT) < 0) {
#endif
            return -1;
        }

        uint64 tmp;
        /*
         * Find the end of the file mapping for this phdr, and
         * keep track of the largest address we see for this.
         */
        tmp = elf_phpnt->p_vaddr + elf_phpnt->p_filesz;
        if (tmp > elf_bss)
            elf_bss = tmp;

        /*
         * Do the same thing for the memory mapping - between
         * elf_bss and last_bss is the bss section.
         */
        tmp = elf_phpnt->p_vaddr + elf_phpnt->p_memsz;
        if (tmp > last_bss) {
            last_bss = tmp;
            // bss_prot = flags2perm(elf_phpnt->p_flags);
        }
    }

    bprm->last_bss = last_bss;
    bprm->elf_bss = elf_bss;
    bprm->size = sz;
    // Log("load successfully!");
    return 0;
}

/* if load successfully, return 0, or return -1 to indicate an error */
static int load_elf_binary(struct binprm *bprm) {
    Elf64_Ehdr *elf_ex = NULL;
    Elf64_Phdr *elf_phdata = NULL, *elf_phpnt; /* ph poiner */
    struct inode *ip = bprm->ip;

    /* lock ip at the start of load_elf_binary, and free it at the end */
    ip->i_op->ilock(bprm->ip);

    if ((elf_ex = load_elf_ehdr(bprm)) == NULL) {
        Warn("load_elf_ehdr failed");
        goto bad;
    }
    bprm->elf_ex = elf_ex;

    if ((elf_phdata = load_elf_phdrs(elf_ex, bprm->ip)) == NULL) {
        Warn("load_elf_phdr failed");
        goto bad;
    }
    bprm->phvaddr = elf_phdata->p_vaddr;

    if (strcmp(bprm->path, lmpath[0]) == 0 || strcmp(bprm->path, lmpath[1]) == 0) {
        void *pa = kzalloc(PGSIZE);
        memmove((void *)pa + elf_ex->e_phoff, (void *)elf_phdata, elf_ex->e_phnum * elf_ex->e_phentsize);
        mappages(bprm->mm->pagetable, 0, PGSIZE, (paddr_t)pa, PTE_U | PTE_R | PTE_W, 0);
        if (vma_map(bprm->mm, 0, PGSIZE, PERM_READ | PERM_WRITE, VMA_TEXT) < 0) {
            return -1;
        }
    }

    if (load_program(bprm, elf_phdata) < 0) {
        goto bad;
    }

#ifdef __DEBUG_LDSO__
    START = 0;
#endif
    elf_phpnt = elf_phdata;
    for (int i = 0; i < elf_ex->e_phnum; i++, elf_phpnt++) {
        if (elf_phpnt->p_type != PT_INTERP)
            continue;
        load_elf_interp("/libc.so");
        map_interpreter(bprm->mm);
        bprm->interp = 1;
        break;
    }

    /* only free elf_phdata, we still use elf_ex in ustack_init */
    kfree(elf_phdata);

    /* unlock ip */
    ip->i_op->iunlock_put(ip);
    ip = 0;
    bprm->e_entry = elf_ex->e_entry + START;
    return 0;

bad:
    if (elf_ex != NULL)
        kfree(elf_ex);
    if (elf_phdata != NULL)
        kfree(elf_phdata);
    bprm->ip->i_op->iunlock_put(bprm->ip);
    return -1;
}

int do_execve(char *path, struct binprm *bprm) {
    // struct commit commit;
    // memset(&commit, 0, sizeof(commit));
    struct proc *p = proc_current();
    struct tcb *t = thread_current();
    vaddr_t brk; /* program_break */
    bprm->path = path;

    struct mm_struct *mm, *oldmm = p->mm;

#ifdef __DEBUG_LDSO__
    if (strcmp(path, "/busybox/busybox_d") == 0) {
        START = 0x20000000;
    }
    if (strcmp(path, "entry-dynamic.exe") == 0) {
        START = 0x20000000;
    }
#endif
    bprm->ip = namei(path);
    if (bprm->ip == 0) {
        return -1;
    }

    mm = alloc_mm();
    if (mm == NULL) {
        return -1;
    }
    bprm->mm = mm;

    /* Loader */
    if (load_elf_binary(bprm) < 0) {
        goto bad;
    }

    /* Heap Initialization */
    brk = PGROUNDUP(bprm->size);
    mm->start_brk = mm->brk = brk;
    mm->heapvma = NULL;

    /* Stack Initialization */
    int ustack_page = USTACK_PAGE;
    if (bprm->stack_limit == 1) {
        ustack_page = 2;
    }
    if (uvm_thread_stack(mm->pagetable, ustack_page) < 0) {
        Warn("bad");
        goto bad;
    }
    if (vma_map(mm, USTACK, ustack_page * PGSIZE, PERM_READ | PERM_WRITE, VMA_STACK) < 0) {
        goto bad;
    }

    int argc = ustack_init(p, mm->pagetable, bprm, bprm->argv, bprm->envp, ustack_page);
    if (argc < 0) {
        goto bad;
    }

    /* Save program name for debugging */
    char *s, *last;
    for (last = s = path; *s; s++)
        if (*s == '/')
            last = s + 1;
    safestrcpy(p->name, last, MIN(sizeof(p->name), 19));

    // thread name
    char name_tmp[20];
    snprintf(name_tmp, 20, "%s-%d", p->name, p->tg->group_leader->tidx);
    strncpy(p->tg->group_leader->name, name_tmp, 20);
    safestrcpy(p->name, last, MIN(sizeof(p->name), 19));

    /* Commit to the user image */
    t->trapframe->sp = bprm->sp;
    t->trapframe->a1 = bprm->a1;
    t->trapframe->a2 = bprm->a2;
    if (bprm->interp) {
        t->trapframe->epc = ldso.entry + LDSO;
    } else {
        t->trapframe->epc = bprm->e_entry;
    }

    // If  any  of the threads in a thread group performs an execve(2), then all threads other than the thread
    // group leader are terminated, and the new program is executed in the thread group leader.
    // TODO
    if (mappages(mm->pagetable, TRAPFRAME - 0 * PGSIZE, PGSIZE, (uint64)(t->trapframe), PTE_R | PTE_W, 0) < 0) {
        Warn("map failed!");
        // error handler TODO
    }
    // uvm_thread_trapframe(mm->pagetable, 0);

    /* free the old pagetable */
    free_mm(oldmm, atomic_read(&p->tg->thread_cnt));

    /* commit new mm */
    p->mm = mm;

    kfree(bprm->elf_ex);

    if (bprm->interp) {
        // Log("entry is %p", t->trapframe->epc);
    }
    /* for debug, print the pagetable and vmas after exec */
    // if (strcmp(path, "entry-static.exe") == 0) {
    // vmprint(p->mm->pagetable, 1, 0, 0x2f000, 0x30000, 0);
    // }
    // print_vma(&mm->head_vma);
    // panic(0);

    // printfGreen("mm: %d pages\n", get_free_mem()/4096);
    return argc; // this ends up in a0, the first argument to main(argc, argv)

bad:
    // TODO
    // Note: cnt is 1!
    free_mm(mm, 0);
    // todo
    // kfree(ex);
    return -1;
}
