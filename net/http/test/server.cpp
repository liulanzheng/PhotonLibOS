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

#include <netinet/tcp.h>
#include <chrono>
#include <gflags/gflags.h>
#include <photon/thread/thread11.h>
#include <photon/io/signalfd.h>
#include <photon/net/socket.h>
#include <photon/common/alog-stdstring.h>
#include <photon/io/fd-events.h>
#include <photon/common/iovector.h>
#include "../parser.h"
#include "boost/beast/http/buffer_body.hpp"
#include "boost/beast/http/dynamic_body.hpp"
#include "boost/beast/http/empty_body.hpp"
#include "boost/beast/http/message.hpp"
#include "boost/beast/http/parser.hpp"
#include "boost/beast/http/serializer.hpp"
#include "boost/beast/http/verb.hpp"
#include "boost/beast/http/write.hpp"
#include "boost/beast/core/static_buffer.hpp"
#include "boost/beast/http/error.hpp"
#include "boost/beast/http/read.hpp"

using namespace photon;

DEFINE_int32(body_size, 4, "filesize in KB");
using beast_string_view =
    boost::basic_string_view<char, struct std::char_traits<char>>;
#define RETURN_IF_FAILED(func)                             \
    if (0 != (func)) {                                     \
        status = Status::failure;                          \
        LOG_ERROR_RETURN(0, , "Failed to perform " #func); \
    }

template <typename T>
struct has_data {
    template <class, class>
    class checker;

    template <typename C>
    static std::true_type test(checker<C, decltype(&C::data)>*) {
        return {};
    };

    template <typename C>
    static std::false_type test(...) {
        return {};
    };

    typedef decltype(test<T>(nullptr)) type;
    static const bool value =
        std::is_same<std::true_type, decltype(test<T>(nullptr))>::value;
};

class MutableIOVBufferSequence {
    iovector_view view;

public:
    explicit MutableIOVBufferSequence(const iovector_view& view) : view(view) {}
    MutableIOVBufferSequence(const MutableIOVBufferSequence& rhs) = default;
    MutableIOVBufferSequence& operator=(const MutableIOVBufferSequence& rhs) =
        default;

    struct iterator {
        struct iovec* iov;
        boost::asio::mutable_buffer buf;

        using value_type = boost::asio::mutable_buffer;
        using reference = value_type&;
        using pointer = value_type*;
        using difference_type = int;
        using iterator_category = std::forward_iterator_tag;
        iterator() = default;
        iterator(struct iovec* iov) : iov(iov) {
            if (iov) buf = value_type(iov->iov_base, iov->iov_len);
        }
        iterator(const iterator& rhs) = default;
        iterator& operator=(const iterator& rhs) = default;
        ~iterator() {}
        iterator operator++() {
            iov++;
            buf = boost::asio::mutable_buffer(iov->iov_base, iov->iov_len);
            return *this;
        }
        iterator operator++(int) {
            auto ret = *this;  // copy
            ++(*this);
            return ret;
        }
        reference operator*() { return buf; }
        pointer operator->() { return &buf; }
        bool operator==(const iterator& rhs) const { return iov == rhs.iov; }
        bool operator!=(const iterator& rhs) const { return iov != rhs.iov; }
    };

    iterator begin() const { return iterator(view.iov); }

    iterator end() const { return iterator(view.iov + view.iovcnt); }

    struct iovec* iovec() {
        return view.iov;
    }

    int iovcnt() { return view.iovcnt; }
};

class ConstIOVBufferSequence {
    iovector_view view;

public:
    explicit ConstIOVBufferSequence(const iovector_view& view) : view(view) {}
    ConstIOVBufferSequence(const ConstIOVBufferSequence& rhs) = default;
    ConstIOVBufferSequence& operator=(const ConstIOVBufferSequence& rhs) =
        default;

    struct iterator {
        struct iovec* iov;
        boost::asio::const_buffer buf;

        using value_type = boost::asio::const_buffer;
        using reference = const value_type&;
        using pointer = const value_type*;
        using difference_type = int;
        using iterator_category = std::forward_iterator_tag;
        iterator() = default;
        iterator(struct iovec* iov) : iov(iov) {
            if (iov) buf = value_type(iov->iov_base, iov->iov_len);
        }
        iterator(const iterator& rhs) = default;
        iterator& operator=(const iterator& rhs) = default;
        ~iterator() {}
        iterator operator++() {
            iov++;
            buf = value_type(iov->iov_base, iov->iov_len);
            return *this;
        }
        iterator operator++(int) {
            auto ret = *this;  // copy
            ++(*this);
            return ret;
        }
        reference operator*() const { return buf; }
        pointer operator->() { return &buf; }
        bool operator==(const iterator& rhs) const { return iov == rhs.iov; }
        bool operator!=(const iterator& rhs) const { return iov != rhs.iov; }
    };

    iterator begin() const { return iterator(view.iov); }

    iterator end() const { return iterator(view.iov + view.iovcnt); }

    struct iovec* iovec() {
        return view.iov;
    }

    int iovcnt() { return view.iovcnt; }
};

class IOVDynamicBuffer {
    mutable IOVectorEntity<4096, 0> iov;
    std::size_t dstart;
    std::size_t dsize;
    std::size_t dprepare;

public:
    photon::mutex mutex;

    using const_buffers_type = ConstIOVBufferSequence;
    using mutable_buffers_type = MutableIOVBufferSequence;

    IOVDynamicBuffer(struct iovec* iov, int iovcnt, std::size_t dsize = 0)
        : iov(iov, iovcnt), dstart(0), dsize(dsize) {}

    IOVDynamicBuffer(IOVDynamicBuffer&& rhs)
        : iov(std::move(rhs.iov)), dstart(rhs.dstart), dsize(rhs.dsize) {}

    IOVDynamicBuffer& operator=(IOVDynamicBuffer&& rhs) {
        iov = std::move(rhs.iov);
        dstart = rhs.dstart;
        dsize = rhs.dsize;
        return *this;
    }

    std::size_t size() { return dsize - dstart; }

    std::size_t max_size() { return iov.sum(); }

    std::size_t capacity() { return iov.sum() - dsize; }

    const_buffers_type data() const {
        iovector_view view;
        iov.slice(dsize, 0, &view);
        return const_buffers_type(view);
    }

    mutable_buffers_type prepare(std::size_t n) {
        iovector_view view;
        int ret = iov.slice(n, dsize, &view);
        if (ret != (ssize_t)n) {
            BOOST_THROW_EXCEPTION(
                std::length_error{"iov_dynamic_buffer overflow"});
        }
        dprepare = n;
        return mutable_buffers_type(view);
    }

    void commit(std::size_t n) { dsize += std::min(n, dprepare); }

    void consume(std::size_t n) { dstart = std::min(dstart + n, dsize); }
};

class EaseTCPStream {
public:
    net::ISocketStream* sock;

    explicit EaseTCPStream(net::ISocketStream* sock) : sock(sock) {}

    template <class MutableBufferSequence>
    typename std::enable_if<!has_data<MutableBufferSequence>::value,
                            size_t>::type
    read_some(MutableBufferSequence const& buffers) {
        IOVector iovs;
        for (const auto& x : buffers) {
            iovs.push_back((void*)x.data(), x.size());
        }
        ssize_t ret;
        { ret = sock->recv((const iovec*)iovs.iovec(), (int)iovs.iovcnt()); }
        if (ret < 0) {
            LOG_ERROR(VALUE(ret), ERRNO());
            return 0;
        }
        return ret;
    }

    template <class MutableBuffer>
    typename std::enable_if<has_data<MutableBuffer>::value, size_t>::type
    read_some(MutableBuffer const& buffer) {
        ssize_t ret = sock->recv(buffer.data(), buffer.size());
        if (ret < 0) {
            LOG_ERROR(VALUE(ret), ERRNO());
            return 0;
        }
        LOG_DEBUG(buffer.size(),
                  ALogString((char*)buffer.data(), buffer.size()));
        return ret;
    }

    template <class MutableBufferSequence>
    std::size_t read_some(MutableBufferSequence const& buffers,
                          boost::beast::error_code& ec) {
        auto ret = read_some(buffers);
        if (ret == 0) ec = boost::beast::http::error::end_of_stream;
        return ret;
    }

    template <class ConstBufferSequence>
    typename std::enable_if<!has_data<ConstBufferSequence>::value, size_t>::type
    write_some(ConstBufferSequence const& buffers) {
        IOVector iovs;
        for (const auto& x : buffers) {
            iovs.push_back((void*)x.data(), x.size());
        }
        ssize_t ret = sock->send2((const struct iovec*)iovs.iovec(),
                                  (int)iovs.iovcnt(), send2_flag);
        if (ret < 0) {
            LOG_ERROR(VALUE(ret), ERRNO());
            return 0;
        }
        return ret;
    }

    template <class ConstBuffer>
    typename std::enable_if<has_data<ConstBuffer>::value, size_t>::type
    write_some(ConstBuffer const& buffer) {
        ssize_t ret = sock->send2((const void*)buffer.data(),
                                  (size_t)buffer.size(), send2_flag);
        if (ret < 0) {
            LOG_ERROR(VALUE(ret), ERRNO());
            return 0;
        }
        return ret;
    }

    template <class ConstBufferSequence>
    std::size_t write_some(ConstBufferSequence const& buffers,
                           boost::beast::error_code& ec) {
        auto ret = write_some(buffers);
        if (ret == 0) ec = boost::beast::http::error::end_of_stream;
        return ret;
    }

    void set_send2_flag(int flag) { send2_flag = flag; }

private:
    int send2_flag;
};
static std::string data_str;
#define HTTPBufferSize 4096
using BeastRequest =
    boost::beast::http::request<boost::beast::http::buffer_body>;
using BeastResponse =
    boost::beast::http::response<boost::beast::http::buffer_body>;
using BeastBuffer = boost::beast::multi_buffer;
using BeastErrorCode = boost::beast::error_code;
using BeastError = boost::beast::http::error;
using BeastField = boost::beast::http::field;
using BeastRequestSerializer =
    boost::beast::http::request_serializer<boost::beast::http::buffer_body>;
using BeastResponseParser =
    boost::beast::http::response_parser<boost::beast::http::buffer_body>;

boost::string_view to_boost_sv(std::string_view sv) {
    return boost::string_view(sv.data(), sv.size());
}
std::string_view to_std_sv(boost::string_view bsv) {
    return std::string_view(bsv.data(), bsv.size());
}

inline uint64_t GetSteadyTimeS() {
    auto now = std::chrono::steady_clock::now();
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
               now.time_since_epoch())
        .count() / 1000 / 1000 / 1000;
}
class TestServer {
public:
    enum class Status {
        ready = 0,
        running = 1,
        stopping = 2,
        failure = 3
    } status = Status::ready;
    uint16_t m_port = 19876;
    photon::join_handle* th = nullptr;
    std::string m_text = "";
    uint64_t qps = 0, rec_time;
    char req_buf[4096];

    ~TestServer() {
        stop();
        if (th) {
            photon::thread_interrupt((photon::thread*)th, ECANCELED);
            photon::thread_join(th);
            th = nullptr;
        }
    }

    void run(uint16_t port, net::IPAddr ip) {
        if (status != Status::ready) {
            return;
        }
        auto sock = net::new_tcp_socket_server();
        DEFER(delete (sock));
        sock->timeout(1000UL * 1000);
        RETURN_IF_FAILED(sock->setsockopt(IPPROTO_TCP, TCP_NODELAY, 1L));
        RETURN_IF_FAILED(sock->setsockopt(SOL_SOCKET, SO_REUSEPORT, 1));
        RETURN_IF_FAILED(sock->bind(port, ip));
        RETURN_IF_FAILED(sock->listen(1024));
        status = Status::running;
        rec_time = GetSteadyTimeS();
        sock->set_handler({this, &TestServer::control_handler});
        sock->start_loop(false);

        while (status != Status::stopping) {
            photon::thread_usleep(1000 * 1000);
        }
        status = Status::ready;
    }

    int control_handler(net::ISocketStream* sock) {
        LOG_DEBUG("enter control handler");
        bool flag_keep_alive = true;
        while (flag_keep_alive) {
            sock->setsockopt(IPPROTO_TCP, TCP_NODELAY, 1L);
            EaseTCPStream stream(sock);
            stream.set_send2_flag(0);
            BeastErrorCode ec{};
            BeastBuffer buffer;
            boost::beast::http::request<boost::beast::http::buffer_body> req;
            std::string chunk = "";
            boost::beast::http::request_parser<boost::beast::http::buffer_body> rp(
                req);
            rp.get().body().data = req_buf;
            rp.get().body().size = sizeof(req_buf);
            rp.get().body().more = true;
            boost::beast::http::read_header(stream, buffer, rp, ec);
            while (!rp.is_done()) {
                rp.get().body().data = req_buf;
                rp.get().body().size = sizeof(req_buf);
                rp.get().body().more = true;
                boost::beast::http::read(stream, buffer, rp, ec);
                m_text +=
                    std::string(req_buf, sizeof(req_buf) - rp.get().body().size);
                if (ec == BeastError::end_of_chunk) ec = {};
                if (ec) break;
            }
            boost::beast::http::request_parser<boost::beast::http::buffer_body> rp1(
                req);
            //--------LOG REQ-------
            req = rp.release();
            // LOG_INFO("request get");
            // auto &log_req = req;
            // LOG_INFO("User-Agent: ", to_std_sv(log_req["User-Agent"]));
            // LOG_INFO("Connection: ", to_std_sv(log_req["Connection"]));
            // for (auto &kv : log_req) {
            //     auto key = kv.name_string();
            //     auto value = kv.value();
            //     LOG_INFO(std::string_view(key.data(), key.size()), ":", std::string_view(value.data(), value.size()));
            // }
            //----------------------
            boost::beast::http::response<boost::beast::http::buffer_body> resp;
            boost::beast::http::response_serializer<boost::beast::http::buffer_body>
                rs(resp);
            if (ec) {
                LOG_ERRNO_RETURN(0, -1, ec.message());
            }
            ++qps;
            auto now = GetSteadyTimeS();
            if (now > rec_time) {
                LOG_INFO("qps = `, body_size = `", qps, data_str.size());
                qps = 0;
                rec_time = now;
            }
            size_t body_write_cnt = 0, tmp = 0;
            switch (rp.get().method()) {
                case boost::beast::http::verb::get:
                    resp.content_length(data_str.size());
                    resp.keep_alive(true);
                    resp.result(200);
                    boost::beast::http::write_header(stream, rs, ec);
                    if (ec) {
                        LOG_ERRNO_RETURN(0, -1, ec.message());
                    }
                    resp.body().data = (void*)data_str.data();
                    resp.body().size = data_str.size();
                    resp.body().more = false;
                    tmp = boost::beast::http::write(stream, rs, ec);
                    // LOG_INFO("send response, body_size=", data_str.size());
                    if (ec) {
                        LOG_ERRNO_RETURN(0, -1, ec.message());
                    }
                    if (tmp != data_str.size()) {
                        LOG_ERROR_RETURN(0, -1, "write resp failed ", VALUE(tmp));
                    }
                    break;
                default:
                    break;
            }
        }
        return 0;
    }

    bool launch() {
        th = photon::thread_enable_join(photon::thread_create11(
            &TestServer::run, this, m_port, net::IPAddr()));
        while (status == Status::ready) photon::thread_usleep(1000);
        return status != Status::failure;
    }

    void stop() {
        if (status == Status::running) status = Status::stopping;
    }
};
bool stop_flag = false;
bool server_done(TestServer* serv) {
    if (serv->status == TestServer::Status::ready ||
        serv->status == TestServer::Status::failure) return true;
    return false;
}
static void stop_handler(int signal) { stop_flag = true; }
int main(int argc, char** argv) {
    gflags::ParseCommandLineFlags(&argc, &argv, true);
    photon::init();
    DEFER(photon::fini());
    photon::fd_events_init();
    DEFER(photon::fd_events_fini());
    set_log_output_level(ALOG_INFO);
    if (photon::sync_signal_init() < 0) {
        LOG_ERROR("photon::sync_signal_init failed");
        exit(EAGAIN);
    }
    DEFER(photon::sync_signal_fini());

    photon::block_all_signal();
    photon::sync_signal(SIGINT, &stop_handler);
    photon::sync_signal(SIGTERM, &stop_handler);
    photon::sync_signal(SIGTSTP, &stop_handler);
    data_str.resize(FLAGS_body_size * 1024);
    for (auto &c : data_str) c = '0';
    auto serv = new TestServer();
    auto serv_th = photon::thread_enable_join(
        photon::thread_create11(&TestServer::launch, serv));
    while (!stop_flag || !server_done(serv)) {
        photon::thread_usleep(100UL * 1000);
        if (stop_flag) {
            serv->stop();
        }
    }
    photon::thread_interrupt((photon::thread*)serv_th, ECANCELED);
    photon::thread_join(serv_th);
}
