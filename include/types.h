#pragma once

typedef unsigned char      uint8_t;
typedef unsigned short     uint16_t;
typedef unsigned int       uint32_t;
typedef unsigned long long uint64_t;
typedef signed char        int8_t;
typedef signed short       int16_t;
typedef signed int         int32_t;
typedef signed long long   int64_t;
typedef uint64_t           size_t;
typedef int64_t            ssize_t;
typedef uint64_t           uintptr_t;
typedef int64_t            intptr_t;
typedef _Bool              bool;
#define true  1
#define false 0
#define NULL  ((void*)0)

#define PACKED      __attribute__((packed))
#define NORETURN    __attribute__((noreturn))
#define ALIGN(n)    __attribute__((aligned(n)))
#define UNUSED      __attribute__((unused))

/* Handy round-up / round-down macros */
#define ALIGN_UP(val, align)   (((val) + ((align)-1)) & ~((align)-1))
#define ALIGN_DOWN(val, align) ((val) & ~((align)-1))
#define PAGE_SIZE 4096ULL
