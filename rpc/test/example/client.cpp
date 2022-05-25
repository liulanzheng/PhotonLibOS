#include <gflags/gflags.h>
#include <photon/common/alog-stdstring.h>
#include <photon/common/alog.h>
#include <photon/common/iovector.h>
#include <photon/net/socket.h>
#include <photon/photon.h>
#include <photon/rpc/rpc.h>
#include <sys/uio.h>

#include <ctime>

#include "protocol.h"

struct RPCClient {
    std::unique_ptr<photon::rpc::StubPool> pool;

    // create a tcp rpc connection pool
    // unused connections will be drop after 10 seconds(10UL*1000*1000)
    // TCP connection will failed in 1 second(1UL*1000*1000) if not accepted
    // and connection send/recv will take 5 socneds(5UL*1000*1000) as timedout
    RPCClient()
        : pool(photon::rpc::new_stub_pool(10UL * 1000 * 1000, 1UL * 1000 * 1000,
                                          5UL * 1000 * 1000)) {}

    int64_t RPCHeartbeat(photon::net::EndPoint ep) {
        Heartbeat::Request req;
        req.now = photon::now;
        Heartbeat::Response resp;
        int ret;

        auto stub = pool->get_stub(ep, false);
        if (!stub) return -1;
        DEFER(pool->put_stub(ep, ret < 0));

        ret = stub->call<Heartbeat>(req, resp);

        if (ret < 0) return -errno;

        return resp.now;
    }

    std::string RPCEcho(photon::net::EndPoint ep, const std::string& str) {
        Echo::Request req;
        req.str.assign(str);

        Echo::Response resp;
        // string or variable length fields should pre-set buffer
        char tmpbuf[4096];
        resp.str = {tmpbuf, 4096};
        int ret;

        auto stub = pool->get_stub(ep, false);
        if (!stub) return {};
        DEFER(pool->put_stub(ep, ret < 0));

        ret = stub->call<Echo>(req, resp);

        if (ret < 0) return {};
        std::string s(resp.str.c_str());
        return s;
    }

    ssize_t RPCRead(photon::net::EndPoint ep, const std::string& fn,
                    const struct iovec* iovec, int iovcnt) {
        ReadBuffer::Request req;
        req.fn.assign(fn);
        ReadBuffer::Response resp;
        resp.buf.assign(iovec, iovcnt);
        int ret;

        auto stub = pool->get_stub(ep, false);
        if (!stub) return -1;
        DEFER(pool->put_stub(ep, ret < 0));
        ret = stub->call<ReadBuffer>(req, resp);
        if (ret < 0) return ret;
        return resp.ret;
    }

    ssize_t RPCWrite(photon::net::EndPoint ep, const std::string& fn,
                     struct iovec* iovec, int iovcnt) {
        WriteBuffer::Request req;
        req.fn.assign(fn);
        req.buf.assign(iovec, iovcnt);
        WriteBuffer::Response resp;
        int ret;

        auto stub = pool->get_stub(ep, false);
        if (!stub) return -1;
        DEFER(pool->put_stub(ep, ret < 0));

        ret = stub->call<WriteBuffer>(req, resp);
        if (ret < 0) return ret;
        return resp.ret;
    }

    void TestRPC(photon::net::EndPoint ep) {
        Testrun::Request req;

        // prepare some data
        int iarr[] = {1, 2, 3, 4};

        // sample of
        char buf1[] = "some";
        char buf2[] = "buf";
        char buf3[] = "content";
        IOVector iov;
        iov.push_back(buf1, 4);
        iov.push_back(buf2, 3);
        iov.push_back(buf3, 7);

        // set up request
        req.someuint = 2233U;
        req.someint64 = 4455LL;
        req.somestruct = Testrun::SomePODStruct{.foo = true, .bar = 32767};
        req.intarray.assign(iarr, 4);
        req.somestr.assign("Hello");
        req.buf.assign(iov.iovec(), iov.iovcnt());

        // make room for response
        Testrun::Response resp;
        // iovector should pre_allocated
        IOVector riov;
        riov.push_back(1024);
        resp.buf.assign(riov.iovec(), riov.iovcnt());

        int ret;
        auto stub = pool->get_stub(ep, false);
        if (!stub) return;
        DEFER(pool->put_stub(ep, ret < 0));
        // Single step call
        ret = stub->call<Testrun>(req, resp);
        // ret < 0 means RPC failed on send or receive
        if (ret < 0) {
            LOG_INFO("RPC fail");
        } else {
            LOG_INFO("RPC succ: ", VALUE(resp.len),
                     VALUE((char*)riov.begin()->iov_base));
        }
    }
};

DEFINE_int32(port, 0, "server port");
DEFINE_string(host, "127.0.0.1", "server ip");

static bool running = true;
static photon::join_handle* heartbeat;
static photon::net::EndPoint ep;

void* heartbeat_thread(void* arg) {
    auto client = (RPCClient*)arg;
    while (running) {
        auto start = photon::now;
        auto half = client->RPCHeartbeat(ep);
        auto end = photon::now;
        if (half > 0) {
            LOG_INFO("single trip `us, round trip `us", half - start,
                     end - start);
        } else {
            LOG_INFO("heartbeat failed in `us", end - start);
        }
        photon::thread_sleep(1);
    }
    return nullptr;
}

void run_some_task(RPCClient* client) {
    auto echo = client->RPCEcho(ep, "Hello");
    LOG_INFO(VALUE(echo));

    IOVector iov;
    char writebuf[] = "write data like pwrite";
    iov.push_back(writebuf, sizeof(writebuf));
    auto tmpfile = "/tmp/test_file_" + std::to_string(rand());
    auto ret = client->RPCWrite(ep, tmpfile, iov.iovec(), iov.iovcnt());
    LOG_INFO("Write to tmpfile ` ret=`", tmpfile, ret);
    
    char readbuf[4096];
    struct iovec iovrd{
        .iov_base=readbuf,
        .iov_len=4096,
    };
    ret = client->RPCRead(ep, tmpfile, &iovrd, 1);
    LOG_INFO("Read from tmpfile ` ret=`", tmpfile, ret);
    LOG_INFO(VALUE((char*)readbuf));
    client->TestRPC(ep);
}

int main(int argc, char** argv) {
    srand(time(NULL));
    gflags::ParseCommandLineFlags(&argc, &argv, true);
    photon::init();
    DEFER(photon::fini());
    RPCClient client;
    ep = photon::net::EndPoint(photon::net::IPAddr(FLAGS_host.c_str()),
                               FLAGS_port);
    heartbeat = photon::thread_enable_join(
        photon::thread_create(heartbeat_thread, &client));
    DEFER({ photon::thread_join(heartbeat); });
    DEFER(running = false);
    run_some_task(&client);
    photon::thread_suspend();
}