cmake_minimum_required(VERSION 3.12)

project(
        photon
        VERSION 0.1.0
        LANGUAGES C CXX ASM
)

# Compiler options
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED on)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -mcrc32 -msse4.2 -fPIC")

# CMake dirs
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/build/output)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/build/output)
set(CMAKE_EXECUTABLE_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/build/output)
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${CMAKE_SOURCE_DIR}/CMake)

include(CMake/pack.cmake)
add_subdirectory(third_party)

# Find packages either from cmake-modules or external packages
find_package(ZLIB REQUIRED)
find_package(CURL REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(Boost REQUIRED COMPONENTS system)

find_package(aio REQUIRED)
find_package(fuse REQUIRED)
find_package(krb5 REQUIRED)
find_package(gsasl REQUIRED)
find_package(uring REQUIRED)

# Get CPU arch
execute_process(COMMAND uname -m OUTPUT_VARIABLE ARCH OUTPUT_STRIP_TRAILING_WHITESPACE)
if (NOT (${ARCH} STREQUAL x86_64) AND NOT (${ARCH} STREQUAL aarch64))
    message(FATAL_ERROR "Unknown CPU architecture ${ARCH}")
endif ()

# Compile objects
file(
        GLOB PHOTON_SRC
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
        thread/switch_context_${ARCH}.s
)
add_library(photon_obj OBJECT ${PHOTON_SRC})
target_include_directories(photon_obj PUBLIC include)
target_compile_definitions(photon_obj PRIVATE _FILE_OFFSET_BITS=64 FUSE_USE_VERSION=29)
target_compile_options(photon_obj PRIVATE -Wno-deprecated-declarations)

# Make virtual interface for third-party libs
add_library(third_party INTERFACE)
target_link_libraries(third_party INTERFACE
        ZLIB::ZLIB
        CURL::libcurl
        Boost::system
        OpenSSL::SSL
        OpenSSL::Crypto

        ${AIO_LIBRARIES}
        ${FUSE_LIBRARIES}
        ${KRB5_LIBRARIES}
        ${GSASL_LIBRARIES}
        ${URING_LIBRARIES}
)

# Link shared lib
add_library(photon_shared SHARED $<TARGET_OBJECTS:photon_obj>)
set_target_properties(photon_shared PROPERTIES OUTPUT_NAME photon)
target_link_libraries(
        photon_shared
        third_party
)

# Archive static lib
add_library(photon_static STATIC $<TARGET_OBJECTS:photon_obj>)
set_target_properties(photon_static PROPERTIES OUTPUT_NAME photon)

# Build test cases
if (BUILD_TESTING)
    enable_testing()
    include(CTest)

    find_package(GTest REQUIRED)
    find_package(gmock REQUIRED)
    find_package(gflags REQUIRED)

    set(testing_libs ${GTEST_BOTH_LIBRARIES} ${GMOCK_LIBRARIES} ${GFLAGS_LIBRARIES} -lpthread)

    add_subdirectory(io/test)
    add_subdirectory(thread/test)
    add_subdirectory(rpc/test)
    add_subdirectory(net/test)
    add_subdirectory(fs/test)
endif ()
