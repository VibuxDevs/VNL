#pragma once

/*
 * Minimal full-screen framebuffer stand-in for an X session.
 * True Xorg still needs ELF + libc userspace; this gives a graphical :0 view on VNC.
 */
void x11_minimal_session_run(void);
