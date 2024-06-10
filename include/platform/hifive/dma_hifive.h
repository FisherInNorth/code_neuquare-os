#ifndef __DMA_HIFIVE_H__
#define __DMA_HIFIVE_H__

#include "common.h"
#define DMA_BASE ((unsigned int)0x03000000)
#define DMA_CHANNEL_BASE(chanID) ((unsigned long)(DMA_BASE + 0x80000 + (0x1000 * (chanID))))

// #define DMA_NCHANNEL    4
// the memory map of the PDMA control registers  ( 0 <= chanID <= 3)
#define DMA_CONTROL(chanID)             *(volatile unsigned int*)(DMA_CHANNEL_BASE(chanID) + 0x000) // Channel Control Register (RW)
#define DMA_NEXT_CONFIG(chanID)         *(volatile unsigned int*)(DMA_CHANNEL_BASE(chanID) + 0x004) // Next transfer type (RW)
#define DMA_NEXT_BYTES(chanID)          *(volatile unsigned long*)(DMA_CHANNEL_BASE(chanID) + 0x008) // Number of bytes to move (RW)
#define DMA_NEXT_DESTINATION(chanID)    *(volatile unsigned long*)(DMA_CHANNEL_BASE(chanID) + 0x010) // Destination start address (RW)
#define DMA_NEXT_SOURCE(chanID)         *(volatile unsigned long*)(DMA_CHANNEL_BASE(chanID) + 0x018) // Source start address (RW)
#define DMA_EXEC_CONFIG(chanID)         *(volatile unsigned int*)(DMA_CHANNEL_BASE(chanID) + 0x104) // Active transfer type (RO)
#define DMA_EXEC_BYTES(chanID)          *(volatile unsigned long*)(DMA_CHANNEL_BASE(chanID) + 0x108) // Number of bytes remaining (RO)
#define DMA_EXEC_DESTINATION(chanID)    *(volatile unsigned long*)(DMA_CHANNEL_BASE(chanID) + 0x110) // Destination current address (RO)
#define DMA_EXEC_SOURCE(chanID)         *(volatile unsigned long*)(DMA_CHANNEL_BASE(chanID) + 0x118) // Source current address (RO)

// control register
#define CLAIM_FIELD 0
#define RUN_FIELD 1
#define DONEIE_FIELD 14
#define ERRORIE_FIELD 15
#define DONE_FIELD 30
#define ERROR_FIELD 31

#endif // __DMA_HIFIVE_H__