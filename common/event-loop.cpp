#include "event-loop.h"
#include "../thread/thread.h"
using namespace photon;

class EventLoopImpl : public EventLoop
{
public:
    photon::condition_variable m_cond;
    Wait4Events m_wait;
    OnEvents m_on_event;

    EventLoopImpl(Wait4Events wait, OnEvents on_event) :
        m_wait(wait), m_on_event(on_event) { }

    virtual ~EventLoopImpl()
    {
        stop();
    }

    virtual void run() override {
        m_state = RUNNING;
        __run();
    }
    virtual void async_run() override
    {
        if (m_state != STOP)
            return;
        m_state = RUNNING;
        m_thread = thread_create(&_run, this);
    }
    static void* _run(void* loop)
    {
        static_cast<EventLoopImpl*>(loop)->__run();
        return nullptr;
    }
    void __run()
    {
        while(m_state == RUNNING)
        {
            m_state = WAITING;
            while(true)
            {
                int ret = m_wait(this);
                if (ret < 0)
                    goto exit;
                if (ret > 0)
                    break;
            }
            m_state = RUNNING;
            m_on_event(this);
        }

    exit:
        m_state = STOP;
        m_cond.notify_all();
    }

    virtual void stop() override
    {
        if (m_state <= STOP)
            return;

        auto state = m_state;
        m_state = STOPPING;
        if (state == WAITING && m_thread != nullptr)
            thread_interrupt(m_thread);

        while (m_state != STOP)
            m_cond.wait_no_lock();
    }
};

EventLoop* new_event_loop(EventLoop::Wait4Events wait,
                          EventLoop::OnEvents on_event)
{
    return new EventLoopImpl(wait, on_event);
}
