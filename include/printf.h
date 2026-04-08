#pragma once
#include "types.h"

typedef __builtin_va_list va_list;
#define va_start __builtin_va_start
#define va_end   __builtin_va_end
#define va_arg   __builtin_va_arg

int  kvsnprintf(char *buf, size_t size, const char *fmt, va_list ap);
int  kvprintf(const char *fmt, va_list ap);
int  kprintf(const char *fmt, ...);
int  ksprintf(char *buf, size_t size, const char *fmt, ...);
void kputs(const char *s);

/* Output capture (used by sh pipe implementation) */
void   kout_redirect(char *buf, size_t cap);
void   kout_reset(void);
size_t kout_written(void);
