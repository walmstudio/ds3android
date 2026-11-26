#include "audio-out.h"
#include <errno.h>
#include <time.h>

uint64_t ds_audio_now_ms(void) {
    struct timespec t;
    if (clock_gettime(CLOCK_MONOTONIC,&t)) return 0;
    return (uint64_t)t.tv_sec*1000+(unsigned)t.tv_nsec/1000000;
}

int ds_audio_pump_write(struct ds_audio_pump* p, int limit, ds_render_fn render, ds_write_fn write, void* ctx) {
    if (limit <= 0) return 0;
    if (!p->frames) {
        p->head = 0;
        p->frames = limit > DS_AUDIO_BLOCK ? DS_AUDIO_BLOCK : limit;
        render(p->samples, p->frames);
    }
    int requested = p->frames < limit ? p->frames : limit;
    long n = write(ctx, p->samples + 2*p->head, (unsigned)requested);
    if (n > requested) n = -EIO;
    if (n > 0) {
        p->head += (int)n;
        p->frames -= (int)n;
        p->submitted += (uint64_t)n;
        p->last_ms = ds_audio_now_ms();
    } else if (n < 0 && n != -EAGAIN && n != -EINTR) {
        p->dropped += (unsigned)p->frames;
        p->frames = p->head = 0;
    }
    return (int)n;
}
