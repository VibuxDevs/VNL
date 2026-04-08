#pragma once
#include "types.h"

#define GDT_NULL        0x00
#define GDT_KERNEL_CODE 0x08
#define GDT_KERNEL_DATA 0x10
#define GDT_USER_CODE   0x18
#define GDT_USER_DATA   0x20
#define GDT_TSS         0x28   /* occupies two 8-byte slots */

typedef struct PACKED {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t  base_mid;
    uint8_t  access;
    uint8_t  granularity;
    uint8_t  base_high;
} GDTEntry;

typedef struct PACKED {
    uint16_t size;
    uint64_t base;
} GDTPointer;

typedef struct PACKED {
    uint32_t reserved0;
    uint64_t rsp[3];     /* RSP for rings 0-2 */
    uint64_t reserved1;
    uint64_t ist[7];     /* Interrupt Stack Table */
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t iopb_offset;
} TSS;

void gdt_init(void);
/* Kernel entry stack for interrupts from ring 3 (per HW manual). */
void tss_set_rsp0(uint64_t rsp0);
