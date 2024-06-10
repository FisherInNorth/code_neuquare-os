#include "kernel/cpu.h"
#include "proc/sched.h"
#include "proc/pcb_life.h"
#include "memory/vm.h"
#include "memory/memlayout.h"
#include "fs/fat/fat32_mem.h"
#include "lib/sbi.h"
#include "lib/riscv.h"
#include "test.h"
#include "common.h"
#include "param.h"

void printfinit(void);
void consoleinit(void);
void timer_init();
void trapinithart(void);
void kvminit(void);
void kvminithart(void);
void plicinit(void);
void plicinithart(void);
void virtio_disk_init(void);
void binit(void);
void fileinit(void);
void vmas_init();
void mm_init();
void userinit(void);
void proc_init();
void inode_table_init(void);
void hash_tables_init(void);
void hartinit();
void pdflush_init();
void page_writeback_timer_init(void);
void disk_init(void);
void null_zero_dev_init();
void dma_init(void);
void init_socket_table();

volatile static int started = 0;
__attribute__((aligned(16))) char stack0[4096 * NCPU];
extern char _entry[];

int debug_lock = 0;

int first_core = 1;
int first_hartid = 0;
void hart_start() {
    for (int i = 0; i < NCPU; i++) {
        if (i != first_hartid) {
            if (sbi_hart_start(i, (uint64)_entry, 0) != SBI_SUCCESS) {
                panic("hart start failed");
            }
        }
    }
}

// start() jumps here in supervisor mode on all CPUs.
void main(uint64 hartid) {
    if (first_core == 1) {
        first_core = 0;
        first_hartid = hartid;
        // int status[10] = {0};
        // for (int i = 0; i < NCPU; i++) {
        //     status[i] = sbi_hart_get_status(i);
        // }

        Info("========= Character device ==========\n");
        //========== console ============
        consoleinit();
        //========== zero and null ============
        null_zero_dev_init();
        //========== printf ============
        printfinit();
        //========== hart ============
        hartinit();
        debug_lock = 1;

        
        //========== physical memory management ==========
        mm_init();
        //========== VMA management ==========
        vmas_init();

        //========== kernel virtual memory ==========
        kvminit();     // create kernel page table
        kvminithart(); // turn on paging

        // ========= Proc management and Thread management =======
        proc_init(); // process table
        tcb_init();

        // ========== timer init ==========
        timer_init();

        // !!! Note: trapinithart can be called after timer_init
        // Trap
        trapinithart(); // install kernel trap vector

        // =========== PLIC ===========
        plicinit();     // set up interrupt controller
        plicinithart(); // ask PLIC for device interrupts

        // =========== File System ==========
        binit();
        fileinit();
        inode_table_init();

        //========== socket ==========
        init_socket_table();

        //========== global map ==========
        hash_tables_init();


        Info("========= Block device ==========\n");
        //========== block device ============
        disk_init();

#if defined(SIFIVE_U) || defined(SIFIVE_B)
        // DMA
        dma_init();
#endif

#ifdef SUBMIT
        extern void oscomp_init(void);
        oscomp_init();
#else
        //========== First user process =========
        
#endif
        userinit();
        // pdflush kernel thread
        // pdflush_init();
        __sync_synchronize();

        hart_start();
        Info("hart %d is working\n", cpuid());
        started = 1;
    } else {
        while (atomic_read4((int *)&started) == 0)
            ;
        hartinit();
        __sync_synchronize();
        Info("hart %d is working\n", cpuid());
        kvminithart();  // turn on paging
        trapinithart(); // install kernel trap vector
        plicinithart(); // ask PLIC for device interrupts

#if defined(SIFIVE_U) || defined(SIFIVE_B)
        // DMA
        dma_init();
#endif
    }

    thread_scheduler();
}
