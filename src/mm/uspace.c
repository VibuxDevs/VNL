#include "uspace.h"
#include "cpu.h"
#include "pmm.h"
#include "string.h"

uint64_t uspace_create_pml4(void)
{
    void *pg = pmm_alloc();
    if (!pg) return 0;
    uint64_t phys = (uint64_t)pg;
    memset((void *)phys, 0, PAGE_SIZE);
    uint64_t *kp = (uint64_t *)read_cr3();
    uint64_t *np = (uint64_t *)phys;
    /*
     * DO NOT copy PML4[0] from the kernel: boot maps 0–4 GiB with 1 GiB *pages*
     * in pdpt_low (PS bit). vmm_* assumes every present entry below PTE is a
     * pointer to the next table — following a huge PTE as an address causes #GP.
     * Per-process low canonical space gets a fresh PDPT chain from vmm_map_in_pml4.
     */
    np[511] = kp[511];
    return phys;
}
