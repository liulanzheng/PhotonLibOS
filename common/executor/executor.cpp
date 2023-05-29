#include "executor.h"

#include <photon/common/alog.h>
#include <photon/common/event-loop.h>
#include <photon/common/executor/executor.h>
#include <photon/common/lockfree_queue.h>
#include <photon/common/utility.h>
#include <photon/io/fd-events.h>
#include <photon/thread/thread-pool.h>
#include <photon/thread/thread11.h>

#include <atomic>
#include <thread>

namespace photon {

class ExecutorImpl {
public:
    using CBList =
        common::RingChannel<LockfreeMPMCRingQueue<Delegate<void>, 32UL * 1024>>;
    std::unique_ptr<std::thread> th;
    photon::thread *pth = nullptr;
    CBList queue;
    photon::ThreadPoolBase *pool;

    ExecutorImpl(int init_ev, int init_io) {
        th.reset(
            new std::thread(&ExecutorImpl::do_loop, this, init_ev, init_io));
    }

    ~ExecutorImpl() {
        queue.send({});
        th->join();
    }

    struct CallArg {
        Delegate<void> task;
        photon::thread *backth;
    };

    static void *do_event(void *arg) {
        auto a = (CallArg *)arg;
        auto task = a->task;
        photon::thread_yield_to(a->backth);
        task();
        return nullptr;
    }

    void main_loop() {
        CallArg arg;
        arg.backth = photon::CURRENT;
        for (;;) {
            arg.task = queue.recv();
            if (!arg.task) {
                return;
            }
            auto th =
                pool->thread_create(&ExecutorImpl::do_event, (void *)&arg);
            photon::thread_yield_to(th);
        }
    }

    void do_loop(int init_ev, int init_io) {
        photon::init(init_ev, init_io);
        DEFER(photon::fini());
        pth = photon::CURRENT;
        pool = photon::new_thread_pool(32);
        LOG_INFO("worker start");
        main_loop();
        LOG_INFO("worker finished");
        photon::delete_thread_pool(pool);
        pool = nullptr;
    }
};

ExecutorImpl *_new_executor(int init_ev, int init_io) {
    return new ExecutorImpl(init_ev, init_io);
}

void _delete_executor(ExecutorImpl *e) { delete e; }

void _issue(ExecutorImpl *e, Delegate<void> act) { e->queue.send<ThreadPause>(act); }

}  // namespace photon