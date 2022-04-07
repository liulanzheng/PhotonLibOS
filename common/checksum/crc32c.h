/*
 * Alibaba Group Inc. 2019 Copyrights
 */

#include <cstdint>
#include <string>

#pragma once

namespace FileSystem {

namespace crc32 {

uint32_t crc32c(const void *data, size_t nbytes);
uint32_t crc32c(const std::string &text);

uint32_t crc32c_extend(const void *data, size_t nbytes, uint32_t crc);
uint32_t crc32c_extend(const std::string &text, uint32_t crc);

namespace testing {
uint32_t crc32c_slow(const void *data, size_t nbytes, uint32_t crc);
uint32_t crc32c_fast(const void *data, size_t nbytes, uint32_t crc);
} // namespace testing

} // namespace crc32

} // namespace FileSystem
