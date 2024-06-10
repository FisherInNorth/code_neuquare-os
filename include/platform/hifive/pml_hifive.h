#ifndef __PML_HIFIVE_H__
#define __PML_HIFIVE_H__

// #include "debug.h"

// hifive u740 puts UART registers here in physical memory.
// #define UART0_BASE 0x10010000L // UART0 base address
// #define UART1_BASE 0x10011000L // UART1 base address

#ifdef SIFIVE_B
#define UART0_IRQ 39
#define UART1_IRQ 40
#else
#define UART0_IRQ 4
#define UART1_IRQ 5
#endif

// hifive u740 puts SPI registers here in physical memory.
// #define QSPI_2_BASE ((unsigned int)0x10050000)
#define SPI0_IRQ 41
#define SPI1_IRQ 42
#define SPI2_IRQ 43

// PDMA
#ifdef SIFIVE_B
#define DMA_IRQ_START 11
#define DMA_IRQ_END 18
#else
#define DMA_IRQ_START 23
#define DMA_IRQ_END 30
#endif

#define DMA_NCHANNEL 4
#define DMA_COMPLETE_IRQ(chanID) (DMA_IRQ_START + (chanID)*2)
#define DMA_ERROR_IRQ(chanID) (DMA_IRQ_START + (chanID)*2 + 1)

#define ChanID2HART(chanID) ((chanID) + 1)
#define HART2ChanID(hart) ((hart)-1)

// core local interruptor (CLINT), which contains the timer.
#define CLINT 0x2000000L
// #define CLINT_MTIMECMP(hartid) (CLINT + 0x4000 + 8 * (hartid))
#define CLINT_MTIME (CLINT + 0xBFF8) // cycles since boot.
#define CLINT_INTERVAL 200000       // 

// hifive u740 puts platform-level interrupt controller (PLIC_BASE) here. (width=4B)
#define PLIC_BASE ((unsigned long)0x0C000000)
#define PLIC_PRIORITY(intID) *(unsigned int *)(PLIC_BASE + 4 * (intID)) // intID starts from 1, ends to 69
#define PLIC_PENDING_ARRAYBASE (unsigned int *)(PLIC_BASE + 0x1000)
#define PLIC_IFSET_PENDING(intID) (((PLIC_PENDING_ARRAYBASE[(intID) / 32] | ((intID) % 32))) ? 1 : 0)

// M-Mode interrupt enable
#define PLIC_MENABLE_ARRAYBASE(hart) (unsigned int *)((hart) > 0 ? PLIC_BASE + 0x2080 + ((hart)-1) * 0x100 : PLIC_BASE + 0x2000) // Start M-Mode interrupt enables
#define PLIC_SET_MENABLE(hart, intID)            \
    do {                                         \
        PLIC_MENABLE_ARRAYBASE((hart))           \
        [(intID) / 32] |= (1 << ((intID) % 32)); \
    } while (0);

// S-Mode interrupt enable
#define PLIC_SENABLE_ARRAYBASE(hart) ((unsigned int *)(PLIC_BASE + 0x2100 + ((hart)-1) * 0x100)) //  Start Hart S-Mode interrupt enables (only hart 1-4 has S-mode IE)
#define PLIC_SET_SENABLE(hart, intID)            \
    do {                                         \
        PLIC_SENABLE_ARRAYBASE((hart))           \
        [(intID) / 32] |= (1 << ((intID) % 32)); \
    } while (0);                                 \
    // ASSERT((hart) > 0);

#define PLIC_MPRIORITY(hart) *(unsigned int *)((hart) > 0 ? PLIC_BASE + 0x201000 + (hart)*0x2000 : PLIC_BASE + 0x200000)
#define PLIC_MCLAIM(hart) *(unsigned int *)((hart) > 0 ? PLIC_BASE + 0x201004 + (hart)*0x2000 : PLC + 0X200004) // M-Mode claim/complete
#define PLIC_SPRIORITY(hart) *(unsigned int *)(PLIC_BASE + 0x202000 + ((hart)-1) * 0x2000)                      // hart starts from 1, ends to 4
#define PLIC_SCLAIM(hart) *(unsigned int *)(PLIC_BASE + 0x202004 + ((hart)-1) * 0x2000)                         // S-Mode claim/complete

#endif // __PML_HIFIVE_H__