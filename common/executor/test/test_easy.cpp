#include <fcntl.h>
#include <gtest/gtest.h>

#include <photon/common/alog.h>
#include <photon/fs/filesystem.h>
#include <photon/fs/localfs.h>
#include <photon/common/utility.h>
#include <photon/common/executor/executor.h>
#include <photon/common/executor/easylock.h>
#include <photon/thread/thread.h>

using namespace photon;

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
        easy_pool_destroy(pool);
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
photon::semaphore sem(128);

int ftask(easy_baseth_t *, easy_task_t *task) {
    auto eth = (Executor::HybridEaseExecutor *)task->user_data;
    auto ret = eth->perform<Executor::EasyContext>([] {
        sem.wait(1);
        DEFER(sem.signal(1));
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
    if (ret == 1) easy_atomic_dec(&count);
    return EASY_OK;
}

TEST(easy_executor, test) {
    EasyCoroutinePool ecp;
    auto eth = Executor::new_ease_executor();
    DEFER(delete eth);

    printf("Task applied, wait for loop\n");

    easy_atomic_set(count, 10000);
    auto start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < 10000; i++) {
        ecp.runtask(&ftask, (void *)eth, i);
    }
    while (count) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    auto spent = std::chrono::high_resolution_clock::now() - start;
    auto microsec =
        std::chrono::duration_cast<std::chrono::microseconds>(spent).count();
    printf("10k tasks done, take %ld us, qps=%ld\n", microsec,
           10000L * 1000000 / microsec);
}
