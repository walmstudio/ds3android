/* Public DirectSound API characterization only. Use a disposable WINEPREFIX. */
#define COBJMACROS
#include <windows.h>
#include <dsound.h>
#include <stdio.h>
#include <string.h>

#define RING 96256u
#define CHECK(test) do { if (!(test)) { fprintf(stderr,"FAIL %s:%d: %s\n",__FILE__,__LINE__,#test); goto end; } } while (0)

int main(void) {
    IDirectSound8 *ds = NULL;
    IDirectSoundBuffer *buf = NULL;
    WAVEFORMATEX fmt = {WAVE_FORMAT_PCM, 2, 48000, 192000, 4, 16, 0};
    DSBUFFERDESC desc = {0};
    DWORD play, write, n1, n2, old_play = 0, old_write = 0, changed = 0;
    void *a, *b;
    int rc = 1;
    desc.dwSize = sizeof(desc);
    desc.dwFlags = DSBCAPS_GETCURRENTPOSITION2 | DSBCAPS_GLOBALFOCUS;
    desc.dwBufferBytes = RING;
    desc.lpwfxFormat = &fmt;
    CHECK(SUCCEEDED(DirectSoundCreate8(NULL, &ds, NULL)));
    CHECK(SUCCEEDED(IDirectSound8_SetCooperativeLevel(ds, GetDesktopWindow(), DSSCL_NORMAL)));
    CHECK(SUCCEEDED(IDirectSound8_CreateSoundBuffer(ds, &desc, &buf, NULL)));
    CHECK(SUCCEEDED(IDirectSoundBuffer_SetCurrentPosition(buf, RING - 4)));
    CHECK(SUCCEEDED(IDirectSoundBuffer_GetCurrentPosition(buf, &play, &write)));
    CHECK(play == RING - 4 && write == play);
    CHECK(SUCCEEDED(IDirectSoundBuffer_Lock(buf, 12, 12, &a, &n1, &b, &n2, DSBLOCK_FROMWRITECURSOR)));
    CHECK(a && b && n1 == 4 && n2 == 8 && (char *)a == (char *)b + RING - 4);
    memset(a, 0, n1); memset(b, 0, n2);
    CHECK(SUCCEEDED(IDirectSoundBuffer_Unlock(buf, a, n1, b, n2)));
    printf("stopped play=%lu write=%lu lock=%lu+%lu\n", (unsigned long)play, (unsigned long)write, (unsigned long)n1, (unsigned long)n2);
    CHECK(SUCCEEDED(IDirectSoundBuffer_SetCurrentPosition(buf, 0)));
    CHECK(SUCCEEDED(IDirectSoundBuffer_Play(buf, 0, 0, DSBPLAY_LOOPING)));
    for (unsigned i = 0; i < 80; ++i) {
        CHECK(SUCCEEDED(IDirectSoundBuffer_GetCurrentPosition(buf, &play, &write)));
        CHECK(play < RING && write < RING && ((play | write) & 3) == 0);
        if (play != old_play || write != old_write) {
            printf("playing t=%lu play=%lu write=%lu\n", (unsigned long)GetTickCount(), (unsigned long)play, (unsigned long)write);
            old_play = play; old_write = write; ++changed;
        }
        Sleep(5);
    }
    CHECK(changed > 0);
    CHECK(SUCCEEDED(IDirectSoundBuffer_Stop(buf)));
    puts("PASS public DirectSound stopped cursor, write-relative wrapped lock and moving cursor pairs");
    rc = 0;
end:
    if (buf) IDirectSoundBuffer_Release(buf);
    if (ds) IDirectSound8_Release(ds);
    return rc;
}
