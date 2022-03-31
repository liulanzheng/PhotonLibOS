#pragma once
#define _FILE_OFFSET_BITS 64
#define FUSE_USE_VERSION 29
#include <fuse.h>

#include "filesystem.h"

int fuser_go(FileSystem::IFileSystem* fs, int argc, char* argv[]);

int fuser_go_exportfs(FileSystem::IFileSystem* fs, int argc, char* argv[]);

void set_fuse_fs(FileSystem::IFileSystem* fs);

fuse_operations* get_fuse_xmp_oper();