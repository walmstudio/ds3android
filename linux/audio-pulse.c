#define _POSIX_C_SOURCE 200809L
#include "audio-out.h"
#include <pulse/pulseaudio.h>
#include <dlfcn.h>
#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <time.h>

#define PULSE_API(X) X(pa_mainloop_new) X(pa_mainloop_free) X(pa_mainloop_get_api) X(pa_mainloop_iterate) X(pa_mainloop_wakeup) \
    X(pa_context_new) X(pa_context_connect) X(pa_context_get_state) X(pa_context_disconnect) X(pa_context_unref) \
    X(pa_stream_new) X(pa_stream_connect_playback) X(pa_stream_get_state) X(pa_stream_disconnect) X(pa_stream_unref) \
    X(pa_stream_set_write_callback) X(pa_stream_set_underflow_callback) X(pa_stream_writable_size) X(pa_stream_write) \
    X(pa_stream_cork) X(pa_operation_unref) X(pa_stream_get_buffer_attr) X(pa_stream_get_latency)
#define DECL(f) __typeof__(f)* f;
static struct { PULSE_API(DECL) } api;
#undef DECL
static struct {
    void* lib;
    pa_mainloop* loop;
    pa_context* context;
    pa_stream* stream;
    pthread_t thread;
    int running, failed, uncorked;
    size_t target;
    _Atomic int stop;
    ds_render_fn render;
    struct ds_audio_pump pump;
    unsigned underruns;
} p;

static void pulse_close(void) {
    if (p.running) {
        atomic_store(&p.stop,1); api.pa_mainloop_wakeup(p.loop);
        pthread_join(p.thread,NULL); p.running=0;
    }
    if (p.stream) { api.pa_stream_disconnect(p.stream); api.pa_stream_unref(p.stream); p.stream=NULL; }
    if (p.context) { api.pa_context_disconnect(p.context); api.pa_context_unref(p.context); p.context=NULL; }
    if (p.loop) { api.pa_mainloop_free(p.loop); p.loop=NULL; }
    if (p.lib) { dlclose(p.lib); p.lib=NULL; }
}

static uint64_t now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC,&ts);
    return (uint64_t)ts.tv_sec*1000 + ts.tv_nsec/1000000;
}
static void uncork_done(pa_stream* stream,int ok,void* unused) {
    (void)stream; (void)unused;
    p.uncorked=ok?1:-1;
}

static int pulse_open(const char* unused,int rate,int timeout_ms,const char** why) {
    (void)unused;
    const char* reason="PulseAudio endpoint unavailable";
    int err=-ENODEV;
    uint64_t end=now_ms()+(unsigned)timeout_ms;
    if (why) *why=NULL;
    p.lib=dlopen("libpulse.so.0",RTLD_NOW|RTLD_LOCAL);
    if (!p.lib) { reason="libpulse.so.0 unavailable"; err=-ENOENT; goto fail; }
#define GET(f) if (!(api.f=dlsym(p.lib,#f))) { reason="missing PulseAudio symbol " #f; err=-ENOSYS; goto fail; }
    PULSE_API(GET)
#undef GET
    if (!(p.loop=api.pa_mainloop_new()) || !(p.context=api.pa_context_new(api.pa_mainloop_get_api(p.loop),"Dead Space 3"))) goto fail;
    if (api.pa_context_connect(p.context,NULL,PA_CONTEXT_NOAUTOSPAWN,NULL)<0) goto fail;
    for (;;) {
        pa_context_state_t c=api.pa_context_get_state(p.context);
        if (!PA_CONTEXT_IS_GOOD(c)) goto fail;
        if (c==PA_CONTEXT_READY && !p.stream) {
            pa_sample_spec spec={PA_SAMPLE_S16LE,(uint32_t)rate,2};
            pa_buffer_attr attr={.maxlength=(uint32_t)rate*4/25,.tlength=(uint32_t)rate*4/50,
                .prebuf=(uint32_t)rate*4/100,.minreq=(uint32_t)rate*4/200,.fragsize=UINT32_MAX};
            if (!(p.stream=api.pa_stream_new(p.context,"DS3 playback",&spec,NULL))) goto fail;
            if (api.pa_stream_connect_playback(p.stream,NULL,&attr,PA_STREAM_START_CORKED|PA_STREAM_ADJUST_LATENCY|
                    PA_STREAM_AUTO_TIMING_UPDATE|PA_STREAM_INTERPOLATE_TIMING,NULL,NULL)<0) goto fail;
        }
        if (p.stream) {
            pa_stream_state_t s=api.pa_stream_get_state(p.stream);
            if (!PA_STREAM_IS_GOOD(s)) goto fail;
            if (s==PA_STREAM_READY) break;
        }
        if (now_ms()>=end) { reason="PulseAudio startup timeout"; err=-ETIMEDOUT; goto fail; }
        if (api.pa_mainloop_iterate(p.loop,0,NULL)<0) goto fail;
        nanosleep(&(struct timespec){.tv_nsec=2000000},NULL);
    }
    const pa_buffer_attr* attr=api.pa_stream_get_buffer_attr(p.stream);
    if (!attr || attr->tlength<4 || attr->tlength==UINT32_MAX) goto fail;
    p.target=attr->tlength;
    fprintf(stderr,"[ds3-pulse] rate=%d target=%u minreq=%u bytes\n",rate,attr->tlength,attr->minreq);
    p.uncorked=0;
    pa_operation* op=api.pa_stream_cork(p.stream,0,uncork_done,NULL);
    if (!op) goto fail;
    api.pa_operation_unref(op);
    while (!p.uncorked) {
        if (now_ms()>=end) { reason="PulseAudio activation timeout"; err=-ETIMEDOUT; goto fail; }
        if (api.pa_mainloop_iterate(p.loop,0,NULL)<0 || api.pa_stream_get_state(p.stream)!=PA_STREAM_READY) goto fail;
        if (!p.uncorked) nanosleep(&(struct timespec){.tv_nsec=2000000},NULL);
    }
    if (p.uncorked<0) { reason="PulseAudio activation rejected"; goto fail; }
    return rate;
fail:
    pulse_close(); if (why) *why=reason; return err;
}

static long pulse_submit(void* stream,const int16_t* pcm,unsigned frames) {
    return api.pa_stream_write(stream,pcm,(size_t)frames*4,NULL,0,PA_SEEK_RELATIVE)<0 ? -EIO : (long)frames;
}

static void pulse_write(pa_stream* stream,size_t requested,void* unused) {
    (void)unused;
    if (atomic_load(&p.stop) || p.failed) return;
    size_t avail=api.pa_stream_writable_size(stream);
    if (avail==(size_t)-1) { p.failed=1; return; }
    if (requested>p.target) requested=p.target;
    size_t frames=(requested<avail?requested:avail)/4;
    while (frames && !atomic_load(&p.stop)) {
        int n=ds_audio_pump_write(&p.pump,frames>DS_AUDIO_BLOCK?DS_AUDIO_BLOCK:(int)frames,p.render,pulse_submit,stream);
        if (n<=0) { p.failed=1; return; }
        frames-=(unsigned)n;
    }
}

static void pulse_underflow(pa_stream* stream,void* unused) {
    (void)stream; (void)unused;
    ++p.underruns;
}

static void* pulse_run(void* unused) {
    (void)unused;
    /* READY may have delivered the initial write credit while corked, before
     * the application installed its write callback. Consume that credit once. */
    size_t initial=api.pa_stream_writable_size(p.stream);
    if (initial==(size_t)-1) p.failed=1;
    else if (initial) pulse_write(p.stream,initial,NULL);
    while (!atomic_load(&p.stop) && !p.failed && PA_CONTEXT_IS_GOOD(api.pa_context_get_state(p.context)) &&
           PA_STREAM_IS_GOOD(api.pa_stream_get_state(p.stream))) {
        if (api.pa_mainloop_iterate(p.loop,1,NULL)<0) break;
    }
    if (!atomic_load(&p.stop)) fprintf(stderr,"[ds3-pulse] output stopped unexpectedly\n");
    pa_usec_t us=0; int negative=0;
    int valid=api.pa_stream_get_latency(p.stream,&us,&negative)==0 && !negative;
    fprintf(stderr,"[ds3-pulse] submitted=%llu dropped=%llu underruns=%u delay_us=%llu valid=%d status=%s age_ms=%llu\n",
        (unsigned long long)p.pump.submitted,(unsigned long long)p.pump.dropped,p.underruns,(unsigned long long)us,valid,
        atomic_load(&p.stop) && !p.failed && api.pa_stream_get_state(p.stream)==PA_STREAM_READY ? "ok" : "failed",
        (unsigned long long)(ds_audio_now_ms()-p.pump.last_ms));
    return NULL;
}

static int pulse_start(ds_render_fn render) {
    if (!p.stream || p.running || !render) return -EINVAL;
    p.render=render; p.pump=(struct ds_audio_pump){0}; p.failed=0; p.underruns=0;
    atomic_store(&p.stop,0);
    api.pa_stream_set_write_callback(p.stream,pulse_write,NULL);
    api.pa_stream_set_underflow_callback(p.stream,pulse_underflow,NULL);
    int r=pthread_create(&p.thread,NULL,pulse_run,NULL);
    if (r) return -r;
    p.running=1;
    return 0;
}

const struct ds_audio_ops ds_audio_pulse={pulse_open,pulse_start,pulse_close};
