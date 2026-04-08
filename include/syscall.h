#pragma once
#include "types.h"
#include "idt.h"

/* VNL syscall numbers */
#define SYS_WRITE       1
#define SYS_READ        2
#define SYS_OPEN        3
#define SYS_CLOSE       4
#define SYS_MKDIR       7
#define SYS_UNLINK      8
#define SYS_GETPID      39
#define SYS_EXIT        60
#define SYS_UNAME       63
#define SYS_UPTIME      100
#define SYS_TASK_CREATE 101
#define SYS_TASK_SLEEP  103

void syscall_init(void);
