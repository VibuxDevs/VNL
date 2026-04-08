#pragma once
#include "types.h"

/* Port I/O */
void     outb(uint16_t port, uint8_t  val);
uint8_t  inb (uint16_t port);
void     outw(uint16_t port, uint16_t val);
uint16_t inw (uint16_t port);
void     outl(uint16_t port, uint32_t val);
uint32_t inl (uint16_t port);
void     io_wait(void);

/* MSR */
uint64_t rdmsr_q(uint32_t msr);
void     wrmsr_q(uint32_t msr, uint64_t val);

/* Interrupt control */
void     sti_asm(void);
void     cli_asm(void);
void     halt_loop(void) NORETURN;

/* CR registers */
uint64_t read_cr2(void);
uint64_t read_cr3(void);
void     write_cr3(uint64_t val);

/* GDT / IDT flush */
void     gdt_flush(void *ptr);
void     idt_flush(void *ptr);
void     tss_flush(uint16_t sel);
