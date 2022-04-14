#include <fcntl.h>
#include <gtest/gtest.h>

#include <photon/common/alog.h>
#include <photon/fs/exportfs.h>
#include <photon/fs/filesystem.h>
#include <photon/fs/localfs.h>
#include <photon/common/utility.h>
#include <photon/common/executor/executor.h>
#include <photon/common/executor/stdlock.h>

using namespace photon;

std::atomic<int> count;

int ftask(Executor::HybridEaseExecutor *eth) {
    for (int i = 0; i < 1000; i++) {
        auto ret = eth->perform([] {
            auto fs = fs::new_localfs_adaptor();
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

int exptask(fs::IFileSystem *fs) {
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
        fs::exportfs_init();
        auto local = fs::new_localfs_adaptor();
        return fs::export_as_sync_fs(local);
    });
    ASSERT_NE(nullptr, fs);
    DEFER(eth.perform([&fs] {
        delete fs;
        fs::exportfs_fini();
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
//             auto fs = fs::new_localfs_adaptor();
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
