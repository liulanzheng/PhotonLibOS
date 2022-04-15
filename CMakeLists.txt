cmake_minimum_required(VERSION 3.11)

project(
        photon
        VERSION 0.1.0
        LANGUAGES C CXX ASM
)

set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/build/output")
set(CMAKE_EXECUTABLE_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/build/output")
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/CMake")

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fPIC")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fPIC -mcrc32 -msse4.2")

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED on)

if (BUILD_TESTING)
    enable_testing()
    include(CTest)
endif ()

add_subdirectory(third_party)

find_package(aio REQUIRED)
find_package(gflags REQUIRED)
find_package(fuse REQUIRED)
find_package(uring REQUIRED)
find_package(krb5 REQUIRED)
find_package(gsasl REQUIRED)

find_package(ZLIB REQUIRED)
find_package(CURL REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(Boost REQUIRED)

execute_process(COMMAND uname -m OUTPUT_VARIABLE ARCH OUTPUT_STRIP_TRAILING_WHITESPACE)
if (NOT (${ARCH} STREQUAL x86_64) AND NOT (${ARCH} STREQUAL aarch64))
    message(FATAL_ERROR "Unknown CPU architecture ${ARCH}")
endif ()

file(GLOB PHOTON_SRC
        common/*.cpp
        common/checksum/*.cpp
        common/memory-stream/*.cpp
        common/stream-messenger/*.cpp
        fs/*.cpp
        fs/httpfs/*.cpp
        io/*.cpp
        net/*.cpp
        net/http/*.cpp
        net/security-context/*.cpp
        rpc/*.cpp
        thread/*.cpp
        thread/switch_context_${ARCH}.s'
        )
add_library(photon STATIC ${PHOTON_SRC})
target_include_directories(photon PUBLIC third_party/liburing/src/include include .)
target_compile_options(photon PUBLIC
        -D_FILE_OFFSET_BITS=64
        -DFUSE_USE_VERSION=29)



include(CMake/pack.cmake)