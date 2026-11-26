#define _POSIX_C_SOURCE 200809L
#include "ds3settings.h"
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

int ds3settings_gm_route(const char* renderer, const char* driver, char route[DS3_GM_ROUTE_CAP]) {
    *route = 0;
    if (!renderer || (strcmp(renderer, "gles") && strcmp(renderer, "vulkan"))) return 0;
#ifdef __ANDROID__
    const char* host = "android";
#else
    const char* host = "linux";
#endif
    if (!driver) {
        snprintf(route, DS3_GM_ROUTE_CAP, "%s:%s:system", host, renderer);
        return 1;
    }
    if (strcmp(renderer, "vulkan") || *driver != '/') return 0;
    const char* name = strrchr(driver, '/') + 1;
    int zip = !strncmp(name, "zip-", 4), standalone = !strncmp(name, "icd-", 4);
    if (!zip && !standalone) return 0;
    size_t len = zip ? 64 : 36;
    if (strlen(name) != 4 + len + 3 || strcmp(name + 4 + len, ".so")) return 0;
    for (size_t i = 0; i < len; ++i) {
        int hyphen = standalone && (i == 8 || i == 13 || i == 18 || i == 23);
        if (hyphen ? name[4 + i] != '-' : !strchr("0123456789abcdef", name[4 + i])) return 0;
    }
    struct stat st;
    if (lstat(driver, &st) || !S_ISREG(st.st_mode) || st.st_size <= 0) return 0;
    /* Import ID + file identity: replacing or editing a staged library invalidates its evidence. */
    int n = snprintf(route, DS3_GM_ROUTE_CAP, "%s:vulkan:%s:%jx:%jx:%jx:%jx:%jx", host, name,
                     (uintmax_t)st.st_dev, (uintmax_t)st.st_ino, (uintmax_t)st.st_size,
                     (uintmax_t)st.st_ctim.tv_sec, (uintmax_t)st.st_ctim.tv_nsec);
    if (n <= 0 || n >= DS3_GM_ROUTE_CAP) { *route = 0; return 0; }
    return 1;
}

/* Exact DS3 AppData VFS path, not ME1's Documents/Config nor the game mount. */
static int system_dir(int parent) {
    static const char* const dirs[] = {"drive_c", "Users", "Shepard", "AppData", "Local", "EA Games", "Dead Space 3"};
    int fd = dup(parent);
    for (size_t i = 0; fd >= 0 && i < sizeof dirs / sizeof *dirs; ++i) {
        int next = openat(fd, dirs[i], O_RDONLY | O_CLOEXEC | O_DIRECTORY | O_NOFOLLOW);
        close(fd);
        fd = next;
    }
    return fd;
}

static int read_file(int dir, const char* name, char* data, size_t cap, struct stat* info) {
    int fd = openat(dir, name, O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
    if (fd < 0) return errno == ENOENT ? 0 : -1;
    struct stat st;
    if (fstat(fd, &st) || !S_ISREG(st.st_mode) || st.st_size >= (off_t)cap) { close(fd); return -1; }
    size_t used = 0;
    ssize_t n;
    do {
        n = read(fd, data + used, cap - used);
        if (n > 0) used += (size_t)n;
    } while (used < cap && (n > 0 || (n < 0 && errno == EINTR)));
    close(fd);
    if (n < 0 || used == cap || memchr(data, 0, used)) return -1;
    data[used] = 0;
    if (info) *info = st;
    return 1;
}

/* Commit complete files via same-directory rename; a preexisting temp is never followed. */
static int replace_file(int dir, const char* name, const char* data, mode_t mode) {
    char temporary[96];
    snprintf(temporary, sizeof temporary, ".%s.tmp.%ld", name, (long)getpid());
    int fd = openat(dir, temporary, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, mode & 0777);
    if (fd < 0) return 0;
    size_t left = strlen(data);
    while (left) {
        ssize_t n = write(fd, data, left);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) break;
        data += n; left -= (size_t)n;
    }
    int ok = !left && !fsync(fd);
    if (close(fd)) ok = 0;
    if (ok && !renameat(dir, temporary, dir, name)) return 1;
    unlinkat(dir, temporary, 0);
    return 0;
}

/* Same-aspect subset of init_modes() in hle_d3d9.c; never invent arbitrary sizes. */
static const struct { int w, h; const char* text; } modes[] = {
    {1280, 720, "1280x720"}, {1600, 900, "1600x900"}, {1920, 1080, "1920x1080"},
};
typedef struct { const char *first, *end; int value; } Span;
static int dimensions(const char* file, Span size[2]) {
    const char* line = file;
    while (*line) {
        const char *end = strchr(line, '\n'), *key = line;
        if (!end) end = line + strlen(line);
        key += strspn(key, " \t");
        const char* keys[] = {"Window.Width", "Window.Height"};
        for (int i = 0; i < 2; ++i) {
            size_t len = strlen(keys[i]);
            if (strncmp(key, keys[i], len)) continue;
            const char* value = key + len;
            if (*value != '=' && *value != ' ' && *value != '\t') continue;
            value += strspn(value, " \t");
            if (*value++ != '=' || size[i].first) return -1;
            value += strspn(value, " \t");
            size_t digits = strspn(value, "0123456789");
            if (!digits || digits > 5 || value + digits > end) return -1;
            const char* tail = value + digits;
            if (tail + strspn(tail, " \t\r") != end) return -1;
            size[i] = (Span){value, tail, atoi(value)};
        }
        line = *end ? end + 1 : end;
    }
    for (size_t i = 0; i < sizeof modes / sizeof *modes; ++i)
        if (size[0].value == modes[i].w && size[1].value == modes[i].h) return (int)i;
    return -1;
}

static int number(const char* text, double lo, double hi, double* result) {
    char* end;
    errno = 0;
    double n = strtod(text, &end);
    if (!*text || *end || errno || !isfinite(n) || n < lo || n > hi) return 0;
    *result = n;
    return 1;
}

static int valid_run(const char* s) {
    return *s && strlen(s) < 80 && s[strspn(s, "0123456789abcdef-")] == 0;
}

static int sample(int root, const char* route, const char* current, const char* limit, char run[80]) {
    char data[4096];
    if (read_file(root, "me1_gearmass_last.txt", data, sizeof data, NULL) != 1) return 0;
    enum { TAG, ROUTE, STAMP, MODE, SIZE, LIMIT, MIXED, RUN, SECONDS, FPS, GPU, CAPPED, COUNT };
    const char* keys[COUNT] = {"ds3_autores", "ds3_route", "ds3_sample_unix", "mode", "resolution", "fps_limit", "fps_limit_mixed",
                              "run_id", "active_seconds", "active_fps", "active_gpu", "active_capped"};
    const char* values[COUNT] = {0};
    char* rest = NULL;
    for (char* line = strtok_r(data, "\n", &rest); line; line = strtok_r(NULL, "\n", &rest)) {
        char* eq = strchr(line, '=');
        if (!eq) continue;
        *eq++ = 0;
        for (int i = 0; i < COUNT; ++i) if (!strcmp(line, keys[i])) {
            if (values[i]) return 0;
            values[i] = eq;
            break;
        }
    }
    for (int i = 0; i < COUNT; ++i) if (!values[i]) return 0;
    time_t now = time(NULL);
    double stamp, seconds, fps, gpu, capped;
    if (now <= 0 || strcmp(values[ROUTE], route) || values[STAMP][strspn(values[STAMP], "0123456789")] ||
        !number(values[STAMP], 1, (double)now, &stamp) || (double)now - stamp > 86400) return 0;
    if (strcmp(values[TAG], "1") || strcmp(values[MODE], "AUTO") || strcmp(values[SIZE], current) ||
        strcmp(values[LIMIT], limit) || strcmp(values[MIXED], "0") || !valid_run(values[RUN]) ||
        !number(values[SECONDS], 180, 1e8, &seconds) || !number(values[FPS], 0, 1000, &fps) ||
        !number(values[GPU], 40, 100, &gpu) || !number(values[CAPPED], 0, 100, &capped) ||
        fps >= atoi(limit) * 0.85) return 0;
    strcpy(run, values[RUN]);
    return 1;
}

int ds3settings_gm_resolution(const char* docs, const char* route, char expected[32], char* message, size_t cap) {
    const char *opt = getenv("ME1_GM_AUTORES"), *skip = getenv("ME1_GM_RES_SKIP");
    const char *mode = getenv("ME1_GEARMASS"), *limit = getenv("ME1_FPS_LIMIT");
    *expected = 0;
    if (cap) *message = 0;
    if (!opt || strcmp(opt, "1") || (skip && !strcmp(skip, "1"))) return 0;
    const char* why = "requires explicit AUTO mode";
    if (!mode || (strcasecmp(mode, "auto") && strcmp(mode, "1"))) goto inactive;
    if (!limit || !*limit) limit = getenv("ME1_GM_FPS");
    why = "requires a fixed FPS cap (10–240)";
    if (!limit || !*limit || *limit == '0' || strlen(limit) > 3 || limit[strspn(limit, "0123456789")] ||
        atoi(limit) < 10 || atoi(limit) > 240) goto inactive;
    why = "renderer or ICD origin not identifiable";
    if (!route || !*route) goto inactive;
    why = "cannot open canonical DS3 documents";
    if (!docs || !*docs) goto inactive;
    int root = open(docs, O_RDONLY | O_CLOEXEC | O_DIRECTORY | O_NOFOLLOW);
    if (root < 0) goto inactive;
    int dir = system_dir(root), result = 0;
    char file[8192], run[80], consumed[80];
    struct stat original, now;
    Span spans[2] = {{0}};
    int mode_index = -1;
    const char system[] = "system.txt", state[] = "ds3_gearmass_resolution.state";
    why = "no supported 720p/900p/1080p size in DS3 user system.txt";
    if (dir < 0 || read_file(dir, system, file, sizeof file, &original) != 1 ||
        (mode_index = dimensions(file, spans)) < 0) goto done;
    strcpy(expected, modes[mode_index].text);
    why = "already at the 720p floor";
    if (!mode_index) goto done;
    why = "waiting for a matching GPU-bound AUTO session from the last 24h (renderer/ICD, cap and measured size)";
    if (!sample(root, route, expected, limit, run)) goto done;
    int read_state = read_file(root, state, consumed, sizeof consumed, NULL);
    why = "cannot read adaptation state";
    if (read_state < 0 || (read_state && !valid_run(consumed))) { result = -1; goto done; }
    why = "matching session already used";
    if (read_state && !strcmp(consumed, run)) goto done;
    const int next = mode_index - 1;
    /* D3D only advertises the standard sizes that fit its reported desktop. */
    const char* screen = getenv("ME1_SCREEN");
    unsigned sw = 1280, sh = 720;
    if (screen && (sscanf(screen, "%ux%u", &sw, &sh) != 2 || sw < 320 || sh < 200)) { sw = 1280; sh = 720; }
    why = "next standard mode does not fit the reported desktop";
    if (sw < (unsigned)modes[next].w || sh < (unsigned)modes[next].h) goto done;
    char output[sizeof file];
    int first = spans[0].first < spans[1].first ? 0 : 1, last = 1 - first;
    int nums[] = {modes[next].w, modes[next].h};
    int count = snprintf(output, sizeof output, "%.*s%d%.*s%d%s",
                         (int)(spans[first].first - file), file, nums[first],
                         (int)(spans[last].first - spans[first].end), spans[first].end,
                         nums[last], spans[last].end);
    why = "DS3 user system.txt changed during startup";
    if (count < 0 || (size_t)count >= sizeof output || fstatat(dir, system, &now, AT_SYMLINK_NOFOLLOW) ||
        original.st_dev != now.st_dev || original.st_ino != now.st_ino || original.st_size != now.st_size ||
        original.st_mtim.tv_sec != now.st_mtim.tv_sec || original.st_mtim.tv_nsec != now.st_mtim.tv_nsec) goto done;
    /* Consume first: if the settings write fails, lose one adjustment rather than replay it. */
    why = "cannot commit adaptation state";
    if (!replace_file(root, state, run, 0600)) { result = -1; goto done; }
    why = "cannot atomically update DS3 user system.txt";
    if (!replace_file(dir, system, output, original.st_mode)) { result = -1; goto done; }
    strcpy(expected, modes[next].text);
    if (cap) snprintf(message, cap, "resolution %s -> %s (prior GPU-bound AUTO session)", modes[mode_index].text, expected);
    result = 1;
done:
    if (dir >= 0) close(dir);
    close(root);
    if (result == 1) return result;
    if (cap) snprintf(message, cap, "%s: %s", result < 0 ? "not applied" : "unchanged", why);
    return result;
inactive:
    if (cap) snprintf(message, cap, "inactive: %s", why);
    return 0;
}
