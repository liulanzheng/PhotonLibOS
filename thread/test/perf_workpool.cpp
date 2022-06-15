#include <photon/common/alog.h>
#include <photon/photon.h>
#include <photon/thread/workerpool.h>
#include <sys/fcntl.h>
#include <unistd.h>

#include <thread>
#include <vector>

photon::WorkPool* pool;

void* task(void*) {
    for (int i = 0; i < 10000; i++) {
        pool->call([] {
            char buffer[4096];
            int fd = ::open("/dev/zero", O_RDONLY);
            DEFER(::close(fd));
            pread(fd, buffer, 4096, 0);
        });
    }
    return nullptr;
}

void* norm(void*) {
    for (int i = 0; i < 10000; i++) {
        char buffer[4096];
        int fd = ::open("/dev/zero", O_RDONLY);
        photon::thread_yield();
        DEFER(::close(fd));
        pread(fd, buffer, 4096, 0);
        photon::thread_yield();
    }
    return nullptr;
}

int main() {
    photon::init(0, 0);
    DEFER(photon::fini());
    pool = photon::new_work_pool(8);
    DEFER(delete pool);
    auto start = photon::now;
    photon::threads_create_join(100, task, nullptr);
    auto end = photon::now;
    LOG_INFO("COPY from zero 4GB in 100 paralles work in 8 threads ", VALUE(end - start));
    start = photon::now;
    photon::threads_create_join(100, norm, nullptr);
    end = photon::now;
    LOG_INFO("COPY from zero 4GB in 100 paralles work in 1 thread ", VALUE(end - start));
    return 0;
}