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
#include <photon/thread/thread11.h>

namespace photon {

template <typename T>
struct WorkResult {
    photon::semaphore sem;
    T ret;
    template <typename V>
    void set(V&& v) {
        ret = std::move(v);
        sem.signal(1);
    }

    T get() {
        sem.wait(1);
        return std::move(ret);
    }
};

template <>
struct WorkResult<void> {
    photon::semaphore sem;
    void set(void) { sem.signal(1); }

    void get() { sem.wait(1); }
};

class WorkPool : public Object {
public:
    template <class F, class... Args>
    auto call(F&& f, Args&&... args) -> typename std::enable_if<
        !std::is_void<typename std::result_of<F(Args&&...)>::type>::value,
        typename std::result_of<F(Args&&...)>::type>::type {
        using return_type = typename std::result_of<F(Args...)>::type;
        static_assert(!std::is_void<return_type>::value, "...");
        WorkResult<return_type> result;
        auto task = [&]() -> void {
            result.set(f(std::forward<Args>(args)...));
        };
        do_call(task);
        return result.get();
    }

    template <class F, class... Args>
    auto call(F&& f, Args&&... args) -> typename std::enable_if<
        std::is_void<typename std::result_of<F(Args...)>::type>::value>::type {
        using return_type = typename std::result_of<F(Args...)>::type;
        WorkResult<return_type> result;
        auto task = [&]() -> void {
            f(std::forward<Args>(args)...);
            result.set();
        };
        do_call(task);
        return result.get();
    }

protected:
    // send delegate to run at a workerthread,
    // Caller should keep callable object and resources alive
    virtual void do_call(Delegate<void> call) = 0;
};

WorkPool* new_work_pool(int thread_num, int ev_engine = 0, int io_engine = 0);

}  // namespace photon
