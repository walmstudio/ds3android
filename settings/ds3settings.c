#include "ds3settings.h"
#include "me1settings.h"
#include "ini.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>

/* Port-only presets: DS3 has no engine ini to tune, so a preset touches the port's own options. */
static const char* const DS3_P_ECO[] = { "port.fps_limit=30", "port.gearmass=eco", 0 };
static const char* const DS3_P_BAL[] = { "port.fps_limit=30", "port.gearmass=balance", 0 };
static const char* const DS3_P_PERF[] = { "port.fps_limit=60", "port.gearmass=perf", 0 };
static const char* const DS3_P_MAX[] = { "port.fps_limit=0", "port.gearmass=max", 0 };
const SettingPreset ds3_presets[] = {
    {"eco", "Эконом", "30 кадров/с и режим нагрузки «Эконом»: меньше нагрев и расход батареи", DS3_P_ECO},
    {"balanced", "Баланс", "30 кадров/с и режим нагрузки «Баланс»", DS3_P_BAL},
    {"perf", "Производительность", "60 кадров/с и режим нагрузки «Производительность»", DS3_P_PERF},
    {"max", "Максимум", "Без ограничения кадров, режим нагрузки «Максимум»", DS3_P_MAX},
};
const int ds3_npresets = sizeof ds3_presets / sizeof ds3_presets[0];

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
/* ---------------- port-only settings (Dead Space 3) ----------------
 * DS3's own configuration is opaque, so only the port's options are exposed: the SF_PORT entries of the shared
 * settings table, stored in <docs>/ds3port.ini [Port]. No engine ini is read or written. */
static int g_port[DS3_MAXSET], g_nport = -1;

/* Options the DS3 runtime does not read: main_ds3.c pins ME1_SHADER_LITE and ME1_PHONE_MOD to 0, and nothing
 * consumes the touch options. They stay in the shared table for ME1 but are not offered for DS3. */
static int ds3_supported(const char* id) {
    return strcmp(id, "port.simple_shaders") && strcmp(id, "port.phone_mod") &&
           strcmp(id, "port.touch_controls") && strcmp(id, "port.touch_opacity");
}

static void port_index(void) {
    if (g_nport >= 0) return;
    g_nport = 0;
    for (int i = 0; i < me1_nsettings && g_nport < DS3_MAXSET; i++)
        if (me1_settings[i].file == SF_PORT && ds3_supported(me1_settings[i].id)) g_port[g_nport++] = i;
}
static int port_slot(const char* id) {
    port_index();
    for (int k = 0; k < g_nport; k++) if (!strcmp(me1_settings[g_port[k]].id, id)) return k;
    return -1;
}
int ds3_nsettings(void) { port_index(); return g_nport; }
const SettingDef* ds3_setting_at(int slot) {
    port_index();
    return slot >= 0 && slot < g_nport ? &me1_settings[g_port[slot]] : NULL;
}
const SettingDef* ds3cfg_find(const char* id, int* index) {
    int k = port_slot(id);
    if (index) *index = k;
    return k < 0 ? NULL : &me1_settings[g_port[k]];
}
static int mkdirs(const char* path) {
    char t[2048];
    if (snprintf(t, sizeof t, "%s", path) >= (int)sizeof t) return -1;
    for (char* p = t + 1; *p; p++) if (*p == '/') { *p = 0; if (mkdir(t, 0755) && errno != EEXIST) return -1; *p = '/'; }
    return 0;
}
static void ds3cfg_defaults(Ds3Config* c) {
    port_index();
    for (int k = 0; k < g_nport; k++) {
        snprintf(c->value[k], sizeof c->value[k], "%s", me1_settings[g_port[k]].def);
        snprintf(c->source[k], sizeof c->source[k], "builtin");
        c->dirty[k] = 0;
    }
}
static int ds3cfg_path(const Ds3Config* c, char* out, size_t cap) {
    if (!c->docs_dir[0]) return -1;
    return snprintf(out, cap, "%s/ds3port.ini", c->docs_dir) >= (int)cap ? -1 : 0;
}
int ds3cfg_load(Ds3Config* c, const char* docs) {    memset(c, 0, sizeof *c);
    if (docs) snprintf(c->docs_dir, sizeof c->docs_dir, "%s", docs);
    ds3cfg_defaults(c);
    char path[2048]; Ini ini;
    if (ds3cfg_path(c, path, sizeof path) || ini_load_file(&ini, path) != 0) return 0;
    for (int k = 0; k < g_nport; k++) {
        const SettingDef* d = &me1_settings[g_port[k]]; char v[128];
        if (!ini_get(&ini, d->section, d->key, v, sizeof v)) continue;
        snprintf(c->value[k], sizeof c->value[k], "%s", v);
        snprintf(c->source[k], sizeof c->source[k], "user");
    }
    ini_free(&ini);
    return 1;
}
/* The runtime reads its options from the environment (main_ds3.c, gearmass.c, hle_d3d9_gl.h). This maps the
 * supported ds3port.ini keys onto those variables, so the launcher only has to save the file: no extra
 * parameters are appended to the game's command line. An explicit variable already in the environment wins
 * (overwrite 0), which keeps the launcher's free-form field and manual overrides working. */
static const struct { const char *id, *env; } DS3_ENV[] = {
    {"port.fps_limit", "ME1_FPS_LIMIT"}, {"port.stretch_mode", "ME1_STRETCH_MODE"},
    {"port.gearmass", "ME1_GEARMASS"}, {"port.gm_autores", "ME1_GM_AUTORES"},
    {"port.gm_hud", "ME1_GM_HUD"}, {"port.gm_thermal", "ME1_GM_THERMAL"},
    {"port.gm_workers", "ME1_GM_WORKERS"}, {"port.gm_ahead", "ME1_GM_AHEAD"},
    {"port.gm_spin", "ME1_GM_SPIN"}, {"port.gm_prime", "ME1_GM_PRIME"},
    {"port.gm_aniso", "ME1_GM_ANISO"},
};
int ds3cfg_apply_env(const char* docs) {
    if (!docs || !*docs) return 0;
    Ds3Config c;
    ds3cfg_load(&c, docs);
    int n = 0;
    for (size_t i = 0; i < sizeof DS3_ENV / sizeof *DS3_ENV; i++) {
        int k;
        if (!ds3cfg_find(DS3_ENV[i].id, &k) || !c.value[k][0]) continue;
        const char* v = c.value[k];
        if (!strcmp(DS3_ENV[i].id, "port.gm_thermal")) {   /* the table stores percent, the runtime wants a fraction */
            char f[16];
            snprintf(f, sizeof f, "%.2f", atoi(v) / 100.0);
            setenv(DS3_ENV[i].env, f, 0);
        } else setenv(DS3_ENV[i].env, v, 0);
        n++;
    }
    int sf;
    if (ds3cfg_find("port.show_fps", &sf) && !strcmp(c.value[sf], "1")) { setenv("ME1_SHOW_FPS", "1", 0); n++; }
    return n;
}

static int ds3cfg_valid(const SettingDef* d, const char* v, char* err, size_t cap) {
    if (d->type == ST_BOOL || d->type == ST_BOOL01) {
        if (ini_parse_bool(v, -1) < 0) { snprintf(err, cap, "%s: ожидается 0/1", d->id); return 0; }
        return 1;
    }
    if (d->type == ST_INT || d->type == ST_FLOAT) {
        char* e; double x = d->type == ST_INT ? (double)strtol(v, &e, 10) : strtod(v, &e);
        if (e == v || *e) { snprintf(err, cap, "%s: ожидается %s", d->id, d->type == ST_INT ? "целое" : "число"); return 0; }
        if (x < d->min || x > d->max) { snprintf(err, cap, "%s: допустимо %g..%g", d->id, d->min, d->max); return 0; }
        return 1;
    }
    if (d->type == ST_ENUM) {
        for (const SetChoice* ch = d->choices; ch && ch->value; ch++) if (!strcmp(ch->value, v)) return 1;
        snprintf(err, cap, "%s: недопустимое значение '%s'", d->id, v);
        return 0;
    }
    return 1;
}
int ds3cfg_set(Ds3Config* c, const char* id, const char* value, char* err, size_t cap) {
    int k = port_slot(id);
    if (k < 0) { snprintf(err, cap, "неизвестная настройка '%s'", id); return -1; }
    if (!ds3cfg_valid(&me1_settings[g_port[k]], value, err, cap)) return -1;
    snprintf(c->value[k], sizeof c->value[k], "%s", value);
    snprintf(c->source[k], sizeof c->source[k], "user");
    c->dirty[k] = 1;
    return 0;
}
int ds3cfg_reset(Ds3Config* c) {
    ds3cfg_defaults(c);
    for (int k = 0; k < g_nport; k++) { c->dirty[k] = 1; snprintf(c->source[k], sizeof c->source[k], "reset"); }
    return 0;
}
int ds3cfg_save(Ds3Config* c, char* msg, size_t cap) {
    char path[2048]; Ini ini;
    if (ds3cfg_path(c, path, sizeof path)) { snprintf(msg, cap, "папка документов не задана"); return -1; }
    if (ini_load_file(&ini, path) != 0) {
        if (mkdirs(c->docs_dir)) { snprintf(msg, cap, "не удалось создать папку для %s", path); return -1; }
        const char* hdr = "; Dead Space 3 port (ds3port): options of the port itself";
        ini_load_mem(&ini, hdr, strlen(hdr));
    }
    for (int k = 0; k < g_nport; k++)
        ini_set(&ini, me1_settings[g_port[k]].section, me1_settings[g_port[k]].key, c->value[k]);
    int rc = ini_save_file(&ini, path);
    ini_free(&ini);
    if (rc) { snprintf(msg, cap, "ошибка записи %s", path); return -1; }
    for (int k = 0; k < g_nport; k++) c->dirty[k] = 0;
    snprintf(msg, cap, "обновлён: %s", path);
    return 0;
}
char* ds3cfg_dump(const Ds3Config* c) {
    size_t cap = 4096, k = 0; char* o = malloc(cap);
    if (!o) return NULL;
    o[0] = 0;
    for (int s = 0; s < g_nport; s++)
        k += (size_t)snprintf(o + k, cap - k, "%s=%s\t%s\n", me1_settings[g_port[s]].id, c->value[s], c->source[s]);
    return o;
}
static size_t ds3_jstr(char* o, size_t k, size_t cap, const char* s) {
    if (k < cap) o[k++] = '"';
    for (; s && *s; s++) {
        unsigned char ch = (unsigned char)*s;
        if (ch == '"' || ch == '\\') { if (k + 1 < cap) { o[k++] = '\\'; o[k++] = (char)ch; } }
        else if (ch == '\n') { if (k + 1 < cap) { o[k++] = '\\'; o[k++] = 'n'; } }
        else if (ch < 0x20) { if (k + 6 < cap) k += (size_t)snprintf(o + k, cap - k, "\\u%04x", ch); }
        else if (k < cap) o[k++] = (char)ch;
    }
    if (k < cap) o[k++] = '"';
    return k;
}
char* ds3cfg_schema_json(void) {
    static const char* const TN[] = { "bool", "bool", "int", "float", "enum", "resolution", "string" };
    size_t cap = 32768, k = 0; char* o = malloc(cap);
    if (!o) return NULL;
    port_index();
    k += (size_t)snprintf(o + k, cap - k, "{\"settings\":[");
    for (int s = 0; s < g_nport; s++) {
        const SettingDef* d = &me1_settings[g_port[s]];
        k += (size_t)snprintf(o + k, cap - k, "%s{\"id\":", s ? "," : ""); k = ds3_jstr(o, k, cap, d->id);
        k += (size_t)snprintf(o + k, cap - k, ",\"group\":"); k = ds3_jstr(o, k, cap, d->group);
        k += (size_t)snprintf(o + k, cap - k, ",\"label\":"); k = ds3_jstr(o, k, cap, d->label);
        k += (size_t)snprintf(o + k, cap - k, ",\"help\":"); k = ds3_jstr(o, k, cap, d->help);
        k += (size_t)snprintf(o + k, cap - k, ",\"type\":\"%s\",\"min\":%g,\"max\":%g,\"choices\":[", TN[d->type], d->min, d->max);
        int first = 1;
        for (const SetChoice* ch = d->choices; ch && ch->value; ch++) {
            k += (size_t)snprintf(o + k, cap - k, "%s[", first ? "" : ","); k = ds3_jstr(o, k, cap, ch->value);
            k += (size_t)snprintf(o + k, cap - k, ","); k = ds3_jstr(o, k, cap, ch->label);
            k += (size_t)snprintf(o + k, cap - k, "]"); first = 0;
        }
        k += (size_t)snprintf(o + k, cap - k, "]}");
    }
    k += (size_t)snprintf(o + k, cap - k, "],\"presets\":[");
    for (int p = 0; p < ds3_npresets; p++) {
        k += (size_t)snprintf(o + k, cap - k, "%s{\"id\":", p ? "," : ""); k = ds3_jstr(o, k, cap, ds3_presets[p].id);
        k += (size_t)snprintf(o + k, cap - k, ",\"label\":"); k = ds3_jstr(o, k, cap, ds3_presets[p].label);
        k += (size_t)snprintf(o + k, cap - k, ",\"help\":"); k = ds3_jstr(o, k, cap, ds3_presets[p].help);
        k += (size_t)snprintf(o + k, cap - k, "}");
    }
    k += (size_t)snprintf(o + k, cap - k, "]}");
    return o;
}
int ds3cfg_preset(Ds3Config* c, const char* id, char* err, size_t cap) {
    for (int p = 0; p < ds3_npresets; p++) {
        if (strcmp(ds3_presets[p].id, id)) continue;
        for (const char* const* kv = ds3_presets[p].kv; kv && *kv; kv++) {
            const char* eq = strchr(*kv, '=');
            if (!eq) continue;
            char key[64];
            snprintf(key, sizeof key, "%.*s", (int)(eq - *kv), *kv);
            if (ds3cfg_set(c, key, eq + 1, err, cap)) return -1;
        }
        return 0;
    }
    snprintf(err, cap, "неизвестный пресет '%s'", id);
    return -1;
}
int ds3cfg_auto(Ds3Config* c, const ME1Device* d, char* report, size_t cap) {
    char res[32], err[256]; int fps;
    int t = me1cfg_classify(d, report, cap, &fps, res, sizeof res);
    char fs[8];
    snprintf(fs, sizeof fs, "%d", fps);
    if (ds3cfg_set(c, "port.fps_limit", fs, err, sizeof err) ||
        ds3cfg_set(c, "port.gearmass", t <= 1 ? "eco" : t == 2 ? "balance" : "perf", err, sizeof err)) {
        snprintf(report, cap, "ошибка: %s", err);
        return -1;
    }
    return t;
}
