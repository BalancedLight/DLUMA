#ifndef DLUMA_NATIVE_MODS_H
#define DLUMA_NATIVE_MODS_H

#include "dlw_common.h"
#include "dlw_companion_archive.h"
#include "dlw_native_companion_source_owner.h"
#include "dlw_project_host_exit.h"
#include "core/dlw_mod_manifest.h"
#include "../sdk/c/dluma_editor_api.h"

/* Auxiliary DLL MODs are deliberately separate from Lua discovery.  A record
 * is copied before presentation so XUI never holds an author-owned pointer. */
#define DLUMA_MAX_NATIVE_MODS 128U
#define DLUMA_NATIVE_MOD_FILE_MAX_BYTES (64ULL * 1024ULL * 1024ULL)
#define DLUMA_NATIVE_MOD_SIDECAR_MAX_BYTES (64ULL * 1024ULL)

typedef enum DlumaModKind {
    DLUMA_MOD_KIND_NATIVE_DLL = 1,
    DLUMA_MOD_KIND_LUA = 2,
    DLUMA_MOD_KIND_CSHARP = 3
} DlumaModKind;

typedef enum DlumaNativeModAbiKind {
    DLUMA_NATIVE_ABI_UNKNOWN = 0,
    DLUMA_NATIVE_ABI_V2,
    DLUMA_NATIVE_ABI_EDITOR_V1,
    DLUMA_NATIVE_ABI_LEGACY_V1,
    DLUMA_NATIVE_ABI_LEGACY_GAME_SCRIPT,
    DLUMA_NATIVE_ABI_INVALID
} DlumaNativeModAbiKind;

typedef enum DlumaNativeModRuntimeState {
    DLUMA_NATIVE_STATE_DISCOVERED_DISABLED = 0,
    DLUMA_NATIVE_STATE_APPROVED_NEXT_LAUNCH,
    DLUMA_NATIVE_STATE_LOADED,
    DLUMA_NATIVE_STATE_DISABLE_NEXT_LAUNCH,
    DLUMA_NATIVE_STATE_CHANGED_REVIEW_REQUIRED,
    DLUMA_NATIVE_STATE_CONFLICT,
    DLUMA_NATIVE_STATE_INCOMPATIBLE,
    DLUMA_NATIVE_STATE_LOAD_ERROR,
    DLUMA_NATIVE_STATE_STARTUP_RECOVERY_SUPPRESSED
} DlumaNativeModRuntimeState;

typedef enum DlumaNativeModHostState {
    DLUMA_NATIVE_HOST_OFF = 0,
    DLUMA_NATIVE_HOST_PLAYER = 1,
    DLUMA_NATIVE_HOST_EDITOR = 2,
    DLUMA_NATIVE_HOST_BOTH = 3
} DlumaNativeModHostState;

typedef struct DlumaNativeQuestPhaseHandlerSnapshot {
    char id[128];
    char command_alias[64];
    char description[256];
} DlumaNativeQuestPhaseHandlerSnapshot;

typedef struct DlumaNativeModSnapshot {
    WCHAR final_path[DLUMA_MAX_WPATH];
    WCHAR project_root[DLUMA_MAX_WPATH];
    char id[128];
    char name[128];
    char description[1025];
    char version[64];
    char source_name[128];
    char workshop_id[64];
    char canonical_relative_path[512];
    char source_identity[512];
    char registry_key[DLUMA_SHA256_HEX_CAPACITY];
    char module_sha256[DLUMA_SHA256_HEX_CAPACITY];
    char sidecar_sha256[DLUMA_SHA256_HEX_CAPACITY];
    char manifest_sha256[DLUMA_SHA256_HEX_CAPACITY];
    char lock_sha256[DLUMA_SHA256_HEX_CAPACITY];
    char approval_fingerprint[DLUMA_SHA256_HEX_CAPACITY];
    char player_approval_fingerprint[DLUMA_SHA256_HEX_CAPACITY];
    char editor_approval_fingerprint[DLUMA_SHA256_HEX_CAPACITY];
    char api_version[16];
    char config_schema_fingerprint[DLUMA_SHA256_HEX_CAPACITY];
    char config_diagnostic[256];
    DlumaCompanionArchiveSnapshot companion_pack;
    char runtime_error[256];
    DWORD volume_serial;
    DWORD file_index_high;
    DWORD file_index_low;
    DWORD file_size;
    int load_priority;
    unsigned int resolved_load_index;
    unsigned int access_count;
    DlumaNativeQuestPhaseHandlerSnapshot quest_phase_handlers[
        DLUMA_MOD_MANIFEST_MAX_QUEST_PHASE_HANDLERS];
    unsigned int quest_phase_handler_count;
    unsigned short config_category_count;
    unsigned short config_setting_count;
    unsigned char kind;
    unsigned char abi_kind;
    unsigned char pe_machine_valid;
    unsigned char pe_dll_valid;
    unsigned char editor_abi_available;
    unsigned char source_is_workshop;
    unsigned char source_is_subscribed;
    unsigned char sidecar_present;
    unsigned char manifest_present;
    unsigned char scope;
    unsigned char declared_id;
    unsigned char duplicate_id;
    unsigned char approval_valid;
    unsigned char enabled_requested;
    unsigned char declared_host_flags;
    unsigned char requested_host_flags;
    unsigned char player_approval_valid;
    unsigned char editor_approval_valid;
    unsigned char loaded;
    unsigned char pre_init_succeeded;
    unsigned char post_init_succeeded;
    unsigned char state;
    unsigned char config_declared;
    unsigned char config_available;
    unsigned char source_selected;
    unsigned char source_selection_missing;
    unsigned char auto_approved;
} DlumaNativeModSnapshot;

typedef struct DlumaNativeModMutationRequest {
    char registry_key[DLUMA_SHA256_HEX_CAPACITY];
    char approval_fingerprint[DLUMA_SHA256_HEX_CAPACITY];
    ULONGLONG registry_revision;
} DlumaNativeModMutationRequest;

#ifdef __cplusplus
extern "C" {
#endif

void dluma_native_mods_discover(void);
void dluma_native_mods_set_original_gamedll(HMODULE original_gamedll);
unsigned int dluma_native_mods_snapshot(
    DlumaNativeModSnapshot* out, unsigned int capacity,
    ULONGLONG* revision_out);
int dluma_native_mods_request_enabled(
    const DlumaNativeModMutationRequest* request, int enabled,
    int approve_current);
int dluma_native_mods_request_source(
    const char* declared_id, const char* source_identity,
    ULONGLONG registry_revision);
int dluma_native_mods_request_hosts(
    const DlumaNativeModMutationRequest* request,
    unsigned int host_flags,
    int approve_player,
    int approve_editor);
int dluma_native_mods_disable_exact_next_launch(
    const char* registry_key, const char* approval_fingerprint);
int dluma_native_mods_disable_all_next_launch(void);
void dluma_native_mods_make_mutation_request(
    DlumaNativeModMutationRequest* request,
    const DlumaNativeModSnapshot* item, ULONGLONG revision);
const char* dluma_native_mods_abi_text(unsigned char abi_kind);
const char* dluma_native_mods_state_text(const DlumaNativeModSnapshot* item);
void dluma_native_mods_pre_game_init(HMODULE original_gamedll);
void dluma_native_mods_post_game_init(HMODULE original_gamedll,
                                    void* game_script_manager);
int dluma_native_mods_editor_load(void);
int dluma_native_mods_editor_context_changed(
    const WCHAR* project_root,
    const char* project_name,
    const char* map_name,
    ULONGLONG context_generation);
int dluma_native_mods_editor_session_begin(ULONGLONG session_generation);
int dluma_native_mods_editor_session_end(ULONGLONG session_generation);
int dluma_native_mods_editor_callbacks_drained(void);
int dluma_native_mods_editor_validate_phase(
    const char* provider_mod,
    const char* handler_id,
    const char* phase_id,
    unsigned int source_line,
    const char* arguments_json,
    unsigned int arguments_size,
    char* diagnostic,
    unsigned int diagnostic_capacity);
void dluma_native_mods_transition_scope(
    const WCHAR* project_root,
    const char* project_name,
    const char* map_name,
    unsigned int map_generation);
void dluma_native_mods_deactivate_scope(
    unsigned int next_map_generation);
int dluma_native_mods_revoke_project_for_exit(
    unsigned int expected_generation, DlumaProjectHostExitDrain* out);
int dluma_native_mods_revoke_project_for_exit_ex(
    unsigned int expected_generation, const DlumaProjectExitDrainAuthority* authority,
    DlumaProjectHostExitDrain* out);
int dluma_native_mods_freeze_map_exit(unsigned int generation,
    const DlumaProjectExitDrainAuthority* authority);
int dluma_native_mods_suspend_global_map(unsigned int generation,
    const DlumaProjectExitDrainAuthority* authority,unsigned int* count);
int dluma_native_mods_bind_frontend_after_exit(unsigned int old_generation,
    unsigned int new_generation,const DlumaProjectExitDrainAuthority* authority);
void dluma_native_mods_resume_frontend_after_exit(void);
void dluma_native_mods_activate_scope(
    const WCHAR* project_root,
    const char* project_name,
    const char* map_name,
    unsigned int map_generation);
int dluma_native_mods_enqueue_game_event(
    unsigned int kind,
    unsigned int map_generation,
    ULONGLONG native_index_epoch,
    ULONGLONG native_subscription_key_revision,
    const char* exact_name,
    int state,
    double number);
void dluma_native_mods_prepare_game_frame(void);
void dluma_native_mods_game_thread_pump(
    double delta_seconds);
int dluma_native_mods_config_changed(
    const char* registry_key,
    const char* approval_fingerprint,
    const char* setting_key);
void dluma_native_mods_shutdown_chain(HMODULE original_gamedll,
                                    int loader_callbacks_drained);
int dluma_native_mods_copy_crash_context(char* out, unsigned int capacity);
int dluma_native_mods_validate_companion_source_owner(
    const DlumaProcessLifetimeSourceRecord* record);
DlumaNativeCompanionOwnerValidationResult
dluma_native_mods_validate_companion_source_owner_ex(
    const DlumaProcessLifetimeSourceRecord* record,
    DlumaNativeCompanionOwnerValidationDiagnostic* diagnostic);

#ifdef __cplusplus
}
#endif

#endif
