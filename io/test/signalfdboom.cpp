#define protected public
#include "io/signalfd.cpp"
#undef protected

#include <csignal>
#include <gtest/gtest.h>
#include <gflags/gflags.h>

using namespace photon;
using namespace std;

static int count = 0;

void handler(int signal) {
    LOG_DEBUG(VALUE(signal));
    count ++;
}

TEST(Boom, boom) {
    sync_signal(SIGCHLD, handler);
    sync_signal(SIGPROF, handler);

    for (int i=0;i < 100;i++) {
        kill(getpid(), SIGCHLD);
        kill(getpid(), SIGPROF);
    }

    thread_usleep(1000*1000);
    EXPECT_EQ(2, count);
}

int main(int argc, char** arg)
{
    LOG_INFO("Set native signal handler");
    init();
    DEFER({fini();});
    auto ret = fd_events_init();
    if (ret != 0)
        LOG_ERROR_RETURN(0, -1, "failed to init fdevents");
    DEFER({fd_events_fini();});
    ret = sync_signal_init();
    if (ret != 0)
        LOG_ERROR_RETURN(0, -1, "failed to init signalfd");
    DEFER({sync_signal_fini();});
    ::testing::InitGoogleTest(&argc, arg);
    google::ParseCommandLineFlags(&argc, &arg, true);
    LOG_DEBUG("test result:`",RUN_ALL_TESTS());
    return 0;
}
