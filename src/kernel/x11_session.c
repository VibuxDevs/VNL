#include "x11_session.h"
#include "fb.h"
#include "keyboard.h"
#include "vga.h"
#include "types.h"

void x11_minimal_session_run(void)
{
    if (!fb_is_available())
        return;

    FBInfo fb;
    fb_get_info(&fb);

    const uint32_t bg  = 0xFF2D2D3D;
    const uint32_t bar = 0xFF3A3A5C;
    const uint32_t fg  = 0xFFE8E8F5;
    const uint32_t dim = 0xFF9A9AB8;

    fb_fill_rect(0, 0, fb.width, fb.height, bg);

    uint32_t bar_h = 40;
    if (bar_h > fb.height / 4)
        bar_h = fb.height / 8;
    if (bar_h < 24)
        bar_h = 24;
    fb_fill_rect(0, 0, fb.width, bar_h, bar);

    fb_draw_string(12, 11,
                   "VNL :0   framebuffer session (not Xorg / no window manager)",
                   fg, bar);

    uint32_t y = bar_h + 28;
    fb_draw_string(12, y,
                   "A real X server needs a static or dynamic Xorg build plus libc.",
                   dim, bg);
    y += 14;
    fb_draw_string(12, y,
                   "This is a kernel-drawn placeholder so startx switches VNC to a GUI.",
                   dim, bg);
    y += 14;
    fb_draw_string(12, y,
                   "Press Esc or q to return to the text shell.",
                   fg, bg);

    y += 36;
    fb_draw_string(12, y,
                   "Tip: DISPLAY=:0 and /tmp/.X11-unix/X0 are still prepared for future X clients.",
                   dim, bg);

    for (;;) {
        int k = keyboard_poll();
        if (k < 0) {
            asm volatile("hlt");
            continue;
        }
        if (k == 27 || k == 'q' || k == 'Q')
            break;
    }

    fb_console_reset();
    vga_fb_mirror_refresh();
}
