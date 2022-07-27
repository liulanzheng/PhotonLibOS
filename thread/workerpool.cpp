/*
Copyright 2022 The Photon Authors

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
*/

#include "workerpool.h"

#include <photon/common/lockfree_queue.h>
#include <photon/photon.h>
#include <photon/thread/thread.h>

#include <thread>

namespace photon {

class WorkPool::impl {
public:
    static constexpr uint32_t RING_SIZE = 256;

    std::vector<std::thread> workers;
    std::atomic<bool> stop;
    photon::semaphore queue_sem;
    LockfreeMPMCRingQueue<Delegate<void>, RING_SIZE> ring;
    int m_vcpu_num;
    std::vector<photon::vcpu_base*> m_vcpus;

    impl(int vcpu_num, int ev_engine, int io_engine)
        : stop(false), queue_sem(0), m_vcpu_num(vcpu_num) {
        m_vcpus.resize(vcpu_num);
        for (int i = 0; i < vcpu_num; ++i)
            workers.emplace_back(&WorkPool::impl::worker_thread_routine, this, i,
                                 ev_engine, io_engine);
    }

    ~impl() {
        stop = true;
        queue_sem.signal(m_vcpu_num);
        for (auto& worker : workers) worker.join();
    }

    void enqueue(Delegate<void> call) {
        {
            ring.send(call);
        }
        queue_sem.signal(1);
    }

    void do_call(Delegate<void> call) {
        photon::semaphore sem(0);
        auto task = [call, &sem] {
            call();
            sem.signal(1);
        };
        enqueue(Delegate<void>(task));
        sem.wait(1);
    }

    void worker_thread_routine(int index, int ev_engine, int io_engine) {
        photon::init(ev_engine, io_engine);
        DEFER(photon::fini());
        m_vcpus[index] = photon::get_vcpu();
        for (;;) {
            Delegate<void> task;
            {
                queue_sem.wait(1);
                if (this->stop && ring.empty()) return;
                task = ring.recv();
            }
            task();
        }
    }
};

WorkPool::WorkPool(int vcpu_num, int ev_engine, int io_engine)
    : pImpl(new impl(vcpu_num, ev_engine, io_engine)) {}

WorkPool::~WorkPool() {}

void WorkPool::do_call(Delegate<void> call) { pImpl->do_call(call); }
void WorkPool::enqueue(Delegate<void> call) { pImpl->enqueue(call); }

std::vector<photon::vcpu_base*> WorkPool::get_vcpus() const {
    return pImpl->m_vcpus;
}

}  // namespace photon