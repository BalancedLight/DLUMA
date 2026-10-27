#ifndef DLUMA_QUEST_PHASE_PLAYER_H
#define DLUMA_QUEST_PHASE_PLAYER_H

#include "dlw_common.h"
#include "core/dlw_quest_phase.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Install the exact-build Phase::OnEnter observation detour.  Failure leaves
 * the stock gamedll untouched and custom phase execution unavailable. */
int dluma_quest_phase_player_start(HMODULE gamedll);

/* Publish one immutable active-project binding generation.  An empty project
 * is valid and clears the prior generation. */
int dluma_quest_phase_player_on_map_generation_changed(
    const WCHAR* project_root,
    const char* map_name,
    unsigned int map_generation);

/* Consume observations on the established game thread. */
void dluma_quest_phase_player_game_thread_pump(void);

int dluma_quest_phase_player_register_provider(
    const char* owner_mod,
    const char* handler_id,
    int load_priority,
    DlumaQuestPhaseDispatchCallback callback,
    void* user,
    uint64_t* token);
int dluma_quest_phase_player_register_observer(
    const char* owner_mod,
    const char* contract_id,
    int load_priority,
    DlumaQuestPhaseDispatchCallback callback,
    void* user,
    uint64_t* token);
int dluma_quest_phase_player_unregister(uint64_t token);

/* Stop new hook entry, restore only if DLUMA still owns the target, and drain
 * every in-flight hook before releasing the trampoline/binding graph. */
int dluma_quest_phase_player_stop(void);
int dluma_quest_phase_player_started(void);

#ifdef __cplusplus
}
#endif

#endif
