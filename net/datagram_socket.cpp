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
#include <photon/net/basic_socket.h>
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

struct EPAddr : public IDatagramSocket::Addr {
    using IDatagramSocket::Addr::paddr;
    using IDatagramSocket::Addr::size;
    struct sockaddr_in addr;
    EPAddr(EndPoint ep)
        : Addr{(struct sockaddr*)&addr, sizeof(addr)},
          addr(ep.to_sockaddr_in()) {}
};

struct PathAddr : public sockaddr_un, public IDatagramSocket::Addr {
    struct sockaddr_un addr;
    PathAddr(const char* path) : Addr{(struct sockaddr*)&addr, sizeof(addr)} {
        // make size = 0 so connect/receive will failed as EINVAL
        if (!path || fill_path(addr, path, strlen(path)) < 0) size = 0;
    }
};

IDatagramSocket::Addr IDatagramSocket::Addr::from_endpoint(EndPoint ep) {
    return EPAddr(ep);
};

IDatagramSocket::Addr IDatagramSocket::Addr::from_path(const char* path) {
    return PathAddr(path);
};

class UDPSocketImpl : public IDatagramSocket {
protected:
    int fd;
    uint64_t m_timeout;
    constexpr static size_t MAX_MESSAGE_SIZE = 65507;

public:
    UDPSocketImpl(int AF)
        : fd(::socket(AF, SOCK_DGRAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0)),
          m_timeout(-1) {}

    ~UDPSocketImpl() override {
        if (fd != -1) ::close(fd);
    }

    virtual uint64_t flags() override {
        return 0;  // not reliable and not preserve orders
    }

    virtual uint64_t max_message_size() override { return MAX_MESSAGE_SIZE; }

    virtual int connect(Addr addr) override {
        return doio(
            [&] {
                return ::connect(fd, (const struct sockaddr*)addr.paddr,
                                 sizeof(addr));
            },
            [&] { return photon::wait_for_fd_writable(fd); });
    }
    virtual int bind(Addr addr) override {
        return doio(
            [&] {
                return ::bind(fd, (const struct sockaddr*)addr.paddr,
                              sizeof(addr));
            },
            [&] { return photon::wait_for_fd_writable(fd); });
    }

    virtual ssize_t send(const struct iovec* iov, int iovcnt, const void* addr,
                 size_t addrlen, int flags = 0) override {
        struct msghdr hdr {
            .msg_name = (void*)addr, .msg_namelen = (socklen_t)addrlen,
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
    virtual ssize_t recv(const struct iovec* iov, int iovcnt, void* addr,
                 size_t* addrlen, int flags) override {
        struct msghdr hdr {
            .msg_name = addr, .msg_namelen = (socklen_t)*addrlen,
            .msg_iov = (struct iovec*)iov, .msg_iovlen = (size_t)iovcnt,
            .msg_control = nullptr, .msg_controllen = 0,
            .msg_flags = MSG_NOSIGNAL | flags,
        };
        auto ret =
            doio([&] { return ::recvmsg(fd, &hdr, MSG_DONTWAIT | flags); },
                 [&] { return photon::wait_for_fd_readable(fd); });
        if (addrlen) *addrlen = hdr.msg_namelen;
        return ret;
    }
    virtual Object* get_underlay_object(uint64_t recursion) override {
        return (Object*)(uint64_t)fd;
    }
    virtual int setsockopt(int level, int option_name, const void* option_value,
                   socklen_t option_len) override {
        return ::setsockopt(fd, level, option_name, option_value, option_len);
    };
    virtual int getsockopt(int level, int option_name, void* option_value,
                   socklen_t* option_len) override {
        return ::getsockopt(fd, level, option_name, option_value, option_len);
    }
    // get/set timeout, in us, (default +∞)
    virtual uint64_t timeout() const override { return m_timeout; }
    virtual void timeout(uint64_t tm) override { m_timeout = tm; }

    virtual int getsockname(EndPoint& addr) override {
        struct sockaddr_in buf;
        socklen_t len = sizeof(buf);
        auto ret = ::getsockname(fd, (struct sockaddr*)&buf, &len);
        if (ret >= 0 && buf.sin_family == AF_INET) addr.from_sockaddr_in(buf);
        return ret;
    }
    virtual int getpeername(EndPoint& addr) override {
        struct sockaddr_in buf;
        socklen_t len = sizeof(buf);
        auto ret = ::getpeername(fd, (struct sockaddr*)&buf, &len);
        if (ret >= 0 && buf.sin_family == AF_INET) addr.from_sockaddr_in(buf);
        return ret;
    }
    virtual int getsockname(char* path, size_t count) override {
        struct sockaddr_un buf;
        socklen_t len = sizeof(buf);
        auto ret = ::getsockname(fd, (struct sockaddr*)path, &len);
        if (ret >= 0 && buf.sun_family == AF_UNIX)
            strncpy(path, buf.sun_path, count);
        return ret;
    }
    virtual int getpeername(char* path, size_t count) override {
        struct sockaddr_un buf;
        socklen_t len = sizeof(buf);
        auto ret = ::getpeername(fd, (struct sockaddr*)path, &len);
        if (ret >= 0 && buf.sun_family == AF_UNIX)
            strncpy(path, buf.sun_path, count);
        return ret;
    }
};

inline IDatagramSocket* _new_udp_socket(int af) {
    auto ret = new UDPSocketImpl(af);
    if (ret->get_underlay_fd() < 0) {
        delete ret;
        ret = nullptr;
    }
    return ret;
}

UDPSocket* new_udp_socket() { return (UDPSocket*)_new_udp_socket(AF_INET); }

UDS_DatagramSocket* new_uds_datagram_socket() {
    return (UDS_DatagramSocket*)_new_udp_socket(AF_UNIX);
}

}  // namespace net
}  // namespace photon