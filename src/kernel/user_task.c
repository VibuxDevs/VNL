#include "types.h"
#include "sched.h"
#include "uspace.h"
#include "vmm.h"
#include "pmm.h"
#include "string.h"
#include "printf.h"

/*
 * Minimal ring-3 proof task: SYS_WRITE(1, "Hello from ring3\n", len) via int 0x80.
 * Code at user VA 0x400000; stack page 0x801000..0x801fff, RSP 0x802000.
 */
void user_ring3_demo_spawn(void)
{
    uint64_t pml4 = uspace_create_pml4();
    if (!pml4) {
        kprintf("ring3: cannot create page table\n");
        return;
    }
    void *cp = pmm_alloc();
    void *sp = pmm_alloc();
    if (!cp || !sp) {
        kprintf("ring3: out of physical pages\n");
        return;
    }
    uint8_t *page = (uint8_t *)((uintptr_t)cp);
    memset(page, 0, PAGE_SIZE);

    static const uint8_t blob[] = {
        0xb8, 0x01, 0x00, 0x00, 0x00,
        0xbf, 0x01, 0x00, 0x00, 0x00,
        0x48, 0xbe, 0x30, 0x00, 0x40, 0x00, 0x00, 0x00, 0x00,
        0xba, 0x13, 0x00, 0x00, 0x00,
        0xcd, 0x80,
        0xb8, 0x3c, 0x00, 0x00, 0x00,
        0x31, 0xff,
        0xcd, 0x80,
    };
    memcpy(page, blob, sizeof(blob));
    const char *msg = "Hello from ring 3\n";
    memcpy(page + 0x30, msg, strlen(msg) + 1);

    vmm_map_in_pml4(pml4, 0x400000, (uint64_t)cp, VMM_FLAG_PRESENT | VMM_FLAG_USER);
    vmm_map_in_pml4(pml4, 0x801000, (uint64_t)sp, VMM_FLAG_PRESENT | VMM_FLAG_WRITE | VMM_FLAG_USER);

    int pid = task_create_user("ring3", pml4, 0x400000, 0x802000, 0x400000, 0x401000);
    if (pid < 0)
        kprintf("ring3: task_create_user failed\n");
    else
        kprintf("ring3: user task pid %u (timer will run it)\n", (unsigned)pid);
}
