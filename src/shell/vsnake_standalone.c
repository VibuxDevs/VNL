#include <stdint.h>
#include <stdbool.h>

/* VNL Syscalls for standalone snake */
#define SYS_WRITE 1
#define SYS_READ  0
#define SYS_EXIT  60

static long syscall3(long n, long a1, long a2, long a3) {
    long ret;
    asm volatile ("int $0x80" : "=a"(ret) : "a"(n), "D"(a1), "S"(a2), "d"(a3) : "rcx", "r11", "memory");
    return ret;
}

void _start() {
    const char *msg = "VNL SNAKE (ELF) STARTED.\n";
    syscall3(SYS_WRITE, 1, (long)msg, 25);
    
    /* Minimal loop */
    while(1) {
        char buf;
        if (syscall3(SYS_READ, 0, (long)&buf, 1) > 0) {
            if (buf == 'q') break;
        }
    }
    syscall3(SYS_EXIT, 0, 0, 0);
}
