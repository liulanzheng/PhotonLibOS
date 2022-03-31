#pragma once
#include <inttypes.h>
#include <vector>
#include "common/object.h"
#include "common/callback.h"
#include "common/string_view.h"
#include "common/iovector.h"
#include "verb.h"
namespace FileSystem {
    class IFileSystem;
}
namespace Net {

namespace HTTP {

using HeaderLists = std::vector<std::pair<std::string_view, std::string_view>>;
enum class Protocol {
    HTTP = 0,
    HTTPS
};

class HTTPServerRequest {
public:
    virtual void SetProtocol(Protocol p) = 0;
    virtual Protocol GetProtocol() = 0;
    virtual Verb GetMethod() = 0;
    virtual void SetMethod(Verb v) = 0;
    virtual std::string_view GetOriginHost() = 0;
    virtual std::string_view GetTarget() = 0;
    virtual void SetTarget(std::string_view target) = 0;
    virtual std::string_view Find(std::string_view key) = 0;
    virtual int Insert(std::string_view key, std::string_view value) = 0;
    virtual int Erase(std::string_view key) = 0;
    virtual HeaderLists GetKVs() = 0;
    virtual std::string_view Body() = 0;
    virtual size_t ContentLength() = 0;
    virtual std::pair<ssize_t, ssize_t> Range() = 0;
};
enum class RetType {
    failed = 0,
    success = 1,
    connection_close = 2
};
class HTTPServerResponse {
public:
    virtual RetType HeaderDone() = 0;
    virtual RetType Done() = 0;
    virtual void SetResult(int status_code) = 0;
    virtual int GetResult() = 0;
    virtual std::string_view Find(std::string_view key) = 0;
    virtual int Insert(std::string_view key, std::string_view value) = 0;
    virtual int Erase(std::string_view key) = 0;
    virtual HeaderLists GetKVs() = 0;
    virtual ssize_t Write(void* buf, size_t count) = 0;
    virtual ssize_t Writev(const struct iovec *iov, int iovcnt) = 0;
    virtual void ContentLength(size_t len) = 0;
    virtual void KeepAlive(bool alive) = 0;
    virtual int ContentRange(size_t start, size_t end, ssize_t size = -1) = 0;
    virtual HTTPServerRequest* GetRequest() = 0;

};

using HTTPServerHandler = Delegate<RetType, HTTPServerRequest&, HTTPServerResponse&>;

class HTTPServer : public Object {
public:
    virtual bool Launch() = 0;
    virtual void Stop() = 0;
    virtual void SetHandler(HTTPServerHandler handler) = 0;
};

class HTTPHandler : public Object {
public:
    virtual HTTPServerHandler GetHandler() = 0;
};

class MuxHandler : public HTTPHandler {
public:
    //should matching prefix in add_handler calling order
    virtual void AddHandler(std::string_view prefix, HTTPHandler *handler) = 0;
    /* use default_handler when matching prefix failed
       if no default_handler was set, return 404*/
    virtual void SetDefaultHandler(HTTPHandler *default_handler) = 0;
};
class Client;
//modify body is not allowed
using Director = Delegate<RetType, HTTPServerRequest&>;
//modify body is not allowed
using Modifier = Delegate<RetType, HTTPServerResponse&>;

//handler will ignore @ignore_prefix in target prefix
HTTPHandler* new_fs_handler(FileSystem::IFileSystem* fs,
                            std::string_view ignore_prefix = "", int worker = 0,
                            size_t prefetch_size = 16 * 1024 * 1024UL);
HTTPHandler* new_reverse_proxy_handler(Director cb_Director,
                                       Modifier cb_Modifier, Client* client);

// maybe imply new_static_file later
// HTTPHandler new_static_file(std::string root);

MuxHandler* new_mux_handler();
HTTPServer* new_http_server(uint16_t port);

} // end of namespace HTTP

} // end of namespace Net