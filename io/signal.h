#pragma once
#include <signal.h>


// sync_signal will be executed sequentially in a dedicated photon thread
namespace photon
{
    extern "C" int sync_signal_init();

    // block all default action to signals
    // except SIGSTOP & SIGKILL
    extern "C" int block_all_signal();

    // set signal handler for signal `signum`, default to SIG_IGN
    // the handler is always restartable, until removed by setting to SIG_IGN
    extern "C" sighandler_t sync_signal(int signum, sighandler_t handler);

    // set signal handler for signal `signum`, default to SIG_IGN
    // the handler is always restartable, until removed by setting to SIG_IGN
    // `sa_mask` and most flags of `sa_flags` are ingored
    extern "C" int sync_sigaction(int signum,
        const struct sigaction *act, struct sigaction *oldact);

    extern "C" int sync_signal_fini();
}
