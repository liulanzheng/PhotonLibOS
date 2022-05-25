#include <photon/common/alog-stdstring.h>
#include <photon/common/alog.h>
#include <photon/net/socket.h>
#include <photon/photon.h>
#include <photon/rpc/rpc.h>
#include <photon/io/signalfd.h>
#include <sys/fcntl.h>
#include <unistd.h>

#include "protocol.h"

static photon::net::EndPoint ep;

// Server side rpc handler
struct ExampleService {
    // public methods named `do_rpc_service` takes rpc requests
    // and produce response
    // able to set or operate connection directly(like close or set flags)
    // iov is temporary buffer created by skeleton with defined allocator
    // able to use as temporary buffer
    // return value will be droped
    int do_rpc_service(Testrun::Request* req, Testrun::Response* resp,
                       IOVector* iov, IStream* conn) {
        LOG_INFO(VALUE(req->someuint));
        LOG_INFO(VALUE(req->someint64));
        LOG_INFO(VALUE(req->somestruct.foo), VALUE(req->somestruct.bar));
        LOG_INFO(VALUE(req->somestr.c_str()));
        LOG_INFO(VALUE((char*)req->buf.begin()->iov_base));

        iov->push_back((void*)"some response", 14);

        resp->len = 14;
        resp->buf.assign(iov->iovec(), iov->iovcnt());

        return 0;
    }

    int do_rpc_service(Echo::Request* req, Echo::Response* resp, IOVector*,
                       IStream*) {

        resp->str = req->str;

        return 0;
    }

    int do_rpc_service(Heartbeat::Request* req, Heartbeat::Response* resp,
                       IOVector*, IStream*) {
        resp->now = photon::now;

        return 0;
    }

    int do_rpc_service(ReadBuffer::Request* req, ReadBuffer::Response* resp,
                       IOVector* iov, IStream*) {
        auto fd = ::open(req->fn.c_str(), O_RDONLY);
        if (fd < 0) {
            resp->ret = -errno;
            return 0;
        }
        DEFER(::close(fd));
        iov->push_back(4096);
        resp->ret = ::preadv(fd, iov->iovec(), iov->iovcnt(), 0);
        if (resp->ret < 0) {
            resp->ret = -errno;
            resp->buf.assign(nullptr, 0);
        } else {
            iov->shrink_to(resp->ret);
            resp->buf.assign(iov->iovec(), iov->iovcnt());
        }
        return 0;
    }

    int do_rpc_service(WriteBuffer::Request* req, WriteBuffer::Response* resp,
                       IOVector* iov, IStream*) {
        auto fd = ::open(req->fn.c_str(), O_WRONLY | O_CREAT | O_TRUNC);
        if (fd < 0) {
            resp->ret = -errno;
            return 0;
        }
        DEFER(::close(fd));
        iov->push_back(4096);
        resp->ret = ::pwritev(fd, req->buf.begin(), req->buf.size(), 0);
        if (resp->ret < 0) {
            resp->ret = -errno;
        }
        return 0;
    }
};

void handle_null(int) {}

int main(int argc, char** argv) {
    photon::init();
    DEFER(photon::fini());

    photon::sync_signal(SIGPIPE, &handle_null);
    // start server

    // construct rpcservice
    ExampleService rpcservice;

    // construct skeleton and register TestProtocol handler;
    auto skeleton = photon::rpc::new_skeleton();
    DEFER(delete skeleton);
    // register service, able to register multiple service
    skeleton
        ->register_service<Testrun, Heartbeat, Echo, ReadBuffer, WriteBuffer>(
            &rpcservice);

    // construct tcp server and start listen
    auto tcpserver = photon::net::new_tcp_socket_server();
    DEFER(delete tcpserver);
    tcpserver->bind();
    tcpserver->listen();

    auto handler = [&](photon::net::ISocketStream* sock) {
        // since tcp server will delete socket after finish
        // skeleton does not own socket
        LOG_INFO("Accept ", sock->getpeername());
        skeleton->serve(sock, false);
        return 0;
    };

    tcpserver->set_handler(handler);
    LOG_INFO("Listen at `", tcpserver->getsockname());
    tcpserver->start_loop(true);
}