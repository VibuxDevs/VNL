#pragma once
#include "types.h"

void     pmm_init(uint64_t mem_upper_kb);
void     pmm_reserve(uint64_t base, uint64_t len);
void    *pmm_alloc(void);        /* returns one 4 KiB physical frame */
void     pmm_free(void *frame);
uint64_t pmm_free_pages(void);
uint64_t pmm_total_pages(void);
