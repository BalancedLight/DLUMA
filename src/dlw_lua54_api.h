#ifndef DLUMA_LUA54_API_H
#define DLUMA_LUA54_API_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DLUMA_LUA54_ABI_VERSION 0x00040000u
#define DLUMA_LUA54_EXPECTED_RELEASE "Lua 5.4.8"

#define DLUMA_LUA54_MAX_ERROR 1024u
#define DLUMA_LUA54_MAX_EVENT 160u
#define DLUMA_LUA54_MAX_OBJECT_NAME 96u
#define DLUMA_LUA54_MAX_CLASS_NAME 96u
#define DLUMA_LUA54_MAX_BULK_OBJECTS 32u
#define DLUMA_LUA54_MAX_SUBTITLE_TEXT 1024u
#define DLUMA_LUA54_MAX_DIALOG_NAME 96u
#define DLUMA_LUA54_MAX_SCRIPT_ID 128u
#define DLUMA_LUA54_MAX_QUEST_ID 128u
#define DLUMA_LUA54_MAX_QUEST_TITLE 160u
#define DLUMA_LUA54_MAX_QUEST_VERSION 64u
#define DLUMA_LUA54_MAX_QUEST_OBJECTIVE_SUMMARY 1024u
#define DLUMA_LUA54_MAX_QUEST_HUD_TARGET_ID 96u
#define DLUMA_LUA54_MAX_QUEST_HUD_TEXT 1024u
#define DLUMA_LUA54_MAX_QUEST_HUD_OVERRIDE_TEXT 512u
#define DLUMA_LUA54_MAX_QUEST_HUD_OBJECTIVES 4u
#define DLUMA_LUA54_MAX_CONFIG_KEY 64u
#define DLUMA_LUA54_MAX_CONFIG_VALUE 512u

enum DlumaLua54Scope {
    DLUMA_LUA54_SCOPE_GLOBAL = 1,
    DLUMA_LUA54_SCOPE_PROJECT = 2,
    DLUMA_LUA54_SCOPE_MAP = 3
};

enum DlumaLua54Permission {
    DLUMA_LUA54_PERMISSION_GAME_READ = 1u << 0,
    DLUMA_LUA54_PERMISSION_PLAYER_READ = 1u << 1,
    DLUMA_LUA54_PERMISSION_WORLD_QUERY = 1u << 2,
    DLUMA_LUA54_PERMISSION_EVENTS_SENSOR = 1u << 3,
    DLUMA_LUA54_PERMISSION_EVENTS_INTERACT = 1u << 4,
    DLUMA_LUA54_PERMISSION_STORAGE_MOD = 1u << 5,
    DLUMA_LUA54_PERMISSION_UI_DEBUG = 1u << 6,
    DLUMA_LUA54_PERMISSION_UI_OVERLAY = 1u << 7,
    DLUMA_LUA54_PERMISSION_AUDIO_PLAY = 1u << 8,
    DLUMA_LUA54_PERMISSION_WORLD_WRITE = 1u << 9,
    DLUMA_LUA54_PERMISSION_WORLD_TRANSFORM = 1u << 10,
    DLUMA_LUA54_PERMISSION_PLAYER_WRITE = 1u << 11,
    DLUMA_LUA54_PERMISSION_MODEL_WRITE = 1u << 12,
    DLUMA_LUA54_PERMISSION_MODEL_ANIMATE = 1u << 13,
    DLUMA_LUA54_PERMISSION_MAP_WRITE = 1u << 14,
    DLUMA_LUA54_PERMISSION_CAMERA_EFFECT = 1u << 15,
    DLUMA_LUA54_PERMISSION_AI_SPAWN = 1u << 16,
    DLUMA_LUA54_PERMISSION_CINEMATIC_CONTROL = 1u << 17,
    DLUMA_LUA54_PERMISSION_INVENTORY_WRITE = 1u << 18,
    DLUMA_LUA54_PERMISSION_QUEST_WRITE = 1u << 19,
    DLUMA_LUA54_PERMISSION_WORLD_GLOBAL_STATE = 1u << 20,
    DLUMA_LUA54_PERMISSION_WINDOW_CONTROL = 1u << 21,
    DLUMA_LUA54_PERMISSION_FILESYSTEM_WORKSHOP_READ = 1u << 22,
    DLUMA_LUA54_PERMISSION_FILESYSTEM_ARBITRARY = 1u << 23,
    DLUMA_LUA54_PERMISSION_PROCESS_LAUNCH = 1u << 24,
    DLUMA_LUA54_PERMISSION_NATIVE_FFI = 1u << 25,
    /* Death observations stay opt-in even when a future exact-build adapter
     * can provide them.  They are not inferred from events.interact. */
    DLUMA_LUA54_PERMISSION_EVENTS_ENEMY_KILLED = 1u << 26,
    DLUMA_LUA54_PERMISSION_QUEST_PHASE_HANDLE = 1u << 27,
    DLUMA_LUA54_PERMISSION_QUEST_PHASE_OBSERVE = 1u << 28,
    /* Editor Lua is intentionally unavailable in the MVP.  Retaining the
     * manifest permission in the private bitset keeps parsing and diagnostics
     * additive without exposing an Editor Lua host. */
    DLUMA_LUA54_PERMISSION_EDITOR_QUEST_EXTEND = 1u << 29
};

enum DlumaLua54Capability {
    DLUMA_LUA54_CAP_LUA_RUNTIME = 1u << 0,
    DLUMA_LUA54_CAP_STORAGE_MOD = 1u << 1,
    DLUMA_LUA54_CAP_WORLD_FIND = 1u << 2,
    DLUMA_LUA54_CAP_WORLD_LOCAL_ENABLE = 1u << 3,
    DLUMA_LUA54_CAP_EVENTS_SENSOR = 1u << 4,
    DLUMA_LUA54_CAP_EVENTS_INTERACT = 1u << 5,
    DLUMA_LUA54_CAP_PLAYER_READ = 1u << 6,
    DLUMA_LUA54_CAP_UI_OVERLAY = 1u << 7,
    DLUMA_LUA54_CAP_AUDIO_REGISTERED_CUES = 1u << 8,
    DLUMA_LUA54_CAP_ACTION_INVOKE = 1u << 9,
    DLUMA_LUA54_CAP_ACTION_CHAIN = 1u << 10,
    DLUMA_LUA54_CAP_MODIFIER_ALLOWLIST = 1u << 11,
    DLUMA_LUA54_CAP_UI_SUBTITLES = 1u << 12,
    DLUMA_LUA54_CAP_AUDIO_RADIO_DIALOG = 1u << 13,
    /* A future exact-build QuestManager binding may set this.  It is not
     * advertised merely because quest.write was approved. */
    DLUMA_LUA54_CAP_QUEST_NATIVE_CONTROL = 1u << 14,
    DLUMA_LUA54_CAP_EVENTS_ENEMY_KILLED = 1u << 15,
    /* These remain false until independently validated exact-build adapters
     * exist.  They are intentionally not inferred from PlayerVariables or
     * world.find readiness. */
    DLUMA_LUA54_CAP_PLAYER_VITALS = 1u << 16,
    DLUMA_LUA54_CAP_PLAYER_TRANSFORM = 1u << 17,
    DLUMA_LUA54_CAP_WORLD_POSITION = 1u << 18,
    /* Local, presentation-only access to the stock HudQuestObjective
     * component.  This never implies native quest-state mutation. */
    DLUMA_LUA54_CAP_UI_QUEST_HUD = 1u << 19,
    DLUMA_LUA54_CAP_QUEST_PHASE_HANDLERS = 1u << 20
};

enum DlumaLua54QuestHudTargetKind {
    DLUMA_LUA54_QUEST_HUD_HEADER = 1,
    DLUMA_LUA54_QUEST_HUD_OBJECTIVE = 2,
    DLUMA_LUA54_QUEST_HUD_TIMER = 3
};

enum DlumaLua54EventKind {
    DLUMA_LUA54_EVENT_LOAD = 1,
    DLUMA_LUA54_EVENT_UNLOAD = 2,
    DLUMA_LUA54_EVENT_MAP_LOAD = 3,
    DLUMA_LUA54_EVENT_MAP_UNLOAD = 4,
    DLUMA_LUA54_EVENT_UPDATE = 5,
    DLUMA_LUA54_EVENT_KEY = 6,
    DLUMA_LUA54_EVENT_SENSOR_ENTER = 7,
    DLUMA_LUA54_EVENT_SENSOR_LEAVE = 8,
    DLUMA_LUA54_EVENT_INTERACT_BEGIN = 9,
    DLUMA_LUA54_EVENT_INTERACT_END = 10,
    DLUMA_LUA54_EVENT_ENEMY_KILLED = 11,
    DLUMA_LUA54_EVENT_CONFIG_CHANGED = 12,
    DLUMA_LUA54_EVENT_QUEST_PHASE_PROVIDER = 13,
    DLUMA_LUA54_EVENT_QUEST_PHASE_OBSERVER = 14
};

enum DlumaLua54QuestOperation {
    DLUMA_LUA54_QUEST_START = 1,
    DLUMA_LUA54_QUEST_COMPLETE_CURRENT_PHASE = 2,
    DLUMA_LUA54_QUEST_JUMP_TO_PHASE = 3,
    DLUMA_LUA54_QUEST_COMPLETE = 4
};

enum DlumaLua54QuestState {
    DLUMA_LUA54_QUEST_STATE_DECLARED = 1,
    DLUMA_LUA54_QUEST_STATE_ACTIVE = 2,
    DLUMA_LUA54_QUEST_STATE_COMPLETED = 3,
    DLUMA_LUA54_QUEST_STATE_FAILED = 4
};

enum DlumaLua54QuestNativeState {
    DLUMA_LUA54_QUEST_NATIVE_NONE = 0,
    DLUMA_LUA54_QUEST_NATIVE_UNAVAILABLE = 1,
    DLUMA_LUA54_QUEST_NATIVE_MIRRORED = 2
};

enum DlumaLua54ValueType {
    DLUMA_LUA54_VALUE_NONE = 0,
    DLUMA_LUA54_VALUE_STRING = 1,
    DLUMA_LUA54_VALUE_NUMBER = 2,
    DLUMA_LUA54_VALUE_BOOLEAN = 3,
    DLUMA_LUA54_VALUE_INTEGER = 4
};

typedef uint64_t (*DlumaLua54ClockMsFn)(void* host_user);
typedef void (*DlumaLua54LogFn)(
    void* host_user,
    const char* level,
    const char* text);
typedef int (*DlumaLua54StorageGetFn)(
    void* host_user,
    const char* key,
    uint32_t* value_type,
    char* value,
    uint32_t value_capacity);
typedef int (*DlumaLua54StorageSetFn)(
    void* host_user,
    const char* key,
    uint32_t value_type,
    const char* value);
typedef int (*DlumaLua54StorageDeleteFn)(void* host_user, const char* key);
typedef void (*DlumaLua54DebugSetFn)(
    void* host_user,
    const char* key,
    const char* value,
    int remove_value);

/* Every native adapter result crosses the private helper boundary as bounded,
 * copied data.  None of these records contains an engine pointer, module base,
 * vtable, callable address, or FFI token. */
typedef struct DlumaLua54Error {
    uint32_t struct_size;
    uint32_t retryable;
    char code[64];
    char message[256];
    char capability[64];
    char permission[64];
    char object_name[DLUMA_LUA54_MAX_OBJECT_NAME];
} DlumaLua54Error;

typedef int (*DlumaLua54ConfigGetFn)(
    void* host_user,
    const char* key,
    uint32_t* value_type,
    char* value,
    uint32_t value_capacity,
    DlumaLua54Error* error);

typedef struct DlumaLua54Object {
    uint32_t struct_size;
    uint32_t map_generation;
    uint64_t handle_id;
    uint64_t owner_id;
    int32_t object_id;
    uint32_t reserved;
    char name[DLUMA_LUA54_MAX_OBJECT_NAME];
    char class_name[DLUMA_LUA54_MAX_CLASS_NAME];
} DlumaLua54Object;

/* A copied position result.  The helper owns this record for the duration of
 * one host call and converts it to ordinary Lua numbers immediately; no game
 * object, matrix, pointer, or callable crosses this ABI. */
typedef struct DlumaLua54Vec3 {
    uint32_t struct_size;
    float x;
    float y;
    float z;
} DlumaLua54Vec3;

typedef struct DlumaLua54SetEnabledRequest {
    uint32_t struct_size;
    uint32_t map_generation;
    uint64_t owner_id;
    uint32_t requested_count;
    uint32_t enabled;
    uint32_t atomic;
    uint32_t best_effort;
    char names[DLUMA_LUA54_MAX_BULK_OBJECTS][DLUMA_LUA54_MAX_OBJECT_NAME];
} DlumaLua54SetEnabledRequest;

typedef struct DlumaLua54SetEnabledResult {
    uint32_t struct_size;
    uint32_t requested_count;
    uint32_t changed_count;
    uint32_t unchanged_count;
    uint32_t partial;
    uint32_t name_count;
    char names[DLUMA_LUA54_MAX_BULK_OBJECTS][DLUMA_LUA54_MAX_OBJECT_NAME];
} DlumaLua54SetEnabledResult;

typedef struct DlumaLua54SubtitleRequest {
    uint32_t struct_size;
    uint32_t force;
    double duration_seconds;
    char text[DLUMA_LUA54_MAX_SUBTITLE_TEXT];
} DlumaLua54SubtitleRequest;

typedef struct DlumaLua54SubtitleUpdateRequest {
    uint32_t struct_size;
    uint32_t fade;
    char text[DLUMA_LUA54_MAX_SUBTITLE_TEXT];
} DlumaLua54SubtitleUpdateRequest;

typedef struct DlumaLua54RadioDialogRequest {
    uint32_t struct_size;
    char speaker[DLUMA_LUA54_MAX_DIALOG_NAME];
    char group[DLUMA_LUA54_MAX_DIALOG_NAME];
} DlumaLua54RadioDialogRequest;

typedef struct DlumaLua54QuestHudTarget {
    uint32_t struct_size;
    uint32_t kind;
    uint32_t slot;
    uint32_t overridden;
    char id[DLUMA_LUA54_MAX_QUEST_HUD_TARGET_ID];
    char text[DLUMA_LUA54_MAX_QUEST_HUD_TEXT + 1u];
    char display_text[DLUMA_LUA54_MAX_QUEST_HUD_TEXT + 1u];
} DlumaLua54QuestHudTarget;

/* A fully copied view of the currently displayed local quest HUD.  Header
 * and timer presence are explicit; objectives contains only bound, visible
 * stock rows and therefore never exceeds the four-row XUI contract. */
typedef struct DlumaLua54QuestHudSnapshot {
    uint32_t struct_size;
    uint32_t map_generation;
    uint64_t revision;
    uint32_t has_header;
    uint32_t objective_count;
    uint32_t has_timer;
    uint32_t reserved;
    DlumaLua54QuestHudTarget header;
    DlumaLua54QuestHudTarget objectives[DLUMA_LUA54_MAX_QUEST_HUD_OBJECTIVES];
    DlumaLua54QuestHudTarget timer;
} DlumaLua54QuestHudSnapshot;

typedef struct DlumaLua54QuestHudOverrideRequest {
    uint32_t struct_size;
    uint32_t map_generation;
    uint64_t owner_id;
    uint64_t override_id;
    char target_id[DLUMA_LUA54_MAX_QUEST_HUD_TARGET_ID];
    char text[DLUMA_LUA54_MAX_QUEST_HUD_OVERRIDE_TEXT + 1u];
} DlumaLua54QuestHudOverrideRequest;

typedef struct DlumaLua54QuestHudOverrideResult {
    uint32_t struct_size;
    uint32_t active;
    uint64_t override_id;
} DlumaLua54QuestHudOverrideResult;

/* Native quest transitions only name pre-authored quest/phase identifiers.
 * There is deliberately no pointer, compiler source, localization payload,
 * raw state field, or arbitrary native-call surface in this ABI. */
typedef struct DlumaLua54QuestRequest {
    uint32_t struct_size;
    uint32_t map_generation;
    uint64_t owner_id;
    uint32_t operation;
    uint32_t reserved;
    char native_quest_id[DLUMA_LUA54_MAX_QUEST_ID];
    char native_phase_id[DLUMA_LUA54_MAX_QUEST_ID];
} DlumaLua54QuestRequest;

typedef struct DlumaLua54QuestResult {
    uint32_t struct_size;
    uint32_t operation;
    uint32_t changed;
    uint32_t native_state;
    char native_quest_id[DLUMA_LUA54_MAX_QUEST_ID];
    char native_phase_id[DLUMA_LUA54_MAX_QUEST_ID];
} DlumaLua54QuestResult;

/* This is copied into the Lua event record when a future validated post-death
 * observer is introduced.  It never aliases a game object or engine string. */
typedef struct DlumaLua54EnemyKilledEvent {
    uint32_t struct_size;
    uint32_t map_generation;
    uint64_t event_sequence;
    uint32_t killed_by_local_player;
    uint32_t reserved;
    char target_name[DLUMA_LUA54_MAX_OBJECT_NAME];
    char target_class[DLUMA_LUA54_MAX_CLASS_NAME];
    char killer_name[DLUMA_LUA54_MAX_OBJECT_NAME];
} DlumaLua54EnemyKilledEvent;

/* Config values cross the helper boundary only as bounded, copied scalars.
 * Integer payloads use canonical signed decimal text. Enum and text settings
 * both use STRING, while the statically parsed schema remains loader-owned. */
typedef struct DlumaLua54ConfigChangedEvent {
    uint32_t struct_size;
    uint32_t old_value_type;
    uint32_t value_type;
    uint32_t reserved;
    char key[DLUMA_LUA54_MAX_CONFIG_KEY + 1u];
    char old_value[DLUMA_LUA54_MAX_CONFIG_VALUE + 1u];
    char value[DLUMA_LUA54_MAX_CONFIG_VALUE + 1u];
} DlumaLua54ConfigChangedEvent;

/* Every field aliases one immutable, callback-lifetime event copy.  Sizes are
 * authoritative; arguments_json may contain JSON escapes that decode to Lua
 * strings with embedded NUL bytes. */
typedef struct DlumaLua54ByteView {
    const char* data;
    size_t size;
} DlumaLua54ByteView;

typedef struct DlumaLua54QuestPhaseEvent {
    uint32_t struct_size;
    uint32_t map_generation;
    uint32_t runtime_kind;
    uint32_t completion;
    uint64_t sequence;
    double wait_seconds;
    uint32_t local_peer;
    uint32_t reserved;
    DlumaLua54ByteView quest_id;
    DlumaLua54ByteView phase_id;
    DlumaLua54ByteView contract_id;
    DlumaLua54ByteView provider;
    DlumaLua54ByteView handler;
    DlumaLua54ByteView binding_hash;
    DlumaLua54ByteView arguments_json;
} DlumaLua54QuestPhaseEvent;

/* Legacy ABI record retained for compatible helper/runtime binaries.  The
 * loader no longer emits Lua-owned quest snapshots or maintains a local quest
 * graph; native quest state remains game-owned. */
typedef struct DlumaLua54QuestStateReport {
    uint32_t struct_size;
    uint32_t map_generation;
    uint64_t owner_id;
    uint64_t revision;
    uint32_t active;
    uint32_t state;
    uint32_t native_state;
    uint32_t reserved;
    char script_id[DLUMA_LUA54_MAX_SCRIPT_ID];
    char quest_id[DLUMA_LUA54_MAX_QUEST_ID];
    char title[DLUMA_LUA54_MAX_QUEST_TITLE];
    char version[DLUMA_LUA54_MAX_QUEST_VERSION];
    char phase_id[DLUMA_LUA54_MAX_QUEST_ID];
    char phase_title[DLUMA_LUA54_MAX_QUEST_TITLE];
    char objective_summary[DLUMA_LUA54_MAX_QUEST_OBJECTIVE_SUMMARY];
} DlumaLua54QuestStateReport;

typedef int (*DlumaLua54SubscriptionAddedFn)(
    void* host_user,
    uint32_t token,
    uint32_t kind,
    const char* exact_name,
    DlumaLua54Error* error);
typedef int (*DlumaLua54SubscriptionRemovedFn)(
    void* host_user,
    uint32_t token,
    uint32_t kind,
    const char* exact_name,
    DlumaLua54Error* error);
typedef int (*DlumaLua54WorldFindFn)(
    void* host_user,
    const char* exact_name,
    const char* expected_class,
    DlumaLua54Object* object,
    DlumaLua54Error* error);
typedef int (*DlumaLua54WorldGetInfoFn)(
    void* host_user,
    uint64_t handle_id,
    uint64_t owner_id,
    uint32_t map_generation,
    DlumaLua54Object* object,
    DlumaLua54Error* error);
typedef int (*DlumaLua54WorldGetLocalEnabledFn)(
    void* host_user,
    uint64_t handle_id,
    uint64_t owner_id,
    uint32_t map_generation,
    int* enabled,
    DlumaLua54Error* error);
typedef int (*DlumaLua54WorldGetPositionFn)(
    void* host_user,
    uint64_t handle_id,
    uint64_t owner_id,
    uint32_t map_generation,
    DlumaLua54Vec3* position,
    DlumaLua54Error* error);
/* Returns a copied, normalized world-forward vector from the same validated
 * IControlObject anchor as world_get_position. */
typedef int (*DlumaLua54WorldGetForwardFn)(
    void* host_user,
    uint64_t handle_id,
    uint64_t owner_id,
    uint32_t map_generation,
    DlumaLua54Vec3* forward,
    DlumaLua54Error* error);
typedef int (*DlumaLua54WorldSetLocalEnabledFn)(
    void* host_user,
    const DlumaLua54SetEnabledRequest* request,
    DlumaLua54SetEnabledResult* result,
    DlumaLua54Error* error);
typedef int (*DlumaLua54SensorIsInsideFn)(
    void* host_user,
    const char* exact_name,
    int* inside,
    DlumaLua54Error* error);
typedef int (*DlumaLua54SubtitleShowFn)(
    void* host_user,
    const DlumaLua54SubtitleRequest* request,
    DlumaLua54Error* error);
typedef int (*DlumaLua54SubtitleUpdateFn)(
    void* host_user,
    const DlumaLua54SubtitleUpdateRequest* request,
    DlumaLua54Error* error);
typedef int (*DlumaLua54RadioDialogFn)(
    void* host_user,
    const DlumaLua54RadioDialogRequest* request,
    DlumaLua54Error* error);
typedef int (*DlumaLua54QuestMutateFn)(
    void* host_user,
    const DlumaLua54QuestRequest* request,
    DlumaLua54QuestResult* result,
    DlumaLua54Error* error);
typedef int (*DlumaLua54QuestStateChangedFn)(
    void* host_user,
    const DlumaLua54QuestStateReport* report,
    DlumaLua54Error* error);
typedef int (*DlumaLua54QuestHudSnapshotFn)(
    void* host_user,
    DlumaLua54QuestHudSnapshot* snapshot,
    DlumaLua54Error* error);
typedef int (*DlumaLua54QuestHudOverrideSetFn)(
    void* host_user,
    const DlumaLua54QuestHudOverrideRequest* request,
    DlumaLua54QuestHudOverrideResult* result,
    DlumaLua54Error* error);
typedef int (*DlumaLua54QuestHudOverrideClearFn)(
    void* host_user,
    uint64_t owner_id,
    uint32_t map_generation,
    uint64_t override_id,
    DlumaLua54QuestHudOverrideResult* result,
    DlumaLua54Error* error);
typedef void (*DlumaLua54QuestHudClearOwnerFn)(
    void* host_user,
    uint64_t owner_id);
typedef int (*DlumaLua54ActionRunChainFn)(
    void* host_user,
    const char* exact_name,
    DlumaLua54Error* error);
typedef uint32_t (*DlumaLua54CapabilitiesFn)(void* host_user);
typedef int (*DlumaLua54CapabilityQueryFn)(
    void* host_user,
    uint32_t capability_id,
    uint32_t* configured,
    uint32_t* ready,
    char* status,
    uint32_t status_capacity,
    DlumaLua54Error* error);
typedef int (*DlumaLua54ApiInvokeFn)(
    void* host_user,
    uint32_t operation_id,
    const void* request,
    uint32_t request_size,
    void* result,
    uint32_t result_size,
    DlumaLua54Error* error);
typedef int (*DlumaLua54ModuleLoadFn)(
    void* host_user,
    const char* module_name,
    const char** source_out,
    size_t* source_size_out,
    void** artifact_token_out,
    DlumaLua54Error* error);
typedef void (*DlumaLua54ModuleReleaseFn)(
    void* host_user,
    void* artifact_token);

typedef struct DlumaLua54HostCallbacks {
    uint32_t struct_size;
    DlumaLua54ClockMsFn clock_ms;
    DlumaLua54LogFn log;
    DlumaLua54StorageGetFn storage_get;
    DlumaLua54StorageSetFn storage_set;
    DlumaLua54StorageDeleteFn storage_delete;
    DlumaLua54DebugSetFn debug_set;
    DlumaLua54SubscriptionAddedFn subscription_added;
    DlumaLua54SubscriptionRemovedFn subscription_removed;
    DlumaLua54WorldFindFn world_find;
    DlumaLua54WorldGetInfoFn world_get_info;
    DlumaLua54WorldGetLocalEnabledFn world_get_local_enabled;
    DlumaLua54WorldGetPositionFn world_get_position;
    DlumaLua54WorldGetForwardFn world_get_forward;
    DlumaLua54WorldSetLocalEnabledFn world_set_local_enabled;
    DlumaLua54SensorIsInsideFn sensor_is_inside;
    DlumaLua54SubtitleShowFn subtitle_show;
    DlumaLua54SubtitleUpdateFn subtitle_update;
    DlumaLua54RadioDialogFn radio_dialog;
    DlumaLua54ActionRunChainFn action_run_chain;
    DlumaLua54QuestMutateFn quest_mutate;
    DlumaLua54QuestStateChangedFn quest_state_changed;
    DlumaLua54QuestHudSnapshotFn quest_hud_snapshot;
    DlumaLua54QuestHudOverrideSetFn quest_hud_override_set;
    DlumaLua54QuestHudOverrideClearFn quest_hud_override_clear;
    DlumaLua54QuestHudClearOwnerFn quest_hud_clear_owner;
    /* Returns the complete currently-ready capability mask.  The configured
     * mask remains the stable support surface used for API gates. */
    DlumaLua54CapabilitiesFn capabilities;
    DlumaLua54ConfigGetFn config_get;
    DlumaLua54ModuleLoadFn module_load;
    DlumaLua54ModuleReleaseFn module_release;
    /* Stable generated capability IDs replace the legacy helper bitmask at
     * the authored API boundary. The bitmask callback above remains private
     * only for helper-internal fast gates. */
    DlumaLua54CapabilityQueryFn capability_query;
    /* Generic generated-contract dispatcher. Language-specific bridges only
     * marshal POD records; all validation and engine work stays in the shared
     * runtime and game services. */
    DlumaLua54ApiInvokeFn api_invoke;
} DlumaLua54HostCallbacks;

typedef struct DlumaLua54ScriptConfig {
    uint32_t struct_size;
    uint32_t scope;
    uint32_t map_generation;
    uint32_t approved_permissions;
    uint32_t capabilities;
    uint64_t owner_id;
    uint64_t source_fingerprint;
    uint64_t memory_limit_bytes;
    uint64_t instruction_limit;
    uint32_t wall_clock_limit_ms;
    uint32_t max_callbacks;
    uint32_t max_timers;
    uint32_t max_rules;
    uint32_t max_log_messages_per_second;
    const char* id;
    const char* name;
    const char* version;
    const char* permissions;
    const char* file_path;
    const char* project;
    const char* map;
    const char* workshop_id;
    const char* loader_version;
    int is_editor;
    int is_player;
    void* host_user;
    DlumaLua54HostCallbacks host;
} DlumaLua54ScriptConfig;

typedef struct DlumaLua54Event {
    uint32_t struct_size;
    uint32_t kind;
    uint32_t map_generation;
    int state;
    double number;
    const char* name;
    const char* project;
    const char* map;
    DlumaLua54EnemyKilledEvent enemy_killed;
    DlumaLua54ConfigChangedEvent config_changed;
    DlumaLua54QuestPhaseEvent quest_phase;
} DlumaLua54Event;

typedef struct DlumaLua54Status {
    uint32_t struct_size;
    uint32_t alive;
    uint32_t disabled;
    uint32_t declaration_seen;
    uint32_t has_update_callback;
    uint32_t active_callbacks;
    uint32_t budget_violations;
    uint64_t memory_used_bytes;
    uint64_t source_fingerprint;
    uint64_t last_callback_lua_us;
    uint64_t last_callback_host_excluded_us;
    uint64_t last_instruction_count_or_remaining;
    char last_event[DLUMA_LUA54_MAX_EVENT];
    char budget_type[32];
    char last_error[DLUMA_LUA54_MAX_ERROR];
} DlumaLua54Status;

typedef struct DlumaLua54Api {
    uint32_t struct_size;
    uint32_t abi_version;
    const char* release;
    void* (*create_script)(const DlumaLua54ScriptConfig* config);
    int (*load_script)(void* script, const char* source, size_t source_size);
    int (*dispatch)(void* script, const DlumaLua54Event* event);
    int (*get_status)(void* script, DlumaLua54Status* status);
    int (*destroy_script)(void* script);
    /* Starts one game-thread pump budget epoch for this script. All events,
     * timers, and update callbacks dispatched before the next call share the
     * aggregate instruction and Lua-only wall-clock allowance. */
    int (*begin_pump)(void* script);
} DlumaLua54Api;

typedef int (*DlumaLua54GetApiFn)(
    uint32_t requested_abi,
    DlumaLua54Api* out_api,
    uint32_t out_size);

#ifdef __cplusplus
}
#endif

#endif
