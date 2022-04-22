#pragma once

#include <photon/common/callback.h>
#include <photon/common/executor/stdlock.h>

#include <atomic>
#include <type_traits>

namespace Executor {

class ExecutorImpl;

ExecutorImpl *new_ease_executor();
void delete_ease_executor(ExecutorImpl *e);
void issue(ExecutorImpl *e, Callback<> cb);

class Executor {
protected:
    static constexpr int64_t kCondWaitMaxTime = 1000L * 1000;

    struct void_t {};
    template <typename T>
    struct no_void {
        using type =
            typename std::conditional<std::is_void<T>::value, void_t, T>::type;
    };

    template <typename Func, typename R = typename std::result_of<Func()>::type>
    static auto _no_void_ret_func_helper(Func &&func) ->
        typename std::enable_if<std::is_void<R>::value, void_t>::type {
        func();
        return {};
    }

    template <typename Func, typename R = typename std::result_of<Func()>::type>
    static auto _no_void_ret_func_helper(Func &&func) ->
        typename std::enable_if<!std::is_void<R>::value, R>::type {
        return func();
    }

public:
    ExecutorImpl *e;
    Executor() : e(new_ease_executor()) {}
    ~Executor() { delete_ease_executor(e); }

    template <typename Context = StdContext, typename Func,
              typename R = typename std::result_of<Func()>::type>
    typename no_void<R>::type perform(Func &&act) {
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
            void done(typename no_void<R>::type &&t) {
                typename Context::CondLock lock(mtx);
                result = std::move(t);
                err = errno;
                gotit.store(true, std::memory_order_release);
                cond.notify_all();
            }
        } aret;
        auto work = [act, &aret] {
            if (!aret.gotit.load(std::memory_order_acquire)) {
                aret.done(_no_void_ret_func_helper(act));
            }
            return 0;
        };
        Callback<> cb(work);
        issue(e, cb);
        return aret.wait_for_result();
    }
};

}  // namespace Executor
