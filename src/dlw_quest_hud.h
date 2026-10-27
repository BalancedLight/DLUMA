#ifndef DLUMA_QUEST_HUD_H
#define DLUMA_QUEST_HUD_H

#include "game/dlw_game_types.h"

int dluma_quest_hud_start(void);
void dluma_quest_hud_quiesce(void);
int dluma_quest_hud_stop(void);
int dluma_quest_hud_is_available(void);
void dluma_quest_hud_on_map_generation_changed(uint32_t map_generation);

int dluma_quest_hud_snapshot(
    DlumaGameQuestHudSnapshot *snapshot,
    DlumaGameError *error);
int dluma_quest_hud_override_set(
    const DlumaGameQuestHudOverrideRequest *request,
    DlumaGameQuestHudOverrideResult *result,
    DlumaGameError *error);
int dluma_quest_hud_override_clear(
    uint64_t owner_id,
    uint32_t map_generation,
    uint64_t override_id,
    DlumaGameQuestHudOverrideResult *result,
    DlumaGameError *error);
void dluma_quest_hud_clear_owner(uint64_t owner_id);

#endif
