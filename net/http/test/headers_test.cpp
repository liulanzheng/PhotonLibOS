#include <fcntl.h>
#include <gtest/gtest.h>
#include <openssl/hmac.h>
#include <utils/Utils.h>

#include <chrono>
#include <cstddef>
#include <cstring>
#include <string>
#include <gflags/gflags.h>

#include "../../curl.h"
#include "../../socket.h"
#include "../../etsocket.h"
#include "common/alog-stdstring.h"
#include "../client.cpp"
#include "io/fd-events.h"
#include "thread/thread11.h"
#include "common/stream.h"
#include "../headers.cpp"

using namespace Net::HTTP;
using namespace std;
template<uint16_t BUF_CAPACITY = 64*1024 - 1>
class RequestHeadersStored : public RequestHeaders
{
public:
    RequestHeadersStored(Verb v, std::string_view url, bool enable_proxy = false) :
        RequestHeaders(_buffer, BUF_CAPACITY, v, url, enable_proxy) { }

protected:
    char _buffer[BUF_CAPACITY];
};
TEST(headers, req_header) {
    char std_req_stream[] = "GET /targetName HTTP/1.1\r\n"
                             "Host: HostName\r\n"
                             "Content-Length: 0\r\n\r\n";
    RequestHeadersStored<> req_header(Verb::GET, "http://HostName:80/targetName");
    req_header.content_length(0);
    EXPECT_EQ(false, req_header.empty());
    EXPECT_EQ(true, req_header.whole() == std_req_stream);
    LOG_DEBUG(VALUE(req_header.whole()));
    EXPECT_EQ(Verb::GET, req_header.verb());
    EXPECT_EQ(true, "/targetName" == req_header.target());
    LOG_DEBUG(VALUE(req_header.target()));
    EXPECT_EQ(true, req_header["Content-Length"] == "0");
    LOG_DEBUG(req_header["Content-Length"]);
    EXPECT_EQ(true, req_header["Host"] == "HostName");
    EXPECT_EQ(true, req_header.find("noexist") == req_header.end());
    LOG_DEBUG(req_header["Host"]);
    string capacity_overflow;
    capacity_overflow.resize(100000);
    auto ret = req_header.insert("overflow_test", capacity_overflow);
    EXPECT_EQ(-ENOBUFS, ret);
    RequestHeadersStored<> req_header_proxy(Verb::GET, "http://HostName:80/targetName", true);
    LOG_DEBUG(VALUE(req_header_proxy.whole()));
    LOG_DEBUG(VALUE(req_header_proxy.target()));
    EXPECT_EQ(true, req_header_proxy.target() == "http://HostName/targetName");
}

class test_stream : public Net::ISocketStream {
public:
    string rand_stream;
    size_t remain;
    char* ptr;
    int kv_count;
    test_stream(int kv_count) : kv_count(kv_count) {
        rand_stream = "HTTP/1.1 200 ok\r\n";
        for (auto i = 0; i < kv_count; i++) rand_stream += "key" + to_string(i) + ": value" + to_string(i) + "\r\n";
        rand_stream += "\r\n0123456789";
        ptr = (char*)rand_stream.data();
        remain = rand_stream.size();
    }

    virtual ssize_t recv(void *buf, size_t count) override {
        // assert(count > remain);
        // LOG_DEBUG(remain);
        if (remain > 200) {
            auto len = rand() % 100 + 1;
            // cout << string(ptr, len);
            memcpy(buf, ptr, len);
            ptr += len;
            remain -= len;
            return len;
        }
        // cout << string(ptr, remain);
        memcpy(buf, ptr, remain);
        ptr += remain;
        auto ret = remain;
        remain = 0;
        return ret;
    }
    virtual ssize_t recv(const struct iovec *iov, int iovcnt) override {
        ssize_t ret = 0;
        auto iovec = IOVector(iov, iovcnt);
        while (!iovec.empty()) {
            auto tmp = recv(iovec.front().iov_base, iovec.front().iov_len);
            if (tmp < 0) return tmp;
            if (tmp == 0) break;
            iovec.extract_front(tmp);
            ret += tmp;
        }
        return ret;
    }

    UNIMPLEMENTED(ssize_t send(const void *buf, size_t count) override);
    UNIMPLEMENTED(ssize_t send(const struct iovec *iov, int iovcnt) override);
    UNIMPLEMENTED(ssize_t send2(const void *buf, size_t count, int flag) override);
    UNIMPLEMENTED(ssize_t send2(const struct iovec *iov, int iovcnt, int flag) override);

    UNIMPLEMENTED(ssize_t sendfile(int in_fd, off_t offset, size_t count) override);
    UNIMPLEMENTED(int close() override);

    UNIMPLEMENTED(ssize_t read(void *buf, size_t count) override);
    UNIMPLEMENTED(ssize_t readv(const struct iovec *iov, int iovcnt) override);

    UNIMPLEMENTED(ssize_t write(const void *buf, size_t count) override);
    UNIMPLEMENTED(ssize_t writev(const struct iovec *iov, int iovcnt) override);

    UNIMPLEMENTED(int setsockopt(int level, int option_name,
        const void *option_value, socklen_t option_len) override);
    UNIMPLEMENTED(int getsockopt(int level, int option_name,
                void* option_value, socklen_t* option_len) override);


    UNIMPLEMENTED(int getsockname(Net::EndPoint& addr) override);
    UNIMPLEMENTED(int getpeername(Net::EndPoint& addr) override);
    UNIMPLEMENTED(int getsockname(char* path, size_t count) override);
    UNIMPLEMENTED(int getpeername(char* path, size_t count) override);
    UNIMPLEMENTED(uint64_t timeout() override);

    void timeout(uint64_t tm) override {
        LOG_ERROR_RETURN(0, , "method timeout(uint64_t) UNIMPLEMENTED");
    }

    bool done() {
        return remain == 0;
    }
    int get_kv_count() {
        return kv_count;
    }

    void reset() {
        ptr = (char*)rand_stream.data();
        remain = rand_stream.size();
    }
};

TEST(headers, resp_header) {
    char of_buf[128 * 1024 - 1];
    ResponseHeaders of_header(of_buf, sizeof(of_buf));
    string of_stream = "HTTP/1.1 123 status_message\r\n";
    for (auto i = 0; i < 10; i++) of_stream += "key" + to_string(i) + ": value" + to_string(i) + "\r\n";
    of_stream += "\r\n0123456789";
    memcpy(of_buf, of_stream.data(), of_stream.size());
    auto ret = of_header.append_bytes(of_stream.size());
    EXPECT_EQ(0, ret);
    ret = of_header.append_bytes(of_stream.size());
    EXPECT_EQ(-1, ret);
    EXPECT_EQ(true, of_header.version() == "1.1");
    EXPECT_EQ(123, of_header.status_code());
    EXPECT_EQ(true, of_header.status_message() == "status_message");
    EXPECT_EQ(true, of_header.partial_body() == "0123456789");
    of_header.reset(of_buf, sizeof(of_buf));
    ret = of_header.append_bytes(of_stream.size());
    EXPECT_EQ(0, ret);

    char rand_buf[64 * 1024 - 1];
    ResponseHeaders rand_header(rand_buf, sizeof(rand_buf));
    srand(time(0));
    test_stream stream(2000);
    do {
        auto ret = rand_header.append_bytes(&stream);
        if (stream.done()) EXPECT_EQ(0, ret); else
            EXPECT_EQ(1, ret);
    } while (!stream.done());
    EXPECT_EQ(true, rand_header.version() == "1.1");
    EXPECT_EQ(200, rand_header.status_code());
    EXPECT_EQ(true, rand_header.status_message() == "ok");
    EXPECT_EQ(true, rand_header.partial_body() == "0123456789");
    auto kv_count = stream.get_kv_count();
    for (int i = 0; i < kv_count; i++) {
        string key = "key" + to_string(i);
        string value = "value" + to_string(i);
        EXPECT_EQ(true, rand_header[key] == value);
    }

    char exceed_buf[64 * 1024 - 1];
    ResponseHeaders exceed_header(exceed_buf, sizeof(exceed_buf));
    srand(time(0));
    test_stream exceed_stream(3000);
    do {
        auto ret = exceed_header.append_bytes(&exceed_stream);
        if (exceed_stream.done()) EXPECT_EQ(-1, ret); else
            EXPECT_EQ(1, ret);
    } while (!exceed_stream.done());
}

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

TEST(headers, network) {
    char CRLF[] = "\r\n";
    auto expire =
        std::chrono::duration_cast<std::chrono::seconds>(
            (std::chrono::system_clock::now() + std::chrono::seconds(3600))
                .time_since_epoch())
            .count();
    auto signature = oss_signature(
        "qisheng-ds", "GET", "/ease_ut/ease-httpclient-test-postfile", expire,
        OSS_ID, OSS_KEY);
    auto queryparam =
        "OSSAccessKeyId=LTAIWsbCDjMKQbaW&Expires=" + std::to_string(expire) +
        "&Signature=" + Net::url_escape(signature.c_str());
    // LOG_DEBUG(VALUE(queryparam));
    std::string target =
        "http://qisheng-ds.oss-cn-hangzhou-zmf.aliyuncs.com/ease_ut/"
        "ease-httpclient-test-postfile?" +
        queryparam;
    RequestHeadersStored<> req_header(Verb::GET, target);
    PooledDialer dialer;
    bool is_new_conn;
    auto pooledsock = dialer.dial(req_header.host(), req_header.port(), req_header.secure(), is_new_conn);
    auto sock = pooledsock->sock;
    req_header.content_length(0);
    req_header.insert("Host", req_header.host());
    req_header.insert("User-Agent", "curl/7.78.0");
    req_header.insert("Accept", "*/*");
    auto req = req_header.whole();
    auto ret = sock->write(req.data(), req.size());
    LOG_DEBUG(req.size());
    auto newline="\n";
    LOG_DEBUG(newline, req);
    EXPECT_EQ(req.size(), ret);
    ret = sock->write(CRLF, 2);
    EXPECT_EQ(2, ret);
    char resp_buf[1024 * 16];
    ResponseHeaders resp_header(resp_buf, sizeof(resp_buf));
    while (1) {
        LOG_DEBUG("try append from sock");
        auto ret = resp_header.append_bytes(sock);
        if (ret < 0) {
            EXPECT_GT(ret, 0);
            return;
        }
        if (ret == 0) break;
    }
    auto sv = resp_header["Content-Length"];
    estring_view content_length(sv);
    auto len = content_length.to_uint64();
    string text(resp_header.partial_body());
    auto partial_size = text.size();
    auto remain = len - partial_size;
    text.resize(len);
    ret = sock->recv((void*)(text.data()) + partial_size, remain);
    EXPECT_EQ(remain, ret);
    LOG_DEBUG(text);
}

TEST(headers, url) {
    RequestHeadersStored<> headers(Verb::UNKNOWN, "https://domain.com/dir1/dir2/file?key1=value1&key2=value2");
    LOG_DEBUG(VALUE(headers.target()));
    LOG_DEBUG(VALUE(headers.host()));
    LOG_DEBUG(VALUE(headers.secure()));
    LOG_DEBUG(VALUE(headers.query()));
    LOG_DEBUG(VALUE(headers.port()));
    RequestHeadersStored<> new_headers(Verb::UNKNOWN, "");
    if (headers.secure())
        new_headers.insert("Referer", http_url_scheme);
    else
        new_headers.insert("Referer", https_url_scheme);
    new_headers.value_append(headers.host());
    new_headers.value_append(headers.target());
    auto Referer_value = new_headers["Referer"];
    LOG_DEBUG(VALUE(Referer_value));
}

TEST(ReqHeaders, redirect) {
    RequestHeadersStored<> req(Verb::PUT, "http://domain1.com:1234/target1?param1=x1");
    req.content_length(0);
    req.insert("test_key", "test_value");
    req.redirect(Verb::GET, "https://domain2asjdhuyjabdhcuyzcbvjankdjcniaxnkcnkn.com:4321/target2?param2=x2");
    LOG_DEBUG(VALUE(req.whole()));
    LOG_DEBUG(VALUE(req.query()));
    LOG_DEBUG(VALUE(req.port()));
    EXPECT_EQ(4321, req.port());
    EXPECT_EQ(true, req["Host"] == "domain2asjdhuyjabdhcuyzcbvjankdjcniaxnkcnkn.com:4321");
    EXPECT_EQ(true, req["test_key"] == "test_value");
    auto value = req["Host"];
    LOG_DEBUG(VALUE(value));
    req.redirect(Verb::DELETE, "https://domain.redirect1/targetName", true);
    EXPECT_EQ(true, req.target() == "https://domain.redirect1/targetName");
    EXPECT_EQ(true, req["Host"] == "domain.redirect1");
    LOG_DEBUG(VALUE(req.whole()));
    LOG_DEBUG(VALUE(req.target()));
    req.redirect(Verb::GET, "/redirect_test", true);
    EXPECT_EQ(true, req.target() == "https://domain.redirect1/redirect_test");
    EXPECT_EQ(true, req["Host"] == "domain.redirect1");
    LOG_DEBUG(VALUE(req.whole()));
    LOG_DEBUG(VALUE(req.target()));
    req.redirect(Verb::GET, "/redirect_test1", false);
    EXPECT_EQ(true, req.target() == "/redirect_test1");
    EXPECT_EQ(true, req["Host"] == "domain.redirect1");
    LOG_DEBUG(VALUE(req.whole()));
    LOG_DEBUG(VALUE(req.target()));
}
TEST(debug, debug) {
    RequestHeadersStored<> req(Verb::PUT, "http://domain2asjdhuyjabdhcuyzcbvjankdjcniaxnkcnkn.com:80/target1?param1=x1");
    req.content_length(0);
    req.insert("test_key", "test_value");
    req.redirect(Verb::GET, "https://domain.com:442/target2?param2=x2", true);
    LOG_DEBUG(VALUE(req.whole()));
}

int main(int argc, char** arg) {
    photon::init();
    DEFER(photon::fini());
    photon::fd_events_init();
    DEFER(photon::fd_events_fini());
    if (Net::et_poller_init() < 0) {
        LOG_ERROR("Net::et_poller_init failed");
        exit(EAGAIN);
    }
    DEFER(Net::et_poller_fini());
    set_log_output_level(ALOG_DEBUG);
    ::testing::InitGoogleTest(&argc, arg);
    LOG_DEBUG("test result:`", RUN_ALL_TESTS());
    return 0;
}