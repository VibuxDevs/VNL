#pragma once
#include "types.h"

/* Copy NUL-terminated string from user VA under cr3_phys into kernel buffer. */
int copy_user_cstring(uint64_t cr3_phys, const void *user_ptr, char *out, size_t out_cap);
