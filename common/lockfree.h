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

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <thread>
#include <utility>

template <size_t x>
struct Capacity_2expN {
    constexpr static size_t capacity = Capacity_2expN<(x >> 1)>::capacity << 1;
    constexpr static size_t mask = capacity - 1;
};

template <size_t x>
constexpr size_t Capacity_2expN<x>::capacity;

template <size_t x>
constexpr size_t Capacity_2expN<x>::mask;

template <>
struct Capacity_2expN<0> {
    constexpr static size_t capacity = 2;
    constexpr static size_t mask = 1;
};

template <>
struct Capacity_2expN<1> {
    constexpr static size_t capacity = 2;
    constexpr static size_t mask = 1;
};

template <typename T, size_t N>
class LockfreeRingQueue {
public:
    constexpr static size_t CACHELINE_SIZE = 64;

    constexpr static size_t size = Capacity_2expN<N>::capacity;
    constexpr static size_t mask = Capacity_2expN<N>::mask;

    struct Entry {
        T x;
        alignas(CACHELINE_SIZE) std::atomic_bool commit;
    };

    static_assert(sizeof(Entry) % CACHELINE_SIZE == 0,
                  "Entry should aligned to cacheline");
    Entry arr[size];
    static_assert(sizeof(arr) % CACHELINE_SIZE == 0,
                  "Entry should aligned to cacheline");

    alignas(CACHELINE_SIZE) std::atomic<size_t> rtail;
    alignas(CACHELINE_SIZE) std::atomic<size_t> rhead;
    alignas(CACHELINE_SIZE) std::atomic<size_t> wtail;
    alignas(CACHELINE_SIZE) std::atomic<size_t> whead;

    template <typename TI>
    bool push(TI&& x) {
        // try to push forward read head to make room for write
        auto h = rhead.load(std::memory_order_relaxed);
        while (
            !arr[h & mask].commit.load(std::memory_order_relaxed) &&
            rhead.compare_exchange_weak(h, h + 1, std::memory_order_acq_rel))
            ;
        // auto h = rhead.load(std::memory_order_relaxed);
        auto t = wtail.load(std::memory_order_relaxed);
        // return failure if might be full
        if ((h & mask) == ((t + 1) & mask)) return false;
        // acquire write tail, return failure if cannot acquire position
        if (!wtail.compare_exchange_weak(t, t + 1, std::memory_order_acq_rel,
                                         std::memory_order_relaxed))
            return false;
        auto& item = arr[t & mask];
        item.x = std::forward<TI>(x);
        // mark push done
        item.commit.store(true, std::memory_order_relaxed);
        return true;
    }

    bool pop(T& x) {
        // try to push forward write head
        auto t = whead.load(std::memory_order_relaxed);
        while (
            arr[t & mask].commit.load(std::memory_order_relaxed) &&
            whead.compare_exchange_weak(t, t + 1, std::memory_order_acq_rel))
            ;
        auto h = rtail.load(std::memory_order_relaxed);
        // auto t = whead.load(std::memory_order_relaxed);
        // return failure if might be empty
        if (h == t) return false;
        // acquire read tail, return failure if cannot acquire position
        if (!rtail.compare_exchange_weak(h, h + 1, std::memory_order_acq_rel,
                                         std::memory_order_relaxed))
            return false;
        auto& item = arr[h & mask];
        x = std::move(item.x);
        // mark read done
        item.commit.store(false, std::memory_order_relaxed);
        return true;
    }

    T recv() {
        T ret;
        while (!pop(ret)) ::sched_yield();
        return ret;
    }

    template <typename TI>
    void send(TI&& x) {
        while (!push(std::forward<TI>(x))) ::sched_yield();
    }

    bool empty() {
        auto wh = whead.load(std::memory_order_relaxed);
        while (
            arr[wh & mask].commit.load(std::memory_order_relaxed) &&
            whead.compare_exchange_weak(wh, wh + 1, std::memory_order_acq_rel))
            ;
        return (whead.load(std::memory_order_relaxed) ==
                rtail.load(std::memory_order_relaxed));
    }

    bool full() {
        auto rh = rhead.load(std::memory_order_relaxed);
        while (
            !arr[rh & mask].commit.load(std::memory_order_relaxed) &&
            rhead.compare_exchange_weak(rh, rh + 1, std::memory_order_acq_rel))
            ;
        return (rhead.load(std::memory_order_relaxed) & mask) ==
               ((wtail.load(std::memory_order_relaxed) + 1) & mask);
    }
};
