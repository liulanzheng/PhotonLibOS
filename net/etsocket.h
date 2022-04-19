#pragma once
#include <photon/net/socket.h>

namespace photon {
namespace net {

extern "C" int et_poller_init();
extern "C" int et_poller_fini();

extern "C" ISocketClient* new_et_tcp_socket_client();
extern "C" ISocketServer* new_et_tcp_socket_server();
extern "C" ISocketStream* new_et_tcp_socket_stream(int fd);

}
}
