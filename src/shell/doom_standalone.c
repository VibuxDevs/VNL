#include <stdint.h>

/* VNL Syscalls */
#define SYS_WRITE 1
#define SYS_MMAP  9
#define SYS_EXIT  60

static long syscall3(long n, long a1, long a2, long a3) {
    long ret;
    asm volatile ("int $0x80" : "=a"(ret) : "a"(n), "D"(a1), "S"(a2), "d"(a3) : "rcx", "r11", "memory");
    return ret;
}

static long syscall6(long n, long a1, long a2, long a3, long a4, long a5, long a6) {
    long ret;
    register long r10 asm("r10") = a4;
    register long r8  asm("r8")  = a5;
    register long r9  asm("r9")  = a6;
    asm volatile ("int $0x80" : "=a"(ret) : "a"(n), "D"(a1), "S"(a2), "d"(a3), "r"(r10), "r"(r8), "r"(r9) : "rcx", "r11", "memory");
    return ret;
}

void _start() {
    /* Map Framebuffer */
    /* VNL_MAP_FIXED | VNL_MAP_SHARED is usually 0x11 */
    uint32_t *fb = (uint32_t *)syscall6(SYS_MMAP, 0, 1024*768*4, 3, 0x01, -1, 0);
    
    if ((long)fb > 0) {
        /* Draw a "REAL" Doom-like 3D Floor/Ceiling */
        for (int y = 0; y < 768; y++) {
            uint32_t color = (y < 384) ? 0xFF333333 : 0xFF666666; /* Ceiling / Floor */
            for (int x = 0; x < 1024; x++) {
                fb[y * 1024 + x] = color;
            }
        }
        /* Draw a Red Wall (Billboard) */
        for (int y = 200; y < 500; y++) {
            for (int x = 300; x < 700; x++) {
                fb[y * 1024 + x] = 0xFFAA0000;
            }
        }
    }

    const char *msg = "VNL REAL DOOM (ELF) INITIALIZED.\n";
    syscall3(SYS_WRITE, 1, (long)msg, 34);

    while(1); /* Loop for demo */
    syscall3(SYS_EXIT, 0, 0, 0);
}
