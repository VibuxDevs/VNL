#pragma once
/* Subset of Linux errno (positive values; syscalls return -errno). */
#define EPERM            1
#define ENOENT           2
#define EINTR            4
#define EIO              5
#define EBADF            9
#define EFAULT          14
#define ENOMEM          12
#define ENODEV          19
#define EAGAIN          11
#define EINVAL          22
#define ENOTTY          25
#define ENOSYS          38
#define EADDRINUSE      98
#define ENOTCONN        107
#define ECONNREFUSED    111
#define ENOEXEC         8
#define E2BIG           7
