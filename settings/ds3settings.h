#pragma once
#include <stddef.h>

/* DS3's game-owned data is opaque. Only supported port settings are exposed. */
int ds3settings_get(const char* docs, const char* id, char* value, size_t cap);

#define DS3_GM_ROUTE_CAP 192
/* Identifies system GLES/Vulkan or a recognizably staged custom Vulkan ICD. */
int ds3settings_gm_route(const char* renderer, const char* driver, char route[DS3_GM_ROUTE_CAP]);

/* Call after VFS init. Returns 1 for a committed change, 0 for no change, -1 on write error.
 * Expected Present size is empty if ineligible, otherwise the current DS3 size. */
int ds3settings_gm_resolution(const char* docs, const char* route, char expected[32], char* message, size_t cap);
