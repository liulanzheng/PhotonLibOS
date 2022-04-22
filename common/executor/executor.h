#pragma once

#include <photon/common/callback.h>
#include <photon/common/executor/stdlock.h>

#include <atomic>
#include <type_traits>

namespace Executor {

constexpr int64_t kCondWaitMaxTime = 1000L * 1000;

struct void_t {};
template <typename T>
struct no_void {
    using type =
        typename std::conditional<std::is_void<T>::value, void_t, T>::type;
};

template <typename Func, typename R = typename std::result_of<Func()>::type>
auto _no_void_ret_func_helper(Func &&func) ->
    typename std::enable_if<std::is_void<R>::value, void_t>::type {
    func();
    return {};
}

template <typename Func, typename R = typename std::result_of<Func()>::type>
auto _no_void_ret_func_helper(Func &&func) ->
    typename std::enable_if<!std::is_void<R>::value, R>::type {
    return func();
}

template <typename R, typename Context>
struct AsyncResult {
    typename no_void<R>::type result;
    int err = 0;
    std::atomic_bool gotit;
    typename Context::Mutex mtx;
    typename Context::Cond cond;
    AsyncResult() : gotit(false), cond(mtx) {}
    typename no_void<R>::type wait_for_result() {
        typename Context::CondLock lock(mtx);
        while (!gotit.load(std::memory_order_acquire)) {
            cond.wait_for(lock, kCondWaitMaxTime);
        }
        if (err) errno = err;
        return std::move(result);
    }
    template <typename T>
    void done(T &&t) {
        typename Context::CondLock lock(mtx);
        result = std::forward<T>(t);
        err = errno;
        gotit.store(true, std::memory_order_release);
        cond.notify_all();
    }
};

class HybridExecutor {
public:
    virtual ~HybridExecutor(){};

    template <typename Context = StdContext, typename Func,
              typename R = typename std::result_of<Func()>::type>
    typename no_void<R>::type perform(Func &&act) {
        AsyncResult<R, Context> aret;
        auto work = [act, &aret, this] {
            if (!aret.gotit.load(std::memory_order_acquire)) {
                aret.done(_no_void_ret_func_helper(act));
            }
            return 0;
        };
        Callback<> cb(work);
        issue(cb);
        return aret.wait_for_result();
    }

protected:
    virtual void issue(Callback<>) = 0;
};

HybridExecutor *new_ease_executor();

}  // namespace Executor
