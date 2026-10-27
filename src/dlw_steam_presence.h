#ifndef DLUMA_STEAM_PRESENCE_H
#define DLUMA_STEAM_PRESENCE_H

#include "dlw_common.h"
#include "dlw_presence_core.h"

#ifdef __cplusplus
extern "C" {
#endif

int dluma_steam_presence_start(void);
void dluma_steam_presence_set_enabled(int enabled);
int dluma_steam_presence_is_enabled(void);
int dluma_steam_presence_needs_game_thread_service(void);
void dluma_steam_presence_game_thread_service(void);
void dluma_steam_presence_game_thread_observe(
    const DlumaPresenceSnapshot* snapshot);
void dluma_steam_presence_game_thread_expire(void);
void dluma_steam_presence_quiesce(void);
void dluma_steam_presence_stop(void);

#ifdef __cplusplus
}
#endif

#endif
