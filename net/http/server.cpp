#include "server.h"
#include <netinet/tcp.h>
#include <chrono>
#include <string>
#include <fcntl.h>
#include <sys/statvfs.h>
#include <sys/vfs.h>
#include <queue>
#include <photon/thread/thread11.h>
#include <photon/io/signalfd.h>
#include <photon/net/socket.h>
#include <photon/common/alog-stdstring.h>
#include <photon/io/fd-events.h>
#include <photon/common/iovector.h>
#include <photon/common/estring.h>
#include <photon/fs/filesystem.h>
#include <photon/fs/httpfs/httpfs.h>
#include <photon/common/expirecontainer.h>
#include "client.h"
#include <boost/beast/http/buffer_body.hpp>
#include <boost/beast/http/dynamic_body.hpp>
#include <boost/beast/http/empty_body.hpp>
#include <boost/beast/http/message.hpp>
#include <boost/beast/http/parser.hpp>
#include <boost/beast/http/serializer.hpp>
#include <boost/beast/http/verb.hpp>
#include <boost/beast/http/write.hpp>
#include <boost/beast/core/static_buffer.hpp>
#include <boost/beast/http/error.hpp>
#include <boost/beast/http/read.hpp>

namespace photon {
namespace net {

constexpr static uint64_t KminFileLife = 30 * 1000UL * 1000UL;
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
using BeastResponseSerializer =
    boost::beast::http::response_serializer<boost::beast::http::buffer_body>;
using BeastRequestParser =
    boost::beast::http::request_parser<boost::beast::http::buffer_body>;

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

class HTTPServerRequestImpl : public HTTPServerRequest {
public:
    BeastRequest req;
    Protocol m_secure = Protocol::HTTP;
    estring m_origin_host;
    BeastErrorCode get_req(EaseTCPStream &stream) {
        BeastErrorCode ec{};
        BeastBuffer buffer;
        char buf[4096];
        BeastRequestParser rp(req);
        rp.get().body().data = buf;
        rp.get().body().size = sizeof(buf);
        rp.get().body().more = true;
        boost::beast::http::read_header(stream, buffer, rp, ec);
        while (!rp.is_done()) {
            rp.get().body().data = buf;
            rp.get().body().size = sizeof(buf);
            rp.get().body().more = true;
            boost::beast::http::read(stream, buffer, rp, ec);
            if (ec == BeastError::end_of_chunk) ec = {};
            if (ec) break;
        }
        if (!ec) {
            req = rp.release();
            m_origin_host = to_std_sv(req["Host"]);
        }
        return ec;
    }
    void SetProtocol(Protocol p) override{
        m_secure = p;
    }
    Protocol GetProtocol() override {
        return m_secure;
    }
    Verb GetMethod() override {
        auto v = req.method_string();
        return string_to_verb(to_std_sv(v));
    }
    std::string_view GetOriginHost() override {
        return m_origin_host;
    }
    void SetMethod(Verb v) override {
        req.method_string(to_boost_sv(verbstr[v]));
    }
    std::string_view GetTarget() override {
        return to_std_sv(req.target());
    }
    void SetTarget(std::string_view target) override {
        req.target(to_boost_sv(target));
    }
    std::string_view Find(std::string_view key) override {
        return to_std_sv(req[to_boost_sv(key)]);
    }
    int Insert(std::string_view key, std::string_view value) override {
        req.insert(to_boost_sv(key), to_boost_sv(value));
        return 0;
    }
    int Erase(std::string_view key) override {
        req.erase(to_boost_sv(key));
        return 0;
    }
    HeaderLists GetKVs() override {
        HeaderLists ret;
        for (auto &kv : req) {
            auto key = kv.name_string();
            auto value = kv.value();
            ret.emplace_back(to_std_sv(key), to_std_sv(value));
        }
        return ret;
    }
    std::string_view Body() override {
        return {(char*)req.body().data, req.body().size};
    }
    size_t ContentLength() override {
        return estring_view(to_std_sv(req["Content-Length"])).to_uint64();
    }
    std::pair<ssize_t, ssize_t> Range() override {
        auto range = to_std_sv(req["Range"]);
        if (range.empty()) return {-1, -1};
        auto eq_pos = range.find("=");
        auto dash_pos = range.find("-");
        auto start_sv = estring_view(range.substr(eq_pos + 1, dash_pos - eq_pos + 1));
        auto end_sv = estring_view(range.substr(dash_pos + 1));
        auto start = start_sv.to_uint64();
        auto end = end_sv.to_uint64();
        if (start_sv.empty()) return {-1, end};
        if (end_sv.empty()) return {start, -1};
        return {start, end};
    }
};

class HTTPServerResponseImpl : public HTTPServerResponse {
public:
    BeastResponse resp;
    BeastResponseSerializer rs = BeastResponseSerializer(resp);
    EaseTCPStream *stream;
    HTTPServerRequestImpl *m_req;
    size_t body_cnt = 0;
    bool header_is_done = false, is_done = false;
    HTTPServerResponseImpl(EaseTCPStream* stream, HTTPServerRequestImpl* req)
                                                : stream(stream), m_req(req) {
        stream->set_send2_flag(MSG_MORE);
    }
    ~HTTPServerResponseImpl() {
        Done();
    }
    RetType HeaderDone() override {
        if (header_is_done) return RetType::success;
        header_is_done = true;
        BeastErrorCode ec{};
        boost::beast::http::write_header(*stream, rs, ec);
        if (ec) {
            LOG_ERRNO_RETURN(0, RetType::failed, ec.message());
        }
        return RetType::success;
    }
    RetType Done() override {
        if (is_done) return RetType::success;
        is_done = true;
        if (!header_is_done) HeaderDone();
        if (resp.chunked() && (send_chunk(nullptr, 0) < 0))
            return RetType::failed;
        return RetType::success;
    }
    void SetResult(int status_code) override {
        if (header_is_done) return;
        resp.result(status_code);
    }
    int GetResult() override {
        return resp.result_int();
    }
    std::string_view Find(std::string_view key) override {
        return to_std_sv(resp[to_boost_sv(key)]);
    }
    HeaderLists GetKVs() override {
        HeaderLists ret;
        for (auto &kv : resp) {
            auto key = kv.name_string();
            auto value = kv.value();
            ret.emplace_back(to_std_sv(key), to_std_sv(value));
        }
        return ret;
    }
    int Insert(std::string_view key, std::string_view value) override {
        resp.set(to_boost_sv(key), to_boost_sv(value));
        return 0;
    }
    int Erase(std::string_view key) override {
        resp.erase(to_boost_sv(key));
        return 0;
    }
    int send_chunk(void* buf, size_t count) {
        auto s = std::to_string(count) + "\r\n";
        if (stream->sock->write(s.data(), s.size()) != (ssize_t)s.size()) return -1;
        if (stream->sock->write(buf, count) != (ssize_t)count) return -1;
        if (stream->sock->write("\r\n", 2) != 2) return -1;
        return count;
    }
    ssize_t Write(void* buf, size_t count) override {
        if (!header_is_done) HeaderDone();
        if (resp.chunked())
            return send_chunk(buf, count);
        return stream->sock->write(buf, count);
    }
    ssize_t Writev(const struct iovec *iov, int iovcnt) override {
        if (!header_is_done) HeaderDone();
        ssize_t ret = 0;
        auto iovec = IOVector(iov, iovcnt);
        while (!iovec.empty()) {
            auto tmp = Write(iovec.front().iov_base, iovec.front().iov_len);
            if (tmp < 0) return tmp;
            iovec.extract_front(tmp);
            ret += tmp;
        }
        return ret;
    }
    void ContentLength(size_t len) override {
        resp.content_length(len);
    }
    void KeepAlive(bool alive) override {
        resp.keep_alive(alive);
    }
    int ContentRange(size_t start, size_t end, ssize_t size = -1) override {
        std::string s = "bytes ";
        s += std::to_string(start) + "-" + std::to_string(end) + "/" +
             (size == -1 ? "*" : std::to_string(size));
        return Insert("Content-Range", s);
    }
    HTTPServerRequest* GetRequest() override {
        return m_req;
    }
};

class HTTPServerImpl : public HTTPServer {
public:
    enum class Status {
        ready = 0,
        running = 1,
        stopping = 2,
        failure = 3
    } status = Status::ready;
    uint16_t m_port = 19876;
    photon::join_handle* th = nullptr;
    HTTPServerHandler m_handler;
    uint64_t connection_idx = 0, working_thread_cnt = 0;
    std::map<uint64_t, net::ISocketStream*> connection_map;
    HTTPServerImpl(uint16_t port) : m_port(port) {}
    ~HTTPServerImpl() {
        Stop();
    }

    void run(uint16_t port, net::IPAddr ip) {
        working_thread_cnt++;
        DEFER(working_thread_cnt--);
        if (status != Status::ready) {
            return;
        }
        auto sock = net::new_tcp_socket_server();
        DEFER(delete (sock));
        sock->timeout(1000UL * 1000);
        RETURN_IF_FAILED(sock->setsockopt(IPPROTO_TCP, TCP_NODELAY, 1L));
        RETURN_IF_FAILED(sock->setsockopt(SOL_SOCKET, SO_REUSEPORT, 1));
        RETURN_IF_FAILED(sock->bind(m_port, net::IPAddr {}));
        RETURN_IF_FAILED(sock->listen(1024));
        status = Status::running;
        sock->set_handler({this, &HTTPServerImpl::control_handler});
        sock->start_loop(false);

        while (status != Status::stopping) {
            photon::thread_usleep(1000 * 1000);
        }
        status = Status::ready;
    }

    int control_handler(net::ISocketStream* sock) {

        auto idx = connection_idx++;
        LOG_DEBUG("enter control handler `", idx);
        DEFER(LOG_DEBUG("leave control handler `", idx));
        working_thread_cnt++;
        DEFER(working_thread_cnt--);
        connection_map.insert({idx, sock});
        DEFER(connection_map.erase(idx));
        while (status == Status::running) {
            sock->setsockopt(IPPROTO_TCP, TCP_NODELAY, 1L);
            EaseTCPStream stream(sock);
            HTTPServerRequestImpl req;
            auto ec = req.get_req(stream);
            if (ec) {
                LOG_ERRNO_RETURN(0, -1, ec.message());
            }
            HTTPServerResponseImpl resp(&stream, &req);
            auto ret = m_handler(req, resp);
            switch (ret) {
            case RetType::success:
                    break;
                default:
                    return -1;
            }
        }
        return 0;
    }

    bool Launch() override{
        th = photon::thread_enable_join(photon::thread_create11(
            &HTTPServerImpl::run, this, m_port, net::IPAddr()));
        while (status == Status::ready) photon::thread_usleep(1000);
        return status != Status::failure;
    }

    void Stop() override{
        if (status == Status::running) status = Status::stopping;
        if (th) {
            photon::thread_interrupt((photon::thread*)th, ECANCELED);
            photon::thread_join(th);
            th = nullptr;
        }
        for (auto it : connection_map) {
            it.second->shutdown(ShutdownHow::ReadWrite);
            connection_map.erase(it.first);
        }
        while (working_thread_cnt != 0) {
            photon::thread_usleep(50 * 1000);
        }
    }

    void SetHandler(HTTPServerHandler handler) override {
        m_handler = handler;
    }
};

class FsHandler : public HTTPHandler {
public:
    struct task {
        std::string filename;
        off_t offset;
        size_t count;
    };
    fs::IFileSystem* m_fs;
    estring m_ignore_prefix = "";
    ObjectCache<std::string, fs::IFile*> m_files;
    std::queue<task> m_tasks;
    photon::condition_variable m_cond;
    std::vector<photon::join_handle*> m_workers;
    size_t m_prefetch_size, m_advance_size;
    bool running = true;
    FsHandler(fs::IFileSystem* fs, std::string_view prefix,
              int worker, size_t prefetch_size)
              : m_fs(fs), m_files(KminFileLife),
              m_prefetch_size(prefetch_size) {
        if (!prefix.empty()) m_ignore_prefix = prefix;
        if (!m_ignore_prefix.starts_with("/"))
            m_ignore_prefix = "/" + m_ignore_prefix;
        for (int i = 0; i < worker; i++) {
            m_workers.emplace_back(photon::thread_enable_join(
                                   photon::thread_create11(&FsHandler::download, this)));
        }
        m_advance_size = m_prefetch_size * worker * 2;
    }
    ~FsHandler() {
        running = false;
        m_cond.notify_all();
        for (auto jh : m_workers) photon::thread_join(jh);
    }
    void download() {
        while (running) {
            if (m_tasks.empty())
                m_cond.wait_no_lock();
            else {
                task t = std::move(m_tasks.front());
                m_tasks.pop();
                auto file = m_files.borrow(t.filename, [&]{
                                return m_fs->open(t.filename.c_str(), O_RDONLY);
                            });
                file->fadvise(t.offset, t.count, POSIX_FADV_WILLNEED);
            }
        }
    }
    HTTPServerHandler GetHandler() override {
        return {this, &FsHandler::HandlerImpl};
    }
    void FailedResp(HTTPServerResponse &resp, int result = 404) {
        resp.SetResult(result);
        resp.ContentLength(0);
        resp.KeepAlive(true);
        resp.Done();
    }
    void create_task(off_t offset, off_t &task_tail, off_t req_tail, const estring& filename) {
        if (!m_workers.empty() && (offset + (m_advance_size >> 1) > (size_t)task_tail)) {
            req_tail = std::min(offset + m_advance_size, (size_t)req_tail);
            for (; task_tail < req_tail; task_tail += m_prefetch_size) {
                m_tasks.emplace(task{.filename = filename, .offset = task_tail,
                                     .count = m_prefetch_size});
            }
            m_cond.notify_all();
        }
    }
    RetType HandlerImpl(HTTPServerRequest &req, HTTPServerResponse &resp) {
        auto target = req.GetTarget();
        auto pos = target.find("?");
        std::string query;
        if (pos != std::string_view::npos) {
            query = std::string(target.substr(pos + 1));
            target = target.substr(0, pos);
        }
        estring filename(target);
        if ((!m_ignore_prefix.empty() && (filename.starts_with(m_ignore_prefix))))
            filename = filename.substr(m_ignore_prefix.size() - 1);
        auto file = m_files.borrow(filename, [&]{
            return m_fs->open(filename.c_str(), O_RDONLY);
        });
        if (!file) {
            FailedResp(resp);
            LOG_ERROR_RETURN(0, RetType::failed, "open file ` failed", target);
        }
        if (!query.empty()) file->ioctl(fs::HTTP_URL_PARAM, query.c_str());
        auto range = req.Range();
        struct stat buf;
        if (file->fstat(&buf) < 0) {
            FailedResp(resp);
            LOG_ERROR_RETURN(0, RetType::failed, "stat file ` failed", target);
        }
        auto file_end_pos = buf.st_size - 1;
        if ((range.first < 0) && (range.second < 0)) {
            range.first = 0;
            range.second = file_end_pos;
        }
        if (range.first < 0) {
            range.first = file_end_pos - range.second;
            range.second = file_end_pos;
        }
        if (range.second < 0) {
            range.second = file_end_pos;
        }
        if ((range.second < range.first) || (range.first > file_end_pos)
                                         || (range.second > file_end_pos)) {
            FailedResp(resp, 416);
            LOG_ERROR_RETURN(0, RetType::failed, "invalid request range",
                             target);
        }
        auto req_size = range.second - range.first + 1;
        if (req_size == buf.st_size) resp.SetResult(200); else {
            resp.SetResult(206);
            resp.ContentRange(range.first, range.second, buf.st_size);
        }
        resp.ContentLength(req_size);
        resp.KeepAlive(true);
        char seg_buf[65536];
        off_t offset = range.first;
        off_t task_cursor = offset;
        while (offset <= range.second) {
            create_task(offset, task_cursor, range.second, filename);
            auto seg_size = std::min(size_t(range.second - offset + 1),
                                     sizeof(seg_buf));
            auto ret_r = file->pread(seg_buf, seg_size, offset);
            if (ret_r == 0)
                LOG_ERRNO_RETURN(0, RetType::failed, "pread return unexpected 0");
            if (ret_r < 0)
                LOG_ERROR_RETURN(0, RetType::failed,
                                 "read file ` failed", target);
            auto ret_w = resp.Write(seg_buf, ret_r);
            if (ret_w != ret_r)
                LOG_ERROR_RETURN(0, RetType::failed,
                                 "send body failed, target: `", target, VALUE(ret_w), VALUE(ret_r));
            offset += ret_r;
        }
        return resp.Done();
    }
};

class ReverseProxyHandler : public HTTPHandler {
public:
    Director m_director;
    Modifier m_modifier;
    Client* m_client;
    ReverseProxyHandler(Director cb_Director, Modifier cb_Modifier, Client* client)
        : m_director(cb_Director), m_modifier(cb_Modifier), m_client(client) {}
    HTTPServerHandler GetHandler() override {
        return {this, &ReverseProxyHandler::HandlerImpl};
    }
    RetType HandlerImpl(HTTPServerRequest &req, HTTPServerResponse &resp) {
        auto ret = m_director(req);
        if (ret != RetType::success) return ret;
        estring url;
        url.appends((req.GetProtocol() == Protocol::HTTP) ? http_url_scheme
                                                          : https_url_scheme,
                    req.Find("Host"), req.GetTarget());
        LOG_INFO("new operation: ",VALUE(url));
        auto op = m_client->new_operation(req.GetMethod(), url);
        if (!op) LOG_ERRNO_RETURN(0, RetType::failed, "op is null");
        DEFER(delete op);
        op->follow = 0;
        auto req_kvs = req.GetKVs();
        for (auto &kv : req_kvs) {
            LOG_DEBUG(kv.first, ": ", kv.second);
            if (kv.first != "Host") op->req.insert(kv.first, kv.second);
        }
        op->call();
        resp.SetResult(op->resp.status_code());
        for (auto kv : op->resp) {
            resp.Insert(kv.first, kv.second);
            LOG_DEBUG(kv.first, ": ", kv.second);
        }
        ret = m_modifier(resp);
        if (ret != RetType::success) return ret;
        char seg_buf[65536];
        while (1) {
            auto read_count = op->resp_body->read(seg_buf, sizeof(seg_buf));
            if (read_count < 0) LOG_ERROR_RETURN(0, RetType::failed,
                                                 "read from op->body failed");
            if (read_count == 0) break;
            auto write_count = resp.Write(seg_buf, read_count);
            if (write_count != read_count) LOG_ERROR_RETURN(0, RetType::failed,
                                                  "failed to write response_body");
        }
        return resp.Done();
    }
};

struct HandlerRecord {
    estring prefix;
    HTTPHandler* handler;
};

class MuxHandlerImpl : public MuxHandler {
public:
    std::vector<HandlerRecord> m_record;
    HTTPHandler* m_default_handler = nullptr;
    void AddHandler(std::string_view prefix, HTTPHandler* handler) override {
        m_record.emplace_back(HandlerRecord{prefix, handler});
    }
    void SetDefaultHandler(HTTPHandler *default_handler) override {
        m_default_handler = default_handler;
    }
    HTTPServerHandler GetHandler() override {
        return {this, &MuxHandlerImpl::HandlerImpl};
    }
    RetType defaultHandlerImpl(HTTPServerRequest &req, HTTPServerResponse &resp) {
        resp.SetResult(404);
        resp.ContentLength(0);
        resp.KeepAlive(true);
        return resp.Done();
    }
    RetType HandlerImpl(HTTPServerRequest &req, HTTPServerResponse &resp) {
        estring_view target = req.GetTarget();
        for (auto &rule : m_record) {
            if (target.starts_with(rule.prefix)) {
                auto handler = rule.handler->GetHandler();
                return handler(req, resp);
            }
        }
        if (m_default_handler) {
            auto default_handler = m_default_handler->GetHandler();
            return default_handler(req, resp);
        }
        return defaultHandlerImpl(req, resp);
    }
};

HTTPServer* new_http_server(uint16_t port) {
    return new HTTPServerImpl(port);
}
HTTPHandler* new_fs_handler(fs::IFileSystem* fs,
                            std::string_view prefix, int worker,
                            size_t prefetch_size) {
    return new FsHandler(fs, prefix, worker, prefetch_size);
}
MuxHandler* new_mux_handler() {
    return new MuxHandlerImpl();
}
HTTPHandler* new_reverse_proxy_handler(Director cb_Director,
                    Modifier cb_Modifier, Client* client) {
    return new ReverseProxyHandler(cb_Director, cb_Modifier, client);
}

}
}
