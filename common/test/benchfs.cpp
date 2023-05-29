// $$PHOTON_UNPUBLISHED_FILE$$

#include <benchmark/benchmark.h>
#include <array>
#include <thread>
#include <unistd.h>
#include <sched.h>

#include <photon/photon.h>
#include <photon/common/alog.h>
#include <photon/common/utility.h>
#include <photon/common/executor/executor.h>
#include <photon/thread/thread.h>
#include <photon/thread/workerpool.h>
#include <photon/fs/filesystem.h>
#include <photon/fs/forwardfs.h>
#include <photon/fs/exportfs.h>

using namespace photon::fs;

class NullFileSystem : public ForwardFS_Ownership {
public:
    NullFileSystem(IFileSystem* fs) : ForwardFS_Ownership(fs, true) {}
    IFile* open(const char *pathname, int flags) override
    {
        return 0;
    }
    int rmdir(const char *pathname) override {
        return 0;
    }

};

IFileSystem * nullfs = nullptr;
IFileSystem * fs = nullptr;
photon::semaphore sem;
std::thread * th = nullptr;

static void MultiThreaded_bench_exportfs(benchmark::State& state)
{
    if (state.thread_index() == 0) {
        // Setup code here.
        th = new std::thread([&]{
            photon::init(photon::INIT_EVENT_EPOLL, 0);
            exportfs_init(32);
            DEFER({
                exportfs_fini();
                photon::fini();
            });
            nullfs = new NullFileSystem(nullptr);
            fs = export_as_sync_fs(nullfs);
            sem.wait(1);
            DEFER({
                delete fs;
                fs = nullptr;
            });
        });

        while (!fs) {::sched_yield();}
    }

    for (auto _: state) {
        //  while (true) {
        fs->rmdir("testname");
        // state.PauseTiming();
        // usleep(1000);
        // state.ResumeTiming();
        //  }
    }

    if (state.thread_index() == 0) {
        // Teardown code here.
        sem.signal(1);
        th->join();
        delete th;
    }

}


photon::WorkPool *pool = nullptr;
auto nullfunc = [](){};

static void MultiThreaded_bench_workpool(benchmark::State& state)
{
    photon::init(photon::INIT_EVENT_EPOLL, 0);

    if (state.thread_index() == 0) {
        if (pool) safe_delete(pool);
        pool = new photon::WorkPool(4, photon::INIT_EVENT_EPOLL, 0, 64);
    }

    for (auto _: state) {
        // while (true) {
        pool->call(nullfunc);
        // state.PauseTiming();
        // usleep(1000);
        // state.ResumeTiming();
        // }
    }


    if (state.thread_index() == 0) {
    }
    photon::fini();
}

photon::Executor *ex = nullptr;
static void MultiThreaded_bench_executor(benchmark::State& state)
{
    if (state.thread_index() == 0) {
        if (ex) safe_delete(ex);
        ex = new photon::Executor(photon::INIT_EVENT_EPOLL, 0);
    }

    for (auto _: state) {
        // while (true) {
        ex->perform(nullfunc);
        // state.PauseTiming();
        // usleep(1000);
        // state.ResumeTiming();
        // }
    }

    if (state.thread_index() == 0) {
    }
}

BENCHMARK(MultiThreaded_bench_exportfs)->ThreadRange(1, 32);
BENCHMARK(MultiThreaded_bench_workpool)->ThreadRange(1, 32);
BENCHMARK(MultiThreaded_bench_executor)->ThreadRange(1, 32);

BENCHMARK_MAIN();