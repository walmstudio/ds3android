#include "hle.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>
#include <unistd.h>
#include <pthread.h>

static unsigned pass, fail;
extern void ds3_audio_trace_init(void) __attribute__((weak));
extern void ds3_audio_trace_drain(void) __attribute__((weak));
extern void ds3_audio_trace(int,uint64_t,uint32_t,uint32_t,uint32_t) __attribute__((weak));
#define CHECK(x) do { if (x) ++pass; else { ++fail; fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); } } while (0)
static uint32_t call(const char* dll, const char* name, unsigned n, uint32_t* a) {
    uint32_t t = hle_resolve_impl(dll, name);
    if (!t) return UINT32_MAX;
    uint32_t sp = g_cpu.esp, r = rt_call(&g_cpu, t, n, a);
    CHECK(g_cpu.esp == sp);
    return r;
}
#define API(d, f, ...) call(d, f, sizeof((uint32_t[]){0, ##__VA_ARGS__}) / 4 - 1, ((uint32_t[]){0, ##__VA_ARGS__}) + 1)
#define W(f, ...) API("winmm.dll", "wave" f, ##__VA_ARGS__)
#define D(f, ...) API("dsound.com", "IDirectSoundBuffer8::" f, __VA_ARGS__)
static int await_count(int (*count)(void), int n) {
    for (int i=0; i<100 && count()<n; ++i) usleep(10000);
    return count() == n;
}
static void* foreground(void* unused) { (void)unused; hle_al_set_paused(0); return NULL; }
static int pull_sample(void (*pull)(int16_t*, int32_t)) {
    int16_t pcm[2] = {0};
    for (int i=0; i<100 && !pcm[0]; ++i) { pull(pcm,1); if (!pcm[0]) usleep(10000); }
    return pcm[0];
}
static void check_rate(uint32_t dev, uint32_t out, unsigned want) {
    CHECK(hle_al_rate() == (int)want);
    API("OpenAL32.dll", "alcGetIntegerv", dev, 0x1007u, 1u, out);
    CHECK(rd32(out) == want);
    API("OpenAL32.dll", "alcGetIntegerv", dev, 0x1003u, 11u, out);
    CHECK(rd32(out) == 0x1007u && rd32(out+4) == want);
}
static int rate_commit(int fail_start) {
    unsetenv("ME1_AL_SINK"); setenv("ME1_AL_RATE", "32000", 1);
    rt_init(); hle_al_set_paused(1);
    void* lib = dlopen("libaaudio.so", RTLD_NOW);
    if (!lib) return 2;
    void (*hold)(int) = dlsym(lib,"fixture_hold"), (*rate)(int) = dlsym(lib,"fixture_rate");
    void (*release)(void) = dlsym(lib,"fixture_release"), (*reject)(void) = dlsym(lib,"fixture_fail_start");
    void (*pull)(int16_t*, int32_t) = dlsym(lib,"fixture_pull");
    int (*entered)(void) = dlsym(lib,"fixture_entered"), (*stops)(void) = dlsym(lib,"fixture_stops");
    int (*opens)(void) = dlsym(lib,"fixture_opens");
    if (!hold || !rate || !release || !reject || !pull || !entered || !stops || !opens) return 2;
    CHECK(hle_al_rate() == 32000);
    uint32_t dev=API("OpenAL32.dll","alcOpenDevice",0u), out=ghalloc(44,1);
    CHECK(dev);
    check_rate(dev,out,32000);
    uint32_t fmt=ghalloc(18,1), hdr=ghalloc(32,1), data=ghalloc(16,1);
    wr16(fmt,1); wr16(fmt+2,1); wr32(fmt+4,32000); wr32(fmt+8,64000); wr16(fmt+12,2); wr16(fmt+14,16);
    CHECK(W("OutOpen",out,0u,fmt,0u,0u,0u) == 0);
    uint32_t h=rd32(out);
    wr32(hdr,data); wr32(hdr+4,16);
    for(int i=0;i<8;++i) wr16(data+2*i,(i+1)*1024);
    CHECK(W("OutPrepareHeader",h,hdr,32u) == 0 && W("OutWrite",h,hdr,32u) == 0);
    rate(48000); hold(1);
    hle_al_set_paused(0);
    CHECK(await_count(entered,1)); check_rate(dev,out,32000);
    hold(3); /* release open and block start */
    CHECK(await_count(entered,3)); check_rate(dev,out,32000);
    int16_t pcm[2]; pull(pcm,1); CHECK(!pcm[0] && (rd32(hdr+16)&16));
    if (fail_start) {
        reject(); release();
        for (int i=0; i<100 && (rd32(hdr+16)&16); ++i) usleep(10000);
        CHECK(opens() == 0 && (rd32(hdr+16)&0x13) == 3);
        check_rate(dev,out,32000);
    } else {
        hle_al_set_paused(1); hold(2); /* superseded even though requestStart succeeds */
        CHECK(await_count(entered,2)); check_rate(dev,out,32000);
        release(); CHECK(await_count(stops,1));
        rate(64000); hle_al_set_paused(0);
        CHECK(await_count(opens,2));
        CHECK(pull_sample(pull) == 1024);
        pull(pcm,1); CHECK(pcm[0] == 1536); /* first callback used the committed 2:1 rate */
        check_rate(dev,out,64000);
        hle_al_set_paused(1); CHECK(await_count(stops,2));
        rate(96000); hold(3); hle_al_set_paused(0);
        CHECK(await_count(entered,3)); check_rate(dev,out,64000);
        pull(pcm,1); CHECK(!pcm[0]);
        release(); CHECK(await_count(opens,3));
        CHECK(pull_sample(pull) == 2048); /* cursor survived restart */
        check_rate(dev,out,96000);
    }
    hle_al_set_paused(1);
    fprintf(stderr,"test_ds3_audio --rate-%s: %u ok, %u failed\n",fail_start?"failure":"commit",pass,fail);
    return fail != 0;
}
static int device_loss(int fail_first) {
    unsetenv("ME1_AL_SINK"); rt_init(); hle_al_set_paused(1);
    void* lib = dlopen("libaaudio.so", RTLD_NOW);
    int (*opens)(void) = lib ? dlsym(lib, "fixture_opens") : NULL;
    void (*lose)(void) = lib ? dlsym(lib, "fixture_lose") : NULL;
    void (*pull)(int16_t*, int32_t) = lib ? dlsym(lib, "fixture_pull") : NULL;
    CHECK(opens && lose && pull);
    if (!opens || !lose || !pull) return 1;
    uint32_t out=ghalloc(4,1), fmt=ghalloc(18,1), p=ghalloc(32,1), data=ghalloc(16,1);
    wr16(fmt,1); wr16(fmt+2,1); wr32(fmt+4,44100); wr32(fmt+8,88200); wr16(fmt+12,2); wr16(fmt+14,16);
    CHECK(W("OutOpen",out,0u,fmt,0u,0u,0u) == 0 && opens() == 0);
    uint32_t h=rd32(out);
    for (unsigned i=0; i<8; ++i) wr16(data+2*i, (i+1)*1024);
    wr32(p,data); wr32(p+4,16);
    CHECK(W("OutPrepareHeader",h,p,32u) == 0 && W("OutWrite",h,p,32u) == 0);
    if (fail_first == 2) {
        void (*hold)(int) = dlsym(lib,"fixture_hold");
        void (*release)(void) = dlsym(lib,"fixture_release");
        int (*entered)(void) = dlsym(lib,"fixture_entered");
        int (*timed_out)(void) = dlsym(lib,"fixture_timed_out");
        int (*stops)(void) = dlsym(lib,"fixture_stops");
        if (!hold || !release || !entered || !timed_out || !stops) return 2;
        hold(1);
        pthread_t ui;
        if (pthread_create(&ui,NULL,foreground,NULL)) return 2;
        CHECK(await_count(entered,1));
        hle_al_set_paused(1);
        CHECK(!timed_out()); /* must return while the open is still blocked */
        int16_t pcm[2]; hle_al_render(pcm,1); CHECK(!pcm[0]);
        release(); pthread_join(ui,NULL);
        hle_al_set_paused(0);
        CHECK(await_count(opens,1));
        hold(2);
        hle_al_set_paused(1);
        CHECK(await_count(entered,2) && !timed_out());
        hle_al_set_paused(0); hle_al_set_paused(1); hle_al_set_paused(0);
        pull(pcm,1); CHECK(!pcm[0]); /* obsolete stream cannot consume samples */
        CHECK(!timed_out());
        release(); CHECK(await_count(stops,1) && await_count(opens,2));
        CHECK(pull_sample(pull) == 1024 && (rd32(p+16)&16));
        fprintf(stderr,"test_ds3_audio --blocked-driver: %u ok, %u failed\n",pass,fail);
        return fail != 0;
    }
    if (fail_first) {
        void (*reject)(void) = dlsym(lib,"fixture_fail_open");
        CHECK(reject != NULL); if (!reject) return 1;
        reject();
        CHECK((rd32(p+16)&0x13) == 0x12);
        hle_al_set_paused(0);
        for (int i=0; i<100 && (rd32(p+16)&16); ++i) usleep(10000);
        CHECK(opens() == 0 && (rd32(p+16)&0x13) == 3);
        hle_al_set_paused(1);
        CHECK(W("OutWrite",h,p,32u) == 0);
        usleep(30000);
        CHECK((rd32(p+16)&0x13) == 0x12);
        hle_al_set_paused(0);
        for (int i=0; i<100 && (rd32(p+16)&16); ++i) usleep(10000);
        CHECK(opens() == 0 && (rd32(p+16)&0x13) == 3);
        fprintf(stderr,"test_ds3_audio --failed-open: %u ok, %u failed\n",pass,fail);
        return fail != 0;
    }
    hle_al_set_paused(0); CHECK(await_count(opens,1));
    CHECK(pull_sample(pull) == 1024);
    lose();
    CHECK(await_count(opens,2)); CHECK(pull_sample(pull) == 2048);
    lose(); hle_al_set_paused(1); hle_al_set_paused(0);
    usleep(350000);
    CHECK(await_count(opens,3)); CHECK(pull_sample(pull) == 3072 && (rd32(p+16)&16));
    hle_al_set_paused(1);
    fprintf(stderr, "test_ds3_audio --device-loss: %u ok, %u failed\n",pass,fail);
    return fail != 0;
}
static int retain_pcm(void) {
    setenv("DS3_DSOUND_CURSORS","wine",1);
    setenv("DS3_DSOUND_PCM","retain",1);
    setenv("ME1_AL_RATE","48000",1);
    setenv("ME1_AL_SINK","manual",1);
    rt_init();
    uint32_t p=ghalloc(16,1), out=ghalloc(4,1), fmt=ghalloc(18,1), desc=ghalloc(36,1);
    wr16(fmt,1); wr16(fmt+2,2); wr32(fmt+4,48000); wr32(fmt+8,192000); wr16(fmt+12,4); wr16(fmt+14,16);
    CHECK(API("dsound.dll","DirectSoundCreate8",0u,out,0u)==0 && rd32(out));
    uint32_t dev=rd32(out);
    wr32(desc,36); wr32(desc+8,96256); wr32(desc+16,fmt);
    CHECK(API("dsound.com","IDirectSound8::CreateSoundBuffer",dev,desc,out,0u)==0 && rd32(out));
    uint32_t b=rd32(out);
    CHECK(D("Lock",b,0u,96256u,p,p+4,p+8,p+12,0u)==0);
    uint32_t init=rd32(p);
    CHECK(D("Play",b,0u,0u,1u)==0);
    CHECK(D("Unlock",b,init,96256u,0u,0u)==0);
    int16_t pcm[512];
    hle_al_render(pcm,256);
    CHECK(pcm[0]==0 && D("GetCurrentPosition",b,p,p+4)==0 && rd32(p+4)==1024);
    CHECK(D("SetCurrentPosition",b,0u)==0);
    CHECK(D("Lock",b,0u,8192u,p,p+4,p+8,p+12,0u)==0);
    uint32_t base=rd32(p);
    for (unsigned i=0;i<4096;++i) wr16(base+2*i,6000);
    CHECK(D("Unlock",b,base,8192u,0u,0u)==0);
    hle_al_render(pcm,256); CHECK(pcm[0]==6000);
    for (unsigned i=1;i<94;++i) {
        hle_al_render(pcm,256);
        if (i==8) CHECK(pcm[0]==0 && pcm[511]==0); /* never written */
    }
    CHECK(D("Lock",b,0u,1024u,p,p+4,p+8,p+12,0u)==0);
    for (unsigned i=0;i<512;++i) wr16(base+2*i,12000);
    hle_al_render(pcm,256); CHECK(pcm[0]==0 && pcm[511]==0); /* Locked, not yet re-Unlocked: no stale replay */
    CHECK(D("Unlock",b,base,1024u,0u,0u)==0);
    for (unsigned i=1;i<94;++i) hle_al_render(pcm,256);
    hle_al_render(pcm,256); CHECK(pcm[0]==12000 && pcm[511]==12000);
    CHECK(API("dsound.com","IDirectSound8::DuplicateSoundBuffer",dev,b,out)==0 && rd32(out));
    uint32_t clone=rd32(out);
    CHECK(D("Lock",b,0u,1024u,p,p+4,p+8,p+12,0u)==0);
    CHECK(D("Lock",clone,512u,1024u,p,p+4,p+8,p+12,0u)!=0);
    CHECK(D("Unlock",clone,base,1024u,0u,0u)!=0);
    CHECK(D("Unlock",b,base,1024u,0u,0u)==0);
    CHECK(D("Release",b)==0);
    CHECK(D("Play",clone,0u,0u,1u)==0);
    hle_al_render(pcm,256); CHECK(pcm[0]==12000 && pcm[511]==12000);
    CHECK(D("Release",clone)==0);
    fprintf(stderr,"test_ds3_audio --retain-pcm: %u ok, %u failed\n",pass,fail);
    return fail != 0;
}
static int wine_cursors(int variant) {
    int donor=variant==2;
    setenv("DS3_DSOUND_CURSORS", donor ? "donor30" : variant ? "wine9" : "wine", 1);
    setenv("ME1_AL_RATE", "48000", 1);
    setenv("ME1_AL_SINK", "manual", 1);
    rt_init();
    uint32_t p=ghalloc(16,1), out=ghalloc(4,1), fmt=ghalloc(18,1), desc=ghalloc(36,1);
    wr16(fmt,1); wr16(fmt+2,2); wr32(fmt+4,48000); wr32(fmt+8,192000); wr16(fmt+12,4); wr16(fmt+14,16);
    CHECK(API("dsound.dll","DirectSoundCreate8",0u,out,0u)==0 && rd32(out));
    uint32_t dev=rd32(out);
    wr32(desc,36); wr32(desc+8,4096); wr32(desc+16,fmt);
    CHECK(API("dsound.com","IDirectSound8::CreateSoundBuffer",dev,desc,out,0u)==0 && rd32(out));
    uint32_t b=rd32(out);
    int16_t samples[512];
    CHECK(D("SetCurrentPosition",b,3072u)==0);
    CHECK(D("GetCurrentPosition",b,p,p+4)==0 && rd32(p)==3072 && rd32(p+4)==3072);
    if (variant) {
        CHECK(D("Lock",b,3072u,4u,p,p+4,p+8,p+12,0u)==0);
        uint32_t start=rd32(p);
        CHECK(D("Unlock",b,start,4u,0u,0u)==0);
        CHECK(D("Play",b,0u,0u,1u)==0);
        CHECK(D("GetCurrentPosition",b,p,p+4)==0 && rd32(p)==3072 && rd32(p+4)==(donor?1024u:896u));
        CHECK(D("Lock",b,0u,4u,p,p+4,p+8,p+12,1u)==0 && rd32(p)==start-(donor?2048u:2176u));
        CHECK(D("Unlock",b,rd32(p),4u,0u,0u)==0);
        hle_al_render(samples,256);
        CHECK(D("GetCurrentPosition",b,p,p+4)==0 && rd32(p)==0 && rd32(p+4)==(donor?2048u:1920u));
        CHECK(D("SetFrequency",b,donor?10079u:22079u)==0);
        CHECK(D("GetCurrentPosition",b,p,p+4)==0 && rd32(p)==0 && rd32(p+4)==(donor?1208u:880u));
        CHECK(D("Stop",b)==0);
        CHECK(D("GetCurrentPosition",b,p,p+4)==0 && rd32(p)==rd32(p+4));
        CHECK(D("Release",b)==0);
        if (donor) {
            wr32(desc+8,96256);
            CHECK(API("dsound.com","IDirectSound8::CreateSoundBuffer",dev,desc,out,0u)==0 && rd32(out));
            b=rd32(out);
            CHECK(D("Play",b,0u,0u,1u)==0);
            CHECK(D("GetCurrentPosition",b,p,p+4)==0 && rd32(p)==0 && rd32(p+4)==5760);
            CHECK(D("Release",b)==0);
        }
        fprintf(stderr,"test_ds3_audio --%s-cursors: %u ok, %u failed\n",donor?"donor30":"wine9",pass,fail);
        return fail != 0;
    }
    CHECK(D("Lock",b,99u,4u,p,p+4,p+8,p+12,1u)==0);
    uint32_t pos=rd32(p);
    CHECK(D("Unlock",b,pos,4u,0u,0u)==0);
    CHECK(D("Lock",b,3072u,4u,p,p+4,p+8,p+12,0u)==0 && rd32(p)==pos);
    CHECK(D("Unlock",b,pos,4u,0u,0u)==0);
    CHECK(D("Play",b,0u,0u,1u)==0);
    hle_al_render(samples,256);
    CHECK(D("GetCurrentPosition",b,p,p+4)==0 && rd32(p)==3072 && rd32(p+4)==0);
    hle_al_render(samples,256);
    CHECK(D("GetCurrentPosition",b,p,p+4)==0 && rd32(p)==0 && rd32(p+4)==1024);
    CHECK(D("Stop",b)==0);
    CHECK(D("GetCurrentPosition",b,p,p+4)==0 && !rd32(p) && !rd32(p+4));
    CHECK(D("SetFrequency",b,24000u)==0 && D("Play",b,0u,0u,1u)==0);
    hle_al_render(samples,1);
    CHECK(D("GetCurrentPosition",b,p,p+4)==0 && !rd32(p) && !rd32(p+4));
    hle_al_render(samples,1);
    CHECK(D("GetCurrentPosition",b,p,p+4)==0 && !rd32(p) && rd32(p+4)==4);
    CHECK(D("Stop",b)==0 && D("SetFrequency",b,48000u)==0 && D("SetCurrentPosition",b,4092u)==0);
    CHECK(D("Play",b,0u,0u,0u)==0);
    hle_al_render(samples,1);
    CHECK(D("GetCurrentPosition",b,p,p+4)==0 && rd32(p)==4092 && !rd32(p+4));
    hle_al_render(samples,1);
    CHECK(D("GetCurrentPosition",b,p,p+4)==0 && !rd32(p) && !rd32(p+4));
    CHECK(D("GetStatus",b,p)==0 && !rd32(p));
    CHECK(D("SetCurrentPosition",b,128u)==0);
    CHECK(API("dsound.com","IDirectSound8::DuplicateSoundBuffer",dev,b,out)==0 && rd32(out));
    uint32_t clone=rd32(out);
    CHECK(D("GetCurrentPosition",clone,p,p+4)==0 && !rd32(p) && !rd32(p+4));
    CHECK(D("Release",clone)==0);
    CHECK(D("Release",b)==0);
    wr32(desc+8,96256);
    CHECK(API("dsound.com","IDirectSound8::CreateSoundBuffer",dev,desc,out,0u)==0 && rd32(out));
    b=rd32(out);
    CHECK(D("Play",b,0u,0u,1u)==0);
    CHECK(D("Lock",b,0u,256u,p,p+4,p+8,p+12,0u)==0);
    uint32_t tracked=rd32(p);
    CHECK(D("SetFrequency",b,44100u)==0);
    CHECK(D("Unlock",b,tracked,256u,0u,0u)==0);
    CHECK(D("SetFrequency",b,48000u)==0 && D("Play",b,0u,0u,1u)==0);
    CHECK(D("Lock",b,0u,256u,p,p+4,p+8,p+12,0u)==0);
    CHECK(D("Unlock",b,tracked,256u,0u,0u)==0);
    CHECK(D("Stop",b)==0 && D("SetCurrentPosition",b,96252u)==0 && D("Play",b,0u,0u,1u)==0);
    hle_al_render(samples,256);
    CHECK(D("GetCurrentPosition",b,p,p+4)==0 && rd32(p)==96252 && rd32(p+4)==96252);
    CHECK(D("Stop",b)==0 && D("SetCurrentPosition",b,0u)==0);
    CHECK(D("Play",b,0u,0u,1u)==0);
    hle_al_render(samples,256);
    CHECK(D("GetCurrentPosition",b,p,p+4)==0 && !rd32(p) && !rd32(p+4));
    CHECK(D("Lock",b,0u,8192u,p,p+4,p+8,p+12,1u)==0);
    memset(M+rd32(p),0,rd32(p+4));
    CHECK(D("Unlock",b,rd32(p),rd32(p+4),rd32(p+8),rd32(p+12))==0);
    for (unsigned i=0;i<8;++i) hle_al_render(samples,256);
    CHECK(D("GetCurrentPosition",b,p,p+4)==0 && rd32(p)==7168 && rd32(p+4)==8192);
    CHECK(D("Lock",b,8192u,256u,p,p+4,p+8,p+12,0u)==0);
    uint32_t late=rd32(p);
    for (unsigned i=0;i<128;++i) wr16(late+2*i,12000);
    CHECK(D("Lock",b,8704u,512u,p,p+4,p+8,p+12,0u)==0);
    for (unsigned i=0;i<256;++i) wr16(rd32(p)+2*i,6000);
    CHECK(D("Unlock",b,rd32(p),rd32(p+4),rd32(p+8),rd32(p+12))==0);
    hle_al_render(samples,256);
    CHECK(samples[0]==0 && samples[255]==0 && samples[256]==6000 && samples[511]==6000);
    CHECK(D("GetCurrentPosition",b,p,p+4)==0 && rd32(p)==8192 && rd32(p+4)==9216);
    CHECK(D("Unlock",b,late,256u,0u,0u)==0);
    for (unsigned i=0;i<94;++i) hle_al_render(samples,256);
    CHECK(samples[0]==12000 && samples[511]==0); /* late completed block is heard on the next lap */
    CHECK(D("Release",b)==0);
    CHECK(API("dsound.com","IDirectSound8::CreateSoundBuffer",dev,desc,out,0u)==0 && rd32(out));
    b=rd32(out);
    CHECK(D("SetCurrentPosition",b,96252u)==0 && D("Play",b,0u,0u,1u)==0);
    hle_al_render(samples,256);
    CHECK(D("GetCurrentPosition",b,p,p+4)==0 && rd32(p)==96252 && rd32(p+4)==96252);
    CHECK(D("Lock",b,0u,8192u,p,p+4,p+8,p+12,1u)==0 && rd32(p+4)==4 && rd32(p+12)==8188);
    memset(M+rd32(p),0,rd32(p+4)); memset(M+rd32(p+8),0,rd32(p+12));
    CHECK(D("Unlock",b,rd32(p),rd32(p+4),rd32(p+8),rd32(p+12))==0);
    hle_al_render(samples,256);
    CHECK(D("GetCurrentPosition",b,p,p+4)==0 && rd32(p)==96252 && rd32(p+4)==1020);
    for (unsigned i=0;i<92;++i) hle_al_render(samples,256);
    hle_al_render(samples,256);
    CHECK(D("GetCurrentPosition",b,p,p+4)==0 && rd32(p+4)==96252);
    CHECK(D("Lock",b,0u,8u,p,p+4,p+8,p+12,1u)==0 && rd32(p+4)==4 && rd32(p+12)==4);
    uint32_t wrap_head=rd32(p),wrap_tail=rd32(p+8);
    wr16(wrap_head,12000); wr16(wrap_tail,12000);
    hle_al_render(samples,2);
    CHECK(!samples[0] && !samples[2]);
    CHECK(D("Unlock",b,wrap_head,4u,wrap_tail,4u)==0);
    for (unsigned i=0;i<94;++i) hle_al_render(samples,256);
    CHECK(samples[508]==12000 && samples[510]==12000); /* late split write is audible on the next lap */
    CHECK(D("Release",b)==0);
    fprintf(stderr,"wine_cursors: %u ok, %u failed\n",pass,fail);
    return fail != 0;
}
static int published_tail(int donor, int edges) {
    setenv("DS3_DSOUND_CURSORS", donor ? "donor30" : "wine9", 1);
    setenv("DS3_DSOUND_PCM", edges ? "fresh" : "retain", 1);
    setenv("ME1_AL_RATE", "48000", 1);
    setenv("ME1_AL_SINK", "manual", 1);
    rt_init();
    uint32_t p=ghalloc(16,1), out=ghalloc(4,1), fmt=ghalloc(18,1), desc=ghalloc(36,1);
    wr16(fmt,1); wr16(fmt+2,2); wr32(fmt+4,48000); wr32(fmt+8,192000); wr16(fmt+12,4); wr16(fmt+14,16);
    CHECK(API("dsound.dll","DirectSoundCreate8",0u,out,0u)==0 && rd32(out));
    uint32_t dev=rd32(out);
    wr32(desc,36); wr32(desc+8,96256); wr32(desc+16,fmt);
    CHECK(API("dsound.com","IDirectSound8::CreateSoundBuffer",dev,desc,out,0u)==0 && rd32(out));
    uint32_t b=rd32(out);
    CHECK(D("Play",b,0u,0u,1u)==0);
    int16_t samples[512];
    if (edges) {
        const uint32_t lead=donor?5760:1920;
        CHECK(D("Lock",b,4096u,256u,p,p+4,p+8,p+12,0u)==0);
        CHECK(D("Unlock",b,rd32(p),0u,0u,0u)==0);
        CHECK(D("GetCurrentPosition",b,p,p+4)==0 && rd32(p+4)==lead);
        /* Publish beyond a hole before filling it. */
        const uint32_t start[]={0,4096,2048}, count[]={2048,4096,2048};
        for (unsigned i=0;i<3;++i) {
            CHECK(D("Lock",b,start[i],count[i],p,p+4,p+8,p+12,0u)==0);
            for (uint32_t j=0,ptr=rd32(p);j<count[i];j+=2) wr16(ptr+j,6000);
            CHECK(D("Unlock",b,rd32(p),count[i],0u,0u)==0);
        }
        CHECK(D("Lock",b,0u,256u,p,p+4,p+8,p+12,1u)==0);
        CHECK(D("Unlock",b,rd32(p),256u,0u,0u)==0);
        hle_al_render(samples,256);
        CHECK(samples[0]==6000 && samples[511]==6000);
        CHECK(D("SetCurrentPosition",b,96248u)==0);
        CHECK(D("Lock",b,96248u,16u,p,p+4,p+8,p+12,0u)==0 && rd32(p+4)==8 && rd32(p+12)==8);
        CHECK(D("Unlock",b,rd32(p),4u,rd32(p+8),4u)==0);
        CHECK(D("GetCurrentPosition",b,p,p+4)==0 && rd32(p)==96248 && rd32(p+4)==4);
        CHECK(D("Release",b)==0);
        fprintf(stderr,"test_ds3_audio --published-tail-edges-%s: %u ok, %u failed\n",donor?"donor30":"wine9",pass,fail);
        return fail != 0;
    }
    CHECK(D("Lock",b,0u,1024u,p,p+4,p+8,p+12,0u)==0 && rd32(p+4)==1024);
    uint32_t base=rd32(p);
    memset(M+base,0,1024);
    CHECK(D("Unlock",b,base,1024u,0u,0u)==0);
    hle_al_render(samples,256);
    hle_al_render(samples,256);
    CHECK(D("GetCurrentPosition",b,p,p+4)==0 && rd32(p)==2048 && rd32(p+4)==1024);
    CHECK(D("Lock",b,0u,1024u,p,p+4,p+8,p+12,1u)==0 && rd32(p)==base+1024);
    memset(M+rd32(p),0,1024);
    CHECK(D("Unlock",b,rd32(p),1024u,0u,0u)==0);
    CHECK(D("GetCurrentPosition",b,p,p+4)==0 && rd32(p)==2048 && rd32(p+4)==2048);
    CHECK(D("Release",b)==0);
    fprintf(stderr,"test_ds3_audio --published-tail-%s: %u ok, %u failed\n",donor?"donor30":"wine9",pass,fail);
    return fail != 0;
}
/* RED/GREEN for the doubled block. Three properties on the same run:
 *   1. an on-time block is heard exactly once, not again on the next lap;
 *   2. a late block is still heard exactly once on the next lap (the №72
 *      recovery must survive), not a second time on the lap after that;
 *   3. when the mixer passes the published tail, GetCurrentPosition still
 *      clamps write to the tail, so the guest's x stays 0 and the №71
 *      cursor-delay cascade cannot return. */
static int retain_double(void) {
    setenv("DS3_DSOUND_CURSORS","donor30",1);
    setenv("DS3_DSOUND_PCM","retain",1);
    setenv("ME1_AL_RATE","48000",1);
    setenv("ME1_AL_SINK","manual",1);
    rt_init();
    uint32_t p=ghalloc(16,1), out=ghalloc(4,1), fmt=ghalloc(18,1), desc=ghalloc(36,1);
    wr16(fmt,1); wr16(fmt+2,2); wr32(fmt+4,48000); wr32(fmt+8,192000); wr16(fmt+12,4); wr16(fmt+14,16);
    CHECK(API("dsound.dll","DirectSoundCreate8",0u,out,0u)==0 && rd32(out));
    uint32_t dev=rd32(out);
    wr32(desc,36); wr32(desc+8,96256); wr32(desc+16,fmt);
    CHECK(API("dsound.com","IDirectSound8::CreateSoundBuffer",dev,desc,out,0u)==0 && rd32(out));
    uint32_t b=rd32(out);
    CHECK(D("Play",b,0u,0u,1u)==0);
    int16_t s[512];

    /* 1. On-time block at frame 0 = 1111: heard now, must not return. */
    CHECK(D("Lock",b,0u,1024u,p,p+4,p+8,p+12,0u)==0 && rd32(p+4)==1024);
    uint32_t base=rd32(p);
    for (unsigned i=0;i<512;++i) wr16(base+2*i,1111);
    CHECK(D("Unlock",b,base,1024u,0u,0u)==0);
    hle_al_render(s,256);
    CHECK(s[0]==1111 && s[511]==1111);
    for (unsigned i=0;i<93;++i) hle_al_render(s,256);   /* mixer back to frame 0 */
    hle_al_render(s,256);                              /* second visit */
    CHECK(s[0]==0 && s[511]==0);   /* RED in №72: 1111 is heard again here */

    /* 2. Late block at frame 1024 = 2222, completed after the mixer passed. */
    for (unsigned i=0;i<4;++i) hle_al_render(s,256);   /* mixer -> frame 1280 */
    CHECK(D("Lock",b,4096u,1024u,p,p+4,p+8,p+12,0u)==0);
    uint32_t late=rd32(p);
    for (unsigned i=0;i<512;++i) wr16(late+2*i,2222);
    CHECK(D("Unlock",b,late,1024u,0u,0u)==0);          /* 1024 < 1280: late */
    for (unsigned i=0;i<93;++i) hle_al_render(s,256);   /* mixer -> frame 1024 */
    hle_al_render(s,256);
    CHECK(s[0]==2222 && s[511]==2222); /* late block is heard once, next lap */
    for (unsigned i=0;i<93;++i) hle_al_render(s,256);
    hle_al_render(s,256);
    CHECK(s[0]==0 && s[511]==0);   /* RED in №72: 2222 returns a second time */

    /* 3. Mixer ahead of the published tail: write must clamp to the tail. */
    for (unsigned i=0;i<3;++i) hle_al_render(s,256);   /* frame 1280 -> 2048 */
    CHECK(D("GetCurrentPosition",b,p,p+4)==0);
    CHECK(rd32(p)==8192);          /* play = mixer frame 2048 * 4 */
    CHECK(rd32(p+4)==5120);        /* write = tail 1280 * 4, not mix+lead */
    uint32_t own=1280;             /* frames the producer has published */
    uint32_t x=((own - rd32(p+4)/4) % 24064 + 24064) % 24064;
    CHECK(x==0);                   /* allowance stays open: no cascade */
    CHECK(D("Release",b)==0);
    fprintf(stderr,"test_ds3_audio --retain-double: %u ok, %u failed\n",pass,fail);
    return fail != 0;
}
int main(int argc, char** argv) {
    if (argc == 2 && !strcmp(argv[1], "--retain-double")) return retain_double();
    if (argc == 2 && !strcmp(argv[1], "--published-tail-donor30")) return published_tail(1,0);
    if (argc == 2 && !strcmp(argv[1], "--published-tail-wine9")) return published_tail(0,0);
    if (argc == 2 && !strcmp(argv[1], "--published-tail-edges-donor30")) return published_tail(1,1);
    if (argc == 2 && !strcmp(argv[1], "--published-tail-edges-wine9")) return published_tail(0,1);
    if (argc == 2 && !strcmp(argv[1], "--wine-cursors")) return wine_cursors(0);
    if (argc == 2 && !strcmp(argv[1], "--wine9-cursors")) return wine_cursors(1);
    if (argc == 2 && !strcmp(argv[1], "--donor30-cursors")) return wine_cursors(2);
    if (argc == 2 && !strcmp(argv[1], "--retain-pcm")) return retain_pcm();
    if (argc == 2 && !strcmp(argv[1], "--rate-commit")) return rate_commit(0);
    if (argc == 2 && !strcmp(argv[1], "--rate-failure")) return rate_commit(1);
    if (argc == 2 && !strcmp(argv[1], "--device-loss")) return device_loss(0);
    if (argc == 2 && !strcmp(argv[1], "--failed-open")) return device_loss(1);
    if (argc == 2 && !strcmp(argv[1], "--blocked-driver")) return device_loss(2);
    if (ds3_audio_trace_init) ds3_audio_trace_init();
    if (getenv("TEST_FRESH")) setenv("ME1_AL_RATE","48000",1);
    setenv("ME1_AL_SINK", "manual", 1); rt_init();
    uint32_t fmt = ghalloc(18, 1), out = ghalloc(4, 1), header = ghalloc(32, 1), pcm = ghalloc(32, 1);
    unsigned rate = hle_al_rate();
    wr16(fmt, 1); wr16(fmt + 2, 1); wr32(fmt + 4, rate); wr32(fmt + 8, rate * 2); wr16(fmt + 12, 2); wr16(fmt + 14, 16);
    for (unsigned i = 0; i < 16; ++i) wr16(pcm + 2*i, 12000);
    CHECK(W("InGetNumDevs") == 0);
    CHECK(W("InOpen", out, UINT32_MAX, fmt, 0u, 0u, 0u) == 6);
    CHECK(W("OutGetNumDevs") == 1);
    CHECK(W("OutOpen", 0u, UINT32_MAX, fmt, 0u, 0u, 1u) == 0); /* format query */
    uint32_t ev = API("kernel32.dll", "CreateEventW", 0u, 0u, 0u, 0u);
    uint32_t opened = W("OutOpen", out, UINT32_MAX, fmt, ev, 0u, 0x50000u);
    CHECK(opened == 0 && rd32(out));
    int16_t samples[64];
    if (!opened && rd32(out)) {
        uint32_t h = rd32(out);
        CHECK(API("kernel32.dll", "WaitForSingleObject", ev, 0u) == 0);
        wr32(header, pcm); wr32(header+4, 32);
        CHECK(W("OutWrite", h, header, 32u) == 34); /* WAVERR_UNPREPARED */
        CHECK(W("OutPrepareHeader", h, header, 32u) == 0);
        CHECK(W("OutSetVolume", h, 0x0000ffffu) == 0);
        CHECK(W("OutWrite", h, header, 32u) == 0 && (rd32(header+16) & 0x10));
        CHECK(W("OutClose", h) == 33); /* WAVERR_STILLPLAYING */
        CHECK(W("OutPause", h) == 0); hle_al_render(samples, 4);
        CHECK(samples[0] == 0 && (rd32(header+16) & 0x10));
        CHECK(W("OutRestart", h) == 0); hle_al_render(samples, 16);
        CHECK(samples[0] == 12000 && samples[1] == 0);
        CHECK((rd32(header+16) & 0x13) == 3);
        CHECK(API("kernel32.dll", "WaitForSingleObject", ev, 0u) == 0);
        CHECK(W("OutWrite", h, header, 32u) == 0 && W("OutReset", h) == 0 && (rd32(header+16) & 0x13) == 3);
        uint32_t queue = ghalloc(65*32,1);
        for (unsigned i=0; i<65; ++i) {
            uint32_t q = queue+i*32;
            wr32(q,pcm); wr32(q+4,2);
            CHECK(W("OutPrepareHeader",h,q,32u) == 0);
            CHECK(W("OutWrite",h,q,32u) == (i==64 ? 7u : 0u));
        }
        CHECK((rd32(queue+64*32+16)&0x13) == 2);
        hle_al_render(samples,1);
        CHECK((rd32(queue+16)&0x13) == 3);
        CHECK(W("OutWrite",h,queue+64*32,32u) == 0);
        CHECK(W("OutReset",h) == 0);
        for (unsigned i=0; i<65; ++i) CHECK((rd32(queue+i*32+16)&0x13) == 3);
        CHECK(W("OutUnprepareHeader", h, header, 32u) == 0 && W("OutClose", h) == 0);
    }
    CHECK(API("dsound.dll", "DirectSoundCreate8", 0u, out, 0u) == 0 && rd32(out));
    uint32_t desc = ghalloc(36, 1), dev = rd32(out);
    wr32(desc, 36); wr32(desc+8, 6); wr32(desc+16, fmt);
    CHECK(API("dsound.com", "IDirectSound8::CreateSoundBuffer", dev, desc, out, 0u) == 0 && rd32(out));
    uint32_t buf = rd32(out), p = ghalloc(16, 1);
    CHECK(D("Lock", buf, 0u, 6u, p, p+4, p+8, p+12, 0u) == 0);
    wr16(rd32(p), 12000); wr16(rd32(p)+2, 6000); wr16(rd32(p)+4, 2000);
    CHECK(D("Unlock", buf, rd32(p), rd32(p+4), rd32(p+8), rd32(p+12)) == 0);
    CHECK(D("SetVolume", buf, (uint32_t)-2000) == 0);
    CHECK(D("SetFrequency", buf, 4 * rate) == 0);
    CHECK(D("Play", buf, 0u, 0u, 1u) == 0);
    hle_al_render(samples, 2);
    CHECK(samples[0] == 1200 && samples[2] == 600); /* large frequency step still wraps correctly */
    CHECK(D("GetCurrentPosition", buf, p, 0u) == 0 && rd32(p) == 4);
    {
        hle_al_set_paused(1); hle_al_render(samples, 16);
        CHECK(!samples[0] && !samples[31] && D("GetCurrentPosition", buf, p, 0u) == 0 && rd32(p) == 4);
        hle_al_set_paused(0); hle_al_render(samples, 1);
        CHECK(samples[0] == 200 && D("GetCurrentPosition", buf, p, 0u) == 0 && rd32(p) == 0);
    }
    if (getenv("TEST_FRESH")) {
        CHECK(D("Stop",buf)==0);
        wr16(fmt+2,2); wr32(fmt+8,rate*4); wr16(fmt+12,4); wr32(desc+8,96256);
        CHECK(API("dsound.com", "IDirectSound8::CreateSoundBuffer", dev, desc, out, 0u)==0);
        buf=rd32(out);
        CHECK(D("Play",buf,0u,0u,1u)==0);
        hle_al_render(samples,16);
        CHECK(samples[0]==0 && D("GetCurrentPosition",buf,p,0u)==0 && rd32(p)==0);
        CHECK(D("Lock",buf,96252u,8196u,p,p+4,p+8,p+12,0u)==0);
        CHECK(rd32(p+4)==4 && rd32(p+12)==8192);
        memset(M+rd32(p),0,rd32(p+4)); memset(M+rd32(p+8),0,rd32(p+12));
        CHECK(D("Unlock",buf,rd32(p),rd32(p+4),rd32(p+8),rd32(p+12))==0);
        hle_al_render(samples,16);
        CHECK(samples[0]==0 && D("GetCurrentPosition",buf,p,0u)==0 && rd32(p)==64);
        CHECK(D("Release",buf)==0);
    }
    if (ds3_audio_trace_drain) ds3_audio_trace_drain();
    if (getenv("TEST_AUDIO_OVERFLOW") && ds3_audio_trace) {
        for (unsigned i=0;i<8200;i++) ds3_audio_trace(4,0,16,48000,0);
        if (ds3_audio_trace_drain) ds3_audio_trace_drain();
        hle_al_render(samples,1);
    }
    fprintf(stderr, "test_ds3_audio: %u ok, %u failed\n", pass, fail);
    return fail != 0;
}
