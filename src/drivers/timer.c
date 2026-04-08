#include "timer.h"
#include "idt.h"
#include "cpu.h"

#define PIT_CH0   0x40
#define PIT_CMD   0x43
#define PIT_BASE  1193182UL

/* tick_count is incremented by sched_timer_c after sched_init.
   Before sched_init, the local timer_irq below handles it. */
volatile uint64_t tick_count = 0;

static void timer_irq(Registers *r)
{
    (void)r;
    tick_count++;
    outb(0x20, 0x20);
}

void timer_init(uint32_t hz)
{
    uint32_t divisor = (uint32_t)(PIT_BASE / hz);
    outb(PIT_CMD, 0x36);
    outb(PIT_CH0, (uint8_t)(divisor & 0xFF));
    outb(PIT_CH0, (uint8_t)(divisor >> 8));
    /* Install basic handler; sched_init() will replace this with
       sched_timer_stub which also increments tick_count. */
    idt_set_handler(32, timer_irq);
}

uint64_t timer_ticks(void)
{
    return tick_count;
}

/* Busy-wait sleep (uses tick counter; HZ must match timer_init arg) */
void timer_sleep(uint64_t ms)
{
    /* Assumes timer_init(1000) for 1 ms/tick resolution */
    uint64_t end = tick_count + ms;
    while (tick_count < end) {
        asm volatile("hlt");
    }
}
