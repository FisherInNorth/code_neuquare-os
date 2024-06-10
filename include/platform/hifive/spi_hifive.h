#ifndef __SPI_HIFIVE_H__
#define __SPI_HIFIVE_H__
#include "common.h"

void QSPI2_Init();
void spi_write(uint8 dataframe);
uint8 spi_read();

// base address
#define QSPI_2_BASE ((unsigned int)0x10050000)

// SPI control registers address
// (present only on controllers with the direct-map flash interface)
#define QSPI2_SCKDIV        *(volatile unsigned int *)(QSPI_2_BASE + 0x00)  // Serial clock divisor
#define QSPI2_SCKMODE       *(volatile unsigned int *)(QSPI_2_BASE + 0x04) // Serial clock mode
#define QSPI2_CSID          *(volatile unsigned int *)(QSPI_2_BASE + 0x10)    // Chip select ID
#define QSPI2_CSDEF         *(volatile unsigned int *)(QSPI_2_BASE + 0x14)   // Chip select default
#define QSPI2_CSMODE        *(volatile unsigned int *)(QSPI_2_BASE + 0x18)  // Chip select mode
#define QSPI2_DELAY0        *(volatile unsigned int *)(QSPI_2_BASE + 0x28)  // Delay control 0
#define QSPI2_DELAY1        *(volatile unsigned int *)(QSPI_2_BASE + 0x2C)  // Delay control 1
#define QSPI2_FMT           *(volatile unsigned int *)(QSPI_2_BASE + 0x40)     // Frame format
#define QSPI2_TXDATA        *(volatile unsigned int *)(QSPI_2_BASE + 0x48)  // Tx FIFO data
#define QSPI2_RXDATA        *(volatile unsigned int *)(QSPI_2_BASE + 0x4C)  // Rx FIFO data
#define QSPI2_TXMARK        *(volatile unsigned int *)(QSPI_2_BASE + 0x50)  // Tx FIFO watermark
#define QSPI2_RXMARK        *(volatile unsigned int *)(QSPI_2_BASE + 0x54)  // Rx FIFO watermark
#define QSPI2_FCTRL         *(volatile unsigned int *)(QSPI_2_BASE + 0x60)   // SPI flash interface control
#define QSPI2_FFMT          *(volatile unsigned int *)(QSPI_2_BASE + 0x64)    // SPI flash instruction format
#define QSPI2_IE            *(volatile unsigned int *)(QSPI_2_BASE + 0x70)      // SPI interrupt enablle
#define QSPI2_IP            *(volatile unsigned int *)(QSPI_2_BASE + 0x74)      // SPI interrupt pending

// chip select mode
#define CSMODE_AUTO 0 // AUTO Assert/deassert CS at the beginning/end of each frame
#define CSMODE_HOLD 2 // HOLD Keep CS continuously asserted after the initial frame
#define CSMODE_OFF 3  // OFF Disable hardware control of the CS pin



// Inlining header functions in C
// https://stackoverflow.com/a/23699777/7433423

/**
 * Get smallest clock divisor that divides input_khz to a quotient less than or
 * equal to max_target_khz;
 */

// temporarily not used
inline unsigned int spi_min_clk_divisor(unsigned int input_khz, unsigned int max_target_khz) {
    // f_sck = f_in / (2 * (div + 1)) => div = (f_in / (2*f_sck)) - 1
    //
    // The nearest integer solution for div requires rounding up as to not exceed
    // max_target_khz.
    //
    // div = ceil(f_in / (2*f_sck)) - 1
    //     = floor((f_in - 1 + 2*f_sck) / (2*f_sck)) - 1
    //
    // This should not overflow as long as (f_in - 1 + 2*f_sck) does not exceed
    // 2^32 - 1, which is unlikely since we represent frequencies in kHz.
    unsigned int quotient = (input_khz + 2 * max_target_khz - 1) / (2 * max_target_khz);
    // Avoid underflow
    if (quotient == 0) {
        return 0;
    } else {
        return quotient - 1;
    }
}


#endif