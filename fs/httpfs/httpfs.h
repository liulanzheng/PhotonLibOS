#pragma once

#include <sys/ioctl.h>

#include <cinttypes>
#include <string>
#include <utility>

#include "photon/common/callback.h"
#include "photon/fs/filesystem.h"

namespace FileSystem {
enum HTTPFileFlags {
    HTTP_HEADER = 0xF01,  // (const char*, const char*) ... for header
    HTTP_URL_PARAM =
        0xF02,  // const char* ... for url param (concat by '?' in url)
};

using FileOpenCallback = Delegate<void, const char*, IFile*>;

/**
 * @brief create httpfs object
 *
 * @param default_https once fn does not contains a protocol part, assume it is
 * a https url
 * @param conn_timeout timeout for http connection. -1 as forever.
 * @param stat_expire stat info will store till expired, then refresh.
 * @param open_cb callback when httpfile created, as AOP to set params just
 * before open function returned
 * @return IFileSystem* created httpfs
 */
IFileSystem* new_httpfs(bool default_https = false,
                        uint64_t conn_timeout = -1UL,
                        uint64_t stat_expire = -1UL,
                        FileOpenCallback open_cb = {});
/**
 * @brief create http file object
 *
 * @param url URL for file
 * @param httpfs set associated httpfs, set `nullptr` to create without httpfs,
 * means self-holding cURL object on demand
 * @param conn_timeout timeout for http connection, -1 as forever.
 * @param stat_expire stat info will store till expired, then refresh.
 * @param open_cb callback when httpfile created, as AOP to set params just
 * before open function returned
 * @return IFile* created httpfile
 */
IFile* new_httpfile(const char* url, IFileSystem* httpfs = nullptr,
                    uint64_t conn_timeout = -1UL, uint64_t stat_expire = -1UL,
                    FileOpenCallback open_cb = {});

IFileSystem* new_httpfs_v2(bool default_https = false,
                           uint64_t conn_timeout = -1UL,
                           uint64_t stat_expire = -1UL);

IFile* new_httpfile_v2(const char* url, IFileSystem* httpfs = nullptr,
                       uint64_t conn_timeout = -1UL,
                       uint64_t stat_expire = -1UL);
}  // namespace FileSystem