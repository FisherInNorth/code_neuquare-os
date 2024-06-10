#include "kernel/plic.h"
#include "kernel/trap.h"
#include "kernel/cpu.h"
#include "memory/vm.h"
#include "memory/memlayout.h"
#include "proc/pcb_life.h"
#include "ipc/signal.h"
#include "proc/sched.h"
#include "lib/riscv.h"
#include "lib/sbi.h"
#include "driver/uart.h"
#include "driver/disk.h"
#include "atomic/spinlock.h"
#include "atomic/cond.h"
#include "atomic/semaphore.h"
#include "kernel/trap.h"
#include "lib/queue.h"
#include "common.h"
#include "param.h"
#include "debug.h"
#include "lib/timer.h"
#include "kernel/syscall.h"
#include "memory/pagefault.h"

int print_tf_flag;

// in kernelvec.S, calls kerneltrap().
void kernelvec();
extern int devintr();
extern char trampoline[], uservec[], userret[];

#define ISPAGEFAULT(cause) ((cause) == INSTUCTION_PAGEFAULT || (cause) == LOAD_PAGEFAULT || (cause) == STORE_PAGEFAULT)

static char *cause[16] = {
    [2] "ILLEGAL INSTRUCTION",
    [INSTUCTION_PAGEFAULT] "INSTRUCTION PAGEFAULT",
    [STORE_PAGEFAULT] "STORE/AMO PAGEFAULT",
    [LOAD_PAGEFAULT] "LOAD PAGEFAULT",
};

// set up to take exceptions and traps while in the kernel.
void trapinithart(void) {
    w_stvec((uint64)kernelvec);
    w_sie(r_sie() | SIE_SEIE | SIE_STIE | SIE_SSIE);
    // uint64 time = rdtime();
    // printf("time = %d\n",time);
    SET_TIMER();
    Info("cpu %d, timer is enable !!!\n", cpuid());
}

void tf_flstore(struct trapframe *self) {
    __asm__ __volatile__(
        "fsd f0,296(%0)\n\t"
        "fsd f1,304(%0)\n\t"
        "fsd f2,312(%0)\n\t"
        "fsd f3,320(%0)\n\t"
        "fsd f4,328(%0)\n\t"
        "fsd f5,336(%0)\n\t"
        "fsd f6,344(%0)\n\t"
        "fsd f7,352(%0)\n\t"
        "fsd f8,360(%0)\n\t"
        "fsd f9,368(%0)\n\t"
        "fsd f10,376(%0)\n\t"
        "fsd f11,384(%0)\n\t"
        "fsd f12,392(%0)\n\t"
        "fsd f13,400(%0)\n\t"
        "fsd f14,408(%0)\n\t"
        "fsd f15,416(%0)\n\t"
        "fsd f16,424(%0)\n\t"
        "fsd f17,432(%0)\n\t"
        "fsd f18,440(%0)\n\t"
        "fsd f19,448(%0)\n\t"
        "fsd f20,456(%0)\n\t"
        "fsd f21,464(%0)\n\t"
        "fsd f22,472(%0)\n\t"
        "fsd f23,480(%0)\n\t"
        "fsd f24,488(%0)\n\t"
        "fsd f25,496(%0)\n\t"
        "fsd f26,504(%0)\n\t"
        "fsd f27,512(%0)\n\t"
        "fsd f28,520(%0)\n\t"
        "fsd f29,528(%0)\n\t"
        "fsd f30,536(%0)\n\t"
        "fsd f31,544(%0)\n\t"
        "frcsr t0\n\t"
        "sd t0,552(%0)\n\t"
        :
        : "r"(self)
        : "t0");
}

void tf_flrestore(struct trapframe *self) {
    __asm__ __volatile__(
        "fld f0,296(%0)\n\t"
        "fld f1,304(%0)\n\t"
        "fld f2,312(%0)\n\t"
        "fld f3,320(%0)\n\t"
        "fld f4,328(%0)\n\t"
        "fld f5,336(%0)\n\t"
        "fld f6,344(%0)\n\t"
        "fld f7,352(%0)\n\t"
        "fld f8,360(%0)\n\t"
        "fld f9,368(%0)\n\t"
        "fld f10,376(%0)\n\t"
        "fld f11,384(%0)\n\t"
        "fld f12,392(%0)\n\t"
        "fld f13,400(%0)\n\t"
        "fld f14,408(%0)\n\t"
        "fld f15,416(%0)\n\t"
        "fld f16,424(%0)\n\t"
        "fld f17,432(%0)\n\t"
        "fld f18,440(%0)\n\t"
        "fld f19,448(%0)\n\t"
        "fld f20,456(%0)\n\t"
        "fld f21,464(%0)\n\t"
        "fld f22,472(%0)\n\t"
        "fld f23,480(%0)\n\t"
        "fld f24,488(%0)\n\t"
        "fld f25,496(%0)\n\t"
        "fld f26,504(%0)\n\t"
        "fld f27,512(%0)\n\t"
        "fld f28,520(%0)\n\t"
        "fld f29,528(%0)\n\t"
        "fld f30,536(%0)\n\t"
        "fld f31,544(%0)\n\t"
        "ld t0,552(%0)\n\t"
        "fscsr t0\n\t"
        :
        : "r"(self)
        : "t0");
}

void killproc(struct proc *p) {
    printf("usertrap(): process name: %s pid: %d\n", p->name, p->pid);
    printf("scause %p %s\n", r_scause(), cause[r_scause()]);
    printf("sepc=%p\n", r_sepc());
    printf("stval=%p\n", r_stval());
    proc_sendsignal_all_thread(p, SIGKILL, 1);
}

//
// handle an interrupt, exception, or system call from user space.
// called from trampoline.S
//
void thread_usertrap(void) {
    int which_dev = 0;

    tf_flstore(thread_current()->trapframe);
    if ((r_sstatus() & SSTATUS_SPP) != 0) {
        trapframe_print(thread_current()->trapframe);
        panic("usertrap: not from user mode");
    }

    // send interrupts and exceptions to kerneltrap(),
    // since we're now in the kernel.
    w_stvec((uint64)kernelvec);

    struct proc *p = proc_current();
    struct tcb *t = thread_current();

    // p->last_in = rdtime();
    // p->utime += rdtime() - p->last_out;

    // save user program counter.
    p->utime += rdtime() - p->stub_time;
    t->trapframe->epc = r_sepc();

    uint64 cause = r_scause();
    if (cause == SYSCALL) { /* user-mode ecall ~ 8 */
        // system call

        if (thread_killed(t))
            do_exit(-1);

        // sepc points to the ecall instruction,
        // but we want to return to the next instruction.
        t->trapframe->epc += 4;

        p->stub_time = rdtime();
        // an interrupt will change sepc, scause, and sstatus,
        // so enable only now that we're done with those registers.
        intr_on();

        syscall();
        p->stime += rdtime() - p->stub_time;
    } else if ((which_dev = devintr()) != 0) {
        // ok
    } else {
        // if (thread_current()->trapframe->epc == 0x2f418) {
        //     struct proc *p = thread_current()->p;
        //     vaddr_t va = walkaddr(p->mm->pagetable, 0x33786);
        //     vmprint(p->mm->pagetable, 1, 0, 0x2f000, 0x30000, 0);
        //     Log("hit %d", va);
        // }
        // if (r_stval() < 0x35000 && r_stval() > 0x30000) {
        //     Log("hit: %d", r_scause());
        // }
        if (ISPAGEFAULT(cause)) {
            if (pagefault(cause, p->mm->pagetable, r_stval()) < 0) {
                killproc(p);
            }
        } else {
            killproc(p);
        }
    }

    if (proc_killed(p))
        do_exit(-1);

    // if (thread_killed(t))
    //     do_exit(-1);

    // give up the CPU if this is a timer interrupt.
    if (which_dev == 2)
        thread_yield();

    // handle the signal
    signal_handle(t);

    thread_usertrapret();
}

//
// return to user space
//
void thread_usertrapret() {
    struct proc *p = proc_current();
    struct tcb *t = thread_current();
    // we're about to switch the destination of traps from
    // kerneltrap() to usertrap(), so turn off interrupts until
    // we're back in user space, where usertrap() is correct.
    intr_off();
    p->stub_time = rdtime();

    // p->last_out = rdtime();
    // p->stime += rdtime() - p->last_in;

    // send syscalls, interrupts, and exceptions to uservec in trampoline.S
    uint64 trampoline_uservec = TRAMPOLINE + (uservec - trampoline);
    w_stvec(trampoline_uservec);

    // set up trapframe values that uservec will need when
    // the process next traps into the kernel.

    t->trapframe->kernel_satp = r_satp();         // kernel page table
    t->trapframe->kernel_sp = t->kstack + KSTACK_PAGE * PGSIZE; // process's kernel stack
    t->trapframe->kernel_trap = (uint64)thread_usertrap;
    t->trapframe->hartid = r_tp(); // hartid for cpuid()

    // trapframe_print(t->trapframe);// debug

    // if (print_tf_flag) {
    //     printf("%d\n", p->pid);
    //     trapframe_print(t->trapframe);
    //     print_tf_flag = 0;
    // }

    // set up the registers that trampoline.S's sret will use
    // to get to user space.

    // set S Previous Privilege mode to User.
    unsigned long x = r_sstatus();
    x &= ~SSTATUS_SPP; // clear SPP to 0 for user mode
    x |= SSTATUS_SPIE; // enable interrupts in user mode
    w_sstatus(x);
    tf_flrestore(t->trapframe);

    // set S Exception Program Counter to the saved user pc.
    w_sepc(t->trapframe->epc);

    // tell trampoline.S the user page table to switch to.
    uint64 satp = MAKE_SATP(p->mm->pagetable);

    // write thread idx into sscratch
    w_sscratch(t->tidx);

    // jump to userret in trampoline.S at the top of memory, which
    // switches to the user page table, restores user registers,
    // and switches to user mode with sret.
    uint64 trampoline_userret = TRAMPOLINE + (userret - trampoline);
    ((void (*)(uint64))trampoline_userret)(satp);
}

// interrupts and exceptions from kernel code go here via kernelvec,
// on whatever the current kernel stack is.
void kerneltrap() {
    int which_dev = 0;
    uint64 sepc = r_sepc();
    uint64 sstatus = r_sstatus();
    uint64 scause = r_scause();

    if ((sstatus & SSTATUS_SPP) == 0)
        panic("kerneltrap: not from supervisor mode");
    if (intr_get() != 0)
        panic("kerneltrap: interrupts enabled");

    if ((which_dev = devintr()) == 0) {
        printf("scause %p\n", scause);
        printf("sepc=%p stval=%p\n", r_sepc(), r_stval());
        panic("kerneltrap");
    }

    // give up the CPU if this is a timer interrupt.
    if (which_dev == 2 && thread_current() != 0 && thread_current()->state == TCB_RUNNING)
        thread_yield();

    // the yield() may have caused some traps to occur,
    // so restore trap registers for use by kernelvec.S's sepc instruction.
    w_sepc(sepc);
    w_sstatus(sstatus);
}

// check if it's an external interrupt or software interrupt,
// and handle it.
// returns 2 if timer interrupt,
// 1 if other device,
// 0 if not recognized.

int devintr() {
    uint64 scause = r_scause();

    if ((scause & 0x8000000000000000L) && (scause & 0xff) == 9) {
        // this is a supervisor external interrupt, via PLIC.
        // irq indicates which device interrupted.
        int irq = plic_claim();

        if (irq == UART0_IRQ) {
            uartintr();
        }
#if defined(SIFIVE_U) || defined(SIFIVE_B)
        // TODO()
        if (irq >= DMA_IRQ_START && irq <= DMA_IRQ_END) {
            dma_intr(irq);
        }
        // else if (irq == VIRTIO0_IRQ) {
        //     disk_intr();
        // }
#else
        else if (irq == VIRTIO0_IRQ) {
            disk_intr();
        }
#endif
        else if (irq) {
            printf("unexpected interrupt irq=%d\n", irq);
        }

        // the PLIC allows each device to raise at most one
        // interrupt at a time; tell the PLIC the device is
        // now allowed to interrupt again.
        if (irq)
            plic_complete(irq);

        return 1;

    } else if (scause == 0x8000000000000005L) {
#if defined(SIFIVE_U) || defined(SIFIVE_B)
        if (cpuid() == 1) { // bugs: can't be 0 when based on sifive_u
            clockintr();
        }
#else
        if (cpuid() == 0) { // QEMU : cpuid is 0
            clockintr();
        }
#endif
        SET_TIMER();

        return 2;
    } else {
        return 0;
    }
}

void trapframe_print(struct trapframe *tf) {
    printf("Trapframe {\n");
    printf("    sp: %lx\n", tf->sp);
    printf("    fp: %lx\n", tf->s0);
    printf("    pc: %lx\n", tf->epc);
    printf("    ra: %lx\n", tf->ra);
    printf("    a0: %lx\n", tf->a0);
    printf("    a1: %lx\n", tf->a1);
    printf("    a2: %lx\n", tf->a2);
    printf("    a3: %lx\n", tf->a3);
    printf("    a4: %lx\n", tf->a4);
    printf("    a5: %lx\n", tf->a5);
    printf("    a6: %lx\n", tf->a6);
    printf("    a7: %lx\n", tf->a7);
    printf("    s3: %lx\n", tf->s3);
    printf("}\n");
}