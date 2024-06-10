#include "common.h"
#include "kernel/plic.h"
#include "param.h"
// #include "memory/memlayout.h"
#include "platform/hifive/pml_hifive.h"
#include "lib/riscv.h"
#include "kernel/cpu.h"
#include "proc/pcb_life.h"
#include "debug.h"

// the riscv Platform Level Interrupt Controller (PLIC).
//

void plicinit(void) {
    // set desired IRQ priorities non-zero (otherwise disabled).
    // *(uint32 *)(PLIC + UART0_IRQ * 4) = 1;
    // *(uint32 *)(PLIC_PRIORITY(UART0_IRQ)) = 1;

    PLIC_PRIORITY(UART0_IRQ) = 1;

    for ( int intid = DMA_IRQ_START; intid <= DMA_IRQ_END; ++intid) {
        PLIC_PRIORITY(intid) = 1;
    }

    return;
}

void plicinithart(void) {
    int hart = cpuid();

    // ((uint32 *)PLIC_SENABLE(hart))[ UART0_IRQ / 32 ] |= UART0_IRQ % 32; 

    // set enable bits for this hart's S-mode
    if (hart) {
        // for the uart 
        PLIC_SET_SENABLE(hart,UART0_IRQ);
        
        // for the PDMA
        // for ( int intid = DMA_IRQ_START; intid <= DMA_IRQ_END; ++intid) {
        //     PLIC_SET_SENABLE(hart,intid);
        // }
        
        // 将 hart 绑定到对应的 DMA channel 上
        // 这样一个 dma 中断发来时，只有一个核进入中断处理程序
        PLIC_SET_SENABLE(hart, DMA_IRQ_START + HART2ChanID(hart) * 2);      // transfer complete 
        PLIC_SET_SENABLE(hart, DMA_IRQ_START + HART2ChanID(hart) * 2 + 1);  // encounter an error

        // set this hart's S-mode priority threshold to 0.
        PLIC_SPRIORITY(hart) = 0;
    } 
    return;
}

// ask the PLIC what interrupt we should serve.
int plic_claim(void) {
    int hart = cpuid();
    ASSERT(hart > 0);
    int irq = PLIC_SCLAIM(hart);
    return irq;

}

// tell the PLIC we've served this IRQ.
void plic_complete(int irq) {
    int hart = cpuid();
    ASSERT(hart > 0);
    PLIC_SCLAIM(hart) = irq;
}
