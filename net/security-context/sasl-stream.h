#pragma once

#include "photon/common/callback.h"
#include <gsasl.h>

namespace Net {
class ISocketStream;
class ISocketServer;
class ISocketClient;
} // namespace Net
namespace Security {
class SaslSession;

using Gsasl_auth_cb = Callback<Gsasl_session *, Net::ISocketStream *>;
using Gsasl_prep_cb = Callback<Gsasl *, Gsasl_session *, Gsasl_property>;

/**
 * @brief Create a sasl client session. It will init the Gsasl ctx, bind the @cb to Gsasl_session,
 * and start the Gsasl_session;
 *
 * @param mech IANA-registered mechanism name of this SASL client. (e.g. "CRAM-MD5", "GSSAPI")
 * @param auth_cb Callback fuction for performing the authentication loop. The format of the data to
 * be transferred, the number of iterations in the loop, and other details are specified by each
 * mechanism.
 * @param prep_cb Callback fuction used by mechanisms to set various parameters (such as username and
 * passwords) or perform verification.  It will be called with a Gsasl_property value indicating the
 * requested behaviour.
 * @return Return sasl session handle or nullptr if gsasl setup failed.
 */
SaslSession *new_sasl_client_session(const char *mech, Gsasl_auth_cb auth_cb,
                                     Gsasl_prep_cb prep_cb);
/**
 * @brief Create a sasl server session.
 * It will init the Gsasl ctx, bind the @cb to Gsasl_session, and start the Gsasl_session;
 *
 * @param mech IANA-registered mechanism name of this SASL server. (e.g. "CRAM-MD5", "GSSAPI")
 * @param auth_cb Callback fuction for performing the authentication loop. The format of the data to
 * be transferred, the number of iterations in the loop, and other details are specified by each
 * mechanism.
 * @param prep_cb Callback fuction used by mechanisms to set various parameters (such as username and
 * passwords) or perform verification.  It will be called with a Gsasl_property value indicating the
 * requested behaviour.
 * @returnReturn sasl session handle or nullptr if gsasl setup failed.
 */
SaslSession *new_sasl_server_session(const char *mech, Gsasl_auth_cb auth_cb,
                                     Gsasl_prep_cb prep_cb);

/**
 * @brief Destruct and free a sasl session.
 *
 * @param session Sasl Session handle.
 */
void delete_sasl_context(SaslSession *session);

/**
 * @brief Create sasl stream.
 * If the auth_cb in @session is not null, it will be called to perform the authentification loop
 * during the construnction.
 *
 * @param session Sasl seesion. One session for one stream. Do not use one session for multiple
 * streams.
 * @param base underlay socketstream using for data transport.
 * @param ownership if new socket stream owns @base socket.
 * @return Return sasl stream handle or nullptr if authentification failed.
 */
Net::ISocketStream *new_sasl_stream(SaslSession *session, Net::ISocketStream *base, bool ownership);

/**
 * @brief Create sasl client. Act as a client socket factory.
 *
 * @param session Sasl session. One session for one client. Do not use one session for multiple
 * clients.
 * @param base underlay socketstream using for data transport.
 * @param ownership if new socket stream owns @base socket.
 * @return Return sasl client handle or nullptr if parameters are invalid.
 */
Net::ISocketClient *new_sasl_client(SaslSession *session, Net::ISocketClient *base, bool ownership);

/**
 * @brief Create sasl server. Act as a server socket factory.
 *
 * @param session Sasl session. One session for one server. Do not use one session for multiple
 * servers.
 * @param base underlay socketstream using for data transport.
 * @param ownership if new socket stream owns @base socket.
 * @return Return sasl server handle or nullptr if parameters are invalid.
 */
Net::ISocketServer *new_sasl_server(SaslSession *session, Net::ISocketServer *base, bool ownership);
} // namespace Security