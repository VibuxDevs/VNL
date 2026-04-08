#include "vfs.h"
#include "heap.h"
#include "string.h"
#include "printf.h"
#include "cpu.h"

/* ---- Node store -------------------------------------------------- */
static VFSNode nodes[VFS_MAX_NODES];
static uint32_t next_inode = 1;   /* 0 = root */

uint32_t vfs_cwd = VFS_INODE_ROOT;
void vfs_set_cwd(uint32_t inode) { vfs_cwd = inode; }

/* ---- File descriptor table --------------------------------------- */
typedef struct {
    bool     open;
    uint32_t inode;
    size_t   offset;
    int      flags;
} FDEntry;

static FDEntry fds[VFS_MAX_FDS];
/* fd 0 = stdin (keyboard), fd 1 = stdout (VGA), fd 2 = stderr — handled by syscall layer */

/* ---- Node helpers ------------------------------------------------ */
static VFSNode *node_get(uint32_t inode)
{
    for (int i = 0; i < VFS_MAX_NODES; i++)
        if (nodes[i].used && nodes[i].inode == inode) return &nodes[i];
    return NULL;
}

static VFSNode *node_alloc(void)
{
    for (int i = 0; i < VFS_MAX_NODES; i++)
        if (!nodes[i].used) return &nodes[i];
    return NULL;
}

/* ---- Path resolver ----------------------------------------------- */
/* Splits path into components and walks the node tree.
   Returns inode on success, -1 on failure. */
int vfs_resolve(const char *path)
{
    if (!path || !*path) return (int)vfs_cwd;

    uint32_t cur = (*path == '/') ? VFS_INODE_ROOT : vfs_cwd;
    if (*path == '/') path++;
    if (!*path) return (int)cur;   /* root */

    char component[VFS_NAME_MAX];
    while (*path) {
        /* Extract next component */
        int ci = 0;
        while (*path && *path != '/' && ci < VFS_NAME_MAX - 1)
            component[ci++] = *path++;
        component[ci] = '\0';
        if (*path == '/') path++;

        if (strcmp(component, ".") == 0) continue;
        if (strcmp(component, "..") == 0) {
            VFSNode *n = node_get(cur);
            if (n) cur = n->parent;
            continue;
        }

        /* Find child with this name */
        bool found = false;
        for (int i = 0; i < VFS_MAX_NODES; i++) {
            if (nodes[i].used && nodes[i].parent == cur &&
                strcmp(nodes[i].name, component) == 0) {
                cur = nodes[i].inode;
                found = true;
                break;
            }
        }
        if (!found) return -1;
    }
    return (int)cur;
}

/* Resolve parent dir and return leaf name */
static int resolve_parent(const char *path, char *leaf_out)
{
    /* Find last '/' */
    const char *slash = NULL;
    for (const char *p = path; *p; p++)
        if (*p == '/') slash = p;

    if (!slash) {
        strncpy(leaf_out, path, VFS_NAME_MAX - 1);
        leaf_out[VFS_NAME_MAX - 1] = '\0';
        return (int)vfs_cwd;
    }

    /* Everything before slash is parent path */
    char parent_path[256];
    size_t plen = (size_t)(slash - path);
    if (plen == 0) {
        parent_path[0] = '/'; parent_path[1] = '\0';
    } else {
        if (plen >= sizeof(parent_path)) plen = sizeof(parent_path) - 1;
        memcpy(parent_path, path, plen);
        parent_path[plen] = '\0';
    }
    strncpy(leaf_out, slash + 1, VFS_NAME_MAX - 1);
    leaf_out[VFS_NAME_MAX - 1] = '\0';
    return vfs_resolve(parent_path);
}

/* ---- Public API -------------------------------------------------- */

void vfs_init(void)
{
    memset(nodes, 0, sizeof(nodes));
    memset(fds,   0, sizeof(fds));
    next_inode = 1;

    /* Root directory */
    nodes[0].used   = true;
    nodes[0].type   = VFS_DIR;
    nodes[0].inode  = VFS_INODE_ROOT;
    nodes[0].parent = VFS_INODE_ROOT;
    nodes[0].name[0] = '/';
    nodes[0].name[1] = '\0';

    /* Create some starter directories */
    vfs_mkdir("/bin");
    vfs_mkdir("/etc");
    vfs_mkdir("/tmp");
    vfs_mkdir("/home");
}

int vfs_open(const char *path, int flags)
{
    /* Find a free fd (start from 3 to leave 0/1/2 for std streams) */
    int fd = -1;
    for (int i = 3; i < VFS_MAX_FDS; i++) {
        if (!fds[i].open) { fd = i; break; }
    }
    if (fd < 0) return -1;

    int inode = vfs_resolve(path);

    if (inode < 0) {
        if (!(flags & VFS_O_CREATE)) return -1;
        /* Create the file */
        char leaf[VFS_NAME_MAX];
        int parent = resolve_parent(path, leaf);
        if (parent < 0) return -1;
        VFSNode *pn = node_get((uint32_t)parent);
        if (!pn || pn->type != VFS_DIR) return -1;
        VFSNode *nn = node_alloc();
        if (!nn) return -1;
        nn->used   = true;
        nn->type   = VFS_FILE;
        nn->inode  = next_inode++;
        nn->parent = (uint32_t)parent;
        strncpy(nn->name, leaf, VFS_NAME_MAX - 1);
        nn->data = NULL;
        nn->size = nn->capacity = 0;
        inode = (int)nn->inode;
    }

    VFSNode *n = node_get((uint32_t)inode);
    if (!n || n->type != VFS_FILE) return -1;

    if (flags & VFS_O_TRUNC) {
        kfree(n->data);
        n->data = NULL;
        n->size = n->capacity = 0;
    }

    fds[fd].open   = true;
    fds[fd].inode  = (uint32_t)inode;
    fds[fd].offset = (flags & VFS_O_WRITE) && !(flags & VFS_O_TRUNC) ? n->size : 0;
    fds[fd].flags  = flags;
    return fd;
}

int vfs_close(int fd)
{
    if (fd < 3 || fd >= VFS_MAX_FDS || !fds[fd].open) return -1;
    fds[fd].open = false;
    return 0;
}

int vfs_read(int fd, void *buf, size_t len)
{
    if (fd < 0 || fd >= VFS_MAX_FDS || !fds[fd].open) return -1;
    if (!(fds[fd].flags & VFS_O_READ)) return -1;
    VFSNode *n = node_get(fds[fd].inode);
    if (!n) return -1;
    size_t avail = n->size - fds[fd].offset;
    if (avail == 0) return 0;
    size_t to_read = (len < avail) ? len : avail;
    memcpy(buf, n->data + fds[fd].offset, to_read);
    fds[fd].offset += to_read;
    return (int)to_read;
}

int vfs_write(int fd, const void *buf, size_t len)
{
    if (fd < 0 || fd >= VFS_MAX_FDS || !fds[fd].open) return -1;
    if (!(fds[fd].flags & VFS_O_WRITE)) return -1;
    VFSNode *n = node_get(fds[fd].inode);
    if (!n || n->type != VFS_FILE) return -1;

    size_t new_end = fds[fd].offset + len;
    if (new_end > n->capacity) {
        size_t new_cap = new_end + 256;
        uint8_t *new_data = (uint8_t *)krealloc(n->data, new_cap);
        if (!new_data) return -1;
        n->data     = new_data;
        n->capacity = new_cap;
    }
    memcpy(n->data + fds[fd].offset, buf, len);
    fds[fd].offset += len;
    if (fds[fd].offset > n->size) n->size = fds[fd].offset;
    return (int)len;
}

int vfs_mkdir(const char *path)
{
    if (vfs_resolve(path) >= 0) return -1;  /* already exists */
    char leaf[VFS_NAME_MAX];
    int parent = resolve_parent(path, leaf);
    if (parent < 0 || !leaf[0]) return -1;
    VFSNode *pn = node_get((uint32_t)parent);
    if (!pn || pn->type != VFS_DIR) return -1;
    VFSNode *nn = node_alloc();
    if (!nn) return -1;
    nn->used   = true;
    nn->type   = VFS_DIR;
    nn->inode  = next_inode++;
    nn->parent = (uint32_t)parent;
    strncpy(nn->name, leaf, VFS_NAME_MAX - 1);
    nn->data = NULL;
    nn->size = nn->capacity = 0;
    return 0;
}

int vfs_unlink(const char *path)
{
    int inode = vfs_resolve(path);
    if (inode < 0) return -1;
    if ((uint32_t)inode == VFS_INODE_ROOT) return -1;
    VFSNode *n = node_get((uint32_t)inode);
    if (!n) return -1;
    /* Refuse to remove non-empty directory */
    if (n->type == VFS_DIR) {
        for (int i = 0; i < VFS_MAX_NODES; i++)
            if (nodes[i].used && nodes[i].parent == (uint32_t)inode)
                return -1;
    }
    kfree(n->data);
    memset(n, 0, sizeof(*n));
    return 0;
}

int vfs_readdir(const char *path, char (*names)[VFS_NAME_MAX], int max)
{
    int inode = vfs_resolve(path);
    if (inode < 0) return -1;
    VFSNode *dir = node_get((uint32_t)inode);
    if (!dir || dir->type != VFS_DIR) return -1;
    int count = 0;
    for (int i = 0; i < VFS_MAX_NODES && count < max; i++) {
        if (nodes[i].used && nodes[i].parent == (uint32_t)inode &&
            nodes[i].inode != (uint32_t)inode) {
            strncpy(names[count], nodes[i].name, VFS_NAME_MAX - 1);
            names[count][VFS_NAME_MAX - 1] = '\0';
            /* Append '/' for dirs */
            if (nodes[i].type == VFS_DIR) {
                size_t l = strlen(names[count]);
                if (l < VFS_NAME_MAX - 1) { names[count][l] = '/'; names[count][l+1] = '\0'; }
            }
            count++;
        }
    }
    return count;
}

int vfs_stat(const char *path, VFSNodeType *type, size_t *size)
{
    int inode = vfs_resolve(path);
    if (inode < 0) return -1;
    VFSNode *n = node_get((uint32_t)inode);
    if (!n) return -1;
    if (type) *type = n->type;
    if (size) *size = n->size;
    return 0;
}

char *vfs_getcwd(char *buf, size_t bufsz)
{
    if (!buf || bufsz == 0) return NULL;
    if (vfs_cwd == VFS_INODE_ROOT) { strncpy(buf, "/", bufsz); return buf; }

    /* Build path by walking to root */
    char tmp[1024];
    int pos = (int)sizeof(tmp) - 1;
    tmp[pos] = '\0';
    uint32_t cur = vfs_cwd;
    while (cur != VFS_INODE_ROOT) {
        VFSNode *n = node_get(cur);
        if (!n) break;
        size_t l = strlen(n->name);
        pos -= (int)l;
        if (pos < 1) break;
        memcpy(tmp + pos, n->name, l);
        tmp[--pos] = '/';
        cur = n->parent;
    }
    if (tmp[pos] == '\0') tmp[--pos] = '/';
    strncpy(buf, tmp + pos, bufsz - 1);
    buf[bufsz - 1] = '\0';
    return buf;
}
