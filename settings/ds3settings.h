#pragma once
#include <stddef.h>
#include "me1settings.h"

/* DS3's game-owned data is opaque. Only supported port settings are exposed. */
int ds3settings_get(const char* docs, const char* id, char* value, size_t cap);

/* Port-only settings (the SF_PORT entries of the shared table), stored in <docs>/ds3port.ini [Port]. */
#define DS3_MAXSET 32
typedef struct {
    char docs_dir[512];
    char value[DS3_MAXSET][96];
    char source[DS3_MAXSET][16];   /* "user", "builtin", "reset" */
    int dirty[DS3_MAXSET];
} Ds3Config;

extern const SettingPreset ds3_presets[];
extern const int ds3_npresets;

int   ds3cfg_load(Ds3Config* c, const char* docs);
/* Applies <docs>/ds3port.ini to the environment the runtime reads; explicit variables win. Returns how many were set. */
int   ds3cfg_apply_env(const char* docs);
int   ds3_nsettings(void);
const SettingDef* ds3_setting_at(int slot);
const SettingDef* ds3cfg_find(const char* id, int* index);
int   ds3cfg_set(Ds3Config* c, const char* id, const char* value, char* err, size_t errcap);   /* 0 ok */
int   ds3cfg_reset(Ds3Config* c);
int   ds3cfg_save(Ds3Config* c, char* msg, size_t cap);   /* 0 ok */
char* ds3cfg_dump(const Ds3Config* c);                    /* malloc'd "id=value\tsource\n" lines */
char* ds3cfg_schema_json(void);                           /* malloc'd JSON for UIs */
int   ds3cfg_preset(Ds3Config* c, const char* id, char* err, size_t cap);
int   ds3cfg_auto(Ds3Config* c, const ME1Device* d, char* report, size_t cap);   /* -> tier or -1 */

#define DS3_GM_ROUTE_CAP 192
/* Identifies system GLES/Vulkan or a recognizably staged custom Vulkan ICD. */
int ds3settings_gm_route(const char* renderer, const char* driver, char route[DS3_GM_ROUTE_CAP]);

/* Call after VFS init. Returns 1 for a committed change, 0 for no change, -1 on write error.
 * Expected Present size is empty if ineligible, otherwise the current DS3 size. */
int ds3settings_gm_resolution(const char* docs, const char* route, char expected[32], char* message, size_t cap);
