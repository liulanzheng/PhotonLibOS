# Photon

## Overview

Photon is a high-efficiency app framework, based on a set of carefully selected C++ libs.

Our goal is to make programs run as fast and lightweight as the photon particle, which exactly is the name came from.

## Key Features
* Coroutine lib (support multi-core)
* Async event engine, natively integrated into coroutine scheduling (support epoll or io_uring)
* Multiple I/O engines: psync, posix_aio, libaio, io_uring
* Multiple socket implementations: tcp (level-trigger/edge-trigger), unix-domain, zero-copy, libcurl, TLS support, etc.
* A full functionality HTTP client/server (even faster than Nginx)
* A simple RPC client/server
* A POSIX-like filesystem abstraction and some implementations: local fs, http fs, fuse fs, etc.
* A bunch of useful tools: io-vector manipulation, resource pool, object cache, mem allocator, callback delegator,
  pre-compiled logging, ring buffer, etc.

While Photon has already encapsulated many mature OS functionalities, it remains keen to the latest kernel features,
and prepared to wrap them into the framework. It is a real killer in the low level programing field.

## Build

### Install dependencies

#### CentOS 8.5
```shell
dnf install gcc-c++ epel-release cmake
dnf install openssl-devel libcurl-devel libaio-devel fuse-devel libgsasl-devel krb5-devel
```

#### Ubuntu 20.04
```shell
apt install cmake
apt install libssl-dev libcurl4-openssl-dev libaio-dev libfuse-dev libgsasl7-dev libkrb5-dev
```

### Build from source
```shell
mkdir build && cd build
cmake ..
make -j
```
All the libs and executables will be saved in `build/output`.

### Testing
```shell
# CentOS
dnf config-manager --set-enabled PowerTools
dnf install gtest-devel gmock-devel gflags-devel
# Ubuntu
apt install libgtest-dev libgmock-dev libgflags-dev

cmake -D BUILD_TESTING=1 ..
make -j
ctest
```

## Contributing
Welcome to contribute!

## Licenses
Photon is released under the Apache License, Version 2.0.