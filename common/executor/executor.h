#pragma once

#include <atomic>
#include <boost/lockfree/spsc_queue.hpp>
#include <condition_variable>
#include <mutex>
#include <thread>

#include <photon/common/alog.h>
#include <photon/common/event-loop.h>
#include <photon/io/fd-events.h>
#include <photon/thread/thread-pool.h>
#include <photon/thread/thread11.h>
#include <photon/common/utility.h>
#include <photon/common/executor/stdlock.h>

namespace Executor {

constexpr int64_t kCondWaitMaxTime = 1000L * 1000;

template <typename R, typename Context>
struct AsyncReturn {
    R result;
    int err = 0;
    std::atomic_bool gotit;
    typename Context::Mutex mtx;
    typename Context::Cond cond;
    AsyncReturn() : gotit(false), cond(mtx) {}
    R wait_for_result() {
        typename Context::CondLock lock(mtx);
        while (!gotit) {
            cond.wait_for(lock, kCondWaitMaxTime);
        }
        if (err) errno = err;
        return result;
    }
    void set_result(R r) {
        typename Context::CondLock lock(mtx);
        err = errno;
        result = std::forward<R>(r);
        gotit = true;
        cond.notify_all();
    }
};

template <typename Context>
struct AsyncReturn<void, Context> {
    int err = 0;
    std::atomic_bool gotit;
    typename Context::Mutex mtx;
    typename Context::Cond cond;
    AsyncReturn() : gotit(false), cond(mtx) {}
    void wait_for_result() {
        typename Context::CondLock lock(mtx);
        while (!gotit) {
            cond.wait_for(lock, kCondWaitMaxTime);
        }
        if (err) errno = err;
    }
    void set_result() {
        typename Context::CondLock lock(mtx);
        err = errno;
        gotit = true;
        cond.notify_all();
    }
};

struct YieldOp {
    static void yield() { ::sched_yield(); }
};

class HybridEaseExecutor {
public:
    HybridEaseExecutor() {
        loop = new_event_loop({this, &HybridEaseExecutor::wait_for_event},
                              {this, &HybridEaseExecutor::on_event});
        th = new std::thread(&HybridEaseExecutor::do_loop, this);
        while (!loop || loop->state() != loop->WAITING) ::sched_yield();
    }

    ~HybridEaseExecutor() {
        photon::thread_interrupt(pth);
        th->join();
        delete th;
    }

    template <
        typename Context = StdContext, typename Func,
        typename R = typename std::result_of<Func()>::type,
        typename = typename std::enable_if<!std::is_same<R, void>::value>::type>
    R perform(Func &&act) {
        auto aret = new AsyncReturn<R, Context>();
        DEFER(delete aret);
        auto work = [act, aret, this] {
            if (!aret->gotit) {
                aret->set_result(act());
            }
            return 0;
        };
        Callback<> cb(work);
        issue(cb);
        return aret->wait_for_result();
    }

    template <
        typename Context = StdContext, typename Func,
        typename R = typename std::result_of<Func()>::type,
        typename = typename std::enable_if<std::is_same<R, void>::value>::type>
    void perform(Func &&act) {
        auto aret = new AsyncReturn<void, Context>();
        DEFER(delete aret);
        auto work = [act, aret, this] {
            if (!aret->gotit) {
                act();
                aret->set_result();
            }
            return 0;
        };
        Callback<> cb(work);
        issue(cb);
        return aret->wait_for_result();
    }

protected:
    using CBList =
        typename boost::lockfree::spsc_queue<Callback<>,
                                        boost::lockfree::capacity<32UL * 1024>>;
    std::thread *th = nullptr;
    photon::thread *pth = nullptr;
    EventLoop *loop = nullptr;
    CBList queue;
    photon::ThreadPoolBase *pool;
    std::mutex mutex;

    void issue(Callback<> act) {
        {
            std::lock_guard<std::mutex> lock(mutex);
            while (!queue.push(act)) YieldOp::yield();
        }
        photon::thread_interrupt(loop->loop_thread(), EINPROGRESS);
    }

    int wait_for_event(EventLoop *) {
        if (!queue.empty()) return 1;
        auto th = photon::CURRENT;
        int ret = photon::thread_usleep_defer(-1UL, [&] {
            if (!queue.empty()) photon::thread_interrupt(th, EINPROGRESS);
        });
        if (ret < 0) {
            ERRNO err;
            if (err.no == EINPROGRESS)
                return 1;
            else if (err.no == EINTR)
                return -1;
        }
        return 0;
    }

    static void *do_event(void *arg) {
        auto a = (Callback<> *)arg;
        auto task = *a;
        delete a;
        task();
        return nullptr;
    }

    int on_event(EventLoop *) {
        while (!queue.empty()) {
            auto args = new Callback<>;
            auto &task = *args;
            if (queue.pop(task)) {
                pool->thread_create(&HybridEaseExecutor::do_event,
                                    (void *)args);
            }
        }
        return 0;
    }

    void do_loop() {
        photon::init();
        photon::fd_events_init();
        pth = photon::CURRENT;
        LOG_INFO("worker start");
        pool = photon::new_thread_pool(32);
        loop->async_run();
        photon::thread_usleep(-1);
        LOG_INFO("worker finished");
        while (!queue.empty()) photon::thread_usleep(1000);
        delete loop;
        photon::delete_thread_pool(pool);
        pool = nullptr;
        photon::fd_events_fini();
        photon::fini();
    }
};
}  // namespace Executor
