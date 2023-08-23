#include <linux/mman.h>
#include <stdlib.h>
#include <sys/mman.h>

#include <atomic>
#include <vector>

#include "list.h"

namespace photon {

namespace thread_stack_allocator {

template <size_t MIN_ALLOCATION_SIZE = 4UL * 1024,
          size_t MAX_ALLOCATION_SIZE = 64UL * 1024 * 1024,
          size_t ALIGNMENT = 64>
class ThreadStackAllocator {
    constexpr static bool is_power2(size_t n) { return (n & (n - 1)) == 0; }
    static_assert(is_power2(ALIGNMENT), "must be 2^n");
    static_assert(is_power2(MAX_ALLOCATION_SIZE), "must be 2^n");
    const static size_t N_SLOTS =
        __builtin_ffsl(MAX_ALLOCATION_SIZE / MIN_ALLOCATION_SIZE);

public:
    ThreadStackAllocator() {
        auto size = MIN_ALLOCATION_SIZE;
        for (auto& slot : slots) {
            slot.set_alloc_size(size);
            size *= 2;
        }
    }

protected:
    const int BASE_OFF = log2_round(MIN_ALLOCATION_SIZE);
    class Slot {
        size_t alloc_size = ALIGNMENT;
        std::vector<void*> pool;

    public:
        ~Slot() {
            for (auto pt : pool) {
                free(pt);
            }
        }
        void set_alloc_size(size_t x) { alloc_size = x; }
        int alloc(void** ptr) {
            int ret = ::posix_memalign((void**)ptr, ALIGNMENT, alloc_size);
            if (ret != 0) {
                errno = ret;
                return -1;
            }
#if defined(__linux__)
            madvise(*ptr, alloc_size, MADV_NOHUGEPAGE);
#endif
            return alloc_size;
        }
        void* get() {
            if (!pool.empty()) {
                auto ret = pool.back();
                pool.pop_back();
                return ret;
            } else {
                void* ptr = nullptr;
                alloc(&ptr);
                return ptr;
            }
        }
        void put(void* ptr) { pool.push_back(ptr); }
    };

    static inline int log2_round(unsigned int x, bool round_up = false) {
        assert(x > 0);
        int ret = sizeof(x) * 8 - 1 - __builtin_clz(x);
        if (round_up && (1U << ret) < x) return ret + 1;
        return ret;
    }

    int get_slot(unsigned int x) {
        int i = log2_round(x, true);
        if (i < BASE_OFF) return 0;
        return i - BASE_OFF;
    }

    Slot slots[N_SLOTS];

public:
    void* alloc(size_t size) {
        if (unlikely(size > MAX_ALLOCATION_SIZE)) {
            void* ptr = nullptr;
            int ret = ::posix_memalign(&ptr, ALIGNMENT, size);
            if (ret != 0) {
                errno = ret;
                return nullptr;
            }
#if defined(__linux__)
            madvise(ptr, size, MADV_NOHUGEPAGE);
#endif
            return ptr;
        }
        return slots[get_slot(size)].get();
    }
    int dealloc(void* ptr, size_t size) {
        if (unlikely(size > MAX_ALLOCATION_SIZE)) {
            madvise(ptr, size, MADV_DONTNEED);
            free(ptr);
            return 0;
        }
        slots[get_slot(size)].put(ptr);
        return 0;
    }
};

static thread_local ThreadStackAllocator<> _alloc;

}  // namespace thread_stack_allocator

void* threadlocal_pooled_photon_thread_stack_alloc(void*, size_t stack_size) {
    return photon::thread_stack_allocator::_alloc.alloc(stack_size);
}
void threadlocal_pooled_photon_thread_stack_dealloc(void*, void* stack_ptr,
                                                    size_t stack_size) {
    photon::thread_stack_allocator::_alloc.dealloc(stack_ptr, stack_size);
}
}  // namespace photon