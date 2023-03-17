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

#include <unordered_map>

#include <photon/common/alog.h>
#include <photon/io/fd-events.h>
#include <photon/thread/thread11.h>
#include <photon/thread/timer.h>
#include <photon/net/basic_socket.h>

#include "base_socket.h"

namespace photon {
namespace net {

class TCPSocketPool;

class PooledTCPSocketStream : public ForwardSocketStream {
public:
    TCPSocketPool* pool;
    EndPoint ep;
    bool drop;

    PooledTCPSocketStream(ISocketStream* stream, TCPSocketPool* pool, const EndPoint& ep)
            : ForwardSocketStream(stream, false), pool(pool), ep(ep), drop(false) {}
    // release socket back to pool when dtor
    ~PooledTCPSocketStream() override;
    // forwarding all actions
    int shutdown(ShutdownHow how) override {
        drop = true;
        return m_underlay->shutdown(how);
    }

#define FORWARD_SOCK_ACT(how, action, count)                            \
    if (count == 0) return 0;                                           \
    auto ret = m_underlay->action;                                      \
    if (ret < 0 || (ShutdownHow::Read == ShutdownHow::how && ret == 0)) \
        drop = true;                                                    \
    return ret

    int close() override {
        drop = true;
        m_underlay->close();
        return 0;
    }
    ssize_t read(void* buf, size_t count) override {
        FORWARD_SOCK_ACT(Read, read(buf, count), count);
    }
    ssize_t write(const void* buf, size_t count) override {
        FORWARD_SOCK_ACT(Write, write(buf, count), count);
    }
    ssize_t readv(const struct iovec* iov, int iovcnt) override {
        FORWARD_SOCK_ACT(Read, readv(iov, iovcnt),
                         iovector_view((struct iovec*)iov, iovcnt).sum());
    }
    ssize_t readv_mutable(struct iovec* iov, int iovcnt) override {
        FORWARD_SOCK_ACT(Read, readv_mutable(iov, iovcnt),
                         iovector_view((struct iovec*)iov, iovcnt).sum());
    }
    ssize_t writev(const struct iovec* iov, int iovcnt) override {
        FORWARD_SOCK_ACT(Write, writev(iov, iovcnt),
                         iovector_view((struct iovec*)iov, iovcnt).sum());
    }
    ssize_t writev_mutable(struct iovec* iov, int iovcnt) override {
        FORWARD_SOCK_ACT(Write, writev_mutable(iov, iovcnt),
                         iovector_view((struct iovec*)iov, iovcnt).sum());
    }
    ssize_t recv(void* buf, size_t count, int flags = 0) override {
        FORWARD_SOCK_ACT(Read, recv(buf, count, flags), count);
    }
    ssize_t recv(const struct iovec* iov, int iovcnt, int flags = 0) override {
        FORWARD_SOCK_ACT(Read, recv(iov, iovcnt, flags),
                         iovector_view((struct iovec*)iov, iovcnt).sum());
    }
    ssize_t send(const void* buf, size_t count, int flags = 0) override {
        FORWARD_SOCK_ACT(Write, send(buf, count, flags), count);
    }
    ssize_t send(const struct iovec* iov, int iovcnt, int flags = 0) override {
        FORWARD_SOCK_ACT(Write, send(iov, iovcnt, flags),
                         iovector_view((struct iovec*)iov, iovcnt).sum());
    }
    ssize_t sendfile(int in_fd, off_t offset, size_t count) override {
        FORWARD_SOCK_ACT(Write, sendfile(in_fd, offset, count), count);
    }

#undef FORWARD_SOCK_ACT
};

struct StreamListNode : public intrusive_list_node<StreamListNode> {
    EndPoint key;
    std::unique_ptr<ISocketStream> stream;
    Timeout expire;

    StreamListNode() : expire(0) {}
    StreamListNode(EndPoint key, ISocketStream* stream, uint64_t expire)
            : key(key), stream(stream), expire(expire) {}
};

class TCPSocketPool : public ForwardSocketClient {
protected:
    CascadingEventEngine* ev;
    photon::thread* collector;
    std::unordered_map<EndPoint, intrusive_list<StreamListNode>> fdmap;
    uint64_t expiration;
    photon::Timer timer;

public:
    TCPSocketPool(ISocketClient* client, uint64_t expiration, bool client_ownership = false)
            : ForwardSocketClient(client, client_ownership),
              ev(photon::new_default_cascading_engine()),
              expiration(expiration),
              timer(expiration, {this, &TCPSocketPool::evict}) {
        collector = (photon::thread*) photon::thread_enable_join(
                photon::thread_create11(&TCPSocketPool::collect, this));
    }

    ~TCPSocketPool() override {
        timer.stop();
        auto th = collector;
        collector = nullptr;
        photon::thread_interrupt((photon::thread*)th);
        photon::thread_join((photon::join_handle*)th);
        for (auto &l : fdmap) {
            l.second.delete_all();
        }
        delete ev;
    }

    ISocketStream* connect(const char* path, size_t count) override {
        LOG_ERROR_RETURN(ENOSYS, nullptr,
                         "Socket pool supports TCP-like socket only");
    }

    ISocketStream* connect(EndPoint remote,
                           EndPoint local = EndPoint()) override {
    again:
        auto node = fdmap[remote].pop_front();
        if (!node) {
            ISocketStream* sock = m_underlay->connect(remote, local);
            if (sock) {
                return new PooledTCPSocketStream(sock, this, remote);
            }
            return nullptr;
        } else {
            auto fd = node->stream->get_underlay_fd();
            if (fd >= 0) {
                ev->rm_interest({fd, EVENT_READ, node});
            }
            if (fd >= 0 && wait_for_fd_readable(fd, 0) == 0) {
                delete node;
                goto again;
            }
            auto ret =
                new PooledTCPSocketStream(node->stream.release(), this, remote);
            delete node;
            return ret;
        }
    }

    void drop_from_pool(StreamListNode* node) {
        // remove fd interest
        auto fd = node->stream->get_underlay_fd();
        if (fd >= 0) {
            ev->rm_interest({(int)fd, EVENT_READ, node});
        }
        // or node have no record
        auto &list = fdmap[node->key];
        list.erase(node);
        delete node;
    }

    uint64_t evict() {
        // update time
        photon::thread_yield_to(photon::CURRENT);
        for (auto& n : fdmap) {
            auto& list = n.second;
            while (!list.empty() &&
                   list.front()->expire.expire() < photon::now) {
                drop_from_pool(list.pop_front());
            }
        }
        // remove empty entry in fdmap
        uint64_t near_expire = expiration;
        for (auto it = fdmap.begin(); it != fdmap.end();) {
            if (it->second.empty()) {
                it = fdmap.erase(it);
            } else {
                near_expire =
                    std::min(near_expire, it->second.front()->expire.timeout());
                it++;
            }
        }
        return near_expire;
    }

    bool release(EndPoint ep, ISocketStream* stream) {
        auto node = new StreamListNode(ep, stream, expiration);
        auto fd = stream->get_underlay_fd();
        if (fd >= 0) {
            // able to fetch fd
            // check by epoll
            if (wait_for_fd_readable(fd, 0) == 0) {
                return false;
            }
            ev->add_interest({fd, EVENT_READ, node});
        }
        // stream back to pool
        fdmap[ep].push_back(node);
        return true;
    }

    void collect() {
        StreamListNode* nodes[16];
        while (collector) {
            auto ret = ev->wait_for_events((void**)nodes, 16, -1UL);
            for (int i = 0; i < ret; i++) {
                // since destructed socket should never become readable before
                // it have been acquired again
                // if it is readable or RDHUP, both condition should treat as
                // socket shutdown
                auto node = nodes[i];
                drop_from_pool(node);
            }
        }
    }
};

PooledTCPSocketStream::~PooledTCPSocketStream() {
    if (drop || !pool->release(ep, m_underlay)) {
        delete m_underlay;
    }
}

extern "C" ISocketClient* new_tcp_socket_pool(ISocketClient* client, uint64_t expire, bool client_ownership) {
    return new TCPSocketPool(client, expire, client_ownership);
}

}
}
