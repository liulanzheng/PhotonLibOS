#include <fcntl.h>
#include <gtest/gtest.h>

#include "fs/async_filesystem.h"
#include "fs/exportfs.h"
#include "fs/filesystem.h"
#include "fs/localfs.h"
#include "io/fd-events.h"
#include "thread/thread.h"
#include "common/executor/executor.h"
#include "common/executor/easylock.h"

class EasyCoroutinePool {
    easy_uthread_control_t euc;
    easy_atomic_t ccnt;
    easy_pool_t *pool;
    easy_io_t *eio;
    easy_thread_pool_t *gtp;

public:
    EasyCoroutinePool(int threads = 4) {
        pool = easy_pool_create(0);
        pool->flags = 1;
        eio = (easy_io_t *)easy_pool_calloc(pool, sizeof(easy_io_t));
        eio->started = 1;
        eio->pool = pool;
        easy_list_init(&eio->thread_pool_list);
        gtp = easy_coroutine_pool_create(eio, threads, NULL, NULL);
        easy_baseth_pool_start(gtp);
    }
    ~EasyCoroutinePool() {
        easy_baseth_pool_stop(gtp);
        easy_baseth_pool_wait(gtp);
        easy_baseth_pool_destroy(gtp);
        easy_eio_destroy(eio);
    }
    void runtask(easy_task_process_pt t, void *arg, int hv) {
        auto task = (easy_task_t *)easy_pool_calloc(pool, sizeof(easy_task_t));
        task->istask = 1;
        task->user_data = arg;
        easy_coroutine_process_set(task, t);
        easy_thread_pool_addin(gtp, task, hv);
    }
    void wait() { easy_baseth_pool_wait(gtp); }
};

easy_atomic_t count;

int ftask(easy_baseth_t *, easy_task_t *task) {
    auto fs = (FileSystem::IFileSystem *)(task->user_data);
    auto file = fs->open("/etc/hosts", O_RDONLY);
    if (file == nullptr) {
        return EASY_ABORT;
    } else {
        DEFER(delete file);
        struct stat stat;
        auto ret = file->fstat(&stat);
        if (ret == 0) easy_atomic_dec(&count);
    }
    return EASY_OK;
}

static FileSystem::IFileSystem *fs = nullptr;

TEST(easy_performer, test) {
    easy_atomic_set(count, 10000);

    std::thread([]() {
        photon::init();
        photon::fd_events_init();
        FileSystem::exportfs_init();

        fs = FileSystem::export_as_easy_sync_fs(
            FileSystem::new_localfs_adaptor());
        DEFER(delete fs);

        while (count) {
            photon::thread_usleep(100UL);
        }

        FileSystem::exportfs_fini();
        photon::fd_events_fini();
    }).detach();

    EasyCoroutinePool ecp;

    while (fs == nullptr) {
        std::this_thread::sleep_for(std::chrono::microseconds(100));
    }

    printf("all ready\n");

    auto start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < 10000; i++) {
        ecp.runtask(&ftask, (void *)fs, i);
    }
    while (count) {
        std::this_thread::sleep_for(std::chrono::microseconds(100));
    }
    auto spent = std::chrono::high_resolution_clock::now() - start;
    auto microsec =
        std::chrono::duration_cast<std::chrono::microseconds>(spent).count();
    printf("10k tasks done, take %ld us, qps=%ld\n", microsec,
           10000L * 1000000 / microsec);
}