#include "fb.h"
#include "pmm.h"
#include "string.h"
#include "vga.h"

#define MB2_TAG_END 0
#define MB2_TAG_FB  8

typedef struct PACKED {
    uint32_t type;
    uint32_t size;
    uint64_t addr;
    uint32_t pitch;
    uint32_t width;
    uint32_t height;
    uint8_t  bpp;
    uint8_t  fb_type;
    uint8_t  reserved;
} MB2TagFB;

static bool     s_fb_ok;
static FBInfo   s_fb;
static uint8_t *s_fb_virt; /* identity VA == physical (PML4[0] maps 0–4 GiB) */

static bool parse_fb_tag(uint64_t mb_phys, FBInfo *info)
{
    uint8_t *ptr = (uint8_t *)mb_phys;
    uint32_t total = *(uint32_t *)ptr;
    uint8_t *end = ptr + total;
    ptr += 8;

    while (ptr < end) {
        uint32_t type = *(uint32_t *)ptr;
        uint32_t sz = *(uint32_t *)(ptr + 4);
        if (type == MB2_TAG_END) break;
        if (type == MB2_TAG_FB && sz >= sizeof(MB2TagFB)) {
            MB2TagFB *t = (MB2TagFB *)ptr;
            info->addr    = t->addr;
            info->pitch   = t->pitch;
            info->width   = t->width;
            info->height  = t->height;
            info->bpp     = t->bpp;
            info->fb_type = t->fb_type;
            return info->width > 0 && info->height > 0 && info->pitch > 0 && info->addr != 0;
        }
        ptr += ALIGN_UP(sz, 8);
    }
    return false;
}

bool fb_multiboot_init(uint64_t mb_info_phys)
{
    s_fb_ok = false;
    s_fb_virt = NULL;
    if (!mb_info_phys) return false;
    if (!parse_fb_tag(mb_info_phys, &s_fb)) return false;
    if (s_fb.fb_type != 1) return false; /* RGB only */
    if (s_fb.bpp != 32) return false;

    uint64_t nbytes = (uint64_t)s_fb.pitch * (uint64_t)s_fb.height;
    uint64_t p0 = ALIGN_DOWN(s_fb.addr, PAGE_SIZE);
    uint64_t p1 = ALIGN_UP(s_fb.addr + nbytes, PAGE_SIZE);

    /* High-half KERNEL_VMA+phys only aliases the first 1 GiB of RAM; GRUB's
     * framebuffer is often above that (e.g. 0xE0000000). Use identity VA. */
    if (s_fb.addr > 0xFFFFFFFFULL || p1 > (1ULL << 32)) {
        s_fb_virt = NULL;
        s_fb_ok = false;
        return false;
    }

    pmm_reserve(p0, p1 - p0);
    s_fb_virt = (uint8_t *)(uintptr_t)s_fb.addr;
    s_fb_ok = true;
    return true;
}

bool fb_is_available(void) { return s_fb_ok; }

void fb_get_info(FBInfo *out)
{
    if (out) *out = s_fb;
}

bool fb_get_mmap_region(uint64_t *phys_start, uint64_t *byte_len,
                        uint32_t *line_length, uint32_t *width,
                        uint32_t *height, uint32_t *bpp)
{
    if (!s_fb_ok) return false;
    if (phys_start) *phys_start = s_fb.addr;
    if (byte_len) *byte_len = (uint64_t)s_fb.pitch * (uint64_t)s_fb.height;
    if (line_length) *line_length = s_fb.pitch;
    if (width) *width = s_fb.width;
    if (height) *height = s_fb.height;
    if (bpp) *bpp = s_fb.bpp;
    return true;
}

void fb_plot(uint32_t x, uint32_t y, uint32_t c)
{
    if (!s_fb_ok || x >= s_fb.width || y >= s_fb.height) return;
    *(uint32_t *)(s_fb_virt + y * s_fb.pitch + x * 4) = c;
}

void fb_fill_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t c)
{
    if (!s_fb_ok || w == 0 || h == 0) return;
    if (x >= s_fb.width || y >= s_fb.height) return;
    if (x + w > s_fb.width)  w = s_fb.width - x;
    if (y + h > s_fb.height) h = s_fb.height - y;
    for (uint32_t row = 0; row < h; row++) {
        uint32_t *p = (uint32_t *)(s_fb_virt + (y + row) * s_fb.pitch + x * 4);
        for (uint32_t col = 0; col < w; col++) p[col] = c;
    }
}

/* 8x8 bitmaps: alnum + punctuation for shell prompt, paths, neofetch. */
#define GLYPH_NUM 65

static int glyph_index(char ch)
{
    if (ch == ' ') return 0;
    if (ch >= '0' && ch <= '9') return 1 + (ch - '0');
    if (ch >= 'A' && ch <= 'Z') return 11 + (ch - 'A');
    if (ch >= 'a' && ch <= 'z') return 11 + (ch - 'a');
    switch (ch) {
    case '/':  return 37;
    case '\\': return 38;
    case '-':  return 39;
    case ':':  return 40;
    case '@':  return 41;
    case '%':  return 42;
    case '#':  return 43;
    case '.':  return 44;
    case ',':  return 45;
    case '(':  return 46;
    case ')':  return 47;
    case '[':  return 48;
    case ']':  return 49;
    case '$':  return 50;
    case '_':  return 51;
    case '=':  return 52;
    case '*':  return 53;
    case '+':  return 54;
    case '!':  return 55;
    case '?':  return 56;
    case '>':  return 57;
    case '&':  return 58;
    case '\'': return 59;
    case '"':  return 60;
    case ';':  return 61;
    case '`':  return 62;
    case '~':  return 63;
    case '|':  return 64;
    default:   return 0;
    }
}

static void glyph_blit(uint32_t x, uint32_t y, int idx, uint32_t fg, uint32_t bg)
{
    static const uint8_t g[GLYPH_NUM][8] = {
        {0,0,0,0,0,0,0,0},
        {0x3C,0x66,0x6E,0x76,0x66,0x66,0x3C,0x00},
        /* '1' was same shape as 'C'; use a vertical “1” bar */
        {0x18,0x38,0x18,0x18,0x18,0x18,0x3C,0x00},
        {0x3C,0x66,0x06,0x3C,0x06,0x66,0x3C,0x00},
        {0x30,0x38,0x3C,0x36,0x7E,0x06,0x06,0x00},
        {0x7E,0x60,0x7C,0x06,0x06,0x46,0x3C,0x00},
        {0x1C,0x30,0x60,0x7C,0x66,0x66,0x3C,0x00},
        {0x7E,0x46,0x0C,0x18,0x18,0x18,0x18,0x00},
        {0x3C,0x66,0x66,0x3C,0x66,0x66,0x3C,0x00},
        {0x3C,0x66,0x66,0x3C,0x66,0x66,0x3C,0x00},
        {0x3C,0x66,0x66,0x3E,0x06,0x0C,0x38,0x00},
        {0x18,0x3C,0x66,0x66,0x7E,0x66,0x66,0x00},
        {0x7C,0x66,0x66,0x7C,0x66,0x66,0x7C,0x00},
        {0x3C,0x66,0x60,0x60,0x60,0x66,0x3C,0x00},
        {0x78,0x6C,0x66,0x66,0x66,0x6C,0x78,0x00},
        {0x7E,0x60,0x60,0x7C,0x60,0x60,0x7E,0x00},
        {0x7E,0x60,0x60,0x7C,0x60,0x60,0x60,0x00},
        {0x3C,0x66,0x60,0x6E,0x66,0x66,0x3C,0x00},
        {0x66,0x66,0x66,0x7E,0x66,0x66,0x66,0x00},
        {0x3C,0x18,0x18,0x18,0x18,0x18,0x3C,0x00},
        {0x1E,0x0C,0x0C,0x0C,0x4C,0x78,0x30,0x00},
        {0x66,0x6C,0x78,0x70,0x78,0x6C,0x66,0x00},
        {0x60,0x60,0x60,0x60,0x60,0x60,0x7E,0x00},
        {0x63,0x77,0x7F,0x6B,0x63,0x63,0x63,0x00},
        {0x66,0x76,0x7E,0x7E,0x6E,0x66,0x66,0x00},
        {0x3C,0x66,0x66,0x66,0x66,0x66,0x3C,0x00},
        {0x7C,0x66,0x66,0x7C,0x60,0x60,0x60,0x00},
        {0x3C,0x66,0x66,0x6A,0x6C,0x38,0x0E,0x00},
        {0x7C,0x66,0x66,0x7C,0x6C,0x66,0x66,0x00},
        {0x3C,0x66,0x60,0x3C,0x06,0x66,0x3C,0x00},
        {0x7E,0x18,0x18,0x18,0x18,0x18,0x18,0x00},
        {0x66,0x66,0x66,0x66,0x66,0x66,0x3C,0x00},
        {0x66,0x66,0x66,0x66,0x3C,0x3C,0x18,0x00},
        {0x63,0x63,0x63,0x6B,0x77,0x63,0x63,0x00},
        {0x66,0x66,0x3C,0x18,0x3C,0x66,0x66,0x00},
        {0x66,0x66,0x66,0x3C,0x18,0x18,0x18,0x00},
        {0x7E,0x06,0x0C,0x18,0x30,0x60,0x7E,0x00},
        /* / \\ - : @ % # . , ( ) */
        {0x02,0x04,0x08,0x10,0x20,0x40,0x80,0x00},
        {0x80,0x40,0x20,0x10,0x08,0x04,0x02,0x00},
        {0x00,0x00,0x00,0xFE,0xFE,0x00,0x00,0x00},
        {0x00,0x18,0x18,0x00,0x00,0x18,0x18,0x00},
        {0x78,0x87,0x89,0x8F,0x8F,0x89,0x86,0x78},
        {0x63,0x63,0x06,0x0C,0x18,0x33,0x63,0x00},
        {0x24,0x24,0xFF,0x24,0x24,0xFF,0x24,0x24},
        {0x00,0x00,0x00,0x00,0x00,0x18,0x00,0x00},
        {0x00,0x00,0x00,0x00,0x00,0x0C,0x06,0x00},
        {0x38,0x44,0x82,0x82,0x82,0x82,0x44,0x3C},
        {0x78,0x84,0x02,0x02,0x02,0x84,0x78,0x00},
        /* [ ] $ _ = * + ! ? > & ' " ; ` ~ */
        {0x7E,0x40,0x40,0x40,0x40,0x40,0x40,0x7E},
        {0x7E,0x02,0x02,0x02,0x02,0x02,0x02,0x7E},
        {0x18,0x24,0x22,0x38,0x0E,0x12,0x24,0x18},
        {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xFE},
        {0x00,0x00,0xFE,0x00,0x00,0xFE,0x00,0x00},
        {0x24,0x18,0x7E,0x18,0x24,0x00,0x00,0x00},
        {0x18,0x18,0x7E,0x18,0x18,0x00,0x00,0x00},
        {0x18,0x18,0x18,0x18,0x18,0x00,0x18,0x00},
        {0x3C,0x42,0x02,0x04,0x08,0x00,0x08,0x00},
        {0x00,0x60,0x18,0x06,0x18,0x60,0x00,0x00},
        {0x30,0x48,0x48,0x30,0x48,0x48,0x34,0x00},
        {0x18,0x18,0x10,0x00,0x00,0x00,0x00,0x00},
        {0x36,0x36,0x24,0x00,0x00,0x00,0x00,0x00},
        {0x00,0x18,0x18,0x00,0x00,0x18,0x18,0x10},
        {0x30,0x18,0x0C,0x00,0x00,0x00,0x00,0x00},
        {0x00,0x08,0x10,0x20,0x40,0x20,0x10,0x08},
        /* | (banner art, pipes) */
        {0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18},
    };
    if (idx < 0 || idx >= GLYPH_NUM) return;
    const uint8_t *gp = g[idx];
    for (int yy = 0; yy < 8; yy++) {
        uint8_t bits = gp[yy];
        for (int xx = 0; xx < 8; xx++) {
            fb_plot(x + xx, y + yy, (bits & (1 << (7 - xx))) ? fg : bg);
        }
    }
}

/* ---- Framebuffer console (mirrors 80×25 VGA for VNC / linear fb viewers) --------- */
#define FB_CON_PAD      0
#define FB_CON_TOP      0
#define FB_MIRROR_COLS  80
#define FB_MIRROR_ROWS  25

static int fb_m_row, fb_m_col;

/* 0xAARRGGBB for typical little-endian linear RGB framebuffers */
static uint32_t fb_vga16_rgb(uint8_t idx)
{
    idx &= 15;
    uint8_t r, g, b;
    switch (idx) {
    case 0:  r = 0;   g = 0;   b = 0;   break;
    case 1:  r = 0;   g = 0;   b = 170; break;
    case 2:  r = 0;   g = 170; b = 0;   break;
    case 3:  r = 0;   g = 170; b = 170; break;
    case 4:  r = 170; g = 0;   b = 0;   break;
    case 5:  r = 170; g = 0;   b = 170; break;
    case 6:  r = 170; g = 85;  b = 0;   break;
    case 7:  r = 170; g = 170; b = 170; break;
    case 8:  r = 85;  g = 85;  b = 85;  break;
    case 9:  r = 85;  g = 85;  b = 255; break;
    case 10: r = 85;  g = 255; b = 85;  break;
    case 11: r = 85;  g = 255; b = 255; break;
    case 12: r = 255; g = 85;  b = 85;  break;
    case 13: r = 255; g = 85;  b = 255; break;
    case 14: r = 255; g = 255; b = 85;  break;
    default: r = 255; g = 255; b = 255; break;
    }
    return 0xFF000000u | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}

static void fb_attr_to_colors(uint8_t attr, uint32_t *fg, uint32_t *bg)
{
    *fg = fb_vga16_rgb(attr & 0x0F);
    *bg = fb_vga16_rgb((attr >> 4) & 0x0F);
}

static void fb_mirror_cur_colors(uint32_t *fg, uint32_t *bg)
{
    fb_attr_to_colors(vga_get_attr(), fg, bg);
}

static void fb_draw_gchar(uint32_t px, uint32_t py, char ch, uint32_t fg, uint32_t bg)
{
    glyph_blit(px, py, glyph_index(ch), fg, bg);
}

static void fb_console_scroll_gfx_one_line(void)
{
    if (!s_fb_ok) return;
    uint32_t top = FB_CON_TOP;
    uint32_t left = FB_CON_PAD;
    uint32_t text_w = FB_MIRROR_COLS * 8;
    if (left + text_w > s_fb.width)
        text_w = s_fb.width - left;
    uint32_t bot = s_fb.height;
    uint32_t line = 8;
    uint32_t text_h = (bot > top) ? (bot - top) : 0;
    if (text_h <= line) return;

    uint8_t *base = s_fb_virt + top * s_fb.pitch + left * 4;
    size_t span = (size_t)text_w * 4;
    uint32_t move_h = text_h - line;
    for (uint32_t r = 0; r < move_h; r++)
        memmove(base + (size_t)r * s_fb.pitch, base + (size_t)(r + line) * s_fb.pitch, span);
    uint32_t fg, bg;
    fb_mirror_cur_colors(&fg, &bg);
    fb_fill_rect(left, bot - line, text_w, line, bg);
}

void fb_console_mirror_cursor(int row, int col)
{
    if (!s_fb_ok) return;
    if (row < 0)
        row = 0;
    else if (row >= FB_MIRROR_ROWS)
        row = FB_MIRROR_ROWS - 1;
    if (col < 0)
        col = 0;
    else if (col >= FB_MIRROR_COLS)
        col = FB_MIRROR_COLS - 1;
    fb_m_row = row;
    fb_m_col = col;
}

void fb_console_reset(void)
{
    fb_m_row = fb_m_col = 0;
    if (!s_fb_ok) return;
    uint32_t fg, bg;
    fb_mirror_cur_colors(&fg, &bg);
    if (FB_CON_TOP < s_fb.height)
        fb_fill_rect(0, FB_CON_TOP, s_fb.width, s_fb.height - FB_CON_TOP, bg);
}

void fb_console_import_vga_buffer(const uint16_t *vram, int cursor_row, int cursor_col)
{
    if (!s_fb_ok || !vram) return;
    fb_m_row = cursor_row;
    if (fb_m_row >= FB_MIRROR_ROWS)
        fb_m_row = FB_MIRROR_ROWS - 1;
    if (fb_m_row < 0) fb_m_row = 0;
    fb_m_col = cursor_col;
    if (fb_m_col >= FB_MIRROR_COLS)
        fb_m_col = FB_MIRROR_COLS - 1;
    if (fb_m_col < 0) fb_m_col = 0;

    uint32_t x0 = FB_CON_PAD;
    uint32_t y0 = FB_CON_TOP;
    for (int r = 0; r < FB_MIRROR_ROWS; r++) {
        for (int c = 0; c < FB_MIRROR_COLS; c++) {
            uint16_t cell = vram[r * FB_MIRROR_COLS + c];
            char ch = (char)(cell & 0xFF);
            uint32_t fg, bg;
            fb_attr_to_colors((uint8_t)(cell >> 8), &fg, &bg);
            fb_draw_gchar(x0 + (uint32_t)c * 8, y0 + (uint32_t)r * 8, ch, fg, bg);
        }
    }
}

void fb_console_putchar(char c)
{
    if (!s_fb_ok) return;
    uint32_t x0 = FB_CON_PAD;
    uint32_t y0 = FB_CON_TOP;
    uint32_t fg, bg;
    fb_mirror_cur_colors(&fg, &bg);

    if (c == '\n') {
        fb_m_col = 0;
        fb_m_row++;
    } else if (c == '\r') {
        fb_m_col = 0;
    } else if (c == '\b') {
        if (fb_m_col > 0) {
            fb_m_col--;
            fb_draw_gchar(x0 + (uint32_t)fb_m_col * 8, y0 + (uint32_t)fb_m_row * 8,
                          ' ', fg, bg);
        }
    } else if (c == '\t') {
        fb_m_col = (fb_m_col + 8) & ~7;
        if (fb_m_col >= FB_MIRROR_COLS) {
            fb_m_col = 0;
            fb_m_row++;
        }
    } else {
        fb_draw_gchar(x0 + (uint32_t)fb_m_col * 8, y0 + (uint32_t)fb_m_row * 8,
                      c, fg, bg);
        fb_m_col++;
        if (fb_m_col >= FB_MIRROR_COLS) {
            fb_m_col = 0;
            fb_m_row++;
        }
    }

    while (fb_m_row >= FB_MIRROR_ROWS) {
        fb_console_scroll_gfx_one_line();
        fb_m_row--;
    }
}

void fb_draw_string(uint32_t x, uint32_t y, const char *s, uint32_t fg, uint32_t bg)
{
    if (!s || !s_fb_ok) return;
    uint32_t cx = x;
    while (*s) {
        glyph_blit(cx, y, glyph_index(*s), fg, bg);
        cx += 8;
        s++;
    }
}
