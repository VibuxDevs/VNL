#pragma once
#include "types.h"

#define VFS_NAME_MAX   64
#define VFS_MAX_NODES  256
#define VFS_MAX_FDS    64
#define VFS_INODE_ROOT 0

typedef enum { VFS_FILE = 0, VFS_DIR = 1, VFS_CHR = 2 } VFSNodeType;

/* Open flags */
#define VFS_O_READ   (1 << 0)
#define VFS_O_WRITE  (1 << 1)
#define VFS_O_CREATE (1 << 2)
#define VFS_O_TRUNC  (1 << 3)
#define VFS_O_RDWR   (VFS_O_READ | VFS_O_WRITE)

typedef struct {
    bool         used;
    VFSNodeType  type;
    uint32_t     inode;
    uint32_t     parent;
    uint16_t     dev_major;
    uint16_t     dev_minor;
    char         name[VFS_NAME_MAX];
    uint8_t     *data;
    size_t       size;
    size_t       capacity;
} VFSNode;

void   vfs_init(void);

/* File descriptor operations */
int    vfs_open(const char *path, int flags);
int    vfs_close(int fd);
int    vfs_read(int fd, void *buf, size_t len);
int    vfs_write(int fd, const void *buf, size_t len);
int    vfs_ioctl(int fd, uint64_t request, void *arg);

VFSNode *vfs_node_from_fd(int fd);
int      vfs_mknod_chr(const char *path, uint16_t major, uint16_t minor);

/* Directory operations */
int    vfs_mkdir(const char *path);
int    vfs_unlink(const char *path);
int    vfs_readdir(const char *path, char (*names)[VFS_NAME_MAX], int max);

/* Stat / path utilities */
int    vfs_stat(const char *path, VFSNodeType *type, size_t *size);
int    vfs_resolve(const char *path);   /* returns inode or -1 */
char  *vfs_getcwd(char *buf, size_t bufsz);

/* CWD */
extern uint32_t vfs_cwd;
void   vfs_set_cwd(uint32_t inode);
