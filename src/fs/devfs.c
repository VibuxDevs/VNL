#include "devfs.h"
#include "vfs.h"
#include "fb.h"
#include "uapi_fb.h"
#include "string.h"
#include "errno.h"

void devfs_init(void)
{
    if (vfs_resolve("/dev") < 0)
        vfs_mkdir("/dev");
    vfs_mknod_chr("/dev/fb0", DEV_FB_MAJOR, DEV_FB0_MINOR);
    vfs_mknod_chr("/dev/null", DEV_NULL_MAJOR, DEV_NULL_MINOR);
}

int devfs_chr_read(VFSNode *n, void *buf, size_t len, size_t *off)
{
    (void)buf;
    (void)len;
    (void)off;
    if (!n || n->type != VFS_CHR) return -EIO;
    if (n->dev_major == DEV_NULL_MAJOR) return 0;
    if (n->dev_major == DEV_FB_MAJOR) return 0;
    return -ENODEV;
}

int devfs_chr_write(VFSNode *n, const void *buf, size_t len, size_t *off)
{
    (void)buf;
    (void)off;
    if (!n || n->type != VFS_CHR) return -EIO;
    if (n->dev_major == DEV_NULL_MAJOR) return (int)len;
    if (n->dev_major == DEV_FB_MAJOR) return -EINVAL;
    return -ENODEV;
}

int devfs_chr_ioctl(VFSNode *n, uint64_t request, void *arg)
{
    if (!n || n->type != VFS_CHR || !arg) return -EINVAL;
    if (n->dev_major != DEV_FB_MAJOR) return -ENOTTY;

    if (request == FBIOGET_FSCREENINFO) {
        fb_fix_screeninfo *fix = (fb_fix_screeninfo *)arg;
        memset(fix, 0, sizeof(*fix));
        strncpy(fix->id, "VNLFB", sizeof(fix->id) - 1);
        uint64_t p0, ln;
        uint32_t ll, w, h, bpp;
        if (!fb_get_mmap_region(&p0, &ln, &ll, &w, &h, &bpp))
            return -ENODEV;
        fix->smem_start  = p0;
        fix->smem_len    = ln > 0xFFFFFFFFu ? 0xFFFFFFFFu : (uint32_t)ln;
        fix->line_length = ll;
        fix->type        = 1;
        fix->visual      = 2;
        return 0;
    }
    if (request == FBIOGET_VSCREENINFO) {
        fb_var_screeninfo *v = (fb_var_screeninfo *)arg;
        memset(v, 0, sizeof(*v));
        uint64_t p0, ln;
        uint32_t ll, w, h, bpp;
        (void)p0;
        (void)ln;
        if (!fb_get_mmap_region(&p0, &ln, &ll, &w, &h, &bpp))
            return -ENODEV;
        v->xres = v->xres_virtual = w;
        v->yres = v->yres_virtual = h;
        v->bits_per_pixel = bpp;
        v->red_offset = 16;
        v->green_offset = 8;
        v->blue_offset = 0;
        v->red_length = v->green_length = v->blue_length = 8;
        return 0;
    }
    return -ENOTTY;
}
