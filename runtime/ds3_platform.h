/* Portable front-end contract: no OS-specific window types cross this boundary. */
#pragma once
#include <stdint.h>
typedef struct Ds3Platform {
    void (*window)(void *opaque, unsigned width, unsigned height);
    /* Presenter thread, only after successful real window swap/post; not a
     * guest Present submission, surfaceless render or fake-window pbuffer. */
    void (*present)(void *opaque);
    int (*audio_open)(int rate, const char **name);
    void (*audio_start)(void);
    void (*poll_input)(void *opaque);
    void (*exit)(int code);
    void *opaque;
} Ds3Platform;
/* One session per process. Caller supplies Task-2-validated game_dir and a separate
 * writable docs_dir. The exact generated image is currently out/exe/image.bin. */
int ds3_main(const Ds3Platform *platform, const char *game_dir, const char *docs_dir);
