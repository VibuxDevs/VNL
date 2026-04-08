#include "gdt.h"
#include "cpu.h"
#include "string.h"

/* 7 descriptors: null, kcode, kdata, ucode, udata, tss_lo, tss_hi */
#define GDT_ENTRIES 7

static GDTEntry gdt[GDT_ENTRIES] ALIGN(8);
static GDTPointer gdt_ptr;
static TSS tss ALIGN(16);

/* ---- TSS kernel stack (for interrupt entry from user mode) ------- */
extern char boot_stack_top[];   /* defined in boot.asm BSS */

static void gdt_set_entry(int idx, uint32_t base, uint32_t limit,
                           uint8_t access, uint8_t gran)
{
    gdt[idx].base_low  = base & 0xFFFF;
    gdt[idx].base_mid  = (base >> 16) & 0xFF;
    gdt[idx].base_high = (base >> 24) & 0xFF;
    gdt[idx].limit_low = limit & 0xFFFF;
    gdt[idx].granularity = ((limit >> 16) & 0x0F) | (gran & 0xF0);
    gdt[idx].access    = access;
}

/* Set a 16-byte System Segment Descriptor for the 64-bit TSS */
static void gdt_set_tss(int idx, uint64_t base, uint32_t limit)
{
    /* Low 8 bytes reuse the GDTEntry layout */
    gdt[idx].limit_low  = limit & 0xFFFF;
    gdt[idx].base_low   = base & 0xFFFF;
    gdt[idx].base_mid   = (base >> 16) & 0xFF;
    gdt[idx].access     = 0x89;  /* Present, DPL0, 64-bit TSS Available */
    gdt[idx].granularity = ((limit >> 16) & 0x0F);
    gdt[idx].base_high  = (base >> 24) & 0xFF;

    /* High 8 bytes: upper 32 bits of base + reserved */
    uint32_t *hi = (uint32_t *)&gdt[idx + 1];
    hi[0] = (uint32_t)(base >> 32);
    hi[1] = 0;
}

void gdt_init(void)
{
    /* Null */
    gdt_set_entry(0, 0, 0, 0, 0);
    /* Kernel code  — 64-bit (L=1, D=0, G=1) */
    gdt_set_entry(1, 0, 0xFFFFF, 0x9A, 0xA0);
    /* Kernel data  — 64-bit */
    gdt_set_entry(2, 0, 0xFFFFF, 0x92, 0xC0);
    /* User code    — 64-bit, DPL3 */
    gdt_set_entry(3, 0, 0xFFFFF, 0xFA, 0xA0);
    /* User data    — 64-bit, DPL3 */
    gdt_set_entry(4, 0, 0xFFFFF, 0xF2, 0xC0);

    /* TSS */
    memset(&tss, 0, sizeof(TSS));
    tss.rsp[0]       = (uint64_t)boot_stack_top;
    tss.iopb_offset  = sizeof(TSS);
    gdt_set_tss(5, (uint64_t)&tss, sizeof(TSS) - 1);

    gdt_ptr.size = sizeof(gdt) - 1;
    gdt_ptr.base = (uint64_t)&gdt;

    gdt_flush(&gdt_ptr);
    tss_flush(GDT_TSS);
}
