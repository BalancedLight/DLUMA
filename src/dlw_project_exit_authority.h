#ifndef DLUMA_PROJECT_EXIT_AUTHORITY_H
#define DLUMA_PROJECT_EXIT_AUTHORITY_H
#include "dlw_common.h"

typedef struct DlumaProjectExitDrainAuthority {
    unsigned int map_generation;
    DlumaContentFinalizationAuthorityEvidence owner;
} DlumaProjectExitDrainAuthority;

/* A previously sampled thread ID is not authority. Recheck the current
 * serialized route or exact natural/window owner at the retirement call. */
static __inline int dluma_project_exit_validate_authority(
    unsigned int generation, const DlumaProjectExitDrainAuthority* supplied)
{
    DlumaContentFinalizationAuthorityEvidence current;
    const DlumaNaturalShutdownOwnerProof* natural = 0;
    LONG window_owner = 0;
    if (supplied) {
        if (supplied->map_generation != generation ||
            supplied->owner.current_thread_id != (LONG)GetCurrentThreadId() ||
            supplied->owner.selected_proof == DLUMA_CONTENT_FINALIZATION_PROOF_NONE)
            return 0;
        window_owner = supplied->owner.validated_window_owner_thread_id;
        if (supplied->owner.selected_proof == DLUMA_CONTENT_FINALIZATION_PROOF_NATURAL_SHUTDOWN_OWNER)
            natural = &supplied->owner.natural_shutdown;
    }
    memset(&current,0,sizeof(current));
    if (!dluma_engine_current_thread_can_finalize_content_ex(
            &g_dluma.engine,window_owner,natural,&current)) return 0;
    if (!supplied)
        return current.selected_proof == DLUMA_CONTENT_FINALIZATION_PROOF_SERIALIZED_FRAME_ROUTE;
    if (current.selected_proof != supplied->owner.selected_proof ||
        current.current_thread_id != supplied->owner.current_thread_id) return 0;
    if (current.selected_proof == DLUMA_CONTENT_FINALIZATION_PROOF_SERIALIZED_FRAME_ROUTE)
        return current.serialized_route_token != 0 &&
            current.serialized_route_token == supplied->owner.serialized_route_token;
    if (current.selected_proof == DLUMA_CONTENT_FINALIZATION_PROOF_WINDOW_OWNER)
        return current.observed_window_owner_thread_id == supplied->owner.observed_window_owner_thread_id;
    return current.natural_shutdown.observed_token != 0 &&
        current.natural_shutdown.observed_token == supplied->owner.natural_shutdown.observed_token;
}

#endif
