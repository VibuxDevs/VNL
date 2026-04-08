#pragma once
#include "types.h"

typedef enum {
    VGA_BLACK   = 0, VGA_BLUE       = 1, VGA_GREEN      = 2, VGA_CYAN  = 3,
    VGA_RED     = 4, VGA_MAGENTA    = 5, VGA_BROWN      = 6, VGA_LGRAY = 7,
    VGA_DGRAY   = 8, VGA_LBLUE      = 9, VGA_LGREEN     = 10,VGA_LCYAN = 11,
    VGA_LRED    = 12,VGA_LMAGENTA   = 13,VGA_YELLOW     = 14,VGA_WHITE = 15,
} VGAColor;

void vga_init(void);
void vga_clear(void);
void vga_set_color(VGAColor fg, VGAColor bg);
uint8_t vga_get_attr(void); /* (bg<<4)|fg for mirrors */
void vga_putchar(char c);
void vga_puts(const char *s);
void vga_set_cursor(int row, int col);
void vga_get_cursor(int *row, int *col);
int  vga_get_row(void);
int  vga_get_col(void);

/* Copy current VGA text cells into the linear fb mirror (call once after fb init). */
void vga_export_fb_mirror_once(void);
/* Re-import full 80x25 from VRAM (fixes drift / scroll glitches on VNC mirror). */
void vga_fb_mirror_refresh(void);
