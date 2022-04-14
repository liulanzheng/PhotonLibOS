#pragma once
#include <photon/net/socket.h>

struct ssl_st;
typedef struct ssl_st SSL;

namespace photon {
namespace net {

int ssl_init(const char* cert_path, const char* key_path,
             const char* passphrase);
int ssl_init(void* cert, void* key);
int ssl_init();
void* ssl_get_ctx();
int ssl_set_cert(const char* cert);
int ssl_set_pkey(const char* key, const char* passphrase);
int ssl_fini();
ssize_t ssl_set_cert_file(const char* path);
ssize_t ssl_set_pkey_file(const char* path);

ISocketClient* new_tls_socket_client();
ISocketServer* new_tls_socket_server();

}  // namespace net
}
