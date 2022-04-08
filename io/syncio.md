# Photon中的同步I/O封装

## 文件描述符（fd）事件（photon/syncio/fd-events.h）
支持select和epoll（仅限于Linux系统）两种事件引擎。

```cpp
namespace photon { extern "C"
{
    // init the epoll event engine, installing to photon
    // an idle sleeping handler, which watches fd events by epoll_wait()
    int fd_events_epoll_init();
    int fd_events_epoll_fini();

    // init the select event engine, installing to photon
    // an idle sleeping handler, which watches fd events by select()
    int fd_events_select_init();
    int fd_events_select_fini();

    inline int fd_events_init()
    {
        return
#if defined(__linux__) && !defined(SELECT)
            fd_events_epoll_init();
#else
            fd_events_select_init();
#endif
    }
    inline int fd_events_fini()
    {
        return
#if defined(__linux__) && !defined(SELECT)
            fd_events_epoll_fini();
#else
            fd_events_select_fini();
#endif
    }

    // blocks current photon thread, and wait
    // for the fd to become readable / writable
    int wait_for_fd_readable(int fd, uint64_t timedout = -1);
    int wait_for_fd_writable(int fd, uint64_t timedout = -1);

    struct thread;
    // interrupt a photon `th` from another host OS thread
    // this will also interrupt idle sleeping, if possible
    // void safe_thread_interrupt(thread* th, int error_number = EINTR);
} }
```

## aio封装（photon/syncio/aio-wrapper.h）
可将操作系统的aio调用封装为photon内的同步调用（只阻塞当前photon线程），
支持libaio和posix-aio。

```cpp
namespace photon
{
    extern "C"
    {
        int libaio_wrapper_init();
        int libaio_wrapper_fini();

        // `fd` must be opened with O_DIRECT, and the buffers must be aligned
        ssize_t libaio_pread(int fd, void *buf, size_t count, off_t offset);
        ssize_t libaio_preadv(int fd, const struct iovec *iov, int iovcnt, off_t offset);
        ssize_t libaio_pwrite(int fd, const void *buf, size_t count, off_t offset);
        ssize_t libaio_pwritev(int fd, const struct iovec *iov, int iovcnt, off_t offset);
        static int libaio_fsync(int fd) { return 0; }

        ssize_t posixaio_pread(int fd, void *buf, size_t count, off_t offset);
        ssize_t posixaio_pwrite(int fd, const void *buf, size_t count, off_t offset);
        int posixaio_fsync(int fd);
        int posixaio_fdatasync(int fd);
    }
    
    // 分别将libaio和posixaio操作函数进一步封装为风格一致的成员函数，以便于泛型代码使用
    struct libaio
    {
        static ssize_t pread(int fd, void *buf, size_t count, off_t offset);
        static ssize_t preadv(int fd, const struct iovec *iov, int iovcnt, off_t offset);
        static ssize_t pwrite(int fd, const void *buf, size_t count, off_t offset);
        static ssize_t pwritev(int fd, const struct iovec *iov, int iovcnt, off_t offset);
        static int fsync(int fd);
        static int fdatasync(int fd);
    };

    // 分别将libaio和posixaio操作函数进一步封装为风格一致的成员函数，以便于泛型代码使用
    struct posixaio
    {
        static ssize_t pread(int fd, void *buf, size_t count, off_t offset);
        static ssize_t preadv(int fd, const struct iovec *iov, int iovcnt, off_t offset);
        static ssize_t pwrite(int fd, const void *buf, size_t count, off_t offset);
        static ssize_t pwritev(int fd, const struct iovec *iov, int iovcnt, off_t offset);
        static int fsync(int fd);
        static int fdatasync(int fd);
    };
}
```

## Socket API 封装（photon/synio/socket.h）
可将操作系统的非阻塞socket调用封装为photon内的同步调用（只阻塞当前photon线程）。
```cpp
namespace photon
{
    // 创建OS的socket，并将其设为non-block模式，以便于photon封装
    int socket(int domain, int type, int protocol);

    // 基本socket操作，语义均与OS提供的操作函数一致，但只阻塞（如果有）当前photon线程
    int connect(int fd, const struct sockaddr *addr, socklen_t addrlen, uint64_t timeout = -1);
    int accept(int fd, struct sockaddr* addr, socklen_t* addrlen, uint64_t timeout = -1);
    ssize_t read(int fd, void *buf, size_t count, uint64_t timeout = -1);
    ssize_t readv(int fd, const struct iovec *iov, int iovcnt, uint64_t timeout = -1);
    ssize_t write(int fd, const void *buf, size_t count, uint64_t timeout = -1);
    ssize_t writev(int fd, const struct iovec *iov, int iovcnt, uint64_t timeout = -1);
    ssize_t sendfile(int out_fd, int in_fd, off_t *offset, size_t count, uint64_t timeout = -1);

    // 增强型socket操作，在连接正常的情况下，会反复循环迭代，直到传输count字节的数据为止
    ssize_t read_n(int fd, void *buf, size_t count, uint64_t timeout = -1);
    ssize_t write_n(int fd, const void *buf, size_t count, uint64_t timeout = -1);
    ssize_t readv_n(int fd, struct iovec *iov, int iovcnt, uint64_t timeout = -1);
    ssize_t writev_n(int fd, struct iovec *iov, int iovcnt, uint64_t timeout = -1);
    ssize_t sendfile_n(int out_fd, int in_fd, off_t *offset, size_t count, uint64_t timeout = -1);

    // 辅助函数
    int set_socket_nonblocking(int fd);
    int set_fd_nonblocking(int fd);
}
```

## libcurl API 封装（photon/syncio/libcurl-adaptor.h）
将libcurl的异步非阻塞模式操作封装为基于photon的同步操作（只阻塞当前photon线程）。
```cpp
namespace photon { extern "C"
{
    int libcurl_init();

    int curl_perform(CURL *curl, uint64_t timeout);

    void libcurl_fini();

} }
```
