#ifndef __UART_H__
#define __UART_H__

void uartinit(void);
void uartintr(void);
void uartputc(int);
void uartputc_sync(int);
int uartgetc(void);

#endif // __UART_H__