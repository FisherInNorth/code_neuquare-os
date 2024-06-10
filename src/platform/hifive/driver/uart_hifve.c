#include "platform/hifive/uart_hifive.h"
#include "driver/console.h"
#include "common.h"
#include "debug.h"

volatile uarts_t *uarths = (volatile uarts_t *)UART0_BASE;
struct uart_hifve uart;
extern volatile int panicked;

// init
void uart_hifive_init() {
    // tx and rx channel is active.
    uarths->txctrl.txen = 1;
    uarths->rxctrl.rxen = 1;
    // threshold of interrupt triggers
    uarths->txctrl.txcnt = 0;
    uarths->rxctrl.rxcnt = 0;
    // raised less than txcnt
    uarths->ip.txwm = 1;
    uarths->ip.rxwm = 1;
    // raised less than txcnt
    uarths->ie.txwm = 0;
    uarths->ie.rxwm = 1;

    // spinlock for mutex
    initlock(&uart.uart_tx_lock, "uart");
    // semaphore for Sync
    sema_init(&uart.uart_tx_r_sem, 0, "uart_tx_r_sem");
}

// uart interrupt
void uart_hifive_intr(void) {
    while (1) {
        char ch = uart_hifive_getc();
        if (ch == -1)
            break;
        consoleintr(ch);
    }
    acquire(&uart.uart_tx_lock);
    uart_hifve_submit();
    release(&uart.uart_tx_lock);
}

// for asynchronous
void uart_hifve_submit() {
    while (1) {
        if (BUF_IS_EMPETY(uart) || UART_TX_FULL) {
            return;
        }
        int ch = uart.uart_tx_buf[uart.uart_tx_r % UART_HIFIVE_TX_BUF_SIZE];
        uart.uart_tx_r++;
        sema_signal(&uart.uart_tx_r_sem);
        UART_TX_PUTCHAR(ch);
    }
}

// asynchronous
void uart_hifive_putc_asyn(char ch) {
    if (panicked) {
        for (;;)
            ;
    }
    while (BUF_IS_FULL(uart)) {
        sema_wait(&uart.uart_tx_r_sem);
    }
    acquire(&uart.uart_tx_lock);
    uart.uart_tx_buf[uart.uart_tx_w % UART_HIFIVE_TX_BUF_SIZE] = ch;
    uart.uart_tx_w += 1;
    uart_hifve_submit();
    release(&uart.uart_tx_lock);
}

// synchronous
#include "lib/sbi.h"
void uart_hifive_putc_syn(char ch) {
    push_off();

// #ifndef SIFIVE_B
    if (panicked) {
        for (;;)
            ;
    }

    while (ReadReg_hifive(TXDATA) & TX_FULL_MASK) {
        ;
    }
    WriteReg_hifive(TXDATA, ch);
// #else
    // sbi_putchar(ch);
// #endif
    pop_off();
}

int uart_hifive_getc() {
    // if (UART_RX_EMPTY(uarths))
    char ch = UART_RX_GETCHAR;
    if(ch & RX_EMPTY_MASK)
        return -1;
    else
        return ch;
        // return UART_RX_GETCHAR(uarths);
}

// interfaces for upper layer
void uartinit() {
    uart_hifive_init();
}

void uartintr(void) {
    uart_hifive_intr();
}

void uartputc(int ch) {
    uart_hifive_putc_asyn(ch);
}

void uartputc_sync(int ch) {
    uart_hifive_putc_syn(ch);
}

int uartgetc(void) {
    return uart_hifive_getc();
}
