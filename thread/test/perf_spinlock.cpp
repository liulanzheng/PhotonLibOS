#include <unistd.h>
#include <thread>
#include <vector>
#include <atomic>
#include <pthread.h>

#include <photon/photon.h>
#include <photon/thread/thread11.h>
#include <photon/common/alog.h>

const size_t vcpu_num = 8;
std::atomic<std::uint64_t> g_count{0};

static void set_affinity(size_t index) {
    cpu_set_t cpuset;
    pthread_t thread = pthread_self();
    CPU_ZERO(&cpuset);
    CPU_SET(index, &cpuset);
    if (pthread_setaffinity_np(thread, sizeof(cpuset), &cpuset) != 0) {
        LOG_ERRNO_RETURN(0, , "fail to set affinity");
    }
}

static void show_qps(size_t index) {
    while (true) {
        sleep(1);
        LOG_INFO("qps `", g_count.load());
        g_count.store(0);
    }
}

void basic_lock_unlock() {
    photon::init(0, 0);
    auto lock = new photon::spinlock();

    auto run_worker = [&](size_t index) {
        set_affinity(index);
        photon::init(0, 0);
        LOG_INFO("worker start running ...");
        while (true) {
            lock->lock();
            ++g_count;
            lock->unlock();
        }
    };

    for (size_t i = 0; i < vcpu_num; ++i) {
        new std::thread(run_worker, i);
    }
    set_affinity(vcpu_num);
    new std::thread(show_qps, vcpu_num + 1);

    photon::thread_sleep(-1);
}

void semaphore() {
    photon::init(0, 0);
    auto sem = new photon::semaphore;

    auto run_worker = [&](size_t index) {
        set_affinity(index);
        photon::init(0, 0);
        LOG_INFO("worker start running ...");
        while (true) {
            sem->signal(1);
        }
    };

    for (size_t i = 0; i < vcpu_num; ++i) {
        new std::thread(run_worker, i);
    }

    set_affinity(vcpu_num);
    new std::thread(show_qps, vcpu_num + 1);

    while (true) {
        sem->wait(1);
        g_count++;
    }
}

void fairness() {
    photon::init(0, 0);
    auto lock = new photon::spinlock();
    std::vector<size_t> count_slot;
    for (size_t i = 0; i < vcpu_num; ++i) {
        count_slot.push_back(0);
    }

    auto run_worker = [&](size_t index) {
        set_affinity(index);
        photon::init(0, 0);
        LOG_INFO("worker start running ...");
        while (true) {
            lock->lock();
            count_slot[index] += 1;
            lock->unlock();
        }
    };

    for (size_t i = 0; i < vcpu_num; ++i) {
        new std::thread(run_worker, i);
    }
    set_affinity(vcpu_num);

    photon::thread_sleep(10);
    for (size_t i = 0; i < vcpu_num; ++i) {
        LOG_INFO("slot `, count `", i, count_slot[i]);
    }
}

int main() {
    basic_lock_unlock();
    // semaphore();
    // fairness();
}