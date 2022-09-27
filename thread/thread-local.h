
#pragma once

#include <memory>
#include <photon/thread/thread.h>

template<typename T>
class ThreadLocal {
public:
    ThreadLocal() : ThreadLocal(std::function<T*()>([] { return new T(); })) {}

    explicit ThreadLocal(const T& t) : ThreadLocal([t] { return new T(t); }) {}

    explicit ThreadLocal(T&& t) : ThreadLocal([t] { return new T(std::move(t)); }) {}

    explicit ThreadLocal(std::function<T*()> c, std::function<void(T*)> d = std::default_delete<T>())
            : m_ctor(c), m_dtor(d) {
        photon::thread_key_create(&m_key, default_exit);
    }

    ~ThreadLocal() {
        photon::thread_key_delete(m_key);
    }

    T* operator&() const {
        return get();
    }

    explicit operator T&() const = delete;
    ThreadLocal(const ThreadLocal& l) = delete;
    ThreadLocal& operator=(ThreadLocal& l) = delete;
    ThreadLocal(ThreadLocal&& rhs) noexcept = delete;
    ThreadLocal& operator=(ThreadLocal&& rhs) noexcept = delete;
    ThreadLocal& operator=(const T& t) noexcept = delete;
    ThreadLocal& operator=(T&& t) noexcept = delete;

private:
    T* get() const {
        auto data = (Data*) photon::thread_getspecific(m_key);
        if (data == nullptr) {
            data = new Data();
            data->tl = this;
            data->t = m_ctor();
            photon::thread_setspecific(m_key, data);
            return data->t;
        }
        return data->t;
    }

    static void default_exit(void* v) {
        auto d = (Data*) v;
        if (d && d->tl->m_dtor)
            d->tl->m_dtor(d->t);
        delete d;
    }

    struct Data {
        const ThreadLocal* tl;
        T* t;
    };

    // For static ThreadLocal<T> var, the constructor of var will be called only once,
    // so we must store how var is allocated and initialized, by using m_ctor
    std::function<T*()> m_ctor = nullptr;
    std::function<void(T*)> m_dtor = nullptr;
    photon::thread_key_t m_key = -1U;
};
