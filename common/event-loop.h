#pragma once
#include "photon/common/callback.h"
#include "photon/common/object.h"

namespace photon {
class thread;
};

class EventLoop : public Object {
public:
    const static int STOP = 0;
    const static int RUNNING = 1;
    const static int WAITING = 2;
    const static int STOPPING = -1;

    // return value > 0 indicates there is (are) event(s)
    // return value = 0 indicates there is still no event
    // return value < 0 indicates interrupted
    using Wait4Events = Callback<EventLoop*>;

    // return value is ignored
    using OnEvents = Callback<EventLoop*>;

    // run event loop and block current photon-thread, till the loop stopped
    virtual void run() = 0;
    // run event loop in new photon thread, so that will not block thread
    virtual void async_run() = 0;
    virtual void stop() = 0;

    int state() { return m_state; }

    photon::thread* loop_thread() { return m_thread; }

protected:
    EventLoop() {}  // not allowed to directly construct
    photon::thread* m_thread = nullptr;
    int m_state = STOP;
};

extern "C" EventLoop* new_event_loop(EventLoop::Wait4Events wait,
                                     EventLoop::OnEvents on_event);
