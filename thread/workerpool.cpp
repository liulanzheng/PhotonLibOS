#include <photon/common/alog.h>
#include <photon/common/ring.h>
#include <photon/photon.h>
#include <photon/thread/workerpool.h>

#include <thread>
#include <vector>

namespace photon {

class WorkPoolImpl : public WorkPool {
public:
    std::vector<std::thread> workers;
    photon::ticket_spinlock queue_lock;
    std::atomic<bool> stop;
    photon::semaphore queue_sem;
    RingQueue<Delegate<void>> ring;
    int th_num;

    WorkPoolImpl(int thread_num, int ev_engine, int io_engine)
        : stop(false), queue_sem(0), ring(4096), th_num(thread_num) {
        for (int i = 0; i < thread_num; ++i)
            workers.emplace_back([this] {
                photon::init(0, 0);
                DEFER(photon::fini());
                for (;;) {
                    Delegate<void> task;
                    {
                        queue_sem.wait(1);
                        if (this->stop && ring.empty()) return;
                        photon::locker<photon::ticket_spinlock> lock(
                            queue_lock);
                        ring.pop_front(task);
                    }
                    task();
                }
            });
    }

    ~WorkPoolImpl() override {
        stop = true;
        queue_sem.signal(th_num);
        for (std::thread& worker : workers) worker.join();
    }

    virtual void do_call(Delegate<void> call) override {
        {
            photon::locker<photon::ticket_spinlock> lock(queue_lock);
            ring.push_back(call);
        }
        queue_sem.signal(1);
    }
};

WorkPool* new_work_pool(int thread_num, int ev_engine, int io_engine) {
    if (thread_num <= 0) return nullptr;
    return new WorkPoolImpl(thread_num, ev_engine, io_engine);
}

}  // namespace photon