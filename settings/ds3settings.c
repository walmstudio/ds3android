#include "ds3settings.h"
#include "ini.h"
#include <stdio.h>
#include <string.h>

static const struct { const char *id, *key; } options[] = {
    {"port.fps_limit", "FpsLimit"}, {"port.stretch_mode", "StretchMode"}, {"port.show_fps", "ShowFps"},
};

int ds3settings_get(const char* docs, const char* id, char* value, size_t cap) {
    if (!value || !cap) return 0;
    *value = 0;
    if (!docs || !*docs || !id) return 0;
    unsigned i = 0;
    while (i < sizeof options / sizeof *options && strcmp(options[i].id,id)) ++i;
    if (i == sizeof options / sizeof *options) return 0;
    static const char* const files[] = {"ds3port.ini", "BioWare/Mass Effect/me1port.ini"};
    for (unsigned j = 0; j < sizeof files / sizeof *files; ++j) {
        char path[2048], raw[128]; Ini ini;
        if (snprintf(path, sizeof path, "%s/%s", docs, files[j]) >= (int)sizeof path) return 0;
        int ok = ini_load_file(&ini, path) == 0 && ini_get(&ini, "Port", options[i].key, raw, sizeof raw);
        ini_free(&ini);
        if (!ok) continue;
        if (i == 0 && (!raw[0] || raw[strspn(raw,"0123456789")])) continue;
        if (i == 1 && strcmp(raw,"fit") && strcmp(raw,"fill") && strcmp(raw,"stretch")) continue;
        if (i == 2) {
            int b = ini_parse_bool(raw,-1);
            if (b < 0) continue;
            snprintf(value, cap, "%d", b);
        } else snprintf(value, cap, "%s", raw);
        return 1;
    }
    return 0;
}
