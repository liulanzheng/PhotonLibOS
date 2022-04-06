#pragma once

#include <cinttypes>
#include <cstddef>

#include "photon/common/callback.h"
#include "photon/common/object.h"
#include "photon/common/stream.h"

namespace FileSystem {
struct IFile;
using FileOpenCallback = Delegate<void, const char*, IFile*>;
class AuthManager : public Object {
public:
    virtual int new_conn(IStream* conn) = 0;
    virtual bool conn_verify(IStream* conn, const char* key, const char*) = 0;
    virtual int close_conn(IStream* conn) = 0;
    virtual int put(const char* key, const char* auth, bool verify = false) = 0;
    virtual int get(const char* key, char* auth, size_t size) = 0;
    virtual int drop(const char* key, const char* auth) = 0;
    virtual bool verify(const char* key, const char* auth) = 0;
    virtual FileOpenCallback opencb() = 0;
};

}  // namespace FileSystem