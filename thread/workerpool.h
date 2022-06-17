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

#pragma once

#include <photon/common/object.h>
#include <photon/thread/thread.h>

#include <memory>

namespace photon {

class WorkPool {
public:
    WorkPool(int thread_num, int ev_engine = 0, int io_engine = 0);

    WorkPool(const WorkPool& other) = delete;
    WorkPool& operator=(const WorkPool& rhs) = delete;

    ~WorkPool();

    template <class F, class... Args>
    void call(F&& f, Args&&... args) {
        photon::semaphore sem(0);
        auto task = [&]() -> void {
            f(std::forward<Args>(args)...);
            sem.signal(1);
        };
        do_call(task);
        sem.wait(1);
        return;
    }

protected:
    class impl;  // does not depend on T
    std::unique_ptr<impl> pImpl;
    // send delegate to run at a workerthread,
    // Caller should keep callable object and resources alive
    void do_call(Delegate<void> call);
};

}  // namespace photon
