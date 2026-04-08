#include "x11_env.h"
#include "vfs.h"
#include "string.h"

extern uint8_t vnl_x_elf[];
extern uint8_t vnl_x_elf_end[];

void x11_environment_init(void)
{
    if (vfs_resolve("/tmp") < 0) vfs_mkdir("/tmp");
    if (vfs_resolve("/tmp/.X11-unix") < 0) vfs_mkdir("/tmp/.X11-unix");
    if (vfs_resolve("/var") < 0) vfs_mkdir("/var");
    if (vfs_resolve("/var/log") < 0) vfs_mkdir("/var/log");
    if (vfs_resolve("/etc") < 0) vfs_mkdir("/etc");
    if (vfs_resolve("/etc/X11") < 0) vfs_mkdir("/etc/X11");
    if (vfs_resolve("/etc/X11/xorg.conf.d") < 0) vfs_mkdir("/etc/X11/xorg.conf.d");
    if (vfs_resolve("/usr") < 0) vfs_mkdir("/usr");
    if (vfs_resolve("/usr/bin") < 0) vfs_mkdir("/usr/bin");
    if (vfs_resolve("/run") < 0) vfs_mkdir("/run");

    int fd = vfs_open("/etc/vnl-x11.env", VFS_O_WRITE | VFS_O_CREATE | VFS_O_TRUNC);
    if (fd >= 0) {
        static const char env[] =
            "DISPLAY=:0\n"
            "VNL_X11_PATH=/tmp/.X11-unix/X0\n"
            "# Embedded /usr/bin/Xorg is a usermode FB client; real Xorg is still an external port.\n";
        vfs_write(fd, env, strlen(env));
        vfs_close(fd);
    }

    fd = vfs_open("/etc/X11/README.vnl", VFS_O_WRITE | VFS_O_CREATE | VFS_O_TRUNC);
    if (fd >= 0) {
        static const char txt[] =
            "VNL: use GRUB entry with linear framebuffer; open /dev/fb0; "
            "AF_UNIX stream sockets for X transport.\n";
        vfs_write(fd, txt, strlen(txt));
        vfs_close(fd);
    }

    fd = vfs_open("/usr/bin/startx", VFS_O_WRITE | VFS_O_CREATE | VFS_O_TRUNC);
    if (fd >= 0) {
        /* Same env as cmd_startx; do not invoke “startx” here or sh recurses. */
        static const char sx[] =
            "#!/bin/sh\n"
            "export DISPLAY=:0\n"
            "export XAUTHORITY=/run/xauth_0\n"
            "echo '# VNL stub :0' > /tmp/.X11-unix/X0\n"
            "write /run/xauth_0 .\n"
            "x11info\n";
        vfs_write(fd, sx, sizeof(sx) - 1);
        vfs_close(fd);
    }

    fd = vfs_open("/usr/bin/Xorg", VFS_O_WRITE | VFS_O_CREATE | VFS_O_TRUNC);
    if (fd >= 0) {
        size_t xsz = (size_t)(vnl_x_elf_end - vnl_x_elf);
        vfs_write(fd, vnl_x_elf, xsz);
        vfs_close(fd);
    }
}
