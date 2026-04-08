#pragma once
#include "types.h"

void mouse_init(void);
bool mouse_poll(int *dx, int *dy, uint8_t *buttons); /* true if new packet */
void mouse_flush(void);
