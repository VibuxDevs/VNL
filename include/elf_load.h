#pragma once
#include "types.h"

#define VNL_USER_STACK_BASE  0x000000007F800000ULL
#define VNL_USER_STACK_PAGES 48u

/* Map a static ET_EXEC ELF64 into a fresh page table root; returns heap max VA as *out_brk. */
int  elf_load_exec(uint64_t pml4_phys, const void *elf_data, size_t elf_len,
                   uint64_t *out_entry, uint64_t *out_brk);

/* Load /path from VFS, spawn ring-3 task; returns pid or negative errno. */
int vnl_spawn_elf_path(const char *path);

void vnl_copy_to_user_va(uint64_t pml4_phys, uint64_t dst_va, const void *src, size_t n);
