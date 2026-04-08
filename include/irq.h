#pragma once
#include "types.h"

void irq_init(void);
void irq_eoi(uint8_t irq);
void irq_mask(uint8_t irq);
void irq_unmask(uint8_t irq);
