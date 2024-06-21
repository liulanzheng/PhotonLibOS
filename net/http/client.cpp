/*
Copyright 2022 The Photon Authors

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
*/

#include "client.h"
#include <bitset>
#include <algorithm>
#include <random>
#include <atomic>
#include <photon/common/alog-stdstring.h>
#include <photon/common/iovector.h>
#include <photon/common/string_view.h>
#include <photon/net/socket.h>
#include <photon/net/security-context/tls-stream.h>
#include <photon/net/utils.h>
#include <photon/thread/vcpu_local.h>

namespace photon {
namespace net {
namespace http {
static const uint64_t kDNSCacheLife = 3600UL * 1000 * 1000;
static constexpr char USERAGENT[] = "PhotonLibOS_HTTP";


class PooledDialer: public VCPULocal {
public:
    net::TLSContext* tls_ctx = nullptr;
    bool tls_ctx_ownership;
    std::unique_ptr<ISocketClient> tcpsock;
    std::unique_ptr<Resolver> resolver;
    std::vector<EndPoint> ips;
    int cur_ip = 0;
    int total_ips = 0;

    PooledDialer(TLSContext *_tls_ctx) :
            tls_ctx(_tls_ctx ? _tls_ctx : new_tls_context(nullptr, nullptr, nullptr)),
            tls_ctx_ownership(_tls_ctx == nullptr),
            resolver(new_default_resolver(kDNSCacheLife)) {
        auto tcp_cli = new_tcp_socket_client();
        tcpsock.reset(new_tcp_socket_pool(tcp_cli, -1, true));
        const char* env_src_ips = std::getenv("HTTP_SOURCE_IP");
        LOG_INFO(VALUE(env_src_ips));
        if (env_src_ips != nullptr) {
            auto str_ips = estring(env_src_ips);
            LOG_INFO(VALUE(str_ips));
            auto ips_split = str_ips.split(',');
            for (auto it : ips_split) {
                std::string x(it);
                LOG_INFO(VALUE(x));
                ips.emplace_back(IPAddr(x.c_str()), 0);
            }
        }
        total_ips = ips.size();
        LOG_INFO(VALUE(total_ips));
    }

    ~PooledDialer() {
        if (tls_ctx_ownership)
            delete tls_ctx;
    }

    int vcpu_exit() override {
        resolver.reset();
        tcpsock.reset();
        return 0;
    }

    ISocketStream* dial(std::string_view host, uint16_t port, bool secure,
                             uint64_t timeout = -1UL);

    template <typename T>
    ISocketStream* dial(const T& x, uint64_t timeout = -1UL) {
        return dial(x.host_no_port(), x.port(), x.secure(), timeout);
    }
};

ISocketStream* PooledDialer::dial(std::string_view host, uint16_t port, bool secure, uint64_t timeout) {
    LOG_DEBUG("Dialing to `:`", host, port);
    auto ipaddr = resolver->resolve(host);
    if (ipaddr.undefined()) {
        LOG_ERROR_RETURN(ENOENT, nullptr, "DNS resolve failed, name = `", host)
    }

    EndPoint ep(ipaddr, port);
    LOG_DEBUG("Connecting ` ssl: `", ep, secure);
    tcpsock->timeout(timeout);

    ISocketStream *sock = nullptr;
    if (total_ips == 0) {
        sock = tcpsock->connect(ep);
    } else {
        sock = tcpsock->connect(ep, &ips[cur_ip]);
        cur_ip = (cur_ip+1) % total_ips;
    }
    if (secure) {
        sock = new_tls_stream(tls_ctx, sock, photon::net::SecurityRole::Client, true);
    }
    if (sock) {
        LOG_DEBUG("Connected ` ", ep, VALUE(host), VALUE(secure));
        return sock;
    }
    LOG_ERROR("connection failed, ssl : ` ep : `  host : `", secure, ep, host);
    if (ipaddr.undefined()) LOG_DEBUG("No connectable resolve result");
    // When failed, remove resolved result from dns cache so that following retries can try
    // different ips.
    resolver->discard_cache(host, ipaddr);
    return nullptr;
}

constexpr uint64_t code3xx() { return 0; }
template<typename...Ts>
constexpr uint64_t code3xx(uint64_t x, Ts...xs)
{
    return (1 << (x-300)) | code3xx(xs...);
}
constexpr static std::bitset<10>
    code_redirect_verb(code3xx(300, 301, 302, 307, 308));

static constexpr size_t kMinimalHeadersSize = 8 * 1024 - 1;
enum RoundtripStatus {
    ROUNDTRIP_SUCCESS,
    ROUNDTRIP_FAILED,
    ROUNDTRIP_REDIRECT,
    ROUNDTRIP_NEED_RETRY,
    ROUNDTRIP_FORCE_RETRY,
    ROUNDTRIP_FAST_RETRY,
};

thread_local PooledDialer *dialer = nullptr;

class ClientImpl : public Client {
public:
    CommonHeaders<> m_common_headers;
    TLSContext *m_tls_ctx;
    ICookieJar *m_cookie_jar;
    ClientImpl(ICookieJar *cookie_jar, TLSContext *tls_ctx) :
        m_tls_ctx(tls_ctx), m_cookie_jar(cookie_jar) { }

    PooledDialer* get_dialer() {
        if (dialer == nullptr) {
            dialer = new PooledDialer(m_tls_ctx);
        }
        return dialer;
    }

    using SocketStream_ptr = std::unique_ptr<ISocketStream>;
    int redirect(Operation* op) {
        if (op->resp.body_size() > 0) {
            op->resp.skip_remain();
        }

        auto location = op->resp.headers["Location"];
        if (location.empty()) {
            LOG_ERROR_RETURN(EINVAL, ROUNDTRIP_FAILED,
                "redirect but has no field location");
        }
        LOG_DEBUG("Redirect to ", location);

        Verb v;
        auto sc = op->status_code - 300;
        if (sc == 3) {  // 303
            v = Verb::GET;
        } else if (sc < 10 && code_redirect_verb[sc]) {
            v = op->req.verb();
        } else {
            LOG_ERROR_RETURN(EINVAL, ROUNDTRIP_FAILED,
                "invalid 3xx status code: ", op->status_code);
        }

        if (op->req.redirect(v, location, op->enable_proxy) < 0) {
            LOG_ERRNO_RETURN(0, ROUNDTRIP_FAILED, "redirect failed");
        }
        return ROUNDTRIP_REDIRECT;
    }

    std::atomic_int concurreny{0};
    int do_roundtrip(Operation* op, Timeout tmo) {
        concurreny++;
        LOG_DEBUG(VALUE(concurreny.load(std::memory_order_relaxed)));
        DEFER(concurreny--);
        op->status_code = -1;
        if (tmo.timeout() == 0)
            LOG_ERROR_RETURN(ETIMEDOUT, ROUNDTRIP_FAILED, "connection timedout");
        auto &req = op->req;
        auto dialer = get_dialer();
        auto s = (m_proxy && !m_proxy_url.empty())
                     ? dialer->dial(m_proxy_url, tmo.timeout())
                     : dialer->dial(req, tmo.timeout());
        if (!s) {
            if (errno == ECONNREFUSED || errno == ENOENT) {
                LOG_ERROR_RETURN(0, ROUNDTRIP_FAST_RETRY, "connection refused")
            }
            LOG_ERROR_RETURN(0, ROUNDTRIP_NEED_RETRY, "connection failed");
        }

        SocketStream_ptr sock(s);
        LOG_DEBUG("Sending request ` `", req.verb(), req.target());
        if (req.send_header(sock.get()) < 0) {
            sock->close();
            req.reset_status();
            LOG_ERROR_RETURN(0, ROUNDTRIP_NEED_RETRY, "send header failed, retry");
        }
        sock->timeout(tmo.timeout());
        if (op->body_stream) {
            // send body_stream
            if (req.write_stream(op->body_stream) < 0) {
                sock->close();
                req.reset_status();
                LOG_ERROR_RETURN(0, ROUNDTRIP_NEED_RETRY, "send body stream failed, retry");
            }
        } else {
            // call body_writer
            if (op->body_writer(&req) < 0) {
                sock->close();
                req.reset_status();
                LOG_ERROR_RETURN(0, ROUNDTRIP_NEED_RETRY, "failed to call body writer, retry");
            }
        }

        if (req.send() < 0) {
            sock->close();
            req.reset_status();
            LOG_ERROR_RETURN(0, ROUNDTRIP_NEED_RETRY, "failed to ensure send");
        }

        LOG_DEBUG("Request sent, wait for response ` `", req.verb(), req.target());
        auto space = req.get_remain_space();
        auto &resp = op->resp;

        if (space.second > kMinimalHeadersSize) {
            resp.reset(space.first, space.second, false, sock.release(), true, req.verb());
        } else {
            auto buf = malloc(kMinimalHeadersSize);
            resp.reset((char *)buf, kMinimalHeadersSize, true, sock.release(), true, req.verb());
        }
        if (op->resp.receive_header(tmo.timeout()) != 0) {
            req.reset_status();
            LOG_ERROR_RETURN(0, ROUNDTRIP_NEED_RETRY, "read response header failed");
        }

        op->status_code = resp.status_code();
        LOG_DEBUG("Got response ` ` code=` || content_length=`", req.verb(),
                  req.target(), resp.status_code(), resp.headers.content_length());
        if (m_cookie_jar) m_cookie_jar->get_cookies_from_headers(req.host(), &resp);
        if (resp.status_code() < 400 && resp.status_code() >= 300 && op->follow)
            return redirect(op);
        return ROUNDTRIP_SUCCESS;
    }

    int call(Operation* /*IN, OUT*/ op) override {
        auto content_length = op->req.headers.content_length();
        auto encoding = op->req.headers["Transfer-Encoding"];
        if ((content_length != 0) && (encoding == "chunked")) {
            op->status_code = -1;
            LOG_ERROR_RETURN(EINVAL, ROUNDTRIP_FAILED,
                            "Content-Length and Transfer-Encoding conflicted");
        }
        op->req.headers.insert("User-Agent", USERAGENT);
        op->req.headers.insert("Connection", "keep-alive");
        op->req.headers.merge(m_common_headers);
        if (m_cookie_jar && m_cookie_jar->set_cookies_to_headers(&op->req) != 0)
            LOG_ERROR_RETURN(0, -1, "set_cookies_to_headers failed");
        Timeout tmo(std::min(op->timeout.timeout(), m_timeout));
        int retry = 0, followed = 0, ret = 0;
        uint64_t sleep_interval = 0;
        while (followed <= op->follow && retry <= op->retry && tmo.timeout() != 0) {
            ret = do_roundtrip(op, tmo);
            if (ret == ROUNDTRIP_SUCCESS || ret == ROUNDTRIP_FAILED) break;
            switch (ret) {
                case ROUNDTRIP_NEED_RETRY:
                    photon::thread_usleep(sleep_interval * 1000UL);
                    sleep_interval = (sleep_interval + 500) * 2;
                    ++retry;
                    break;
                case ROUNDTRIP_FAST_RETRY:
                    ++retry;
                    break;
                case ROUNDTRIP_REDIRECT:
                    retry = 0;
                    ++followed;
                    break;
                default:
                    break;
            }
            if (tmo.timeout() == 0)
                LOG_ERROR_RETURN(ETIMEDOUT, -1, "connection timedout");
            if (followed > op->follow || retry > op->retry)
                LOG_ERRNO_RETURN(0, -1,  "connection failed");
        }
        if (ret != ROUNDTRIP_SUCCESS) LOG_ERROR_RETURN(0, -1,"too many retry, roundtrip failed");
        return 0;
    }

    ISocketStream* native_connect(std::string_view host, uint16_t port, bool secure, uint64_t timeout) override {
        return get_dialer()->dial(host, port, secure, timeout);
    }

    CommonHeaders<>* common_headers() override {
        return &m_common_headers;
    }
};

Client* new_http_client(ICookieJar *cookie_jar, TLSContext *tls_ctx) {
    return new ClientImpl(cookie_jar, tls_ctx);
}

} // namespace http
} // namespace net
} // namespace photon
