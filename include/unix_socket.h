#pragma once
#include "types.h"

#define AF_UNIX  1
#define SOCK_STREAM 1

#define UNIX_SOCK_FD_BASE 128
#define UNIX_SOCK_MAX     24

void   unix_socket_init(void);
bool   unix_is_sockfd(int fd);
int    unix_sock_read(int fd, void *buf, size_t len);
int    unix_sock_write(int fd, const void *buf, size_t len);
int    unix_sock_close(int fd);

int unix_socket(int domain, int type, int protocol);
int unix_socketpair(int domain, int type, int protocol, int *sv);
int unix_bind(int fd, const void *addr, size_t addrlen);
int unix_listen(int fd, int backlog);
int unix_accept(int fd, void *addr, size_t *addrlen);
int unix_connect(int fd, const void *addr, size_t addrlen);
