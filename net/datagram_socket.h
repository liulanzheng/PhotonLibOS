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

#pragma once

#include <netinet/in.h>
#include <photon/common/message.h>
#include <photon/net/basic_socket.h>
#include <photon/net/socket.h>
#include <sys/un.h>

namespace photon {
namespace net {

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Woverloaded-virtual"

// Those interfaces intensionally hide overloading virtual methods
// supperess the warning here.

class IDatagramSocket : public IMessage,
                        public ISocketBase,
                        public ISocketName {
public:
    struct Addr {
        struct sockaddr* addr;
        size_t len;
    };
    virtual int connect(Addr addr) = 0;
    virtual int bind(Addr addr) = 0;

    ssize_t send(const struct iovec* iov, int iovcnt, int flags = 0) {
        return ((IMessage*)this)->send(iov, iovcnt, nullptr, 0, flags);
    }
    ssize_t send(const void* buf, size_t count, int flags = 0) {
        return ((IMessage*)this)->send(buf, count, nullptr, 0, flags);
    }
    ssize_t recv(const struct iovec* iov, int iovcnt, int flags = 0) {
        return ((IMessage*)this)->recv(iov, iovcnt, nullptr, nullptr, flags);
    }
    ssize_t recv(void* buf, size_t count, int flags = 0) {
        iovec v{buf, count};
        return ((IMessage*)this)->recv(&v, 1, nullptr, nullptr, flags);
    }
    template <typename B, typename S>
    ssize_t sendto(B* buf, S count, const Addr to, int flags = 0) {
        return ((IMessage*)this)->send(buf, count, to.addr, to.len, flags);
    }
    template <typename B, typename S>
    ssize_t recvfrom(B* buf, S count, Addr from, int flags = 0) {
        return ((IMessage*)this)->recv(buf, count, from.addr, &from.len, flags);
    }
};

class UDPSocket : public IDatagramSocket {
protected:
    using base = IDatagramSocket;
    using base::bind;
    using base::connect;

public:
    int connect(const EndPoint ep) {
        auto addrin = ep.to_sockaddr_in();
        Addr addr{(struct sockaddr*)&addrin, sizeof(addrin)};
        return connect(addr);
    }
    int bind(const EndPoint ep) {
        auto addrin = ep.to_sockaddr_in();
        Addr addr{(struct sockaddr*)&addrin, sizeof(addrin)};
        return bind(addr);
    }
    ssize_t sendto(const struct iovec* iov, int iovcnt, const EndPoint to,
                   int flags = 0) {
        auto addrin = to.to_sockaddr_in();
        Addr addr{(struct sockaddr*)&addrin, sizeof(addrin)};
        return base::sendto(iov, iovcnt, addr, flags);
    }
    ssize_t sendto(const void* buf, size_t count, const EndPoint to,
                   int flags = 0) {
        auto addrin = to.to_sockaddr_in();
        Addr addr{(struct sockaddr*)&addrin, sizeof(addrin)};
        return base::sendto(buf, count, addr, flags);
    }
    ssize_t recvfrom(const struct iovec* iov, int iovcnt, EndPoint* from,
                     int flags = 0) {
        struct sockaddr_in addrin;
        Addr addr{(struct sockaddr*)&addrin, sizeof(addrin)};
        auto ret = base::recvfrom(iov, iovcnt, addr, flags);
        if (from) {
            from->from_sockaddr_in(addrin);
        }
        return ret;
    }
    ssize_t recvfrom(void* buf, size_t count, EndPoint* from, int flags = 0) {
        struct sockaddr_in addrin;
        Addr addr{(struct sockaddr*)&addrin, sizeof(addrin)};
        auto ret = base::recvfrom(buf, count, addr, flags);
        if (from) {
            from->from_sockaddr_in(addrin);
        }
        return ret;
    }
};

class UDS_DatagramSocket : public IDatagramSocket {
protected:
    using base = IDatagramSocket;
    using base::bind;
    using base::connect;

public:
    int connect(const char* ep) {
        struct sockaddr_un addrun;
        if (!ep || fill_uds_path(addrun, ep, 0) < 0) return -1;
        Addr addr{(struct sockaddr*)&addrun, sizeof(addrun)};
        return connect(addr);
    }
    int bind(const char* ep) {
        struct sockaddr_un addrun;
        if (!ep || fill_uds_path(addrun, ep, 0) < 0) return -1;
        Addr addr{(struct sockaddr*)&addrun, sizeof(addrun)};
        return bind(addr);
    }
    ssize_t sendto(const struct iovec* iov, int iovcnt, const char* to,
                   int flags = 0) {
        struct sockaddr_un addrun;
        if (to) fill_uds_path(addrun, to, 0);
        Addr addr{(struct sockaddr*)&addrun, sizeof(addrun)};
        return base::sendto(iov, iovcnt, addr, flags);
    }
    ssize_t sendto(const void* buf, size_t count, const char* to,
                   int flags = 0) {
        struct sockaddr_un addrun;
        if (to) fill_uds_path(addrun, to, 0);
        Addr addr{(struct sockaddr*)&addrun, sizeof(addrun)};
        return base::sendto(buf, count, addr, flags);
    }
    ssize_t recvfrom(const struct iovec* iov, int iovcnt, char* from,
                     size_t addrlen, int flags = 0) {
        struct sockaddr_un addrun;
        Addr addr{(struct sockaddr*)&addrun, sizeof(addrun)};
        auto ret = base::recvfrom(iov, iovcnt, addr, flags);
        if (from) {
            strncpy(from, addrun.sun_path,
                    std::min(addrlen - 1, sizeof(addrun.sun_path) - 1));
        }
        return ret;
    }
    ssize_t recvfrom(void* buf, size_t count, char* from, size_t addrlen,
                     int flags = 0) {
        struct sockaddr_un addrun;
        Addr addr{(struct sockaddr*)&addrun, sizeof(addrun)};
        auto ret = base::recvfrom(buf, count, addr, flags);
        if (from) {
            strncpy(from, addrun.sun_path,
                    std::min(addrlen - 1, sizeof(addrun.sun_path) - 1));
        }
        return ret;
    }
};

#pragma GCC diagnostic pop

UDPSocket* new_udp_socket();

UDS_DatagramSocket* new_uds_datagram_socket();

}  // namespace net
}  // namespace photon
