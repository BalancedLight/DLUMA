#ifndef DLUMA_QUEST_HUD_SERVICE_H
#define DLUMA_QUEST_HUD_SERVICE_H

#include <stdint.h>

#define DLUMA_GAME_HOST_QUEST_HUD_LIMIT 8U
#define DLUMA_GAME_TOKEN_QUEST_HUD 4U

typedef struct DlumaGameHostQuestHudOverride {
    uint64_t token;
    uint64_t native_id;
    uint32_t map_generation;
    uint32_t owner_generation;
} DlumaGameHostQuestHudOverride;

#endif
