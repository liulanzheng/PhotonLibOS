#pragma once

namespace photon {

// a helper class to translate events into underlay representation
template<int UNDERLAY_EVENT_READ_,
        int UNDERLAY_EVENT_WRITE_,
        int UNDERLAY_EVENT_ERROR_>
struct EventsMap {
    const static int UNDERLAY_EVENT_READ = UNDERLAY_EVENT_READ_;
    const static int UNDERLAY_EVENT_WRITE = UNDERLAY_EVENT_WRITE_;
    const static int UNDERLAY_EVENT_ERROR = UNDERLAY_EVENT_ERROR_;
    static_assert(UNDERLAY_EVENT_READ != UNDERLAY_EVENT_WRITE, "...");
    static_assert(UNDERLAY_EVENT_READ != UNDERLAY_EVENT_ERROR, "...");
    static_assert(UNDERLAY_EVENT_ERROR != UNDERLAY_EVENT_WRITE, "...");
    static_assert(UNDERLAY_EVENT_READ, "...");
    static_assert(UNDERLAY_EVENT_WRITE, "...");
    static_assert(UNDERLAY_EVENT_ERROR, "...");

    int ev_read, ev_write, ev_error;

    EventsMap(int event_read, int event_write, int event_error) {
        ev_read = event_read;
        ev_write = event_write;
        ev_error = event_error;
        assert(ev_read);
        assert(ev_write);
        assert(ev_error);
        assert(ev_read != ev_write);
        assert(ev_read != ev_error);
        assert(ev_error != ev_write);
    }

    int translate_bitwisely(int events) const {
        int ret = 0;
        if (events & ev_read)
            ret |= UNDERLAY_EVENT_READ;
        if (events & ev_write)
            ret |= UNDERLAY_EVENT_WRITE;
        if (events & ev_error)
            ret |= UNDERLAY_EVENT_ERROR;
        return ret;
    }

    int translate_byval(int event) const {
        if (event == ev_read)
            return UNDERLAY_EVENT_READ;
        if (event == ev_write)
            return UNDERLAY_EVENT_WRITE;
        if (event == ev_error)
            return UNDERLAY_EVENT_ERROR;
    }
};

}
