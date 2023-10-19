#include "mns.h"
#include <sys/fcntl.h>
#include <photon/photon.h>
#include <photon/common/alog-stdstring.h>
#include <photon/thread/thread.h>
#include <sys/types.h>
#include <photon/net/http/client.h>
#include <photon/net/http/url.h>
#include <photon/net/utils.h>
#include <openssl/hmac.h>
#include <openssl/evp.h>
#include <openssl/bio.h>
#include <openssl/buffer.h>
#include <sstream>
#include <tinyxml2.h>
#include <photon/common/estring.h>
#include <photon/net/http/client.h>

namespace photon {
namespace net {
namespace mq {
namespace mns {

using namespace std;

class XMLHandle : public tinyxml2::XMLHandle {
public:
    template <typename T>
    XMLHandle(const T &x) : tinyxml2::XMLHandle(x) {
    }
    operator bool() {
        return ToNode();
    }
    const char *GetText() {
        auto element = ToElement();
        return element ? element->GetText() : nullptr;
    }
    XMLHandle operator[](const char *s) {
        return {FirstChildElement(s)};
    }
    XMLHandle NextSibling(const char *s) {
        return {NextSiblingElement(s)};
    }
};

class XMLReader : public XMLHandle {
public:
    XMLReader(const char *buf, int64_t len) : XMLHandle(nullptr) {
        _xmldoc.Parse(buf, len);
        static_assert(sizeof(XMLHandle) == sizeof(void *), "");
        *(void **)this = &_xmldoc;
    }

protected:
    tinyxml2::XMLDocument _xmldoc;
};

using HTTP_OP = photon::net::http::Client::OperationOnStack<64 * 1024 - 1>;

const int MAX_BODY_SIZE = 64 * 1024;
const std::string CONTENT_TYPE_XML = "text/xml;charset=UTF-8";

std::string get_date_time_str() {
    time_t now = time(NULL);
    char timeBuffer[80];

    struct tm utc_tm;
    struct tm *r = gmtime_r(&now, &utc_tm);
    if (r == NULL)
        return "";

    strftime(timeBuffer, sizeof(timeBuffer), "%a, %d %b %Y %H:%M:%S GMT", &utc_tm);

    return timeBuffer;
}

std::string sign(const char *data, const std::string &akid, const std::string &aksecret) {
    char sha1sum[EVP_MAX_MD_SIZE];
    unsigned int sha1len;
    HMAC(EVP_sha1(), aksecret.c_str(), aksecret.length(), (const unsigned char *)data, strlen(data),
         (unsigned char *)sha1sum, &sha1len);
    std::string base64_str;
    std::string_view a(sha1sum, sha1len);
    photon::net::Base64Encode(a, base64_str);
    return "MNS " + akid + ":" + base64_str;
}

MQ::MQ(std::string &endpoint, std::string &akid, std::string &aksecret)
    : endpoint(endpoint), akid(akid), aksecret(aksecret) {
    m_client = photon::net::http::new_http_client();
}
MQ::~MQ() {
    delete m_client;
}

int MQ::do_send_message(const std::string &queue_name, const std::string &message_body) {
    std::string path("/queues/" + queue_name + "/messages");
    std::string url(endpoint + path);
    HTTP_OP op(m_client, photon::net::http::Verb::POST, url);
    std::string date = get_date_time_str();
    std::string sign_content =
        "POST\n\n" + CONTENT_TYPE_XML + "\n" + date + "\n" + "x-mns-version:2015-06-06\n" + path;
    std::string authorization = sign(sign_content.c_str(), akid, aksecret);
    op.req.headers.insert("Authorization", authorization);
    op.req.headers.insert("Date", date);
    op.req.headers.insert("Content-Type", CONTENT_TYPE_XML);
    op.req.headers.insert("x-mns-version", "2015-06-06");
    std::string header =
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?><Message xmlns=\"http://mns.aliyuncs.com/doc/v1/\"><MessageBody>";
    std::string trailer = "</MessageBody></Message>";
    std::string msg_body = header + message_body + trailer;
    op.req.headers.content_length(msg_body.size());
    auto writer = [&](photon::net::http::Request *req) {
        return req->write(msg_body.data(), msg_body.size());
    };
    op.body_writer = writer;
    if (op.call() < 0) {
        LOG_ERRNO_RETURN(0, -1, "failed to perform http request");
    }
    auto len = op.resp.body_size();
    std::string body;
    body.resize(len);
    auto rc = op.resp.read((void *)body.data(), len);
    if (rc != len) {
        LOG_ERROR_RETURN(0, -1, "failed to read http body");
    }
    if (op.status_code / 100 != 2) {
        LOG_ERROR(VALUE(body));
        LOG_ERROR_RETURN(0, -1, "failed to do http post ", VALUE(url), VALUE(op.status_code));
    }

    return 0;
}

int MQ::send_message(const std::string &queue_name, const std::string &message_body, int retry) {
    for (int i = 0; i < retry; i++) {
        auto ret = do_send_message(queue_name, message_body);
        if (ret == 0)
            return 0;
        photon::thread_usleep(20 * 1000);
    }
    LOG_ERROR("failed to send message after retry");
    return -1;
}

int MQ::receive_message(const std::string &queue_name, Message &message) {
    std::string path("/queues/" + queue_name + "/messages?waitseconds=1");
    std::string url(endpoint + path);
    HTTP_OP op(m_client, photon::net::http::Verb::GET, url);
    std::string date = get_date_time_str();
    std::string sign_content = "GET\n\n\n" + date + "\nx-mns-version:2015-06-06\n" + path;
    std::string authorization = sign(sign_content.c_str(), akid, aksecret);
    op.req.headers.insert("Authorization", authorization);
    op.req.headers.insert("Date", date);
    op.req.headers.insert("x-mns-version", "2015-06-06");
    if (op.call() < 0) {
        LOG_ERRNO_RETURN(0, -1, "failed to perform http request");
    }
    char http_body_buf[MAX_BODY_SIZE];
    auto len = op.resp.body_size();
    auto rc = op.resp.read(http_body_buf, len);
    if (rc != len) {
        LOG_ERROR_RETURN(0, -1, "failed to read http body");
    }
    if (op.status_code == 404) {
        LOG_DEBUG("empty message queue, name: ", queue_name);
        return -1;
    }
    if (op.status_code / 100 != 2) {
        LOG_ERROR(VALUE(http_body_buf));
        LOG_ERROR_RETURN(0, -1, "failed to do http get ", VALUE(url), VALUE(op.status_code));
    }
    XMLReader reader(http_body_buf, len);
    auto message_xml = reader["Message"];
    message.receipt_handle = message_xml["ReceiptHandle"].GetText();
    message.body = message_xml["MessageBody"].GetText();
    std::string nvt_str;
    nvt_str = message_xml["NextVisibleTime"].GetText();
    message.next_visible_time = estring_view(nvt_str).to_uint64();
    return 0;
}

int MQ::do_delete_message(const std::string &queue_name, Message &message) {
    std::string path("/queues/" + queue_name + "/messages?ReceiptHandle=" + message.receipt_handle);
    std::string url(endpoint + path);
    HTTP_OP op(m_client, photon::net::http::Verb::DELETE, url);
    std::string date = get_date_time_str();
    std::string sign_content = "DELETE\n\n\n" + date + "\nx-mns-version:2015-06-06\n" + path;
    std::string authorization = sign(sign_content.c_str(), akid, aksecret);
    op.req.headers.insert("Authorization", authorization);
    op.req.headers.insert("Date", date);
    op.req.headers.insert("x-mns-version", "2015-06-06");
    op.req.keep_alive(true);
    if (op.call() < 0) {
        LOG_ERRNO_RETURN(0, -1, "failed to perform http request");
    }
    if (op.status_code != 204) {
        auto len = op.resp.body_size();
        std::string body;
        body.resize(len);
        auto rc = op.resp.read((void *)body.data(), len);
        if (rc != len) {
            LOG_ERROR_RETURN(0, -1, "failed to read http body");
        }
        LOG_ERROR(VALUE(body));
        LOG_ERROR_RETURN(0, -1, "failed to do http delete ", VALUE(url), VALUE(op.status_code));
    }
    return 0;
}

int MQ::delete_message(const std::string &queue_name, Message &message, int retry) {
    for (int i = 0; i < retry; i++) {
        auto ret = do_delete_message(queue_name, message);
        if (ret == 0)
            return 0;
        photon::thread_usleep(20 * 1000);
    }
    LOG_ERROR("failed to delete message after retry");
    return -1;
}

int MQ::resend_message(const std::string &queue_name, Message &message) {
    auto next_vt = message.next_visible_time * 1000;
    if (next_vt < photon::now + 1 * 1024 * 1024) {
        // no need to resend_message;
        LOG_INFO("no need to resend_message");
        return 0;
    }
    if (send_message(queue_name, message.body) < 0) {
        LOG_ERROR_RETURN(0, -1, "failed to resend_message message");
    }
    if (delete_message(queue_name, message) < 0) {
        LOG_WARN("failed to delete resended message");
    }
    return 0;
}

MQ *new_mns_mq(std::string &endpoint, std::string &akid, std::string &aksecret) {
    MQ *ret = new MQ(endpoint, akid, aksecret);
    return ret;
}

} // namespace mns
} // namespace mq
} // namespace net
} // namespace photon