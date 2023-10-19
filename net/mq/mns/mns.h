#include <string>
#include <photon/common/object.h>

namespace photon {
namespace net {

namespace http {
class Client;
}

namespace mq {
namespace mns {

class Message : public Object {
public:
    std::string body;
    std::string receipt_handle;
    uint64_t next_visible_time;
};
class MQ : public Object {
public:
    MQ(std::string &endpoint, std::string &akid, std::string &aksecret);
    ~MQ();

    int send_message(const std::string &queue_name, const std::string &message_body, int retry = 3);
    int receive_message(const std::string &queue_name, Message &message);
    int resend_message(const std::string &queue_name, Message &message);
    int delete_message(const std::string &queue_name, Message &message, int retry = 3);

private:
    std::string endpoint;
    std::string akid;
    std::string aksecret;
    photon::net::http::Client *m_client;
    int do_send_message(const std::string &queue_name, const std::string &message_body);
    int do_delete_message(const std::string &queue_name, Message &message);
};

MQ *new_mns_mq(std::string &endpoint, std::string &akid, std::string &aksecret);

} // namespace mns
} // namespace mq
} // namespace net
} // namespace photon
