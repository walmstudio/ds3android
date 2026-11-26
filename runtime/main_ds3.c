/* Runs only the exact-build recompiled guest; never loads host Windows code. */
#include "hle.h"
#include "ds3_platform.h"
#include "gearmass.h"
#include "../settings/ds3settings.h"
#include <stdio.h>
#include <pthread.h>

extern const FuncEntry ds3_funcs[];
extern const ImportEntry ds3_imports[];
extern const ExportEntry ds3_exports[];
extern const unsigned ds3_nfuncs, ds3_nimports;
extern const uint32_t ds3_base, ds3_entry, ds3_image_size, ds3_image_crc;
const char *hle_fs_docs(void);
static Module exe;
static const Ds3Platform *platform;
static void on_window(unsigned w, unsigned h) { if (platform->window) platform->window(platform->opaque, w, h); }
static void on_present(void) { if (platform->present) platform->present(platform->opaque); }
static void on_input(void) { if (platform->poll_input) platform->poll_input(platform->opaque); }
static uint32_t result;
static void *entry_thread(void *unused) {
    (void)unused;
    result = rt_call(&g_cpu, exe.entry, 0, NULL);
    return NULL;
}
int ds3_main(const Ds3Platform *p, const char *game, const char *docs) {
    static int started;
    if (!game || !*game || !docs || !*docs) return 2;
    if (started++) { fprintf(stderr, "[ds3] one runtime session per process is required\n"); return 2; }
#ifdef __ANDROID__
    setenv("DS3_DSOUND_CURSORS", "donor30", 0);
    setenv("DS3_DSOUND_PCM", "retain", 0);
    /* Classify the unity-rate DS ring before AAudio reports its rate. */
    setenv("ME1_AL_RATE", "48000", 0);
#endif
    const char *renderer = getenv("DS3_RENDERER");
    if (!renderer || !*renderer) renderer = "gles";
    if (strcmp(renderer, "gles") && strcmp(renderer, "vulkan")) {
        fprintf(stderr, "[ds3] unsupported renderer '%s'; select GLES or experimental Vulkan\n", renderer);
        return 2;
    }
    const char *driver = !strcmp(renderer, "vulkan") ? getenv("DS3_VK_DRIVER") : NULL;
    if (driver && driver[0] != '/') {
        fprintf(stderr, "[ds3] invalid selected Vulkan ICD path; select system Vulkan or GLES\n");
        return 2;
    }
    fprintf(stderr, "[ds3] renderer requested=%s driver=%s\n", renderer, driver ? driver : "system");
    setenv("ME1_RENDERER", renderer, 1);
    setenv("ME1_GPU_DRIVER", driver ? driver : "system", 1);
    setenv("ME1_GPU_ZINK", "0", 1);
    setenv("ME1_SHADER_LITE", "0", 1);
    setenv("ME1_PHONE_MOD", "0", 1);
    setenv("ME1_TEX_16", "0", 1);
    setenv("ME1_OCCLUSION", "1", 1); /* measured results, not legacy last-visible heuristic */
    gm_ds3_config();
    rt_init();
    hle_fs_layout("C:\\DeadSpace3", "C:\\DeadSpace3", 1);
    if (!hle_fs_init(game, docs, NULL)) return 2;
    const char* autores = getenv("ME1_GM_AUTORES");
#ifdef __ANDROID__
    if (!p || !autores || strcmp(autores, "1")) setenv("ME1_GM_RES_SKIP", "1", 1);
#endif
    if (autores && !strcmp(autores, "1")) {
        static char route[DS3_GM_ROUTE_CAP];
        char expected[32], note[256];
        unsetenv("ME1_GM_RENDER_RES"); /* DS3 computes its own expectation; never evidence of a Present. */
        ds3settings_gm_route(renderer, driver, route);
        ds3settings_gm_resolution(hle_fs_docs(), route, expected, note, sizeof note);
        if (*expected) {
            setenv("ME1_GM_RENDER_RES", expected, 1);
            gm_ds3_render_route(route, !strcmp(renderer, "vulkan") ? GM_RENDER_VULKAN : GM_RENDER_GLES);
        }
        if (*note) fprintf(stderr, "[ds3-gm] auto-resolution: %s\n", note);
    }
    hle_set_cmdline("\"C:\\DeadSpace3\\deadspace3.exe\"");
    exe = (Module){"deadspace3.exe", ds3_base, ds3_image_size, ds3_entry,
                   ds3_funcs, ds3_nfuncs, ds3_imports, ds3_nimports, ds3_exports};
    hle_register_module(&exe, "C:\\DeadSpace3\\deadspace3.exe");
    rt_expect_image_crc(ds3_image_crc);
    const char *image = getenv("DS3_IMAGE");
    rt_load_module(&exe, image && *image ? image : "out/exe/image.bin");
    if (p) {
        platform = p;
        hle_window_hook = on_window;
        hle_present_hook = on_present;
        hle_poll_input_hook = on_input;
        hle_exit_hook = p->exit;
        hle_al_out_open = p->audio_open;
        hle_al_out_start = p->audio_start;
    }
    fprintf(stderr, "[ds3] recompiled entry %08x; game read-only, docs=%s\n", exe.entry, docs);
    pthread_attr_t attr; pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, 256u << 20);
    pthread_t thread;
    int error = pthread_create(&thread, &attr, entry_thread, NULL);
    pthread_attr_destroy(&attr);
    if (error) { fprintf(stderr, "[ds3] pthread_create: %d\n", error); return 1; }
    pthread_join(thread, NULL);
    fprintf(stderr, "[ds3] entry returned %u (renderer gate NOT established)\n", result);
    hle_report();
    return !result && getenv("DS3_BOOT_SMOKE") ? 1 : (int)result;
}
#ifndef DS3_LIB
int main(int argc, char **argv) {
    if (argc != 5) { fprintf(stderr, "usage: %s --game DIR --docs DIR\n", argv[0]); return 2; }
    const char *game = NULL, *docs = NULL;
    for (int i = 1; i + 1 < argc; i += 2) {
        if (!strcmp(argv[i], "--game")) game = argv[i+1];
        else if (!strcmp(argv[i], "--docs")) docs = argv[i+1];
        else return 2;
    }
    return ds3_main(NULL, game, docs);
}
#endif
