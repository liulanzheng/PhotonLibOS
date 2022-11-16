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

#include <photon/common/object.h>
#include <photon/net/socket.h>

namespace photon {
namespace net {

class IDatagramSocket : public ISocketBase, public ISocketName {
public:
    virtual int connect(const char* addr, size_t addrlen) = 0;
    virtual int connect(EndPoint ep) = 0;
    virtual int bind(const char* addr, size_t addrlen) = 0;
    virtual int bind(EndPoint ep) = 0;
    virtual ssize_t send(const void* buffer, size_t cnt, int flags = 0) = 0;
    virtual ssize_t send(const struct iovec* iov, int iovcnt,
                         int flags = 0) = 0;
    virtual ssize_t sendto(const void* buffer, size_t cnt, const char* addr,
                           size_t addrlen, int flags = 0) = 0;
    virtual ssize_t sendto(const void* buffer, size_t cnt, EndPoint ep,
                           int flags = 0) = 0;
    virtual ssize_t sendto(const struct iovec* iov, int iovcnt,
                           const char* addr, size_t addrlen, int flags = 0) = 0;
    virtual ssize_t sendto(const struct iovec* iov, int iovcnt, EndPoint ep,
                           int flags = 0) = 0;
    virtual ssize_t recv(void* buffer, size_t cnt, int flags = 0) = 0;
    virtual ssize_t recv(const struct iovec* iov, int iovcnt,
                         int flags = 0) = 0;
    virtual ssize_t recvfrom(void* buffer, size_t cnt, char* addr,
                             size_t addrlen, int flags = 0) = 0;
    virtual ssize_t recvfrom(const struct iovec* iov, int iovcnt, char* addr,
                             size_t addrlen, int flags = 0) = 0;
    virtual ssize_t recvfrom(void* buffer, size_t cnt, EndPoint* ep,
                             int flags = 0) = 0;
    virtual ssize_t recvfrom(const struct iovec* iov, int iovcnt, EndPoint* ep,
                             int flags = 0) = 0;
};

IDatagramSocket* new_udp_socket();
IDatagramSocket* new_uds_datagram_socket();

}  // namespace net
}  // namespace photon