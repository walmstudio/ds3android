#include "audio-out.h"
static SDL_AudioDeviceID g_adev;
static int audio_trace_rate;
static int audio_native_rate;
static const char* audio_native_name;
static void SDLCALL audio_cb(void* u, Uint8* stream, int len) {
    (void)u;
    ds3_audio_trace(DAT_BEGIN,0,len/4,audio_trace_rate,0);
    hle_al_render((int16_t*)stream, len / 4);
    ds3_audio_trace(DAT_END,0,len/4,audio_trace_rate,0);
}
static int audio_open(int rate, const char** name) {
    if (audio_native_rate) { *name=audio_native_name; return audio_native_rate; }
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) { fprintf(stderr, "[linux] SDL audio: %s\n", SDL_GetError()); return 0; }
    SDL_AudioSpec want, have; memset(&want, 0, sizeof want);
    want.freq = rate; want.format = AUDIO_S16SYS; want.channels = 2; want.samples = rate > 32000 ? 1024 : 512; want.callback = audio_cb;
    g_adev = SDL_OpenAudioDevice(NULL, 0, &want, &have, SDL_AUDIO_ALLOW_FREQUENCY_CHANGE | SDL_AUDIO_ALLOW_SAMPLES_CHANGE);
    if (!g_adev) { fprintf(stderr, "[linux] SDL_OpenAudioDevice: %s\n", SDL_GetError()); return 0; }
    static char nm[96]; snprintf(nm, sizeof nm, "SDL2 %s", SDL_GetCurrentAudioDriver() ? SDL_GetCurrentAudioDriver() : "audio");
    *name = nm;
    audio_trace_rate=have.freq;
    ds3_audio_trace(DAT_SINK,0,have.freq,have.samples,0);
    fprintf(stderr, "[linux] audio: %s, %d Hz, %d frames per callback\n", nm, have.freq, have.samples);
    return have.freq;
}
static void audio_start(void) {
    if (audio_native_rate) {
        int rc=ds_audio_start(hle_al_render);
        if (rc<0) {
            fprintf(stderr,"[ds3-audio-out] cannot activate %s (%d)\n",audio_native_name,rc);
            ds_audio_close();
            fflush(NULL);
            _Exit(2);
        }
    } else if (g_adev) SDL_PauseAudioDevice(g_adev, 0);
}
static int audio_preflight(void) {
    const char* mode=getenv("DS3_AUDIO_BACKEND");
    const char* cursors=getenv("DS3_DSOUND_CURSORS");
    int sdl=mode && !strcmp(mode,"sdl");
    if (!cursors || !*cursors) {
        cursors=sdl?"legacy":"wine";
        if (setenv("DS3_DSOUND_CURSORS",cursors,1)) return 0;
    }
    if (strcmp(cursors,"wine") && strcmp(cursors,"wine9") && strcmp(cursors,"donor30") && (!sdl || strcmp(cursors,"legacy"))) {
        fprintf(stderr,"[ds3-audio-out] invalid mode/cursors combination\n"); return 0;
    }
    if (sdl) return 1;
    const char* sink=getenv("ME1_AL_SINK");
    if (sink && (!strcmp(sink,"manual") || !strcmp(sink,"null"))) {
        fprintf(stderr,"[ds3-audio-out] manual/null sink conflicts with native device selection\n"); return 0;
    }
    const char *r=getenv("ME1_AL_RATE"), *lite=getenv("ME1_SHADER_LITE");
    int want=r && atoi(r)>=8000?atoi(r):!r && lite && *lite=='1'?32000:48000;
    audio_native_rate=ds_audio_open(mode,getenv("DS3_ALSA_DEVICE"),want,&audio_native_name);
    if (audio_native_rate<8000) return 0;
    return 1;
}
