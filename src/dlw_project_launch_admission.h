#ifndef DLUMA_PROJECT_LAUNCH_ADMISSION_H
#define DLUMA_PROJECT_LAUNCH_ADMISSION_H

#include "dlw_win32.h"

#ifdef __cplusplus
extern "C" {
#endif

#define DLUMA_PROJECT_LAUNCH_TEXT_CAPACITY 192U
#define DLUMA_PROJECT_LAUNCH_ROOT_CAPACITY 1024U
#define DLUMA_PROJECT_LAUNCH_WORKSHOP_ID_CAPACITY 32U
#define DLUMA_PROJECT_LAUNCH_REQUEST_MAX_AGE_MS 5000ULL
#define DLUMA_PROJECT_PREMOUNT_TOKEN_MAX_AGE_MS 5000ULL
#define DLUMA_PROJECT_DENIED_ACTIVATION_DEBOUNCE_MS 350ULL
#define DLUMA_PROJECT_LAUNCH_MODULE_CAPACITY 96U
#define DLUMA_PROJECT_LAUNCH_SOURCE_KIND_CAPACITY 48U
#define DLUMA_PROJECT_CHANGE_AFTER_FADE_ENTRY_RVA 0x00434DA0UL

typedef enum DlumaProjectLaunchOwner {
    DLUMA_PROJECT_LAUNCH_OWNER_NONE = 0,
    DLUMA_PROJECT_LAUNCH_OWNER_MENU_CUSTOM_GAME,
    DLUMA_PROJECT_LAUNCH_OWNER_MENU_START_LEVEL_DI,
    DLUMA_PROJECT_LAUNCH_OWNER_MENU_STEAM_WORKSHOP,
    DLUMA_PROJECT_LAUNCH_OWNER_COUNT
} DlumaProjectLaunchOwner;

typedef enum DlumaProjectLaunchRoute {
    DLUMA_PROJECT_LAUNCH_ROUTE_NONE = 0,
    DLUMA_PROJECT_LAUNCH_ROUTE_MOUSE_START,
    DLUMA_PROJECT_LAUNCH_ROUTE_GAMEPAD_KEYBOARD_START,
    DLUMA_PROJECT_LAUNCH_ROUTE_LIST_DOUBLE_CLICK,
    DLUMA_PROJECT_LAUNCH_ROUTE_ACCEPT_ACTION_CHAIN,
    DLUMA_PROJECT_LAUNCH_ROUTE_QUEUED_REQUEST,
    DLUMA_PROJECT_LAUNCH_ROUTE_DELAYED_TRANSITION,
    DLUMA_PROJECT_LAUNCH_ROUTE_DIRECT_PRESENTER
} DlumaProjectLaunchRoute;

typedef enum DlumaProjectLaunchAdmissionResult {
    DLUMA_PROJECT_LAUNCH_ADMISSION_NOT_ATTEMPTED = 0,
    DLUMA_PROJECT_LAUNCH_ADMISSION_DEFERRED,
    DLUMA_PROJECT_LAUNCH_ADMISSION_BLOCKED,
    DLUMA_PROJECT_LAUNCH_ADMISSION_ALLOWED,
    DLUMA_PROJECT_LAUNCH_ADMISSION_B0_PREPARING,
    DLUMA_PROJECT_LAUNCH_ADMISSION_B0_PERMANENT_SOURCE_FAILURE,
    DLUMA_PROJECT_LAUNCH_ADMISSION_B0_RESTART_REQUIRED,
    DLUMA_PROJECT_LAUNCH_ADMISSION_STALE_REQUEST,
    DLUMA_PROJECT_LAUNCH_ADMISSION_IDENTITY_MISMATCH,
    DLUMA_PROJECT_LAUNCH_ADMISSION_ACTIVE_TOKEN
} DlumaProjectLaunchAdmissionResult;

typedef enum DlumaProjectPreMountTokenState {
    DLUMA_PROJECT_PREMOUNT_TOKEN_NONE = 0,
    DLUMA_PROJECT_PREMOUNT_TOKEN_ARMED_PRE_MOUNT,
    DLUMA_PROJECT_PREMOUNT_TOKEN_PROJECT_ROOT_OBSERVED,
    DLUMA_PROJECT_PREMOUNT_TOKEN_EPOCH_BOUND,
    DLUMA_PROJECT_PREMOUNT_TOKEN_SOURCE_SET_COMPLETE,
    DLUMA_PROJECT_PREMOUNT_TOKEN_EXPIRED,
    DLUMA_PROJECT_PREMOUNT_TOKEN_FAILED
} DlumaProjectPreMountTokenState;

typedef enum DlumaProjectMountSourceKind {
    DLUMA_PROJECT_MOUNT_SOURCE_UNKNOWN = 0,
    DLUMA_PROJECT_MOUNT_SOURCE_PROJECT_ROOT,
    DLUMA_PROJECT_MOUNT_SOURCE_DATA_DIRECTORY,
    DLUMA_PROJECT_MOUNT_SOURCE_ASSETS_PC
} DlumaProjectMountSourceKind;

typedef enum DlumaProjectCancelledCleanupPreparation {
    DLUMA_PROJECT_CANCELLED_CLEANUP_NOT_REQUIRED = 0,
    DLUMA_PROJECT_CANCELLED_CLEANUP_READY,
    DLUMA_PROJECT_CANCELLED_CLEANUP_REJECTED
} DlumaProjectCancelledCleanupPreparation;

typedef struct DlumaProjectLaunchIdentity {
    char selection_key[DLUMA_PROJECT_LAUNCH_TEXT_CAPACITY];
    char campaign_name[DLUMA_PROJECT_LAUNCH_TEXT_CAPACITY];
    char map_name[DLUMA_PROJECT_LAUNCH_TEXT_CAPACITY];
    char display_project_root[DLUMA_PROJECT_LAUNCH_ROOT_CAPACITY];
    char normalized_project_root[DLUMA_PROJECT_LAUNCH_ROOT_CAPACITY];
    char workshop_id[DLUMA_PROJECT_LAUNCH_WORKSHOP_ID_CAPACITY];
    int profile_index;
    unsigned int transition_identity_complete;
} DlumaProjectLaunchIdentity;

typedef struct DlumaProjectSourceBypassEvidence {
    ULONG_PTR source_object;
    unsigned int source_flags;
    char requested_source_path[DLUMA_PROJECT_LAUNCH_ROOT_CAPACITY];
    char source_path[DLUMA_PROJECT_LAUNCH_ROOT_CAPACITY];
    char source_kind[DLUMA_PROJECT_LAUNCH_SOURCE_KIND_CAPACITY];
    ULONG_PTR caller_return_address;
    char caller_module[DLUMA_PROJECT_LAUNCH_MODULE_CAPACITY];
    ULONG_PTR caller_module_base;
    ULONG_PTR caller_module_rva;
    DWORD thread_id;
    char current_campaign[DLUMA_PROJECT_LAUNCH_TEXT_CAPACITY];
    char current_map[DLUMA_PROJECT_LAUNCH_TEXT_CAPACITY];
    char project_identity[DLUMA_PROJECT_LAUNCH_ROOT_CAPACITY];
    ULONG_PTR active_launch_controller;
    DlumaProjectLaunchRoute last_activation_route;
    DlumaProjectLaunchRoute last_premount_route;
    DlumaProjectLaunchOwner last_premount_owner;
    char last_b0_state[DLUMA_PROJECT_LAUNCH_TEXT_CAPACITY];
    ULONGLONG allowed_token_generation;
    DlumaProjectPreMountTokenState token_state;
    unsigned int bound_epoch;
    ULONGLONG set_custom_content_generation;
    ULONGLONG update_dlc_content_generation;
} DlumaProjectSourceBypassEvidence;

typedef struct DlumaProjectPreMountEvidence {
    DlumaProjectLaunchRoute route;
    DlumaProjectLaunchOwner owner;
    DWORD entry_rva;
    DWORD thread_id;
    ULONG_PTR controller;
    ULONG_PTR controller_vtable;
    ULONG_PTR ui_data;
    ULONG_PTR payload;
    ULONG_PTR caller_return_address;
    char caller_module[DLUMA_PROJECT_LAUNCH_MODULE_CAPACITY];
    ULONG_PTR caller_module_base;
    ULONG_PTR caller_module_rva;
    int map_source;
    unsigned int custom_map_source;
    char custom_content_name[DLUMA_PROJECT_LAUNCH_ROOT_CAPACITY];
    unsigned int custom_content_identity_corroborated;
    DlumaProjectLaunchIdentity identity;
    char b0_state[DLUMA_PROJECT_LAUNCH_TEXT_CAPACITY];
    ULONGLONG b0_generation;
    ULONGLONG frame_route_token;
    ULONGLONG now_ms;
    unsigned int startup_b0_complete_authorized;
    unsigned int restart_registry_can_admit;
    unsigned int authority_conflict;
    unsigned int content_isolation_enabled;
    unsigned int startup_b0_permanent_source_failure;
    char startup_b0_source_failure_reason[
        DLUMA_PROJECT_LAUNCH_TEXT_CAPACITY];
    char startup_b0_rejected_source[
        DLUMA_PROJECT_LAUNCH_TEXT_CAPACITY];
} DlumaProjectPreMountEvidence;

typedef struct DlumaProjectPreMountHit {
    DlumaProjectPreMountEvidence evidence;
    DlumaProjectLaunchAdmissionResult admission_result;
    unsigned int set_custom_content_name_reached;
    unsigned int update_dlc_content_reached;
    unsigned int original_invoked;
    unsigned int original_returned;
} DlumaProjectPreMountHit;

typedef struct DlumaProjectOnStartLevelAttestation {
    ULONGLONG token_generation;
    unsigned int epoch;
    DlumaProjectLaunchOwner selected_map_owner;
    DlumaProjectLaunchOwner on_start_level_owner;
    ULONG_PTR controller;
    DlumaProjectLaunchIdentity identity;
    unsigned int source_set_complete;
    ULONGLONG original_invocation_count;
    unsigned int succeeded;
} DlumaProjectOnStartLevelAttestation;

typedef struct DlumaProjectCancelledCleanupPlan {
    ULONGLONG prior_token_generation;
    ULONGLONG prior_token_created_ms;
    unsigned int prior_epoch;
    ULONGLONG fresh_request_generation;
    ULONGLONG fresh_request_created_ms;
    DWORD owner_thread_id;
    ULONG_PTR controller;
    char prior_normalized_project_root[
        DLUMA_PROJECT_LAUNCH_ROOT_CAPACITY];
    char fresh_normalized_project_root[
        DLUMA_PROJECT_LAUNCH_ROOT_CAPACITY];
    char first_failure[DLUMA_PROJECT_LAUNCH_TEXT_CAPACITY];
} DlumaProjectCancelledCleanupPlan;

typedef struct DlumaProjectCancelledCleanupResult {
    unsigned int active_source_count_before;
    unsigned int removed_source_count;
    unsigned int active_source_count_after;
    unsigned int live_registry_verified_absent;
    unsigned int loader_sources_unchanged;
    unsigned int replacement_source_absent;
    unsigned int prior_epoch_consumed;
    unsigned int succeeded;
    char first_failure[DLUMA_PROJECT_LAUNCH_TEXT_CAPACITY];
} DlumaProjectCancelledCleanupResult;

typedef enum DlumaProjectChangeAfterFadeDisposition {
    DLUMA_PROJECT_CHANGE_AFTER_FADE_NOT_ATTESTED = 0,
    DLUMA_PROJECT_CHANGE_AFTER_FADE_VERIFIED,
    DLUMA_PROJECT_CHANGE_AFTER_FADE_QUARANTINED_FOR_CLEANUP,
    DLUMA_PROJECT_CHANGE_AFTER_FADE_FATAL_SOURCE_CONTRADICTION
} DlumaProjectChangeAfterFadeDisposition;

typedef enum DlumaProjectSourceSetSnapshotResult {
    DLUMA_PROJECT_SOURCE_SET_SNAPSHOT_INVALID_ARGUMENT = 0,
    DLUMA_PROJECT_SOURCE_SET_SNAPSHOT_SUCCEEDED,
    DLUMA_PROJECT_SOURCE_SET_SNAPSHOT_LEDGER_BUSY,
    DLUMA_PROJECT_SOURCE_SET_SNAPSHOT_EPOCH_MISSING,
    DLUMA_PROJECT_SOURCE_SET_SNAPSHOT_PROJECT_ROOT_INVALID,
    DLUMA_PROJECT_SOURCE_SET_SNAPSHOT_PROJECT_ROOT_MISMATCH,
    DLUMA_PROJECT_SOURCE_SET_SNAPSHOT_OWNERSHIP_INCOMPLETE,
    DLUMA_PROJECT_SOURCE_SET_SNAPSHOT_EXACT_HANDLE_INCOMPLETE,
    DLUMA_PROJECT_SOURCE_SET_SNAPSHOT_DUPLICATE_SOURCE_KIND,
    DLUMA_PROJECT_SOURCE_SET_SNAPSHOT_REQUIRED_SOURCE_KIND_MISSING
} DlumaProjectSourceSetSnapshotResult;

typedef struct DlumaProjectChangeAfterFadeAttestation {
    ULONG_PTR session;
    ULONGLONG transition_generation;
    ULONGLONG transition_context_generation;
    ULONGLONG premount_frame_route_token;
    ULONGLONG current_frame_route_token;
    unsigned int epoch;
    DWORD exact_hook_entry_rva;
    DWORD premount_thread_id;
    DWORD transition_thread_id;
    /* Retained for source compatibility with older in-tree callers. */
    DWORD thread_id;
    unsigned int exact_change_after_fade_hook;
    unsigned int transition_context_current;
    unsigned int transition_context_depth;
    unsigned int transition_context_serialized;
    unsigned int transition_context_recursive;
    unsigned int transition_context_concurrent;
    /* Retained as an ABI-compatible alias of transition_context_serialized. */
    unsigned int serialized_route_valid;
    unsigned int authority_conflict;
    unsigned int non_frontend_destination;
    unsigned int source_set_complete;
    unsigned int source_ownership_complete;
    unsigned int project_root_source_count;
    unsigned int data_directory_source_count;
    unsigned int assets_pc_source_count;
    DlumaProjectSourceSetSnapshotResult source_set_snapshot_result;
    char normalized_project_root[
        DLUMA_PROJECT_LAUNCH_ROOT_CAPACITY];
    char destination_map[DLUMA_PROJECT_LAUNCH_TEXT_CAPACITY];
} DlumaProjectChangeAfterFadeAttestation;

typedef struct DlumaProjectLaunchAdmissionState {
    ULONGLONG generation;
    ULONGLONG activation_attempt_count;
    ULONGLONG custom_project_premount_attempt_count;
    ULONGLONG custom_project_premount_allowed_count;
    ULONGLONG custom_project_premount_denied_count;
    ULONGLONG custom_project_update_dlc_content_count;
    ULONGLONG custom_project_source_epoch_bound_count;
    ULONGLONG custom_project_on_start_level_attestation_count;
    ULONGLONG custom_project_attestation_superseded_count;
    ULONGLONG custom_project_cancelled_cleanup_attempt_count;
    ULONGLONG custom_project_cancelled_cleanup_success_count;
    ULONGLONG custom_project_cancelled_cleanup_failure_count;
    ULONGLONG custom_project_change_after_fade_attestation_count;
    ULONGLONG custom_project_change_after_fade_attestation_success_count;
    ULONGLONG custom_project_change_after_fade_attestation_failure_count;
    ULONGLONG custom_project_mount_bypass_count;
    ULONGLONG source_mount_observation_count;
    ULONGLONG denied_activation_duplicate_count;
    ULONGLONG denied_premount_duplicate_count;

    unsigned int request_valid;
    ULONGLONG request_generation;
    ULONGLONG request_created_ms;
    DlumaProjectLaunchRoute request_route;
    DlumaProjectLaunchIdentity request_identity;

    unsigned int last_activation_delivery_valid;
    unsigned int last_activation_delivery_debounced;
    ULONGLONG last_activation_delivery_ms;
    DlumaProjectLaunchRoute last_activation_delivery_route;
    DlumaProjectLaunchAdmissionResult
        last_activation_delivery_result;
    DlumaProjectLaunchIdentity last_activation_delivery_identity;

    DlumaProjectPreMountTokenState token_state;
    unsigned int token_active;
    unsigned int attestation_pending;
    ULONGLONG token_generation;
    ULONGLONG token_created_ms;
    DlumaProjectLaunchRoute token_route;
    DlumaProjectLaunchOwner token_owner;
    DWORD token_entry_rva;
    DWORD token_thread_id;
    ULONG_PTR token_controller_identity;
    ULONG_PTR token_controller_vtable;
    ULONG_PTR token_ui_data_identity;
    ULONG_PTR token_payload_identity;
    int token_map_source;
    ULONGLONG token_b0_generation;
    ULONGLONG token_frame_route_token;
    unsigned int token_epoch;
    unsigned int token_project_root_observed;
    unsigned int token_data_directory_observed;
    unsigned int token_assets_pc_observed;
    unsigned int selected_map_body_active;
    unsigned int update_dlc_content_active;
    unsigned int transition_started;
    unsigned int project_admitted;
    unsigned int admission_blocked;
    ULONGLONG set_custom_content_generation;
    ULONGLONG update_dlc_content_generation;
    ULONGLONG original_selected_map_invocation_count;
    ULONGLONG original_selected_map_return_count;
    DlumaProjectLaunchIdentity token_identity;

    ULONGLONG premount_owner_hit_count[
        DLUMA_PROJECT_LAUNCH_OWNER_COUNT];
    DlumaProjectPreMountHit premount_owner_hits[
        DLUMA_PROJECT_LAUNCH_OWNER_COUNT];
    DlumaProjectLaunchRoute last_activation_route;
    DlumaProjectLaunchRoute last_premount_route;
    DlumaProjectLaunchOwner last_premount_owner;
    DWORD last_premount_entry_rva;
    DlumaProjectLaunchAdmissionResult last_admission_result;
    DlumaProjectLaunchIdentity last_selected_identity;
    char last_b0_state[DLUMA_PROJECT_LAUNCH_TEXT_CAPACITY];
    unsigned int last_request_consumed;
    unsigned int last_original_selected_map_invoked;
    unsigned int last_original_selected_map_returned;
    unsigned int last_transition_queued;
    DlumaProjectPreMountHit last_premount_hit;
    DlumaProjectOnStartLevelAttestation last_on_start_level_attestation;
    unsigned int last_premount_denial_valid;
    unsigned int last_premount_delivery_debounced;
    ULONGLONG last_premount_denial_ms;
    ULONG_PTR last_premount_denial_controller;
    ULONG_PTR last_premount_denial_payload;
    DlumaProjectLaunchAdmissionResult last_premount_denial_result;
    DlumaProjectLaunchIdentity last_premount_denial_identity;

    ULONGLONG custom_project_cancelled_cleanup_prior_token_generation;
    unsigned int custom_project_cancelled_cleanup_prior_epoch;
    unsigned int custom_project_cancelled_cleanup_active_source_count_before;
    unsigned int custom_project_cancelled_cleanup_removed_source_count;
    unsigned int custom_project_cancelled_cleanup_active_source_count_after;
    char custom_project_cancelled_cleanup_first_failure[
        DLUMA_PROJECT_LAUNCH_TEXT_CAPACITY];
    unsigned int custom_project_fresh_epoch;

    DlumaProjectLaunchOwner custom_project_transition_attestation_owner;
    char custom_project_transition_attestation_destination[
        DLUMA_PROJECT_LAUNCH_TEXT_CAPACITY];
    ULONGLONG custom_project_transition_attestation_generation;
    ULONGLONG
        custom_project_last_consumed_transition_generation;
    ULONGLONG custom_project_transition_context_generation;
    ULONGLONG custom_project_premount_frame_route_token;
    ULONGLONG custom_project_current_frame_route_token;
    ULONG_PTR custom_project_transition_attestation_session;
    DWORD custom_project_premount_thread_id;
    DWORD custom_project_transition_thread_id;
    DWORD custom_project_change_after_fade_entry_rva;
    unsigned int custom_project_transition_context_depth;
    unsigned int custom_project_transition_context_serialized;
    unsigned int custom_project_transition_thread_migrated;
    DlumaProjectSourceSetSnapshotResult
        custom_project_source_set_snapshot_result;
    DlumaProjectChangeAfterFadeDisposition
        custom_project_change_after_fade_disposition;
    unsigned int active_epoch_quarantined;
    unsigned int transition_attestation_failure;
    unsigned int new_project_admission_blocked;
    unsigned int cleanup_still_authorized;
    unsigned int quarantined_epoch;
    char custom_project_transition_attestation_failure_reason[
        DLUMA_PROJECT_LAUNCH_TEXT_CAPACITY];

    unsigned int custom_project_mount_bypassed_admission_gate;
    char bypass_project_root[DLUMA_PROJECT_LAUNCH_ROOT_CAPACITY];
    unsigned int bypass_evidence_valid;
    DlumaProjectSourceBypassEvidence bypass_evidence;
    char first_failure_reason[DLUMA_PROJECT_LAUNCH_TEXT_CAPACITY];
} DlumaProjectLaunchAdmissionState;

void dluma_project_launch_admission_initialize(
    DlumaProjectLaunchAdmissionState* state);

int dluma_project_launch_admission_note_activation(
    DlumaProjectLaunchAdmissionState* state,
    DlumaProjectLaunchRoute route,
    const DlumaProjectLaunchIdentity* identity,
    ULONGLONG now_ms,
    DlumaProjectLaunchAdmissionResult preflight_result);

DlumaProjectLaunchAdmissionResult
dluma_project_launch_admission_begin_premount(
    DlumaProjectLaunchAdmissionState* state,
    const DlumaProjectPreMountEvidence* evidence);

DlumaProjectCancelledCleanupPreparation
dluma_project_launch_admission_prepare_cancelled_cleanup(
    DlumaProjectLaunchAdmissionState* state,
    const DlumaProjectPreMountEvidence* evidence,
    DlumaProjectCancelledCleanupPlan* plan);

int dluma_project_launch_admission_note_cancelled_cleanup_result(
    DlumaProjectLaunchAdmissionState* state,
    const DlumaProjectCancelledCleanupPlan* plan,
    const DlumaProjectCancelledCleanupResult* result);

void dluma_project_launch_admission_note_selected_map_original(
    DlumaProjectLaunchAdmissionState* state,
    int entering);

void dluma_project_launch_admission_note_transition_queued(
    DlumaProjectLaunchAdmissionState* state,
    int queued);

int dluma_project_launch_admission_note_update_dlc_content(
    DlumaProjectLaunchAdmissionState* state,
    DlumaProjectLaunchOwner owner,
    DWORD caller_return_rva,
    DWORD current_thread_id,
    int entering);

int dluma_project_launch_admission_note_custom_source(
    DlumaProjectLaunchAdmissionState* state,
    DlumaProjectMountSourceKind source_kind,
    const char* normalized_project_root,
    unsigned int epoch,
    DWORD current_thread_id,
    ULONGLONG now_ms);

int dluma_project_launch_admission_note_custom_source_ex(
    DlumaProjectLaunchAdmissionState* state,
    DlumaProjectMountSourceKind source_kind,
    const char* normalized_project_root,
    unsigned int epoch,
    DWORD current_thread_id,
    ULONGLONG now_ms,
    const DlumaProjectSourceBypassEvidence* evidence);

int dluma_project_launch_admission_attest_on_start_level(
    DlumaProjectLaunchAdmissionState* state,
    DlumaProjectLaunchOwner owner,
    ULONG_PTR controller,
    const DlumaProjectLaunchIdentity* identity,
    ULONGLONG now_ms);

int dluma_project_launch_admission_attest_change_after_fade(
    DlumaProjectLaunchAdmissionState* state,
    const DlumaProjectChangeAfterFadeAttestation* attestation);

int dluma_project_launch_admission_note_quarantined_cleanup_result(
    DlumaProjectLaunchAdmissionState* state,
    unsigned int epoch,
    int cleanup_verified);

void dluma_project_launch_admission_block(
    DlumaProjectLaunchAdmissionState* state,
    const char* reason);

void dluma_project_launch_admission_note_on_start_level_original(
    DlumaProjectLaunchAdmissionState* state);

void dluma_project_launch_admission_cancel(
    DlumaProjectLaunchAdmissionState* state,
    const char* reason);

const char* dluma_project_launch_owner_name(
    DlumaProjectLaunchOwner owner);
const char* dluma_project_launch_route_name(
    DlumaProjectLaunchRoute route);
const char* dluma_project_launch_admission_result_name(
    DlumaProjectLaunchAdmissionResult result);
const char* dluma_project_premount_token_state_name(
    DlumaProjectPreMountTokenState state);
const char* dluma_project_mount_source_kind_name(
    DlumaProjectMountSourceKind kind);
const char* dluma_project_change_after_fade_disposition_name(
    DlumaProjectChangeAfterFadeDisposition disposition);
const char* dluma_project_source_set_snapshot_result_name(
    DlumaProjectSourceSetSnapshotResult result);

#ifdef __cplusplus
}
#endif

#endif
