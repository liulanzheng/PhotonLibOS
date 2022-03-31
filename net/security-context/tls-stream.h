#pragma once

#include "common/object.h"

namespace Net {
class ISocketStream;
class ISocketServer;
class ISocketClient;
}  // namespace Net
namespace Security {

enum class SecurityRole {
    Client = 1,
    Server = 2,
};

class TLSContext;

/**
 * @brief Create a tls context, contains cert and private key infomation.
 *
 * @param cert_str certificate in string format
 * @param key_str private key in string format
 * @param passphrase passphrase for private key
 * @return TLSContext* context object pointer
 */
TLSContext* new_tls_context(const char* cert_str, const char* key_str,
                            const char* passphrase = nullptr);

/**
 * @brief Destruct and free a tls context.
 *
 * @param ctx
 */
void delete_tls_context(TLSContext* ctx);

/**
 * @brief Create socket stream on TLS.
 *
 * @param ctx TLS context. Context lifetime is inrelevant to Stream, user should
 *            keep it accessable during whole life time
 * @param base base socket, as underlay socket using for data transport
 * @param role should act as client or server during TLS handshake
 * @param ownership if new socket stream owns base socket.
 * @return Net::ISocketStream*
 */
Net::ISocketStream* new_tls_stream(TLSContext* ctx, Net::ISocketStream* base,
                                   SecurityRole role, bool ownership = false);
/**
 * @brief Create socket server on TLS. as a client socket factory.
 *
 * @param ctx TLS context. Context lifetime is inrelevant to Stream, user should
 *            keep it accessable during whole life time.
 * @param base base socket, as underlay socket using for data transport.
 * @param ownership if new socket stream owns base socket.
 * @return Net::ISocketServer* server factory
 */
Net::ISocketServer* new_tls_server(TLSContext* ctx, Net::ISocketServer* base,
                                   bool ownership = false);

/**
 * @brief Create socket client on TLS. as a client socket factory.
 *
 * @param ctx TLS context. Context lifetime is inrelevant to Stream, user should
 *            keep it accessable during whole life time.
 * @param base base socket, as underlay socket using for data transport.
 * @param ownership if new socket stream owns base socket.
 * @return Net::ISocketClient* client factory
 */
Net::ISocketClient* new_tls_client(TLSContext* ctx, Net::ISocketClient* base,
                                   bool ownership = false);

}  // namespace Security