#include <easy/easy_io_struct.h>

#include <cstdio>
#include <cerrno>

static const char* err_msg =
    "calling a weak implement, please link libeasy library.";

#define RETURN_ERROR(code) do {         \
    fprintf(stderr, "%s\n", err_msg);   \
    errno = ENXIO;                      \
    return code;                        \
} while (0)

#ifdef __cplusplus
extern "C" {
#endif

int __attribute__((weak)) v1_easy_comutex_init(easy_comutex_t*) {
    RETURN_ERROR(-1);
}

int __attribute__((weak)) v1_easy_comutex_cond_wait(easy_comutex_t*) {
    RETURN_ERROR(-1);
}

int __attribute__((weak))
v1_easy_comutex_cond_timedwait(easy_comutex_t*, int64_t) {
    RETURN_ERROR(-1);
}

int __attribute__((weak)) v1_easy_comutex_cond_signal(easy_comutex_t*) {
    RETURN_ERROR(-1);
}

int __attribute__((weak)) v1_easy_comutex_cond_broadcast(easy_comutex_t*) {
    RETURN_ERROR(-1);
}

void __attribute__((weak)) v1_easy_comutex_lock(easy_comutex_t*) {
    RETURN_ERROR();
}

void __attribute__((weak)) v1_easy_comutex_unlock(easy_comutex_t*) {
    RETURN_ERROR();
}

#ifdef __cplusplus
}
#endif