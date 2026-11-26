#define _GNU_SOURCE
#include "audio-out.h"
#include <alsa/asoundlib.h>
#include <dlfcn.h>
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <sys/eventfd.h>
#include <unistd.h>

#define ALSA_API(X) \
    X(snd_pcm_open) X(snd_pcm_close) X(snd_pcm_set_params) X(snd_pcm_get_params) \
    X(snd_pcm_poll_descriptors_count) X(snd_pcm_poll_descriptors) X(snd_pcm_poll_descriptors_revents) \
    X(snd_pcm_avail_delay) X(snd_pcm_writei) X(snd_pcm_prepare) X(snd_pcm_drop) X(snd_strerror)
#define DECL(f) __typeof__(f)* f;
static struct { ALSA_API(DECL) } api;
#undef DECL
static struct {
    void* lib;
    snd_pcm_t* pcm;
    snd_pcm_uframes_t capacity, period;
    int wake, count, running;
    struct pollfd fds[33];
    pthread_t thread;
    ds_render_fn render;
} a = {.wake = -1};
static char reason[200];

static int valid_device(const char* dev) {
    if (!dev || strncmp(dev,"hw:CARD=",8)) return 0;
    const char* p=dev+8;
    size_t n=strspn(p,"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-");
    if (!n || strncmp(p+n,",DEV=",5)) return 0;
    p+=n+5;
    return *p && p[strspn(p,"0123456789")]==0;
}

static void alsa_close(void) {
    if (a.running) {
        uint64_t one=1;
        while (write(a.wake,&one,sizeof one)<0 && errno==EINTR) {}
        pthread_join(a.thread,NULL);
        a.running=0;
    }
    if (a.pcm) { api.snd_pcm_close(a.pcm); a.pcm=NULL; }
    if (a.wake>=0) { close(a.wake); a.wake=-1; }
    if (a.lib) { dlclose(a.lib); a.lib=NULL; }
}

static int alsa_error(const char* op,int err,const char** why) {
    snprintf(reason,sizeof reason,"%s: %s (%d)",op,api.snd_strerror(err),err);
    if (why) *why=reason;
    alsa_close();
    return err;
}

static int alsa_open(const char* dev,int rate,int timeout_ms,const char** why) {
    (void)timeout_ms; /* direct nonblocking hw open; no server handshake */
    if (why) *why=NULL;
    if (a.pcm) return -EBUSY;
    if (!valid_device(dev)) { if (why) *why="set DS3_ALSA_DEVICE=hw:CARD=<card>,DEV=<n>"; return -EINVAL; }
    a.lib=dlopen("libasound.so.2",RTLD_NOW|RTLD_LOCAL);
    if (!a.lib) { if (why) *why="libasound.so.2 unavailable"; return -ENOENT; }
#define GET(f) if (!(api.f=dlsym(a.lib,#f))) { if (why) *why="missing ALSA symbol " #f; alsa_close(); return -ENOSYS; }
    ALSA_API(GET)
#undef GET
    int r=api.snd_pcm_open(&a.pcm,dev,SND_PCM_STREAM_PLAYBACK,SND_PCM_NONBLOCK);
    if (r<0) return alsa_error(dev,r,why);
    r=api.snd_pcm_set_params(a.pcm,SND_PCM_FORMAT_S16_LE,SND_PCM_ACCESS_RW_INTERLEAVED,2,(unsigned)rate,0,20000);
    if (r<0) return alsa_error("ALSA S16 stereo rate/buffer negotiation",r,why);
    r=api.snd_pcm_get_params(a.pcm,&a.capacity,&a.period);
    if (r<0) return alsa_error("ALSA buffer parameters",r,why);
    a.count=api.snd_pcm_poll_descriptors_count(a.pcm);
    if (a.count<=0 || a.count>32 || !a.period || !a.capacity) return alsa_error("ALSA invalid poll or period parameters",-EINVAL,why);
    if ((r=api.snd_pcm_poll_descriptors(a.pcm,a.fds,(unsigned)a.count))!=a.count)
        return alsa_error("ALSA poll descriptors",r<0?r:-EINVAL,why);
    a.wake=eventfd(0,EFD_CLOEXEC|EFD_NONBLOCK);
    if (a.wake<0) return alsa_error("ALSA wake descriptor",-errno,why);
    a.fds[a.count]=(struct pollfd){.fd=a.wake,.events=POLLIN};
    fprintf(stderr,"[ds3-alsa] %s rate=%d period=%lu buffer=%lu\n",dev,rate,a.period,a.capacity);
    return rate;
}

static long alsa_write(void* pcm,const int16_t* out,unsigned frames) {
    return api.snd_pcm_writei(pcm,out,frames);
}

static void* alsa_run(void* unused) {
    (void)unused;
    struct ds_audio_pump p={0};
    unsigned xruns=0;
    int err=0;
    snd_pcm_sframes_t last_delay=0;
    int valid=0;
    for (;;) {
        int r=poll(a.fds,(nfds_t)a.count+1,-1);
        if (r<0 && errno==EINTR) continue;
        if (r<0) { err=-errno; break; }
        if (a.fds[a.count].revents) break;
        unsigned short events=0;
        r=api.snd_pcm_poll_descriptors_revents(a.pcm,a.fds,(unsigned)a.count,&events);
        if (r<0) { err=r; break; }
        if (!(events & (POLLOUT|POLLERR|POLLHUP|POLLNVAL))) continue;
        snd_pcm_sframes_t avail=0,delay=0;
        r=api.snd_pcm_avail_delay(a.pcm,&avail,&delay);
        valid=!r && delay>=0 && delay<=(snd_pcm_sframes_t)a.capacity;
        if (valid) last_delay=delay;
        if (!r && avail>0 && !(events&(POLLHUP|POLLNVAL))) {
            /* set_params negotiated a bounded device buffer. avail limits the
             * next physical submission, not a latency estimate. */
            snd_pcm_sframes_t lim=avail<(snd_pcm_sframes_t)a.capacity?avail:(snd_pcm_sframes_t)a.capacity;
            r=ds_audio_pump_write(&p,lim<DS_AUDIO_BLOCK?(int)lim:DS_AUDIO_BLOCK,a.render,alsa_write,a.pcm);
        } else if (!r && (events&(POLLHUP|POLLNVAL))) r=-ENODEV;
        if (r==-EPIPE || r==-ESTRPIPE) {
            if (r==-EPIPE) ++xruns;
            p.dropped+=(unsigned)p.frames;
            p.frames=p.head=0;
            fprintf(stderr,"[ds3-alsa] recover=%d xruns=%u delay=%ld valid=0\n",r,xruns,(long)delay);
            r=api.snd_pcm_prepare(a.pcm);
        }
        if (r<0 && r!=-EAGAIN && r!=-EINTR) { err=r; break; }
    }
    api.snd_pcm_drop(a.pcm);
    if (err) fprintf(stderr,"[ds3-alsa] output stopped: %s (%d)\n",api.snd_strerror(err),err);
    fprintf(stderr,"[ds3-alsa] submitted=%llu dropped=%llu xruns=%u delay_frames=%ld valid=%d status=%s age_ms=%llu\n",
        (unsigned long long)p.submitted,(unsigned long long)(p.dropped+p.frames),xruns,(long)last_delay,valid,err?"failed":"ok",
        (unsigned long long)(ds_audio_now_ms()-p.last_ms));
    return NULL;
}

static int alsa_start(ds_render_fn render) {
    if (!a.pcm || a.running || !render) return -EINVAL;
    a.render=render;
    int err=pthread_create(&a.thread,NULL,alsa_run,NULL);
    if (err) return -err;
    a.running=1;
    return 0;
}

const struct ds_audio_ops ds_audio_alsa={alsa_open,alsa_start,alsa_close};
