#pragma once

#define _FILE_OFFSET_BITS 64
#define FUSE_USE_VERSION 29

#include <fuse.h>
#include <photon/fs/filesystem.h>

namespace photon {
namespace fs {

int fuser_go(fs::IFileSystem* fs, int argc, char* argv[]);

int fuser_go_exportfs(fs::IFileSystem* fs, int argc, char* argv[]);

void set_fuse_fs(fs::IFileSystem* fs);

fuse_operations* get_fuse_xmp_oper();

}
}
