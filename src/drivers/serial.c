#include "serial.h"
#include "cpu.h"

#define COM1 0x3F8

void serial_init(void)
{
    outb(COM1 + 1, 0x00);   /* Disable interrupts */
    outb(COM1 + 3, 0x80);   /* Enable DLAB (set baud rate divisor) */
    outb(COM1 + 0, 0x03);   /* 38400 baud: divisor = 3 (lo byte) */
    outb(COM1 + 1, 0x00);   /*                          (hi byte) */
    outb(COM1 + 3, 0x03);   /* 8 bits, no parity, one stop bit */
    outb(COM1 + 2, 0xC7);   /* Enable FIFO, clear them, 14-byte threshold */
    outb(COM1 + 4, 0x0B);   /* IRQs enabled, RTS/DSR set */
}

int serial_received(void)
{
    return inb(COM1 + 5) & 1;
}

char serial_getchar(void)
{
    while (!serial_received());
    return (char)inb(COM1);
}

static int serial_tx_empty(void)
{
    return inb(COM1 + 5) & 0x20;
}

void serial_putchar(char c)
{
    while (!serial_tx_empty());
    if (c == '\n') {
        outb(COM1, '\r');
        while (!serial_tx_empty());
    }
    outb(COM1, (uint8_t)c);
}

void serial_puts(const char *s)
{
    while (*s) serial_putchar(*s++);
}
