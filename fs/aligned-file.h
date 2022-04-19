#pragma once
#include <cinttypes>

struct AlignedAlloc;
struct IOAlloc;

namespace photon {
namespace fs
{
    class IFile;
    class IFileSystem;


    // create an adaptor to freely access a file that requires aligned access
    // alignment must be 2^n
    // only pread() and pwrite() are supported
    IFile* new_aligned_file_adaptor(IFile* file, uint32_t alignment,
                                    bool align_memory, bool ownership = false,
                                    IOAlloc* allocator = nullptr);

    IFileSystem* new_aligned_fs_adaptor(IFileSystem* fs, uint32_t alignment,
                                        bool align_memory, bool ownership,
                                        IOAlloc* allocator = nullptr);

}
}
