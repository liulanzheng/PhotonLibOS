#include "../socket.h"
#include "../tlssocket.h"
#include "thread/thread.h"
#include "io/fd-events.h"
#include "common/alog.h"

int main(int argc, char** argv) {
    photon::init();
    photon::fd_events_init();
    Net::ssl_init("net/test/cert.pem", "net/test/key.pem", "Just4Test");
    DEFER({
        Net::ssl_fini();
        photon::fd_events_fini();
        photon::fini();
    });
    auto server = Net::new_tls_socket_server();
    DEFER(delete server);

    auto logHandle = [&](Net::ISocketStream* arg) {
        auto sock = (Net::ISocketStream*) arg;
        char buff[4096];
        uint64_t recv_cnt = 0;
        ssize_t len = 0;
        uint64_t launchtime = photon::now;
        while ((len = sock->read(buff, 4096)) > 0) {
            recv_cnt += len;
        }
        LOG_INFO("Received ` bytes in ` seconds, throughput: `",
                 recv_cnt,
                 (photon::now - launchtime) / 1e6,
                 recv_cnt / ((photon::now - launchtime) / 1e6));
        return 0;
    };
    server->set_handler(logHandle);
    server->bind(31526, Net::IPAddr());
    server->listen(1024);
    server->start_loop(true);
}