#pragma once

#include <condition_variable>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <queue>
#include <stdexcept>
#include <thread>
#include <vector>

#include "thread11.h"

class WorkPool {
public:
    WorkPool(size_t);
    template <class F, class... Args>
    auto enqueue(F&& f, Args&&... args)
        -> std::future<typename std::result_of<F(Args...)>::type>;

    template <class F, class... Args>
    auto photon_call(F&& f, Args&&... args) ->
        typename std::result_of<F(Args...)>::type;
    ~WorkPool();

private:
    // need to keep track of threads so we can join them
    std::vector<std::thread> workers;
    // the task queue
    std::queue<std::function<void()> > tasks;

    // synchronization
    std::mutex queue_mutex;
    std::condition_variable condition;
    bool stop;
};

// the constructor just launches some amount of workers
inline WorkPool::WorkPool(size_t threads) : stop(false) {
    for (size_t i = 0; i < threads; ++i)
        workers.emplace_back([this] {
            for (;;) {
                std::function<void()> task;

                {
                    std::unique_lock<std::mutex> lock(this->queue_mutex);
                    this->condition.wait(lock, [this] {
                        return this->stop || !this->tasks.empty();
                    });
                    if (this->stop && this->tasks.empty()) return;
                    task = std::move(this->tasks.front());
                    this->tasks.pop();
                }

                task();
            }
        });
}

// add new work item to the pool
template <class F, class... Args>
auto WorkPool::enqueue(F&& f, Args&&... args)
    -> std::future<typename std::result_of<F(Args...)>::type> {
    using return_type = typename std::result_of<F(Args...)>::type;

    auto task = std::make_shared<std::packaged_task<return_type()> >(
        std::bind(std::forward<F>(f), std::forward<Args>(args)...));

    std::future<return_type> res = task->get_future();
    {
        std::unique_lock<std::mutex> lock(queue_mutex);

        // don't allow enqueueing after stopping the pool
        if (stop) throw std::runtime_error("enqueue on stopped WorkPool");

        tasks.emplace([task]() { (*task)(); });
    }
    condition.notify_one();
    return res;
}

template <class F, class... Args>
auto WorkPool::photon_call(F&& f, Args&&... args) ->
    typename std::result_of<F(Args...)>::type {
    photon::thread* th = photon::CURRENT;

    using return_type = typename std::result_of<F(Args...)>::type;

    std::future<return_type> future;

    auto runtask = [&]() -> return_type {
        auto ret = f(std::forward<Args>(args)...);
        photon::thread_interrupt(th);
        return ret;
    };

    auto caller = [&]() -> void { future = enqueue(runtask); };

    photon::thread_usleep_defer(-1UL, caller);

    return future.get();
}

// the destructor joins all threads
inline WorkPool::~WorkPool() {
    {
        std::unique_lock<std::mutex> lock(queue_mutex);
        stop = true;
    }
    condition.notify_all();
    for (std::thread& worker : workers) worker.join();
}