#include "common/alog.h"
#include "io/fd-events.h"
#include "thread/thread.h"
#include "common/timeout.h"
#include "net/socket.h"
#include "net/tlssocket.h"

int main(int argc, char** argv) {
    photon::init();
    photon::fd_events_init();
    Net::ssl_init("net/test/cert.pem", "net/test/key.pem", "Just4Test");
    DEFER({
        Net::ssl_fini();
        photon::fd_events_fini();
        photon::fini();
    });
    auto cli = Net::new_tls_socket_client();
    DEFER(delete cli);
    char buff[4096];
    auto tls = cli->connect(Net::EndPoint{Net::IPAddr("127.0.0.1"), 31526});
    if (!tls) {
        LOG_ERRNO_RETURN(0, -1, "failed to connect");
    }
    DEFER(delete tls);
    int timeout_sec = 30;
    Timeout tmo(timeout_sec * 1000 * 1000);
    uint64_t cnt = 0;
    LOG_INFO(tmo.timeout());
    while (photon::now < tmo.expire()) {
        auto ret = tls->send(buff, 4096);
        if (ret < 0) LOG_ERROR_RETURN(0, -1, "Failed to send");
        cnt += ret;
    }
    LOG_INFO("Send ` in ` seconds, ` MB/s", cnt, timeout_sec, cnt / 1024 / 1024 / timeout_sec);
}