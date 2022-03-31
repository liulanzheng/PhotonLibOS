#include "fs/httpfs/auth_manager.h"

#include <fcntl.h>

#include <cassert>
#include <memory>
#include <unordered_map>
#include <unordered_set>

#include "common/alog.h"
#include "common/string-keyed.h"
#include "common/string_view.h"
#include "httpfs.h"

namespace FileSystem {
class HttpParamSignatureManager : public AuthManager {
protected:
    std::unordered_map<uint64_t, std::unordered_set<std::string>> allow_list;
    unordered_map_string_key<std::string> store;
    std::unique_ptr<IFileSystem> httpfs;

public:
    HttpParamSignatureManager(bool https, uint64_t timeout, IFileSystem* httpfs)
        : httpfs(httpfs) {}

    virtual int put(const char* key, const char* auth,
                    bool need_verify) override {
        while (*key == '/') key++;
        if (need_verify) {
            if (!verify(key, auth)) {
                return -1;
            }
        }
        store[key] = auth;
        return 0;
    }

    virtual int get(const char* key, char* auth, size_t size) override {
        while (*key == '/') key++;
        auto it = store.find(key);
        if (it == store.end()) return -1;
        if (it->second.empty()) return -1;
        strncpy(auth, it->second.c_str(), size);
        return 0;
    }

    virtual int drop(const char* key, const char* auth) override {
        while (*key == '/') key++;
        store.erase(key);
        return 0;
    }

    virtual bool verify(const char* key, const char* auth) override {
        std::unique_ptr<IFile> file(httpfs->open(key, O_RDONLY));
        file->ioctl(FileSystem::HTTP_URL_PARAM, auth);
        if (file == nullptr) return false;
        struct stat buf;
        auto ret = file->fstat(&buf);
        return ret == 0;
    }

    virtual int new_conn(IStream* stream) override {
        uint64_t skey = (uint64_t)stream;
        assert(allow_list[skey].empty());
        (void)(skey);
        return 0;
    }

    virtual int close_conn(IStream* stream) override {
        uint64_t skey = (uint64_t)stream;
        allow_list.erase(skey);
        return 0;
    }

    virtual bool conn_verify(IStream* stream, const char* key,
                             const char* auth) override {
        uint64_t skey = (uint64_t)stream;
        auto& al = allow_list[skey];
        if (al.find(key) != al.end())  // already checked
            return true;
        return put(key, auth, true) == 0;
    }

    void set_auth_for_fn(const char* key, IFile* fn) {
        while (*key == '/') key++;
        auto it = store.find(key);
        if (it != store.end()) {
            fn->ioctl(HTTP_URL_PARAM, it->second.c_str());
        }
    }

    virtual FileOpenCallback opencb() override {
        return {this, &HttpParamSignatureManager::set_auth_for_fn};
    }
};

AuthManager* new_http_param_signature_manager(bool secure, uint64_t timeout,
                                              IFileSystem* httpfs) {
    if (!httpfs) {
        httpfs = new_httpfs(secure, timeout, timeout);
    }
    return new HttpParamSignatureManager(secure, timeout, httpfs);
}

}  // namespace FileSystem