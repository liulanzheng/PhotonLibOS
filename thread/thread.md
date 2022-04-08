# 高效轻量的用户态线程库Photon

## 主要API（photon/thread.h）
### 线程操作
```cpp
namespace photon
{
    int init();
    int fini();

    struct timer;
    struct thread;
    extern thread* CURRENT;
    extern uint64_t now;

    enum states
    {
        READY = 0,
        RUNNING = 1,
        WAITING = 2,
        DONE = 3,
    };

    typedef void* (*thread_entry)(void*);
    const uint64_t DEFAULT_STACK_SIZE = 8 * 1024 * 1024;
    thread* thread_create(thread_entry start, void* arg,
    uint64_t stack_size = DEFAULT_STACK_SIZE);

    // threads are join-able *only* through their join_handle
    struct join_handle;
    join_handle* thread_enable_join(thread* th);
    void thread_join(join_handle* jh);

    // switching to other threads *without* resuming sleepers, event if it's time to
    void thread_yield();

    // suspend CURRENT thread for specified time duration, and switch
    // control to other threads, resuming possible sleepers
    int thread_usleep(uint64_t useconds);
    int thread_sleep(uint64_t seconds);

    void thread_suspend();

    states thread_stat(thread* th = CURRENT);
    void thread_interrupt(thread* th, int error_number = EINTR);
    void thread_resume(thread* th);
}
```

### Timer
```cpp
namespace photon
{
    // the prototype of timer callback function
    typedef uint64_t (*timer_entry)(void*);

    // if set, the timer will fire repeatedly, using `timer_entry` return val as the next timeout
    // if the return val is 0, use `default_timedout` as the next timedout instead;
    // if the return val is -1, stop the timer;
    const uint64_t TIMER_FLAG_REPEATING     = 1 << 0;

    // if set, the timer object is ready for reuse via `timer_reset()`, implying REPEATING
    // if `default_timedout` is -1, the created timer does NOT fire a first shot before `timer_reset()`
    const uint64_t TIMER_FLAG_REUSE         = 1 << 1;

    // Create a timer object with `default_timedout` in usec, callback function `on_timer`,
    // and callback argument `arg`. The timer object is implemented as a special thread, so
    // it has a `stack_size`, and the `on_timer` is invoked within the thread's context.
    // The timer object is deleted after it is finished.
    timer* timer_create(uint64_t default_timedout, timer_entry on_timer, void* arg,
    uint64_t flags = 0, uint64_t stack_size = 1024 * 64);

    // cancel a scheduled timer;
    // if it's a `TIMER_FLAG_REUSE` timer, it can be reused by `timer_reset`
    int timer_cancel(timer* tmr);

    // reset the timer's timeout
    int timer_reset(timer* tmr, uint64_t new_timeout);

    // destroy a timer
    int timer_destroy(timer* tmr);
}
```

### 同步
```cpp
namespace photon
{
    class mutex
    {
    public:
        void unlock();
        int lock(uint64_t timeout = -1);        // threads are guaranteed to get the lock
        int try_lock(uint64_t timeout = -1);    // in FIFO order, when there's contention
    };

    class scoped_lock
    {
    public:
        explicit scoped_lock(mutex& mutex, bool do_lock = true);
        scoped_lock(scoped_lock&& rhs);

        int lock();
        bool locked();
        operator bool();
        void unlock();

        scoped_lock(const scoped_lock& rhs) = delete;
        void operator = (const scoped_lock& rhs) = delete;
        void operator = (scoped_lock&& rhs) = delete;
    };

    class condition_variable
    {
    public:
        int wait(scoped_lock& lock, uint64_t timeout = -1);
        int wait_no_lock(uint64_t timeout = -1);
        void notify_one();
        void notify_all();
    };

    class semaphore
    {
    public:
        explicit semaphore(uint64_t count);
        int wait(uint64_t count, uint64_t timeout = -1);
        int signal(uint64_t count);
    };
}
```

### 其他
```cpp
namespace photon
{
    // `usec` is the *maximum* amount of time to sleep
    //  returns 0 if slept well or interrupted by IdleWakeUp() or qlen
    //  returns -1 error occured with in IdleSleeper()
    typedef int (*IdleSleeper)(uint64_t usec);
    void set_idle_sleeper(IdleSleeper idle_sleeper);
    IdleSleeper get_idle_sleeper();

    // Saturating addition, primarily for timeout caculation
    __attribute__((always_inline)) inline
    uint64_t sat_add(uint64_t x, uint64_t y);

    // Saturating subtract, primarily for timeout caculation
    __attribute__((always_inline)) inline
    uint64_t sat_sub(uint64_t x, uint64_t y);
};
```

## 扩展API 1：现代语法（photon/thread11.h）
```cpp
namespace photon
{
    // 以任何函数f创建线程，同时设置栈大小
    template<typename F, ENABLE_IF_PF(F), typename...ARGUMENTS>
    thread* thread_create11(uint64_t stack_size, F f, ARGUMENTS&&...args);

    // 以任何函数f创建线程，同时使用默认栈大小
    template<typename F, ENABLE_IF_PF(F), typename...ARGUMENTS>
    thread* thread_create11(F f, ARGUMENTS&&...args);

    // 以任何类`CLASS`的成员函数f创建线程，同时设置栈大小
    template<typename CLASS, typename F, ENABLE_IF_PMF(F), typename...ARGUMENTS>
    thread* thread_create11(uint64_t stack_size, F f, CLASS* obj, ARGUMENTS&&...args);

    // 以任何类`CLASS`的成员函数f创建线程，同时使用默认栈大小
    template<typename CLASS, typename F, ENABLE_IF_PMF(F), typename...ARGUMENTS>
    thread* thread_create11(F f, CLASS* obj, ARGUMENTS&&...args);
}
```

## 扩展API 2：线程池（photon/thread-pool.h）
线程池基于identity-pool实现，并以相似的办法分别支持栈（静态）分配，
和堆（动态）分配。
```cpp
namespace photon
{
    class ThreadPoolBase
    {
    public:
        // 从线程池里创建线程，该线程执行完后自动重回线程池
        thread* thread_create(thread_entry start, void* arg);

        // 堆（动态）分配线程池
        static ThreadPoolBase* new_thread_pool(
        uint32_t capacity, uint64_t stack_size = DEFAULT_STACK_SIZE);

        // 删除堆（动态）分配的线程池
        static void delete_thread_pool(ThreadPoolBase* p);

    protected:
        ThreadPoolBase();
    };

    // 简单调用ThreadPoolBase::new_thread_pool()
    inline ThreadPoolBase* new_thread_pool(
    uint32_t capacity, uint64_t stack_size = DEFAULT_STACK_SIZE);

    // 栈或静态分配（常量容量的）线程池
    template<uint32_t CAPACITY>
    class ThreadPool : public ThreadPoolBase
    {
    public:
        ThreadPool(uint64_t stack_size = DEFAULT_STACK_SIZE);
    };
}
```

## 扩展API 3：异步乱序执行引擎（photon/out-of-order-execution.h）
This module implements a generic framework that enables concurrent
out-of-ordering exeuction of *asynchronous* operations, while providing
a simple *synchronous* interface.

This framework supports potentially any asynchronous operation, by
dividing an operation into 3 parts: issue, wait for completion and
collection of result. The first 2 parts are realized via callbacks.

```cpp
namespace photon
{
    class OutOfOrder_Execution_Engine;

    // 创建异步乱序执行引擎
    OutOfOrder_Execution_Engine* new_ooo_execution_engine();

    // 删除异步乱序执行引擎
    void delete_ooo_execution_engine(OutOfOrder_Execution_Engine* engine);

    // 乱序操作及其参数（定义见后）
    struct OutOfOrderContext;
    
    // Issue an asynchronous operation,
    // storing it's *tag* to args if (!args.flag_tag_valid).
    // return 0 for success, negative for failure
    // Arguments: engine, do_issue, [tag, flag_tag_valid]
    extern "C" int ooo_issue_operation(OutOfOrderContext& args);

    // Wait for the completion of the operation.
    // returns 0 for success, negative for failures
    // if returns -2 and errno == ENOENT, there is a completed
    // operation but there is no caller in the registry to
    // collect the result, so users have to fix it up.
    // Arguments: engine, do_issue, [tag, flag_tag_valid], do_completion
    extern "C" int ooo_wait_completion(OutOfOrderContext& args);

    // Issue and operation and wait for its completion.
    // Return values are defined the same as above
    // Arguments: engine, do_completion
    extern "C" int ooo_issue_wait(OutOfOrderContext& args);

    // Inform the engine that the result has been colleted,
    // so that the engine can goes on.
    // Arguments: engine
    extern "C" void ooo_result_collected(OutOfOrderContext& args);

    struct OutOfOrderContext
    {
        OutOfOrder_Execution_Engine* engine;

        // an unique tag of the opeartion, which can be filled
        // by user (together with `flag_tag_valid` = true),
        // by the `engine`, or by `do_completion`.
        uint64_t tag;

        // The `CallbackType` have an prototype of
        // either `int (*)(void*, OutOfOrderContext*)`,
        // or     `int (CLAZZ::*)(OutOfOrderContext*)`.
        typedef Callback<OutOfOrderContext*> CallbackType;

        // The callback to issue an asynchronous operation, with
        // a tag specified in the argument. The tag should be retrieved
        // when the operation completes.
        // It's guaranteed not to be called concurrently.
        CallbackType do_issue;

        // The callback to do a blocking wait for the completion of any
        // issued operations, storing its *tag* to the `tag` field of the
        // provided `OutOfOrderContext` argument. After a successful return,
        // it's guaranteed not to be called again before `ooo_result_collected()`.
        // It's guaranteed not to be called concurrently.
        CallbackType do_completion;

        // whether or not the `tag` field is valid
        bool flag_tag_valid = false;
    };
}
```
