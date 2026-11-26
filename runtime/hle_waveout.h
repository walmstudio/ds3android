/* Private implementation included by hle_al.c: WinMM PCM on the shared mixer.
 * The guest owns WAVEHDR and samples while queued. Only native event signals
 * (not guest code) execute on the output thread. */
enum { MM_HANDLE=5, MM_NODRIVER=6, MM_NOMEM=7, MM_NOTSUPPORTED=8, MM_PARAM=11,
       WAV_FORMAT=32, WAV_PLAYING=33, WAV_UNPREPARED=34, WH_DONE=1, WH_PREPARED=2, WH_QUEUE=16 };
#define WAVE_QUEUE 64
typedef struct { uint32_t header, data, frames; } WaveItem;
typedef struct {
    uint32_t h, event, volume;
    int head, count, paused;
    DSB fmt;
    uint64_t fp;
    WaveItem q[WAVE_QUEUE];
} Wave;
static Wave g_wave[8];
static uint32_t g_wave_next = 0x57410000;
static int wave_span(uint32_t p, uint32_t n) { return p && n && p <= UINT32_MAX - (n - 1); }
static Wave* wave_get(uint32_t h) {
    for (unsigned i = 0; h && i < sizeof g_wave / sizeof *g_wave; ++i) if (g_wave[i].h == h) return &g_wave[i];
    return NULL;
}
static void wave_done(Wave* w) {
    uint32_t p = w->q[w->head].header + 16;
    wr32(p, (rd32(p) | WH_DONE) & ~WH_QUEUE);
    w->head = (w->head + 1) % WAVE_QUEUE; --w->count;
    if (w->event) hle_event_signal(w->event);
}
static int wave_mix(float* mix, int n) {
    int active = 0;
    for (unsigned k = 0; k < sizeof g_wave / sizeof *g_wave; ++k) {
        Wave* w = &g_wave[k];
        if (!w->h || !w->count || w->paused) continue;
        ++active;
        uint64_t step = ((uint64_t)w->fmt.freq << 32) / g_rate;
        float gl = (w->volume & 65535) / 65535.0f, gr = (w->volume >> 16) / 65535.0f;
        for (int i = 0; i < n && w->count; ++i) {
            WaveItem* q = &w->q[w->head];
            uint32_t index = w->fp >> 32, next = index + 1 < q->frames ? index + 1 : index;
            float a,b,c,d, t = (uint32_t)w->fp * (1.0f / 4294967296.0f);
            ds_sample(&w->fmt, M + q->data, index, &a, &b);
            ds_sample(&w->fmt, M + q->data, next, &c, &d);
            mix[2*i] += (a+(c-a)*t)*gl; mix[2*i+1] += (b+(d-b)*t)*gr;
            w->fp += step;
            while (w->count && (w->fp >> 32) >= w->q[w->head].frames) {
                w->fp -= (uint64_t)w->q[w->head].frames << 32;
                wave_done(w);
            }
        }
        if (!w->count) w->fp = 0;
    }
    return active;
}
static void wave_num(CPU* c) { al_init(); RET(g_on != 0); }
static void wave_open(CPU* c) {
    uint32_t out=ARG(0), id=ARG(1), f=ARG(2), event=ARG(3), flags=ARG(5), mode=flags & 0x70000u;
    int query = flags & 1;
    if ((!query && !wave_span(out, 4)) || !wave_span(f, 16)) { RET(MM_PARAM); return; }
    if (!query) wr32(out, 0);
    al_init();
    if (!g_on) { RET(MM_NODRIVER); return; }
    if (id != 0 && id != UINT32_MAX) { RET(2); return; } /* BADDEVICEID */
    if ((flags & ~0x70009u) || (mode != 0 && mode != 0x50000u)) { RET(MM_NOTSUPPORTED); return; }
    unsigned tag=rd16(f), ch=rd16(f+2), rate=rd32(f+4), align=rd16(f+12), bits=rd16(f+14);
    if (tag != 1 || (ch != 1 && ch != 2) || (bits != 8 && bits != 16) ||
        rate < 4000 || rate > 192000 || align != ch*bits/8 || rd32(f+8) != rate*align) { RET(WAV_FORMAT); return; }
    if (query) { RET(0); return; }
    if (mode && !event) { RET(MM_PARAM); return; }
    if (!out_start()) { RET(MM_NODRIVER); return; }
    LK;
    Wave* w = NULL;
    for (unsigned i = 0; i < sizeof g_wave / sizeof *g_wave; ++i) if (!g_wave[i].h) { w = &g_wave[i]; break; }
    if (w) {
        *w = (Wave){.h=++g_wave_next, .event=mode ? event : 0, .volume=UINT32_MAX,
                    .fmt={.ch=ch,.bits=bits,.align=align,.freq=rate}};
        wr32(out, w->h);
        if (w->event) hle_event_signal(w->event);
    }
    UL; RET(w ? 0 : MM_NOMEM);
}
static void wave_caps(CPU* c) {
    uint32_t id=ARG(0), p=ARG(1), n=ARG(2);
    al_init(); if (!g_on) { RET(MM_NODRIVER); return; }
    LK; int valid = id == UINT32_MAX || !id || wave_get(id); UL;
    if (!valid) { RET(2); return; }
    if (n > 52) n = 52;
    if (!wave_span(p,n)) { RET(MM_PARAM); return; }
    uint8_t caps[52] = {1,0,1,0,0,0,1,0};
    memcpy(caps+8,"DS3 PCM mixer",14); caps[40]=0xff; caps[41]=0xff; caps[42]=0x0f;
    caps[44]=2; caps[48]=12; /* volume + independent left/right */
    memcpy(M+p,caps,n); RET(0);
}
static void wave_ctl(CPU* c, unsigned op) {
    LK; Wave* w = wave_get(ARG(0)); uint32_t r = 0;
    if (!w) r = MM_HANDLE;
    else switch (op) {
    case 0: if (w->count) r=WAV_PLAYING; else { if (w->event) hle_event_signal(w->event); w->h=0; } break;
    case 1: w->paused=1; break;
    case 2: w->paused=0; break;
    case 3: while (w->count) wave_done(w); w->paused=0; w->fp=0; break;
    case 4: w->volume=ARG(1); break;
    }
    UL; RET(r);
}
static void wave_head(CPU* c, unsigned op) {
    uint32_t p=ARG(1), r=0;
    LK; Wave* w = wave_get(ARG(0));
    if (!w) r=MM_HANDLE;
    else if (ARG(2) < 32 || !wave_span(p,32)) r=MM_PARAM;
    else {
        uint32_t flags=rd32(p+16), data=rd32(p), size=rd32(p+4);
        if (flags & WH_QUEUE) r=WAV_PLAYING;
        else if (op == 0) wr32(p+16, flags | WH_PREPARED);
        else if (op == 1) wr32(p+16, flags & ~WH_PREPARED);
        else if (!(flags & WH_PREPARED)) r=WAV_UNPREPARED;
        else if (flags & 12u) r=MM_NOTSUPPORTED; /* WAVEHDR loop markers unsupported */
        else if (!wave_span(data,size) || size % w->fmt.align) r=MM_PARAM;
        else if (w->count >= WAVE_QUEUE) r=MM_NOMEM;
        else {
            w->q[(w->head+w->count++) % WAVE_QUEUE]=(WaveItem){p,data,size/w->fmt.align};
            wr32(p+16,(flags | WH_QUEUE) & ~WH_DONE);
        }
    }
    UL; RET(r);
}
static void wave_close(CPU* c) { wave_ctl(c,0); }
static void wave_pause(CPU* c) { wave_ctl(c,1); }
static void wave_restart(CPU* c) { wave_ctl(c,2); }
static void wave_reset(CPU* c) { wave_ctl(c,3); }
static void wave_volume(CPU* c) { wave_ctl(c,4); }
static void wave_prepare(CPU* c) { wave_head(c,0); }
static void wave_unprepare(CPU* c) { wave_head(c,1); }
static void wave_write(CPU* c) { wave_head(c,2); }
static void wave_message(CPU* c) { RET(MM_NOTSUPPORTED); }
