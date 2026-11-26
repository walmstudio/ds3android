#ifndef DS3_AUDIO_OUT_H
#define DS3_AUDIO_OUT_H
#include <stdint.h>
typedef void (*ds_render_fn)(int16_t*, int);
struct ds_audio_ops {
    int (*open)(const char*, int, int, const char**);
    int (*start)(ds_render_fn);
    void (*close)(void);
};
extern const struct ds_audio_ops ds_audio_alsa, ds_audio_pipewire, ds_audio_pulse;
int ds_audio_open(const char*, const char*, int, const char**);
int ds_audio_start(ds_render_fn);
void ds_audio_close(void);

enum { DS_AUDIO_BLOCK = 1024 };
struct ds_audio_pump {
    int16_t samples[DS_AUDIO_BLOCK * 2];
    int head, frames;
    uint64_t submitted, dropped, last_ms;
};
typedef long (*ds_write_fn)(void*, const int16_t*, unsigned);
int ds_audio_pump_write(struct ds_audio_pump*, int limit, ds_render_fn, ds_write_fn, void*);
uint64_t ds_audio_now_ms(void);
#endif
