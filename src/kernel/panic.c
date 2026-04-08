#include "panic.h"
#include "printf.h"
#include "vga.h"
#include "serial.h"
#include "cpu.h"

static void panic_puts(const char *s)
{
    vga_puts(s);
    serial_puts(s);
}

void kpanic(const char *fmt, ...)
{
    cli_asm();
    vga_set_color(VGA_WHITE, VGA_RED);
    panic_puts("\n\n*************************************\n");
    panic_puts("*        VNL  KERNEL  PANIC         *\n");
    panic_puts("*************************************\n");

    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    kvsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    panic_puts(buf);

    panic_puts("\n\nSystem halted. Please reboot.\n");
    halt_loop();
}
