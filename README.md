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
* A bunch of useful tools: io-vector manipulation, resource pool, object cache, mem allocator, callback delegator, pre-compiled logging, ring buffer, etc.

While Photon has already encapsulated many mature OS functionalities, it remains keen to the latest kernel features,
and prepared to wrap them into the framework. It is a real killer in the low level programing field.

## Build

### alimake

Ensure your alimake version is greater than 2.0.9-20220117. If alimake is upgraded, first remove ~/.dep_create_cache/ and .dep_create/ to make a clean environment.

```bash
./build.sh -b release -j 8 -c
```

### cmake

```bash
yum config-manager --set-enabled PowerTools
yum insall epel-release
yum install openssl-devel libcurl-devel boost-devel libaio-devel fuse-devel gflags-devel libgsasl-devel krb5-devel
mkdir build && cd build
cmake ..
make -j
```

## Contributing
Welcome to contribute!

## Licenses
Photon is released under the Apache License, Version 2.0.