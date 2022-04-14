#pragma once
#include <utility>

#include <photon/common/alog.h>
#include <photon/common/utility.h>

#define AU_FILEOP(pathname, offset, size)   \
    make_named_value("pathname", pathname), \
        make_named_value("offset", offset), make_named_value("size", size)

#define AU_SOCKETOP(ep) make_named_value("endpoint", ep)

#ifndef DISABLE_AUDIT
#define SCOPE_AUDIT(...)                                                       \
    auto _CONCAT(__audit_start_time__, __LINE__) = photon::now;                \
    DEFER(LOG_AUDIT(__VA_ARGS__,                                               \
                    make_named_value(                                          \
                        "latency", photon::now - _CONCAT(__audit_start_time__, \
                                                         __LINE__))));

#define SCOPE_AUDIT_THRESHOLD(threshold, ...)                                 \
    auto _CONCAT(__audit_start_time__, __LINE__) = photon::now;               \
    DEFER({                                                                   \
        auto latency = photon::now - _CONCAT(__audit_start_time__, __LINE__); \
        if (latency >= (threshold)) {                                         \
            LOG_AUDIT(__VA_ARGS__, make_named_value("latency", latency));     \
        }                                                                     \
    });

#else
#define SCOPE_AUDIT(...)
#define SCOPE_AUDIT_THRESHOLD(...)
#endif
