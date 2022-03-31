#pragma once
#include <stdint.h>
#include <inttypes.h>
#include <atomic>

struct __perf_counter;

extern "C"
{
    void init_perf_counter(__perf_counter* pc);
}

enum PerfType
{
    TOTAL = 0,
    ACCUMULATE,
    AVERAGE,
};

#define DEFAULT_INTERVAL 5

struct __perf_counter
{
    const char* _name;
    std::atomic<uint64_t> _value;
    const PerfType _type;
    const int _name_length;
    const int _interval;
    __perf_counter* _next;
    uint64_t _last;

    template<int N>
    constexpr __perf_counter(const char (&name)[N], PerfType type,
        int interval = DEFAULT_INTERVAL) : _name(name), _value(0),
            _type(type), _name_length(N - 1), _interval(interval), _last(0)
    {
        static_assert(sizeof(_type) == 4, "...");
    }

    void add(uint64_t dx)
    {
#ifdef PERF
        if (_type == AVERAGE)
            dx = (dx << 24) + 1;
        _value.fetch_add(dx, std::memory_order_relaxed);
#endif
    }
};

struct __PCR
{
    __PCR(__perf_counter* pc)
    {
#ifdef PERF
        init_perf_counter(pc);
#endif
    }
};

#define REGISTER_PERF(NAME, ...) \
    static __perf_counter __g_PC_##NAME(#NAME, __VA_ARGS__); \
    static __PCR __pcr##NAME( &__g_PC_##NAME );

#define REPORT_PERF(NAME, VALUE) __g_PC_##NAME.add(VALUE);
