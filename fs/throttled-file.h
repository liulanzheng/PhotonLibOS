#pragma once
#include <cinttypes>

namespace FileSystem
{
    struct ThrottleLimits
    {
        uint32_t struct_size = sizeof(ThrottleLimits);

        // the time window (in seconds) of I/O events to analyse, minimally 1
        uint32_t time_window = 1;

        struct UpperLimits
        {
            uint32_t concurent_ops = 0, IOPS = 0, throughput = 0, block_size = 0;
        };

        // limits for read, write, and either read or write
        UpperLimits R, W, RW;
    };

    class IFileSystem;
    class IFile;

    extern "C" IFile *new_throttled_file(IFile *file,
                                         const ThrottleLimits &limits,
                                         bool ownership = false);

    extern "C" IFileSystem *new_throttled_fs(IFileSystem *fs,
                                             const ThrottleLimits &limits,
                                             bool ownership = false);
}
