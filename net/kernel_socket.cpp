/*
Copyright 2022 The Photon Authors

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
*/

#include "socket.h"

#include <inttypes.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/epoll.h>
#include <sys/sendfile.h>
#include <unistd.h>
#include <memory>
#include <unordered_map>

#include <photon/common/alog.h>
#include <photon/common/iovector.h>
#include <photon/io/fd-events.h>
#include <photon/thread/thread11.h>
#include <photon/thread/list.h>
#include <photon/thread/timer.h>
#include <photon/common/utility.h>
#include <photon/common/event-loop.h>
#include <photon/net/basic_socket.h>
#include <photon/net/tlssocket.h>
#include <photon/net/utils.h>
#include <photon/net/zerocopy.h>

#include "base_socket.h"

#ifndef SO_ZEROCOPY
#define SO_ZEROCOPY 60
#endif

namespace photon {
namespace net {

bool ISocketStream::skip_read(size_t count) {
    if (!count) return true;
    while(count) {
        static char buf[1024];
        size_t len = count < sizeof(buf) ? count : sizeof(buf);
        ssize_t ret = read(buf, len);
        if (ret < (ssize_t)len) return false;
        count -= len;
    }
    return true;
}

static int fill_path(struct sockaddr_un& name, const char* path, size_t count) {
    const int LEN = sizeof(name.sun_path) - 1;
    if (count == 0) count = strlen(path);
    if (count > LEN)
        LOG_ERROR_RETURN(ENAMETOOLONG, -1, "pathname is too long (`>`)", count,
                         LEN);

    memset(&name, 0, sizeof(name));
    memcpy(name.sun_path, path, count + 1);
#ifndef __linux__
    name.sun_len = 0;
#endif
    name.sun_family = AF_UNIX;
    return 0;
}

class KernelSocketStream : public SocketStreamBase {
public:
    int fd = -1;
protected:
    uint64_t m_timeout = -1;
public:
    explicit KernelSocketStream(int fd) : fd(fd) {}
    KernelSocketStream(int socket_family, bool nonblocking) {
        if (nonblocking) {
            fd = net::socket(socket_family, SOCK_STREAM, 0);
        } else {
            fd = ::socket(socket_family, SOCK_STREAM, 0);
        }
        if (fd > 0 && socket_family == AF_INET) {
            int val = 1;
            ::setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &val, sizeof(val));
        }
    }
    virtual ~KernelSocketStream() {
        if (fd > 0) shutdown(ShutdownHow::ReadWrite);
        if (fd <= 0) return;
        close();
    }

    virtual ssize_t read(void* buf, size_t count) override {
        return net::read_n(fd, buf, count, m_timeout);
    }
    virtual ssize_t write(const void* buf, size_t count) override {
        return net::send_n(fd, buf, count, 0, m_timeout);
    }
    virtual ssize_t readv(const struct iovec* iov, int iovcnt) override {
        SmartCloneIOV<32> ciov(iov, iovcnt);
        return readv_mutable(ciov.ptr, iovcnt);
    }
    virtual ssize_t readv_mutable(struct iovec* iov, int iovcnt) override {
        return net::readv_n(fd, iov, iovcnt, m_timeout);
    }
    virtual ssize_t writev(const struct iovec* iov, int iovcnt) override {
        SmartCloneIOV<32> ciov(iov, iovcnt);
        return writev_mutable(ciov.ptr, iovcnt);
    }
    virtual ssize_t writev_mutable(struct iovec* iov, int iovcnt) override {
        return net::sendv_n(fd, iov, iovcnt, 0, m_timeout);
    }
    virtual ssize_t recv(void* buf, size_t count, int flags = 0) override {
        return net::read(fd, buf, count, m_timeout);
    }
    virtual ssize_t recv(const struct iovec* iov, int iovcnt, int flags = 0) override {
        return net::readv(fd, iov, iovcnt, m_timeout);
    }
    virtual ssize_t send(const void* buf, size_t count, int flags = 0) override {
        return net::send(fd, buf, count, 0, m_timeout);
    }
    virtual ssize_t send(const struct iovec* iov, int iovcnt, int flags = 0) override {
        return net::sendv(fd, iov, iovcnt, 0, m_timeout);
    }
    virtual ssize_t sendfile(int in_fd, off_t offset, size_t count) override {
        return net::sendfile_n(fd, in_fd, &offset, count);
    }
    virtual int shutdown(ShutdownHow how) override {
        // shutdown how defined as 0 for RD, 1 for WR and 2 for RDWR
        // in sys/socket.h, cast ShutdownHow into int just fits
        return ::shutdown(fd, static_cast<int>(how));
    }
    virtual Object* get_underlay_object(uint64_t recursion = 0) override {
        return (Object*) (uint64_t) fd;
    }
    virtual int close() override {
        auto ret = ::close(fd);
        fd = -1;
        return ret;
    }
    typedef int (*Getter)(int sockfd, struct sockaddr* addr, socklen_t* addrlen);
    int do_getname(EndPoint& addr, Getter getter) {
        struct sockaddr_in addr_in;
        socklen_t len = sizeof(addr_in);
        int ret = getter(fd, (struct sockaddr*)&addr_in, &len);
        if (ret < 0 || len != sizeof(addr_in)) return -1;
        addr.from_sockaddr_in(addr_in);
        return 0;
    }
    virtual int getsockname(EndPoint& addr) override {
        return do_getname(addr, &::getsockname);
    }
    virtual int getpeername(EndPoint& addr) override {
        return do_getname(addr, &::getpeername);
    }
    int do_getname(char* path, size_t count, Getter getter) {
        struct sockaddr_un addr_un;
        socklen_t len = sizeof(addr_un);
        int ret = getter(fd, (struct sockaddr*)&addr_un, &len);
        // if len larger than size of addr_un, or less than prefix in addr_un
        if (ret < 0 || len > sizeof(addr_un) ||
            len <= sizeof(addr_un.sun_family))
            return -1;

        strncpy(path, addr_un.sun_path, count);
        return 0;
    }
    virtual int getsockname(char* path, size_t count) override {
        return do_getname(path, count, &::getsockname);
    }
    virtual int getpeername(char* path, size_t count) override {
        return do_getname(path, count, &::getpeername);
    }
    virtual int setsockopt(int level, int option_name, const void* option_value,
                           socklen_t option_len) override {
        return ::setsockopt(fd, level, option_name, option_value, option_len);
    }
    virtual int getsockopt(int level, int option_name, void* option_value,
                           socklen_t* option_len) override {
        return ::getsockopt(fd, level, option_name, option_value, option_len);
    }
    virtual uint64_t timeout() const override { return m_timeout; }
    virtual void timeout(uint64_t tm) override { m_timeout = tm; }
};

template<typename SocketStreamType, typename...Ts>
static KernelSocketStream* socket_stream_ctor(Ts...xs) { return new SocketStreamType(xs...); }

class KernelSocketClient : public SocketClientBase {
public:
    KernelSocketClient(int socket_family, bool nonblocking) :
            m_socket_family(socket_family),
            m_nonblocking(nonblocking) {}

    ISocketStream* connect(const char* path, size_t count) override {
        struct sockaddr_un addr_un;
        int ret = fill_path(addr_un, path, count);
        if (ret < 0) {
            LOG_ERROR_RETURN(0, nullptr, "Failed to fill uds addr");
        }
        return do_connect((const sockaddr*) &addr_un, nullptr, sizeof(addr_un));
    }

    ISocketStream* connect(EndPoint remote, EndPoint local = EndPoint()) override {
        sockaddr_in addr_remote = remote.to_sockaddr_in();
        sockaddr_in addr_local = local.to_sockaddr_in();
        return do_connect((sockaddr*) &addr_remote, local.empty() ? nullptr : (sockaddr*) &addr_local,
                          sizeof(addr_remote));
    }

protected:
    using ConnectFunc = int (*)(int fd, const sockaddr*, socklen_t addrlen, uint64_t timeout);
    using StreamCtor = KernelSocketStream* (*)(int socket_family, bool non_blocking);

    int m_socket_family;
    bool m_nonblocking;

    ConnectFunc m_connect_func = &net::connect;
    StreamCtor m_stream_ctor = &socket_stream_ctor<KernelSocketStream>;

    ISocketStream* do_connect(const sockaddr* remote, const sockaddr* local, socklen_t addrlen) {
        auto s = m_stream_ctor(m_socket_family, m_nonblocking);
        std::unique_ptr<KernelSocketStream> sock(s);
        if (!sock || sock->fd < 0) {
            LOG_ERROR_RETURN(0, nullptr, "Failed to create socket fd");
        }
        for (auto& opt : m_opts) {
            auto ret = sock->setsockopt(opt.level, opt.opt_name, opt.opt_val, opt.opt_len);
            if (ret < 0) {
                LOG_ERROR_RETURN(EINVAL, nullptr, "Failed to setsockopt ", VALUE(opt.level), VALUE(opt.opt_name));
            }
        }
        sock->timeout(m_timeout);
        if (local != nullptr) {
            if (::bind(sock->fd, local, addrlen) != 0) {
                LOG_ERRNO_RETURN(0, nullptr, "fail to bind socket");
            }
        }
        auto ret = m_connect_func(sock->fd, remote, addrlen, m_timeout);
        if (ret < 0) {
            LOG_ERRNO_RETURN(0, nullptr, "Failed to connect socket");
        }
        return sock.release();
    }
};

class KernelSocketServer : public SocketServerBase {
public:
    KernelSocketServer(int socket_family, bool autoremove, bool nonblocking) :
            m_socket_family(socket_family),
            m_autoremove(autoremove),
            m_nonblocking(nonblocking) {
    }

    ~KernelSocketServer() {
        terminate();

        if (m_listen_fd >= 0) {
            ::shutdown(m_listen_fd, static_cast<int>(ShutdownHow::ReadWrite));
        }
        if (m_listen_fd < 0) {
            return;
        }
        if (m_autoremove) {
            char filename[PATH_MAX];
            if (0 == do_getname(filename, PATH_MAX, &::getsockname)) {
                unlink(filename);
            }
        }
        ::close(m_listen_fd);
        m_listen_fd = -1;
    }

    int start_loop(bool block) override {
        if (workth) LOG_ERROR_RETURN(EALREADY, -1, "Already listening");
        if (block) return accept_loop();
        auto th = photon::thread_create11(&KernelSocketServer::accept_loop, this);
        photon::thread_yield_to(th);
        return 0;
    }

    void terminate() override {
        if (workth) {
            auto th = workth;
            workth = nullptr;
            if (waiting) {
                photon::thread_interrupt(th);
                photon::thread_yield_to(th);
            }
        }
    }

    ISocketServer* set_handler(Handler handler) override {
        m_handler = handler;
        return this;
    }

    int bind(uint16_t port, IPAddr addr) override {
        if (setup_listen_fd() != 0) {
            return -1;
        }
        auto addr_in = EndPoint(addr, port).to_sockaddr_in();
        return ::bind(m_listen_fd, (struct sockaddr*)&addr_in, sizeof(addr_in));
    }

    int bind(const char* path, size_t count) override {
        if (setup_listen_fd() != 0) {
            return -1;
        }
        if (m_autoremove && is_socket(path)) {
            unlink(path);
        }
        struct sockaddr_un addr_un;
        int ret = fill_path(addr_un, path, count);
        if (ret < 0) return -1;
        return ::bind(m_listen_fd, (struct sockaddr*) &addr_un, sizeof(addr_un));
    }

    int listen(int backlog) override {
        return ::listen(m_listen_fd, backlog);
    }

    ISocketStream* accept(EndPoint* remote_endpoint = nullptr) override {
        int cfd = remote_endpoint ? do_accept(*remote_endpoint) : do_accept();
        return cfd < 0 ? nullptr : m_stream_ctor(cfd);
    }

    Object* get_underlay_object(uint64_t recursion = 0) override {
        return (Object*) (uint64_t) m_listen_fd;
    }

    int getsockname(EndPoint& addr) override {
        return do_getname(addr, &::getsockname);
    }

    int getpeername(EndPoint& addr) override {
        return do_getname(addr, &::getpeername);
    }

    int getsockname(char* path, size_t count) override {
        return do_getname(path, count, &::getsockname);
    }

    int getpeername(char* path, size_t count) override {
        return do_getname(path, count, &::getpeername);
    }

protected:
    using AcceptFunc = int (*)(int fd, struct sockaddr* addr, socklen_t* addrlen, uint64_t timeout);
    using StreamCtor = KernelSocketStream* (*)(int fd);
    using Getter = int (*)(int sockfd, struct sockaddr* addr, socklen_t* addrlen);

    int m_socket_family;
    bool m_autoremove;
    bool m_nonblocking;

    Handler m_handler;
    photon::thread* workth = nullptr;
    bool waiting = false;
    int m_listen_fd = -1;

    AcceptFunc m_accept_func = &net::accept;
    StreamCtor m_stream_ctor = &socket_stream_ctor<KernelSocketStream>;

    int do_accept() { return m_accept_func(m_listen_fd, nullptr, nullptr, m_timeout); }

    int do_accept(EndPoint& remote_endpoint) {
        struct sockaddr_in addr_in;
        socklen_t len = sizeof(addr_in);
        int cfd = m_accept_func(m_listen_fd, (struct sockaddr*) &addr_in, &len, m_timeout);
        if (cfd < 0 || len != sizeof(addr_in)) return -1;
        remote_endpoint.from_sockaddr_in(addr_in);
        return cfd;
    }

    int do_getname(EndPoint& addr, Getter getter) const {
        struct sockaddr_in addr_in;
        socklen_t len = sizeof(addr_in);
        int ret = getter(m_listen_fd, (struct sockaddr*) &addr_in, &len);
        if (ret < 0 || len != sizeof(addr_in)) return -1;
        addr.from_sockaddr_in(addr_in);
        return 0;
    }

    int do_getname(char* path, size_t count, Getter getter) const {
        struct sockaddr_un addr_un;
        socklen_t len = sizeof(addr_un);
        int ret = getter(m_listen_fd, (struct sockaddr*) &addr_un, &len);
        // if len larger than size of addr_un, or less than prefix in addr_un
        if (ret < 0 || len > sizeof(addr_un) || len <= sizeof(addr_un.sun_family))
            return -1;
        strncpy(path, addr_un.sun_path, count);
        return 0;
    }

    static bool is_socket(const char* path) {
        struct stat statbuf;
        return (0 == stat(path, &statbuf)) ? S_ISSOCK(statbuf.st_mode) : false;
    }

    int accept_loop() {
        if (workth) LOG_ERROR_RETURN(EALREADY, -1, "Already listening");
        workth = photon::CURRENT;
        DEFER(workth = nullptr);
        while (workth) {
            waiting = true;
            auto sess = accept();
            waiting = false;
            if (!workth) return 0;
            if (sess) {
                photon::thread_create11(&KernelSocketServer::handler, m_handler, sess);
            } else {
                photon::thread_usleep(1000);
            }
        }
        return 0;
    }

    static void handler(Handler m_handler, ISocketStream* sess) {
        m_handler(sess);
        delete sess;
    }

    virtual int setup_listen_fd() {
        if (m_nonblocking) {
            m_listen_fd = net::socket(m_socket_family, SOCK_STREAM, 0);
        } else {
            m_listen_fd = ::socket(m_socket_family, SOCK_STREAM, 0);
        }
        if (m_listen_fd < 0) {
            LOG_ERRNO_RETURN(0, -1, "fail to setup listen fd");
        }
        if (m_socket_family == AF_INET) {
            int val = 1;
            if (::setsockopt(m_listen_fd, IPPROTO_TCP, TCP_NODELAY, &val, sizeof(val)) != 0) {
                LOG_ERRNO_RETURN(EINVAL, -1, "failed to setsockopt of TCP_NODELAY");
            }
        }
        for (auto& opt : m_opts) {
            auto ret = ::setsockopt(m_listen_fd, opt.level, opt.opt_name, opt.opt_val, opt.opt_len);
            if (ret < 0) {
                LOG_ERRNO_RETURN(EINVAL, -1, "failed to setsockopt ", VALUE(opt.level), VALUE(opt.opt_name));
            }
        }
        return 0;
    }
};

class ZeroCopySocketStream : public KernelSocketStream {
protected:
    net::ZerocopyEventEntry* m_event_entry;
    uint32_t m_num_calls;
    bool m_socket_error;

public:
    explicit ZeroCopySocketStream(int fd) : KernelSocketStream(fd) {
        m_num_calls = 0;
        m_socket_error = false;
        m_event_entry = new ZerocopyEventEntry(fd);
    }

    ~ZeroCopySocketStream() { delete m_event_entry; }

    ssize_t writev_mutable(iovec* iov, int iovcnt) override {
        ssize_t n_written;
        auto iov_view = iovector_view(iov, iovcnt);
        size_t sum = iov_view.sum();

        n_written = zerocopy_n(fd, iov_view.iov, iovcnt, m_num_calls, m_timeout);
        if (n_written != (ssize_t)sum) {
            LOG_ERRNO_RETURN(0, n_written, "zerocopy failed");
        }

        int ret = m_event_entry->zerocopy_wait(m_num_calls - 1, m_timeout);
        if (ret == 0) {
            m_socket_error = true;
            LOG_ERRNO_RETURN(ETIMEDOUT, -1, "zerocopy wait timeout (active)")
        }
        if (m_socket_error) {
            LOG_ERRNO_RETURN(ETIMEDOUT, -1, "zerocopy wait timeout (passive)");
        }
        return n_written;
    }
};

class ZeroCopySocketServer : public KernelSocketServer {
public:
    ZeroCopySocketServer(int socket_family, bool autoremove, bool nonblocking) :
            KernelSocketServer(socket_family, autoremove, nonblocking) {
        m_stream_ctor = &socket_stream_ctor<ZeroCopySocketStream>;
    }

protected:
    int setup_listen_fd() override {
        int ret = KernelSocketServer::setup_listen_fd();
        if (ret != 0) {
            return ret;
        }
        int v = 1;
        ret = setsockopt(-SOL_SOCKET, SO_ZEROCOPY, &v, sizeof(v));
        if (ret != 0) {
            LOG_ERRNO_RETURN(0, -1, "fail to set sock opt of SO_ZEROCOPY");
        }
        return 0;
    }
};

class IouringSocketStream : public KernelSocketStream {
public:
    using KernelSocketStream::KernelSocketStream;

    ssize_t read(void* buf, size_t count) override {
        uint64_t timeout = m_timeout;
        auto cb = LAMBDA_TIMEOUT(photon::iouring_pread(fd, buf, count, 0, timeout));
        return net::doio_n(buf, count, cb);
    }

    ssize_t write(const void* buf, size_t count) override {
        uint64_t timeout = m_timeout;
        auto cb = LAMBDA_TIMEOUT(do_send(fd, buf, count, 0, timeout));
        return net::doio_n((void*&) buf, count, cb);
    }

    ssize_t readv(const iovec* iov, int iovcnt) override {
        SmartCloneIOV<8> clone(iov, iovcnt);
        iovector_view view(clone.ptr, iovcnt);
        uint64_t timeout = m_timeout;
        auto cb = LAMBDA_TIMEOUT(photon::iouring_preadv(fd, view.iov, view.iovcnt, 0, timeout));
        return net::doiov_n(view, cb);
    }

    ssize_t writev(const iovec* iov, int iovcnt) override {
        SmartCloneIOV<8> clone(iov, iovcnt);
        iovector_view view(clone.ptr, iovcnt);
        uint64_t timeout = m_timeout;
        auto cb = LAMBDA_TIMEOUT(do_sendmsg(fd, view.iov, view.iovcnt, 0, timeout));
        return net::doiov_n(view, cb);
    }

    ssize_t recv(void* buf, size_t count, int flags = 0) override {
        return photon::iouring_pread(fd, buf, count, 0, m_timeout);
    }

    ssize_t recv(const iovec* iov, int iovcnt, int flags = 0) override {
        return photon::iouring_preadv(fd, iov, iovcnt, 0, m_timeout);
    }

    ssize_t send(const void* buf, size_t count, int flags = 0) override {
        return do_send(fd, buf, count, flags, m_timeout);
    }

    ssize_t send(const iovec* iov, int iovcnt, int flags = 0) override {
        return do_sendmsg(fd, iov, iovcnt, flags, m_timeout);
    }

private:
    static ssize_t do_send(int fd, const void* buf, size_t count, int flags, uint64_t timeout) {
        return photon::iouring_send(fd, buf, count, flags | MSG_NOSIGNAL, timeout);
    }

    static ssize_t do_sendmsg(int fd, const iovec* iov, int iovcnt, int flags, uint64_t timeout) {
        msghdr msg = {};
        msg.msg_iov = (iovec*) iov;
        msg.msg_iovlen = iovcnt;
        return photon::iouring_sendmsg(fd, &msg, flags | MSG_NOSIGNAL, timeout);
    }
};

class IouringSocketClient : public KernelSocketClient {
public:
    IouringSocketClient(int socket_family, bool nonblocking) :
            KernelSocketClient(socket_family, nonblocking) {
        m_stream_ctor = &socket_stream_ctor<IouringSocketStream>;
        m_connect_func = &photon::iouring_connect;
    }
};

class IouringSocketServer : public KernelSocketServer {
public:
    IouringSocketServer(int socket_family, bool autoremove, bool nonblocking) :
            KernelSocketServer(socket_family, autoremove, nonblocking) {
        m_stream_ctor = &socket_stream_ctor<IouringSocketStream>;
        m_accept_func = &photon::iouring_accept;
    }
};

/* ET Socket - Start */

static photon::EventsMap<EVENT_READ, EVENT_WRITE, EVENT_ERROR> et_evmap(EPOLLIN | EPOLLRDHUP, EPOLLOUT, EPOLLERR);

struct NotifyContext {
    photon::thread* waiter[3] = {nullptr, nullptr, nullptr};

    int wait_for_state(uint32_t event, uint64_t timeout) {
        waiter[event >> 1] = photon::CURRENT;
        auto ret = photon::thread_usleep(timeout);
        waiter[event >> 1] = nullptr;
        auto eno = &errno;
        if (ret == 0) {
            *eno = ETIMEDOUT;
            return -1;
        }
        if (*eno != EOK) {
            LOG_DEBUG("failed when wait for events: ", ERRNO(* eno));
            return -1;
        }
        return 0;
    }

    void on_event(int ep_event) {
        auto ev = et_evmap.translate_bitwisely(ep_event);
        for (int i = 0; i < 3; i++) {
            if (waiter[i] && (ev & (1 << i))) {
                photon::thread_interrupt(waiter[i], EOK);
            }
        }
    }

    int wait_for_readable(uint64_t timeout) {
        return wait_for_state(EVENT_READ, timeout);
    }

    int wait_for_writable(uint64_t timeout) {
        return wait_for_state(EVENT_WRITE, timeout);
    }
};

struct ETPoller {
    int epfd = 0;
    EventLoop* loop = nullptr;

    int init() {
        epfd = epoll_create(1);
        loop = new_event_loop({this, &ETPoller::waiter},
                              {this, &ETPoller::notify});
        loop->async_run();
        return epfd;
    }

    int fini() {
        loop->stop();
        if (epfd > 0) {
            close(epfd);
            epfd = 0;
        }
        delete loop;
        loop = nullptr;
        return 0;
    }

    int waiter(EventLoop*) {
        auto ret = photon::wait_for_fd_readable(epfd, 1 * 1000UL);
        // LOG_DEBUG("WAITER ",VALUE(ret));
        auto err = errno;
        if (ret == 0)
            return 1;
        else if (err == ETIMEDOUT)
            return 0;
        else
            return -1;
    }

    int notify(EventLoop*) {
        struct epoll_event evs[1024];
        auto ret = epoll_wait(epfd, evs, 1024, 0);
        for (int i = 0; i < ret; i++) {
            auto c = (NotifyContext*) evs[i].data.ptr;
            c->on_event(evs[i].events);
        }
        return 0;
    }

    int register_notifier(int fd, NotifyContext* context) {
        struct epoll_event ev;
        ev.events = EPOLLIN | EPOLLOUT | EPOLLRDHUP | EPOLLET;
        ev.data.ptr = context;
        return epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &ev);
    }

    int unregister_notifier(int fd) {
        return epoll_ctl(epfd, EPOLL_CTL_DEL, fd, nullptr);
    }
};

thread_local ETPoller etpoller;

int et_poller_init() { return etpoller.init(); }

int et_poller_fini() { return etpoller.fini(); }

template<typename IOCB, typename WAITCB>
static __FORCE_INLINE__ ssize_t etdoio(IOCB iocb, WAITCB waitcb) {
    while (true) {
        ssize_t ret = iocb();
        if (ret < 0) {
            auto e = errno;  // errno is usually a macro that expands to a function call
            if (e == EINTR) continue;
            if (e == EAGAIN || e == EWOULDBLOCK || e == EINPROGRESS) {
                if (waitcb())  // non-zero result means timeout or
                    // interrupt, need to return
                    return ret;
                continue;
            }
        }
        return ret;
    }
}

class ETKernelSocketStream : public KernelSocketStream, public NotifyContext {
public:
    explicit ETKernelSocketStream(int fd) : KernelSocketStream(fd) {
        etpoller.register_notifier(fd, this);
    }

    ETKernelSocketStream() : KernelSocketStream(AF_INET, true) {
        etpoller.register_notifier(fd, this);
    }

    ~ETKernelSocketStream() {
        if (fd <= 0) return;
        etpoller.unregister_notifier(fd);
    }

    ssize_t recv(void* buf, size_t count, int flags = 0) override {
        auto timeout = m_timeout;
        return etdoio(LAMBDA(::read(fd, buf, count)), LAMBDA_TIMEOUT(wait_for_readable(timeout)));
    }

    ssize_t recv(const struct iovec* iov, int iovcnt, int flags = 0) override {
        auto timeout = m_timeout;
        return etdoio(LAMBDA(::readv(fd, iov, iovcnt)), LAMBDA_TIMEOUT(wait_for_readable(timeout)));
    }

    ssize_t send(const void* buf, size_t count, int flags = 0) override {
        return do_send(buf, count, flags);
    }

    ssize_t send(const struct iovec* iov, int iovcnt, int flags = 0) override {
        return do_send(iov, iovcnt, flags);
    }

    ssize_t sendfile(int in_fd, off_t offset, size_t count) override {
        void* buf_unused = nullptr;
        return doio_n(buf_unused, count, LAMBDA(do_sendfile(in_fd, offset, count)));
    }

protected:
    ssize_t do_send(const void* buf, size_t count, int flags = 0) {
        auto timeout = m_timeout;
        return etdoio(LAMBDA(::send(fd, buf, count, MSG_NOSIGNAL | flags)),
                      LAMBDA_TIMEOUT(wait_for_writable(timeout)));
    }

    ssize_t do_send(const struct iovec* iov, int iovcnt, int flags = 0) {
        auto timeout = m_timeout;
        return etdoio(
                [&]() -> ssize_t {
                    struct msghdr msg{};
                    msg.msg_iov = (iovec*) iov;
                    msg.msg_iovlen = iovcnt;
                    auto ret = ::sendmsg(fd, &msg, MSG_NOSIGNAL | flags);
                    return ret;
                },
                LAMBDA_TIMEOUT(wait_for_writable(timeout)));
    }

    ssize_t do_sendfile(int in_fd, off_t offset, size_t count) {
        auto timeout = m_timeout;
        return etdoio(LAMBDA(::sendfile(this->fd, in_fd, &offset, count)),
                      LAMBDA_TIMEOUT(wait_for_writable(timeout)));
    }
};

class ETKernelSocketClient : public KernelSocketClient {
public:
    ETKernelSocketClient(int socket_family, bool nonblocking) :
            KernelSocketClient(socket_family, nonblocking) {
        m_stream_ctor = nullptr;
        m_connect_func = nullptr;
    }

    ISocketStream* connect(EndPoint remote, EndPoint local = EndPoint()) override {
        sockaddr_in addr_remote = remote.to_sockaddr_in();
        sockaddr_in addr_local = local.to_sockaddr_in();
        auto sock = create_socket();
        if (!sock) return nullptr;
        auto ret = do_connect(sock, (struct sockaddr*) &addr_remote,
                              local.empty() ? nullptr : (sockaddr*) &addr_local,
                              sizeof(addr_remote), m_timeout);
        if (ret < 0) {
            delete sock;
            LOG_ERRNO_RETURN(0, nullptr, "Failed to connect to ", remote);
        }
        return sock;
    }

    ISocketStream* connect(const char* path, size_t count) override {
        struct sockaddr_un addr_un;
        int ret = fill_path(addr_un, path, count);
        if (ret < 0) {
            LOG_ERROR_RETURN(0, nullptr, "Failed to fill uds addr");
        }
        auto sock = create_socket();
        if (!sock) return nullptr;
        ret = do_connect(sock, (struct sockaddr*) &addr_un, nullptr, sizeof(addr_un), m_timeout);
        if (ret < 0) return nullptr;
        return sock;
    }

private:
    static int do_connect(ETKernelSocketStream* sock, const sockaddr* remote, const sockaddr* local,
                          socklen_t addrlen, uint64_t timeout) {
        if (local != nullptr) {
            if (::bind(sock->fd, local, addrlen) != 0) {
                LOG_ERRNO_RETURN(0, -1, "fail to bind socket");
            }
        }
        int err = 0;
        while (true) {
            int ret = ::connect(sock->fd, remote, addrlen);
            if (ret < 0) {
                auto e = errno;  // errno is usually a macro that expands to a function call
                if (e == EINTR) {
                    err = 1;
                    continue;
                }
                if (e == EINPROGRESS || (e == EADDRINUSE && err == 1)) {
                    sock->wait_for_writable(timeout);
                    socklen_t n = sizeof(err);
                    ret = ::getsockopt(sock->fd, SOL_SOCKET, SO_ERROR, &err, &n);
                    if (ret < 0) return -1;
                    if (err) {
                        errno = err;
                        return -1;
                    }
                    return 0;
                }
            }
            return ret;
        }
    }

    ETKernelSocketStream* create_socket() {
        auto sock = new ETKernelSocketStream();
        if (sock->fd < 0) LOG_ERROR_RETURN(0, nullptr, "Failed to create socket fd");
        for (auto& opt : m_opts) {
            auto ret = sock->setsockopt(opt.level, opt.opt_name, opt.opt_val, opt.opt_len);
            if (ret < 0) {
                delete sock;
                LOG_ERROR_RETURN(EINVAL, nullptr, "Failed to setsockopt ",
                                 VALUE(opt.level), VALUE(opt.opt_name));
            }
        }
        sock->timeout(m_timeout);
        return sock;
    }
};

class ETKernelSocketServer : public KernelSocketServer, public NotifyContext {
public:
    ETKernelSocketServer(int socket_family, bool autoremove, bool nonblocking) :
            KernelSocketServer(socket_family, autoremove, nonblocking) {
        m_stream_ctor = nullptr;
        m_accept_func = nullptr;
    }

    ~ETKernelSocketServer() {
        if (m_listen_fd >= 0) {
            etpoller.unregister_notifier(m_listen_fd);
        }
    }

    ISocketStream* accept(EndPoint* remote_endpoint = nullptr) override {
        int cfd = remote_endpoint ? et_accept(*remote_endpoint) : et_accept();
        return cfd < 0 ? nullptr : new ETKernelSocketStream(cfd);
    }

protected:
    int setup_listen_fd() override {
        int ret = KernelSocketServer::setup_listen_fd();
        if (ret != 0) {
            return ret;
        }
        etpoller.register_notifier(m_listen_fd, this);
        return 0;
    }

private:
    int fd_accept(int fd, struct sockaddr* addr, socklen_t* addrlen, uint64_t timeout) {
        return (int) etdoio(LAMBDA(::accept4(fd, addr, addrlen, SOCK_NONBLOCK)),
                            LAMBDA_TIMEOUT(wait_for_readable(timeout)));
    }

    int et_accept() {
        return fd_accept(m_listen_fd, nullptr, nullptr, m_timeout);
    }

    int et_accept(EndPoint& remote_endpoint) {
        struct sockaddr_in addr_in;
        socklen_t len = sizeof(addr_in);
        int cfd = fd_accept(m_listen_fd, (struct sockaddr*) &addr_in, &len, m_timeout);
        if (cfd < 0 || len != sizeof(addr_in)) return -1;
        remote_endpoint.from_sockaddr_in(addr_in);
        return cfd;
    }
};

/* ET Socket - End */

LogBuffer& operator<<(LogBuffer& log, const IPAddr addr) {
    return log.printf(addr.d, '.', addr.c, '.', addr.b, '.', addr.a);
}

LogBuffer& operator<<(LogBuffer& log, const EndPoint ep) {
    return log << ep.addr << ':' << ep.port;
}

LogBuffer& operator<<(LogBuffer& log, const in_addr& iaddr) {
    return log << net::IPAddr(ntohl(iaddr.s_addr));
}

LogBuffer& operator<<(LogBuffer& log, const sockaddr_in& addr) {
    return log << net::EndPoint(addr);
}

LogBuffer& operator<<(LogBuffer& log, const sockaddr& addr) {
    if (addr.sa_family == AF_INET) {
        log << (const sockaddr_in&)addr;
    } else {
        log.printf("<sockaddr>");
    }
    return log;
}

extern "C" ISocketClient* new_tcp_socket_client() {
    return new KernelSocketClient(AF_INET, true);
}
extern "C" ISocketServer* new_tcp_socket_server() {
    return new KernelSocketServer(AF_INET, false, true);
}
extern "C" ISocketServer* new_zerocopy_tcp_server() {
    return new ZeroCopySocketServer(AF_INET, false, true);
}
extern "C" ISocketClient* new_iouring_tcp_client() {
    return new IouringSocketClient(AF_INET, false);
}
extern "C" ISocketServer* new_iouring_tcp_server() {
    return new IouringSocketServer(AF_INET, false, true);
}
extern "C" ISocketClient* new_uds_client() {
    return new KernelSocketClient(AF_UNIX, true);
}
extern "C" ISocketServer* new_uds_server(bool autoremove) {
    return new KernelSocketServer(AF_UNIX, autoremove, true);
}
extern "C" ISocketClient* new_et_tcp_socket_client() {
    return new ETKernelSocketClient(AF_INET, true);
}
extern "C" ISocketServer* new_et_tcp_socket_server() {
    return new ETKernelSocketServer(AF_INET, false, true);
}

}
}
