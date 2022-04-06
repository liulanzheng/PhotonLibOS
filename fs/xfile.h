#pragma once
#include <cinttypes>

namespace FileSystem
{
    class IFile;

    // create a linear file that is composed sequencially by `n` `files` of
    // distinct sizes, optionally obtain their ownership (resursive destruction)
    // the sizes of the files will obtained by their fstat() function
    IFile* new_linear_file(IFile** files, uint64_t n, bool ownership = false);

    // create a linear file that is compsoed sequencially by `n` `files` of
    // the same `unit_size`, optionally obtain their ownership (resursive destruction)
    IFile* new_fixed_size_linear_file(uint64_t unit_size, IFile** files, uint64_t n, bool ownership = false);

    // create a strip file that is composed by stripping `n` `files` of the same size
    // `stripe_size` must be power of 2
    IFile* new_stripe_file(uint64_t stripe_size, IFile** files, uint64_t n, bool ownership = false);
}
