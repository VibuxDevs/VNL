/*
 * VNL Virtual Memory Manager
 * Manages the kernel's PML4 page table using 4 KiB pages.
 * Provides vmm_map / vmm_unmap / vmm_get_phys for the kernel address space.
 *
 * The higher-half identity mapping built in boot.asm gives us access to all
 * physical memory via KERNEL_VMA + phys_addr, so we can safely walk and
 * allocate new page table frames.
 */
#include "vmm.h"
#include "pmm.h"
#include "string.h"
#include "panic.h"
#include "cpu.h"

#define KERNEL_VMA  0xFFFFFFFF80000000ULL

/* Convert a physical address to a kernel-accessible virtual address */
#define P2V(phys)  ((uint64_t)(phys) + 0 )   /* identity map still in place for low phys */

/* Extract page-table indices from a virtual address */
#define PML4_IDX(va) (((va) >> 39) & 0x1FF)
#define PDPT_IDX(va) (((va) >> 30) & 0x1FF)
#define PD_IDX(va)   (((va) >> 21) & 0x1FF)
#define PT_IDX(va)   (((va) >> 12) & 0x1FF)
#define PAGE_MASK    (~(PAGE_SIZE - 1))

static uint64_t *current_pml4 = NULL;

/* ---- Internal helpers -------------------------------------------- */

/* Allocate a zeroed 4 KiB page table frame and return its phys address */
static uint64_t alloc_pt_frame(void)
{
    void *frame = pmm_alloc();
    if (!frame) kpanic("VMM: out of physical memory");
    memset((void *)((uint64_t)frame), 0, PAGE_SIZE);
    return (uint64_t)frame;
}

/*
 * Walk/create the 4-level page table for a given virtual address.
 * Returns a pointer to the PTE (level-1 entry).
 * If create=true, missing intermediate tables are allocated.
 */
static uint64_t *vmm_get_pte(uint64_t *pml4, uint64_t virt, bool create)
{
    uint64_t *table = pml4;

    /* Levels: PML4 -> PDPT -> PD -> PT */
    for (int level = 3; level > 0; level--) {
        uint64_t idx;
        switch (level) {
            case 3: idx = PML4_IDX(virt); break;
            case 2: idx = PDPT_IDX(virt); break;
            case 1: idx = PD_IDX(virt);   break;
            default: idx = 0;
        }
        uint64_t entry = table[idx];
        if (!(entry & VMM_FLAG_PRESENT)) {
            if (!create) return NULL;
            uint64_t frame = alloc_pt_frame();
            table[idx] = frame | VMM_FLAG_PRESENT | VMM_FLAG_WRITE;
            table = (uint64_t *)frame;
        } else {
            table = (uint64_t *)(entry & PAGE_MASK);
        }
    }
    /* table now points to the PT; return pointer to the PTE */
    return &table[PT_IDX(virt)];
}

/* ---- Public API -------------------------------------------------- */

void vmm_init(void)
{
    current_pml4 = (uint64_t *)read_cr3();
}

void vmm_map(uint64_t virt, uint64_t phys, uint64_t flags)
{
    uint64_t *pte = vmm_get_pte(current_pml4, virt, true);
    *pte = (phys & PAGE_MASK) | flags | VMM_FLAG_PRESENT;
    /* Invalidate TLB entry */
    asm volatile("invlpg (%0)" :: "r"(virt) : "memory");
}

void vmm_unmap(uint64_t virt)
{
    uint64_t *pte = vmm_get_pte(current_pml4, virt, false);
    if (pte) {
        *pte = 0;
        asm volatile("invlpg (%0)" :: "r"(virt) : "memory");
    }
}

uint64_t vmm_get_phys(uint64_t virt)
{
    uint64_t *pte = vmm_get_pte(current_pml4, virt, false);
    if (!pte || !(*pte & VMM_FLAG_PRESENT)) return 0;
    return (*pte & PAGE_MASK) | (virt & (PAGE_SIZE - 1));
}

