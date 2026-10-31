#ifndef DLUMA_COMMON_H
#define DLUMA_COMMON_H

#include "dlw_win32.h"
#include "dlw_build_identity.h"
#include "dlw_game_frame_authority.h"
#include "dlw_project_launch_admission.h"
#include "dlw_orderly_close_core.h"

#define DLUMA_VERSION "0.5.1"
#define DLUMA_MAX_WPATH 1024
#define DLUMA_WORKSHOP_PREVIEW_DDS_GENERATION 4U
#define DLUMA_WORKSHOP_PREVIEW_EDGE 512U
#define DLUMA_WORKSHOP_PREVIEW_CONTENT_WIDTH 512U
#define DLUMA_WORKSHOP_PREVIEW_CONTENT_HEIGHT 288U
#define DLUMA_WORKSHOP_PREVIEW_CONTENT_TOP \
    ((DLUMA_WORKSHOP_PREVIEW_EDGE - \
      DLUMA_WORKSHOP_PREVIEW_CONTENT_HEIGHT) / 2U)
#define DLUMA_WORKSHOP_PREVIEW_CONTENT_BOTTOM \
    (DLUMA_WORKSHOP_PREVIEW_CONTENT_TOP + \
     DLUMA_WORKSHOP_PREVIEW_CONTENT_HEIGHT)
#define DLUMA_MAX_TEXT 512
#define DLUMA_MAX_MAP_NAME 128
#define DLUMA_MAX_ROOTS 16
#define DLUMA_WORKSHOP_MENU_TITLE_CAPACITY 256
#define DLUMA_WORKSHOP_MENU_DESCRIPTION_CAPACITY 1024
#define DLUMA_WORKSHOP_STEAM_DESCRIPTION_CAPACITY 8001
#define DLUMA_WORKSHOP_MENU_AUTHOR_CAPACITY 128
#define DLUMA_WORKSHOP_MENU_ID_CAPACITY 32
#define DLUMA_MAX_SCRIPT_PATHS 64
#define DLUMA_MAX_SCRIPTS 32
#define DLUMA_MAX_LUA_FILES 256
#define DLUMA_MAX_LUA_IDS 128
#define DLUMA_MAX_QUEST_PHASE_HANDLERS 32U
#define DLUMA_SHA256_HEX_CAPACITY 65
#define DLUMA_MAX_GLOBAL_RUNTIME_COMMANDS 64
#define DLUMA_MAX_EVENTS 192
#define DLUMA_MAX_ACTIONS 384
#define DLUMA_MAX_SCRIPT_FILE_BYTES (256UL * 1024UL)
#define DLUMA_ASSET_TRACE_MAX_WATCH_PATHS 8
#define DLUMA_ASSET_TRACE_WATCH_PATH_CAPACITY 192

#define DLUMA_LOCAL_LUA_POLICY_GAME_READ (1UL << 0)
#define DLUMA_LOCAL_LUA_POLICY_PLAYER_READ (1UL << 1)
#define DLUMA_LOCAL_LUA_POLICY_WORLD_QUERY (1UL << 2)
#define DLUMA_LOCAL_LUA_POLICY_EVENTS_SENSOR (1UL << 3)
#define DLUMA_LOCAL_LUA_POLICY_EVENTS_INTERACT (1UL << 4)
#define DLUMA_LOCAL_LUA_POLICY_STORAGE_MOD (1UL << 5)
#define DLUMA_LOCAL_LUA_POLICY_UI_DEBUG (1UL << 6)
#define DLUMA_LOCAL_LUA_POLICY_UI_OVERLAY (1UL << 7)
#define DLUMA_LOCAL_LUA_POLICY_AUDIO_PLAY (1UL << 8)
#define DLUMA_LOCAL_LUA_POLICY_WORLD_WRITE (1UL << 9)
#define DLUMA_LOCAL_LUA_POLICY_QUEST_WRITE (1UL << 10)
#define DLUMA_LOCAL_LUA_POLICY_EVENTS_ENEMY_KILLED (1UL << 11)
#define DLUMA_LOCAL_LUA_POLICY_QUEST_PHASE_HANDLE (1UL << 12)
#define DLUMA_LOCAL_LUA_POLICY_QUEST_PHASE_OBSERVE (1UL << 13)

#define DLUMA_LUA_QUEST_UI_MAX_SNAPSHOTS 128u
#define DLUMA_LUA_QUEST_UI_MAX_SCRIPT_ID 128u
#define DLUMA_LUA_QUEST_UI_MAX_ID 128u
#define DLUMA_LUA_QUEST_UI_MAX_TITLE 160u
#define DLUMA_LUA_QUEST_UI_MAX_VERSION 64u
#define DLUMA_LUA_QUEST_UI_MAX_OBJECTIVE_SUMMARY 1024u

#define DLUMA_ARRAY_COUNT(x) ((unsigned int)(sizeof(x) / sizeof((x)[0])))

typedef enum DlumaEventType
{
    DLUMA_EVENT_NONE = 0,
    DLUMA_EVENT_MAP_LOAD,
    DLUMA_EVENT_MAP_UNLOAD,
    DLUMA_EVENT_TIMER,
    DLUMA_EVENT_KEY,
    DLUMA_EVENT_TRIGGER_ENTER,
    DLUMA_EVENT_TRIGGER_EXIT,
    DLUMA_EVENT_INTERACT
} DlumaEventType;

typedef enum DlumaActionType
{
    DLUMA_ACTION_NONE = 0,
    DLUMA_ACTION_LOG,
    DLUMA_ACTION_MARKER,
    DLUMA_ACTION_DUMP_CONTEXT
} DlumaActionType;

typedef enum DlumaLifecycleState
{
    DLUMA_LIFECYCLE_COLD = 0,
    DLUMA_LIFECYCLE_RUNNING,
    DLUMA_LIFECYCLE_QUIESCING,
    DLUMA_LIFECYCLE_DETACHED,
    DLUMA_LIFECYCLE_FINALIZED
} DlumaLifecycleState;

typedef struct DlumaAction
{
    unsigned char type;
    char first[DLUMA_MAX_TEXT];
    char second[DLUMA_MAX_TEXT];
} DlumaAction;

typedef struct DlumaEvent
{
    unsigned char type;
    unsigned char script_index;
    unsigned short first_action;
    unsigned short action_count;
    unsigned short key_code;
    unsigned char key_was_down;
    unsigned char reserved;
    DWORD interval_ms;
    ULONGLONG last_fire_ms;
    char target[DLUMA_MAX_TEXT];
} DlumaEvent;

typedef struct DlumaScript
{
    WCHAR file_path[DLUMA_MAX_WPATH];
    WCHAR project_root[DLUMA_MAX_WPATH];
    char id[128];
    char name[128];
    unsigned short first_event;
    unsigned short event_count;
    unsigned char valid;
    unsigned char reserved[3];
} DlumaScript;

typedef struct DlumaScriptSet
{
    DlumaScript scripts[DLUMA_MAX_SCRIPTS];
    DlumaEvent events[DLUMA_MAX_EVENTS];
    DlumaAction actions[DLUMA_MAX_ACTIONS];
    unsigned int script_count;
    unsigned int event_count;
    unsigned int action_count;
} DlumaScriptSet;

typedef enum DlumaLuaScope
{
    DLUMA_LUA_SCOPE_INVALID = 0,
    DLUMA_LUA_SCOPE_GLOBAL,
    DLUMA_LUA_SCOPE_PROJECT,
    DLUMA_LUA_SCOPE_MAP,
    /* Compatibility name used by the 0.1.x discovery probe. */
    DLUMA_LUA_SCOPE_LOCAL = DLUMA_LUA_SCOPE_PROJECT
} DlumaLuaScope;

typedef enum DlumaGlobalReviewState
{
    DLUMA_GLOBAL_REVIEW_INVALID = 0,
    DLUMA_GLOBAL_REVIEW_NEW,
    DLUMA_GLOBAL_REVIEW_SEEN,
    DLUMA_GLOBAL_REVIEW_APPROVED_DISABLED,
    DLUMA_GLOBAL_REVIEW_APPROVED_ENABLED,
    DLUMA_GLOBAL_REVIEW_CHANGED_REVIEW_REQUIRED,
    DLUMA_GLOBAL_REVIEW_CONFLICT
} DlumaGlobalReviewState;

typedef enum DlumaGlobalChangeReason
{
    DLUMA_GLOBAL_CHANGE_NONE = 0,
    DLUMA_GLOBAL_CHANGE_CONTENT = 1 << 0,
    DLUMA_GLOBAL_CHANGE_VERSION = 1 << 1,
    DLUMA_GLOBAL_CHANGE_PERMISSIONS = 1 << 2,
    DLUMA_GLOBAL_CHANGE_PERMISSION_EXPANSION = 1 << 3,
    DLUMA_GLOBAL_CHANGE_SOURCE = 1 << 4,
    DLUMA_GLOBAL_CHANGE_PATH = 1 << 5
} DlumaGlobalChangeReason;

typedef enum DlumaGlobalRuntimeCommandType
{
    DLUMA_GLOBAL_RUNTIME_NONE = 0,
    DLUMA_GLOBAL_RUNTIME_LOAD,
    DLUMA_GLOBAL_RUNTIME_UNLOAD
} DlumaGlobalRuntimeCommandType;

typedef enum DlumaGlobalRuntimeTransition
{
    DLUMA_GLOBAL_TRANSITION_IDLE = 0,
    DLUMA_GLOBAL_TRANSITION_ENABLING,
    DLUMA_GLOBAL_TRANSITION_DISABLING,
    DLUMA_GLOBAL_TRANSITION_ERROR
} DlumaGlobalRuntimeTransition;

typedef struct DlumaSha256Context
{
    DWORD state[8];
    ULONGLONG total_bytes;
    BYTE block[64];
    unsigned int block_size;
} DlumaSha256Context;

/* Immutable loader-owned evidence for the process-lifetime content PAK.
 * The mount path and file identity are captured from the exact handle whose
 * bytes were hashed before fs::add_source.  Startup B0 later joins this record
 * to exactly one live CFSSource inventory object. */
typedef struct DlumaContentPackMountSnapshot
{
    char mounted_path[DLUMA_MAX_WPATH * 2];
    char canonical_path[DLUMA_MAX_WPATH * 2];
    char content_sha256[DLUMA_SHA256_HEX_CAPACITY];
    ULONGLONG file_size;
    ULONGLONG mount_generation;
    ULONGLONG exact_source;
    ULONGLONG exact_link;
    ULONGLONG source_identity;
    DWORD source_flags;
    DWORD source_list_ordinal;
    DWORD volume_serial;
    DWORD file_index_high;
    DWORD file_index_low;
    unsigned char mounted;
    unsigned char metadata_complete;
    unsigned char exact_source_bound;
    unsigned char reserved;
} DlumaContentPackMountSnapshot;

typedef struct DlumaLuaQuestPhaseHandlerMetadata
{
    char id[128];
    char command_alias[64];
} DlumaLuaQuestPhaseHandlerMetadata;

typedef struct DlumaLuaQuestPhaseProviderMetadata
{
    char id[128];
    char manifest_sha256[DLUMA_SHA256_HEX_CAPACITY];
    char content_sha256[DLUMA_SHA256_HEX_CAPACITY];
    DlumaLuaQuestPhaseHandlerMetadata quest_phase_handlers[
        DLUMA_MAX_QUEST_PHASE_HANDLERS];
    unsigned int quest_phase_handler_count;
    unsigned int declared_host_flags;
    int load_priority;
    unsigned char scope;
    unsigned char duplicate_id;
    unsigned char reserved[2];
} DlumaLuaQuestPhaseProviderMetadata;

typedef struct DlumaLuaMetadata
{
    WCHAR file_path[DLUMA_MAX_WPATH];
    WCHAR project_root[DLUMA_MAX_WPATH];
    WCHAR package_root[DLUMA_MAX_WPATH];
    char id[128];
    char name[128];
    char description[1025];
    char version[64];
    char api_version[16];
    char permissions[2048];
    char source_name[128];
    char workshop_id[64];
    ULONGLONG fingerprint;
    char normalized_permissions[2048];
    char canonical_relative_path[512];
    char package_entry[512];
    char source_identity[512];
    char registry_key[DLUMA_SHA256_HEX_CAPACITY];
    char storage_key[DLUMA_SHA256_HEX_CAPACITY];
    char content_sha256[DLUMA_SHA256_HEX_CAPACITY];
    char manifest_sha256[DLUMA_SHA256_HEX_CAPACITY];
    char lock_sha256[DLUMA_SHA256_HEX_CAPACITY];
    char metadata_fingerprint[DLUMA_SHA256_HEX_CAPACITY];
    char permission_fingerprint[DLUMA_SHA256_HEX_CAPACITY];
    char seen_fingerprint[DLUMA_SHA256_HEX_CAPACITY];
    char approved_fingerprint[DLUMA_SHA256_HEX_CAPACITY];
    char prompted_fingerprint[DLUMA_SHA256_HEX_CAPACITY];
    char change_reason[256];
    char runtime_error[256];
    char permission_diagnostic[512];
    char config_schema_fingerprint[DLUMA_SHA256_HEX_CAPACITY];
    char config_diagnostic[256];
    unsigned int change_reason_flags;
    unsigned int access_ids[64];
    unsigned int access_count;
    DlumaLuaQuestPhaseHandlerMetadata quest_phase_handlers[
        DLUMA_MAX_QUEST_PHASE_HANDLERS];
    unsigned int quest_phase_handler_count;
    unsigned int declared_host_flags;
    int load_priority;
    unsigned short config_category_count;
    unsigned short config_setting_count;
    DWORD source_volume_serial;
    DWORD source_file_index_high;
    DWORD source_file_index_low;
    DWORD source_size;
    unsigned char scope;
    unsigned char enabled_by_default;
    unsigned char valid;
    unsigned char duplicate_id;
    unsigned char source_is_workshop;
    unsigned char source_is_subscribed;
    unsigned char enabled;
    unsigned char has_enabled_override;
    unsigned char review_state;
    unsigned char is_new;
    unsigned char pending_review;
    unsigned char approval_valid;
    unsigned char enabled_requested;
    unsigned char loaded;
    unsigned char runtime_transition;
    unsigned char config_declared;
    unsigned char config_available;
    unsigned char manifest_present;
    unsigned char legacy_unsupported;
    unsigned char source_selected;
    unsigned char source_selection_missing;
} DlumaLuaMetadata;

typedef struct DlumaProjectSourceDescription
{
    WCHAR canonical_root[DLUMA_MAX_WPATH];
    char project_name[128];
    char published_id[64];
    char source_identity[512];
    unsigned char is_subscribed_workshop;
} DlumaProjectSourceDescription;

/* Loader-owned quest UI reads only these copied summaries.  They carry no
 * native object, QuestManager, phase, or localization pointer. */
typedef struct DlumaLuaQuestUiSnapshot
{
    unsigned int map_generation;
    unsigned int active;
    unsigned int state;
    unsigned int native_state;
    ULONGLONG owner_id;
    ULONGLONG revision;
    char script_id[DLUMA_LUA_QUEST_UI_MAX_SCRIPT_ID];
    char id[DLUMA_LUA_QUEST_UI_MAX_ID];
    char title[DLUMA_LUA_QUEST_UI_MAX_TITLE];
    char version[DLUMA_LUA_QUEST_UI_MAX_VERSION];
    char phase_id[DLUMA_LUA_QUEST_UI_MAX_ID];
    char phase_title[DLUMA_LUA_QUEST_UI_MAX_TITLE];
    char objective_summary[DLUMA_LUA_QUEST_UI_MAX_OBJECTIVE_SUMMARY];
} DlumaLuaQuestUiSnapshot;

typedef struct DlumaGlobalRuntimeCommand
{
    char registry_key[DLUMA_SHA256_HEX_CAPACITY];
    char metadata_fingerprint[DLUMA_SHA256_HEX_CAPACITY];
    ULONGLONG registry_revision;
    unsigned char type;
    unsigned char reserved[7];
} DlumaGlobalRuntimeCommand;

typedef struct DlumaGlobalUnseenBatch
{
    char batch_fingerprint[DLUMA_SHA256_HEX_CAPACITY];
    char first_name[128];
    unsigned int unseen_count;
    unsigned char should_prompt;
    unsigned char reserved[3];
    ULONGLONG registry_revision;
} DlumaGlobalUnseenBatch;

typedef struct DlumaGlobalMutationRequest
{
    char registry_key[DLUMA_SHA256_HEX_CAPACITY];
    char metadata_fingerprint[DLUMA_SHA256_HEX_CAPACITY];
    ULONGLONG registry_revision;
} DlumaGlobalMutationRequest;

/* An interrupted startup is not attributed to a MOD unless the loader was
 * inside that exact DLL MOD's bounded load/query/callback window. Recovery
 * always suppresses third-party MOD execution for the current process; a
 * later main-menu decision affects only a subsequent launch. */
typedef struct DlumaStartupRecoveryPrompt
{
    char suspected_native_id[128];
    char suspected_native_registry_key[DLUMA_SHA256_HEX_CAPACITY];
    char suspected_native_fingerprint[DLUMA_SHA256_HEX_CAPACITY];
    unsigned char pending;
    unsigned char suppressing_mods;
    unsigned char has_specific_native_suspect;
    unsigned char reserved;
} DlumaStartupRecoveryPrompt;

typedef struct DlumaLuaDiscoveryState
{
    DlumaLuaMetadata global_files[DLUMA_MAX_LUA_FILES];
    DlumaLuaMetadata local_files[DLUMA_MAX_LUA_FILES];
    unsigned int global_count;
    unsigned int local_count;
    unsigned int project_count;
    unsigned int map_count;
    unsigned int invalid_count;
    unsigned int duplicate_count;
    unsigned int global_invalid_count;
    unsigned int local_invalid_count;
    unsigned int global_duplicate_count;
    unsigned int local_duplicate_count;
    ULONGLONG global_registry_hash;
    ULONGLONG global_registry_revision;
    volatile LONG global_registry_lock;
    volatile LONG global_initial_discovery_complete;
    volatile LONG global_persistence_lock;
    volatile LONG global_runtime_queue_lock;
    DlumaGlobalRuntimeCommand global_runtime_commands[DLUMA_MAX_GLOBAL_RUNTIME_COMMANDS];
    unsigned int global_runtime_head;
    unsigned int global_runtime_tail;
    char current_unseen_batch_hash[DLUMA_SHA256_HEX_CAPACITY];
    char last_prompted_batch_hash[DLUMA_SHA256_HEX_CAPACITY];
    unsigned int current_unseen_count;
    DlumaLuaQuestPhaseProviderMetadata
        editor_phase_providers[DLUMA_MAX_LUA_FILES];
    unsigned int editor_phase_provider_count;
    unsigned char current_batch_should_prompt;
    unsigned char reserved[3];
} DlumaLuaDiscoveryState;

typedef struct DlumaEngineState
{
    HMODULE engine_module;
    void *create_game;
    void **game_global_slot;
    unsigned int game_interface_offset;
    void *(__fastcall *get_active_level)(void *game_interface);
    const char *(__fastcall *get_level_name)(void *level_interface);
    HWND(__fastcall *get_game_window)(void *game_interface);
    int(__fastcall *get_ui_screen_id_by_script_name)(void *level_interface, const void *script_name);
    int(__fastcall *register_ui_screen)(void *level_interface, const void *script_path,
                                        unsigned int screen_layer, int ui_pack_index,
                                        unsigned char on_demand, const void *skin_name,
                                        unsigned int user_flags);
    void *(__fastcall *load_ui_screen)(void *level_interface, int screen_id);
    void(__fastcall *show_ui_screen)(void *level_interface, int screen_id, void *ui_data);
    void(__fastcall *hide_ui_screen)(void *level_interface, int screen_id);
    unsigned char(__fastcall *is_ui_screen_visible)(void *level_interface, int screen_id);
    unsigned char(__fastcall *is_ui_screen_loaded)(void *level_interface, int screen_id);
    unsigned char(__fastcall *is_ui_screen_registered)(void *level_interface, int screen_id);
    void *(__fastcall *find_ui_screen)(void *level_interface, int screen_id);
    void *(__fastcall *find_ui_element)(void *ui_element, const void *element_id, unsigned char recursive);
    void *(__fastcall *get_owner_screen)(void *ui_element);
    int(__fastcall *get_screen_id)(void *ui_screen);
    const void *(__fastcall *get_screen_script_name)(void *ui_screen);
    void *last_level_interface;
    volatile void *tracked_level_interface;
    volatile LONG tracked_level_destroyed;
    void *level_destructor;
    void *level_destructor_vtable;
    BYTE level_destructor_original[12];
    HMODULE game_frame_module;
    void *game_frame_expected_primary_vtable;
    void *game_frame_expected_subobject_vtable;
    void **game_frame_vtable_slot;
    void *game_frame_original;
    volatile LONG game_frame_active_calls;
    volatile LONG game_frame_stopping;
    volatile LONG game_frame_authority_lock;
    volatile LONG game_frame_authority_conflict_pending;
    volatile LONG game_frame_callback_active_thread_id;
    volatile LONG game_frame_callback_active_depth;
    volatile LONG game_frame_loader_dispatch_active_thread_id;
    DlumaGameFrameAuthorityState game_frame_authority;
    volatile LONG game_frame_hook_call_count;
    volatile LONG startup_b0_onframe_pump_call_count;
    volatile LONG startup_b0_onframe_pump_skipped_count;
    volatile LONG startup_b0_onframe_last_skip_reason;
    volatile LONG startup_b0_active_level_query_call_count;
    volatile LONG startup_b0_active_level_query_success_count;
    volatile LONG startup_b0_active_level_query_null_count;
    volatile LONG startup_b0_active_level_query_invalid_count;
    unsigned int game_internal_offset;
    unsigned int active_level_holder_offset;
    unsigned int level_interface_offset;
    unsigned int level_internal_offset;
    unsigned int level_name_offset;
    DWORD image_timestamp;
    DWORD image_size;
    unsigned char ready;
    unsigned char level_destructor_hooked;
    unsigned char ui_exports_ready;
    unsigned char game_frame_hooked;
} DlumaEngineState;

typedef enum DlumaContentFinalizationAuthorityProof
{
    DLUMA_CONTENT_FINALIZATION_PROOF_NONE = 0,
    DLUMA_CONTENT_FINALIZATION_PROOF_SERIALIZED_FRAME_ROUTE,
    DLUMA_CONTENT_FINALIZATION_PROOF_WINDOW_OWNER,
    DLUMA_CONTENT_FINALIZATION_PROOF_NATURAL_SHUTDOWN_OWNER
} DlumaContentFinalizationAuthorityProof;

typedef struct DlumaNaturalShutdownOwnerProof
{
    unsigned int expected_exit_owner_calls;
    unsigned int observed_exit_owner_calls;
    LONG expected_thread_id;
    LONG observed_thread_id;
    ULONGLONG expected_token;
    ULONGLONG observed_token;
    unsigned int owner_conflict;
    unsigned int hook_owned;
    unsigned int exact;
} DlumaNaturalShutdownOwnerProof;

typedef struct DlumaContentFinalizationAuthorityEvidence
{
    DlumaContentFinalizationAuthorityProof selected_proof;
    LONG current_thread_id;

    LONG serialized_route_active_thread_id;
    LONG serialized_route_dispatch_thread_id;
    ULONGLONG serialized_route_token;
    unsigned int inside_serialized_route;
    unsigned int serialized_frontend_latched;
    unsigned int serialized_playtest_handoff_latched;
    unsigned int serialized_authority_frozen;
    unsigned int serialized_authority_conflict;

    LONG validated_window_owner_thread_id;
    LONG observed_window_owner_thread_id;
    unsigned int window_owner_conflict;

    DlumaNaturalShutdownOwnerProof natural_shutdown;
} DlumaContentFinalizationAuthorityEvidence;

typedef enum DlumaEngineLevelQueryResult
{
    DLUMA_ENGINE_LEVEL_QUERY_INVALID = 0,
    DLUMA_ENGINE_LEVEL_QUERY_SUCCESS,
    DLUMA_ENGINE_LEVEL_QUERY_NULL
} DlumaEngineLevelQueryResult;

typedef enum DlumaStartupB0OnFrameSkipReason
{
    DLUMA_STARTUP_B0_ONFRAME_SKIP_NONE = 0,
    DLUMA_STARTUP_B0_ONFRAME_SKIP_LOADER_NOT_STARTED,
    DLUMA_STARTUP_B0_ONFRAME_SKIP_SHUTDOWN_STOPPING,
    DLUMA_STARTUP_B0_ONFRAME_SKIP_THREAD_OWNERSHIP_VALIDATION_FAILED,
    DLUMA_STARTUP_B0_ONFRAME_SKIP_AUTHORITY_PENDING,
    DLUMA_STARTUP_B0_ONFRAME_SKIP_AUTHORITY_CONFLICT,
    DLUMA_STARTUP_B0_ONFRAME_SKIP_AUTHORITY_TIMEOUT,
    DLUMA_STARTUP_B0_ONFRAME_SKIP_AUTHORITY_OWNERSHIP_LOST,
    DLUMA_STARTUP_B0_ONFRAME_SKIP_RECURSIVE_CALLBACK,
    DLUMA_STARTUP_B0_ONFRAME_SKIP_INVALID_CALLBACK_ROUTE,
    DLUMA_STARTUP_B0_ONFRAME_SKIP_CONTENT_ISOLATION_DISABLED,
    DLUMA_STARTUP_B0_ONFRAME_SKIP_CONTENT_TRACKING_UNAVAILABLE,
    DLUMA_STARTUP_B0_ONFRAME_SKIP_STATE_BUSY,
    DLUMA_STARTUP_B0_ONFRAME_SKIP_COMPLETE_AUTHORIZED,
    DLUMA_STARTUP_B0_ONFRAME_SKIP_MISSED_BEFORE_FIRST_CUSTOM_EPOCH,
    DLUMA_STARTUP_B0_ONFRAME_SKIP_PERMANENT_FAILURE
} DlumaStartupB0OnFrameSkipReason;

typedef enum DlumaStartupB0PumpStatus
{
    DLUMA_STARTUP_B0_PUMP_REQUIRED = 0,
    DLUMA_STARTUP_B0_PUMP_DISABLED,
    DLUMA_STARTUP_B0_PUMP_UNAVAILABLE,
    DLUMA_STARTUP_B0_PUMP_STATE_BUSY,
    DLUMA_STARTUP_B0_PUMP_COMPLETE_AUTHORIZED,
    DLUMA_STARTUP_B0_PUMP_MISSED_BEFORE_FIRST_CUSTOM_EPOCH,
    DLUMA_STARTUP_B0_PUMP_PERMANENT_FAILURE
} DlumaStartupB0PumpStatus;

typedef enum DlumaCustomProjectAdmission
{
    DLUMA_CUSTOM_PROJECT_ADMISSION_ALLOWED = 0,
    DLUMA_CUSTOM_PROJECT_ADMISSION_DEFERRED,
    DLUMA_CUSTOM_PROJECT_ADMISSION_BLOCKED_RESTART_REQUIRED
} DlumaCustomProjectAdmission;

#define DLUMA_CUSTOM_PROJECT_B0_STATE_CAPACITY 64U
#define DLUMA_CUSTOM_PROJECT_ADMISSION_REASON_CAPACITY 512U

typedef struct DlumaCustomProjectAdmissionEvidence
{
    DlumaCustomProjectAdmission admission;
    unsigned int isolation_enabled;
    unsigned int startup_b0_complete_authorized;
    unsigned int restart_registry_can_admit;
    ULONGLONG startup_b0_generation;
    ULONGLONG frame_route_token;
    unsigned int startup_b0_permanent_source_failure;
    char startup_b0_state[DLUMA_CUSTOM_PROJECT_B0_STATE_CAPACITY];
    char reason[DLUMA_CUSTOM_PROJECT_ADMISSION_REASON_CAPACITY];
    char startup_b0_source_failure_reason[
        DLUMA_CUSTOM_PROJECT_ADMISSION_REASON_CAPACITY];
    char startup_b0_rejected_source[
        DLUMA_CUSTOM_PROJECT_ADMISSION_REASON_CAPACITY];
} DlumaCustomProjectAdmissionEvidence;

typedef struct DlumaGameThreadLevelSnapshot
{
    volatile LONG sequence;
    LONG publisher_thread;
    ULONGLONG qpc;
    ULONGLONG sample_ms;
    void *level_interface;
    ULONG_PTR level_vtable;
    int query_succeeded;
    int frontend;
    char normalized_level_name[DLUMA_MAX_MAP_NAME];
} DlumaGameThreadLevelSnapshot;

typedef enum DlumaProjectOwnerProofStatus
{
    DLUMA_PROJECT_OWNER_PROOF_UNAVAILABLE = 0,
    DLUMA_PROJECT_OWNER_PROOF_PENDING,
    DLUMA_PROJECT_OWNER_PROOF_READY,
    DLUMA_PROJECT_OWNER_PROOF_REJECTED
} DlumaProjectOwnerProofStatus;

typedef enum DlumaProjectOwnerProofReason
{
    DLUMA_PROJECT_OWNER_REASON_NONE = 0,
    DLUMA_PROJECT_OWNER_REASON_TRACKING_UNAVAILABLE,
    DLUMA_PROJECT_OWNER_REASON_NO_EVIDENCE,
    DLUMA_PROJECT_OWNER_REASON_MAP_MISMATCH,
    DLUMA_PROJECT_OWNER_REASON_TRANSITION_MISMATCH,
    DLUMA_PROJECT_OWNER_REASON_CONSUMED,
    DLUMA_PROJECT_OWNER_REASON_AMBIGUOUS_EVIDENCE,
    DLUMA_PROJECT_OWNER_REASON_NO_ACTIVE_EPOCH,
    DLUMA_PROJECT_OWNER_REASON_OWNERSHIP_INCOMPLETE,
    DLUMA_PROJECT_OWNER_REASON_SOURCE_INVALID,
    DLUMA_PROJECT_OWNER_REASON_INVALID_PATH,
    DLUMA_PROJECT_OWNER_REASON_LEDGER_BUSY
} DlumaProjectOwnerProofReason;

typedef struct DlumaProjectOwnerProofSnapshot
{
    unsigned int status;
    unsigned int reason;
    unsigned int epoch;
    unsigned int source_record_index;
    unsigned int source_add_ordinal;
    ULONGLONG transition_generation;
    char map_name[DLUMA_MAX_MAP_NAME];
    WCHAR project_root[DLUMA_MAX_WPATH];
} DlumaProjectOwnerProofSnapshot;

typedef struct DlumaLoaderConfig
{
    DWORD poll_interval_ms;
    DWORD universal_screen_poll_interval_ms;
    DWORD unmatched_level_grace_ms;
    unsigned char enable_key_events;
    unsigned char log_timer_events;
    unsigned char auto_enable_mods;
    unsigned char discord_rich_presence_enabled;
    unsigned char editor_discord_rich_presence_enabled;
    unsigned char steam_rich_presence_enabled;
    /* Startup-only: enables the exact supported Player developer profile,
     * retains the loader-owned debugconf source for explicit command-line
     * selection, and exposes the Lua limits settings category. */
    unsigned char developer_mode;
    /* Local-only, process-private agent diagnostics. Startup defaults are
     * disabled; StateId selects one per-developer protected key namespace. */
    unsigned char analysis_bridge_enabled;
    unsigned char analysis_bridge_allow_actions;
    /* Read into a larger buffer so an overlong INI value is rejected instead
     * of being silently truncated into a different valid 16-byte StateId. */
    char analysis_bridge_state_id[65];
    /* Hidden, startup-only diagnostic. It exercises only an approved
     * PlayerMedia companion manifest and never accepts an arbitrary path or
     * cue name. */
    unsigned char crash_dialog_enabled;
    unsigned char crash_random_titles;
    unsigned char custom_map_quest_level_name_crash_enabled;
    unsigned char custom_map_campaign_credits_exit_crash_enabled;
    unsigned char main_menu_credits_music_enabled;
    unsigned char custom_map_asset_isolation_enabled;
    unsigned char menu_mesh_name_guard_enabled;
    unsigned char hide_invalid_workshop_projects_enabled;
    unsigned char custom_map_workshop_loading_screen_enabled;
    unsigned char custom_map_loading_screen_content_enabled;
    unsigned char mouse_window_lock_enabled;
    char custom_map_asset_isolation_watch_scr
        [DLUMA_ASSET_TRACE_MAX_WATCH_PATHS]
        [DLUMA_ASSET_TRACE_WATCH_PATH_CAPACITY];
    unsigned int custom_map_asset_isolation_watch_scr_count;
    char custom_map_asset_isolation_watch_asset
        [DLUMA_ASSET_TRACE_MAX_WATCH_PATHS]
        [DLUMA_ASSET_TRACE_WATCH_PATH_CAPACITY];
    unsigned int custom_map_asset_isolation_watch_asset_count;
    ULONGLONG lua_memory_limit_bytes;
    ULONGLONG lua_instruction_limit;
    DWORD lua_wall_clock_limit_ms;
    DWORD lua_max_callbacks;
    DWORD lua_max_timers;
    DWORD lua_max_rules;
    DWORD lua_log_messages_per_second;
    DWORD lua_local_permission_policy;
    WCHAR configured_roots[8][DLUMA_MAX_WPATH];
    unsigned int configured_root_count;
} DlumaLoaderConfig;

typedef struct DlumaRuntime
{
    HMODULE self_module;
    volatile LONG started;
    volatile LONG stop_requested;
    volatile LONG lifecycle_state;
    volatile LONG retained_for_process_exit;
    HANDLE worker_thread;
    HANDLE stop_event;
    UINT_PTR ui_timer_id;
    WCHAR module_dir[DLUMA_MAX_WPATH];
    WCHAR log_path[DLUMA_MAX_WPATH];
    WCHAR marker_root[DLUMA_MAX_WPATH];
    WCHAR config_path[DLUMA_MAX_WPATH];
    DlumaLoaderConfig config;
    DlumaEngineState engine;
    WCHAR workshop_roots[DLUMA_MAX_ROOTS][DLUMA_MAX_WPATH];
    unsigned int workshop_root_count;
    WCHAR active_project_root[DLUMA_MAX_WPATH];
    char active_map[DLUMA_MAX_MAP_NAME];
    ULONGLONG map_loaded_ms;
    ULONGLONG last_f8_ms;
    ULONGLONG first_unmatched_ms;
    DlumaScriptSet scripts;
    DlumaLuaDiscoveryState lua_discovery;
    char last_observed_map[DLUMA_MAX_MAP_NAME];
    char last_unowned_map[DLUMA_MAX_MAP_NAME];
    unsigned int active_project_owner_epoch;
    ULONGLONG active_project_owner_transition_generation;
    DlumaGameThreadLevelSnapshot game_thread_level_snapshot;
} DlumaRuntime;

typedef struct DlumaWorkshopMenuItem
{
    WCHAR project_root[DLUMA_MAX_WPATH];
    WCHAR preview_path[DLUMA_MAX_WPATH];
    char title[DLUMA_WORKSHOP_MENU_TITLE_CAPACITY];
    char description[DLUMA_WORKSHOP_MENU_DESCRIPTION_CAPACITY];
    char workshop_id[DLUMA_WORKSHOP_MENU_ID_CAPACITY];
    unsigned char subscribed;
    unsigned char exact_project;
    unsigned char has_data_maps;
    unsigned char preview_available;
    unsigned char title_from_desc;
    unsigned char description_from_desc;
} DlumaWorkshopMenuItem;

typedef struct DlumaWorkshopMenuRemoteItem
{
    WCHAR preview_path[DLUMA_MAX_WPATH];
    char title[DLUMA_WORKSHOP_MENU_TITLE_CAPACITY];
    char description[DLUMA_WORKSHOP_STEAM_DESCRIPTION_CAPACITY];
    char author[DLUMA_WORKSHOP_MENU_AUTHOR_CAPACITY];
    char workshop_id[DLUMA_WORKSHOP_MENU_ID_CAPACITY];
    /* Original Steam-hosted preview URL.  This is copied for consumers such
     * as Discord Rich Presence; local DDS cache paths never leave the loader. */
    char preview_url[1024];
    unsigned int updated_time;
    unsigned char details_ready;
    unsigned char author_ready;
    unsigned char preview_ready;
    unsigned char failed;
} DlumaWorkshopMenuRemoteItem;

typedef struct DlumaWorkshopLoadingMetadata
{
    WCHAR project_root[DLUMA_MAX_WPATH];
    WCHAR preview_path[DLUMA_MAX_WPATH];
    char title[DLUMA_WORKSHOP_MENU_TITLE_CAPACITY];
    char description[DLUMA_WORKSHOP_STEAM_DESCRIPTION_CAPACITY];
    char author[DLUMA_WORKSHOP_MENU_AUTHOR_CAPACITY];
    char workshop_id[DLUMA_WORKSHOP_MENU_ID_CAPACITY];
    unsigned char exact_project;
    unsigned char published;
    unsigned char details_ready;
    unsigned char title_authoritative;
    unsigned char description_authoritative;
    unsigned char author_ready;
    unsigned char preview_ready;
    unsigned char local_project;
} DlumaWorkshopLoadingMetadata;

extern DlumaRuntime g_dluma;
void __cdecl dluma_security_init_cookie(void);

/* no-CRT helpers. C++ translation units get these standard declarations from
 * the MSVC headers pulled in by std::vector; the C definitions still satisfy
 * the linker without importing the CRT. */
#ifndef __cplusplus
void *memcpy(void *dst, const void *src, SIZE_T count);
void *memset(void *dst, int value, SIZE_T count);
int memcmp(const void *a, const void *b, SIZE_T count);
SIZE_T strlen(const char *text);
SIZE_T wcslen(const WCHAR *text);
#endif

char dluma_ascii_lower(char c);
WCHAR dluma_wascii_lower(WCHAR c);
int dluma_stricmp(const char *a, const char *b);
int dluma_wstricmp(const WCHAR *a, const WCHAR *b);
int dluma_contains_icase(const char *haystack, const char *needle);
int dluma_wends_with_icase(const WCHAR *value, const WCHAR *suffix);
int dluma_is_space(char c);
int dluma_is_digit(char c);
unsigned int dluma_parse_u32(const char *text, unsigned int fallback);
unsigned int dluma_u64_to_text(ULONGLONG value, char *out, unsigned int capacity);
unsigned int dluma_u32_hex(DWORD value, char *out, unsigned int capacity);
void dluma_str_copy(char *dst, unsigned int cap, const char *src);
void dluma_wstr_copy(WCHAR *dst, unsigned int cap, const WCHAR *src);
void dluma_str_append(char *dst, unsigned int cap, const char *src);
void dluma_wstr_append(WCHAR *dst, unsigned int cap, const WCHAR *src);
void dluma_path_dirname(WCHAR *path);
void dluma_path_join(WCHAR *out, unsigned int cap, const WCHAR *left, const WCHAR *right);
void dluma_path_parent(WCHAR *path);
int dluma_path_exists(const WCHAR *path);
int dluma_dir_exists(const WCHAR *path);
int dluma_read_file_utf8(const WCHAR *path, char **out_data, DWORD *out_size);
void dluma_free(void *memory);
void *dluma_alloc(SIZE_T bytes);
int dluma_utf8_to_wide(const char *source, WCHAR *target, unsigned int target_count);
int dluma_wide_to_utf8(const WCHAR *source, char *target, unsigned int target_count);
int dluma_is_readable(const void *address, SIZE_T bytes);
void dluma_sanitize_filename(char *text);
int dluma_extract_quoted(const char *line, unsigned int *cursor, char *out, unsigned int out_cap);
const char *dluma_skip_spaces(const char *p);
void dluma_trim_in_place(char *text);
int dluma_mkdirs(const WCHAR *path);
int dluma_write_utf8_file(const WCHAR *path, const char *text, int append);
int dluma_wstarts_with_icase(const WCHAR *value, const WCHAR *prefix);
int dluma_wcontains_icase(const WCHAR *value, const WCHAR *needle);
void dluma_normalize_map_name(const char *source, char *out, unsigned int cap);

/* Internal deterministic SHA-256. */
void dluma_sha256_init(DlumaSha256Context *context);
void dluma_sha256_update(DlumaSha256Context *context, const void *data, SIZE_T size);
void dluma_sha256_final(DlumaSha256Context *context, BYTE digest[32]);
void dluma_sha256_hex(const BYTE digest[32], char out[DLUMA_SHA256_HEX_CAPACITY]);
void dluma_sha256_bytes_hex(const void *data, SIZE_T size,
                          char out[DLUMA_SHA256_HEX_CAPACITY]);
void dluma_sha256_fields_hex(const char *schema,
                           const char *const *fields,
                           const unsigned int *lengths,
                           unsigned int field_count,
                           char out[DLUMA_SHA256_HEX_CAPACITY]);
int dluma_sha256_file_hex(const WCHAR *path, ULONGLONG max_bytes,
                        char out[DLUMA_SHA256_HEX_CAPACITY]);
int dluma_sha256_handle_hex(
    HANDLE file, ULONGLONG max_bytes,
    char out[DLUMA_SHA256_HEX_CAPACITY]);

/* logging */
void dluma_log_line(const char *level, const char *message);
void dluma_log_pair(const char *level, const char *label, const char *value);
void dluma_log_u64(const char *level, const char *label, ULONGLONG value);
void dluma_log_hex(const char *level, const char *label, DWORD value);
int dluma_chrome_log_notice(void);

/* engine / project / script host */
int dluma_configure_runtime_paths(void);
void dluma_load_config(void);
int dluma_mount_content_pack(void);
void dluma_unmount_content_pack(void);
int dluma_content_pack_snapshot(char *path_out, unsigned int path_capacity);
int dluma_content_pack_mount_snapshot(
    DlumaContentPackMountSnapshot *snapshot_out);
int dluma_content_pack_bind_source_identity(
    ULONGLONG exact_source, ULONGLONG exact_link,
    DWORD source_list_ordinal, ULONGLONG source_identity);
int dluma_developer_mode_apply_startup_profile(HMODULE gamedll);
int dluma_workshop_preview_cache_source_snapshot(
    char *path_out, unsigned int path_capacity);
int dluma_developer_mode_source_snapshot(
    char *path_out, unsigned int path_capacity);
int dluma_custom_main_menu_source_snapshot_for_owner(
    const char *owner_id,
    char *path_out, unsigned int path_capacity,
    char *fingerprint_out, unsigned int fingerprint_capacity);
int dluma_custom_main_menu_selected_bank_source_snapshot(
    char *path_out, unsigned int path_capacity,
    char *fingerprint_out, unsigned int fingerprint_capacity);
unsigned int dluma_custom_main_menu_selected_bank_count(void);
int dluma_custom_main_menu_selected_bank_source_snapshot_at(
    unsigned int index,
    char *path_out, unsigned int path_capacity,
    char *fingerprint_out, unsigned int fingerprint_capacity);
LONG dluma_workshop_preview_cache_note_dds_published(void);
int dluma_workshop_preview_cache_refresh_pending_on_game_thread(void);
int dluma_workshop_preview_cache_refresh_for_visibility_retry(void);
int dluma_engine_bind(DlumaEngineState *state);
int dluma_engine_prepare_game_locator(DlumaEngineState *state);
int dluma_engine_query_level(DlumaEngineState *state, char *map_out, unsigned int map_cap);
DlumaEngineLevelQueryResult dluma_engine_query_level_identity(
    DlumaEngineState *state, char *map_out, unsigned int map_cap,
    void **level_out, ULONG_PTR *level_vtable_out);
int dluma_engine_query_session_level_identity(
    DlumaEngineState *state, char *map_out, unsigned int map_cap,
    void **level_out, ULONG_PTR *level_vtable_out);
/* Read-only startup observation; caller must own the exact serialized frame. */
int dluma_engine_query_startup_session_level_identity(
    DlumaEngineState *state, void *game_subobject, unsigned int route_proof,
    void *observed_active_level, char *map_out, unsigned int map_cap,
    void **level_out, ULONG_PTR *level_vtable_out, void **player_out);
DlumaEngineLevelQueryResult dluma_engine_readonly_frontend_probe(
    DlumaEngineState *state, DlumaGameFrameFrontendProbe *probe_out);
int dluma_engine_copy_fresh_latched_frontend(
    DlumaEngineState *state,
    ULONGLONG now_ms,
    char *map_out,
    unsigned int map_capacity,
    void **level_out,
    ULONG_PTR *level_vtable_out);
LONG dluma_engine_game_frame_current_dispatch_thread_id(
    const DlumaEngineState *state);
void *dluma_engine_game_frame_route_subobject(
    const DlumaEngineState *state);
ULONGLONG dluma_engine_game_frame_route_token(
    const DlumaEngineState *state);
int dluma_engine_game_frame_dispatch_active_on_current_thread(
    const DlumaEngineState *state);
int dluma_engine_game_frame_hook_owned(const DlumaEngineState *state);
int dluma_engine_configure_playtest_frame_authority(
    DlumaEngineState *state, const char *expected_level_name);
LONG dluma_engine_game_frame_window_owner_thread_id(
    const DlumaEngineState *state);
DlumaGameFrameAuthorityStatus dluma_engine_game_frame_authority_status(
    DlumaEngineState *state, ULONGLONG now_ms);
int dluma_engine_snapshot_game_frame_authority(
    DlumaEngineState *state,
    DlumaGameFrameAuthorityState *snapshot_out);
void dluma_engine_note_game_frame_message_timer_probe(
    DlumaEngineState *state,
    LONG thread_id,
    const DlumaGameFrameFrontendProbe *probe);
void dluma_engine_note_custom_project_admitted(
    DlumaEngineState *state);
void dluma_engine_note_registry_mutation_started(
    DlumaEngineState *state);
int dluma_engine_current_thread_can_finalize_content(
    DlumaEngineState *state, LONG validated_window_owner_thread_id);
int dluma_engine_current_thread_can_finalize_content_ex(
    DlumaEngineState *state,
    LONG validated_window_owner_thread_id,
    const DlumaNaturalShutdownOwnerProof *natural_shutdown,
    DlumaContentFinalizationAuthorityEvidence *evidence_out);
const char *dluma_content_finalization_authority_proof_name(
    DlumaContentFinalizationAuthorityProof proof);
void dluma_engine_note_startup_b0_onframe_skip(
    DlumaEngineState *state, DlumaStartupB0OnFrameSkipReason reason);
const char *dluma_startup_b0_onframe_skip_reason_name(
    DlumaStartupB0OnFrameSkipReason reason);
void dluma_engine_track_last_level(DlumaEngineState *state);
void dluma_engine_clear_tracked_level(DlumaEngineState *state);
int dluma_engine_take_tracked_level_destroyed(DlumaEngineState *state);
int dluma_engine_unbind(DlumaEngineState *state);
void dluma_engine_quiesce(DlumaEngineState *state);
void *dluma_engine_get_game_interface(DlumaEngineState *state);
void *dluma_engine_get_game_interface_for_localization(DlumaEngineState *state);
HWND dluma_engine_get_window(DlumaEngineState *state);

/* Game-native localization.  All text returned by these helpers is copied
 * into caller-owned storage before the engine-owned string is released. */
int dluma_localization_bind(void);
int dluma_localization_load_bootstrap(void);
int dluma_localization_load_initial(void);
void dluma_localization_game_thread_tick(void);
void dluma_localization_quiesce(void);
void dluma_localization_shutdown(void);
const char *dluma_localization_current_locale(void);
ULONGLONG dluma_localization_generation(void);
int dluma_text_get_utf8(const char *key, const char *english_fallback,
                      char *out, unsigned int capacity);
int dluma_text_get_wide(const char *key, const WCHAR *english_fallback,
                      WCHAR *out, unsigned int capacity);
int dluma_ui_prepare_main_menu_redirect(void);
void dluma_ui_restore_main_menu_redirect(void);
int dluma_ui_main_menu_redirect_is_applied(void);
int dluma_ui_prepare_workshop_menu_redirect(void);
void dluma_ui_restore_workshop_menu_redirect(void);
int dluma_ui_workshop_menu_redirect_is_applied(void);
int dluma_ui_start(void);
void dluma_ui_stop(void);
int dluma_ui_stop_succeeded(void);
void dluma_ui_quiesce(void);
void dluma_ui_quiesce_from_exit_game_owner(
    unsigned int observed_exit_owner_calls);
void dluma_ui_game_thread_tick(
    void *game_subobject, ULONGLONG callback_sequence);
/*
 * Discord observes frontend activity through already-instantiated UI roots.
 * These copied visibility bits never leave the main module and expose no
 * engine pointer to the private RPC helper.
 */
#define DLUMA_UI_MENU_VISIBLE_SELECTING_MAP 0x01U
#define DLUMA_UI_MENU_VISIBLE_SETTINGS 0x02U
#define DLUMA_UI_MENU_VISIBLE_MODS 0x04U
#define DLUMA_UI_MENU_VISIBLE_CREDITS 0x08U
#define DLUMA_UI_MENU_VISIBLE_STATISTICS 0x10U
void dluma_ui_note_live_frontend_screen(void *screen);
unsigned int dluma_ui_discord_menu_visibility_snapshot(void);
int dluma_project_launch_note_custom_source(
    DlumaProjectMountSourceKind source_kind,
    const char *normalized_project_root,
    unsigned int epoch);
int dluma_project_launch_note_custom_source_ex(
    DlumaProjectMountSourceKind source_kind,
    const char *normalized_project_root,
    unsigned int epoch,
    const DlumaProjectSourceBypassEvidence *evidence);
int dluma_project_launch_note_update_dlc_content(
    DlumaProjectLaunchOwner owner,
    DWORD caller_return_rva,
    int entering);
int dluma_project_launch_attest_change_after_fade(
    const DlumaProjectChangeAfterFadeAttestation *attestation);
int dluma_project_launch_note_quarantined_cleanup_result(
    unsigned int epoch, int cleanup_verified);
void dluma_project_launch_block(const char *reason);
int dluma_custom_map_cancelled_presentation_cleanup(
    const DlumaProjectCancelledCleanupPlan *plan,
    DlumaProjectCancelledCleanupResult *result);
int dluma_project_launch_snapshot_admission(
    DlumaProjectLaunchAdmissionState *snapshot_out);
void dluma_ui_report_error(const char *message);
void dluma_ui_get_main_menu_mod_counts(
    unsigned int *active_out, unsigned int *installed_out,
    unsigned int *errors_out);
void dluma_custom_map_asset_trace_game_thread_drain(void);
int dluma_custom_map_asset_trace_game_thread_finalize(void);
int dluma_custom_map_asset_trace_game_thread_finalize_from_exit_game_owner(
    unsigned int observed_exit_owner_calls);
void dluma_custom_map_asset_trace_record_premount_decision(
    unsigned int allowed,
    ULONGLONG token_generation,
    unsigned int admission_result,
    const char *failure_reason);
void dluma_custom_map_asset_trace_record_source_binding(
    unsigned int source_kind,
    ULONGLONG token_generation,
    unsigned int epoch,
    unsigned int accepted,
    const char *failure_reason);
const char *dluma_custom_map_asset_trace_status_text(void);
#define DLUMA_CUSTOM_MAP_TICK_SOURCE_ON_FRAME 1U
#define DLUMA_CUSTOM_MAP_TICK_SOURCE_FALLBACK_TIMER 2U
int dluma_custom_map_content_frontend_restore_allowed(void);
int dluma_custom_map_asset_mods_project_boundary(int after_mount);
void dluma_custom_map_content_game_thread_tick(
    const char *map_name, void *level, ULONG_PTR level_vtable,
    ULONGLONG now_ms,
    unsigned int tick_source, int query_succeeded);
void dluma_custom_map_asset_isolation_note_premount_original(void);
DlumaStartupB0PumpStatus dluma_custom_map_startup_b0_pump_status(void);
int dluma_custom_map_startup_b0_frontend_identity_latched(void);
void dluma_custom_map_startup_b0_note_authority_failure(
    unsigned int conflict_reason);
DlumaCustomProjectAdmission dluma_custom_map_project_admission(
    char *reason_out, unsigned int reason_capacity);
int dluma_custom_map_project_admission_evidence(
    DlumaCustomProjectAdmissionEvidence *evidence_out);
void dluma_custom_map_asset_trace_active_level_observed(
    const char *map_name, void *level, const char *project_root,
    ULONGLONG worker_timestamp_ms);
DlumaProjectOwnerProofStatus
dluma_custom_map_exact_project_owner_snapshot(
    const char *map_name,
    DlumaProjectOwnerProofSnapshot *snapshot_out);
int dluma_custom_map_exact_project_owner_consume(
    const DlumaProjectOwnerProofSnapshot *snapshot);
/* Sticky, allocation-free startup-B0 closure signal.  Local discovery may
 * later clear its published list when returning to the frontend, so the
 * startup owner must be told when project/map Lua first becomes active. */
void dluma_custom_map_asset_isolation_note_local_lua_active(void);
int dluma_ui_read_level_snapshot(char *map_out, unsigned int map_capacity,
                               void **level_out, ULONGLONG *sample_ms_out);
int dluma_read_game_thread_level_snapshot(
    DlumaGameThreadLevelSnapshot *snapshot_out);
void dluma_discover_workshop_roots(void);
int dluma_workshop_menu_parse_desc_text(
    const char *text, DWORD size,
    char *title_out, unsigned int title_capacity,
    char *description_out, unsigned int description_capacity,
    char *id_out, unsigned int id_capacity);
void dluma_workshop_menu_refresh_metadata(void);
int dluma_workshop_menu_find_metadata(
    const char *workshop_id, const char *native_title, int subscribed,
    DlumaWorkshopMenuItem *out);
int dluma_workshop_menu_find_metadata_for_project(
    const WCHAR *project_root, DlumaWorkshopMenuItem *out);
void dluma_workshop_menu_request_remote(const char *workshop_id);
int dluma_workshop_menu_get_remote(
    const char *workshop_id, DlumaWorkshopMenuRemoteItem *out);
int dluma_workshop_menu_get_loading_metadata(
    const WCHAR *project_root, const char *workshop_id,
    DlumaWorkshopLoadingMetadata *out);
void dluma_workshop_menu_game_thread_tick(void);
ULONGLONG dluma_workshop_menu_presentation_revision(void);
void dluma_workshop_menu_worker_tick(void);
void dluma_workshop_menu_shutdown(void);
int dluma_workshop_preview_prepare_dds(
    const WCHAR *source_path, const WCHAR *output_path);
int dluma_workshop_preview_validate_dds(const WCHAR *path);
int dluma_workshop_preview_read_dds_top_mip_bgra(
    const WCHAR *dds_path, BYTE **pixels_out,
    unsigned int *width_out, unsigned int *height_out,
    unsigned int *stride_out);
int dluma_workshop_menu_request_preview_pixels(
    ULONGLONG asset_key, const WCHAR *dds_path);
int dluma_workshop_menu_take_preview_pixels(
    ULONGLONG asset_key, const WCHAR *dds_path,
    BYTE **pixels_out, unsigned int *width_out,
    unsigned int *height_out, unsigned int *stride_out);
int dluma_find_project_for_map(
    const char *map_name, WCHAR *project_out,
    unsigned int project_cap,
    DlumaProjectOwnerProofSnapshot *proof_out,
    unsigned int active_owner_epoch,
    ULONGLONG active_owner_transition_generation);
int dluma_project_roots_same_identity(
    const WCHAR *first, const WCHAR *second);
int dluma_project_candidate_root_is_contained(
    const WCHAR *discovery_root, const WCHAR *candidate);
int dluma_project_map_anchor_is_contained(
    const WCHAR *project_root, const char *map_name);
void dluma_describe_project_source(const WCHAR *project_root,
                                 char *name_out, unsigned int name_capacity,
                                 char *id_out, unsigned int id_capacity,
                                 unsigned char *is_workshop_out);
int dluma_classify_project_source(
    const WCHAR *project_root, DlumaProjectSourceDescription *description);
int dluma_mod_source_selection_read(
    const char *kind, const char *declared_id,
    char *source_identity_out, unsigned int capacity,
    int *explicit_selection_out);
int dluma_mod_source_selection_write(
    const char *kind, const char *declared_id,
    const char *source_identity);
unsigned int dluma_collect_script_paths(const WCHAR *project_root, const char *map_name, WCHAR paths[][DLUMA_MAX_WPATH], unsigned int max_paths);
void dluma_lua_discover_globals(void);
void dluma_lua_discover_editor_quest_phase_metadata(void);
unsigned int dluma_lua_snapshot_editor_quest_phase_providers(
    DlumaLuaQuestPhaseProviderMetadata *out,
    unsigned int capacity);
void dluma_lua_registry_lock(void);
void dluma_lua_registry_unlock(void);
void dluma_lua_rehash_globals_locked(void);
unsigned int dluma_lua_snapshot_globals(DlumaLuaMetadata *out, unsigned int capacity, ULONGLONG *hash_out);
unsigned int dluma_lua_snapshot_globals_with_revision(
    DlumaLuaMetadata *out,
    unsigned int capacity,
    ULONGLONG *hash_out,
    ULONGLONG *revision_out);
void dluma_lua_get_global_registry_ini_path(WCHAR *out, unsigned int capacity);
int dluma_lua_set_global_enabled(const char *id, int enabled);
int dluma_lua_initial_global_discovery_complete(void);
void dluma_lua_auto_enable_startup(DlumaLuaMetadata *item);
int dluma_lua_disable_all_globals_next_launch(void);
int dluma_lua_prepare_global_identity(DlumaLuaMetadata *item);
int dluma_lua_prepare_local_identity(DlumaLuaMetadata *item);
void dluma_lua_classify_global_review(DlumaLuaMetadata *item,
                                    const DlumaLuaMetadata *previous);
void dluma_lua_recompute_unseen_batch_locked(void);
int dluma_lua_snapshot_unseen_batch(DlumaGlobalUnseenBatch *out);
int dluma_lua_mark_unseen_batch_prompted(const char *expected_batch,
                                       ULONGLONG expected_revision);
int dluma_lua_mark_global_seen(const DlumaGlobalMutationRequest *request);
int dluma_lua_request_global_enabled(const DlumaGlobalMutationRequest *request,
                                   int enabled, int approve_current);
int dluma_lua_request_global_source(
    const char *declared_id, const char *source_identity,
    ULONGLONG registry_revision);
int dluma_lua_permission_is_sensitive(const DlumaLuaMetadata *item);
int dluma_lua_permission_is_unsupported(const DlumaLuaMetadata *item);
void dluma_lua_process_global_runtime_queue(void);
void dluma_lua_update_global_runtime_status(
    const char *registry_key,
    const char *metadata_fingerprint,
    int loaded,
    const char *error_text);
unsigned int dluma_lua_discover_locals(const WCHAR *project_root, const char *map_name);
void dluma_lua_clear_locals(void);
int dluma_lua_runtime_start(void);
int dluma_lua_runtime_is_available(void);
void dluma_lua_runtime_append_info_text(char *text, unsigned int capacity);
void dluma_lua_runtime_publish_globals(
    const DlumaLuaMetadata *entries,
    unsigned int count,
    ULONGLONG revision);
void dluma_lua_runtime_publish_context(
    const WCHAR *project_root,
    const char *project_name,
    const char *workshop_id,
    const char *map_name,
    const DlumaLuaMetadata *entries,
    unsigned int count);
int dluma_lua_runtime_enqueue_event(
    unsigned int kind,
    unsigned int map_generation,
    ULONGLONG native_index_epoch,
    ULONGLONG native_subscription_key_revision,
    const char *exact_name,
    int state,
    double number);
int dluma_lua_runtime_enqueue_config_changed(
    const char *registry_key,
    const char *metadata_fingerprint,
    const char *setting_key,
    const char *old_value,
    unsigned int old_value_type,
    const char *new_value,
    unsigned int new_value_type);
unsigned int dluma_lua_runtime_snapshot_quests(
    DlumaLuaQuestUiSnapshot *out,
    unsigned int capacity,
    unsigned int *total,
    ULONGLONG *revision,
    int *archived);
int dluma_lua_runtime_copy_crash_context(
    char *out, unsigned int capacity);
void dluma_lua_runtime_game_thread_pump(void);
void dluma_lua_runtime_stop(void);
int dluma_lua_runtime_stop_succeeded(void);
int dluma_scripts_load(DlumaScriptSet *set, const WCHAR *project_root, const char *map_name);
void dluma_scripts_fire(DlumaScriptSet *set, DlumaEventType type, const char *target);
void dluma_scripts_tick(DlumaScriptSet *set, ULONGLONG now_ms);
void dluma_scripts_clear(DlumaScriptSet *set);
DWORD WINAPI dluma_worker_thread(LPVOID parameter);
int dluma_host_start(void);
int dluma_host_stop(void);
int dluma_wait_for_stop(DWORD timeout_ms);

/* orderly exit interception / two-phase lifecycle */
void dluma_mouse_lock_start(int enabled);
void dluma_mouse_lock_bind_window(HWND window);
void dluma_mouse_lock_window_message_pre(
    HWND window, unsigned int message,
    WPARAM wparam, LPARAM lparam);
void dluma_mouse_lock_window_message_post(
    HWND window, unsigned int message,
    WPARAM wparam, LPARAM lparam);
void dluma_mouse_lock_post_game_frame(void);
void dluma_mouse_lock_quiesce(void);
void dluma_mouse_lock_reset(void);
int dluma_shutdown_hooks_start(void);
void dluma_shutdown_try_bind_window(void);
int dluma_shutdown_schedule_orderly_close(const char *reason);
int dluma_shutdown_schedule_orderly_close_ex(
    DlumaOrderlyCloseOrigin origin,
    const char *reason,
    unsigned long long epoch);
int dluma_shutdown_snapshot_orderly_close(
    DlumaOrderlyCloseSnapshot *snapshot_out);
int dluma_shutdown_hooks_stop(void);
int dluma_shutdown_snapshot_natural_exit_owner(
    unsigned int expected_exit_owner_calls,
    DlumaNaturalShutdownOwnerProof *proof_out);
void dluma_request_quiesce(const char *reason);

/* existing Chrome dump-pipeline wrapper */
int dluma_crash_reporting_start(void);
int dluma_crash_reporting_start_editor_filter(void);
int dluma_crash_reporting_stop(void);
void dluma_crash_reporting_refresh(void);
void dluma_crash_publish_runtime_context(
    const char *phase,
    const WCHAR *project_root,
    const char *map_name,
    unsigned int owner_epoch,
    ULONGLONG owner_generation);
void dluma_crash_publish_host_context(
    const char *host_kind,
    int exact_profile,
    const char *classification_reason,
    const char *session_phase,
    ULONGLONG current_generation,
    ULONGLONG last_generation);
void dluma_crash_localization_refresh(void);

/* Armed before native or Lua MOD code can execute; consumed only by the
 * validated game-thread main-menu prompt. */
void dluma_startup_recovery_begin(void);
int dluma_startup_recovery_suppresses_mods(void);
void dluma_startup_recovery_note_native_mod(
    const char *id,
    const char *registry_key,
    const char *approval_fingerprint,
    const char *phase);
void dluma_startup_recovery_note_native_failure(
    const char *id,
    const char *registry_key,
    const char *approval_fingerprint);
void dluma_startup_recovery_clear_native_mod(void);
int dluma_startup_recovery_snapshot_prompt(DlumaStartupRecoveryPrompt *out);
int dluma_startup_recovery_prompt_before_game_init(void);
int dluma_startup_recovery_complete_prompt(void);

#endif
