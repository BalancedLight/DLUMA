#ifndef DLUMA_MANAGED_HOST_H
#define DLUMA_MANAGED_HOST_H

#include "../dlw_common.h"
#include "../dlw_project_host_exit.h"
#include "../core/dlw_mod_package_runtime.h"

#define DLUMA_MAX_MANAGED_MODS 128U
#define DLUMA_MANAGED_DIAGNOSTIC_CAPACITY 384U

typedef enum DlumaManagedModState {
    DLUMA_MANAGED_STATE_DISCOVERED_DISABLED = 0,
    DLUMA_MANAGED_STATE_APPROVED_NEXT_LAUNCH,
    DLUMA_MANAGED_STATE_LOADED,
    DLUMA_MANAGED_STATE_ACTIVE,
    DLUMA_MANAGED_STATE_DISABLE_NEXT_LAUNCH,
    DLUMA_MANAGED_STATE_CHANGED_REVIEW_REQUIRED,
    DLUMA_MANAGED_STATE_CONFLICT,
    DLUMA_MANAGED_STATE_INCOMPATIBLE,
    DLUMA_MANAGED_STATE_HOST_UNAVAILABLE,
    DLUMA_MANAGED_STATE_LOAD_ERROR,
    DLUMA_MANAGED_STATE_STARTUP_RECOVERY_SUPPRESSED,
    DLUMA_MANAGED_STATE_RETAINED
} DlumaManagedModState;

typedef struct DlumaManagedModSnapshot {
    char id[128];
    char name[128];
    char description[1025];
    char version[64];
    char author[128];
    char source_name[128];
    char workshop_id[64];
    char source_identity[512];
    char canonical_relative_path[512];
    char registry_key[DLUMA_SHA256_HEX_CAPACITY];
    char manifest_sha256[DLUMA_SHA256_HEX_CAPACITY];
    char lock_sha256[DLUMA_SHA256_HEX_CAPACITY];
    char entry_sha256[DLUMA_SHA256_HEX_CAPACITY];
    char approval_fingerprint[DLUMA_SHA256_HEX_CAPACITY];
    char config_schema_fingerprint[DLUMA_SHA256_HEX_CAPACITY];
    char api_version[16];
    char config_diagnostic[DLUMA_MANAGED_DIAGNOSTIC_CAPACITY];
    char runtime_error[DLUMA_MANAGED_DIAGNOSTIC_CAPACITY];
    WCHAR package_root[DLUMA_MAX_WPATH];
    WCHAR project_root[DLUMA_MAX_WPATH];
    int load_priority;
    unsigned int resolved_load_index;
    unsigned int access_count;
    unsigned int config_category_count;
    unsigned int config_setting_count;
    unsigned int scope;
    unsigned int state;
    unsigned int approval_valid;
    unsigned int enabled_requested;
    unsigned int duplicate_id;
    unsigned int loaded;
    unsigned int active;
    unsigned int config_declared;
    unsigned int config_available;
    unsigned int source_is_workshop;
    unsigned int source_is_subscribed;
    unsigned int source_selected;
    unsigned int source_selection_missing;
} DlumaManagedModSnapshot;

typedef struct DlumaManagedModMutationRequest {
    char registry_key[DLUMA_SHA256_HEX_CAPACITY];
    char approval_fingerprint[DLUMA_SHA256_HEX_CAPACITY];
    ULONGLONG registry_revision;
} DlumaManagedModMutationRequest;

#ifdef __cplusplus
extern "C" {
#endif

void dluma_managed_mods_discover(void);
unsigned int dluma_managed_mods_snapshot(
    DlumaManagedModSnapshot* output,
    unsigned int capacity,
    ULONGLONG* revision_out);
int dluma_managed_mods_copy_crash_context(
    char* output,
    unsigned int capacity);
void dluma_managed_mods_make_mutation_request(
    DlumaManagedModMutationRequest* request,
    const DlumaManagedModSnapshot* item,
    ULONGLONG revision);
int dluma_managed_mods_request_enabled(
    const DlumaManagedModMutationRequest* request,
    int enabled,
    int approve_current);
int dluma_managed_mods_request_source(
    const char* declared_id, const char* source_identity,
    ULONGLONG registry_revision);
int dluma_managed_mods_disable_all_next_launch(void);
const char* dluma_managed_mods_state_text(
    const DlumaManagedModSnapshot* item);

/* Starts only the optional managed language host. Failure never blocks the
 * original game manager, native MODs, or Lua. */
int dluma_managed_host_start(void);

/* Scope order is coordinated by the shared game-thread lifecycle owner:
 * outgoing Lua -> C# -> native, incoming native -> C# -> Lua. */
void dluma_managed_host_deactivate_scope(
    unsigned int next_map_generation);
int dluma_managed_host_revoke_project_for_exit(
    unsigned int expected_generation, DlumaProjectHostExitDrain* out);
int dluma_managed_host_revoke_project_for_exit_ex(
    unsigned int expected_generation, const DlumaProjectExitDrainAuthority* authority,
    DlumaProjectHostExitDrain* out);
int dluma_managed_host_freeze_map_exit(unsigned int generation,
    const DlumaProjectExitDrainAuthority* authority);
int dluma_managed_host_suspend_global_map(unsigned int generation,
    const DlumaProjectExitDrainAuthority* authority,unsigned int* count);
int dluma_managed_host_bind_frontend_after_exit(unsigned int old_generation,
    unsigned int new_generation,const DlumaProjectExitDrainAuthority* authority);
void dluma_managed_host_resume_frontend_after_exit(void);
void dluma_managed_host_activate_scope(
    const WCHAR* project_root,
    const char* project_name,
    const char* map_name,
    unsigned int map_generation);

int dluma_managed_host_enqueue_game_event(
    unsigned int kind,
    unsigned int map_generation,
    ULONGLONG native_index_epoch,
    ULONGLONG native_subscription_key_revision,
    const char* exact_name,
    int state,
    double number);
void dluma_managed_host_prepare_game_frame(void);
void dluma_managed_host_game_thread_pump(double delta_seconds);
int dluma_managed_host_config_changed(
    const char* registry_key,
    const char* approval_fingerprint,
    const char* setting_key);

/* Returns nonzero only when all managed callbacks were revoked. Assemblies,
 * ALCs, CoreCLR, and hostfxr stay mapped until process exit by contract. */
int dluma_managed_host_shutdown(int loader_callbacks_drained);

#ifdef __cplusplus
}
#endif

#endif
