#pragma once

#include <sys/types.h>
#include <sys/uio.h>
#include <sys/socket.h>
#include <poll.h>
#include <cstdint>
#include <cerrno>

namespace photon {

ssize_t iouring_pread(int fd, void* buf, size_t count, off_t offset, uint64_t timeout);

ssize_t iouring_pwrite(int fd, const void* buf, size_t count, off_t offset, uint64_t timeout);

ssize_t iouring_preadv(int fd, const iovec* iov, int iovcnt, off_t offset, uint64_t timeout);

ssize_t iouring_pwritev(int fd, const iovec* iov, int iovcnt, off_t offset, uint64_t timeout);

ssize_t iouring_send(int fd, const void* buf, size_t len, int flags, uint64_t timeout);

ssize_t iouring_recv(int fd, void* buf, size_t len, int flags, uint64_t timeout);

ssize_t iouring_sendmsg(int fd, const msghdr* msg, int flags, uint64_t timeout);

ssize_t iouring_recvmsg(int fd, msghdr* msg, int flags, uint64_t timeout);

int iouring_connect(int fd, const sockaddr* addr, socklen_t addrlen, uint64_t timeout);

int iouring_accept(int fd, sockaddr* addr, socklen_t* addrlen, uint64_t timeout);

int iouring_fsync(int fd);

int iouring_fdatasync(int fd);

int iouring_open(const char* path, int flags, mode_t mode);

int iouring_mkdir(const char* path, mode_t mode);

int iouring_close(int fd);

struct iouring
{
    static ssize_t pread(int fd, void *buf, size_t count, off_t offset, uint64_t timeout = -1)
    {
        return iouring_pread(fd, buf, count, offset, timeout);
    }
    static ssize_t preadv(int fd, const struct iovec *iov, int iovcnt, off_t offset, uint64_t timeout = -1)
    {
        return iouring_preadv(fd, iov, iovcnt, offset, timeout);
    }
    static ssize_t pwrite(int fd, const void *buf, size_t count, off_t offset, uint64_t timeout = -1)
    {
        return iouring_pwrite(fd, buf, count, offset, timeout);
    }
    static ssize_t pwritev(int fd, const struct iovec *iov, int iovcnt, off_t offset, uint64_t timeout = -1)
    {
        return iouring_pwritev(fd, iov, iovcnt, offset, timeout);
    }
    static int fsync(int fd, uint64_t timeout = -1)
    {
        return iouring_fsync(fd);
    }
    static int fdatasync(int fd, uint64_t timeout = -1)
    {
        return iouring_fdatasync(fd);
    }
    static int close(int fd)
    {
        return iouring_close(fd);
    }
};

}
