#ifndef DLUMA_GAME_TYPES_H
#define DLUMA_GAME_TYPES_H

/* Transitional ownership boundary for the validated engine adapters. These
 * aliases let the existing private Lua-helper wire format remain binary
 * compatible while engine-facing headers and new hosts use language-neutral
 * names. The next wire-format revision can replace the storage without
 * changing game-service call sites. */
#include "../dlw_lua54_api.h"

typedef DlumaLua54Error DlumaGameError;
typedef DlumaLua54Object DlumaGameObject;
typedef DlumaLua54Vec3 DlumaGameVec3;
typedef DlumaLua54SetEnabledRequest DlumaGameSetEnabledRequest;
typedef DlumaLua54SetEnabledResult DlumaGameSetEnabledResult;
typedef DlumaLua54SubtitleRequest DlumaGameSubtitleRequest;
typedef DlumaLua54SubtitleUpdateRequest DlumaGameSubtitleUpdateRequest;
typedef DlumaLua54RadioDialogRequest DlumaGameRadioDialogRequest;
typedef DlumaLua54QuestRequest DlumaGameQuestRequest;
typedef DlumaLua54QuestResult DlumaGameQuestResult;
typedef DlumaLua54QuestHudSnapshot DlumaGameQuestHudSnapshot;
typedef DlumaLua54QuestHudOverrideRequest DlumaGameQuestHudOverrideRequest;
typedef DlumaLua54QuestHudOverrideResult DlumaGameQuestHudOverrideResult;

#endif
