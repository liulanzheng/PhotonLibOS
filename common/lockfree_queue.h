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

#ifndef __aarch64__
#include <immintrin.h>
#endif

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

struct PauseBase {};

struct CPUPause : PauseBase {
    inline static __attribute__((always_inline)) void pause() {
#ifdef __aarch64__
        asm volatile("isb" : : : "memory");
#else
        _mm_pause();
#endif
    }
};

struct ThreadPause : PauseBase {
    inline static __attribute__((always_inline)) void pause() {
        std::this_thread::yield();
    }
};

namespace photon {
void thread_yield();
}
struct PhotonPause : PauseBase {
    inline static __attribute__((always_inline)) void pause() {
        photon::thread_yield();
    }
};

template <typename Derived, typename T, size_t N, typename BusyPause>
class LockfreeRingQueueBase {
public:
    static_assert(std::has_trivial_copy_constructor<T>::value &&
                      std::has_trivial_copy_assign<T>::value,
                  "T should be trivially copyable");
    static_assert(std::is_base_of<PauseBase, BusyPause>::value,
                  "BusyPause should be derived by PauseBase");

    constexpr static size_t CACHELINE_SIZE = 64;

    constexpr static size_t capacity = Capacity_2expN<N>::capacity;
    constexpr static size_t mask = Capacity_2expN<N>::mask;

    alignas(CACHELINE_SIZE) std::atomic<size_t> tail;
    alignas(CACHELINE_SIZE) std::atomic<size_t> head;

    template <typename Pause = BusyPause>
    T recv() {
        static_assert(std::is_base_of<PauseBase, Pause>::value,
                      "BusyPause should be derived by PauseBase");
        T ret;
        while (!Derived::pop(ret)) Pause::pause();
        return ret;
    }

    template <typename Pause = BusyPause>
    void send(const T& x) {
        static_assert(std::is_base_of<PauseBase, Pause>::value,
                      "BusyPause should be derived by PauseBase");
        while (!Derived::push(x)) Pause::pause();
    }

    bool empty() { return (head & mask) == (tail & mask); }

    bool full() { return ((tail + 1) & mask) == head; }
};

template <typename T, size_t N, typename BusyPause = CPUPause>
class LockfreeRingQueue
    : public LockfreeRingQueueBase<LockfreeRingQueue<T, N, BusyPause>, T, N,
                                   BusyPause> {
public:
    using Base = LockfreeRingQueueBase<LockfreeRingQueue<T, N, BusyPause>, T, N,
                                       BusyPause>;

    constexpr static size_t CACHELINE_SIZE = 64;

    using Base::capacity;
    using Base::head;
    using Base::mask;
    using Base::tail;

    struct alignas(Base::CACHELINE_SIZE) Entry {
        T x;
        std::atomic_bool commit;
    };

    static_assert(sizeof(Entry) % CACHELINE_SIZE == 0,
                  "Entry should aligned to cacheline");
    alignas(CACHELINE_SIZE) Entry arr[capacity];
    static_assert(sizeof(arr) % CACHELINE_SIZE == 0,
                  "Entry should aligned to cacheline");

    bool push(T x) {
        // try to push forward read head to make room for write
        size_t t = tail.load();
        do {
            if (((t + 1) & mask) == head || arr[t & mask].commit) {
                return false;
            }
        } while (
            !tail.compare_exchange_weak(t, t + 1, std::memory_order_acq_rel));
        // acquire write tail, return failure if cannot acquire position
        auto& item = arr[t & mask];
        item.x = x;
        item.commit.store(true, std::memory_order_release);
        return true;
    }

    bool pop(T& x) {
        // take a reading ticket
        size_t h = head.load();
        do {
            if ((h & mask) == (tail & mask)) return false;
        } while (
            !head.compare_exchange_weak(h, h + 1, std::memory_order_acq_rel));
        // h will hold old value if CAS succeed
        auto& item = arr[h & mask];
        // spin on single item commit
        while (!item.commit) {
            BusyPause::pause();
        }
        x = item.x;
        // marked as uncommited
        item.commit.store(false, std::memory_order_release);
        return true;
    }

    bool empty() { return (head & mask) == (tail & mask); }

    bool full() {
        return ((tail + 1) & mask) == head || arr[tail & mask].commit;
    }
};

template <typename T, size_t N, typename BusyPause = CPUPause>
class LockfreeSPSCRingQueue
    : public LockfreeRingQueueBase<LockfreeSPSCRingQueue<T, N, BusyPause>, T, N,
                                   BusyPause> {
public:
    using Base = LockfreeRingQueueBase<LockfreeSPSCRingQueue<T, N, BusyPause>,
                                       T, N, BusyPause>;
    using Base::capacity;
    using Base::head;
    using Base::mask;
    using Base::tail;

    T arr[capacity];

    bool push(T x) {
        // try to push forward read head to make room for write
        if (((tail + 1) & mask) == head) {
            return false;
        }
        auto t = tail.fetch_add(1);
        // acquire write tail, return failure if cannot acquire position
        arr[t & mask] = x;
        return true;
    }

    bool pop(T& x) {
        // take a reading ticket
        if ((head & mask) == (tail & mask)) return false;
        // h will hold old value if CAS succeed
        auto h = head.fetch_add(1);
        arr[h & mask] = x;
        return true;
    }
};