#include "vga.h"
#include "string.h"
#include "cpu.h"

#define VGA_WIDTH   80
#define VGA_HEIGHT  25
#define VGA_MEM     ((uint16_t *)0xFFFFFFFF800B8000ULL)

/* VGA CRTC registers */
#define VGA_CTRL 0x3D4
#define VGA_DATA 0x3D5

static int cur_row, cur_col;
static uint8_t cur_color;

static inline uint16_t make_entry(char c, uint8_t color)
{
    return (uint16_t)c | ((uint16_t)color << 8);
}

static void update_hw_cursor(void)
{
    uint16_t pos = (uint16_t)(cur_row * VGA_WIDTH + cur_col);
    outb(VGA_CTRL, 0x0F);
    outb(VGA_DATA, (uint8_t)(pos & 0xFF));
    outb(VGA_CTRL, 0x0E);
    outb(VGA_DATA, (uint8_t)(pos >> 8));
}

void vga_set_color(VGAColor fg, VGAColor bg)
{
    cur_color = (uint8_t)((bg << 4) | (fg & 0x0F));
}

void vga_set_cursor(int row, int col)
{
    cur_row = row;
    cur_col = col;
    update_hw_cursor();
}

void vga_get_cursor(int *row, int *col)
{
    *row = cur_row;
    *col = cur_col;
}

int vga_get_row(void) { return cur_row; }
int vga_get_col(void) { return cur_col; }

void vga_clear(void)
{
    for (int i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++)
        VGA_MEM[i] = make_entry(' ', cur_color);
    cur_row = cur_col = 0;
    update_hw_cursor();
}

static void scroll(void)
{
    for (int r = 0; r < VGA_HEIGHT - 1; r++)
        for (int c = 0; c < VGA_WIDTH; c++)
            VGA_MEM[r * VGA_WIDTH + c] = VGA_MEM[(r + 1) * VGA_WIDTH + c];
    for (int c = 0; c < VGA_WIDTH; c++)
        VGA_MEM[(VGA_HEIGHT - 1) * VGA_WIDTH + c] = make_entry(' ', cur_color);
    cur_row = VGA_HEIGHT - 1;
}

void vga_putchar(char c)
{
    if (c == '\n') {
        cur_col = 0;
        cur_row++;
    } else if (c == '\r') {
        cur_col = 0;
    } else if (c == '\t') {
        cur_col = (cur_col + 8) & ~7;
        if (cur_col >= VGA_WIDTH) { cur_col = 0; cur_row++; }
    } else if (c == '\b') {
        if (cur_col > 0) {
            cur_col--;
            VGA_MEM[cur_row * VGA_WIDTH + cur_col] = make_entry(' ', cur_color);
        }
    } else {
        VGA_MEM[cur_row * VGA_WIDTH + cur_col] = make_entry(c, cur_color);
        cur_col++;
        if (cur_col >= VGA_WIDTH) { cur_col = 0; cur_row++; }
    }
    if (cur_row >= VGA_HEIGHT) scroll();
    update_hw_cursor();
}

void vga_puts(const char *s)
{
    while (*s) vga_putchar(*s++);
}

void vga_init(void)
{
    cur_color = 0;
    vga_set_color(VGA_WHITE, VGA_BLACK);
    vga_clear();
}
