#pragma once
#include <inttypes.h>

namespace FileSystem
{
    class IFile;
    class IFileSystem;
    
    // create a view of sub tree of an underlay fs
    IFileSystem* new_subfs(IFileSystem* underlayfs, const char* base_path, bool ownership);
    
    // create a view of part of an underlay file
    // only for pread/v and pwrite/v
    IFileSystem* new_subfile(IFile* underlay_file, uint64_t offset, uint64_t length, bool ownership);
}
