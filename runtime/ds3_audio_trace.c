#include "ds3_audio_trace.h"
#include <stdatomic.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#define CAP 8192u
typedef struct { uint64_t ns, id; uint32_t a,b,c; int kind; } Event;
static Event ring[CAP];
static unsigned write_at, read_at;
static atomic_flag guard = ATOMIC_FLAG_INIT;
static pthread_mutex_t drain_lock=PTHREAD_MUTEX_INITIALIZER;
static _Atomic unsigned dropped;
static _Atomic int enabled;
static _Atomic int finished;
void ds3_audio_trace_init(void) {
    const char* opt = getenv("DS3_AUDIO_TRACE");
    enabled = opt && !strcmp(opt,"1");
}
int ds3_audio_trace_enabled(void) { return enabled; }
void ds3_audio_trace(int kind, uint64_t id, uint32_t a, uint32_t b, uint32_t c) {
    if (!enabled || finished) return;
    struct timespec t;
    if (clock_gettime(CLOCK_MONOTONIC,&t)) return;
    if (atomic_flag_test_and_set_explicit(&guard,memory_order_acquire)) {
        atomic_fetch_add_explicit(&dropped,1,memory_order_relaxed); return;
    }
    if (finished) { atomic_flag_clear_explicit(&guard,memory_order_release); return; }
    if (write_at-read_at < CAP) ring[write_at++ % CAP]=(Event){(uint64_t)t.tv_sec*1000000000ull+t.tv_nsec,id,a,b,c,kind};
    else atomic_fetch_add_explicit(&dropped,1,memory_order_relaxed);
    atomic_flag_clear_explicit(&guard,memory_order_release);
}
void ds3_audio_trace_drain(void) {
    if (!enabled) return;
    pthread_mutex_lock(&drain_lock);
    static const char* labels[]={"cursor","lock","unlock","fresh","callback_begin","callback_end","sink_depth_unavailable","create","release","play","stop","seek","mix_lock_wait","mix_begin","mix_end","ring_ambiguous","ring_late"};
    for (unsigned i=0;i<CAP/128;++i) {
        Event batch[128]; unsigned n=0;
        if (atomic_flag_test_and_set_explicit(&guard,memory_order_acquire)) break;
        while (read_at!=write_at && n<128) batch[n++]=ring[read_at++ % CAP];
        atomic_flag_clear_explicit(&guard,memory_order_release);
        if (!n) break;
        for (unsigned k=0;k<n;++k) {
            Event* e=&batch[k];
            fprintf(stderr,"[ds3-audio] %llu %s %llu %u %u %u\n",(unsigned long long)e->ns,labels[e->kind],(unsigned long long)e->id,e->a,e->b,e->c);
        }
    }
    unsigned lost=atomic_exchange_explicit(&dropped,0,memory_order_relaxed);
    if (lost) {
        struct timespec t;
        if (!clock_gettime(CLOCK_MONOTONIC,&t)) fprintf(stderr,"[ds3-audio] %llu dropped 0 %u 0 0\n",(unsigned long long)t.tv_sec*1000000000ull+(unsigned long long)t.tv_nsec,lost);
    }
    pthread_mutex_unlock(&drain_lock);
}
void ds3_audio_trace_finish(void) {
    if (!enabled || atomic_exchange(&finished,1)) return;
    /* Exit is not a callback. Take the guard once so no publisher remains
     * in the critical section; later publishers observe finished. */
    while (atomic_flag_test_and_set_explicit(&guard,memory_order_acquire)) { }
    atomic_flag_clear_explicit(&guard,memory_order_release);
    ds3_audio_trace_drain();
}
