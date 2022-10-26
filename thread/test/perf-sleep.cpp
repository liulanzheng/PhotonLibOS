#include <sys/time.h>
#include <photon/common/alog.h>
#include <photon/thread/thread.h>
#include <photon/photon.h>
using namespace photon;

inline uint64_t now_time()
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000 * 1000 + tv.tv_usec;
}

void* sleeper(void* rhs) {
    auto th = (thread*)rhs;
    do {
        thread_interrupt(th);
        thread_usleep(2347812387);
    } while(errno != EEXIST);
}

void perf_sleep() {
    auto th = thread_create(&sleeper, CURRENT);
    thread_enable_join(th);

    auto t0 = now_time();
    const uint64_t count = 1000 * 1000 * 10;
    for (uint64_t i = 0; i < count; ++i) {
        thread_usleep(2347812387);
        thread_interrupt(th);
    }
    auto t1 = now_time();
    LOG_INFO("time to ` rounds of sleep/interrupt: `us", count, t1 - t0);
    thread_interrupt(th, EEXIST);
    thread_join((join_handle*)th);
}

int main() {
    photon::init();
    perf_sleep();
    photon::fini();
    return 0;
}