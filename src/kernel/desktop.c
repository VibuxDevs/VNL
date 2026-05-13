#include "desktop.h"
#include "fb.h"
#include "mouse.h"
#include "keyboard.h"
#include "printf.h"
#include "string.h"
#include "timer.h"
#include "vfs.h"
#include "neovim.h"
#include "htop.h"
#include "doom.h"
#include "vinstall.h"
#include "vga.h"
#include "heap.h"
#include "fonts.h"
#include "shell.h"

#define WIN95_BG      0xFF008080
#define WIN95_SURFACE 0xFFC0C0C0
#define WIN95_LIGHT   0xFFFFFFFF
#define WIN95_SHADOW  0xFF808080
#define WIN95_DKSHAD  0xFF000000
#define WIN95_TITLE   0xFF000080

#define TASKBAR_HEIGHT 28
#define CURSOR_W 12
#define CURSOR_H 19

#define TERM_COLS 50
#define TERM_ROWS 20
#define TERM_BUF_SIZE (TERM_COLS * TERM_ROWS)

static char term_buffer[TERM_BUF_SIZE];
static int  term_cursor_x = 0;
static int  term_cursor_y = 0;

static void term_putchar_hook(char c) {
    if (c == '\n') {
        term_cursor_x = 0;
        term_cursor_y++;
    } else if (c == '\r') {
        term_cursor_x = 0;
    } else if (c == '\b') {
        if (term_cursor_x > 0) term_cursor_x--;
        term_buffer[term_cursor_y * TERM_COLS + term_cursor_x] = ' ';
    } else {
        if (term_cursor_x < TERM_COLS && term_cursor_y < TERM_ROWS) {
            term_buffer[term_cursor_y * TERM_COLS + term_cursor_x] = c;
            term_cursor_x++;
            if (term_cursor_x >= TERM_COLS) {
                term_cursor_x = 0;
                term_cursor_y++;
            }
        }
    }
    if (term_cursor_y >= TERM_ROWS) {
        /* Scroll */
        memmove(term_buffer, term_buffer + TERM_COLS, TERM_COLS * (TERM_ROWS - 1));
        memset(term_buffer + TERM_COLS * (TERM_ROWS - 1), ' ', TERM_COLS);
        term_cursor_y = TERM_ROWS - 1;
    }
}

static const char *cursor_shape[CURSOR_H] = {
    "B", "BWB", "BWWB", "BWWWB", "BWWWWB", "BWWWWWB", "BWWWWWWB", "BWWWWWWWB",
    "BWWWWWWWWB", "BWWWWWWWWWB", "BWWWWWBBBBB", "BWWBWWB", "BWB BWWB",
    "BB  BWWB", "     BWWB", "     BWWB", "      BWB", "      BB", "       B"
};

typedef struct {
    int x, y, w, h;
    const char *label;
    const char *binary_path;
    void (*action)(int argc, char **argv);
    uint32_t icon_color;
} DesktopIcon;

static uint32_t *backbuffer = NULL;
static FBInfo fb_info;

static void bb_plot(int x, int y, uint32_t c) {
    if (x < 0 || x >= (int)fb_info.width || y < 0 || y >= (int)fb_info.height) return;
    backbuffer[y * fb_info.width + x] = c;
}

static void bb_fill_rect(int x, int y, int w, int h, uint32_t c) {
    if (x >= (int)fb_info.width || y >= (int)fb_info.height) return;
    if (x + w > (int)fb_info.width) w = (int)fb_info.width - x;
    if (y + h > (int)fb_info.height) h = (int)fb_info.height - y;
    for (int r = 0; r < h; r++) {
        uint32_t *row_ptr = backbuffer + (y + r) * fb_info.width + x;
        for (int col = 0; col < w; col++) row_ptr[col] = c;
    }
}

static void bb_draw_bevel(int x, int y, int w, int h, bool pressed) {
    if (pressed) {
        bb_fill_rect(x, y, w, 1, WIN95_SHADOW);
        bb_fill_rect(x, y, 1, h, WIN95_SHADOW);
        bb_fill_rect(x + 1, y + h - 1, w - 1, 1, WIN95_LIGHT);
        bb_fill_rect(x + w - 1, y + 1, 1, h - 1, WIN95_LIGHT);
    } else {
        bb_fill_rect(x, y, w, 1, WIN95_LIGHT);
        bb_fill_rect(x, y, 1, h, WIN95_LIGHT);
        bb_fill_rect(x + 1, y + h - 2, w - 1, 1, WIN95_SHADOW);
        bb_fill_rect(x + w - 2, y + 1, 1, h - 1, WIN95_SHADOW);
        bb_fill_rect(x, y + h - 1, w, 1, WIN95_DKSHAD);
        bb_fill_rect(x + w - 1, y, 1, h, WIN95_DKSHAD);
    }
}

static void bb_draw_string(int x, int y, const char *s, uint32_t fg) {
    if (!s) return;
    int cx = x;
    while (*s) {
        int idx = glyph_index(*s);
        const uint8_t *gp = g_vnl_fonts[idx];
        for (int yy = 0; yy < 8; yy++) {
            uint8_t bits = gp[yy];
            for (int xx = 0; xx < 8; xx++) {
                if (bits & (1 << (7 - xx))) bb_plot(cx + xx, y + yy, fg);
            }
        }
        cx += 8; s++;
    }
}

static void bb_draw_cursor(int x, int y) {
    for (int r = 0; r < CURSOR_H; r++) {
        const char *row_str = cursor_shape[r];
        for (int c = 0; row_str[c] && c < CURSOR_W; c++) {
            char ch = row_str[c];
            if (ch == ' ') continue;
            uint32_t color = (ch == 'W') ? 0xFFFFFFFF : 0xFF000000;
            bb_plot(x + c, y + r, color);
        }
    }
}

void desktop_run(void) {
    if (!fb_is_available()) return;
    fb_get_info(&fb_info);
    
    if (!backbuffer) backbuffer = (uint32_t *)kmalloc(fb_info.width * fb_info.height * 4);
    if (!backbuffer) return;

    memset(term_buffer, ' ', TERM_BUF_SIZE);
    term_cursor_x = term_cursor_y = 0;

    DesktopIcon icons_master[] = {
        {20, 20, 64, 64, "My Computer", NULL, NULL, 0xFFCCCCCC},
        {20, 100, 64, 64, "Terminal", NULL, NULL, 0xFF444444},
        {20, 180, 64, 64, "Editor", "/usr/bin/neovim-vnl", cmd_neovim_vnl, 0xFF00AA00},
        {20, 260, 64, 64, "HTOP", "/usr/bin/htop-gui", cmd_htop_gui, 0xFF00AAAA},
        {20, 340, 64, 64, "DOOM", "/usr/bin/doom-generic", cmd_doom_generic, 0xFFAA0000},
        {100, 20, 64, 64, "Installer", NULL, cmd_vinstall, 0xFFAA5500}
    };
    int master_count = sizeof(icons_master) / sizeof(icons_master[0]);
    DesktopIcon active_icons[10];
    int icon_count = 0;
    for (int i = 0; i < master_count; i++) {
        if (!icons_master[i].binary_path || vfs_resolve(icons_master[i].binary_path) >= 0) {
            active_icons[icon_count++] = icons_master[i];
        }
    }

    bool exit_desktop = false;
    bool start_menu_open = false;
    bool terminal_open = false;
    char term_cmd[64] = {0};
    int term_idx = 0;
    int mx = fb_info.width / 2, my = fb_info.height / 2;
    uint8_t prev_buttons = 0;
    int selected_icon = -1;

    mouse_flush();
    vga_set_putchar_hook(term_putchar_hook);

    while (!exit_desktop) {
        bb_fill_rect(0, 0, fb_info.width, fb_info.height, WIN95_BG);
        
        /* Taskbar */
        bb_fill_rect(0, fb_info.height - TASKBAR_HEIGHT, fb_info.width, TASKBAR_HEIGHT, WIN95_SURFACE);
        bb_fill_rect(0, fb_info.height - TASKBAR_HEIGHT, fb_info.width, 1, WIN95_LIGHT);
        bool start_hover = (mx >= 2 && mx < 62 && my >= (int)fb_info.height - TASKBAR_HEIGHT + 2 && my < (int)fb_info.height - 2);
        bb_draw_bevel(2, fb_info.height - TASKBAR_HEIGHT + 2, 60, TASKBAR_HEIGHT - 4, start_menu_open || (start_hover && (prev_buttons & 1)));
        bb_draw_string(20, fb_info.height - TASKBAR_HEIGHT + 8, "Start", WIN95_DKSHAD);
        bb_draw_bevel(fb_info.width - 70, fb_info.height - TASKBAR_HEIGHT + 3, 66, TASKBAR_HEIGHT - 6, true);
        bb_draw_string(fb_info.width - 65, fb_info.height - TASKBAR_HEIGHT + 8, "13:28", WIN95_DKSHAD);

        /* Icons */
        for (int i = 0; i < icon_count; i++) {
            bb_fill_rect(active_icons[i].x + 16, active_icons[i].y, 32, 32, active_icons[i].icon_color);
            int label_w = strlen(active_icons[i].label) * 8;
            int lx = active_icons[i].x + (64 - label_w) / 2;
            if (selected_icon == i) {
                bb_fill_rect(lx - 2, active_icons[i].y + 35, label_w + 4, 12, WIN95_TITLE);
                bb_draw_string(lx, active_icons[i].y + 37, active_icons[i].label, WIN95_LIGHT);
            } else {
                bb_draw_string(lx, active_icons[i].y + 37, active_icons[i].label, WIN95_LIGHT);
            }
        }

        /* Terminal Window */
        if (terminal_open) {
            int wx = 150, wy = 80, ww = 420, wh = 300;
            bb_fill_rect(wx, wy, ww, wh, WIN95_DKSHAD);
            bb_draw_bevel(wx - 2, wy - 20, ww + 4, wh + 22, false);
            bb_fill_rect(wx - 1, wy - 19, ww + 2, 17, WIN95_TITLE);
            bb_draw_string(wx + 4, wy - 14, "VNL Command Prompt (Live Shell)", WIN95_LIGHT);
            
            /* Render term_buffer */
            for (int r = 0; r < TERM_ROWS; r++) {
                char row_str[TERM_COLS + 1];
                memcpy(row_str, term_buffer + r * TERM_COLS, TERM_COLS);
                row_str[TERM_COLS] = '\0';
                bb_draw_string(wx + 5, wy + 5 + r * 12, row_str, WIN95_LIGHT);
            }
            /* Prompt line */
            bb_draw_string(wx + 5, wy + 5 + term_cursor_y * 12, "> ", 0xFFFFFF00);
            bb_draw_string(wx + 21, wy + 5 + term_cursor_y * 12, term_cmd, WIN95_LIGHT);
            bb_fill_rect(wx + 21 + term_idx * 8, wy + 5 + term_cursor_y * 12, 8, 10, 0x80FFFFFF);
        }

        if (start_menu_open) {
            int menu_w = 160, menu_h = (icon_count + 1) * 24 + 10;
            int menu_x = 2, menu_y = fb_info.height - TASKBAR_HEIGHT - menu_h;
            bb_fill_rect(menu_x, menu_y, menu_w, menu_h, WIN95_SURFACE);
            bb_draw_bevel(menu_x, menu_y, menu_w, menu_h, false);
            bb_fill_rect(menu_x + 2, menu_y + 2, 20, menu_h - 4, WIN95_SHADOW);
            bb_draw_string(menu_x + 30, menu_y + 10, "Terminal", WIN95_DKSHAD);
            for (int i = 0; i < icon_count; i++) bb_draw_string(menu_x + 30, menu_y + 34 + i * 24, active_icons[i].label, WIN95_DKSHAD);
        }

        bb_draw_cursor(mx, my);

        /* Final Blit */
        uint64_t phys, len; uint32_t pitch, w, h, bpp;
        fb_get_mmap_region(&phys, &len, &pitch, &w, &h, &bpp);
        uint8_t *fb_ptr = (uint8_t *)(uintptr_t)phys;
        for (uint32_t y = 0; y < h; y++) memcpy(fb_ptr + y * pitch, backbuffer + y * w, w * 4);

        int k = keyboard_poll();
        if (k == 27) break;
        if (terminal_open && k > 0) {
            if (k == '\n') {
                kprintf("\n> %s\n", term_cmd);
                shell_exec_line(term_cmd);
                term_idx = 0; term_cmd[0] = 0;
            } else if (k == '\b' && term_idx > 0) {
                term_cmd[--term_idx] = 0;
            } else if (term_idx < 48 && k >= 32 && k <= 126) {
                term_cmd[term_idx++] = (char)k; term_cmd[term_idx] = 0;
            }
        }

        int dx, dy; uint8_t buttons;
        if (mouse_poll(&dx, &dy, &buttons)) {
            mx += dx; my -= dy;
            if (mx < 0) mx = 0; if (mx >= (int)fb_info.width) mx = (int)fb_info.width - 1;
            if (my < 0) my = 0; if (my >= (int)fb_info.height) my = (int)fb_info.height - 1;

            if ((buttons & 1) && !(prev_buttons & 1)) {
                if (start_hover) { start_menu_open = !start_menu_open; }
                else if (start_menu_open) { start_menu_open = false; }
                else {
                    selected_icon = -1;
                    for (int i = 0; i < icon_count; i++) {
                        if (mx >= active_icons[i].x && mx < active_icons[i].x + 64 && my >= active_icons[i].y && my < active_icons[i].y + 64) {
                            selected_icon = i;
                            if (active_icons[i].action) {
                                vga_set_putchar_hook(NULL);
                                fb_console_reset();
                                active_icons[i].action(0, NULL);
                                vga_set_putchar_hook(term_putchar_hook);
                                mouse_flush();
                            } else if (strcmp(active_icons[i].label, "Terminal") == 0) {
                                terminal_open = true;
                            }
                            break;
                        }
                    }
                    if (selected_icon == -1) terminal_open = false;
                }
            }
            prev_buttons = buttons;
        }
        timer_sleep(16);
    }
    vga_set_putchar_hook(NULL);
    fb_console_reset();
    vga_fb_mirror_refresh();
}
