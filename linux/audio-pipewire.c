#include "audio-out.h"
#include <pipewire/pipewire.h>
#include <spa/param/audio/format-utils.h>
#include <dlfcn.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PW_API(X) X(pw_init) X(pw_deinit) X(pw_thread_loop_new) X(pw_thread_loop_destroy) X(pw_thread_loop_get_loop) \
    X(pw_thread_loop_start) X(pw_thread_loop_stop) X(pw_thread_loop_lock) X(pw_thread_loop_unlock) \
    X(pw_thread_loop_get_time) X(pw_thread_loop_timed_wait_full) X(pw_thread_loop_signal) \
    X(pw_stream_new_simple) X(pw_stream_connect) X(pw_stream_destroy) X(pw_stream_set_active) \
    X(pw_stream_dequeue_buffer) X(pw_stream_queue_buffer) X(pw_stream_get_time_n) X(pw_properties_new)
#define DECL(f) __typeof__(f)* f;
static struct { PW_API(DECL) } api;
#undef DECL
static struct {
    void* lib;
    struct pw_thread_loop* loop;
    struct pw_stream* stream;
    enum pw_stream_state state;
    int initialized, started, rate, formatted, failed;
    ds_render_fn render;
    uint64_t submitted, dropped, last_ms;
    int queue_target;
    unsigned fifo_frames, fifo_peak;
    uint64_t depth_samples;
    int16_t fifo[DS_AUDIO_BLOCK * 2];
} p;

/* queued and local are playback frames, not graph delay ticks. */
unsigned ds_pw_queue_deficit(unsigned quantum,uint64_t queued,unsigned local,int valid) {
    if (!valid || !quantum) return 0;
    unsigned target=quantum>=DS_AUDIO_BLOCK/2 ? DS_AUDIO_BLOCK : quantum*2;
    if (queued>=target || local>=target-queued) return 0;
    return target-(unsigned)queued-local;
}

static void pw_close(void) {
    if (p.started) { api.pw_thread_loop_stop(p.loop); p.started=0; }
    if (p.stream) { api.pw_stream_destroy(p.stream); p.stream=NULL; }
    if (p.loop) { api.pw_thread_loop_destroy(p.loop); p.loop=NULL; }
    if (p.initialized) { api.pw_deinit(); p.initialized=0; }
    if (p.lib) { dlclose(p.lib); p.lib=NULL; }
}

static void state_changed(void* unused,enum pw_stream_state old,enum pw_stream_state state,const char* msg) {
    (void)unused; (void)old;
    p.state=state;
    if (p.queue_target && state!=PW_STREAM_STATE_STREAMING) p.fifo_frames=0;
    if (state==PW_STREAM_STATE_ERROR) fprintf(stderr,"[ds3-pipewire] %s\n",msg?msg:"stream error");
    api.pw_thread_loop_signal(p.loop,false);
}

static void param_changed(void* unused,uint32_t id,const struct spa_pod* param) {
    (void)unused;
    if (id!=SPA_PARAM_Format) return;
    if (!param) {
        if (p.queue_target) { p.fifo_frames=0; p.formatted=0; }
        return;
    }
    struct spa_audio_info_raw info={0};
    if (spa_format_audio_raw_parse(param,&info)<0 || info.format!=SPA_AUDIO_FORMAT_S16_LE ||
        info.channels!=2 || info.rate!=(unsigned)p.rate) p.failed=1;
    else p.formatted=1;
    api.pw_thread_loop_signal(p.loop,false);
}

static void process(void* unused) {
    (void)unused;
    struct pw_buffer* buf=api.pw_stream_dequeue_buffer(p.stream);
    if (!buf) return;
    unsigned frames=0;
    uint64_t queued=0;
    int depth_valid=0;
    struct spa_buffer* spa=buf->buffer;
    if (spa && spa->n_datas && spa->datas[0].chunk) {
        struct spa_data* d=&spa->datas[0];
        if (p.render && !p.failed && p.formatted && d->data && d->maxsize>=4 && buf->requested) {
            frames=d->maxsize/4;
            if (buf->requested && buf->requested<frames) frames=(unsigned)buf->requested;
            if (frames>DS_AUDIO_BLOCK) frames=DS_AUDIO_BLOCK;
            if (p.queue_target && p.state==PW_STREAM_STATE_STREAMING) {
                struct pw_time t={0};
                uint64_t now=ds_audio_now_ms();
                if (api.pw_stream_get_time_n(p.stream,&t,sizeof t)==0 && t.now>0 && t.rate.num && t.rate.denom) {
                    uint64_t stamp=(uint64_t)t.now/1000000;
                    depth_valid=stamp<=now+2 && now<=stamp+100;
                    queued=t.queued;
                }
            }
            unsigned from_fifo=frames<p.fifo_frames ? frames : p.fifo_frames;
            if (from_fifo) {
                memcpy(d->data,p.fifo,from_fifo*4);
                p.fifo_frames-=from_fifo;
                if (p.fifo_frames) memmove(p.fifo,p.fifo+2*from_fifo,p.fifo_frames*4);
            }
            if (frames>from_fifo) p.render((int16_t*)d->data+2*from_fifo,(int)(frames-from_fifo));
        }
        d->chunk->offset=0; d->chunk->stride=4; d->chunk->size=frames*4;
    }
    buf->size=frames;
    if (api.pw_stream_queue_buffer(p.stream,buf)<0) {
        p.dropped+=frames; p.failed=1;
        fprintf(stderr,"[ds3-pipewire] queue failed; output stopped\n");
    } else if (frames) {
        p.submitted+=frames; p.last_ms=ds_audio_now_ms();
        if (depth_valid) {
            ++p.depth_samples;
            /* The sample was taken before adding this exact submission. */
            unsigned missing=ds_pw_queue_deficit(frames,queued>DS_AUDIO_BLOCK ? queued : queued+frames,p.fifo_frames,1);
            if (missing) {
                p.render(p.fifo+2*p.fifo_frames,(int)missing);
                p.fifo_frames+=missing;
                if (p.fifo_frames>p.fifo_peak) p.fifo_peak=p.fifo_frames;
            }
        }
    }
}

static const struct pw_stream_events events={PW_VERSION_STREAM_EVENTS,
    .state_changed=state_changed,.param_changed=param_changed,.process=process};

static int pw_open(const char* unused,int rate,int timeout_ms,const char** why) {
    (void)unused;
    int locked=0, r=-ENODEV;
    const char* reason="native PipeWire endpoint unavailable";
    if (why) *why=NULL;
    const char* queue=getenv("DS3_PIPEWIRE_QUEUE");
    p.queue_target=queue && !strcmp(queue,"target");
    if (queue && *queue && !p.queue_target && strcmp(queue,"request")) {
        if (why) *why="invalid DS3_PIPEWIRE_QUEUE (expected target or request)";
        return -EINVAL;
    }
    p.render=NULL; p.state=PW_STREAM_STATE_UNCONNECTED; p.failed=p.formatted=0;
    p.rate=rate; p.submitted=p.dropped=p.last_ms=0;
    p.fifo_frames=p.fifo_peak=0; p.depth_samples=0;
    p.lib=dlopen("libpipewire-0.3.so.0",RTLD_NOW|RTLD_LOCAL);
    if (!p.lib) { reason="libpipewire-0.3.so.0 unavailable"; r=-ENOENT; goto fail; }
#define GET(f) if (!(api.f=dlsym(p.lib,#f))) { reason="missing PipeWire symbol " #f; r=-ENOSYS; goto fail; }
    PW_API(GET)
#undef GET
    api.pw_init(NULL,NULL); p.initialized=1;
    if (!(p.loop=api.pw_thread_loop_new("ds3-pipewire",NULL))) goto fail;
    if (api.pw_thread_loop_start(p.loop)<0) goto fail;
    p.started=1;
    api.pw_thread_loop_lock(p.loop); locked=1;
    struct pw_properties* props=api.pw_properties_new(PW_KEY_MEDIA_TYPE,"Audio",PW_KEY_MEDIA_CATEGORY,"Playback",
        PW_KEY_MEDIA_ROLE,"Game",PW_KEY_NODE_LATENCY,"256/48000",NULL);
    p.stream=api.pw_stream_new_simple(api.pw_thread_loop_get_loop(p.loop),"DS3 playback",props,&events,NULL);
    if (!p.stream) goto fail;
    uint8_t storage[512];
    struct spa_pod_builder builder=SPA_POD_BUILDER_INIT(storage,sizeof storage);
    const struct spa_pod* format=spa_format_audio_raw_build(&builder,SPA_PARAM_EnumFormat,
        &SPA_AUDIO_INFO_RAW_INIT(.format=SPA_AUDIO_FORMAT_S16_LE,.rate=(uint32_t)rate,.channels=2,
            .position={SPA_AUDIO_CHANNEL_FL,SPA_AUDIO_CHANNEL_FR}));
    /* No RT_PROCESS: this loop owns the stream; the existing mixer takes a mutex. */
    if (api.pw_stream_connect(p.stream,PW_DIRECTION_OUTPUT,PW_ID_ANY,
            PW_STREAM_FLAG_AUTOCONNECT|PW_STREAM_FLAG_MAP_BUFFERS,&format,1)<0) goto fail;
    struct timespec deadline;
    if (api.pw_thread_loop_get_time(p.loop,&deadline,(int64_t)timeout_ms*1000000)<0) goto fail;
    while (!p.failed && p.state!=PW_STREAM_STATE_ERROR && !(p.formatted && p.state==PW_STREAM_STATE_STREAMING)) {
        r=api.pw_thread_loop_timed_wait_full(p.loop,&deadline);
        if (r<0) { reason="native PipeWire startup wait failed"; goto fail; }
    }
    if (p.failed || p.state==PW_STREAM_STATE_ERROR) { r=-ENODEV; goto fail; }
    api.pw_thread_loop_unlock(p.loop);
    fprintf(stderr,"[ds3-pipewire] native stream rate=%d\n",rate);
    fprintf(stderr,"[ds3-pipewire] refill=%s\n",p.queue_target?"target":"request");
    return rate;
fail:
    if (locked) api.pw_thread_loop_unlock(p.loop);
    pw_close(); if (why) *why=reason; return r<0?r:-ENODEV;
}

static int pw_start(ds_render_fn render) {
    if (!p.stream || !render) return -EINVAL;
    api.pw_thread_loop_lock(p.loop);
    if (p.render || p.failed || p.state!=PW_STREAM_STATE_STREAMING || !p.formatted) { api.pw_thread_loop_unlock(p.loop); return -EIO; }
    p.render=render;
    api.pw_thread_loop_unlock(p.loop);
    return 0;
}

static void pw_report_close(void) {
    if (p.started && p.stream) {
        api.pw_thread_loop_lock(p.loop);
        if (p.render) {
            struct pw_time t={0};
            int valid=api.pw_stream_get_time_n(p.stream,&t,sizeof t)==0 && t.now>0 && t.rate.denom && t.delay>=0;
            fprintf(stderr,"[ds3-pipewire] submitted=%llu dropped=%llu queued=%llu delay=%lld valid=%d status=%s age_ms=%llu\n",
                (unsigned long long)p.submitted,(unsigned long long)p.dropped,(unsigned long long)t.queued,(long long)t.delay,valid,
                !p.failed && p.formatted && p.state==PW_STREAM_STATE_STREAMING ? "ok" : "failed",
                (unsigned long long)(ds_audio_now_ms()-p.last_ms));
            fprintf(stderr,"[ds3-pipewire] depth_samples=%llu premix_fifo=%u peak=%u frames\n",
                    (unsigned long long)p.depth_samples,p.fifo_frames,p.fifo_peak);
            p.render=NULL;
        }
        api.pw_thread_loop_unlock(p.loop);
    }
    pw_close();
}

const struct ds_audio_ops ds_audio_pipewire={pw_open,pw_start,pw_report_close};
