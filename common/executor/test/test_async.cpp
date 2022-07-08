#include <fcntl.h>
#include <gtest/gtest.h>
#include <photon/common/alog.h>
#include <photon/common/executor/executor.h>
#include <photon/common/executor/stdlock.h>
#include <photon/common/utility.h>
#include <photon/fs/exportfs.h>
#include <photon/fs/filesystem.h>
#include <photon/fs/localfs.h>
#include <photon/thread/thread.h>
#include <sched.h>

#include <chrono>
#include <thread>

using namespace photon;

std::atomic<int> count(0), start(0);

static constexpr int th_num = 1;
static constexpr int app_num = 1000;

int ftask(photon::Executor *eth, int i) {
    eth->async_perform(new auto ([i] {
        // sleep for 3 secs
        LOG_INFO("Async work ` start", i);
        start++;
        photon::thread_sleep(3);
        LOG_INFO("Async work ` done", i);
        count++;
    }));
    return 0;
}

TEST(std_executor, test) {
    photon::Executor eth;

    printf("Task applied, wait for loop\n");

    for (int i = 0; i < 10; i++) {
        ftask(&eth, i);
    }
    EXPECT_LT(count.load(), 10);
    while (count.load() != 10) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    EXPECT_EQ(10, start.load());
    EXPECT_EQ(start.load(), count.load());
}

TEST(std_executor, perf) {
    int cnt = 0;
    DEFER(EXPECT_EQ(th_num * app_num, cnt));
    photon::Executor eth;
    auto dura = std::chrono::nanoseconds(0);
    std::vector<std::thread> ths;
    ths.reserve(th_num);
    for (int i = 0; i < th_num; i++) {
        ths.emplace_back([&] {
            for (int i = 0; i < app_num; i++) {
                auto start = std::chrono::high_resolution_clock::now();
                eth.async_perform(new auto ([&] { cnt++; }));
                auto end = std::chrono::high_resolution_clock::now();
                dura = dura + (end - start);
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        });
    }
    for (auto &x : ths) {
        x.join();
    }

    LOG_INFO(
        "Spent ` us for ` task async apply, average ` ns per task",
        std::chrono::duration_cast<std::chrono::microseconds>(dura).count(),
        DEC(th_num * app_num).comma(true),
        std::chrono::duration_cast<std::chrono::nanoseconds>(dura).count() /
            th_num / app_num);
}
