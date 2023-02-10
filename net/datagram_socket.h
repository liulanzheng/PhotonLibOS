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

    template <typename B, typename S>
    ssize_t send(B* buf, S count, int flags = 0) {
        return ((IMessage*)this)->send(buf, count, nullptr, 0, flags);
    }
    template <typename B, typename S>
    ssize_t recv(B* buf, S count, int flags = 0) {
        return ((IMessage*)this)->recv(buf, count, nullptr, nullptr, flags);
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

    Addr set_addr(struct sockaddr_in&, EndPoint = {});
    void load_addr(struct sockaddr_in&, EndPoint*);

public:
    int connect(const EndPoint ep) {
        struct sockaddr_in addrin;
        return connect(set_addr(addrin, ep));
    }
    int bind(const EndPoint ep) {
        struct sockaddr_in addrin;
        return bind(set_addr(addrin, ep));
    }
    template <typename B, typename S>
    ssize_t sendto(B* buf, S count, const EndPoint to, int flags = 0) {
        struct sockaddr_in addrin;
        return base::sendto(buf, count, set_addr(addrin, to), flags);
    }
    template <typename B, typename S>
    ssize_t recvfrom(B* buf, S count, EndPoint* from, int flags = 0) {
        struct sockaddr_in addrin;
        auto ret = base::recvfrom(buf, count, set_addr(addrin), flags);
        load_addr(addrin, from);
        return ret;
    }
};

class UDS_DatagramSocket : public IDatagramSocket {
protected:
    using base = IDatagramSocket;
    using base::bind;
    using base::connect;

    Addr set_addr(struct sockaddr_un&, const char* = nullptr);
    void load_addr(struct sockaddr_un&, char*, size_t);

public:
    int connect(const char* ep) {
        struct sockaddr_un addrun;
        return connect(set_addr(addrun, ep));
    }
    int bind(const char* ep) {
        struct sockaddr_un addrun;
        return bind(set_addr(addrun, ep));
    }
    template <typename B, typename S>
    ssize_t sendto(B* buf, S count, const char* to, int flags = 0) {
        struct sockaddr_un addrun;
        return base::sendto(buf, count, set_addr(addrun, to), flags);
    }
    template <typename B, typename S>
    ssize_t recvfrom(B* buf, S count, char* from, size_t addrlen,
                     int flags = 0) {
        struct sockaddr_un addrun;
        auto ret = base::recvfrom(buf, count, set_addr(addrun), flags);
        load_addr(addrun, from, addrlen);
        return ret;
    }
};

UDPSocket* new_udp_socket();

UDS_DatagramSocket* new_uds_datagram_socket();

}  // namespace net
}  // namespace photon
