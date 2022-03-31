#include "filecopy.h"

#include <stddef.h>
#include <sys/stat.h>

#include "common/alog.h"
#include "filesystem.h"

namespace FileSystem {

static constexpr size_t ALIGNMENT = 4096;

ssize_t filecopy(IFile* infile, IFile* outfile, size_t bs, int retry_limit) {
    if (bs == 0) LOG_ERROR_RETURN(EINVAL, -1, "bs should not be 0");
    void* buff = nullptr;
    ;
    // buffer allocate, with 4K alignment
    ::posix_memalign(&buff, ALIGNMENT, bs);
    if (buff == nullptr)
        LOG_ERROR_RETURN(ENOMEM, -1, "Fail to allocate buffer with ",
                         VALUE(bs));
    DEFER(free(buff));
    off_t offset = 0;
    ssize_t count = bs;
    while (count == (ssize_t)bs) {
        int retry = retry_limit;
    again_read:
        if (!(retry--))
            LOG_ERROR_RETURN(EIO, -1, "Fail to read at ", VALUE(offset),
                             VALUE(count));
        auto rlen = infile->pread(buff, bs, offset);
        if (rlen < 0) {
            LOG_DEBUG("Fail to read at ", VALUE(offset), VALUE(count),
                      " retry...");
            goto again_read;
        }
        retry = retry_limit;
    again_write:
        if (!(retry--))
            LOG_ERROR_RETURN(EIO, -1, "Fail to write at ", VALUE(offset),
                             VALUE(count));
        // cause it might write into file with O_DIRECT
        // keep write length as bs
        auto wlen = outfile->pwrite(buff, bs, offset);
        // but once write lenth larger than read length treats as OK
        if (wlen < rlen) {
            LOG_DEBUG("Fail to write at ", VALUE(offset), VALUE(count),
                      " retry...");
            goto again_write;
        }
        count = rlen;
        offset += count;
    }
    // truncate after write, for O_DIRECT
    outfile->ftruncate(offset);
    return offset;
}

}  // namespace FileSystem
