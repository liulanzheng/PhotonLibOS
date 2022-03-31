#include <fcntl.h>
#include <gtest/gtest.h>

#include "common/alog.h"
#include "fs/exportfs.h"
#include "fs/filesystem.h"
#include "fs/localfs.h"
#include "common/utility.h"
#include "common/executor/executor.h"
#include "common/executor/stdlock.h"

std::atomic<int> count;

int ftask(Executor::HybridEaseExecutor *eth) {
    for (int i = 0; i < 1000; i++) {
        auto ret = eth->perform([] {
            auto fs = FileSystem::new_localfs_adaptor();
            if (!fs) return -1;
            DEFER(delete fs);
            auto file = fs->open("/etc/hosts", O_RDONLY);
            if (!file) return -1;
            DEFER(delete file);
            struct stat stat;
            auto ret = file->fstat(&stat);
            EXPECT_EQ(0, ret);
            return 1;
        });
        if (ret == 1) count--;
    }
    return 0;
}

TEST(std_executor, test) {
    Executor::HybridEaseExecutor eth;

    printf("Task applied, wait for loop\n");

    count = 10000;
    std::vector<std::thread> ths;
    auto start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < 10; i++) {
        ths.emplace_back(std::thread(&ftask, &eth));
    }
    for (auto &th : ths) {
        th.join();
    }
    auto spent = std::chrono::high_resolution_clock::now() - start;
    auto microsec =
        std::chrono::duration_cast<std::chrono::microseconds>(spent).count();
    printf("10k tasks done, take %ld us, qps=%ld\n", microsec,
           10000L * 1000000 / microsec);
}

int exptask(FileSystem::IFileSystem *fs) {
    for (int i = 0; i < 1000; i++) {
        auto file = fs->open("/etc/hosts", O_RDONLY);
        EXPECT_NE(nullptr, file);
        DEFER(delete file);
        struct stat stat;
        auto ret = file->fstat(&stat);
        EXPECT_EQ(0, ret);
        count--;
    }
    return 0;
}

TEST(std_executor, with_exportfs) {
    Executor::HybridEaseExecutor eth;

    auto fs = eth.perform([] {
        FileSystem::exportfs_init();
        auto local = FileSystem::new_localfs_adaptor();
        return FileSystem::export_as_sync_fs(local);
    });
    ASSERT_NE(nullptr, fs);
    DEFER(eth.perform([&fs] {
        delete fs;
        FileSystem::exportfs_fini();
    }));

    printf("Task applied, wait for loop\n");

    count = 10000;
    std::vector<std::thread> ths;
    auto start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < 10; i++) {
        ths.emplace_back(std::thread(&exptask, fs));
    }
    for (auto &th : ths) {
        th.join();
    }
    auto spent = std::chrono::high_resolution_clock::now() - start;
    auto microsec =
        std::chrono::duration_cast<std::chrono::microseconds>(spent).count();
    printf("10k tasks done, take %ld us, qps=%ld\n", microsec,
           10000L * 1000000 / microsec);
}

// int astask(Executor::HybridEaseExecutor *eth) {
//     for (int i = 0; i < 1000; i++) {
//         auto ret = eth->async_perform([] {
//             auto fs = FileSystem::new_localfs_adaptor();
//             if (!fs) return -1;
//             DEFER(delete fs);
//             auto file = fs->open("/etc/hosts", O_RDONLY);
//             if (!file) return -1;
//             DEFER(delete file);
//             struct stat stat;
//             auto ret = file->fstat(&stat);
//             EXPECT_EQ(0, ret);
//             return 1;
//         });
//         count--;
//     }
//     return 0;
// }

// TEST(std_executor, async_perform) {
//     Executor::HybridEaseExecutor eth;
//     printf("Task applied, wait for loop\n");

//     count = 10000;
//     std::vector<std::thread> ths;
//     auto start = std::chrono::high_resolution_clock::now();
//     for (int i = 0; i < 10; i++) {
//         ths.emplace_back(std::thread(&astask, &eth));
//     }
//     for (auto &th : ths) {
//         th.join();
//     }
//     auto spent = std::chrono::high_resolution_clock::now() - start;
//     auto microsec =
//         std::chrono::duration_cast<std::chrono::microseconds>(spent).count();
//     printf("10k tasks done, take %ld us, qps=%ld\n", microsec,
//            10000L * 1000000 / microsec);
// }