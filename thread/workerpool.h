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

#include <photon/common/callback.h>
#include <photon/thread/thread11.h>

#include <memory>

namespace photon {

template <typename F>
static void __async_helper(void* f) {
    auto func = (F*)f;
    (*func)();
    delete func;
}
class WorkPool {
public:
    WorkPool(int thread_num, int ev_engine = 0, int io_engine = 0);

    WorkPool(const WorkPool& other) = delete;
    WorkPool& operator=(const WorkPool& rhs) = delete;

    ~WorkPool();

    template <class F, class... Args>
    void call(F&& f, Args&&... args) {
        auto task = [&] { f(std::forward<Args>(args)...); };
        do_call(task);
    }

    template <class F, class... Args>
    void async_call(F&& f, Args&&... args) {
        auto task = new auto([=] { f(std::forward<Args>(args)...); });
        using Task = decltype(task);
        void (*func)(Task) = [](Task _task) {
            (*_task)();
            delete _task;
        };
        enqueue(Delegate<void>((Delegate<void>::Func&)func, task));
    }

protected:
    class impl;  // does not depend on T
    std::unique_ptr<impl> pImpl;
    // send delegate to run at a workerthread,
    // Caller should keep callable object and resources alive
    void do_call(Delegate<void> call);
    void enqueue(Delegate<void> call);
};

}  // namespace photon