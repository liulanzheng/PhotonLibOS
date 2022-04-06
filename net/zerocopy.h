#pragma once

#include <cstdint>
#include "photon/common/ring.h"
#include "photon/thread/thread.h"

namespace Net {

class ZerocopyEventEntry {
public:
    explicit ZerocopyEventEntry(int fd);

    ~ZerocopyEventEntry();

    // counter is a scalar value that equals to (number of sendmsg calls - 1),
    // it wraps after UINT_MAX.
    int zerocopy_wait(uint32_t counter, uint64_t timeout);

    // Read counter from epoll error msg, and wake up the corresponding threads
    void handle_events();

    int get_fd() const {
        return m_fd;
    }

protected:
    struct Entry {
        uint32_t counter;
        photon::thread* th;
    };
    int m_fd;
    RingQueue<Entry> m_queue;
};

int zerocopy_init();

int zerocopy_fini();

/* Check if kernel version satisfies and thus zerocopy feature should be enabled */
bool zerocopy_available();

}