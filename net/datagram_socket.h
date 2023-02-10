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
        struct sockaddr* paddr;
        size_t size;

        static Addr from_endpoint(EndPoint ep);
        static Addr from_path(const char* ep);
    };
    virtual int connect(Addr addr) = 0;
    virtual int bind(Addr addr) = 0;

    ssize_t send(const struct iovec* iov, int iovcnt, int flags = 0) {
        return IMessage::send(iov, iovcnt, nullptr, 0, flags);
    }
    ssize_t send(const void* buf, size_t count, int flags = 0) {
        return IMessage::send(buf, count, nullptr, 0, flags);
    }
    ssize_t recv(const struct iovec* iov, int iovcnt, int flags = 0) {
        return IMessage::recv(iov, iovcnt, nullptr, nullptr, flags);
    }
    ssize_t recv(void* buf, size_t count, int flags = 0) {
        iovec v{buf, count};
        return IMessage::recv(&v, 1, nullptr, nullptr, flags);
    }
    template <typename B, typename S>
    ssize_t sendto(B* buf, S count, const Addr* to, int flags = 0) {
        return IMessage::send(buf, count, to->paddr, to->size, flags);
    }
    template <typename B, typename S>
    ssize_t recvfrom(B* buf, S count, Addr* from, int flags = 0) {
        return IMessage::recv(buf, count, from->paddr, &from->size, flags);
    }
};

class UDPSocket : public IDatagramSocket {
public:
    using base = IDatagramSocket;

    int connect(const EndPoint ep) {
        return base::connect(Addr::from_endpoint(ep));
    }
    int bind(const EndPoint ep) { return base::bind(Addr::from_endpoint(ep)); }
    ssize_t sendto(const struct iovec* iov, int iovcnt, const EndPoint to,
                   int flags = 0) {
        auto addr = Addr::from_endpoint(to);
        return base::sendto(iov, iovcnt, &addr, flags);
    }
    ssize_t sendto(const void* buf, size_t count, const EndPoint to,
                   int flags = 0) {
        auto addr = Addr::from_endpoint(to);
        return base::sendto(buf, count, &addr, flags);
    }
    ssize_t recvfrom(const struct iovec* iov, int iovcnt, EndPoint* from,
                     int flags = 0) {
        auto addr = Addr::from_endpoint({});
        auto ret = base::recvfrom(iov, iovcnt, &addr, flags);
        if (from) {
            from->from_sockaddr_in(*(struct sockaddr_in*)addr.paddr);
        }
        return ret;
    }
    ssize_t recvfrom(void* buf, size_t count, EndPoint* from, int flags = 0) {
        auto addr = Addr::from_endpoint({});
        auto ret = base::recvfrom(buf, count, &addr, flags);
        if (from) {
            from->from_sockaddr_in(*(struct sockaddr_in*)addr.paddr);
        }
        return ret;
    }
};

class UDS_DatagramSocket : public IDatagramSocket {
public:
    using base = IDatagramSocket;

    int connect(const char* ep) { return base::connect(Addr::from_path(ep)); }
    int bind(const char* ep) { return base::bind(Addr::from_path(ep)); }
    ssize_t sendto(const struct iovec* iov, int iovcnt, const char* to,
                   int flags = 0) {
        auto addr = Addr::from_path(to);
        return base::sendto(iov, iovcnt, &addr, flags);
    }
    ssize_t sendto(const void* buf, size_t count, const char* to,
                   int flags = 0) {
        auto addr = Addr::from_path(to);
        return base::sendto(buf, count, &addr, flags);
    }
    ssize_t recvfrom(const struct iovec* iov, int iovcnt, char* from,
                     size_t addrlen, int flags = 0) {
        auto addr = Addr::from_path({});
        auto ret = base::recvfrom(iov, iovcnt, &addr, flags);
        if (from) {
            auto addrun = (struct sockaddr_un*)addr.paddr;
            strncpy(from, addrun->sun_path,
                    std::min(addrlen, sizeof(addrun->sun_path)));
        }
        return ret;
    }
    ssize_t recvfrom(void* buf, size_t count, char* from, size_t addrlen,
                     int flags = 0) {
        auto addr = Addr::from_path({});
        auto ret = base::recvfrom(buf, count, &addr, flags);
        if (from) {
            auto addrun = (struct sockaddr_un*)addr.paddr;
            strncpy(from, addrun->sun_path,
                    std::min(addrlen, sizeof(addrun->sun_path)));
        }
        return ret;
    }
};

#pragma GCC diagnostic pop

UDPSocket* new_udp_socket();

UDS_DatagramSocket* new_uds_datagram_socket();

}  // namespace net
}  // namespace photon
