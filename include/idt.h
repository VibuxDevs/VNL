#pragma once
#include "types.h"

/* CPU-pushed interrupt frame (matches isr_stubs.asm layout) */
typedef struct PACKED {
    /* pushed by PUSH_REGS macro (reverse order of push) */
    uint64_t gs, fs, es, ds;
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    uint64_t rbp, rdi, rsi, rdx, rcx, rbx, rax;
    /* pushed by stub */
    uint64_t int_no, err_code;
    /* pushed by CPU on interrupt */
    uint64_t rip, cs, rflags, rsp, ss;
} Registers;

typedef void (*ISRHandler)(Registers *r);

void idt_init(void);
void idt_set_handler(uint8_t vec, ISRHandler fn);
void idt_set_raw_handler(uint8_t vec, uint64_t addr);
void idt_set_interrupt_dpl(uint8_t vec, uint8_t dpl); /* 0..3 for INT gate */
void isr_handler(Registers *r);   /* called from ASM stubs */
