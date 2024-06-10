#ifndef __UART_HIFIVE_H__
#define __UART_HIFIVE_H__

#include "common.h"
#include "atomic/spinlock.h"
#include "atomic/semaphore.h"

// | Address   | Name     | Description                     |
// |-----------|----------|---------------------------------|
// | 0x000     | txdata   | Transmit data register          |
// | 0x004     | rxdata   | Receive data register           |
// | 0x008     | txctrl   | Transmit control register       |
// | 0x00C     | rxctrl   | Receive control register        |
// | 0x010     | ie       | UART interrupt enable           |
// | 0x014     | ip       | UART Interrupt pending          |
// | 0x018     | div      | Baud rate divisor               |

// Memory map
#define UART0_BASE 0x10010000L // UART0 base address
#define UART1_BASE 0x10011000L // UART1 base address
#define TXDATA 0x00            // Transmit data register
#define RXDATA 0x04            // Receive data register
#define TXCTRL 0x08            // Transmit control register
#define RXCTRL 0x0C            // Receive control register
#define IE 0x10                // UART interrupt enable
#define IP 0x14                // UART interrupt pending
#define DIV 0x18               // Baud rate divisor

// read and write registers
#define Reg_hifive(reg) ((volatile unsigned int *)(UART0_BASE + reg))
#define ReadReg_hifive(reg) (*(Reg_hifive(reg)))
#define WriteReg_hifive(reg, v) (*(Reg_hifive(reg)) = (v))

// used for getchar and putchar
#define TX_RX_DATA_MASK 0xff
#define TX_FULL_MASK 0x80000000
#define RX_EMPTY_MASK 0x80000000

typedef struct uarths_txdata {
    /* Bits [7:0] is data */
    uchar data;
    /* Bits [30:8] is 0 */
    ushort zero;
    /* Bit 31 is full status */
    uchar full;
} __attribute__((packed, aligned(4))) uarths_txdata_t;

typedef struct uarths_rxdata {
    /* Bits [7:0] is data */
    uchar data;
    /* Bits [30:8] is 0 */
    ushort zero;
    /* Bit 31 is empty status */
    uchar empty;
} __attribute__((packed, aligned(4))) uarths_rxdata_t;

typedef struct uarths_txctrl {
    /* Bit 0 is txen, controls whether the Tx channel is active. */
    uint32 txen : 1;
    /* Bit 1 is nstop, 0 for one stop bit and 1 for two stop bits */
    uint32 nstop : 1;
    /* Bits [15:2] is reserved */
    uint32 resv0 : 14;
    /* Bits [18:16] is threshold of interrupt triggers */
    uint32 txcnt : 3;
    /* Bits [31:19] is reserved */
    uint32 resv1 : 13;
} __attribute__((packed, aligned(4))) uarths_txctrl_t;

typedef struct uarths_rxctrl {
    /* Bit 0 is txen, controls whether the Tx channel is active. */
    uint32 rxen : 1;
    /* Bits [15:1] is reserved */
    uint32 resv0 : 15;
    /* Bits [18:16] is threshold of interrupt triggers */
    uint32 rxcnt : 3;
    /* Bits [31:19] is reserved */
    uint32 resv1 : 13;
} __attribute__((packed, aligned(4))) uarths_rxctrl_t;

typedef struct uarths_ie {
    /* Bit 0 is txwm, raised less than txcnt */
    uint32 txwm : 1;
    /* Bit 1 is txwm, raised greater than rxcnt */
    uint32 rxwm : 1;
    /* Bits [31:2] is 0 */
    uint32 zero : 30;
} __attribute__((packed, aligned(4))) uarths_ie_t;

typedef struct uarths_ip {
    /* Bit 0 is txwm, raised less than txcnt */
    uint32 txwm : 1;
    /* Bit 1 is txwm, raised greater than rxcnt */
    uint32 rxwm : 1;
    /* Bits [31:2] is 0 */
    uint32 zero : 30;
} __attribute__((packed, aligned(4))) uarths_ip_t;

typedef struct uarths_div {
    /* Bits [31:2] is baud rate divisor register */
    uint32 div : 16;
    /* Bits [31:16] is 0 */
    uint32 zero : 16;
} __attribute__((packed, aligned(4))) uarths_div_t;

typedef struct uarths {
    /* Address offset 0x00 */
    uarths_txdata_t txdata;
    /* Address offset 0x04 */
    uarths_rxdata_t rxdata;
    /* Address offset 0x08 */
    uarths_txctrl_t txctrl;
    /* Address offset 0x0c */
    uarths_rxctrl_t rxctrl;
    /* Address offset 0x10 */
    uarths_ie_t ie;
    /* Address offset 0x14 */
    uarths_ip_t ip;
    /* Address offset 0x18 */
    uarths_div_t div;
} __attribute__((packed, aligned(4))) uarts_t;

#define UART_HIFIVE_TX_BUF_SIZE 64

// ref : linux-riscv  mmio.h
static inline uchar __raw_readb(const volatile void *addr) {
    uchar val;

    asm volatile("lb %0, 0(%1)"
                 : "=r"(val)
                 : "r"(addr));
    return val;
}
static inline void __raw_writeb(uchar val, volatile void *addr) {
    asm volatile("sb %0, 0(%1)"
                 :
                 : "r"(val), "r"(addr));
}
static inline uint32 __raw_readl(const volatile void *addr) {
    uint32 val;

    asm volatile("lw %0, 0(%1)"
                 : "=r"(val)
                 : "r"(addr));
    return val;
}

static inline void __raw_writel(uint32 val, volatile void *addr) {
    asm volatile("sw %0, 0(%1)"
                 :
                 : "r"(val), "r"(addr));
}

#define writeb_cpu(v, c) ((void)__raw_writeb((v), (c)))
#define readb_cpu(c) ({ uchar  __r = __raw_readb(c); __r; })
#define io_br() \
    do {        \
    } while (0)
#define wmb() __asm__ __volatile__("fence w,o" \
                                   :           \
                                   :           \
                                   : "memory")
#define rmb() __asm__ __volatile__("fence i,r" \
                                   :           \
                                   :           \
                                   : "memory")
#define readb(c) ({ uchar  __v; io_br(); __v = readb_cpu(c); rmb(); __v; })
#define writeb(v, c) ({ wmb(); writeb_cpu((v), (c)); io_br(); })

#define readl(c) ({ uint32 __v; __io_br(); __v = __raw_readl(c); __io_ar(); __v; })
#define writel(v, c) ({ __io_bw(); __raw_writel((v),(c)); __io_aw(); })

// buf is full or emptry???
#define BUF_IS_FULL(uart) (uart.uart_tx_w == uart.uart_tx_r + UART_HIFIVE_TX_BUF_SIZE)
#define BUF_IS_EMPETY(uart) (uart.uart_tx_w == uart.uart_tx_r)
// put char into buffer
#define UART_BUF_PUTCHAR(uart, ch) (uart.uart_tx_buf[(uart.uart_tx_w++) % UART_HIFIVE_TX_BUF_SIZE] = ch)
// get char from buffer
#define UART_BUF_GETCHAR(uart) (uart.uart_tx_buf[(uart.uart_tx_r++) % UART_HIFIVE_TX_BUF_SIZE])
// uart txdata is full ? uart rxdata is emptry ? with memory barrier
#define UART_TX_FULL (ReadReg_hifive(TXDATA) & TX_FULL_MASK)
#define UART_RX_EMPTY (ReadReg_hifive(RXDATA) & RX_EMPTY_MASK)
// uart txdata put char
#define UART_TX_PUTCHAR(ch) (WriteReg_hifive(TXDATA, ch))
// uart rxdata get char
#define UART_RX_GETCHAR (ReadReg_hifive(RXDATA))

// #define UART_RX_EMPTY(uarths) (readb(&(uarths->rxdata.empty)) & RX_EMPTY_MASK)
// #define UART_TX_PUTCHAR(uarths, ch) (writeb(((uchar)ch), &(uarths->txdata.data)))
// #define UART_RX_GETCHAR(uarths) (readb(&(uarths->txdata.data)) & TX_RX_DATA_MASK)
// #define UART_TX_FULL(uarths) (readb(&(uarths->txdata.full)) & TX_FULL_MASK)

struct uart_hifve {
    struct spinlock uart_tx_lock;

    char uart_tx_buf[UART_HIFIVE_TX_BUF_SIZE];
    uint64 uart_tx_w; // write next to uart_tx_buf[uart_tx_w % UART_TX_BUF_SIZE]
    uint64 uart_tx_r; // read next from uart_tx_buf[uart_tx_r % UART_TX_BUF_SIZE]

    struct semaphore uart_tx_r_sem;
};

// init
void uart_hifive_init();
// interrupt
void uart_hifive_intr(void);
// put char (asyc and syc)
void uart_hifive_putc_asyn(char ch);
void uart_hifive_putc_syn(char ch);
// put char submit
void uart_hifve_submit();
// get char
int uart_hifive_getc();

#endif