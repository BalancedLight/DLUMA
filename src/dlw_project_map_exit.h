#ifndef DLUMA_PROJECT_MAP_EXIT_H
#define DLUMA_PROJECT_MAP_EXIT_H
#include "dlw_project_exit_authority.h"

typedef struct DlumaProjectMapExitKey {
    unsigned int map_generation;
    unsigned int project_epoch;
    unsigned int owner_thread_id;
    unsigned long long serialized_route_token;
    const void* outgoing_session;
    const void* outgoing_level;
} DlumaProjectMapExitKey;

typedef struct DlumaProjectMapExitReceipt {
    DlumaProjectMapExitKey key;
    unsigned int begin_complete;
    unsigned int context_rebound;
    unsigned int callbacks_resumed;
    unsigned int new_map_generation;
    unsigned int lua_notifications;
    unsigned int global_lua_owners;
    unsigned int global_managed_owners;
    unsigned int global_native_owners;
    char blocker[128];
} DlumaProjectMapExitReceipt;

#ifdef __cplusplus
extern "C" {
#endif
/* Sole notification site: the exact state-2, pre-original destruction edge.
 * Key/session/level identity must be captured by that native hook. */
int dluma_project_runtime_begin_outgoing_map(
    const DlumaProjectMapExitKey* key,DlumaProjectMapExitReceipt* out);
/* C10 performs no user callbacks. Its current authority may be on a different
 * serialized worker; the receipt still names the original pre-C08 owner. */
int dluma_project_runtime_finish_outgoing_map(
    const DlumaProjectMapExitKey* key,const DlumaProjectExitDrainAuthority* current,
    DlumaProjectMapExitReceipt* out);
/* Root calls only after terminal all-domain verification, never on failure. */
int dluma_project_runtime_resume_frontend(const DlumaProjectMapExitKey* key);
int dluma_project_runtime_snapshot_outgoing_map(DlumaProjectMapExitReceipt* out);
#ifdef __cplusplus
}
#endif
#endif
