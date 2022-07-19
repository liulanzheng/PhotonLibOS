#include "fd-events.h"
#include <inttypes.h>
#include <unistd.h>
#include <sys/event.h>
#include <photon/common/alog.h>

namespace photon {

class KQueue : public MasterEventEngine,
               public CascadingEventEngine {
public:
    struct kevent _events[32];
    int _kq = -1;
    uint32_t _n = 0;    // # of events to submit

    int init() {
        if (_kq >= 0)
            LOG_ERROR_RETURN(EALREADY, -1, "already init-ed");

        _kq = kqueue();
        if (_kq < 0)
            LOG_ERRNO_RETURN(0, -1, "failed to create kqueue()");

        if (enqueue(_kq, EVFILT_USER, EV_ADD | EV_CLEAR, 0, true) < 0) {
            DEFER({ close(_kq); _kq = -1; });
            LOG_ERRNO_RETURN(0, -1, "failed to setup self-wakeup EVFILT_USER event by kevent()");
        }
        return 0;
    }

    virtual ~KQueue() {
        assert(_n == 0);
        if (_kq >= 0)
            close(_kq);
    }

    int enqueue(int fd, short event, uint16_t action, void* udata, bool immediate = false) {
        assert(_n < LEN(_events));
        auto entry = &_events[_n++];
        EV_SET(entry, fd, event, action, 0, 0, udata);
        if (immediate || _n == LEN(_events)) {
            int ret = kevent(_kq, _events, _n, nullptr, 0, nullptr);
            if (ret < 0)
                LOG_ERRNO_RETURN(0, -1, "failed to submit ` events with kevent()");
            _n = 0;
        }
        return 0;
    }

    virtual int wait_for_fd(int fd, uint32_t interests, uint64_t timeout) override {
        short ev = (interests == EVENT_READ) ? EVFILT_READ : EVFILT_WRITE;
        enqueue(fd, ev, EV_ADD | EV_ONESHOT, CURRENT);
        int ret = thread_usleep(timeout);
        ERRNO err;
        if (ret == -1 && err.no == EOK) {
            return 0;  // event arrived
        }

        enqueue(fd, ev, EV_DELETE, CURRENT, true); // immediately
        errno = (ret == 0) ? ETIMEDOUT : err.no;
        return -1;
    }

    virtual ssize_t wait_and_fire_events(uint64_t timeout = -1) override {
        ssize_t nev = 0;
        struct timespec tm;
        tm.tv_sec = timeout / 1024 / 1024;
        tm.tv_nsec = (timeout % (1024 * 1024)) * 1024;

    again:
        int ret = kevent(_kq, _events, _n, _events, LEN(_events), &tm);
        if (ret < 0)
            LOG_ERRNO_RETURN(0, -1, "failed to call kevent()");

        _n = 0;
        nev += ret;
        for (int i = 0; i < ret; ++i) {
            auto th = (thread*) _events[i].udata;
            if (th) thread_interrupt(th, EOK);
        }
        if (ret == LEN(_events)) {  // there may be more events
            tm.tv_sec = tm.tv_nsec = 0;
            goto again;
        }
        return nev;
    }

    virtual int cancel_wait() override {
        enqueue(_kq, EVFILT_USER, EV_ADD | EV_ONESHOT, 0, true);
        return 0;
    }

    virtual int add_interest(Event e) override {
        int ret;
        if (e.interests & EVENT_READ)
            ret = enqueue(e.fd, EVFILT_READ, EV_ADD, e.data);
        if (!ret && e.interests & EVENT_WRITE)
            ret = enqueue(e.fd, EVFILT_WRITE, EV_ADD, e.data);
        if (!ret && e.interests & EVENT_ERROR)
            ret = enqueue(e.fd, EVFILT_EXCEPT, EV_ADD, e.data);
        return ret;
    }
    virtual int rm_interest(Event e) override {
        int ret;
        if (e.interests & EVENT_READ)
            ret = enqueue(e.fd, EVFILT_READ, EV_DELETE, e.data);
        if (!ret && e.interests & EVENT_WRITE)
            ret = enqueue(e.fd, EVFILT_WRITE, EV_DELETE, e.data);
        if (!ret && e.interests & EVENT_ERROR)
            ret = enqueue(e.fd, EVFILT_EXCEPT, EV_DELETE, e.data);
        return ret;
    }

    virtual ssize_t wait_for_events(void** data,
            size_t count, uint64_t timeout = -1) override {
        wait_for_fd_readable(_kq, timeout);
        if (count > LEN(_events))
            count = LEN(_events);
        int ret = kevent(_kq, _events, _n, _events, count, 0);
        if (ret < 0)
            LOG_ERRNO_RETURN(0, -1, "failed to call kevent()");

        _n = 0;
        assert(ret <= count);
        for (int i = 0; i < ret; ++i) {
            data[i] = _events[i].udata;
        }
        return ret;
    }
};

__attribute__((noinline))
KQueue* new_kqueue_engine() {
    return NewObj<KQueue>()->init();
}

MasterEventEngine* new_kqueue_master_engine() {
    return new_kqueue_engine();
}

CascadingEventEngine* new_kqueue_cascading_engine() {
    return new_kqueue_engine();
}


} // namespace photon
