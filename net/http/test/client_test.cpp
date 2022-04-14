#include <fcntl.h>
#include <time.h>
#include <gtest/gtest.h>
#include <openssl/hmac.h>
#include <utils/Utils.h>
#include <netinet/tcp.h>

#include <chrono>
#include <cstddef>
#include <cstring>
#include <cstdlib>
#include <string>

#include <photon/net/curl.h>
#include <photon/net/socket.h>
#include <photon/common/alog.h>
#define protected public
#define private public
#include "../client.cpp"
#undef protected
#undef private
// #include "client.h"
#include <photon/io/fd-events.h>
#include <photon/net/etsocket.h>
#include <photon/thread/thread11.h>
#include <photon/common/stream.h>

using namespace photon;
using namespace photon::net;

static std::string hmac_sha1(const std::string& key, const std::string& data) {
    HMAC_CTX ctx;
    unsigned char output[EVP_MAX_MD_SIZE];
    auto evp_md = EVP_sha1();
    unsigned int output_length;
    HMAC_CTX_init(&ctx);
    HMAC_Init_ex(&ctx, (const unsigned char*)key.c_str(), key.length(), evp_md,
                 nullptr);
    HMAC_Update(&ctx, (const unsigned char*)data.c_str(), data.length());
    HMAC_Final(&ctx, (unsigned char*)output, &output_length);
    HMAC_CTX_cleanup(&ctx);

    return std::string((const char*)output, output_length);
}

static std::string oss_signature(const std::string& bucket,
                                 const std::string& method,
                                 const std::string& path, uint64_t expires,
                                 const std::string& accessid,
                                 const std::string& accesskey) {
    return AlibabaCloud::OSS::Base64Encode(hmac_sha1(
        accesskey,
        method + "\n\n\n" + std::to_string(expires) + "\n/" + bucket + path));
}

static char socket_buf[] =
    "this is a http_client post request body text for socket stream";

int socket_put_cb(void* self, IStream* stream) {
    auto ret = stream->write(socket_buf, sizeof(socket_buf));
    EXPECT_EQ(sizeof(socket_buf), ret);
    return (sizeof(socket_buf) == ret) ? 0 : -1;
}

int socket_get_cb(void* self, IStream* stream) { return 0; }

int timeout_writer(void *self, IStream* stream) {
    photon::thread_usleep(5 * 1000UL * 1000UL);
    char c = '1';
    stream->write((void*)&c, 1);
    return 0;
}
TEST(http_client, post) {
    auto expire =
        std::chrono::duration_cast<std::chrono::seconds>(
            (std::chrono::system_clock::now() + std::chrono::seconds(3600))
                .time_since_epoch())
            .count();
    auto signature_post = oss_signature(
        "qisheng-ds", "PUT", "/ease_ut/ease-httpclient-test-postfile", expire,
        OSS_ID, OSS_KEY);
    auto queryparam_post =
        "OSSAccessKeyId=LTAIWsbCDjMKQbaW&Expires=" + std::to_string(expire) +
        "&Signature=" + net::url_escape(signature_post.c_str());
    LOG_DEBUG(VALUE(queryparam_post));
    std::string target_post =
        "http://qisheng-ds.oss-cn-hangzhou-zmf.aliyuncs.com/ease_ut/"
        "ease-httpclient-test-postfile?" +
        queryparam_post;
    auto client = new_http_client();
    DEFER(delete client);
    auto op1 = client->new_operation(Verb::PUT, target_post);
    DEFER(delete op1);
    op1->req.content_length(sizeof(socket_buf));
    Callback<IStream*> cb_put(nullptr, &socket_put_cb);
    op1->req_body_writer = cb_put;
    client->call(op1);
    EXPECT_EQ(200, op1->status_code);

    auto signature_get = oss_signature(
        "qisheng-ds", "GET", "/ease_ut/ease-httpclient-test-postfile", expire,
        OSS_ID, OSS_KEY);
    auto queryparam_get =
        "OSSAccessKeyId=LTAIWsbCDjMKQbaW&Expires=" + std::to_string(expire) +
        "&Signature=" + net::url_escape(signature_get.c_str());
    std::string target_get =
        "http://qisheng-ds.oss-cn-hangzhou-zmf.aliyuncs.com/ease_ut/"
        "ease-httpclient-test-postfile?" +
        queryparam_get;
    auto op2 = client->new_operation(Verb::GET, target_get);
    DEFER(delete op2);
    op2->req.content_length(0);
    client->call(op2);
    auto stream2 = op2->resp_body.get();
    char resp_body_buf[1024];
    EXPECT_EQ(sizeof(socket_buf), op2->resp.resource_size());
    auto ret = stream2->read(resp_body_buf, sizeof(socket_buf));
    EXPECT_EQ(sizeof(socket_buf), ret);
    resp_body_buf[sizeof(socket_buf)] = '\0';
    EXPECT_EQ(0, strcmp(resp_body_buf, socket_buf));

    auto op3 = client->new_operation(Verb::GET, target_get);
    DEFER(delete op3);
    op3->req.content_length(0);
    op3->req.insert_range(10, 19);
    client->call(op3);
    auto stream3 = op3->resp_body.get();
    char resp_body_buf_range[1024];
    ret = stream3->read(resp_body_buf_range, op3->resp.content_length());
    resp_body_buf_range[10] = '\0';
    EXPECT_EQ(0, strcmp("http_clien", resp_body_buf_range));
    LOG_DEBUG(resp_body_buf_range);

    auto op4 = client->new_operation(Verb::GET, target_get);
    DEFER(delete op4);
    op4->req.content_length(0);
    op4->call();
    auto stream4 = op4->resp_body.get();
    EXPECT_EQ(sizeof(socket_buf), op4->resp.resource_size());
    ret = stream4->read(resp_body_buf, 10);
    EXPECT_EQ(10, ret);
    resp_body_buf[10] = '\0';
    EXPECT_EQ(0, strcmp("this is a ", resp_body_buf));
    LOG_DEBUG(resp_body_buf);
    ret = stream4->read(resp_body_buf, 10);
    EXPECT_EQ(10, ret);
    resp_body_buf[10] = '\0';
    LOG_DEBUG(resp_body_buf);
    EXPECT_EQ(0, strcmp("http_clien", resp_body_buf));

}
#define RETURN_IF_FAILED(func)                             \
    if (0 != (func)) {                                     \
        status = Status::failure;                          \
        LOG_ERROR_RETURN(0, , "Failed to perform " #func); \
    }
constexpr char http_response_data[] = "HTTP/1.1 200 4TEST\r\n"
                                      "Transfer-Encoding: chunked\r\n"
                                    //   "Content-Length: 5\r\n"
                                      "\r\n"
                                      "12\r\n"
                                      "first chunk \r\n"
                                      "13\r\n"
                                      "second chunk \r\n"
                                      "0\r\n"
                                      "\r\n";
int chunked_handler(void*, net::ISocketStream* sock) {
    EXPECT_NE(nullptr, sock);
    LOG_DEBUG("Accepted");
    char recv[4096];
    sock->recv(recv, 4096);
    LOG_DEBUG("RECV `", recv);
    sock->write(http_response_data, sizeof(http_response_data) - 1);
    LOG_DEBUG("SEND `", http_response_data);
    return 0;
}
constexpr char header_data[] = "HTTP/1.1 200 ok\r\n"
                               "Transfer-Encoding: chunked\r\n"
                               "\r\n";
int chunked_handler_complict(void*, net::ISocketStream* sock) {
    EXPECT_NE(nullptr, sock);
    LOG_DEBUG("Accepted");
    char recv[4096];
    sock->recv(recv, 4096);
    LOG_DEBUG("RECV `", recv);
    auto ret = sock->write(header_data, sizeof(header_data) - 1);
    EXPECT_EQ(sizeof(header_data) - 1, ret);
    //-----------------------
    ret = sock->write("10000\r\n", 7);
    char space_buf[10000];
    ret = sock->write(space_buf, 10000);
    EXPECT_EQ(ret, 10000);
    sock->write("\r\n", 2);
    sock->write("4090\r\n", 6);
    ret = sock->write(space_buf, 4090);
    EXPECT_EQ(ret, 4090);
    sock->write("\r\n", 2);
    sock->write("4086\r\n", 6);
    ret = sock->write(space_buf, 4086);
    EXPECT_EQ(ret, 4086);
    sock->write("\r\n", 2);
    sock->write("1024\r\n", 6);
    ret = sock->write(space_buf, 1024);
    EXPECT_EQ(ret, 1024);
    sock->write("\r\n", 2);
    sock->write("0\r\n\r\n", 5);
    return 0;
}

net::EndPoint ep{net::IPAddr("127.0.0.1"), 19731};
std::string std_data;
const size_t std_data_size = 64 * 1024;
static int digtal_num(int n) {
    int ret = 0;
    do {
        ++ret;
        n /= 10;
    } while (n);
    return ret;
}
void chunked_send(int offset, int size, net::ISocketStream* sock) {
    auto s = std::to_string(size) + "\r\n";
    sock->write(s.data(), 2 + digtal_num(size));
    auto ret = sock->write(std_data.data() + offset, size);
    EXPECT_EQ(ret, size);
    sock->write("\r\n", 2);
}
std::vector<int> rec;
int chunked_handler_pt(void*, net::ISocketStream* sock) {
    EXPECT_NE(nullptr, sock);
    LOG_DEBUG("Accepted");
    char recv[4096];
    auto len = sock->recv(recv, 4096);
    EXPECT_GT(len, 0);
    auto ret = sock->write(header_data, sizeof(header_data) - 1);
    EXPECT_EQ(sizeof(header_data) - 1, ret);
    auto offset = 0;
    rec.clear();
    while (offset < std_data_size) {
        auto remain = std_data_size - offset;
        if (remain <= 1024) {
            rec.push_back(remain);
            chunked_send(offset, remain, sock);
            break;
        }
        auto max_seg = std::min(remain - 1024, 2 * 4 * 1024UL);
        auto seg = 1024 + rand() % max_seg;
        chunked_send(offset, seg, sock);
        rec.push_back(seg);
        offset += seg;
    }
    sock->write("0\r\n\r\n", 5);
    return 0;
}

TEST(http_client, chunked) {
    auto server = net::new_tcp_socket_server();
    DEFER({ delete server; });
    server->setsockopt(SOL_SOCKET, SO_REUSEPORT, 1);
    server->set_handler({nullptr, &chunked_handler});
    auto ret = server->bind(ep.port, ep.addr);
    if (ret < 0) LOG_ERROR(VALUE(errno));
    ret |= server->listen(100);
    if (ret < 0) LOG_ERROR(VALUE(errno));
    EXPECT_EQ(0, ret);
    LOG_INFO("Ready to accept");
    server->start_loop();
    photon::thread_sleep(1);
    auto client = new_http_client();
    DEFER(delete client);
    auto op = client->new_operation(Verb::GET, "http://localhost:19731/");
    DEFER(delete op);
    std::string buf;

    op->call();
    EXPECT_EQ(200, op->status_code);
    buf.resize(30);
    ret = op->resp_body->read((void*)buf.data(), 30);
    EXPECT_EQ(25, ret);
    buf.resize(25);
    EXPECT_EQ(true, buf == "first chunk second chunk ");
    LOG_DEBUG(VALUE(buf));

    server->set_handler({nullptr, &chunked_handler_complict});
    auto opc = client->new_operation(Verb::GET, "http://localhost:19731/");
    DEFER(delete opc);
    opc->call();
    EXPECT_EQ(200, opc->status_code);
    buf.resize(20000);
    ret = opc->resp_body->read((void*)buf.data(), 20000);
    EXPECT_EQ(10000 + 4090 + 4086 + 1024, ret);

    std_data.resize(std_data_size);
    int num = 0;
    for (auto &c : std_data) {
        c = '0' + ((++num) % 10);
    }
    srand(time(0));
    server->set_handler({nullptr, &chunked_handler_pt});
    for (auto tmp = 0; tmp < 20; tmp++) {
        auto op_test = client->new_operation(Verb::GET, "http://localhost:19731/");
        DEFER(delete op_test);
        op_test->call();
        EXPECT_EQ(200, op_test->status_code);
        buf.resize(std_data_size);
        memset((void*)buf.data(), '0', std_data_size);
        ret = op_test->resp_body->read((void*)buf.data(), std_data_size);
        EXPECT_EQ(std_data_size, ret);
        EXPECT_EQ(true, buf == std_data);
        if (std_data_size != ret || buf != std_data) {
            std::cout << std::endl;
            std::cout << "n=" << rec.size() << std::endl;
            for (auto &a : rec) {
                std::cout << a << ",";
                std::cout << std::endl;
            }
            std::cout << std::endl;
            std::cout << "buffer = \n" << buf << std::endl;
            break;
        }
        op_test->resp_body->close();
        LOG_INFO("random chunked test ` passed", tmp);
    }
}
int wa_test[] = {5265,
6392,
4623,
7688,
7533,
4084,
8560,
7043,
7374,
4487,
2195,
292};
int chunked_handler_debug(void*, net::ISocketStream* sock) {
    EXPECT_NE(nullptr, sock);
    LOG_DEBUG("Accepted");
    char recv[4096];
    auto len = sock->recv(recv, 4096);
    EXPECT_GT(len, 0);
    auto ret = sock->write(header_data, sizeof(header_data) - 1);
    EXPECT_EQ(sizeof(header_data) - 1, ret);
    auto offset = 0;
    for (auto &a : wa_test) {
        chunked_send(offset, a, sock);
        offset += a;
    }
    sock->write("0\r\n\r\n", 5);
    return 0;
}

TEST(http_client, debug) {
    auto server = net::new_tcp_socket_server();
    DEFER({ delete server; });
    server->setsockopt(SOL_SOCKET, SO_REUSEPORT, 1);
    server->set_handler({nullptr, &chunked_handler_debug});
    auto ret = server->bind(ep.port, ep.addr);
    if (ret < 0) LOG_ERROR(VALUE(errno));
    ret |= server->listen(100);
    if (ret < 0) LOG_ERROR(VALUE(errno));
    EXPECT_EQ(0, ret);
    LOG_INFO("Ready to accept");
    server->start_loop();
    photon::thread_sleep(1);

    std_data.resize(std_data_size);
    int num = 0;
    for (auto &c : std_data) {
        c = '0' + ((++num) % 10);
    }

    auto client = new_http_client();
    DEFER(delete client);
    auto op_test = client->new_operation(Verb::GET, "http://localhost:19731/");
    DEFER(delete op_test);
    op_test->call();
    EXPECT_EQ(200, op_test->status_code);
    std::string buf;
    buf.resize(std_data_size);
    memset((void*)buf.data(), '0', std_data_size);
    ret = op_test->resp_body->read((void*)buf.data(), std_data_size);
    EXPECT_EQ(std_data_size, ret);
    EXPECT_EQ(true, buf == std_data);
    for (int i = 0; i < buf.size(); i++) {
        if (buf[i] != std_data[i]) {
            std::cout << i << std::endl;
        }
    }
    std::cout << "new" << std::endl;
}
int sleep_handler(void*, net::ISocketStream* sock) {
    photon::thread_sleep(3);
    return 0;
}
int dummy_body_writer(void* self, IStream* stream) { return 0; }

TEST(http_client, server_no_resp) {
    auto server = net::new_tcp_socket_server();
    DEFER(delete server);
    server->set_handler({nullptr, &sleep_handler});
    server->bind(38812, net::IPAddr());
    server->listen();
    server->start_loop();

    auto client = new_http_client();
    DEFER(delete client);
    auto op = client->new_operation(Verb::GET, "http://127.0.0.1:38812/wtf");
    op->req.content_length(0);
    client->call(op);
    EXPECT_EQ(-1, op->status_code);
}

TEST(http_client, partial_body) {
    auto expire =
        std::chrono::duration_cast<std::chrono::seconds>(
            (std::chrono::system_clock::now() + std::chrono::seconds(3600))
                .time_since_epoch())
            .count();
    auto signature_get = oss_signature(
        "qisheng-ds", "GET", "/ease_ut/ease-httpclient-test-postfile", expire,
        OSS_ID, OSS_KEY);
    auto queryparam_get =
        "OSSAccessKeyId=LTAIWsbCDjMKQbaW&Expires=" + std::to_string(expire) +
        "&Signature=" + net::url_escape(signature_get.c_str());
    std::string target_get =
        "http://qisheng-ds.oss-cn-hangzhou-zmf.aliyuncs.com/ease_ut/"
        "ease-httpclient-test-postfile?" +
        queryparam_get;
    auto client = new_http_client();
    DEFER(delete client);
    auto op = client->new_operation(Verb::GET, target_get);
    op->req.content_length(0);
    client->call(op);
    auto stream = op->resp_body.get();
    EXPECT_EQ(sizeof(socket_buf), op->resp.resource_size());
    std::string buf;
    buf.resize(10);
    stream->read((void*)buf.data(), 10);
    LOG_DEBUG(VALUE(buf));
    EXPECT_EQ(true, buf == "this is a ");
    stream->read((void*)buf.data(), 10);
    LOG_DEBUG(VALUE(buf));
    EXPECT_EQ(true, buf == "http_clien");
}

// 只作为手动测试样例
// TEST(http_client, proxy) {
//     auto client = new_http_client();
//     DEFER(delete client);
//     client->set_proxy("http://localhost:8899/");
//     auto op = client->new_operation(Verb::delete_, "https://domain:1234/targetName");
//     DEFER(delete op);
//     LOG_DEBUG(VALUE(op->req.whole()));
//     op->req.redirect(Verb::GET, "baidu.com", true);
//     LOG_DEBUG(VALUE(op->req.whole()));
//     op->call();
//     EXPECT_EQ(200, op->status_code);
// }

int main(int argc, char** arg) {
    photon::init();
    DEFER(photon::fini());
    photon::fd_events_init();
    DEFER(photon::fd_events_fini());
    if (net::et_poller_init() < 0) {
        LOG_ERROR("net::et_poller_init failed");
        exit(EAGAIN);
    }
    DEFER(net::et_poller_fini());
    set_log_output_level(ALOG_DEBUG);
    ::testing::InitGoogleTest(&argc, arg);
    LOG_DEBUG("test result:`", RUN_ALL_TESTS());
}
