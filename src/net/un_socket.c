/*
 * Minimal AF_UNIX SOCK_STREAM — enough for X11's Unix-domain transport
 * (/tmp/.X11-unix/X0) once a userspace X server runs.
 */
#include "unix_socket.h"
#include "vfs.h"
#include "string.h"
#include "errno.h"

#define RX_CAP 8192
#define ACCEPT_Q 8

typedef enum {
    SK_UNUSED = 0,
    SK_CREATED,
    SK_BOUND,
    SK_LISTENING,
    SK_CONNECTED,
} SkState;

typedef struct {
    bool    used;
    SkState state;
    char    path[120];
    uint8_t rx[RX_CAP];
    size_t  rhd, rtl;
    int     peer_idx;
    int listen_q[ACCEPT_Q];
    int n_wait;
} USock;

#define PEER_NONE (-1)
static USock socks[UNIX_SOCK_MAX];

void unix_socket_init(void)
{
    memset(socks, 0, sizeof(socks));
}

static int idx_from_fd(int fd)
{
    if (fd < UNIX_SOCK_FD_BASE || fd >= UNIX_SOCK_FD_BASE + UNIX_SOCK_MAX) return -1;
    return fd - UNIX_SOCK_FD_BASE;
}

static int alloc_slot(void)
{
    for (int i = 0; i < UNIX_SOCK_MAX; i++) {
        if (!socks[i].used) {
            memset(&socks[i], 0, sizeof(socks[i]));
            socks[i].used      = true;
            socks[i].state     = SK_CREATED;
            socks[i].peer_idx  = PEER_NONE;
            socks[i].rhd = socks[i].rtl = 0;
            socks[i].n_wait    = 0;
            return i;
        }
    }
    return -1;
}

static int push_rx(USock *dst, const void *data, size_t len)
{
    const uint8_t *p = (const uint8_t *)data;
    for (size_t i = 0; i < len; i++) {
        size_t next = (dst->rhd + 1) % RX_CAP;
        if (next == dst->rtl)
            return -ENOMEM;
        dst->rx[dst->rhd] = p[i];
        dst->rhd = next;
    }
    return (int)len;
}

static int pop_rx(USock *s, void *buf, size_t len)
{
    uint8_t *out = (uint8_t *)buf;
    size_t n = 0;
    while (n < len && s->rtl != s->rhd) {
        out[n++] = s->rx[s->rtl];
        s->rtl = (s->rtl + 1) % RX_CAP;
    }
    return (int)n;
}

bool unix_is_sockfd(int fd)
{
    int i = idx_from_fd(fd);
    return i >= 0 && socks[i].used;
}

int unix_socket(int domain, int type, int protocol)
{
    (void)protocol;
    if (domain != AF_UNIX)
        return -EINVAL;
    if (type != SOCK_STREAM)
        return -EINVAL;
    int i = alloc_slot();
    if (i < 0)
        return -ENOMEM;
    return UNIX_SOCK_FD_BASE + i;
}

int unix_socketpair(int domain, int type, int protocol, int *sv)
{
    (void)protocol;
    if (!sv)
        return -EFAULT;
    if (domain != AF_UNIX || type != SOCK_STREAM)
        return -EINVAL;
    int a = alloc_slot();
    if (a < 0)
        return -ENOMEM;
    int b = alloc_slot();
    if (b < 0) {
        socks[a].used = false;
        return -ENOMEM;
    }
    socks[a].state    = SK_CONNECTED;
    socks[b].state    = SK_CONNECTED;
    socks[a].peer_idx = b;
    socks[b].peer_idx = a;
    sv[0] = UNIX_SOCK_FD_BASE + a;
    sv[1] = UNIX_SOCK_FD_BASE + b;
    return 0;
}

int unix_bind(int fd, const void *addr, size_t addrlen)
{
    int i = idx_from_fd(fd);
    if (i < 0 || !socks[i].used)
        return -EBADF;
    USock *s = &socks[i];
    if (s->state != SK_CREATED)
        return -EINVAL;
    if (!addr || addrlen < 3)
        return -EINVAL;
    uint16_t fam = *(const uint16_t *)addr;
    if (fam != AF_UNIX)
        return -EINVAL;
    size_t plen = addrlen - sizeof(uint16_t);
    if (plen == 0 || plen >= sizeof(s->path))
        return -EINVAL;
    memcpy(s->path, (const char *)addr + sizeof(uint16_t), plen);
    s->path[plen] = '\0';
    if (s->path[0] == '\0')
        return -EINVAL; /* abstract: not yet */

    if (vfs_resolve(s->path) >= 0)
        return -EADDRINUSE;
    for (int k = 0; k < UNIX_SOCK_MAX; k++) {
        if (!socks[k].used || k == i)
            continue;
        if (socks[k].path[0] && strcmp(socks[k].path, s->path) == 0)
            return -EADDRINUSE;
    }
    int tfd = vfs_open(s->path, VFS_O_RDWR | VFS_O_CREATE | VFS_O_TRUNC);
    if (tfd < 0)
        return -EIO;
    vfs_close(tfd);
    s->state = SK_BOUND;
    return 0;
}

int unix_listen(int fd, int backlog)
{
    (void)backlog;
    int i = idx_from_fd(fd);
    if (i < 0 || !socks[i].used)
        return -EBADF;
    USock *s = &socks[i];
    if (s->state != SK_BOUND)
        return -EINVAL;
    s->state = SK_LISTENING;
    return 0;
}

static int find_listener(const char *path)
{
    for (int k = 0; k < UNIX_SOCK_MAX; k++) {
        if (!socks[k].used)
            continue;
        if (socks[k].state != SK_LISTENING)
            continue;
        if (strcmp(socks[k].path, path) == 0)
            return k;
    }
    return -1;
}

int unix_accept(int fd, void *addr, size_t *addrlen)
{
    (void)addr;
    if (addrlen)
        *addrlen = 0;
    int i = idx_from_fd(fd);
    if (i < 0 || !socks[i].used)
        return -EBADF;
    USock *L = &socks[i];
    if (L->state != SK_LISTENING)
        return -EINVAL;
    if (L->n_wait == 0)
        return -EAGAIN;
    int si = L->listen_q[0];
    memmove(L->listen_q, L->listen_q + 1, (size_t)(L->n_wait - 1) * sizeof(int));
    L->n_wait--;
    if (si < 0 || si >= UNIX_SOCK_MAX || !socks[si].used)
        return -EIO;
    return UNIX_SOCK_FD_BASE + si;
}

int unix_connect(int fd, const void *addr, size_t addrlen)
{
    int ci = idx_from_fd(fd);
    if (ci < 0 || !socks[ci].used)
        return -EBADF;
    USock *C = &socks[ci];
    if (C->state != SK_CREATED)
        return -EINVAL;
    if (!addr || addrlen < 3)
        return -EINVAL;
    if (*(const uint16_t *)addr != AF_UNIX)
        return -EINVAL;
    char path[120];
    size_t plen = addrlen - sizeof(uint16_t);
    if (plen == 0 || plen >= sizeof(path))
        return -EINVAL;
    memcpy(path, (const char *)addr + sizeof(uint16_t), plen);
    path[plen] = '\0';

    int li = find_listener(path);
    if (li < 0)
        return -ECONNREFUSED;
    USock *L = &socks[li];

    int si = alloc_slot();
    if (si < 0)
        return -ENOMEM;
    USock *S = &socks[si];
    S->state    = SK_CONNECTED;
    C->state    = SK_CONNECTED;
    C->peer_idx = si;
    S->peer_idx = ci;
    if (L->n_wait >= ACCEPT_Q) {
        S->used = false;
        C->peer_idx = PEER_NONE;
        C->state    = SK_CREATED;
        return -ECONNREFUSED;
    }
    L->listen_q[L->n_wait++] = si;
    return 0;
}

int unix_sock_read(int fd, void *buf, size_t len)
{
    int i = idx_from_fd(fd);
    if (i < 0 || !socks[i].used)
        return -EBADF;
    USock *s = &socks[i];
    if (s->state != SK_CONNECTED)
        return -ENOTCONN;
    return pop_rx(s, buf, len);
}

int unix_sock_write(int fd, const void *buf, size_t len)
{
    int i = idx_from_fd(fd);
    if (i < 0 || !socks[i].used)
        return -EBADF;
    USock *s = &socks[i];
    if (s->state != SK_CONNECTED || s->peer_idx == PEER_NONE)
        return -ENOTCONN;
    USock *peer = &socks[s->peer_idx];
    return push_rx(peer, buf, len);
}

int unix_sock_close(int fd)
{
    int i = idx_from_fd(fd);
    if (i < 0 || !socks[i].used)
        return -EBADF;
    USock *s = &socks[i];
    if (s->peer_idx != PEER_NONE && s->peer_idx >= 0 && s->peer_idx < UNIX_SOCK_MAX) {
        USock *p = &socks[s->peer_idx];
        if (p->used && p->peer_idx == i)
            p->peer_idx = PEER_NONE;
    }
    s->peer_idx = PEER_NONE;
    if ((s->state == SK_BOUND || s->state == SK_LISTENING) && s->path[0])
        vfs_unlink(s->path);
    s->used = false;
    memset(s, 0, sizeof(*s));
    return 0;
}
