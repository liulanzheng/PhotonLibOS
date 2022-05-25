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

#include "../../rpc/rpc.cpp"
#include <memory>
#include <gtest/gtest.h>
#include <photon/photon.h>
#include <photon/thread/thread.h>
#include <photon/common/memory-stream/memory-stream.h>
#include <photon/common/utility.h>
#include <photon/common/alog-stdstring.h>
#include <photon/net/socket.h>
using namespace std;
using namespace photon;
using namespace rpc;

std::string S = "1234567890";
struct Args
{
    int a = 0, b = 0, c = 0, d = 0;
    std::string s;
    void init()
    {
        a = b = c = d = 123;
        s = "1234567890";
    }
    void verify()
    {
        LOG_DEBUG(VALUE(a));
        EXPECT_EQ(a, 123);
        LOG_DEBUG(VALUE(b));
        EXPECT_EQ(b, 123);
        LOG_DEBUG(VALUE(c));
        EXPECT_EQ(c, 123);
        LOG_DEBUG(VALUE(d));
        EXPECT_EQ(d, 123);
        LOG_DEBUG(VALUE(s));
        EXPECT_EQ(s, S);
    }
    uint64_t serialize(iovector& iov)
    {
        iov.clear();
        iov.push_back({&a, offsetof(Args, s)});
        iov.push_back({(void*)s.c_str(), s.length()});
        return 2;
    }
    void deserialize(iovector& iov)
    {
        iov.extract_front(offsetof(Args, s), &a);
        auto slen = iov.sum();
        s.resize(slen);
        iov.extract_front(slen, &s[0]);
    }
};

FunctionID FID(234);

rpc::Header rpc_server_read(IStream* s)
{
    rpc::Header header;
    s->read(&header, sizeof(header));
//    EXPECT_EQ(header.tag, 1);

    IOVector iov;
    iov.push_back(header.size);
    s->readv(iov.iovec(), iov.iovcnt());

    Args args;
    args.deserialize(iov);
    args.verify();

    return header;
}

char STR[] = "!@#$%^&*()_+";
void rpc_server_write(IStream* s, uint64_t tag)
{
    rpc::Header header;
    header.tag = tag;
    header.size = LEN(STR);

    IOVector iov;
    iov.push_back(&header, sizeof(header));
    iov.push_back(STR, LEN(STR));

    s->writev(iov.iovec(), iov.iovcnt());
}

void* rpc_server(void* args_)
{
    LOG_DEBUG("enter");
    auto s = (IStream*)args_;
    while (true)
    {
        auto header = rpc_server_read(s);
        rpc_server_write(s, header.tag);
        if (header.function == (uint64_t)-1) break;
    }
    LOG_DEBUG("exit");
    return nullptr;
}

int server_function(void* instance, iovector* request, rpc::Skeleton::ResponseSender sender, IStream*)
{
    EXPECT_EQ(instance, (void*)123);

    Args args;
    args.deserialize(*request);
    args.verify();

    IOVector iov;
    iov.push_back(STR, LEN(STR));
    sender(&iov);
    LOG_DEBUG("exit");
    return 0;
}

int server_exit_function(void* instance, iovector* request, rpc::Skeleton::ResponseSender sender, IStream*)
{
    IOVector iov;
    iov.push_back(STR, LEN(STR));
    sender(&iov);

    auto sk = (Skeleton*)instance;
    sk->shutdown_no_wait();

    LOG_DEBUG("exit");
    return 0;
}

bool skeleton_exited;
photon::condition_variable skeleton_exit;
rpc::Skeleton* g_sk;
void* rpc_skeleton(void* args)
{
    skeleton_exited = false;
    auto s = (IStream*)args;
    auto sk = new_skeleton();
    g_sk = sk;
    sk->add_function(FID, rpc::Skeleton::Function((void*)123, &server_function));
    sk->add_function(-1,  rpc::Skeleton::Function(sk, &server_exit_function));
    sk->serve(s);
    LOG_DEBUG("exit");
    skeleton_exit.notify_all();
    skeleton_exited = true;
    return nullptr;
}

void do_call(StubImpl& stub, uint64_t function)
{
    SerializerIOV req_iov, resp_iov;
    Args args;
    args.init();
    args.serialize(req_iov.iov);

    LOG_DEBUG("before call");
    stub.do_call(function, req_iov, resp_iov, -1);
    LOG_DEBUG("after call recvd: '`'", (char*)resp_iov.iov.back().iov_base);
    EXPECT_EQ(memcmp(STR, resp_iov.iov.back().iov_base, LEN(STR)), 0);
}

TEST(rpc, call)
{
    unique_ptr<DuplexMemoryStream> ds( new_duplex_memory_stream(16) );
    thread_create(&rpc_skeleton, ds->endpoint_a);
    StubImpl stub(ds->endpoint_b);
    do_call(stub, 234);
    do_call(stub, -1);
    if (!skeleton_exited)
        skeleton_exit.wait_no_lock();
}

uint64_t ncallers;
void* do_concurrent_call(void* arg)
{
    ncallers++;
    LOG_DEBUG("enter");
    auto stub = (StubImpl*)arg;
    for (int i = 0; i < 10; ++i)
        do_call(*stub, 234);
    LOG_DEBUG("exit");
    ncallers--;
    return nullptr;
}

void* do_concurrent_call_shut(void* arg)
{
    ncallers++;
    LOG_DEBUG("enter");
    auto stub = (StubImpl*)arg;
    for (int i = 0; i < 10; ++i)
        do_call(*stub, 234);
    LOG_DEBUG("exit");
    ncallers--;
    return nullptr;
}

TEST(rpc, concurrent)
{
//    log_output_level = 1;
    LOG_INFO("Creating 1,000 threads, each doing 1,000 RPC calls");
    // ds will be destruct just after function returned
    // but server will not
    // therefore, it will cause assert when destruction
    unique_ptr<DuplexMemoryStream> ds( new_duplex_memory_stream(16) );
    thread_create(&rpc_skeleton, ds->endpoint_a);

    LOG_DEBUG("asdf1");
    StubImpl stub(ds->endpoint_b);
    for (int i = 0; i < 10; ++i)
        thread_create(&do_concurrent_call, &stub);

    LOG_DEBUG("asdf2");
    do { thread_usleep(1);
    } while(ncallers > 0);
    LOG_DEBUG("asdf3");
    do_call(stub, -1);
    LOG_DEBUG("asdf4");
    LOG_DEBUG("FINISHED");
    ds->close();
    if (!skeleton_exited)
        skeleton_exit.wait_no_lock();
    log_output_level = 0;
}

void do_call_timeout(StubImpl& stub, uint64_t function)
{
    SerializerIOV req_iov, resp_iov;
    Args args;
    args.init();
    args.serialize(req_iov.iov);

    LOG_DEBUG("before call");
    if (stub.do_call(function, req_iov, resp_iov, 1UL*1000*1000) >= 0) {
        LOG_DEBUG("after call recvd: '`'", (char*)resp_iov.iov.back().iov_base);
    }
}

void* do_concurrent_call_timeout(void* arg)
{
    ncallers++;
    LOG_DEBUG("enter");
    auto stub = (StubImpl*)arg;
    for (int i = 0; i < 10; ++i)
        do_call_timeout(*stub, 234);
    LOG_DEBUG("exit");
    ncallers--;
    return nullptr;
}

int server_function_timeout(void* instance, iovector* request, rpc::Skeleton::ResponseSender sender, IStream*)
{
    EXPECT_EQ(instance, (void*)123);
    Args args;
    args.deserialize(*request);
    args.verify();

    photon::thread_usleep(3UL*1000*1000);

    IOVector iov;
    iov.push_back(STR, LEN(STR));
    LOG_INFO("Before Send");
    sender(&iov);
    LOG_INFO("After Send");
    LOG_DEBUG("exit");
    return 0;
}

void* rpc_skeleton_timeout(void* args)
{
    skeleton_exited = false;
    auto s = (IStream*)args;
    auto sk = new_skeleton();
    g_sk = sk;
    sk->add_function(FID, rpc::Skeleton::Function((void*)123, &server_function_timeout));
    sk->add_function(-1,  rpc::Skeleton::Function(sk, &server_exit_function));
    sk->serve(s);
    LOG_DEBUG("exit");
    skeleton_exit.notify_all();
    skeleton_exited = true;
    return nullptr;
}

TEST(rpc, timeout) {
    LOG_INFO("Creating 1,000 threads, each doing 1,000 RPC calls");
    // ds will be destruct just after function returned
    // but server will not
    // therefore, it will cause assert when destruction
    unique_ptr<DuplexMemoryStream> ds( new_duplex_memory_stream(655360) );

    thread_create(&rpc_skeleton_timeout, ds->endpoint_a);

    LOG_DEBUG("asdf1");
    StubImpl stub(ds->endpoint_b);
    for (int i = 0; i < 10; ++i)
        thread_create(&do_concurrent_call_timeout, &stub);

    LOG_DEBUG("asdf2");
    do { thread_usleep(1);
    } while(ncallers > 0);
    LOG_DEBUG("asdf3");
    do_call_timeout(stub, -1);
    LOG_DEBUG("asdf4");
    LOG_DEBUG("FINISHED");
    ds->close();
    if (!skeleton_exited)
        skeleton_exit.wait_no_lock();
    log_output_level = 0;
}

/*** Usage example ***/

#include <photon/net/socket.h>
#include <photon/rpc/serialize.h>

/**
 * @brief `TestProtocol` is a example of self-defined rpc protocol.
 * A protocol should have `IID` and `FID` as identifier, and struct/class
 * `Request` `Response` inherited `RPC::Message`, defined
 * details of RPC request and response.
 * NOTE: RPC will not adjust by endings. DO NOT try to make RPC across different
 * machine byte order archs.
 */
struct TestProtocol {
    /**
     * @brief `IID` and `FID` will conbined as a coordinate identitfy,
     * these numbers will be set into RPC request and response header.
     * The combination of IID & FID should be identical.
     */
    const static uint32_t IID = 0x222;
    const static uint32_t FID = 0x333;

    struct SomePODStruct {
        bool foo;
        size_t bar;
    };

    /**
     * @brief `Request` struct defines detail of RPC request,
     *
     */
    struct Request : public photon::rpc::Message {
        // All POD type fields can keep by what it should be in fields.
        uint32_t someuint;
        int64_t someint64;
        char somechar;
        SomePODStruct somestruct;
        // Array type should use RPC::array, do not supports multi-dimensional
        // arrays.
        photon::rpc::array<int> intarray;
        // String should use RPC::string instead.
        // NOTE: Since RPC::string is implemented by array, do not support
        // array-of-string `photon::rpc::array<RPC::string>` will leads to
        // unexpected result.
        photon::rpc::string somestr;
        // `photon::rpc::iovec_array` as a special fields, RPC will deal with
        // inside-buffer contents. it deal with buffer as iovectors
        photon::rpc::iovec_array buf;

        // After all, using macro `PROCESS_FIELDS` to set up compile time
        // reflection for those field want to transport by RPC
        PROCESS_FIELDS(someuint, someint64, somechar, somestruct, intarray,
                       somestr, buf);
    };

    struct Response : public photon::rpc::Message {
        // Since response is also a RPC::Message, keeps rule of `Request`

        size_t len;
        // `photon::rpc::aligned_iovec_array` will keep buffer data in the head
        // of RPC message when serializing/deserializing, may help to keep
        // memory alignment.
        photon::rpc::aligned_iovec_array buf;

        PROCESS_FIELDS(len, buf);
    };
};

/**
 * @brief Example client, send rpc request and receive response
 * 
 * @param ep server endpoint
 */
static void example_client(photon::net::EndPoint ep) {
    // create a tcp rpc connection pool
    // unused connections will be drop after 10 seconds(10UL*1000*1000)
    // TCP connection will failed in 1 second(1UL*1000*1000) if not accepted
    // and connection send/recv will take 5 socneds(5UL*1000*1000) as timedout
    auto pool = photon::rpc::new_stub_pool(
        10UL * 1000 * 1000, 1UL * 1000 * 1000, 5UL * 1000 * 1000);
    // Get a stub (with connection) to endpoint(without tls)
    auto stub = pool->get_stub(ep, false);
    // put back stub after finish (without immediatly destruction)
    DEFER(pool->put_stub(ep, false));
    // make room for request
    TestProtocol::Request req;

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
    req.somestruct = TestProtocol::SomePODStruct{.foo = true, .bar = 32767};
    req.intarray.assign(iarr, 4);
    req.somestr.assign("Hello");
    req.buf.assign(iov.iovec(), iov.iovcnt());

    // make room for response
    TestProtocol::Response resp;
    // iovector should pre_allocated
    IOVector riov;
    riov.push_back(1024);
    resp.buf.assign(riov.iovec(), riov.iovcnt());

    // Single step call
    auto ret = stub->call<TestProtocol>(req, resp);
    // ret < 0 means RPC failed on send or receive
    if (ret < 0) {
        LOG_INFO("RPC fail");
    } else {
        LOG_INFO("RPC succ: ", VALUE(resp.len),
                 VALUE((char*)riov.begin()->iov_base));
    }
}

static photon::net::EndPoint ep;

// Server side rpc handler
struct ExampleService {
    // public methods named `do_rpc_service` takes rpc requests
    // and produce response
    // able to set or operate connection directly(like close or set flags)
    // iov is temporary buffer created by skeleton with defined allocator
    // able to use as temporary buffer
    // return value will be droped
    int do_rpc_service(TestProtocol::Request* req, TestProtocol::Response* resp,
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
};

TEST(RPC, one_call_example) {
    // start server

    // construct rpcservice
    ExampleService rpcservice;

    // construct skeleton and register TestProtocol handler;
    auto skeleton = photon::rpc::new_skeleton();
    DEFER(delete skeleton);
    // register TestProtocol service, able to register multiple service
    skeleton->register_service<TestProtocol>(&rpcservice);

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
    tcpserver->start_loop();
    auto ep = tcpserver->getsockname();
    LOG_INFO(VALUE(ep));

    // run example client
    example_client(ep);
}
/*** example finished ***/

int main(int argc, char** arg)
{
    ::photon::init();
    DEFER(photon::fini());
    ::testing::InitGoogleTest(&argc, arg);
    return RUN_ALL_TESTS();
}
