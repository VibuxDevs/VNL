#include "syscall.h"
#include "idt.h"
#include "vga.h"
#include "printf.h"
#include "timer.h"
#include "string.h"
#include "vfs.h"
#include "sched.h"
#include "cpu.h"
#include "keyboard.h"

static const char *uname_str = "VNL 0.2.0 (Vibe Not Linux) x86_64";

static void syscall_handler(Registers *r)
{
    uint64_t num  = r->rax;
    uint64_t arg1 = r->rdi;
    uint64_t arg2 = r->rsi;
    uint64_t arg3 = r->rdx;
    int64_t  ret  = -1;

    switch (num) {
        case SYS_WRITE: {
            int fd = (int)arg1;
            const char *buf = (const char *)arg2;
            size_t len = (size_t)arg3;
            if (fd == 1 || fd == 2) {
                for (size_t i = 0; i < len; i++) vga_putchar(buf[i]);
                ret = (int64_t)len;
            } else {
                ret = vfs_write(fd, buf, len);
            }
            break;
        }
        case SYS_READ: {
            int fd = (int)arg1;
            void *buf = (void *)arg2;
            size_t len = (size_t)arg3;
            if (fd == 0) {
                char c = keyboard_getchar();
                *(char *)buf = c;
                ret = 1;
            } else {
                ret = vfs_read(fd, buf, len);
            }
            break;
        }
        case SYS_OPEN: {
            const char *path = (const char *)arg1;
            int flags = (int)arg2;
            ret = vfs_open(path, flags);
            break;
        }
        case SYS_CLOSE:
            ret = vfs_close((int)arg1);
            break;
        case SYS_MKDIR:
            ret = vfs_mkdir((const char *)arg1);
            break;
        case SYS_UNLINK:
            ret = vfs_unlink((const char *)arg1);
            break;
        case SYS_GETPID:
            ret = (int64_t)sched_current()->pid;
            break;
        case SYS_UNAME: {
            char *buf = (char *)arg1;
            if (buf) { strncpy(buf, uname_str, 128); ret = 0; }
            break;
        }
        case SYS_UPTIME:
            ret = (int64_t)timer_ticks();
            break;
        case SYS_EXIT:
            task_exit();   /* noreturn */
        case SYS_TASK_CREATE: {
            void (*fn)(void) = (void (*)(void))arg1;
            const char *name = (const char *)arg2;
            ret = task_create(name, fn);
            break;
        }
        case SYS_TASK_SLEEP:
            task_sleep(arg1);
            ret = 0;
            break;
        default:
            ret = -38;  /* ENOSYS */
    }

    r->rax = (uint64_t)ret;
}

void syscall_init(void)
{
    idt_set_handler(0x80, syscall_handler);
}
