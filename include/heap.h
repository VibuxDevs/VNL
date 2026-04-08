#pragma once
#include "types.h"

void  heap_init(uintptr_t base, size_t initial_size);
void *kmalloc(size_t size);
void *kcalloc(size_t n, size_t size);
void *krealloc(void *ptr, size_t size);
void  kfree(void *ptr);
