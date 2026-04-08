#pragma once

void kpanic(const char *fmt, ...) __attribute__((noreturn, format(printf, 1, 2)));

#define ASSERT(cond) \
    do { if (!(cond)) kpanic("Assertion failed: %s at %s:%d", #cond, __FILE__, __LINE__); } while(0)
