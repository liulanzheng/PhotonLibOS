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
    constexpr static size_t shift = Capacity_2expN<(x >> 1)>::shift + 1;
    constexpr static size_t lshift = Capacity_2expN<(x >> 1)>::lshift - 1;

    static_assert(shift + lshift == sizeof(size_t)*8, "...");
};

template <size_t x>
constexpr size_t Capacity_2expN<x>::capacity;

template <size_t x>
constexpr size_t Capacity_2expN<x>::mask;

template <>
struct Capacity_2expN<0> {
    constexpr static size_t capacity = 2;
    constexpr static size_t mask = 1;
    constexpr static size_t shift = 1;
    constexpr static size_t lshift = 8 * sizeof(size_t) - shift;
};

template <>
struct Capacity_2expN<1> : public Capacity_2expN<0> {};

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
    constexpr static size_t shift = Capacity_2expN<N>::shift;
    constexpr static size_t lshift = Capacity_2expN<N>::lshift;

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

    bool empty() { return check_empty(head, tail); }

    bool full() { return check_full(head, tail); }

protected:

    inline __attribute__((always_inline)) bool check_mask_equal(size_t x, size_t y) {
        return (x << lshift) == (y << lshift);
    }

    inline __attribute__((always_inline)) bool check_empty(size_t h, size_t t) {
        return check_mask_equal(h, t);
    }

    inline __attribute__((always_inline)) bool check_full(size_t h, size_t t) {
        return check_mask_equal(h, t + 1);
    }

    inline __attribute__((always_inline)) size_t pos(size_t x) {
        return x & mask;
    }
};

template <typename T, size_t N, typename BusyPause = CPUPause>
class LockfreeRingQueue
    : public LockfreeRingQueueBase<LockfreeRingQueue<T, N, BusyPause>, T, N,
                                   BusyPause> {
public:
    using Base = LockfreeRingQueueBase<LockfreeRingQueue<T, N, BusyPause>, T, N,
                                       BusyPause>;

    constexpr static size_t CACHELINE_SIZE = 64;

    using Base::head;
    using Base::tail;
    using Base::empty;
    using Base::full;

    struct alignas(CACHELINE_SIZE) Entry {
        T x;
        std::atomic_bool commit;
    };

    static_assert(sizeof(Entry) % CACHELINE_SIZE == 0,
                  "Entry should aligned to cacheline");
    alignas(CACHELINE_SIZE) Entry arr[Base::capacity];
    static_assert(sizeof(arr) % CACHELINE_SIZE == 0,
                  "Entry should aligned to cacheline");

    bool push(T x) {
        // try to push forward read head to make room for write
        size_t t = tail.load();
        do {
            if (Base::check_full(head, t)) {
                return false;
            }
        } while (
            !tail.compare_exchange_weak(t, t + 1, std::memory_order_acq_rel));
        // acquire write tail, return failure if cannot acquire position
        auto& item = arr[Base::pos(t)];
        std::atomic_thread_fence(std::memory_order_acquire);
        while (item.commit) {
            BusyPause::pause();
        }
        item.x = x;
        item.commit.store(true, std::memory_order_release);
        return true;
    }

    bool pop(T& x) {
        // take a reading ticket
        size_t h = head.load();
        do {
            if (Base::check_empty(h, tail)) return false;
        } while (
            !head.compare_exchange_weak(h, h + 1, std::memory_order_acq_rel));
        // h will hold old value if CAS succeed
        auto& item = arr[Base::pos(h)];
        // spin on single item commit
        std::atomic_thread_fence(std::memory_order_acquire);
        while (!item.commit) {
            BusyPause::pause();
        }
        x = item.x;
        // marked as uncommited
        item.commit.store(false, std::memory_order_release);
        return true;
    }

};

template <typename T, size_t N, typename BusyPause = CPUPause>
class LockfreeSPSCRingQueue
    : public LockfreeRingQueueBase<LockfreeSPSCRingQueue<T, N, BusyPause>, T, N,
                                   BusyPause> {
public:
    using Base = LockfreeRingQueueBase<LockfreeSPSCRingQueue<T, N, BusyPause>,
                                       T, N, BusyPause>;
    using Base::head;
    using Base::tail;
    using Base::empty;
    using Base::full;

    T arr[Base::capacity];

    bool push(T x) {
        // try to push forward read head to make room for write
        if (full()) {
            return false;
        }
        auto t = tail.fetch_add(1, std::memory_order_acq_rel);
        arr[Base::pos(t)] = x;
        return true;
    }

    bool pop(T& x) {
        // take a reading ticket
        if (empty()) return false;
        auto h = head.fetch_add(1, std::memory_order_acq_rel);
        arr[Base::pos(h)] = x;
        return true;
    }
};