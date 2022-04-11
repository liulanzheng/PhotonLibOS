#include <gtest/gtest.h>
#include <openssl/hmac.h>
#include <utils/Utils.h>
#include <chrono>
#include <string>
#include <vector>
#include <thread>
#include "net/curl.h"
#include "net/socket.h"
#include "io/fd-events.h"
#include "thread/thread.h"
#include "fs/localfs.h"
#define protected public
#include "../httpfs.cpp"
#undef protected

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
        "GET\n\n\n" + std::to_string(expires) + "\n/" + bucket + path));
}
TEST(httpfs, basic) {
    system(
        "echo 'hello' > /tmp/ease-httpfs-test-file && osscmd put "
        "/tmp/ease-httpfs-test-file oss://ray-dr/hello");

    auto fs = FileSystem::new_httpfs();
    DEFER(delete fs);
    auto file = fs->open("/ray-dr.oss-cn-hangzhou-zmf.aliyuncs.com/hello", 0);
    DEFER(delete file);
    auto expire =
        std::chrono::duration_cast<std::chrono::seconds>(
            (std::chrono::system_clock::now() + std::chrono::seconds(3600))
                .time_since_epoch())
            .count();
    auto signature =
        oss_signature("ray-dr", "GET", "/hello", expire, OSS_ID,
                      OSS_KEY);

    /// set param by ioctl
    auto queryparam =
        "OSSAccessKeyId=LTAIWsbCDjMKQbaW&Expires=" + std::to_string(expire) +
        "&Signature=" + Net::url_escape(signature.c_str());
    file->ioctl(FileSystem::HTTP_URL_PARAM, queryparam.c_str());
    LOG_DEBUG(VALUE(signature));
    char buffer[64];
    auto ret = file->pread(&buffer, 64, 0);
    EXPECT_EQ(6, ret);
    if (ret >= 0) buffer[ret] = 0;
    EXPECT_STREQ("hello\n", buffer);
}

// TEST(httpfs, multi_threads) {
//     system(
//         "echo 'hello' > /tmp/ease-httpfs-test-file && osscmd put "
//         "/tmp/ease-httpfs-test-file oss://ray-dr/hello");

//     std::vector<std::thread> ths;


//     for (int _=0 ;_<10;_++) {
//         ths.emplace_back([&](){
//             photon::init();
//             DEFER(photon::fini());
//             int ret = photon::fd_events_init();
//             if (ret < 0) LOG_ERROR_RETURN(0, -1, "failed to init epoll subsystem");
//             DEFER(photon::fd_events_fini());
//             DEFER(photon::wait_all());
//             ret = Net::libcurl_init();
//             if (ret < 0) LOG_ERROR_RETURN(0, -1, "failed to init curl");
//             DEFER(Net::libcurl_fini());
//             auto fs = FileSystem::new_httpfs();
//             DEFER(delete fs);
//             auto file = fs->open("/ray-dr.oss-cn-hangzhou-zmf.aliyuncs.com/hello", 0);
//             DEFER(delete file);
//             auto expire =
//                 std::chrono::duration_cast<std::chrono::seconds>(
//                     (std::chrono::system_clock::now() + std::chrono::seconds(3600))
//                         .time_since_epoch())
//                     .count();
//             auto signature =
//                 oss_signature("ray-dr", "GET", "/hello", expire, OSS_ID,
//                             OSS_KEY);

//             /// set param by ioctl
//             auto queryparam =
//                 "OSSAccessKeyId=LTAIWsbCDjMKQbaW&Expires=" + std::to_string(expire) +
//                 "&Signature=" + Net::url_escape(signature.c_str());
//             file->ioctl(FileSystem::HTTP_URL_PARAM, queryparam.c_str());
//             LOG_DEBUG(VALUE(signature));
//             char buffer[64];
//             ret = file->pread(&buffer, 64, 0);
//             EXPECT_EQ(6, ret);
//             if (ret >= 0) buffer[ret] = 0;

//             EXPECT_STREQ("hello\n", buffer);
//         });
//     }

//     for (auto &x : ths)
//         x.join();
// }

TEST(httpfs, wrong_sign) {
    system(
        "echo 'hello' > /tmp/ease-httpfs-test-file && osscmd put "
        "/tmp/ease-httpfs-test-file oss://ray-dr/hello");

    auto fs = FileSystem::new_httpfs();
    DEFER(delete fs);
    auto file = fs->open("/ray-dr.oss-cn-hangzhou-zmf.aliyuncs.com/hello", 0);
    DEFER(delete file);
    auto expire =
        std::chrono::duration_cast<std::chrono::seconds>(
            (std::chrono::system_clock::now() + std::chrono::seconds(3600))
                .time_since_epoch())
            .count();
    auto signature =
        oss_signature("ray-dr", "GET", "/hello", expire, OSS_ID,
                      OSS_KEY);
    /// set param by ioctl
    auto queryparam =
        "OSSAccessKeyId=LTAIWsbCDjMKQbaW&Expires=" + std::to_string(expire) +
        "&Signature=" + Net::url_escape(signature.c_str());
    auto wrongparam = queryparam + 'x';
    file->ioctl(FileSystem::HTTP_URL_PARAM, wrongparam.c_str());
    LOG_DEBUG(VALUE(signature));
    char buffer[64];
    auto ret = file->pread(&buffer, 64, 0);
    ERRNO err;
    EXPECT_EQ(-1, ret);
    EXPECT_EQ(EACCES, err.no);
}

TEST(httpfs, zerofile) {
    system(
        "rm -f /tmp/ease-httpfs-test-zero-file && touch "
        "/tmp/ease-httpfs-test-zero-file && osscmd put "
        "/tmp/ease-httpfs-test-zero-file oss://ray-dr/zero");

    auto fs = FileSystem::new_httpfs();
    DEFER(delete fs);
    auto file = fs->open("/ray-dr.oss-cn-hangzhou-zmf.aliyuncs.com/zero", 0);
    DEFER(delete file);
    auto expire =
        std::chrono::duration_cast<std::chrono::seconds>(
            (std::chrono::system_clock::now() + std::chrono::seconds(3600))
                .time_since_epoch())
            .count();
    auto signature =
        oss_signature("ray-dr", "GET", "/zero", expire, OSS_ID,
                      OSS_KEY);

    /// set param by ioctl
    auto queryparam =
        "OSSAccessKeyId=LTAIWsbCDjMKQbaW&Expires=" + std::to_string(expire) +
        "&Signature=" + Net::url_escape(signature.c_str());
    file->ioctl(FileSystem::HTTP_URL_PARAM, queryparam.c_str());
    LOG_DEBUG(VALUE(signature));
    char buffer[64];
    auto ret = file->pread(&buffer, 64, 0);
    EXPECT_EQ(0, ret);
    if (ret >= 0) buffer[ret] = 0;
    EXPECT_STREQ("", buffer);
}

photon::condition_variable sleep_cv;
int sleep_handler(void*, Net::ISocketStream* sock) {
    sleep_cv.wait_no_lock();
    return 0;
}
TEST(httpfs, timeout) {
    auto server = Net::new_tcp_socket_server();
    DEFER(delete server);
    server->set_handler({nullptr, &sleep_handler});
    server->bind(38812, Net::IPAddr());
    server->listen();
    server->start_loop();
    auto fs = FileSystem::new_httpfs(false, 1UL * 1000 * 1000);
    DEFER(delete fs);
    auto file = fs->open("/127.0.0.1:38812/wtf", 0);
    DEFER(delete file);

    char buffer[64];
    auto ret = file->pread(&buffer, 64, 0);
    EXPECT_EQ(-1, ret);
    EXPECT_EQ(ENOENT, errno);
    sleep_cv.notify_all();
}

TEST(httpfs, with_protocol) {
    system(
        "echo 'hello' > /tmp/ease-httpfs-test-file && osscmd put "
        "/tmp/ease-httpfs-test-file oss://ray-dr/hello");

    auto fs = FileSystem::new_httpfs();
    DEFER(delete fs);
    auto file = fs->open("http://ray-dr.oss-cn-hangzhou-zmf.aliyuncs.com/hello", 0);
    DEFER(delete file);
    auto expire =
        std::chrono::duration_cast<std::chrono::seconds>(
            (std::chrono::system_clock::now() + std::chrono::seconds(3600))
                .time_since_epoch())
            .count();
    auto signature =
        oss_signature("ray-dr", "GET", "/hello", expire, OSS_ID,
                      OSS_KEY);

    /// set param by ioctl
    auto queryparam =
        "OSSAccessKeyId=LTAIWsbCDjMKQbaW&Expires=" + std::to_string(expire) +
        "&Signature=" + Net::url_escape(signature.c_str());
    file->ioctl(FileSystem::HTTP_URL_PARAM, queryparam.c_str());
    LOG_DEBUG(VALUE(signature));
    char buffer[64];
    auto ret = file->pread(&buffer, 64, 0);
    EXPECT_EQ(6, ret);
    if (ret >= 0) buffer[ret] = 0;
    EXPECT_STREQ("hello\n", buffer);
}

TEST(httpfs, callback) {
    system(
        "echo 'hello' > /tmp/ease-httpfs-test-file && osscmd put "
        "/tmp/ease-httpfs-test-file oss://ray-dr/hello");

    auto sign = [&](const char* fn, FileSystem::IFile* fp){
        LOG_DEBUG(VALUE(fn));
        auto expire =
            std::chrono::duration_cast<std::chrono::seconds>(
                (std::chrono::system_clock::now() + std::chrono::seconds(3600))
                    .time_since_epoch())
                .count();
        auto signature =
            oss_signature("ray-dr", "GET", "/hello", expire, OSS_ID,
                        OSS_KEY);

        /// set param by ioctl
        auto queryparam =
            "OSSAccessKeyId=LTAIWsbCDjMKQbaW&Expires=" + std::to_string(expire) +
            "&Signature=" + Net::url_escape(signature.c_str());
        fp->ioctl(FileSystem::HTTP_URL_PARAM, queryparam.c_str());
        LOG_DEBUG(VALUE(signature));
    };

    auto fs = FileSystem::new_httpfs(false, -1, -1, sign);
    DEFER(delete fs);
    auto file = fs->open("http://ray-dr.oss-cn-hangzhou-zmf.aliyuncs.com/hello", 0);
    DEFER(delete file);
    char buffer[64];
    auto ret = file->pread(&buffer, 64, 0);
    EXPECT_EQ(6, ret);
    if (ret >= 0) buffer[ret] = 0;
    EXPECT_STREQ("hello\n", buffer);
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    photon::init();
    DEFER(photon::fini());
    int ret = photon::fd_events_init();
    if (ret < 0) LOG_ERROR_RETURN(0, -1, "failed to init epoll subsystem");
    DEFER(photon::fd_events_fini());
    ret = Net::libcurl_init();
    if (ret < 0) LOG_ERROR_RETURN(0, -1, "failed to init curl");
    DEFER(Net::libcurl_fini());
    LOG_INFO("test result:`", RUN_ALL_TESTS());
    photon::thread_sleep(5);
    return 0;
}