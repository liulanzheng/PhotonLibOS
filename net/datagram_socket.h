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

#include <photon/common/message.h>
#include <photon/net/socket.h>

namespace photon {
namespace net {

constexpr static size_t SOCKET_ADDRESS_BUFFER_SIZE = 128;

class IDatagramSocket : public IMessage,
                        public ISocketBase,
                        public ISocketName {
protected:
    struct Addr {
        size_t len = sizeof(buf);
        char buf[SOCKET_ADDRESS_BUFFER_SIZE];

        Addr() = default;
        Addr(EndPoint);
        Addr(const char*);
        // maybe supports IPV6 later...

        void to_endpoint(EndPoint*);
        void to_path(char*, size_t);
    };

    template <typename B, typename S>
    ssize_t sendto(B* buf, S count, const Addr* addr, int flags = 0) {
        return ((IMessage*)this)->send(buf, count, addr->buf, addr->len, flags);
    }
    template <typename B, typename S>
    ssize_t recvfrom(B* buf, S count, Addr* from, int flags = 0) {
        return ((IMessage*)this)
            ->recv(buf, count, from->buf, &from->len, flags);
    }

public:
    virtual int connect(Addr* addr) = 0;
    virtual int bind(Addr* addr) = 0;

    template <typename B, typename S>
    ssize_t send(B* buf, S count, int flags = 0) {
        return ((IMessage*)this)->send(buf, count, nullptr, 0, flags);
    }
    template <typename B, typename S>
    ssize_t recv(B* buf, S count, int flags = 0) {
        return ((IMessage*)this)->recv(buf, count, nullptr, nullptr, flags);
    }
};

class UDPSocket : public IDatagramSocket {
protected:
    using base = IDatagramSocket;
    using base::bind;
    using base::connect;

public:
    int connect(const EndPoint ep) {
        Addr a(ep);
        return connect(&a);
    }
    int bind(const EndPoint ep) {
        Addr a(ep);
        return bind(&a);
    }
    template <typename B, typename S>
    ssize_t sendto(B* buf, S count, const EndPoint ep, int flags = 0) {
        Addr a(ep);
        return base::sendto(buf, count, &a, flags);
    }
    template <typename B, typename S>
    ssize_t recvfrom(B* buf, S count, EndPoint* from, int flags = 0) {
        Addr a;
        auto ret = base::recvfrom(buf, count, &a, flags);
        a.to_endpoint(from);
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
        Addr a(ep);
        return connect(&a);
    }
    int bind(const char* ep) {
        Addr a(ep);
        return bind(&a);
    }
    template <typename B, typename S>
    ssize_t sendto(B* buf, S count, const char* ep, int flags = 0) {
        Addr a(ep);
        return base::sendto(buf, count, &a, flags);
    }
    // Unix Domain Socket can not detect recvfrom address
    // just ignore it, and forward to `recv` method
    template <typename B, typename S>
    ssize_t recvfrom(B* buf, S count, char* from, size_t len, int flags = 0) {
        return base::recv(buf, count, flags);
    }
};

UDPSocket* new_udp_socket();

UDS_DatagramSocket* new_uds_datagram_socket();

}  // namespace net
}  // namespace photon
