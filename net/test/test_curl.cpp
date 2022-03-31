#include <gtest/gtest.h>
#include <memory>
#include <string>
#include <thread>
#include <vector>
#include "net/curl.h"
#include "common/alog.h"
#include "thread/thread11.h"
#include "io/fd-events.h"

class StringStream {
    std::string s;

    public:
        std::string& str() {
            return s;
        }

        size_t write(void* c, size_t n) {
            LOG_DEBUG("CALL WRITE");
            s.append((char*)c, n);
            return n;
        }
};

TEST(cURL, feature) {
    photon::init();
    photon::fd_events_init();
    Net::cURL::init();

    std::unique_ptr<Net::cURL> client(new Net::cURL());
    std::unique_ptr<StringStream> buffer(new StringStream());
    client->set_redirect(10).set_verbose(true);
    // for (int i=0;i<2;i++) {
        client->GET("http://work.alibaba-inc.com", buffer.get());
    // }
    LOG_INFO(buffer->str().c_str());
    buffer.release();
    client.release();

    Net::cURL::fini();
    photon::fd_events_fini();
    photon::fini();
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
