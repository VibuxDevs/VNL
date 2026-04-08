/*
 * Syscalls: Linux x86_64 numbers where noted — required surface for Xorg, libc,
 * and Mesa/DRM (mmap of /dev/fb*, ioctl, eventually socket IPC).
 * Many are ENOSYS until userspace and networking exist.
 */
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
#include "errno.h"
#include "vmm.h"
#include "devfs.h"
#include "fb.h"
#include "unix_socket.h"
#include "heap.h"
#include "elf_load.h"
#include "uaccess.h"
#include "uspace.h"
#include "pmm.h"

static const char *uname_str = "VNL 0.2.0 (Vibe Not Linux) x86_64";

#ifndef ARCH_SET_FS
#define ARCH_SET_FS 0x1002
#endif
#define MSR_FS_BASE 0xC0000100

static void syscall_handler(Registers *r)
{
    uint64_t num  = r->rax;
    uint64_t arg1 = r->rdi;
    uint64_t arg2 = r->rsi;
    uint64_t arg3 = r->rdx;
    int64_t  ret  = -ENOSYS;

    switch (num) {
    case SYS_WRITE: {
        int fd         = (int)arg1;
        const char *buf = (const char *)arg2;
        size_t len     = (size_t)arg3;
        if (fd == 1 || fd == 2) {
            for (size_t i = 0; i < len; i++) vga_putchar(buf[i]);
            ret = (int64_t)len;
        } else if (unix_is_sockfd(fd)) {
            ret = unix_sock_write(fd, buf, len);
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
        } else if (unix_is_sockfd(fd)) {
            ret = unix_sock_read(fd, buf, len);
        } else {
            ret = vfs_read(fd, buf, len);
        }
        break;
    }
    case SYS_OPEN:
        ret = vfs_open((const char *)arg1, (int)arg2);
        break;
    case SYS_CLOSE: {
        int fd = (int)arg1;
        if (unix_is_sockfd(fd)) ret = unix_sock_close(fd);
        else ret = vfs_close(fd);
        break;
    }
    case SYS_IOCTL:
        ret = vfs_ioctl((int)arg1, arg2, (void *)arg3);
        break;
    case SYS_BRK: {
        Task *t = sched_current();
        uint64_t nb = arg1;
        if (t->brk_start == 0 && t->brk_end == 0) {
            ret = (nb == 0) ? 0 : (int64_t)-ENOMEM;
            break;
        }
        if (nb == 0) {
            ret = (int64_t)t->brk_end;
            break;
        }
        if (nb < t->brk_start) {
            ret = (int64_t)t->brk_end;
            break;
        }
        uint64_t old_pg = ALIGN_UP(t->brk_end, PAGE_SIZE);
        uint64_t new_pg = ALIGN_UP(nb, PAGE_SIZE);
        for (uint64_t v = old_pg; v < new_pg; v += PAGE_SIZE) {
            void *pg = pmm_alloc();
            if (!pg) {
                ret = (int64_t)t->brk_end;
                goto brk_done;
            }
            memset(pg, 0, PAGE_SIZE);
            vmm_map_in_pml4(t->cr3_phys, v, (uint64_t)pg,
                            VMM_FLAG_PRESENT | VMM_FLAG_WRITE | VMM_FLAG_USER);
        }
        t->brk_end = nb;
        ret = (int64_t)nb;
    brk_done:
        break;
    }
    case SYS_MMAP: {
        void  *addr = (void *)arg1;
        size_t len  = (size_t)arg2;
        int    prot = (int)arg3;
        int    flags = (int)r->r10;
        int    fd    = (int)r->r8;
        int64_t off  = (int64_t)r->r9;
        (void)prot;

        Task *t = sched_current();
        if (len == 0 || len > (64u * 1024u * 1024u)) {
            ret = -EINVAL;
            break;
        }
        size_t map_len = ALIGN_UP(len, PAGE_SIZE);
        uint64_t va;
        if (flags & VNL_MAP_FIXED)
            va = (uint64_t)addr & ~(PAGE_SIZE - 1ULL);
        else {
            va = t->mmap_next;
            t->mmap_next += map_len;
        }

        if (flags & VNL_MAP_ANONYMOUS) {
            if (off != 0) {
                ret = -EINVAL;
                break;
            }
            uint64_t cr3p = t->cr3_phys;
            size_t npages = map_len / PAGE_SIZE;
            int64_t aret = (int64_t)va;
            for (size_t i = 0; i < npages; i++) {
                void *pg = pmm_alloc();
                if (!pg) {
                    aret = -ENOMEM;
                    break;
                }
                memset(pg, 0, PAGE_SIZE);
                vmm_map_in_pml4(cr3p, va + i * PAGE_SIZE, (uint64_t)pg,
                                VMM_FLAG_PRESENT | VMM_FLAG_WRITE | VMM_FLAG_USER);
            }
            ret = aret;
            break;
        }

        if (fd < 0) {
            ret = -EBADF;
            break;
        }
        VFSNode *fn = vfs_node_from_fd(fd);
        if (!fn || fn->type != VFS_CHR || fn->dev_major != DEV_FB_MAJOR) {
            ret = -ENODEV;
            break;
        }
        uint64_t p0, ln;
        uint32_t ll, w, h, bpp;
        if (!fb_get_mmap_region(&p0, &ln, &ll, &w, &h, &bpp)) {
            ret = -ENODEV;
            break;
        }
        if (off != 0) {
            ret = -EINVAL;
            break;
        }
        uint64_t map_rest = ln;
        size_t file_map = len;
        if (file_map > map_rest) file_map = (size_t)map_rest;

        uint64_t cr3p = t->cr3_phys;
        size_t npages = ALIGN_UP(file_map, PAGE_SIZE) / PAGE_SIZE;
        for (size_t i = 0; i < npages; i++)
            vmm_map_in_pml4(cr3p, va + i * PAGE_SIZE, p0 + i * PAGE_SIZE,
                            VMM_FLAG_PRESENT | VMM_FLAG_WRITE | VMM_FLAG_USER);
        ret = (int64_t)va;
        break;
    }
    case SYS_MUNMAP:
        vmm_unmap_range_pml4(sched_current()->cr3_phys, arg1, (size_t)arg2);
        ret = 0;
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
        if (buf) {
            strncpy(buf, uname_str, 128);
            ret = 0;
        }
        break;
    }
    case SYS_UPTIME:
        ret = (int64_t)timer_ticks();
        break;
    case SYS_EXIT:
        task_exit();
        __builtin_unreachable();
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
    case SYS_SOCKET:
        ret = unix_socket((int)arg1, (int)arg2, (int)arg3);
        break;
    case SYS_SOCKETPAIR:
        ret = unix_socketpair((int)arg1, (int)arg2, (int)arg3, (int *)r->r10);
        break;
    case SYS_BIND:
        ret = unix_bind((int)arg1, (void *)arg2, (size_t)arg3);
        break;
    case SYS_LISTEN:
        ret = unix_listen((int)arg1, (int)arg2);
        break;
    case SYS_ACCEPT:
        ret = unix_accept((int)arg1, (void *)arg2, (size_t *)arg3);
        break;
    case SYS_CONNECT:
        ret = unix_connect((int)arg1, (void *)arg2, (size_t)arg3);
        break;
    case SYS_SENDTO:
        if (r->r8 != 0)
            ret = -ENOSYS;
        else if (unix_is_sockfd((int)arg1))
            ret = unix_sock_write((int)arg1, (void *)arg2, (size_t)arg3);
        else
            ret = vfs_write((int)arg1, (void *)arg2, (size_t)arg3);
        break;
    case SYS_RECVFROM:
        if (r->r8 != 0)
            ret = -ENOSYS;
        else if (unix_is_sockfd((int)arg1))
            ret = unix_sock_read((int)arg1, (void *)arg2, (size_t)arg3);
        else
            ret = vfs_read((int)arg1, (void *)arg2, (size_t)arg3);
        break;
    case SYS_EXECVE: {
        if (r->cs != 0x1B) {
            ret = -EPERM;
            break;
        }
        Task *t = sched_current();
        char path[256];
        if (copy_user_cstring(t->cr3_phys, (const void *)arg1, path, sizeof path) < 0) {
            ret = -EFAULT;
            break;
        }
        int fd = vfs_open(path, VFS_O_READ);
        if (fd < 0) {
            ret = -ENOENT;
            break;
        }
        char *buf = (char *)kmalloc(512 * 1024);
        if (!buf) {
            vfs_close(fd);
            ret = -ENOMEM;
            break;
        }
        int n = vfs_read(fd, buf, 512 * 1024);
        vfs_close(fd);
        if (n < 64) {
            kfree(buf);
            ret = -ENOEXEC;
            break;
        }
        uint64_t new_pml4 = uspace_create_pml4();
        if (!new_pml4) {
            kfree(buf);
            ret = -ENOMEM;
            break;
        }
        uint64_t entry, brk;
        if (elf_load_exec(new_pml4, buf, (size_t)n, &entry, &brk) < 0) {
            kfree(buf);
            ret = -ENOEXEC;
            break;
        }
        kfree(buf);
        bool stk_ok = true;
        for (size_t i = 0; i < VNL_USER_STACK_PAGES; i++) {
            void *pg = pmm_alloc();
            if (!pg) {
                stk_ok = false;
                break;
            }
            memset(pg, 0, PAGE_SIZE);
            vmm_map_in_pml4(new_pml4, VNL_USER_STACK_BASE + i * PAGE_SIZE, (uint64_t)pg,
                            VMM_FLAG_PRESENT | VMM_FLAG_WRITE | VMM_FLAG_USER);
        }
        if (!stk_ok) {
            ret = -ENOMEM;
            break;
        }

        uint64_t sp = VNL_USER_STACK_BASE + VNL_USER_STACK_PAGES * PAGE_SIZE - 0x40;
        uint64_t z = 0;
        vnl_copy_to_user_va(new_pml4, sp, &z, 8);
        vnl_copy_to_user_va(new_pml4, sp + 8, &z, 8);

        t->cr3_phys = new_pml4;
        t->brk_start = brk;
        t->brk_end = brk;
        t->mmap_next = 0x40000000ULL;
        write_cr3(new_pml4);

        r->rip = entry;
        r->rsp = sp;
        r->rdi = 0;
        r->rsi = sp + 8;
        ret = 0;
        break;
    }
    case SYS_GETUID:
    case SYS_GETEUID:
        ret = 0;
        break;
    case SYS_SETUID:
        ret = 0;
        break;
    case SYS_ARCH_PRCTL:
        if (arg1 == ARCH_SET_FS)
            wrmsr_q(MSR_FS_BASE, arg2);
        ret = (arg1 == ARCH_SET_FS) ? 0 : (int64_t)-EINVAL;
        break;
    case SYS_SET_TID_ADDRESS:
        ret = (int64_t)sched_current()->pid;
        break;
    case SYS_MPROTECT:
        ret = 0;
        break;
    /* ---- Still TODO for full glibc / legacy Xorg -------------------------------- */
    case SYS_FORK:
    case SYS_CLONE:
    case SYS_FCNTL:
    case SYS_FUTEX:
    case SYS_EPOLL_CREATE1:
    case SYS_PIPE:
    case SYS_DUP2:
    case SYS_RT_SIGACTION:
    case SYS_RT_SIGPROCMASK:
    case SYS_PRLIMIT64:
        ret = -ENOSYS;
        break;
    default:
        ret = -ENOSYS;
    }

    r->rax = (uint64_t)ret;
}

void syscall_init(void)
{
    idt_set_handler(0x80, syscall_handler);
    /* Allow `int 0x80` from ring 3 (until FAST_SYSCALL/SYSRET is wired). */
    idt_set_interrupt_dpl(0x80, 3);
}
