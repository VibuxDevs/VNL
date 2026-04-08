#include "sched.h"
#include "idt.h"
#include "cpu.h"
#include "heap.h"
#include "timer.h"
#include "string.h"
#include "printf.h"
#include "panic.h"

/* ---- Task table -------------------------------------------------- */
static Task   tasks[MAX_TASKS];
static int    num_tasks    = 0;
static int    current_idx  = 0;
static uint32_t next_pid   = 1;

/* ---- Fake interrupt frame layout (26 × 8 bytes) ------------------
 * This is what PUSH_REGS + stub int_no/err_code + CPU hardware frame
 * leave on the stack. Must match sched.asm PUSH_REGS and isr_stubs.asm.
 *
 * Stack grows down. After PUSH_REGS the stack pointer points at:
 *   [rsp+0*8]  gs          (pushed last by PUSH_REGS)
 *   [rsp+1*8]  fs
 *   [rsp+2*8]  es
 *   [rsp+3*8]  ds
 *   [rsp+4*8]  r15
 *   ...
 *   [rsp+18*8] rax         (pushed first by PUSH_REGS)
 *   [rsp+19*8] int_no      (pushed by stub before PUSH_REGS)
 *   [rsp+20*8] err_code
 *   [rsp+21*8] rip         (CPU hardware)
 *   [rsp+22*8] cs
 *   [rsp+23*8] rflags
 *   [rsp+24*8] rsp
 *   [rsp+25*8] ss
 */
#define FRAME_WORDS 26

static uint64_t *build_fake_frame(uint8_t *stack_top, void (*entry)(void))
{
    uint64_t *frame = (uint64_t *)stack_top - FRAME_WORDS;

    /* Zero all regs */
    for (int i = 0; i < FRAME_WORDS; i++) frame[i] = 0;

    /* Segment registers (gs..ds at indices 0-3) */
    frame[0]  = 0x10;  /* gs */
    frame[1]  = 0x10;  /* fs */
    frame[2]  = 0x10;  /* es */
    frame[3]  = 0x10;  /* ds */
    /* GPRs 4-18 = zero */
    frame[19] = 0;                       /* int_no  */
    frame[20] = 0;                       /* err_code */
    frame[21] = (uint64_t)entry;         /* rip      */
    frame[22] = 0x08;                    /* cs       */
    frame[23] = 0x202;                   /* rflags: IF=1 + reserved bit 1 */
    frame[24] = (uint64_t)stack_top;     /* rsp (pre-interrupt) */
    frame[25] = 0x10;                    /* ss       */

    return frame;
}

/* ---- Round-robin picker ------------------------------------------ */
static uint64_t do_switch(uint64_t cur_rsp)
{
    /* Save current task's RSP */
    tasks[current_idx].rsp = cur_rsp;
    if (tasks[current_idx].state == TASK_RUNNING)
        tasks[current_idx].state = TASK_READY;

    /* Find next ready task */
    int next = -1;
    for (int i = 1; i <= num_tasks; i++) {
        int idx = (current_idx + i) % num_tasks;
        if (tasks[idx].state == TASK_READY) { next = idx; break; }
    }

    if (next < 0) {
        /* No one else ready — keep running current if it's not dead/sleeping */
        if (tasks[current_idx].state == TASK_READY) {
            tasks[current_idx].state = TASK_RUNNING;
        }
        return 0;   /* no switch */
    }

    tasks[next].state = TASK_RUNNING;
    current_idx = next;
    return tasks[next].rsp;
}

/* ---- Scheduler C handlers (called from sched.asm) ---------------- */
uint64_t sched_timer_c(Registers *r)
{
    tick_count++;
    outb(0x20, 0x20);   /* EOI first, before any RSP swap */

    /* Wake sleeping tasks */
    for (int i = 0; i < num_tasks; i++) {
        if (tasks[i].state == TASK_SLEEPING && tick_count >= tasks[i].sleep_until)
            tasks[i].state = TASK_READY;
    }

    return do_switch((uint64_t)r);
}

uint64_t sched_yield_c(Registers *r)
{
    /* Software interrupt — no EOI needed */
    return do_switch((uint64_t)r);
}

/* ---- Public API -------------------------------------------------- */
void sched_init(void)
{
    memset(tasks, 0, sizeof(tasks));

    /* Task 0 = the current boot/kernel thread */
    tasks[0].pid   = next_pid++;
    tasks[0].state = TASK_RUNNING;
    tasks[0].rsp   = 0;          /* filled in on first preemption */
    tasks[0].stack_base = NULL;  /* uses boot stack */
    strncpy(tasks[0].name, "kernel", sizeof(tasks[0].name) - 1);
    num_tasks = 1;
    current_idx = 0;

    /* Install scheduler stubs */
    extern void sched_timer_stub(void);
    extern void sched_yield_stub(void);
    idt_set_raw_handler(32,   (uint64_t)sched_timer_stub);
    idt_set_raw_handler(0x81, (uint64_t)sched_yield_stub);
}

int task_create(const char *name, void (*entry)(void))
{
    if (num_tasks >= MAX_TASKS) return -1;

    uint8_t *stack = (uint8_t *)kmalloc(TASK_STACK_SZ);
    if (!stack) return -1;

    Task *t = &tasks[num_tasks];
    t->stack_base = stack;
    t->pid   = next_pid++;
    t->state = TASK_READY;
    t->sleep_until = 0;
    strncpy(t->name, name, sizeof(t->name) - 1);

    uint64_t *frame = build_fake_frame(stack + TASK_STACK_SZ, entry);
    t->rsp = (uint64_t)frame;

    int pid = (int)t->pid;
    num_tasks++;
    return pid;
}

void task_exit(void)
{
    cli_asm();
    tasks[current_idx].state = TASK_DEAD;
    /* Yield — sched_yield_c will pick the next ready task */
    asm volatile("sti; int $0x81");
    /* If we somehow return (only task left), spin */
    while (1) asm volatile("hlt");
}

void task_sleep(uint64_t ms)
{
    cli_asm();
    tasks[current_idx].state      = TASK_SLEEPING;
    tasks[current_idx].sleep_until = tick_count + ms;
    asm volatile("sti; int $0x81");
}

void sched_yield(void)
{
    asm volatile("int $0x81");
}

Task *sched_current(void)           { return &tasks[current_idx]; }
Task *sched_get_task(int idx)       { return (idx < num_tasks) ? &tasks[idx] : NULL; }
int   sched_task_count(void)        { return num_tasks; }
