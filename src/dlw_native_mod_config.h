#ifndef DLUMA_NATIVE_MOD_CONFIG_H
#define DLUMA_NATIVE_MOD_CONFIG_H

#include "dlw_lua_config.h"

#define DLUMA_NATIVE_MOD_CONFIG_MAX_JSON_BYTES (64U * 1024U)
#define DLUMA_NATIVE_MOD_CONFIG_PATH_CAPACITY 260U

#ifdef __cplusplus
extern "C" {
#endif

int dluma_native_mod_config_path_valid(const char *path);
int dluma_mod_config_parse_json(
    const char *bytes,
    unsigned int size,
    DlumaLuaConfigSchema *schema,
    char *diagnostic,
    unsigned int diagnostic_capacity);
int dluma_native_mod_config_parse_json(
    const char *bytes,
    unsigned int size,
    DlumaLuaConfigSchema *schema,
    char *diagnostic,
    unsigned int diagnostic_capacity);

#ifdef __cplusplus
}
#endif

#endif
