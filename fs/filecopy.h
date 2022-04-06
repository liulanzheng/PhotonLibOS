#include <cstdio>

namespace FileSystem {
class IFile;

// copy file, return errno when failed
// or return file size
ssize_t filecopy(IFile* infile, IFile* outfile, size_t bs = 65536, int retry_limit=5);

}  // namespace FileSystem