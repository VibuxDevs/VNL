#pragma once
#include "types.h"

void    serial_init(void);
void    serial_putchar(char c);
void    serial_puts(const char *s);
int     serial_received(void);
char    serial_getchar(void);
