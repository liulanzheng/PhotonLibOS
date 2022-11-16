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

#include "datagram_socket.h"

#include <photon/common/alog.h>
#include <photon/io/fd-events.h>
#include <photon/net/socket.h>
#include <sys/un.h>
#include <unistd.h>

namespace photon {
namespace net {

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

template <int AF>
class UDPSocket : public IDatagramSocket {
protected:
    int fd;
    uint64_t m_timeout;

public:
    UDPSocket()
        : fd(::socket(AF, SOCK_DGRAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0)),
          m_timeout(-1) {}

    ~UDPSocket() override {
        if (fd != -1) ::close(fd);
    }

    template <typename IO, typename WAIT>
    int doio(IO act, WAIT waitcb) {
        int ret;
        do {
            ret = act();
            ERRNO err;
            if (err.no == EWOULDBLOCK) {
                ret = waitcb();
                if (ret < 0) return ret;
            } else {
                return ret;
            }
            ret = act();
        } while (ret < 0);
        return ret;
    }

    int connect(const char* addr, size_t addrlen) override {
        struct sockaddr_un addr_un;
        int ret = fill_path(addr_un, addr, addrlen);
        if (ret < 0) return -1;
        return doio(
            [&] {
                return ::connect(fd, (const struct sockaddr*)&addr_un,
                                 sizeof(addr_un));
            },
            [&] { return photon::wait_for_fd_writable(fd); });
    }
    int connect(EndPoint ep) override {
        struct sockaddr_in addr_in = ep.to_sockaddr_in();
        return doio(
            [&] {
                return ::connect(fd, (const struct sockaddr*)&addr_in,
                                 sizeof(addr_in));
            },
            [&] { return photon::wait_for_fd_writable(fd); });
    }
    int bind(const char* addr, size_t addrlen) override {
        struct sockaddr_un addr_un;
        int ret = fill_path(addr_un, addr, addrlen);
        if (ret < 0) return -1;
        return doio(
            [&] {
                return ::bind(fd, (const struct sockaddr*)&addr_un,
                              sizeof(addr_un));
            },
            [&] { return photon::wait_for_fd_writable(fd); });
    }
    int bind(EndPoint ep) override {
        struct sockaddr_in addr_in = ep.to_sockaddr_in();
        return doio(
            [&] {
                return ::bind(fd, (const struct sockaddr*)&addr_in,
                              sizeof(addr_in));
            },
            [&] { return photon::wait_for_fd_writable(fd); });
    }
    ssize_t sendto(const void* buffer, size_t length, const char* addr,
                   size_t addrlen, int flags = 0) override {
        struct sockaddr_un addr_un;
        int ret = fill_path(addr_un, addr, addrlen);
        if (ret < 0) return -1;
        return doio(
            [&] {
                return ::sendto(
                    fd, buffer, length, MSG_NOSIGNAL | MSG_DONTWAIT | flags,
                    (const struct sockaddr*)&addr_un, sizeof(addr_un));
            },
            [&] { return photon::wait_for_fd_writable(fd); });
    }
    ssize_t sendto(const void* buffer, size_t length, EndPoint ep,
                   int flags = 0) override {
        struct sockaddr_in addr_in = ep.to_sockaddr_in();
        return doio(
            [&] {
                return ::sendto(
                    fd, buffer, length, MSG_NOSIGNAL | MSG_DONTWAIT | flags,
                    (const struct sockaddr*)&addr_in, sizeof(addr_in));
            },
            [&] { return photon::wait_for_fd_writable(fd); });
    }
    ssize_t sendto(const struct iovec* iov, int iovcnt, const char* addr,
                   size_t addrlen, int flags = 0) override {
        struct sockaddr_un addr_un;
        int ret = fill_path(addr_un, addr, addrlen);
        if (ret < 0) return -1;
        struct msghdr hdr {
            .msg_name = (void*)&addr_un, .msg_namelen = sizeof(addr_un),
            .msg_iov = (struct iovec*)iov, .msg_iovlen = (size_t)iovcnt,
            .msg_control = nullptr, .msg_controllen = 0,
            .msg_flags = MSG_NOSIGNAL | flags,
        };
        return doio(
            [&] {
                return ::sendmsg(fd, &hdr, MSG_NOSIGNAL | MSG_DONTWAIT | flags);
            },
            [&] { return photon::wait_for_fd_writable(fd); });
    }
    ssize_t sendto(const struct iovec* iov, int iovcnt, EndPoint ep,
                   int flags = 0) override {
        struct sockaddr_in addr_in = ep.to_sockaddr_in();
        struct msghdr hdr {
            .msg_name = (void*)&addr_in, .msg_namelen = sizeof(addr_in),
            .msg_iov = (struct iovec*)iov, .msg_iovlen = (size_t)iovcnt,
            .msg_control = nullptr, .msg_controllen = 0,
            .msg_flags = MSG_NOSIGNAL | flags,
        };
        return doio(
            [&] {
                return ::sendmsg(fd, &hdr, MSG_NOSIGNAL | MSG_DONTWAIT | flags);
            },
            [&] { return photon::wait_for_fd_writable(fd); });
    }
    ssize_t send(const void* buffer, size_t length, int flags) override {
        return doio(
            [&] {
                return ::send(fd, buffer, length,
                              MSG_NOSIGNAL | MSG_DONTWAIT | flags);
            },
            [&] { return photon::wait_for_fd_writable(fd); });
    }
    ssize_t send(const struct iovec* iov, int iovcnt, int flags) override {
        struct msghdr hdr {
            .msg_name = nullptr, .msg_namelen = 0,
            .msg_iov = (struct iovec*)iov, .msg_iovlen = (size_t)iovcnt,
            .msg_control = nullptr, .msg_controllen = 0,
            .msg_flags = MSG_NOSIGNAL | flags,
        };
        return doio(
            [&] {
                return ::sendmsg(fd, &hdr, MSG_NOSIGNAL | MSG_DONTWAIT | flags);
            },
            [&] { return photon::wait_for_fd_writable(fd); });
    }
    ssize_t recv(void* buffer, size_t length, int flags) override {
        return doio(
            [&] { return ::recv(fd, buffer, length, MSG_DONTWAIT | flags); },
            [&] { return photon::wait_for_fd_readable(fd); });
    }
    ssize_t recv(const struct iovec* iov, int iovcnt, int flags = 0) override {
        struct msghdr hdr {
            .msg_name = nullptr, .msg_namelen = 0,
            .msg_iov = (struct iovec*)iov, .msg_iovlen = (size_t)iovcnt,
            .msg_control = nullptr, .msg_controllen = 0,
            .msg_flags = MSG_NOSIGNAL | flags,
        };
        return doio([&] { return ::recvmsg(fd, &hdr, MSG_DONTWAIT | flags); },
                    [&] { return photon::wait_for_fd_readable(fd); });
    }
    ssize_t recvfrom(void* buffer, size_t length, char* addr, size_t addrlen,
                     int flags) override {
        struct sockaddr_un addr_un;
        socklen_t addr_un_size = sizeof(addr_un);
        auto ret = doio(
            [&] {
                return ::recvfrom(fd, buffer, length, MSG_DONTWAIT | flags,
                                  (struct sockaddr*)&addr_un, &addr_un_size);
            },
            [&] { return photon::wait_for_fd_readable(fd); });
        if (addr && ret >= 0 && addr_un.sun_family == AF_UNIX) {
            strncpy(addr, addr_un.sun_path, addrlen);
        }
        return ret;
    }
    ssize_t recvfrom(void* buffer, size_t length, EndPoint* ep,
                     int flags) override {
        struct sockaddr_in addr_in;
        socklen_t addr_in_size = sizeof(addr_in);
        auto ret = doio(
            [&] {
                return ::recvfrom(fd, buffer, length, MSG_DONTWAIT | flags,
                                  (struct sockaddr*)&addr_in, &addr_in_size);
            },
            [&] { return photon::wait_for_fd_readable(fd); });
        if (ep && ret >= 0 && addr_in.sin_family == AF_INET)
            ep->from_sockaddr_in(addr_in);
        return ret;
    }
    ssize_t recvfrom(const struct iovec* iov, int iovcnt, char* addr,
                     size_t addrlen, int flags = 0) override {
        struct sockaddr_un addr_un;
        struct msghdr hdr {
            .msg_name = &addr_un, .msg_namelen = sizeof(addr_un),
            .msg_iov = (struct iovec*)iov, .msg_iovlen = (size_t)iovcnt,
            .msg_control = nullptr, .msg_controllen = 0,
            .msg_flags = MSG_NOSIGNAL | flags,
        };
        auto ret =
            doio([&] { return ::recvmsg(fd, &hdr, MSG_DONTWAIT | flags); },
                 [&] { return photon::wait_for_fd_readable(fd); });
        if (addr && ret >= 0 && addr_un.sun_family == AF_UNIX)
            strncpy(addr, addr_un.sun_path, addrlen);
        return ret;
    }
    ssize_t recvfrom(const struct iovec* iov, int iovcnt, EndPoint* ep,
                     int flags = 0) override {
        struct sockaddr_in addr_in;
        struct msghdr hdr {
            .msg_name = &addr_in, .msg_namelen = sizeof(addr_in),
            .msg_iov = (struct iovec*)iov, .msg_iovlen = (size_t)iovcnt,
            .msg_control = nullptr, .msg_controllen = 0,
            .msg_flags = MSG_NOSIGNAL | flags,
        };
        auto ret =
            doio([&] { return ::recvmsg(fd, &hdr, MSG_DONTWAIT | flags); },
                 [&] { return photon::wait_for_fd_readable(fd); });
        if (ep && ret >= 0 && addr_in.sin_family == AF_INET)
            ep->from_sockaddr_in(addr_in);
        return ret;
    }
    Object* get_underlay_object(uint64_t recursion) override {
        return (Object*)(uint64_t)fd;
    }
    int setsockopt(int level, int option_name, const void* option_value,
                   socklen_t option_len) override {
        return ::setsockopt(fd, level, option_name, option_value, option_len);
    };
    int getsockopt(int level, int option_name, void* option_value,
                   socklen_t* option_len) override {
        return ::getsockopt(fd, level, option_name, option_value, option_len);
    }
    template <typename T>
    int setsockopt(int level, int option_name, T value) {
        return setsockopt(level, option_name, &value, sizeof(value));
    }
    template <typename T>
    int getsockopt(int level, int option_name, T* value) {
        socklen_t len = sizeof(*value);
        return getsockopt(level, option_name, value, &len);
    }

    // get/set timeout, in us, (default +∞)
    uint64_t timeout() const override { return m_timeout; }
    void timeout(uint64_t tm) override { m_timeout = tm; }

    int getsockname(EndPoint& addr) override {
        struct sockaddr_in buf;
        socklen_t len = sizeof(buf);
        auto ret = ::getsockname(fd, (struct sockaddr*)&buf, &len);
        if (ret >= 0 && buf.sin_family == AF_INET) addr.from_sockaddr_in(buf);
        return ret;
    }
    int getpeername(EndPoint& addr) override {
        struct sockaddr_in buf;
        socklen_t len = sizeof(buf);
        auto ret = ::getpeername(fd, (struct sockaddr*)&buf, &len);
        if (ret >= 0 && buf.sin_family == AF_INET) addr.from_sockaddr_in(buf);
        return ret;
    }
    int getsockname(char* path, size_t count) override {
        struct sockaddr_un buf;
        socklen_t len = sizeof(buf);
        auto ret = ::getsockname(fd, (struct sockaddr*)path, &len);
        if (ret >= 0 && buf.sun_family == AF_UNIX)
            strncpy(path, buf.sun_path, count);
        return ret;
    }
    int getpeername(char* path, size_t count) override {
        struct sockaddr_un buf;
        socklen_t len = sizeof(buf);
        auto ret = ::getpeername(fd, (struct sockaddr*)path, &len);
        if (ret >= 0 && buf.sun_family == AF_UNIX)
            strncpy(path, buf.sun_path, count);
        return ret;
    }
};

template <int AF>
inline IDatagramSocket* _new_udp_socket() {
    auto ret = new UDPSocket<AF>();
    if (ret->get_underlay_fd() < 0) {
        delete ret;
        ret = nullptr;
    }
    return ret;
}

IDatagramSocket* new_udp_socket() { return _new_udp_socket<AF_INET>(); }

IDatagramSocket* new_uds_datagram_socket() {
    return _new_udp_socket<AF_UNIX>();
}

}  // namespace net
}  // namespace photon