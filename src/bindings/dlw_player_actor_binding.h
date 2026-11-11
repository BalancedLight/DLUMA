#ifndef DLUMA_PLAYER_ACTOR_BINDING_H
#define DLUMA_PLAYER_ACTOR_BINDING_H

#include <stdint.h>
#include "../core/dlw_engine_script_bootstrap.h"
#ifdef __cplusplus
extern "C" {
#endif

typedef struct DlumaPlayerVitalsSnapshot {
    float health;
    float max_health;
    uint32_t alive;
} DlumaPlayerVitalsSnapshot;

/* Implemented beside the exact Player profile so SHA/RTTI/owner validation
 * has one authority. Only copied values cross into the shared game service. */
int dluma_player_actor_binding_vitals_ready(void);
int dluma_player_actor_binding_read_vitals(DlumaPlayerVitalsSnapshot* snapshot);

typedef enum DlumaPlayerInfectionRequestResult {
    DLUMA_PLAYER_INFECTION_NATIVE_FAILED = -2,
    DLUMA_PLAYER_INFECTION_CONTEXT_CHANGED = -1,
    DLUMA_PLAYER_INFECTION_UNAVAILABLE = 0,
    DLUMA_PLAYER_INFECTION_ACCEPTED = 1
} DlumaPlayerInfectionRequestResult;
/* Exact Player profile/code and qualified native lifetime availability.
 * Currently unavailable until the native caller/retirement fence is qualified.
 * Request also requires current frame and the same validated local controller.
 * ACCEPTED acknowledges one stock request, not a rendered HUD image. */
int dluma_player_actor_binding_infection_flashback_ready(void);
DlumaPlayerInfectionRequestResult
dluma_player_actor_binding_request_infection_flashback(void);

typedef struct DlumaPlayerActorColdProfile {
    uintptr_t gamedll_base;
    uint32_t gamedll_size;
    uintptr_t engine_base;
    uint32_t engine_size;
} DlumaPlayerActorColdProfile;
/* Immutable module/host validation only; does not resolve or borrow actors. */
int dluma_player_actor_binding_probe_cold_profile(uintptr_t original_module,
    DlumaPlayerActorColdProfile* profile);
/* Read-only actual-entry proof. The phase argument must come from the current
 * pre-original callsite; the saved caller/thread are the outer public export's
 * observation. This grants no actor lifetime or installation authority. */
int dluma_player_actor_binding_probe_cold_startup(uintptr_t original_module,
    int before_original_initializer,DlumaEngineScriptBootstrapResult* result);

#ifdef __cplusplus
}
#endif
#endif
