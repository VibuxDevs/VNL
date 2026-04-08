#pragma once
#include "types.h"

/* Special key codes (> 127, returned by keyboard_getkey/keyboard_poll) */
#define KEY_UP    0x80
#define KEY_DOWN  0x81
#define KEY_LEFT  0x82
#define KEY_RIGHT 0x83
#define KEY_HOME  0x84
#define KEY_END   0x85
#define KEY_DEL   0x86
#define KEY_PGUP  0x87
#define KEY_PGDN  0x88

void keyboard_init(void);
int  keyboard_getkey(void);   /* blocks; returns char (0-127) or KEY_* */
int  keyboard_poll(void);     /* non-blocking; returns char/KEY_* or -1 */
char keyboard_getchar(void);  /* blocks; printable chars only */
