#pragma once
#include "types.h"
#include "idt.h"

/* VNL legacy numbers (1–8) for early syscalls; 9+ align with Linux where useful. */
#define SYS_WRITE        1
#define SYS_READ         2
#define SYS_OPEN         3
#define SYS_CLOSE        4
#define SYS_MKDIR        7
#define SYS_UNLINK       8

/* Linux x86_64 — mmap/ioctl path toward Xorg / DRI */
#define SYS_MMAP         9
#define SYS_MPROTECT    10
#define SYS_MUNMAP      11
#define SYS_IOCTL       16
#define SYS_PIPE        22
#define SYS_DUP2        33
#define SYS_SOCKET      41
#define SYS_CONNECT     42
#define SYS_ACCEPT      43
#define SYS_SOCKETPAIR  53
#define SYS_SENDTO      44
#define SYS_RECVFROM    45
#define SYS_BIND        49
#define SYS_LISTEN      50
#define SYS_CLONE       56
#define SYS_FORK        57
#define SYS_EXECVE      59
#define SYS_EXIT        60
#define SYS_UNAME       63
#define SYS_FCNTL       72
#define SYS_GETUID     102
#define SYS_SETUID     105
#define SYS_GETEUID    107
#define SYS_ARCH_PRCTL 158
#define SYS_FUTEX      202
#define SYS_SET_TID_ADDRESS 218
#define SYS_EPOLL_CREATE1 291
#define SYS_PRLIMIT64  302

#define SYS_GETPID      39
#define SYS_RT_SIGACTION 13
#define SYS_RT_SIGPROCMASK 14
#define SYS_BRK         12

#define SYS_UPTIME      100
#define SYS_TASK_CREATE 101
#define SYS_TASK_SLEEP  103

#define VNL_MAP_SHARED     0x01
#define VNL_MAP_PRIVATE    0x02
#define VNL_MAP_FIXED      0x10
#define VNL_MAP_ANONYMOUS  0x20

void syscall_init(void);
