/* libds3run.so: small arm64 executable for the launcher's test button (Android allows exec() only from the native
 * library folder). Loads libds3game.so from its own folder and runs the game without a window: the GLES backend
 * renders offscreen, frames can be saved with ME1_FRAME_DUMP. Usage as test_exe: <game dir> <docs dir> [image dir] */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <dlfcn.h>
#include <limits.h>
#include "ds3_platform.h"
static void (*on_sig)(int);
static void sig(int s) { if (on_sig) on_sig(s); _exit(128 + s); }
int main(int argc, char** argv) {
    char self[PATH_MAX]; ssize_t n = readlink("/proc/self/exe", self, sizeof self - 32);
    if (n <= 0) { fprintf(stderr, "[run] readlink /proc/self/exe failed\n"); return 2; }
    self[n] = 0; char* sl = strrchr(self, '/'); strcpy(sl + 1, "libds3game.so");
    void* h = dlopen(self, RTLD_NOW);
    if (!h) { fprintf(stderr, "[run] dlopen %s: %s\n", self, dlerror()); return 2; }
    int (*m)(const Ds3Platform*,const char*,const char*) = dlsym(h, "ds3_main");
    if (!m || argc<3) { fprintf(stderr, "[run] ds3_main missing or usage: game docs\n"); return 2; }
    signal(SIGTERM, sig); signal(SIGINT, sig);
    setvbuf(stdout, NULL, _IOLBF, 0);
    return m(NULL, argv[1], argv[2]);
}
