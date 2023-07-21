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

#include <fcntl.h>
#include <gtest/gtest.h>
#include <photon/common/alog.h>
#include <photon/common/executor/executor.h>
#include <photon/common/utility.h>
#include <photon/fs/exportfs.h>
#include <photon/fs/filesystem.h>
#include <photon/fs/localfs.h>
#include <photon/photon.h>
#include <photon/thread/thread11.h>
#include <photon/thread/workerpool.h>

using namespace photon;

std::atomic<int> count;

int ftask(photon::Executor *eth) {
    for (int i = 0; i < 10; i++) {
        auto ret = eth->perform<PhotonContext>([] {
            photon::thread_usleep(100 * 1000);
            return 1;
        });
        if (ret == 1) count--;
    }
    return 0;
}

TEST(photon_executor, test) {
    photon::init();
    DEFER(photon::fini());
    photon::Executor eth;

    printf("Task applied, wait for loop\n");

    count = 10000;
    std::vector<photon::join_handle *> ths;
    auto start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < 10; i++) {
        ths.emplace_back(
            photon::thread_enable_join(photon::thread_create11(&ftask, &eth)));
    }
    for (auto const th : ths) {
        photon::thread_join(th);
    }
    auto spent = std::chrono::high_resolution_clock::now() - start;
    auto microsec =
        std::chrono::duration_cast<std::chrono::microseconds>(spent).count();
    printf("10k tasks done, take %ld us, qps=%ld\n", microsec,
           10000L * 1000000 / microsec);
    EXPECT_LE(microsec, 4L * 1000 * 1000);
}

int ftask_auto(photon::Executor *eth) {
    for (int i = 0; i < 10; i++) {
        auto ret = eth->perform<AutoContext>([] {
            photon::thread_usleep(100 * 1000);
            return 1;
        });
        if (ret == 1) count--;
    }
    return 0;
}

TEST(auto_executor, photon) {
    photon::init();
    DEFER(photon::fini());
    photon::Executor eth;

    printf("Task applied, wait for loop\n");

    count = 10000;
    std::vector<photon::join_handle *> ths;
    auto start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < 10; i++) {
        ths.emplace_back(photon::thread_enable_join(
            photon::thread_create11(&ftask_auto, &eth)));
    }
    for (auto const th : ths) {
        photon::thread_join(th);
    }
    auto spent = std::chrono::high_resolution_clock::now() - start;
    auto microsec =
        std::chrono::duration_cast<std::chrono::microseconds>(spent).count();
    printf("10k tasks done, take %ld us, qps=%ld\n", microsec,
           10000L * 1000000 / microsec);
    EXPECT_LE(microsec, 4L * 1000 * 1000);
}

TEST(auto_executor, std) {
    photon::Executor eth;

    printf("Task applied, wait for loop\n");

    count = 10000;
    std::vector<std::thread> ths;
    auto start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < 10; i++) {
        ths.emplace_back(&ftask_auto, &eth);
    }
    for (auto &th : ths) {
        th.join();
    }
    auto spent = std::chrono::high_resolution_clock::now() - start;
    auto microsec =
        std::chrono::duration_cast<std::chrono::microseconds>(spent).count();
    printf("10k tasks done, take %ld us, qps=%ld\n", microsec,
           10000L * 1000000 / microsec);
    EXPECT_LE(microsec, 4L * 1000 * 1000);
}

int wtask(photon::WorkPool *wp) {
    for (int i = 0; i < 10; i++) {
        wp->call<photon::StdContext>([] {
            photon::thread_usleep(100 * 1000);
        });
        count--;
    }
    return 0;
}

TEST(workpool, std) {
    photon::init();
    DEFER(photon::fini());
    // WP will create&dtor in photon env
    photon::WorkPool wp(10);

    printf("Task applied, wait for loop\n");

    count = 10000;
    std::vector<std::thread> ths;
    auto start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < 10; i++) {
        // task will call workpool in std thread
        ths.emplace_back(&wtask, &wp);
    }
    for (auto &th : ths) {
        th.join();
    }
    auto spent = std::chrono::high_resolution_clock::now() - start;
    auto microsec =
        std::chrono::duration_cast<std::chrono::microseconds>(spent).count();
    printf("10k tasks done, take %ld us, qps=%ld\n", microsec,
           10000L * 1000000 / microsec);
    EXPECT_LE(microsec, 4L * 1000 * 1000);
}