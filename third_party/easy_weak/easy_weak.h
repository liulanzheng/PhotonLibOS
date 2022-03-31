#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// size of easy_comutex_t
// is 40 bytes in 64-bit situation
struct __attribute__((packed)) placeholder_easy_comutex_t {
    char content[40];
};
typedef placeholder_easy_comutex_t easy_comutex_t;

#define easy_comutex_cond_broadcast v1_easy_comutex_cond_broadcast
#define easy_comutex_cond_lock v1_easy_comutex_cond_lock
#define easy_comutex_cond_signal v1_easy_comutex_cond_signal
#define easy_comutex_cond_timedwait v1_easy_comutex_cond_timedwait
#define easy_comutex_cond_unlock v1_easy_comutex_cond_unlock
#define easy_comutex_cond_wait v1_easy_comutex_cond_wait
#define easy_comutex_init v1_easy_comutex_init
#define easy_comutex_lock v1_easy_comutex_lock
#define easy_comutex_unlock v1_easy_comutex_unlock

int v1_easy_comutex_init(easy_comutex_t*);

int v1_easy_comutex_cond_wait(easy_comutex_t*);

int v1_easy_comutex_cond_timedwait(easy_comutex_t*, int64_t);

int v1_easy_comutex_cond_signal(easy_comutex_t*);

int v1_easy_comutex_cond_broadcast(easy_comutex_t*);

void v1_easy_comutex_lock(easy_comutex_t*);

void v1_easy_comutex_unlock(easy_comutex_t*);

#ifdef __cplusplus
}
#endif