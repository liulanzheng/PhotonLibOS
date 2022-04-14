#pragma once

#include <condition_variable>
#include <mutex>
#include <thread>

namespace Executor {
struct StdCond {
    std::condition_variable cond;
    std::mutex &mtx;
    StdCond(std::mutex &mtx) : mtx(mtx) {}
    template <typename Lock>
    void wait(Lock &lock) {
        cond.wait(lock);
    }
    template <typename Lock>
    bool wait_for(Lock &lock, int64_t timeout) {
        if (timeout < 0) {
            cond.wait(lock);
            return true;
        }
        return cond.wait_for(lock, std::chrono::microseconds(timeout)) ==
               std::cv_status::no_timeout;
    }
    void notify_one() { cond.notify_one(); }
    void notify_all() { cond.notify_all(); }

    void lock() { mtx.lock(); }

    void unlock() { mtx.unlock(); }
};

struct StdContext {
    using Cond = StdCond;
    using CondLock = std::unique_lock<std::mutex>;
    using Lock = std::unique_lock<std::mutex>;
    using Mutex = std::mutex;
};

}  // namespace Executor
