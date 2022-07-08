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

    struct alignas(CACHELINE_SIZE) Entry {
        std::atomic_bool commit;
        T x;
    };

    static_assert(sizeof(Entry) % CACHELINE_SIZE == 0,
                  "Entry should aligned to cacheline");
    alignas(CACHELINE_SIZE) Entry arr[size];
    static_assert(sizeof(arr) % CACHELINE_SIZE == 0,
                  "Entry should aligned to cacheline");

    alignas(CACHELINE_SIZE) std::atomic<size_t> tail;
    alignas(CACHELINE_SIZE) std::atomic<size_t> head;

    template <typename TI>
    bool push(TI&& x) {
        // try to push forward read head to make room for write
        size_t t = tail.load();
        do {
            if (((t+1)&mask) == head.load() || arr[t & mask].commit.load()) {
                return false;
            }
        }  while (!tail.compare_exchange_weak(t, t+1, std::memory_order_acq_rel));
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
            if ((h & mask) == (tail.load() & mask)) return false;
        } while (!head.compare_exchange_weak(h, h+1, std::memory_order_acq_rel));
        // h will hold old value if CAS succeed
        auto& item = arr[h & mask];
        // spin on single item commit
        while (!item.commit.load()) {
            ::sched_yield();
        }
        x = item.x;
        // marked as uncommited
        item.commit.store(false, std::memory_order_release);
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
        return (head.load() & mask) == (tail.load() & mask);
    }

    bool full() {
        return ((tail.load()+1)&mask) == head.load() || arr[tail.load() & mask].commit.load();
    }
};

template <typename T, size_t N>
class LockfreeSPSCRingQueue {
public:
    constexpr static size_t CACHELINE_SIZE = 64;

    constexpr static size_t size = Capacity_2expN<N>::capacity;
    constexpr static size_t mask = Capacity_2expN<N>::mask;

    T arr[size];

    alignas(CACHELINE_SIZE) std::atomic<size_t> tail;
    alignas(CACHELINE_SIZE) std::atomic<size_t> head;

    template <typename TI>
    bool push(TI&& x) {
        // try to push forward read head to make room for write
        if (((tail.load()+1)&mask) == head.load()) {
            return false;
        }
        auto t = tail.fetch_add(1);
        // acquire write tail, return failure if cannot acquire position
        arr[t & mask] = x;
        return true;
    }

    bool pop(T& x) {
        // take a reading ticket
        if ((head.load() & mask) == (tail.load() & mask)) return false;
        // h will hold old value if CAS succeed
        auto h = head.fetch_add(1);
        arr[h & mask] = x;
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
        return (head.load() & mask) == (tail.load() & mask);
    }

    bool full() {
        return ((tail.load()+1)&mask) == head.load();
    }
};