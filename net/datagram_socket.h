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

class IDatagramSocket : public IMessage, public ISocketBase, public ISocketName {
public:
    virtual int connect(const void* addr) = 0;
    virtual int bind(const void* addr) = 0;
    int connect(const char* addr)   { return connect((void*)addr); }
    int connect(EndPoint ep)        { return connect(&ep); }
    int bind(const char* addr)      { return bind((void*)addr); }
    int bind(EndPoint ep)           { return bind(&ep); }

    ssize_t send(const struct iovec* iov, int iovcnt, int flags = 0) {
        return IMessage::send(iov, iovcnt, nullptr, flags);
    }
    ssize_t send(const void* buf, size_t count, int flags = 0) {
        return IMessage::send(buf, count, nullptr, flags);
    }
    ssize_t sendto(const struct iovec* iov, int iovcnt,
                               EndPoint to, int flags = 0) {
        return IMessage::sendto(iov, iovcnt, &to, flags);
    }
    ssize_t sendto(const void* buf, size_t count,
                       EndPoint to, int flags = 0) {
        return IMessage::sendto(buf, count, &to, flags);
    }
    ssize_t sendto(const struct iovec* iov, int iovcnt,
                            const char* to, int flags = 0) {
        return IMessage::sendto(iov, iovcnt, to, flags);
    }
    ssize_t sendto(const void* buf, size_t count,
                    const char* to, int flags = 0) {
        return IMessage::sendto(buf, count, to, flags);
    }

    ssize_t recv(const struct iovec* iov, int iovcnt, int flags = 0) {
        return IMessage::recv(iov, iovcnt, nullptr, flags);
    }
    ssize_t recv(void* buf, size_t count, int flags = 0) {
        iovec v {buf, count};
        return IMessage::recv(&v, 1, nullptr, flags);
    }
    // if from_addr != nullptr, *(int*)from_addr MUST be sizeof(*from_addr)
    ssize_t recvfrom(const struct iovec* iov, int iovcnt,
                              EndPoint* from, int flags = 0) {
        static_assert(sizeof(*from) >= sizeof(int));
        *(int*)from = sizeof(*from);
        return IMessage::recvfrom(iov, iovcnt, from, flags);
    }
    ssize_t recvfrom(void* buf, size_t count,
                EndPoint* from, int flags = 0) {
        static_assert(sizeof(*from) >= sizeof(int));
        *(int*)from = sizeof(*from);
        return IMessage::recvfrom(buf, count, from, flags);
    }
    ssize_t recvfrom(const struct iovec* iov, int iovcnt,
                     char* from, int addrlen, int flags = 0) {
        assert(addrlen >= sizeof(int));
        *(int*)from = addrlen;
        return IMessage::recvfrom(iov, iovcnt, from, flags);
    }
    ssize_t recvfrom(void* buf, size_t count,
                    char* from, int addrlen, int flags = 0) {
        assert(addrlen >= sizeof(int));
        *(int*)from = addrlen;
        return IMessage::recvfrom(buf, count, from, flags);
    }
};

IDatagramSocket* new_udp_socket();
IDatagramSocket* new_uds_datagram_socket();

}  // namespace net
}  // namespace photon