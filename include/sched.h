#pragma once
#include "types.h"
#include "idt.h"

#define MAX_TASKS     16
#define TASK_STACK_SZ (8 * 4096)   /* 32 KiB per task */

typedef enum {
    TASK_RUNNING  = 0,
    TASK_READY    = 1,
    TASK_SLEEPING = 2,
    TASK_DEAD     = 3,
} TaskState;

typedef struct {
    uint64_t    rsp;           /* saved RSP (points to saved Registers frame) */
    uint8_t    *stack_base;    /* heap allocation; NULL for task 0 (boot stack) */
    TaskState   state;
    uint32_t    pid;
    uint64_t    sleep_until;   /* tick target for TASK_SLEEPING */
    char        name[32];
} Task;

void    sched_init(void);
int     task_create(const char *name, void (*entry)(void));
void    task_exit(void)    __attribute__((noreturn));
void    task_sleep(uint64_t ms);
void    sched_yield(void);
Task   *sched_current(void);
Task   *sched_get_task(int idx);
int     sched_task_count(void);

/* Called from sched.asm stubs */
uint64_t sched_timer_c(Registers *r);
uint64_t sched_yield_c(Registers *r);
