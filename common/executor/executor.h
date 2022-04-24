#pragma once

#include <photon/common/callback.h>
#include <photon/common/executor/stdlock.h>

#include <atomic>
#include <type_traits>

namespace photon {

class ExecutorImpl;

ExecutorImpl *new_ease_executor();
void delete_ease_executor(ExecutorImpl *e);
void issue(ExecutorImpl *e, Delegate<void> cb);

class Executor {
public:
    ExecutorImpl *e = new_ease_executor();
    ~Executor() { delete_ease_executor(e); }

    template <typename Context = StdContext, typename Func,
              typename R = typename std::result_of<Func()>::type,
              typename _ = typename std::enable_if<!std::is_void<R>::value, R>::type>
    R perform(Func &&act) {
        R result;
        AsyncOp<Context> aop;
        auto op = [&]{
            result = act();
            aop.done();
        };
        aop.call(e, op);
        return result;
    }

    template <typename Context = StdContext, typename Func,
              typename R = typename std::result_of<Func()>::type,
              typename _ = typename std::enable_if<std::is_void<R>::value, R>::type>
    void perform(Func &&act) {
        AsyncOp<Context> aop;
        auto op = [&]{
            act();
            aop.done();
        };
        aop.call(e, op);
    }

protected:
    static constexpr int64_t kCondWaitMaxTime = 1000L * 1000;

    template <typename Context>
    struct AsyncOp {
        int err;
        std::atomic_bool gotit;
        typename Context::Mutex mtx;
        typename Context::Cond cond;
        AsyncOp() : gotit(false), cond(mtx) {}
        void wait_for_completion() {
            typename Context::CondLock lock(mtx);
            while (!gotit.load(std::memory_order_acquire)) {
                cond.wait_for(lock, kCondWaitMaxTime);
            }
            if (err) errno = err;
        }
        void done(int error_number = 0) {
            typename Context::CondLock lock(mtx);
            err = error_number;
            gotit.store(true, std::memory_order_release);
            cond.notify_all();
        }
        void call(ExecutorImpl* e, Delegate<void> work) {
            issue(e, work);
            wait_for_completion();
        }
    };
};

}  // namespace photon
