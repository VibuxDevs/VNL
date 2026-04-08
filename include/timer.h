#pragma once
#include "types.h"

extern volatile uint64_t tick_count;

void     timer_init(uint32_t hz);
uint64_t timer_ticks(void);
void     timer_sleep(uint64_t ms);
