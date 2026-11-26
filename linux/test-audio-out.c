#define _POSIX_C_SOURCE 200809L
#include "audio-out.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <stdatomic.h>
#include <time.h>

extern int ds_audio_pump_write(struct ds_audio_pump*, int, ds_render_fn, ds_write_fn, void*) __attribute__((weak));
extern unsigned ds_pw_queue_deficit(unsigned, uint64_t, unsigned, int) __attribute__((weak));
extern const struct ds_audio_ops ds_audio_alsa __attribute__((weak));
extern const struct ds_audio_ops ds_audio_pipewire __attribute__((weak));
extern const struct ds_audio_ops ds_audio_pulse __attribute__((weak));
extern int ds_audio_open(const char*, const char*, int, const char**) __attribute__((weak));
static unsigned renders, calls;
static _Atomic unsigned silence_frames;
static void silence(int16_t* dst,int frames) {
    memset(dst,0,(size_t)frames*4);
    atomic_fetch_add(&silence_frames,(unsigned)frames);
}
static void render(int16_t* dst, int frames) {
    ++renders;
    for (int i=0; i<frames; ++i) dst[2*i]=dst[2*i+1]=(int16_t)(i+1);
}
static long submit(void* unused, const int16_t* src, unsigned n) {
    (void)unused;
    switch (++calls) {
    case 1: assert(src[0]==1 && n==256); return 128;
    case 2: assert(src[0]==129 && n==128); return -EAGAIN;
    case 3: assert(src[0]==129 && n==128); return 128;
    case 4: assert(src[0]==1 && n==256); return 64;
    default: assert(src[0]==65 && n==192); return -EPIPE;
    }
}
int main(int argc, char** argv) {
    if (argc >= 2 && !strcmp(argv[1], "selector")) {
        if (!ds_audio_open) { fputs("FAIL: missing launch selector\n",stderr); return 1; }
        const char* name=NULL;
        assert(ds_audio_open("alsa",NULL,48000,&name)<0);
        assert(ds_audio_open("invalid",NULL,48000,&name)<0);
        const char* expected=argc==3?argv[2]:"pipewire";
        int rc=ds_audio_open("auto",NULL,48000,&name);
        if (!strcmp(expected,"none")) assert(rc<0 && !name);
        else assert(rc>0 && !strcmp(name,expected));
        assert(!atomic_load(&silence_frames));
        ds_audio_close();
        printf("PASS selector strict ALSA and automatic %s\n",expected);
        return 0;
    }
    if (argc >= 2 && (!strcmp(argv[1], "pipewire") || !strcmp(argv[1], "pulse"))) {
        const struct ds_audio_ops* ops = !strcmp(argv[1], "pipewire") ? &ds_audio_pipewire : &ds_audio_pulse;
        if (!ops) { fprintf(stderr,"FAIL: missing %s stream adapter\n",argv[1]); return 1; }
        assert(ops->start(silence)<0);
        const char* why=NULL;
        if (argc == 2) {
            assert(ops->open(NULL,48000,150,&why)<0 && why);
            ops->close(); ops->close();
            assert(!atomic_load(&silence_frames) && ops->start(silence)<0);
            printf("PASS %s unavailable endpoint does not render\n",argv[1]);
        } else {
            assert(ops->open(NULL,48000,1500,&why)>0);
            assert(!atomic_load(&silence_frames));
            assert(!ops->start(silence));
            nanosleep(&(struct timespec){.tv_nsec=250000000},NULL);
            ops->close();
            unsigned frames=atomic_load(&silence_frames);
            assert(frames>0);
            ops->close();
            assert(frames==atomic_load(&silence_frames));
            printf("PASS %s real stream rendered %u frames\n",argv[1],frames);
        }
        return 0;
    }
    if (argc == 3 && !strcmp(argv[1], "alsa-hw")) {
        if (!&ds_audio_alsa) return 1;
        const char* why=NULL;
        int rate=ds_audio_alsa.open(argv[2],48000,100,&why);
        if (rate<0) { fprintf(stderr,"ALSA probe: %s\n",why?why:"open failed"); return 2; }
        if (ds_audio_alsa.start(silence)) { ds_audio_alsa.close(); return 1; }
        nanosleep(&(struct timespec){.tv_nsec=150000000},NULL);
        ds_audio_alsa.close();
        printf("ALSA probe rendered %u frames; see submitted count\n",atomic_load(&silence_frames));
        return !atomic_load(&silence_frames);
    }
    if (argc == 2 && !strcmp(argv[1], "pipewire-target")) {
        if (!ds_pw_queue_deficit) { fputs("FAIL: no PipeWire depth-based refill\n", stderr); return 1; }
        assert(ds_pw_queue_deficit(256,256,0,1)==256);
        assert(ds_pw_queue_deficit(256,256,128,1)==128);
        assert(ds_pw_queue_deficit(256,512,0,1)==0);
        assert(ds_pw_queue_deficit(256,0,0,0)==0);
        assert(ds_pw_queue_deficit(1024,800,0,1)==224);
        assert(ds_pw_queue_deficit(256,UINT64_MAX,256,1)==0);
        puts("PASS PipeWire bounded target and invalid timing");
        return 0;
    }
    if (argc == 2 && !strcmp(argv[1], "alsa")) {
        if (!&ds_audio_alsa) { fputs("FAIL: no direct ALSA adapter\n",stderr); return 1; }
        const char* why=NULL;
        assert(ds_audio_alsa.open("default",48000,100,&why)<0 && why);
        assert(ds_audio_alsa.open("hw:CARD=,DEV=0",48000,100,&why)<0 && why);
        assert(ds_audio_alsa.open("hw:CARD=DS3NOHW,DEV=0",48000,100,&why)<0 && why);
        assert(ds_audio_alsa.start(render)<0 && renders==0);
        ds_audio_alsa.close(); ds_audio_alsa.close();
        puts("PASS alsa strict selector and unopened lifecycle");
        return 0;
    }
    if (!ds_audio_pump_write) { fputs("FAIL: missing bounded ALSA submission\n", stderr); return 1; }
    struct ds_audio_pump p={0};
    assert(!ds_audio_pump_write(&p,0,render,submit,NULL) && !renders);
    assert(ds_audio_pump_write(&p,256,render,submit,NULL)==128 && p.submitted==128);
    assert(ds_audio_pump_write(&p,256,render,submit,NULL)==-EAGAIN && renders==1);
    assert(ds_audio_pump_write(&p,256,render,submit,NULL)==128 && renders==1 && p.submitted==256);
    assert(ds_audio_pump_write(&p,256,render,submit,NULL)==64);
    assert(ds_audio_pump_write(&p,256,render,submit,NULL)==-EPIPE && renders==2);
    assert(p.submitted==320 && p.dropped==192 && !p.frames);
    puts("PASS alsa partial, EAGAIN and XRUN pump");
}
