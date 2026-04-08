#include "idt.h"
#include "cpu.h"

/* 8259A PIC ports */
#define PIC1_CMD  0x20
#define PIC1_DATA 0x21
#define PIC2_CMD  0xA0
#define PIC2_DATA 0xA1
#define PIC_EOI   0x20

/* Remap IRQs to vectors 32-47 */
void irq_init(void)
{
    /* Start init sequence (cascade mode) */
    outb(PIC1_CMD, 0x11);  io_wait();
    outb(PIC2_CMD, 0x11);  io_wait();
    /* Vector offsets */
    outb(PIC1_DATA, 0x20); io_wait();   /* IRQ0-7  -> INT 32-39 */
    outb(PIC2_DATA, 0x28); io_wait();   /* IRQ8-15 -> INT 40-47 */
    /* Cascade identity */
    outb(PIC1_DATA, 0x04); io_wait();
    outb(PIC2_DATA, 0x02); io_wait();
    /* 8086 mode */
    outb(PIC1_DATA, 0x01); io_wait();
    outb(PIC2_DATA, 0x01); io_wait();
    /* Unmask all */
    outb(PIC1_DATA, 0x00);
    outb(PIC2_DATA, 0x00);
}

void irq_eoi(uint8_t irq)
{
    if (irq >= 8) outb(PIC2_CMD, PIC_EOI);
    outb(PIC1_CMD, PIC_EOI);
}

void irq_mask(uint8_t irq)
{
    uint16_t port = (irq < 8) ? PIC1_DATA : PIC2_DATA;
    uint8_t mask = inb(port) | (1u << (irq & 7));
    outb(port, mask);
}

void irq_unmask(uint8_t irq)
{
    uint16_t port = (irq < 8) ? PIC1_DATA : PIC2_DATA;
    uint8_t mask = inb(port) & ~(1u << (irq & 7));
    outb(port, mask);
}
