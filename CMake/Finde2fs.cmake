find_path(LIBE2FS_INCLUDE_DIRS NAMES ext2fs.h PATHS /usr/include/ext2fs NO_DEFAULT_PATH)

find_library(LIBE2FS_LIBRARIES ext2fs)

find_package_handle_standard_args(e2fs DEFAULT_MSG LIBE2FS_LIBRARIES LIBE2FS_INCLUDE_DIRS)

mark_as_advanced(LIBE2FS_INCLUDE_DIRS LIBE2FS_LIBRARIES)