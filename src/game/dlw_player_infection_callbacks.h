#ifndef DLW_PLAYER_INFECTION_CALLBACKS_H
#define DLW_PLAYER_INFECTION_CALLBACKS_H
#include "../core/dlw_infection_request_queue.h"

#ifdef __cplusplus
extern "C" {
#endif
/* Disabled draft. Called only from the classified Player bootstrap before the
 * original script initializer/third-party pre-init. No installer is enabled
 * until identity assignment, cold route and independent review are qualified. */
int dluma_player_infection_callbacks_prepare_cold(uintptr_t original_module);
int dluma_player_infection_callbacks_stop(void);
int dluma_player_infection_callbacks_available(void);
/* Receiver-only draft entry; no production detour/trampoline is installed or
 * published. Relocation/unwind/cold installation support remains unavailable. */
void dluma_player_infection_callbacks_respawn_draft(void* receiver);
void dluma_player_infection_callbacks_publish_frame(uint32_t map_generation, uint64_t frame_sequence);
void dluma_player_infection_callbacks_retire_owner(const DlumaInfectionQueueOwner*);
DlumaInfectionQueueResult dluma_player_infection_callbacks_request(const DlumaInfectionQueueOwner*, uint32_t map_generation, uint32_t timeout_ms, uint64_t* token);
DlumaInfectionQueueResult dluma_player_infection_callbacks_status(const DlumaInfectionQueueOwner*, uint64_t token, DlumaInfectionQueueSnapshot*);
DlumaInfectionQueueResult dluma_player_infection_callbacks_cancel(const DlumaInfectionQueueOwner*, uint64_t token);
DlumaInfectionQueueResult dluma_player_infection_callbacks_release(const DlumaInfectionQueueOwner*, uint64_t token);
#ifdef __cplusplus
}
#endif
#endif
