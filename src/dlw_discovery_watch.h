#ifndef DLUMA_DISCOVERY_WATCH_H
#define DLUMA_DISCOVERY_WATCH_H

#include "dlw_common.h"

#define DLUMA_DISCOVERY_DEBOUNCE_MS 500ULL
#define DLUMA_DISCOVERY_FULL_SWEEP_MS (10ULL * 60ULL * 1000ULL)
#define DLUMA_DISCOVERY_FALLBACK_SWEEP_MS (60ULL * 1000ULL)
#define DLUMA_DISCOVERY_ROOT_PROBE_MS (30ULL * 1000ULL)

#ifdef __cplusplus
extern "C" {
#endif

int dluma_discovery_watch_sync_roots(void);
int dluma_discovery_watch_poll(void);
int dluma_discovery_watch_requires_fallback(void);
void dluma_discovery_watch_shutdown(void);

#ifdef __cplusplus
}
#endif

#endif
