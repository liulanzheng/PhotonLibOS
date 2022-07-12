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

#include <photon/thread/thread.h>
#include <pthread.h>

#include <array>
#include <boost/lockfree/policies.hpp>
#include <boost/lockfree/queue.hpp>
#include <boost/lockfree/spsc_queue.hpp>
#include <mutex>
#include <random>
#include <thread>
#include <vector>

#include "../lockfree_queue.h"

static constexpr size_t sender_num = 8;
static constexpr size_t receiver_num = 1;
static constexpr size_t items_num = 3000000;
static_assert(items_num % sender_num == 0,
              "item_num should able to divided by sender_num");
static_assert(items_num % receiver_num == 0,
              "item_num should able to divided by receiver_num");
static constexpr size_t capacity = 16UL * 1024;

std::array<int, receiver_num> rcnt;
std::array<int, sender_num> scnt;

std::array<std::atomic<int>, items_num / sender_num> sc, rc;

LockfreeRingQueue<int, capacity> queue;
LockfreeSPSCRingQueue<int, capacity> cqueue;
MPSCRingQueue<int, capacity> mqueue;
std::mutex rlock, wlock;

boost::lockfree::queue<int, boost::lockfree::capacity<capacity>> bqueue;
boost::lockfree::spsc_queue<int, boost::lockfree::capacity<capacity>> squeue;

struct WithLock {
    template <typename T>
    static void lock(T &x) {
        x.lock();
    }
    template <typename T>
    static void unlock(T &x) {
        x.unlock();
    }
};

struct NoLock {
    template <typename T>
    static void lock(T &x) {}
    template <typename T>
    static void unlock(T &x) {}
};

template <typename LType, typename QType>
int test_queue(const char *name, QType &queue) {
    std::vector<std::thread> senders, receivers;
    scnt.fill(0);
    rcnt.fill(0);
    auto begin = std::chrono::steady_clock::now();
    for (int i = 0; i < receiver_num; i++) {
        receivers.emplace_back([i, &queue] {
            cpu_set_t cpuset;
            CPU_ZERO(&cpuset);
            CPU_SET(i, &cpuset);
            pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
            for (int x = 0; x < items_num / receiver_num; x++) {
                int t;
                LType::lock(rlock);
                while (!queue.pop(t)) {
                    CPUPause::pause();
                }
                (void)t;
                LType::unlock(rlock);
                rc[t]++;
                rcnt[i]++;
            }
        });
    }
    for (int i = 0; i < sender_num; i++) {
        senders.emplace_back([i, &queue] {
            cpu_set_t cpuset;
            CPU_ZERO(&cpuset);
            CPU_SET(i + receiver_num, &cpuset);
            pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
            for (int x = 0; x < items_num / sender_num; x++) {
                LType::lock(wlock);
                while (!queue.push(x)) {
                    CPUPause::pause();
                }
                LType::unlock(wlock);
                sc[x]++;
                scnt[i]++;
                ThreadPause::pause();
            }
        });
    }
    for (auto &x : senders) x.join();
    for (auto &x : receivers) x.join();
    auto end = std::chrono::steady_clock::now();
    printf("%s %lu p %lu c, %lu items, Spent %ld us\n", name, sender_num,
           receiver_num, items_num,
           std::chrono::duration_cast<std::chrono::microseconds>(end - begin)
               .count());
    for (int i=0;i<items_num / sender_num;i++) {
        if (sc[i] != rc[i]) {
            printf("MISMATCH %d %d %d\n", i, sc[i].load(), rc[i].load());
        }
        sc[i] = 0;
        rc[i] = 0;
    }
    std::this_thread::sleep_for(std::chrono::seconds(1));
    return 0;
}

int main() {
    test_queue<NoLock>("BoostQueue", bqueue);
    test_queue<NoLock>("PhotonQueue", queue);
    test_queue<WithLock>("BoostSPSCQueue", squeue);
    test_queue<WithLock>("PhotonSPSCQueue", cqueue);
    test_queue<NoLock>("PhotonMPSCQueue", mqueue);
}