#pragma once
#include "types.h"

#define KERNEL_VMA_HIGH 0xFFFFFFFF80000000ULL

typedef struct {
    uint64_t addr;
    uint32_t pitch;
    uint32_t width;
    uint32_t height;
    uint8_t  bpp;
    uint8_t  fb_type;
} FBInfo;

bool fb_multiboot_init(uint64_t mb_info_phys);
bool fb_is_available(void);
void fb_get_info(FBInfo *out);
/* Linear framebuffer physical span (for mmap / DRI-style userspace mapping). */
bool fb_get_mmap_region(uint64_t *phys_start, uint64_t *byte_len,
                        uint32_t *line_length, uint32_t *width,
                        uint32_t *height, uint32_t *bpp);

void fb_fill_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color);
void fb_plot(uint32_t x, uint32_t y, uint32_t color);
void fb_draw_string(uint32_t x, uint32_t y, const char *s, uint32_t fg, uint32_t bg);

/* Mirror VGA-style text to the linear framebuffer (for VNC / GOP viewers). */
void fb_console_reset(void);
void fb_console_putchar(char c);
void fb_console_import_vga_buffer(const uint16_t *vram, int cursor_row, int cursor_col);
/* Keep FB text mirror logical cursor aligned with VGA after vga_set_cursor + redraw. */
void fb_console_mirror_cursor(int row, int col);
