#include <errno.h>
#include <gflags/gflags.h>
#include <fcntl.h>
#include <unistd.h>
#include <iostream>
#include <time.h>
#include <chrono>
#include <openssl/hmac.h>
#include <utils/Utils.h>
#include "fs/range-split.h"
#include "common/alog-stdstring.h"
#include "fs/filesystem.h"
#include <sstream>
#include "common/iovector.h"
#include "thread/thread11.h"
#include "io/fd-events.h"
#include "net/etsocket.h"
#include "net/curl.h"
#include "net/http/client.h"
#include "net/socket.h"
#include "fs/httpfs/httpfs.h"
#include "fs/async_filesystem.h"
DECLARE_int32(jobs);
DECLARE_bool(v1);
DECLARE_bool(v2);
DECLARE_bool(v);
DECLARE_bool(vv);
DECLARE_uint64(timeout);
DEFINE_int32(jobs, 8, "concurrency");
DEFINE_uint64(timeout, -1UL, "timeout in seconds");
DEFINE_bool(v1, false, "test v1");
DEFINE_bool(v2, false, "test v2");
DEFINE_bool(v, false, "log detail");
DEFINE_bool(vv, false, "log debug detail");

// int32_t FLAGS_jobs = 128;

// uint64_t FLAGS_timeout = -1UL;

inline bool checkflags() { return false; }

template <typename... Args>
inline bool checkflags(const char* flag, Args... args) {
    gflags::CommandLineFlagInfo info;
    return (gflags::GetCommandLineFlagInfo(flag, &info) && !info.is_default) ||
           checkflags(args...);
}

#define FORBIDDEN_FLAGS(...)                               \
    {                                                      \
        if (checkflags(__VA_ARGS__)) {                     \
            std::cout << "Invalid arguments" << std::endl; \
            usage();                                       \
            exit(EINVAL);                                  \
        }                                                  \
    }

constexpr char USAGESTR[] = R""""(
./ossfs_perf --jobs=64  --v1 --v2
)"""";

inline void usage() { std::cout << USAGESTR << std::endl; }

inline uint64_t GetSteadyTimeNs() {
    auto now = std::chrono::steady_clock::now();
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
               now.time_since_epoch())
        .count();
}

using namespace FileSystem;

struct SegmentGenerator {
    FileSystem::range_split rs;
    FileSystem::range_split::all_parts_t ap;
    FileSystem::range_split::all_parts_t::iterator it;
    SegmentGenerator(off_t offset, size_t count, size_t interval):
        rs(offset, count, interval),
        ap(rs.all_parts()),
        it(ap.begin()) {}

    bool get(off_t &offset, size_t &count) {
        offset = rs.multiply(it->i, it->offset);
        count = it->length;
        return it != ap.end();
    }
    void next() {
        if (it != ap.end())
            ++it;
    }
    int count() {
        return rs.aend - rs.abegin;
    }
};

constexpr size_t INTERVAL = 4* 1024UL * 1024;

static int cnt = 0;
static int all = 0;

static bool failure = false;


static std::vector<std::string> str_split(const std::string& input,
                                          const char delim) {
    std::vector<std::string> list;
    std::stringstream iss(input);
    std::string line;
    while (std::getline(iss, line, delim)) {
        list.emplace_back(line);
    }
    return list;
}

static int download(FileSystem::IFile* file, int dst, SegmentGenerator& rs) {
    off_t offset;
    size_t count;
    while (!failure && rs.get(offset, count)) {
        rs.next();
        LOG_INFO("Start ", VALUE(offset), VALUE(count));
        IOVector iov;
        iov.push_back(count);
        int retry = 0;
        ssize_t ret = -1;
        while (retry < 6 && ret < 0) {
            ret = file->preadv(iov.iovec(), iov.iovcnt(), offset);
            if (ret < 0) {
                photon::thread_sleep(retry);
                retry ++;
            }
        }
        if (ret < 0) {
            failure = true;
            LOG_ERROR_RETURN(0, -1, "Fetch failed on ", VALUE(offset),
                            VALUE(count));
        }
        ret = pwritev(dst, iov.iovec(), iov.iovcnt(), offset);
        if (ret < 0) {
            failure = true;
            LOG_ERROR_RETURN(0, -1, "Write failed on ", VALUE(offset),
                             VALUE(count));
        }
        LOG_DEBUG("Done ", VALUE(offset), VALUE(count));
        cnt ++;
        LOG_INFO("(`/`)`%", cnt, all, 100 * cnt / all);
    }
    return 0;
}

static void* timeout_kill(void* timeout) {
    auto ret = photon::thread_sleep((uint64_t)timeout);
    if (ret >= 0) {
        puts("ERROR: timeout");
        exit(ETIMEDOUT);
    }
    return NULL;
}

// const uint64_t concurrencyDataLen = 512 * 1024 * 1024;
// const uint64_t segLen = 256 * 1024 * 1024;
std::string concurrencyFileName = "/tmp/ease_test_oss_throuput";
std::string dstFileName = "/tmp/ease_test_oss_throuput";

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

int test(FileSystem::IFileSystem* fs) {
    cnt = 0;
    // auto fs = new_httpfs_v2();

    // auto ossfs_v1 = new_ossfs("oss-cn-hangzhou-zmf.aliyuncs.com", "qisheng-ds",
    //                  OSS_ID, OSS_KEY);
    // auto fs = new_async_fs_adaptor(ossfs_v1);

    // DEFER(delete fs);
    auto file = fs->open("/qisheng-ds.oss-cn-hangzhou-zmf.aliyuncs.com/tmp/ease_test_oss_throuput", 0);
    DEFER(delete file);
    auto expire =
        std::chrono::duration_cast<std::chrono::seconds>(
            (std::chrono::system_clock::now() + std::chrono::seconds(3600))
                .time_since_epoch())
            .count();
    auto signature =
        oss_signature("qisheng-ds", "GET", "/tmp/ease_test_oss_throuput", expire, OSS_ID,
                      OSS_KEY);

    /// set param by ioctl
    auto queryparam =
        "OSSAccessKeyId=LTAIWsbCDjMKQbaW&Expires=" + std::to_string(expire) +
        "&Signature=" + Net::url_escape(signature.c_str());
    file->ioctl(FileSystem::HTTP_URL_PARAM, queryparam.c_str());

    struct stat stat;
    auto ret = file->fstat(&stat);
    if (ret < 0) {
        LOG_ERROR_RETURN(0, EACCES, "Failed to stat");
    }
    size_t filesize = stat.st_size;

    int dst = open(dstFileName.c_str(), O_RDWR | O_CREAT, 0644);
    if (dst < 0) {
        LOG_ERROR_RETURN(0, errno, "Failed to open dest file");
    }
    DEFER(close(dst));

    std::vector<photon::join_handle*> pool;

    SegmentGenerator rs(0, filesize, INTERVAL);
    all = rs.count();
    auto t_begin = GetSteadyTimeNs();
    for (int i = 0; i < FLAGS_jobs; i++) {
        pool.emplace_back(photon::thread_enable_join(
            photon::thread_create11(&download, file, dst, rs)));
    }
    for (auto& jh : pool) {
        photon::thread_join(jh);
    }
    if (!failure) {
        LOG_INFO("Done");
        auto t_end = GetSteadyTimeNs();
        return filesize * 1000 / (t_end - t_begin);
    } else {
        LOG_INFO("Failed");
        return -1;
    }
}

int main(int argc, char** argv) {
    gflags::ParseCommandLineFlags(&argc, &argv, true);
    // set_log_output_level(ALOG_AUDIT);  // no log output
    // if (FLAGS_v) {
    //     set_log_output_level(ALOG_INFO);
    // }
    // if (FLAGS_vv) {
    //     set_log_output_level(ALOG_DEBUG);
    // }
    // std::string src = argv[1];
    // if (src == "") LOG_ERROR_RETURN(EINVAL, EINVAL, "Empty filename");
    // if (src[0] != '/') src = "/" + src;
    set_log_output_level(ALOG_INFO);

    if (photon::init() < 0)
        LOG_ERROR_RETURN(0, ENOSYS, "Failed to init photon");
    DEFER(photon::fini());

    photon::thread_create(timeout_kill, (void*)FLAGS_timeout);

    if (photon::fd_events_init() < 0)
        LOG_ERROR_RETURN(0, ENOSYS, "Failed to init fdevents");
    DEFER(photon::fd_events_fini());

    if (Net::et_poller_init() < 0) {
        LOG_ERROR("Net::et_poller_init failed");
        exit(EAGAIN);
    }
    DEFER(Net::et_poller_fini());
    auto ret = Net::libcurl_init();
    if (ret < 0) LOG_ERROR_RETURN(0, -1, "failed to init curl");
    DEFER(Net::libcurl_fini());

    // if (argc != 2) {
    //     LOG_ERROR_RETURN(EINVAL, EINVAL,
    //                      "Can only accept cat one file at once");
    // }

    int res_v1 = 0, res_v2 = 0;
    if (FLAGS_v1) {
        auto fs_v1 = new_httpfs();
        DEFER(delete fs_v1);
        res_v1 = test(fs_v1);
    }

    if (FLAGS_v2) {
        auto fs_v2 = new_httpfs_v2();
        DEFER(delete fs_v2);
        res_v2 = test(fs_v2);
    }

    LOG_INFO("httpfs_v1 download speed : `MB/s", res_v1);
    LOG_INFO("httpfs_v2 download speed : `MB/s", res_v2);
}
