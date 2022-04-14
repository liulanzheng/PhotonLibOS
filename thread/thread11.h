#pragma once
#include <tuple>
#include <utility>
#include <photon/thread/thread.h>
#include <photon/common/PMF.h>
#include <photon/common/utility.h>
#include <photon/common/tuple-assistance.h>
#include <photon/common/callback.h>

namespace photon
{
    template<typename F>
    struct ThreadContext11__ : public tuple_assistance::callable<F>
    {
        using base = tuple_assistance::callable<F>;
        using typename base::return_type;
        using typename base::arguments;
        using base::apply;

        F start;
        uint64_t stack_size;
        arguments args;
        bool got_it = false;
        thread* parent;

        template<typename...ARGUMENTS>
        ThreadContext11__(uint64_t stack_size, F f, ARGUMENTS&&...args_) :
            start(f), stack_size(stack_size), args{std::forward<ARGUMENTS>(args_)...}
        {
            parent = CURRENT;
//            LOG_DEBUG("arguments stored in tuple");
        }

        static return_type stub11(void* args)
        {
            typedef ThreadContext11__ Context;
            auto ctx_ = (Context*)args;
            Context ctx = std::move(*ctx_);
//            LOG_DEBUG("arguments tuple moved");
            ctx_->got_it = true;
            thread_yield_to(ctx.parent);
            return base::apply(ctx.start, ctx.args);
        }
        thread* thread_create(thread_entry start)
        {
            auto th = ::photon::thread_create(start, this, stack_size);
            thread_yield_to(th);
            return th;
        }
    };

    template<typename F>
    struct ThreadContext11 : public ThreadContext11__<F>
    {
        typedef ThreadContext11__<F> base;
        using base::base;
        using base::stub11;
        static void* stub(void* args)
        {
            stub11(args);
            return nullptr;
        }
        thread* thread_create()
        {
            return base::thread_create(&stub);
        }
    };

    template<typename...PARAMETERS>
    struct ThreadContext11<void* (*)(PARAMETERS...)> :
        public ThreadContext11__<void* (*)(PARAMETERS...)>
    {
        typedef void* (*F)(PARAMETERS...);
        typedef ThreadContext11__<F> base;
        using base::base;
        using base::stub11;
        static void* stub(void* args)
        {
            return stub11(args);
        }
        thread* thread_create()
        {
            return base::thread_create(&stub);
        }
    };

    #define ENABLE_IF_PF(F) ENABLE_IF(is_function_pointer<F>::value)

    template<typename F, ENABLE_IF_PF(F), typename...ARGUMENTS>
    inline thread* thread_create11(uint64_t stack_size, F f, ARGUMENTS&&...args)
    {
        return ThreadContext11<F>(stack_size, f, std::forward<ARGUMENTS>(args)...).thread_create();
    }

    template<typename F, ENABLE_IF_PF(F), typename...ARGUMENTS>
    inline thread* thread_create11(F f, ARGUMENTS&&...args)
    {
        return thread_create11(DEFAULT_STACK_SIZE, f, std::forward<ARGUMENTS>(args)...);
    }

    #define ENABLE_IF_PMF(F) typename __P__ = typename std::enable_if<std::is_member_function_pointer<F>::value>::type

    template<typename CLASS, typename F, ENABLE_IF_PMF(F), typename...ARGUMENTS>
    inline thread* thread_create11(uint64_t stack_size, F f, CLASS* obj, ARGUMENTS&&...args)
    {
        auto pmf = ::get_member_function_address(obj, f);
        return thread_create11(stack_size, pmf.f, pmf.obj, std::forward<ARGUMENTS>(args)...);
    }

    template<typename CLASS, typename F, ENABLE_IF_PMF(F), typename...ARGUMENTS>
    inline thread* thread_create11(F f, CLASS* obj, ARGUMENTS&&...args)
    {
        return thread_create11(DEFAULT_STACK_SIZE, f, obj, std::forward<ARGUMENTS>(args)...);
    }

    template<typename Callable>
    inline int thread_usleep_defer(uint64_t timeout, Callable&& callback) {
        Delegate<void> delegate(callback);
        return thread_usleep_defer(timeout, delegate._func, delegate._obj);
    }

    // =============================================================================
    class __Example_of_Thread11__
    {
    public:
        void* member_function(int, char) { return nullptr; }
        int const_member_function(int, char, double) const { return -1; }
        static long any_return_type(int, char) { return 0; }

        void asdf()
        {
            int a; char b;
            auto func = &__Example_of_Thread11__::member_function;
            auto cfunc = &__Example_of_Thread11__::const_member_function;

            // thread { return this->func(a, b); }
            thread_create11(func, this, a, b);

            // thread { return this->cfunc(a, b, 3.14); }
            thread_create11(cfunc, this, a, b, 3.14);

            // thread(stack_size = 64KB) { return this->func(1, 'a'); }
            thread_create11(/* stack size */ 64*1024, func, this, 1, 'a');

            // thread { any_return_type(a, b); return nullptr; }
            thread_create11(&any_return_type, a, b);

            // thread(stack_size = 64KB) { any_return_type(1, 'a'); return nullptr; }
            thread_create11(/* stack size */ 64*1024, &any_return_type, 1, 'a');

            thread_yield();
        }
    };
}
