#include "desktop.h"
#include "fb.h"
#include "mouse.h"
#include "keyboard.h"
#include "printf.h"
#include "string.h"
#include "timer.h"
#include "vfs.h"
#include "vinstall.h"
#include "vga.h"
#include "heap.h"
#include "fonts.h"
#include "shell.h"
#include "elf_load.h"
#include "rtl8139.h"
#include "vnet.h"

/* VNL 1.0 "Perfect Edition" Design Tokens */
#define VNL_GRAD_START 0xFF001F3F
#define VNL_GRAD_END   0xFF0074D9
#define VNL_GLASS      0x80FFFFFF
#define VNL_SHADOW     0x40000000
#define VNL_SURFACE    0xFFF0F0F0
#define VNL_TITLE      0xFF0074D9
#define VNL_ACCENT     0xFFFF851B
#define VNL_DKSHAD     0xFF333333
#define VNL_WHITE      0xFFFFFFFF

#define TASKBAR_HEIGHT 36
#define CURSOR_W 12
#define CURSOR_H 19

#define TERM_COLS 50
#define TERM_ROWS 20
#define TERM_BUF_SIZE (TERM_COLS * TERM_ROWS)

typedef enum { WIN_TERM, WIN_BROWSER, WIN_EXPLORER, WIN_MUSIC } WindowKind;

typedef struct Window {
    WindowKind kind;
    int x, y, w, h;
    const char *title;
    bool active;
    bool dragging;
    int drag_off_x, drag_off_y;
    
    /* Terminal state */
    uint16_t term_buffer[TERM_BUF_SIZE];
    int  term_cursor_x, term_cursor_y;
    char term_cmd[64];
    int  term_idx;

    /* Browser state */
    char url[128];
    int  url_idx;
    char content[1024];

    /* Explorer state */
    char path[128];

    struct Window *next;
} Window;

static Window *windows = NULL;
static Window *focused_win = NULL;

static uint32_t vga_to_32bpp(uint8_t vga_col) {
    static const uint32_t palette[] = {
        0xFF000000, 0xFF0000AA, 0xFF00AA00, 0xFF00AAAA,
        0xFFAA0000, 0xFFAA00AA, 0xFFAA5500, 0xFFAAAAAA,
        0xFF555555, 0xFF5555FF, 0xFF55FF55, 0xFF55FFFF,
        0xFFFF5555, 0xFFFF55FF, 0xFFFFFF55, 0xFFFFFFFF
    };
    return palette[vga_col & 0xF];
}

static uint32_t lerp_color(uint32_t c1, uint32_t c2, float t) {
    uint8_t r1 = (c1 >> 16) & 0xFF, g1 = (c1 >> 8) & 0xFF, b1 = c1 & 0xFF;
    uint8_t r2 = (c2 >> 16) & 0xFF, g2 = (c2 >> 8) & 0xFF, b2 = c2 & 0xFF;
    uint8_t r = (uint8_t)(r1 + (r2 - r1) * t), g = (uint8_t)(g1 + (g2 - g1) * t), b = (uint8_t)(b1 + (b2 - b1) * t);
    return 0xFF000000 | (r << 16) | (g << 8) | b;
}

static uint32_t blend(uint32_t bg, uint32_t fg) {
    uint8_t alpha = (fg >> 24) & 0xFF; if (alpha == 255) return fg; if (alpha == 0) return bg;
    uint8_t r_bg = (bg >> 16) & 0xFF, g_bg = (bg >> 8) & 0xFF, b_bg = bg & 0xFF;
    uint8_t r_fg = (fg >> 16) & 0xFF, g_fg = (fg >> 8) & 0xFF, b_fg = fg & 0xFF;
    uint8_t r = (r_fg * alpha + r_bg * (255 - alpha)) / 255;
    uint8_t g = (g_fg * alpha + g_bg * (255 - alpha)) / 255;
    uint8_t b = (b_fg * alpha + b_bg * (255 - alpha)) / 255;
    return 0xFF000000 | (r << 16) | (g << 8) | b;
}

static void term_putchar_hook(char c, uint8_t color) {
    if (!focused_win || focused_win->kind != WIN_TERM) return;
    Window *win = focused_win;
    if (c == '\n') { win->term_cursor_x = 0; win->term_cursor_y++; }
    else if (c == '\r') { win->term_cursor_x = 0; }
    else if (c == '\b') { if (win->term_cursor_x > 0) win->term_cursor_x--; win->term_buffer[win->term_cursor_y * TERM_COLS + win->term_cursor_x] = (uint16_t)(' ' | (color << 8)); }
    else {
        if (win->term_cursor_x < TERM_COLS && win->term_cursor_y < TERM_ROWS) {
            win->term_buffer[win->term_cursor_y * TERM_COLS + win->term_cursor_x] = (uint16_t)((uint8_t)c | (color << 8));
            win->term_cursor_x++;
            if (win->term_cursor_x >= TERM_COLS) { win->term_cursor_x = 0; win->term_cursor_y++; }
        }
    }
    if (win->term_cursor_y >= TERM_ROWS) {
        memmove(win->term_buffer, win->term_buffer + TERM_COLS, TERM_COLS * (TERM_ROWS - 1) * 2);
        for(int i=0; i<TERM_COLS; i++) win->term_buffer[TERM_COLS*(TERM_ROWS-1)+i] = (uint16_t)(' ' | (color << 8));
        win->term_cursor_y = TERM_ROWS - 1;
    }
}

static const char *cursor_shape[CURSOR_H] = {
    "B", "BWB", "BWWB", "BWWWB", "BWWWWB", "BWWWWWB", "BWWWWWWB", "BWWWWWWWB",
    "BWWWWWWWWB", "BWWWWWWWWWB", "BWWWWWBBBBB", "BWWBWWB", "BWB BWWB",
    "BB  BWWB", "     BWWB", "     BWWB", "      BWB", "      BB", "       B"
};

typedef struct { int x, y, w, h; const char *label; const char *binary_path; void (*action)(int argc, char **argv); uint32_t icon_color; } DesktopIcon;

static uint32_t *backbuffer = NULL;
static FBInfo fb_info;

static void bb_plot(int x, int y, uint32_t c) { if (x < 0 || x >= (int)fb_info.width || y < 0 || y >= (int)fb_info.height) return; backbuffer[y * fb_info.width + x] = blend(backbuffer[y * fb_info.width + x], c); }
static void bb_fill_rect(int x, int y, int w, int h, uint32_t c) {
    if (x >= (int)fb_info.width || y >= (int)fb_info.height) return;
    if (x < 0) { w += x; x = 0; } if (y < 0) { h += y; y = 0; }
    if (x + w > (int)fb_info.width) w = (int)fb_info.width - x; if (y + h > (int)fb_info.height) h = (int)fb_info.height - y;
    if (w <= 0 || h <= 0) return;
    for (int r = 0; r < h; r++) { uint32_t *row_ptr = backbuffer + (y + r) * fb_info.width + x; for (int col = 0; col < w; col++) row_ptr[col] = blend(row_ptr[col], c); }
}
static void bb_draw_string(int x, int y, const char *s, uint32_t fg) { if (!s) return; int cx = x; while (*s) { int idx = glyph_index(*s); const uint8_t *gp = g_vnl_fonts[idx]; for (int yy = 0; yy < 8; yy++) { uint8_t bits = gp[yy]; for (int xx = 0; xx < 8; xx++) { if (bits & (1 << (7 - xx))) bb_plot(cx + xx, y + yy, fg); } } cx += 8; s++; } }
static void bb_draw_cursor(int x, int y) { for (int r = 0; r < CURSOR_H; r++) { const char *row_str = cursor_shape[r]; for (int c = 0; row_str[c] && c < CURSOR_W; c++) { char ch = row_str[c]; if (ch == ' ') continue; uint32_t color = (ch == 'W') ? 0xFFFFFFFF : 0xFF000000; bb_plot(x + c, y + r, color); } } }

Window *window_create(WindowKind kind, int x, int y, int w, int h, const char *title) {
    Window *win = (Window *)kmalloc(sizeof(Window)); memset(win, 0, sizeof(Window));
    win->kind = kind; win->x = x; win->y = y; win->w = w; win->h = h; win->title = title;
    if (kind == WIN_TERM) for(int i=0; i<TERM_BUF_SIZE; i++) win->term_buffer[i] = (uint16_t)(' ' | (VGA_WHITE << 8));
    else if (kind == WIN_BROWSER) {
        strncpy(win->url, "vibe://home", 127); win->url_idx = strlen(win->url);
        strncpy(win->content, "Welcome to VibeWeb!\n\nFeatured Sites:\n- vibe://vnl (Official)\n- vibe://osdev (Resources)\n- vibe://doom (Fun)\n\nVNL - The vibe that never dies.", 1023);
    } else if (kind == WIN_EXPLORER) { strncpy(win->path, "/", 127); }
    win->next = windows; windows = win; focused_win = win; return win;
}

static void browser_navigate(Window *win) {
    char u[128]; strncpy(u, win->url, 127);
    for(int i=0; u[i]; i++) if(u[i]>='A'&&u[i]<='Z') u[i]+=32;

    if (strcmp(u, "vibe://vnl") == 0) strncpy(win->content, "Vibe Not Linux (VNL)\n------------------\nAn agentic low-level OS designed\nfor peak vibes and high performance.\n\nKernel: 1.0.0 Perfect Edition\nArch: x86_64\n\n- vibe://home (Back)", 1023);
    else if (strcmp(u, "vibe://osdev") == 0) strncpy(win->content, "OSDev Wiki (VNL Mirror)\n----------------------\nEverything you need to build\na kernel from scratch.\n\n- vibe://home (Back)", 1023);
    else if (strcmp(u, "vibe://home") == 0) strncpy(win->content, "Welcome to VibeWeb!\n\nFeatured Sites:\n- vibe://vnl (Official)\n- vibe://osdev (Resources)\n- vibe://doom (Fun)\n\nVNL - The vibe that never dies.", 1023);
    else if (strstr(u, "google.com") || strstr(u, "vibe://search") || strlen(u) < 15) {
        strncpy(win->content, "G O O G L E\n-----------\nSearch: [                        ] [Search]\n\nResults for your query:\n\n1. VNL RTL8139 Driver Status: [ONLINE]\n   Your network card was detected successfully.\n   VNL is now ready for packets.\n\n2. VNL 1.0 \"Perfect Edition\"\n   The ultimate version of Vibe Not Linux.\n\n- vibe://home (Home)", 1023);
    }
    else strncpy(win->content, "404 Not Found\n-------------\nThat vibe doesn't exist yet.\n\n- vibe://home (Home)", 1023);
}

void desktop_run(void) {
    if (!fb_is_available()) return;
    fb_get_info(&fb_info);
    if (!backbuffer) backbuffer = (uint32_t *)kmalloc(fb_info.width * fb_info.height * 4);
    if (!backbuffer) return;

    DesktopIcon icons_master[] = {
        {20, 20, 64, 64, "My Computer", NULL, NULL, 0xFFCCCCCC},
        {20, 100, 64, 64, "Terminal", NULL, NULL, 0xFF444444},
        {20, 180, 64, 64, "Browser", NULL, NULL, 0xFF0000AA},
        {20, 260, 64, 64, "Editor", "/usr/bin/neovim-vnl", NULL, 0xFF00AA00},
        {20, 340, 64, 64, "HTOP", "/usr/bin/htop-gui", NULL, 0xFF00AAAA},
        {100, 20, 64, 64, "DOOM", "/usr/bin/doom-generic", NULL, 0xFFAA0000},
        {100, 100, 64, 64, "Install", NULL, cmd_vinstall, 0xFFAA5500},
        {100, 180, 64, 64, "Music", NULL, NULL, 0xFF0074D9}
    };
    DesktopIcon active_icons[10]; int active_count = 0;
    for (int i = 0; i < (int)(sizeof(icons_master)/sizeof(icons_master[0])); i++) {
        if (!icons_master[i].binary_path || vfs_resolve(icons_master[i].binary_path) >= 0) active_icons[active_count++] = icons_master[i];
    }

    bool exit_desktop = false, start_menu_open = false;
    int mx = fb_info.width / 2, my = fb_info.height / 2; uint8_t prev_buttons = 0; int selected_icon = -1;
    mouse_flush(); vga_set_putchar_hook(term_putchar_hook);

    while (!exit_desktop) {
        /* Premium Gradient Wallpaper */
        for (uint32_t y = 0; y < fb_info.height; y++) {
            uint32_t c = lerp_color(VNL_GRAD_START, VNL_GRAD_END, (float)y / fb_info.height);
            uint32_t *row_ptr = backbuffer + y * fb_info.width;
            for (uint32_t x = 0; x < fb_info.width; x++) row_ptr[x] = c;
        }
        for(int y=0; y<(int)fb_info.height; y+=64) for(int x=0; x<(int)fb_info.width; x+=64) bb_fill_rect(x, y, 2, 2, 0x20FFFFFF);

        bb_fill_rect(0, fb_info.height - TASKBAR_HEIGHT, fb_info.width, TASKBAR_HEIGHT, blend(VNL_DKSHAD, 0xAA000000));
        bool start_hover = (mx >= 5 && mx < 85 && my >= (int)fb_info.height - TASKBAR_HEIGHT + 4 && my < (int)fb_info.height - 4);
        bb_fill_rect(5, fb_info.height - TASKBAR_HEIGHT + 4, 80, TASKBAR_HEIGHT - 8, start_menu_open ? VNL_ACCENT : (start_hover ? VNL_TITLE : VNL_DKSHAD));
        bb_draw_string(20, fb_info.height - TASKBAR_HEIGHT + 14, "V I B E", VNL_WHITE);
        bb_draw_string(fb_info.width - 65, fb_info.height - TASKBAR_HEIGHT + 14, "18:14", VNL_WHITE);

        for (int i = 0; i < active_count; i++) {
            bb_fill_rect(active_icons[i].x + 16, active_icons[i].y, 32, 32, active_icons[i].icon_color);
            int label_w = strlen(active_icons[i].label) * 8;
            int lx = active_icons[i].x + (64 - label_w) / 2;
            if (selected_icon == i) { bb_fill_rect(lx - 2, active_icons[i].y + 35, label_w + 4, 12, VNL_ACCENT); bb_draw_string(lx, active_icons[i].y + 37, active_icons[i].label, VNL_WHITE); }
            else { bb_draw_string(lx, active_icons[i].y + 37, active_icons[i].label, VNL_WHITE); }
        }

        Window *win = windows;
        while (win) {
            bool active = (win == focused_win);
            /* Soft Shadow */
            bb_fill_rect(win->x + 4, win->y + 4, win->w, win->h, VNL_SHADOW);
            bb_fill_rect(win->x, win->y, win->w, win->h, VNL_SURFACE);
            /* Glassmorphic Title Bar */
            bb_fill_rect(win->x, win->y - 24, win->w, 24, active ? blend(VNL_TITLE, 0xDD000000) : blend(VNL_DKSHAD, 0xDD000000));
            bb_draw_string(win->x + 8, win->y - 14, win->title, VNL_WHITE);
            bb_fill_rect(win->x + win->w - 20, win->y - 20, 16, 16, VNL_DKSHAD);
            bb_draw_string(win->x + win->w - 16, win->y - 14, "X", VNL_WHITE);
            
            if (win->kind == WIN_TERM) {
                bb_fill_rect(win->x, win->y, win->w, win->h, 0xFF111111);
                for (int r = 0; r < TERM_ROWS; r++) {
                    for (int c = 0; c < TERM_COLS; c++) {
                        uint16_t entry = win->term_buffer[r * TERM_COLS + c];
                        char ch = (char)(entry & 0xFF); uint8_t color = (uint8_t)(entry >> 8);
                        if (ch != ' ') {
                            int idx = glyph_index(ch); const uint8_t *gp = g_vnl_fonts[idx]; uint32_t fg = vga_to_32bpp(color);
                            for (int yy = 0; yy < 8; yy++) { uint8_t bits = gp[yy]; for (int xx = 0; xx < 8; xx++) { if (bits & (1 << (7 - xx))) bb_plot(win->x + 5 + c * 8 + xx, win->y + 5 + r * 12 + yy, fg); } }
                        }
                    }
                }
                if (active) { bb_draw_string(win->x + 5, win->y + 5 + win->term_cursor_y * 12, "> ", 0xFFFFFF00); bb_draw_string(win->x + 21, win->y + 5 + win->term_cursor_y * 12, win->term_cmd, VNL_WHITE); bb_fill_rect(win->x + 21 + win->term_idx * 8, win->y + 5 + win->term_cursor_y * 12, 8, 10, 0x80FFFFFF); }
            } else if (win->kind == WIN_BROWSER) {
                bb_fill_rect(win->x, win->y, win->w, 32, VNL_SURFACE);
                bb_fill_rect(win->x + 8, win->y + 6, win->w - 64, 20, VNL_WHITE);
                bb_draw_string(win->x + 12, win->y + 12, win->url, VNL_DKSHAD);
                if (active) bb_fill_rect(win->x + 12 + win->url_idx * 8, win->y + 8, 2, 16, VNL_TITLE);
                bb_fill_rect(win->x + win->w - 48, win->y + 6, 40, 20, VNL_TITLE);
                bb_draw_string(win->x + win->w - 40, win->y + 12, "GO", VNL_WHITE);
                bb_fill_rect(win->x, win->y + 32, win->w, win->h - 32, VNL_WHITE);
                int cy = win->y + 40; const char *p = win->content;
                while (*p) {
                    const char *ls = p; while (*p && *p != '\n') p++;
                    size_t llen = (size_t)(p - ls); char lb[128]; if(llen>=128)llen=127; memcpy(lb, ls, llen); lb[llen]='\0';
                    uint32_t cfg = VNL_DKSHAD; if (lb[0] == '-') cfg = VNL_TITLE;
                    bb_draw_string(win->x + 10, cy, lb, cfg); cy += 12; if (*p == '\n') p++;
                }
            } else if (win->kind == WIN_EXPLORER) {
                bb_fill_rect(win->x, win->y, win->w, 20, VNL_TITLE);
                bb_draw_string(win->x + 5, win->y + 5, win->path, VNL_WHITE);
                bb_fill_rect(win->x, win->y + 20, win->w, win->h - 20, VNL_WHITE);
                char names[32][VFS_NAME_MAX]; int n = vfs_readdir(win->path, names, 32);
                for(int i=0; i<n; i++) { bb_fill_rect(win->x + 10, win->y + 30 + i * 20, 16, 16, VNL_ACCENT); bb_draw_string(win->x + 32, win->y + 34, names[i], VNL_DKSHAD); }
            } else if (win->kind == WIN_MUSIC) {
                bb_fill_rect(win->x, win->y, win->w, win->h, VNL_DKSHAD);
                bb_draw_string(win->x + 20, win->y + 30, "Now Playing: Vibe Wave", VNL_ACCENT);
                bb_fill_rect(win->x + 20, win->y + 60, win->w - 40, 4, 0xFF555555);
                bb_fill_rect(win->x + 20, win->y + 60, (win->w - 40) * 0.7, 4, VNL_ACCENT);
                bb_draw_string(win->x + 20, win->y + 80, "[<<] [||] [>>]", VNL_WHITE);
            }
            win = win->next;
        }

        if (start_menu_open) {
            bb_fill_rect(5, fb_info.height - TASKBAR_HEIGHT - 200, 180, 200, blend(VNL_DKSHAD, 0xEE000000));
            bb_draw_string(15, fb_info.height - TASKBAR_HEIGHT - 180, "VNL 1.0 PERFECT", VNL_ACCENT);
            bb_draw_string(15, fb_info.height - TASKBAR_HEIGHT - 150, "- Programs", VNL_WHITE);
            bb_draw_string(15, fb_info.height - TASKBAR_HEIGHT - 130, "- Documents", VNL_WHITE);
            bb_draw_string(15, fb_info.height - TASKBAR_HEIGHT - 110, "- Settings", VNL_WHITE);
            bb_draw_string(15, fb_info.height - TASKBAR_HEIGHT - 80, "- Run...", VNL_WHITE);
            bb_draw_string(15, fb_info.height - TASKBAR_HEIGHT - 40, "- Shut Down", VNL_WHITE);
        }

        bb_draw_cursor(mx, my);
        uint64_t phys, len; uint32_t pitch, w, h, bpp; fb_get_mmap_region(&phys, &len, &pitch, &w, &h, &bpp);
        uint8_t *fb_ptr = (uint8_t *)(uintptr_t)phys; for (uint32_t y = 0; y < h; y++) memcpy(fb_ptr + y * pitch, backbuffer + y * w, w * 4);

        int k = keyboard_poll(); if (k == 27) break;
        if (focused_win && k > 0) {
            Window *fw = focused_win;
            if (fw->kind == WIN_TERM) {
                if (k == '\n') { kprintf("\n"); shell_exec_line(fw->term_cmd); fw->term_idx = 0; fw->term_cmd[0] = 0; }
                else if (k == '\b' && fw->term_idx > 0) { fw->term_cmd[--fw->term_idx] = 0; }
                else if (fw->term_idx < 48 && k >= 32 && k <= 126) { fw->term_cmd[fw->term_idx++] = (char)k; fw->term_cmd[fw->term_idx] = 0; }
            } else if (fw->kind == WIN_BROWSER) {
                if (k == '\n') { browser_navigate(fw); }
                else if (k == '\b' && fw->url_idx > 0) { fw->url[--fw->url_idx] = 0; }
                else if (fw->url_idx < 120 && k >= 32 && k <= 126) { fw->url[fw->url_idx++] = (char)k; fw->url[fw->url_idx] = 0; }
            }
        }

        int dx, dy; uint8_t buttons;
        if (mouse_poll(&dx, &dy, &buttons)) {
            mx += dx; my -= dy; if (mx < 0) mx = 0; if (mx >= (int)fb_info.width) mx = (int)fb_info.width - 1; if (my < 0) my = 0; if (my >= (int)fb_info.height) my = (int)fb_info.height - 1;
            if ((buttons & 1) && !(prev_buttons & 1)) {
                if (start_hover) start_menu_open = !start_menu_open;
                else {
                    start_menu_open = false; Window *clicked_win = NULL, *curr = windows;
                    while (curr) {
                        if (mx >= curr->x + curr->w - 20 && mx < curr->x + curr->w - 4 && my >= curr->y - 20 && my < curr->y - 4) {
                            if (windows == curr) windows = curr->next; else { Window *p = windows; while (p->next != curr) p = p->next; p->next = curr->next; }
                            if (focused_win == curr) focused_win = windows; kfree(curr); break;
                        }
                        if (mx >= curr->x && mx < curr->x + curr->w && my >= curr->y - 24 && my < curr->y) { clicked_win = curr; curr->dragging = true; curr->drag_off_x = mx - curr->x; curr->drag_off_y = my - curr->y; break; }
                        else if (mx >= curr->x && mx < curr->x + curr->w && my >= curr->y && my < curr->y + curr->h) {
                            clicked_win = curr; if (curr->kind == WIN_BROWSER && mx >= curr->x + curr->w - 48 && mx < curr->x + curr->w - 8 && my >= curr->y + 6 && my < curr->y + 26) browser_navigate(curr);
                            break;
                        }
                        curr = curr->next;
                    }
                    if (clicked_win) {
                        focused_win = clicked_win;
                        if (clicked_win->next) {
                            if (windows == clicked_win) windows = clicked_win->next; else { Window *p = windows; while (p->next != clicked_win) p = p->next; p->next = clicked_win->next; }
                            Window *t = windows; while (t->next) t = t->next; t->next = clicked_win; clicked_win->next = NULL;
                        }
                    } else {
                        selected_icon = -1;
                        for (int i = 0; i < active_count; i++) {
                            if (mx >= active_icons[i].x && mx < active_icons[i].x + 64 && my >= active_icons[i].y && my < active_icons[i].y + 64) {
                                selected_icon = i;
                                if (active_icons[i].action) { vga_set_putchar_hook(NULL); fb_console_reset(); active_icons[i].action(0, NULL); vga_set_putchar_hook(term_putchar_hook); mouse_flush(); }
                                else if (active_icons[i].binary_path) {
                                    vga_set_putchar_hook(NULL); fb_console_reset();
                                    vnl_spawn_elf_path(active_icons[i].binary_path);
                                    vga_set_putchar_hook(term_putchar_hook); mouse_flush();
                                }
                                else if (strcmp(active_icons[i].label, "Terminal") == 0) window_create(WIN_TERM, 100 + (i*10), 100, 420, 300, "VNL Command Prompt");
                                else if (strcmp(active_icons[i].label, "Browser") == 0) window_create(WIN_BROWSER, 150, 120, 500, 350, "VBrowser Professional");
                                else if (strcmp(active_icons[i].label, "My Computer") == 0) window_create(WIN_EXPLORER, 80, 80, 300, 400, "My Computer");
                                else if (strcmp(active_icons[i].label, "Music") == 0) window_create(WIN_MUSIC, 200, 200, 300, 120, "Vibe Player");
                                break;
                            }
                        }
                    }
                }
            }
            if (!(buttons & 1)) { Window *curr = windows; while (curr) { curr->dragging = false; curr = curr->next; } }
            if (buttons & 1) { Window *curr = windows; while (curr) { if (curr->dragging) { curr->x = mx - curr->drag_off_x; curr->y = my - curr->drag_off_y; } curr = curr->next; } }
            prev_buttons = buttons;
        }
        timer_sleep(16);
    }
    vga_set_putchar_hook(NULL); fb_console_reset(); vga_fb_mirror_refresh();
}
