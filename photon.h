#pragma once

#include <inttypes.h>

namespace photon {

#define SHIFT(n) (1 << n)

const uint64_t INIT_EVENT_EPOLL = SHIFT(0);
const uint64_t INIT_EVENT_IOURING = SHIFT(1);
const uint64_t INIT_EVENT_SELECT = SHIFT(2);
const uint64_t INIT_EVENT_KQUEUE = SHIFT(3);
const uint64_t INIT_EVENT_IOCP = SHIFT(4);
const uint64_t INIT_EVENT_SIGNALFD = SHIFT(10);
const uint64_t INIT_EVENT_DEFAULT = INIT_EVENT_EPOLL | INIT_EVENT_SIGNALFD;

const uint64_t INIT_IO_LIBAIO = SHIFT(0);
const uint64_t INIT_IO_LIBCURL = SHIFT(1);
const uint64_t INIT_IO_SOCKET_ZEROCOPY = SHIFT(2);
const uint64_t INIT_IO_SOCKET_EDGE_TRIGGER = SHIFT(3);
const uint64_t INIT_IO_EXPORTFS = SHIFT(10);
const uint64_t INIT_IO_DEFAULT = INIT_IO_LIBAIO | INIT_IO_LIBCURL;

#undef SHIFT

/**
 * @brief Initialize the main photon thread and ancillary threads by flags.
 *        Ancillary threads will be running in background.
 * @return 0 for success
 */
int init(uint64_t event_engine = INIT_EVENT_DEFAULT,
         uint64_t io_engine = INIT_IO_DEFAULT,
         uint64_t misc = 0);

/**
 * @brief Destroy/join ancillary threads, and finish the main thread.
 */
int fini();

}