#include "platform/hifive/dma_hifive.h"
#include "platform/hifive/pml_hifive.h"
#include "memory/memlayout.h"
#include "param.h"
#include "atomic/semaphore.h"
#include "debug.h"
#include "kernel/cpu.h"

// 目前 hart 1~4 使用 DMA channel 0~3

static struct dma {
    struct semaphore free;
    struct semaphore done;
    int channel;
} dma[NCPU];

void dma_req(uint64 pa_des, uint64 pa_src, uint32 nr_bytes ) {
    int hart = cpuid();
    ASSERT(hart>0);
    int chanID = HART2ChanID(hart);
    sema_wait(&dma[hart].free);
    
uint32 reg_control = DMA_CONTROL(chanID); 
printf("%d",reg_control);
    if ( DMA_CONTROL(chanID) & (1 << RUN_FIELD) ) {
        // slopy handle
        panic("dma_req: dma still running!");
    }
    
    DMA_CONTROL(chanID) = 0;
    DMA_CONTROL(chanID) |= (1 << CLAIM_FIELD) | (1 << DONEIE_FIELD) | (1 << ERRORIE_FIELD); // claim and set the doneIE
    
    wmb();
    
    DMA_NEXT_CONFIG(chanID) = 0;
    DMA_NEXT_SOURCE(chanID) = pa_src;
    DMA_NEXT_DESTINATION(chanID) = pa_des;
    DMA_NEXT_BYTES(chanID) = nr_bytes;
printf("Next bytes: %d\n",DMA_NEXT_BYTES(chanID));
printf("Next source: 0x%x\n",DMA_NEXT_SOURCE(chanID));
printf("Next destination: 0x%x\n",DMA_NEXT_DESTINATION(chanID));

    // run
    wmb();
    DMA_CONTROL(chanID) |= (1 << RUN_FIELD);
printf("dma control: 0x%x\n", DMA_CONTROL(chanID));

printf("Exec bytes: %d\n",DMA_EXEC_BYTES(chanID));
printf("Exec source: 0x%x\n",DMA_EXEC_SOURCE(chanID));
printf("Exec destination: 0x%x\n",DMA_EXEC_DESTINATION(chanID));
    sema_wait(&dma[hart].done);
    return;
}


void dma_intr(int irq) {
    int interrupt = irq - DMA_IRQ_START; 
    ASSERT( (interrupt & 1) == 0); // 只许成功
    int chanID = interrupt / 2; 

    if ( DMA_CONTROL(chanID) & (1 << DONE_FIELD) ) {
        // ensure the transaction has done
        sema_signal(&dma[ChanID2HART(chanID)].done);
        sema_signal(&dma[ChanID2HART(chanID)].free);
    } else {
        // slopy handle
        panic("dam_intr: dma hasn't done yet!");
    }
    return;
}

void dma_init() {
    int hart = cpuid();
    if (hart == 0) {
        return;
    }
    // ASSERT(hart > 0);
    int chanID = HART2ChanID(hart);
    sema_init(&dma[hart].free, 1, "dma");
    sema_init(&dma[hart].done, 0, "dma");
    dma[hart].channel = chanID;
    DMA_CONTROL(chanID) = 0;

    // for (int hart = 0; hart != NCPU; ++hart) {
    //     // sema_init(&dma[hart].done, 0, "dma");
    // }
    return;
}