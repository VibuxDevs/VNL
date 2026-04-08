/*
 * VNL — Vibe Not Linux
 * kernel_main: called from boot.asm after entering 64-bit mode
 *
 * Order:
 *   1. VGA + serial (early output)
 *   2. GDT (proper segments + TSS)
 *   3. IDT (exception/interrupt vectors)
 *   4. IRQ (PIC remapping)
 *   5. PMM (physical memory manager)
 *   6. VMM (virtual memory manager)
 *   7. Heap (kernel malloc)
 *   8. Timer (PIT, 1000 Hz)
 *   9. Keyboard
 *  10. Syscall (INT 0x80)
 *  11. STI (enable interrupts)
 *  12. Kernel shell
 */
#include "types.h"
#include "vga.h"
#include "serial.h"
#include "printf.h"
#include "gdt.h"
#include "idt.h"
#include "cpu.h"
#include "pmm.h"
#include "vmm.h"
#include "heap.h"
#include "timer.h"
#include "keyboard.h"
#include "syscall.h"
#include "panic.h"
#include "vfs.h"
#include "pci.h"
#include "acpi.h"
#include "sched.h"

extern uint8_t kernel_end[];
void shell_run(void);
void irq_init(void);

/* ---- Multiboot2 memory info parsing (minimal) -------------------- */
#define MB2_MAGIC_VAL 0x36D76289   /* bootloader → kernel magic value */
#define MB2_TAG_END   0
#define MB2_TAG_MEM   4    /* Basic memory information */
#define MB2_TAG_MMAP  6    /* Memory map */

typedef struct PACKED { uint32_t type; uint32_t size; } MB2Tag;
typedef struct PACKED {
    MB2Tag hdr;
    uint32_t mem_lower;   /* KiB below 1 MiB */
    uint32_t mem_upper;   /* KiB above 1 MiB */
} MB2TagMem;

static uint64_t parse_mb2_memory(uint64_t mb_info_phys)
{
    /* mb_info_phys is still identity-mapped (below 4 GiB) */
    uint8_t *ptr = (uint8_t *)mb_info_phys;
    uint32_t total_size = *(uint32_t *)ptr;
    uint8_t *end = ptr + total_size;
    ptr += 8; /* skip fixed part (total_size + reserved) */

    while (ptr < end) {
        MB2Tag *tag = (MB2Tag *)ptr;
        if (tag->type == MB2_TAG_END) break;
        if (tag->type == MB2_TAG_MEM) {
            MB2TagMem *m = (MB2TagMem *)tag;
            /* mem_upper is in KiB above 1 MiB; add 1024 for the first MiB */
            return (uint64_t)m->mem_upper + 1024;
        }
        ptr += ALIGN_UP(tag->size, 8);
    }
    /* Fallback: assume 128 MiB */
    return 128 * 1024;
}

/* ---- Boot banner ------------------------------------------------- */
static void print_banner(void)
{
    vga_set_color(VGA_YELLOW, VGA_BLACK);
    kprintf(
        "\n"
        "  ____   ____  _   _   _  __  __\n"
        " / ___| / ___|| \\ | | | ||  \\/  |\n"
        " \\___ \\| |    |  \\| | | || |\\/| |\n"
        "  ___) | |___ | |\\  | | || |  | |\n"
        " |____/ \\____||_| \\_| |_||_|  |_|\n"
        "\n"
    );
    vga_set_color(VGA_LCYAN, VGA_BLACK);
    kprintf("  Vibe Not Linux v0.2.0 — 64-bit kernel\n");
    kprintf("  Built: " __DATE__ " " __TIME__ "\n\n");
    vga_set_color(VGA_WHITE, VGA_BLACK);
}

/* ---- Entry point ------------------------------------------------- */
void kernel_main(uint32_t magic, uint64_t mb_info)
{
    /* Step 1: Early output */
    serial_init();
    vga_init();

    print_banner();

    /* Verify Multiboot2 */
    if (magic != MB2_MAGIC_VAL) {
        kprintf("[WARN] Not loaded by a Multiboot2 loader (magic=0x%x)\n", magic);
    }

    /* Step 2: GDT */
    kprintf("[INIT] GDT...\n");
    gdt_init();

    /* Step 3: IDT */
    kprintf("[INIT] IDT...\n");
    idt_init();

    /* Step 4: IRQ (PIC remapping) */
    kprintf("[INIT] IRQ/PIC...\n");
    irq_init();

    /* Step 5: Physical memory manager */
    kprintf("[INIT] PMM...\n");
    uint64_t mem_kb = (mb_info) ? parse_mb2_memory(mb_info) : 128 * 1024;
    pmm_init(mem_kb);

    /* Reserve physical frames used by the kernel image itself.
     * kernel_end is the high-VMA address; its physical address is
     * kernel_end - KERNEL_VMA.  Reserve from 1 MiB (0x100000) through
     * physical kernel_end so the VMM can't reclaim these frames. */
    uint64_t kern_phys_end = (uint64_t)kernel_end - 0xFFFFFFFF80000000ULL;
    pmm_reserve(0x100000, kern_phys_end - 0x100000);

    kprintf("       %llu MiB RAM detected (%llu free pages)\n",
            (pmm_total_pages() * PAGE_SIZE) / (1024*1024),
            pmm_free_pages());

    /* Step 6: Virtual memory manager */
    kprintf("[INIT] VMM...\n");
    vmm_init();

    /* Step 7: Kernel heap */
    kprintf("[INIT] Heap...\n");
    /* Place heap at 0xFFFF_C000_0000_0000 (arbitrary safe high address) */
    heap_init(0xFFFFC00000000000ULL, 4 * PAGE_SIZE);

    /* Step 8: Timer */
    kprintf("[INIT] Timer (1000 Hz)...\n");
    timer_init(1000);

    /* Step 9: Keyboard */
    kprintf("[INIT] Keyboard...\n");
    keyboard_init();

    /* Step 10: VFS */
    kprintf("[INIT] VFS (ramfs)...\n");
    vfs_init();

    /* Step 11: PCI */
    kprintf("[INIT] PCI...\n");
    pci_init();

    /* Step 12: ACPI */
    kprintf("[INIT] ACPI...\n");
    acpi_init();

    /* Step 13: Syscall */
    kprintf("[INIT] Syscall (INT 0x80)...\n");
    syscall_init();

    /* Step 14: Scheduler (installs sched_timer_stub at IRQ0) */
    kprintf("[INIT] Scheduler...\n");
    sched_init();

    /* Step 15: Enable interrupts */
    kprintf("[INIT] Enabling interrupts...\n");
    sti_asm();

    kprintf("[INIT] All subsystems ready.\n\n");

    /* Step 16: Drop into shell (runs as task 0) */
    shell_run();

    /* Should never reach here */
    kpanic("kernel_main returned!");
}
