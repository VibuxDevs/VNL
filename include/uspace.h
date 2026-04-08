#pragma once
#include "types.h"

/* Allocate empty PML4 with kernel high-half linked (entry 511 shared). */
uint64_t uspace_create_pml4(void);
