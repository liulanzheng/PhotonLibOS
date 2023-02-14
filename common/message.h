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
// #include <stddef.h>
#include <inttypes.h>
#include <sys/types.h>
#include <sys/uio.h>
#include "object.h"

class IMessage : public Object {
public:
    const static int Reliable       = (1 << 0);
    const static int PreserveOrder  = (1 << 1);
    virtual uint64_t flags() = 0;

    virtual uint64_t max_message_size() = 0;

    virtual ssize_t send(const struct iovec* iov, int iovcnt,
                         const void* to_addr = nullptr, int flags = 0) = 0;

    ssize_t send(const void* buf, size_t count,
                 const void* to_addr = nullptr, int flags = 0) {
        iovec v {(void*)buf, count};
        return send(&v, 1, to_addr, flags);
    }

    ssize_t sendto(const struct iovec* iov, int iovcnt,
                   const void* to_addr, int flags = 0) {
        return send(iov, iovcnt, to_addr, flags);
    }

    ssize_t sendto(const void* buf, size_t count,
                   const void* to_addr, int flags = 0) {
        return send(buf, count, to_addr, flags);
    }

    // if from_addr != nullptr, *(int*)from_addr MUST be sizeof(*from_addr)
    virtual ssize_t recv(const struct iovec* iov, int iovcnt,
                       void* from_addr = nullptr, int flags = 0) = 0;

    ssize_t recv(void* buf, size_t count,
                 void* from_addr = nullptr, int flags = 0) {
        iovec v {buf, count};
        return recv(&v, 1, from_addr, flags);
    }

    ssize_t recvfrom(const struct iovec* iov, int iovcnt,
                              void* from_addr, int flags = 0) {
        return recv(iov, iovcnt, from_addr, flags);
    }

    ssize_t recvfrom(void* buf, size_t count,
                     void* from_addr, int flags = 0) {
        return recv(buf, count, from_addr, flags);
    }
};
