#ifndef DLUMA_LUA_DISCOVERY_INTERNAL_H
#define DLUMA_LUA_DISCOVERY_INTERNAL_H

#include "dlw_common.h"

typedef enum DlumaLuaDiscoveryMode {
    DLUMA_LUA_DISCOVERY_INCREMENTAL = 0,
    DLUMA_LUA_DISCOVERY_FULL_STARTUP,
    DLUMA_LUA_DISCOVERY_FULL_CONSISTENCY,
    DLUMA_LUA_DISCOVERY_FALLBACK
} DlumaLuaDiscoveryMode;

typedef struct DlumaLuaDiscoveryMetrics {
    ULONGLONG duration_ms;
    ULONGLONG bytes_read;
    unsigned int projects_visited;
    unsigned int files_visited;
    unsigned int cache_hits;
    unsigned int cache_misses;
    unsigned int projects_truncated;
    unsigned int files_truncated;
    unsigned int mode;
} DlumaLuaDiscoveryMetrics;

#ifdef __cplusplus
extern "C" {
#endif

void dluma_lua_discover_globals_mode(DlumaLuaDiscoveryMode mode);
void dluma_lua_discovery_shutdown(void);

#ifdef __cplusplus
}
#endif

#endif
