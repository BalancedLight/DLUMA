#ifndef DLUMA_DISCORD_PRESENCE_H
#define DLUMA_DISCORD_PRESENCE_H

#include "dlw_common.h"
#include "dlw_presence_core.h"

int dluma_discord_presence_start(void);
void dluma_discord_presence_set_enabled(int enabled);
int dluma_discord_presence_is_enabled(void);
void dluma_discord_presence_game_thread_tick(
    void* game_subobject, ULONGLONG callback_sequence);
void dluma_discord_presence_observe_editor(
    const DlumaPresenceSnapshot* snapshot,
    ULONGLONG document_identity);
void dluma_discord_presence_set_suspended(int suspended);
void dluma_discord_presence_quiesce(void);
void dluma_discord_presence_stop(void);
int dluma_discord_presence_stop_succeeded(void);

#endif
