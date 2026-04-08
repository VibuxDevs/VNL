#pragma once
#include "types.h"

#define VMM_FLAG_PRESENT  (1ULL << 0)
#define VMM_FLAG_WRITE    (1ULL << 1)
#define VMM_FLAG_USER     (1ULL << 2)
#define VMM_FLAG_HUGE     (1ULL << 7)
#define VMM_FLAG_NX       (1ULL << 63)

void     vmm_init(void);
void     vmm_map(uint64_t virt, uint64_t phys, uint64_t flags);
void     vmm_unmap(uint64_t virt);
uint64_t vmm_get_phys(uint64_t virt);

/* Per-process page tables (physical pointer = CR3). Identity map < 4 GiB required. */
void     vmm_map_in_pml4(uint64_t pml4_phys, uint64_t virt, uint64_t phys, uint64_t flags);
void     vmm_unmap_range_pml4(uint64_t pml4_phys, uint64_t virt, size_t nbytes);
/* Walk pml4_phys; return physical address for virt, or 0 if unmapped. */
uint64_t vmm_lookup_phys_pml4(uint64_t pml4_phys, uint64_t virt);
