#include "dlw_common.h"
#include "hosts/dlw_managed_host.h"
#include "dlw_native_gameplay.h"
#include "dlw_native_mods.h"
#include "dlw_quest_hud.h"

#include <math.h>

/* Exact Windows Developer Tools fingerprint and adapter targets.  These
 * constants are intentionally centralized here; none is a Mach-O address. */
#define DLUMA_NATIVE_ENGINE_TIMESTAMP 0x623395D0UL
#define DLUMA_NATIVE_ENGINE_IMAGE_SIZE 0x00E8D000UL
#define DLUMA_NATIVE_GAMEDLL_TIMESTAMP 0x623397AAUL
#define DLUMA_NATIVE_GAMEDLL_IMAGE_SIZE 0x02082000UL

#define DLUMA_NATIVE_ILEVEL_GET_OBJECT_BY_ID_RVA 0x002D3C30UL
#define DLUMA_NATIVE_IGSOBJECT_GET_PREV_RVA 0x002772D0UL
#define DLUMA_NATIVE_IGSOBJECT_GET_NEXT_RVA 0x002772F0UL
#define DLUMA_NATIVE_IGSOBJECT_GET_LEVEL_RVA 0x00277A00UL
#define DLUMA_NATIVE_ILEVEL_GET_LEVEL_NAME_RVA 0x002DEC70UL
#define DLUMA_NATIVE_IGSOBJECT_GET_ID_RVA 0x00277880UL
#define DLUMA_NATIVE_CRTTI_GET_CLASS_NAME_A_RVA 0x0000A640UL
#define DLUMA_NATIVE_CRTTI_FROM_CLASS_NAME_RVA 0x00476F70UL
#define DLUMA_NATIVE_IGSOBJECT_VTABLE_RVA 0x00867768UL
#define DLUMA_NATIVE_IGSOBJECT_GET_RTTI_RVA 0x00276FF0UL
#define DLUMA_NATIVE_IGSOBJECT_GET_ENGINE_RTTI_RVA 0x00277000UL

/* Exact Windows 1.49 engine exports.  These RVAs are validated again through
 * the decorated exports and a fixed executable prologue before the adapter is
 * made ready.  They are not decompiler-import aliases from gamedll. */
#define DLUMA_NATIVE_CONTROL_GET_WORLD_POSITION_RVA 0x00136AF0UL
#define DLUMA_NATIVE_CONTROL_GET_FORWARD_VECTOR_RVA 0x00137170UL

#define DLUMA_NATIVE_SENSOR_VTABLE_RVA 0x0188EEC8UL
#define DLUMA_NATIVE_SENSOR_CHECK_RVA 0x00ECB720UL
#define DLUMA_NATIVE_SENSOR_CHECK_INSIDE_RVA 0x00ECB880UL
#define DLUMA_NATIVE_SENSOR_CHECK_INSIDE_SLOT 0x0310UL

#define DLUMA_NATIVE_LOCAL_ENABLE_RVA 0x000021C0UL
#define DLUMA_NATIVE_LOCAL_DISABLE_RVA 0x00002180UL
#define DLUMA_NATIVE_LOCAL_IS_ENABLED_RVA 0x00EDD2F0UL
#define DLUMA_NATIVE_LOCAL_ENABLE_SLOT 0x0030UL
#define DLUMA_NATIVE_LOCAL_DISABLE_SLOT 0x0038UL
#define DLUMA_NATIVE_LOCAL_IS_ENABLED_SLOT 0x0040UL

#define DLUMA_NATIVE_INTERACTIVE_ITEM_VTABLE_RVA 0x017CD048UL
#define DLUMA_NATIVE_INTERACTIVE_EXECUTE_RVA 0x00AB5FC0UL
#define DLUMA_NATIVE_INTERACTIVE_EXECUTE_SLOT 0x0460UL

#define DLUMA_NATIVE_ACTION_CHAIN_VTABLE_RVA 0x0195B938UL
#define DLUMA_NATIVE_ACTION_CHAIN_RUN_RVA 0x01189800UL
#define DLUMA_NATIVE_ACTION_CHAIN_SIZE 0x0060UL

#define DLUMA_NATIVE_LEVELDI_VTABLE_RVA 0x01581C98UL
#define DLUMA_NATIVE_LEVEL_SHOW_SUBTITLE_RVA 0x003F7120UL
#define DLUMA_NATIVE_LEVEL_HIDE_SUBTITLE_RVA 0x003F7520UL
#define DLUMA_NATIVE_LEVEL_SHOW_SUBTITLE_SLOT 0x0050UL
#define DLUMA_NATIVE_LEVEL_HIDE_SUBTITLE_SLOT 0x0058UL
#define DLUMA_NATIVE_LEVEL_MOVIE_OFFSET 0x0DE8UL
#define DLUMA_NATIVE_GAME_MOVIE_VTABLE_RVA 0x0197DF48UL
#define DLUMA_NATIVE_GAME_MOVIE_SHOW_SUBTITLE_RVA 0x011ED470UL
#define DLUMA_NATIVE_GAME_MOVIE_HIDE_SUBTITLE_RVA 0x011ED860UL
#define DLUMA_NATIVE_GAME_MOVIE_SHOW_SUBTITLE_SLOT 0x0928UL
#define DLUMA_NATIVE_GAME_MOVIE_HIDE_SUBTITLE_SLOT 0x0930UL

#define DLUMA_NATIVE_DIALOG_SPEAKER_VTABLE_RVA 0x01566268UL
#define DLUMA_NATIVE_DIALOG_SPEAKER_FIND_GROUP_RVA 0x0030A240UL
#define DLUMA_NATIVE_DIALOG_SPEAKER_SET_QUEST_RVA 0x00309FA0UL
#define DLUMA_NATIVE_DIALOG_SPEAKER_FIND_GROUP_SLOT 0x0048UL
#define DLUMA_NATIVE_DIALOG_SPEAKER_SET_QUEST_SLOT 0x0070UL
#define DLUMA_NATIVE_DIALOG_MANAGER_RVA 0x01DF5A00UL
#define DLUMA_NATIVE_DIALOG_MANAGER_VTABLE_RVA 0x01565B78UL
#define DLUMA_NATIVE_DIALOG_MANAGER_INIT_RVA 0x003049E0UL
#define DLUMA_NATIVE_DIALOG_MANAGER_FIND_SPEAKER_RVA 0x00306D90UL
#define DLUMA_NATIVE_DIALOG_MANAGER_FIND_GROUP_RVA 0x00309000UL

/* QuestManager bindings recovered from the exact Windows Developer Tools
 * gamedll.  The mutation route deliberately ends at StartSynchronizedInvoke:
 * it writes the engine's replicated invoke-stage request.  Lua must never
 * call AdvanceToQuest or mutate a Quest/Phase state directly. */
#define DLUMA_NATIVE_QUEST_MANAGER_RVA 0x01E0F990UL
#define DLUMA_NATIVE_QUEST_MANAGER_VTABLE_RVA 0x01819B68UL
#define DLUMA_NATIVE_QUEST_MANAGER_QUEST_TREE_OFFSET 0x0020UL
#define DLUMA_NATIVE_QUEST_MANAGER_PEER_TREE_OFFSET 0x00C8UL
#define DLUMA_NATIVE_QUEST_MANAGER_CLIENT_STATE_OFFSET 0x00E0UL
#define DLUMA_NATIVE_QUEST_CLIENT_STATE_STATUS_OFFSET 0x004CUL
#define DLUMA_NATIVE_QUEST_PHASE_OWNER_OFFSET 0x0050UL
#define DLUMA_NATIVE_QUEST_FIND_RVA 0x00D9BDE0UL
#define DLUMA_NATIVE_QUEST_FIND_PHASE_RVA 0x00D87AA0UL
#define DLUMA_NATIVE_QUEST_START_SYNCHRONIZED_INVOKE_RVA 0x00DA2A50UL

#define DLUMA_NATIVE_SUBTITLE_ID 0x444C5701L

#define DLUMA_NATIVE_IGSOBJECT_GET_RTTI_SLOT 0x0010UL
#define DLUMA_NATIVE_IGSOBJECT_GET_ENGINE_RTTI_SLOT 0x0018UL
#define DLUMA_NATIVE_OBJECT_NAME_RECORD_OFFSET 0x0020UL
#define DLUMA_NATIVE_NAME_POINTER_OFFSET 0x0060UL
#define DLUMA_NATIVE_GAMEPLAY_RTTI_GETTER_SIZE 0x0008UL
#define DLUMA_NATIVE_RTTI_READABLE_SIZE 0x00A0UL

#define DLUMA_NATIVE_MAX_INDEX 8192u
#define DLUMA_NATIVE_MAX_VISITED 8192u
#define DLUMA_NATIVE_MAX_HANDLES 512u
#define DLUMA_NATIVE_MAX_SUBSCRIPTIONS 192u
#define DLUMA_NATIVE_MAX_HOOK_EVENTS 64u
#define DLUMA_NATIVE_SEED_SCAN_MAX 65535u
#define DLUMA_NATIVE_SEED_SCAN_BATCH 2048u
#define DLUMA_NATIVE_SENSOR_POLL_MS 75u
#define DLUMA_NATIVE_RESOLVE_RETRY_MS 1000u
#define DLUMA_NATIVE_REJECT_SAMPLE_LIMIT 8u
#define DLUMA_NATIVE_ACCEPT_SAMPLE_LIMIT 24u
#define DLUMA_NATIVE_PROGRESS_INTERVAL 8192u
#define DLUMA_NATIVE_INITIAL_STREAMING_DELAY_MS 1500u
#define DLUMA_NATIVE_RECOVERY_DELAY_MS 2500u
#define DLUMA_NATIVE_MAX_RECOVERY_ATTEMPTS 3u
#define DLUMA_NATIVE_CAPABILITY_COUNT 10u

static const BYTE g_dluma_get_rtti_prefix[] = {
    0x48, 0x8D, 0x05, 0xF9, 0x5A, 0x86, 0x00, 0xC3
};
static const BYTE g_dluma_get_object_by_id_prefix[] = {
    0x48, 0x83, 0xEC, 0x28, 0x48, 0x83, 0x79, 0x08,
    0x00, 0x74, 0x1B, 0x48, 0x8B, 0x49, 0x08, 0x48
};
static const BYTE g_dluma_get_prev_prefix[] = {
    0x48, 0x8B, 0x41, 0x20, 0x48, 0x8B, 0x40, 0x28,
    0x48, 0x85, 0xC0, 0x74, 0x05, 0x48, 0x8B, 0x40
};
static const BYTE g_dluma_get_next_prefix[] = {
    0x48, 0x8B, 0x41, 0x20, 0x48, 0x8B, 0x40, 0x30,
    0x48, 0x85, 0xC0, 0x74, 0x05, 0x48, 0x8B, 0x40
};
static const BYTE g_dluma_get_level_prefix[] = {
    0x48, 0x8B, 0x41, 0x20, 0x48, 0x8B, 0x40, 0x48,
    0x48, 0x85, 0xC0, 0x74, 0x08, 0x48, 0x8B, 0x80
};
static const BYTE g_dluma_get_level_name_prefix[] = {
    0x48, 0x8B, 0x41, 0x08, 0x48, 0x05, 0x74, 0x0C,
    0x00, 0x00, 0xC3
};
static const BYTE g_dluma_get_id_prefix[] = {
    0x48, 0x8B, 0x41, 0x20, 0x8B, 0x40, 0x50, 0xC3
};
static const BYTE g_dluma_control_get_world_position_prefix[] = {
    0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B,
    0x41, 0x08, 0x48, 0x8B, 0xDA, 0x48, 0x8B, 0x88
};
static const BYTE g_dluma_control_get_forward_vector_prefix[] = {
    0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B,
    0x49, 0x08, 0x48, 0x8B, 0xDA, 0xE8, 0x9E, 0xE5
};
static const BYTE g_dluma_get_class_name_prefix[] = {
    0x48, 0x8B, 0x91, 0x98, 0x00, 0x00, 0x00, 0x48,
    0x8D, 0x05, 0xF3, 0xCC, 0x85, 0x00, 0x48, 0x85
};
static const BYTE g_dluma_from_class_prefix[] = {
    0x4C, 0x8B, 0xDA, 0x4C, 0x8B, 0xD1, 0x4C, 0x8B,
    0xC1, 0x48, 0x85, 0xC9, 0x74, 0x63, 0x66, 0x90
};

static const BYTE g_dluma_sensor_check_inside_prefix[] = {
    0x40, 0x53, 0x48, 0x83, 0xEC, 0x30, 0x44, 0x8B,
    0x81, 0x18, 0x01, 0x00, 0x00, 0x48, 0x8D
};
static const BYTE g_dluma_interactive_execute_prefix[] = {
    0x48, 0x89, 0x5C, 0x24, 0x10, 0x57, 0x48, 0x81,
    0xEC, 0x20, 0x01, 0x00, 0x00, 0x49, 0x8B, 0xF8
};
static const BYTE g_dluma_action_chain_run_prefix[] = {
    0x48, 0x89, 0x5C, 0x24, 0x10, 0x57, 0x48, 0x83,
    0xEC, 0x40, 0x48, 0x8B, 0x59, 0x40, 0x48, 0x8B
};
static const BYTE g_dluma_local_enable_prefix[] = {
    0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x80, 0x79,
    0x08, 0x00, 0x48, 0x8B, 0xD9, 0x75, 0x20, 0x48
};
static const BYTE g_dluma_local_disable_prefix[] = {
    0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x80, 0x79,
    0x08, 0x00, 0x48, 0x8B, 0xD9, 0x74, 0x20, 0x48
};
static const BYTE g_dluma_local_is_enabled_prefix[] = {
    0x0F, 0xB6, 0x41, 0x08, 0xC3
};
static const BYTE g_dluma_level_show_subtitle_prefix[] = {
    0x44, 0x89, 0x4C, 0x24, 0x20, 0x48, 0x89, 0x54,
    0x24, 0x10, 0x48, 0x89, 0x4C, 0x24, 0x08, 0x55
};
static const BYTE g_dluma_level_hide_subtitle_prefix[] = {
    0x48, 0x83, 0xEC, 0x28, 0x48, 0x8B, 0x89, 0xE8,
    0x0D, 0x00, 0x00, 0x48, 0x8B, 0x01, 0xFF, 0x90
};
static const BYTE g_dluma_movie_show_subtitle_prefix[] = {
    0x4C, 0x8B, 0xDC, 0x49, 0x89, 0x4B, 0x08, 0x41,
    0x55, 0x41, 0x56, 0x48, 0x81, 0xEC, 0xA8, 0x00
};
static const BYTE g_dluma_movie_hide_subtitle_prefix[] = {
    0x48, 0x89, 0x5C, 0x24, 0x10, 0x57, 0x48, 0x83,
    0xEC, 0x20, 0x4C, 0x8B, 0x81, 0xA0, 0x00, 0x00
};
static const BYTE g_dluma_dialog_find_group_prefix[] = {
    0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C,
    0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x57
};
static const BYTE g_dluma_dialog_set_quest_prefix[] = {
    0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x6C,
    0x24, 0x18, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x48
};
static const BYTE g_dluma_dialog_manager_init_prefix[] = {
    0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x33, 0xDB,
    0x48, 0x8D, 0x05, 0x89, 0x11, 0x26, 0x01, 0x33
};
static const BYTE g_dluma_dialog_manager_find_speaker_prefix[] = {
    0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C,
    0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x57
};
static const BYTE g_dluma_dialog_manager_find_group_prefix[] = {
    0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C,
    0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x57
};
static const BYTE g_dluma_quest_find_prefix[] = {
    0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83,
    0xEC, 0x20, 0x48, 0x8B, 0x19, 0x48, 0x8B, 0xFA
};
static const BYTE g_dluma_quest_find_phase_prefix[] = {
    0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74,
    0x24, 0x10, 0x48, 0x89, 0x7C, 0x24, 0x18, 0x55,
    0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57
};
static const BYTE g_dluma_quest_start_synchronized_invoke_prefix[] = {
    0x48, 0x89, 0x5C, 0x24, 0x18, 0x57, 0x48, 0x83,
    0xEC, 0x70, 0x33, 0xFF, 0x48, 0x8D, 0x99, 0xE0,
    0x00, 0x00, 0x00, 0x48, 0x8B, 0x03, 0x89, 0xBC,
    0x24, 0x80, 0x00, 0x00, 0x00, 0x83, 0x78, 0x4C
};

typedef void* (__fastcall *DlumaNativeGetObjectByIdFn)(void*, int);
typedef void* (__fastcall *DlumaNativeObjectLinkFn)(void*);
typedef void* (*DlumaNativeBackendLinkFn)(void*, void*);
typedef void* (__fastcall *DlumaNativeObjectLevelFn)(void*);
typedef int (__fastcall *DlumaNativeObjectIdFn)(void*);
typedef void* (__fastcall *DlumaNativeDynamicRttiFn)(void*);
typedef const char* (__fastcall *DlumaNativeClassNameFn)(void*);
typedef unsigned char (__fastcall *DlumaNativeFromClassFn)(void*, const char*);
typedef struct DlumaNativeVec3 {
    float x;
    float y;
    float z;
} DlumaNativeVec3;
typedef DlumaNativeVec3* (__fastcall *DlumaNativeControlGetWorldPositionFn)(
    void*, DlumaNativeVec3*);
typedef DlumaNativeVec3* (__fastcall *DlumaNativeControlGetForwardVectorFn)(
    void*, DlumaNativeVec3*);
typedef unsigned char (__fastcall *DlumaNativeLocalEnabledFn)(void*);
typedef void (__fastcall *DlumaNativeLocalSetFn)(void*);
typedef unsigned char (__fastcall *DlumaNativeSensorInsideFn)(void*);
typedef char (__fastcall *DlumaNativeInteractiveExecuteFn)(
    void*, unsigned int, __int64*);
typedef void (__fastcall *DlumaNativeActionChainRunFn)(void*);
typedef struct DlumaNativeTtlString {
    const char* data;
    uint32_t length;
    uint32_t capacity;
} DlumaNativeTtlString;
typedef struct DlumaNativeSubtitleView {
    int32_t id;
    unsigned char enabled;
    unsigned char reserved[3];
    DlumaNativeTtlString speaker;
    DlumaNativeTtlString text;
    DlumaNativeTtlString combined;
    uint32_t priority;
    uint32_t reserved2;
} DlumaNativeSubtitleView;
typedef void (__fastcall *DlumaNativeLevelShowSubtitleFn)(
    void*, const DlumaNativeTtlString*, const DlumaNativeTtlString*,
    int32_t, void*, uint32_t);
typedef void (__fastcall *DlumaNativeLevelHideSubtitleFn)(void*, int32_t);
typedef void (__fastcall *DlumaNativeMovieShowSubtitleFn)(
    void*, const DlumaNativeSubtitleView*);
typedef void (__fastcall *DlumaNativeMovieHideSubtitleFn)(void*, int32_t);
typedef void* (__fastcall *DlumaNativeDialogFindGroupFn)(
    void*, const DlumaNativeTtlString*);
typedef unsigned char (__fastcall *DlumaNativeDialogSetQuestFn)(
    void*, void*, const DlumaNativeTtlString*);
typedef void* (__fastcall *DlumaNativeDialogManagerFindSpeakerFn)(
    void*, const DlumaNativeTtlString*, const char*);
typedef void* (__fastcall *DlumaNativeDialogManagerFindGroupFn)(
    void*, const DlumaNativeTtlString*, const char*);
typedef void* (__fastcall *DlumaNativeQuestFindFn)(
    void*, const DlumaNativeTtlString*);
typedef void* (__fastcall *DlumaNativeQuestFindPhaseFn)(
    void*, const DlumaNativeTtlString*);
typedef char (__fastcall *DlumaNativeQuestStartSynchronizedInvokeFn)(
    void*, void*, int, char, unsigned char);

typedef struct DlumaNativeCompleteObjectLocator {
    DWORD signature;
    DWORD offset;
    DWORD cd_offset;
    DWORD type_descriptor_rva;
    DWORD class_hierarchy_rva;
    DWORD self_rva;
} DlumaNativeCompleteObjectLocator;

typedef struct DlumaNativePmd {
    LONG member_displacement;
    LONG vbtable_displacement;
    LONG displacement_in_vbtable;
} DlumaNativePmd;

typedef struct DlumaNativeBaseClassDescriptor {
    DWORD type_descriptor_rva;
    DWORD contained_bases;
    DlumaNativePmd pmd;
    DWORD attributes;
    DWORD class_hierarchy_rva;
} DlumaNativeBaseClassDescriptor;

typedef struct DlumaNativeClassHierarchyDescriptor {
    DWORD signature;
    DWORD attributes;
    DWORD base_count;
    DWORD base_array_rva;
} DlumaNativeClassHierarchyDescriptor;

typedef struct DlumaNativeIndexEntry {
    void* object;
    void* complete;
    void* level;
    uint64_t index_epoch;
    int32_t object_id;
    uint32_t generation;
    char name[DLUMA_LUA54_MAX_OBJECT_NAME];
    char class_name[DLUMA_LUA54_MAX_CLASS_NAME];
} DlumaNativeIndexEntry;

typedef struct DlumaNativeHandle {
    uint64_t handle_id;
    uint64_t owner_id;
    uint32_t generation;
    unsigned char active;
    unsigned char expired;
    unsigned char reserved[6];
    DlumaNativeIndexEntry entry;
} DlumaNativeHandle;

typedef enum DlumaNativeSubscriptionState {
    DLUMA_NATIVE_SUB_PENDING = 0,
    DLUMA_NATIVE_SUB_RESOLVED = 1,
    DLUMA_NATIVE_SUB_NOT_FOUND = 2,
    DLUMA_NATIVE_SUB_WRONG_CLASS = 3
} DlumaNativeSubscriptionState;

typedef enum DlumaNativeOwnerRejectReason {
    DLUMA_NATIVE_OWNER_ACCEPTED = 0,
    DLUMA_NATIVE_OWNER_LEVEL_MISSING = 1,
    DLUMA_NATIVE_OWNER_ID_INVALID = 2,
    DLUMA_NATIVE_OWNER_NAME_UNAVAILABLE = 3,
    DLUMA_NATIVE_OWNER_NAME_MISMATCH = 4
} DlumaNativeOwnerRejectReason;

typedef struct DlumaNativeSubscription {
    uint64_t owner_id;
    uint64_t next_retry_ms;
    uint64_t registration_id;
    uint64_t index_epoch;
    uint64_t key_revision;
    uint32_t generation;
    uint64_t token;
    uint64_t callback_id;
    uint32_t kind;
    uint32_t index;
    unsigned char active;
    unsigned char resolution_state;
    unsigned char initial_sample;
    unsigned char inside;
    char name[DLUMA_LUA54_MAX_OBJECT_NAME];
} DlumaNativeSubscription;

typedef struct DlumaNativeHookEvent {
    uint64_t index_epoch;
    uint64_t subscription_key_revision;
    uint32_t generation;
    char name[DLUMA_LUA54_MAX_OBJECT_NAME];
} DlumaNativeHookEvent;

typedef struct DlumaNativeInteractionCounters {
    volatile LONG hook_calls;
    volatile LONG action_execute;
    volatile LONG successful_original;
    volatile LONG complete_object_matches;
    volatile LONG subscription_matches;
    volatile LONG events_queued;
    volatile LONG events_dropped;
} DlumaNativeInteractionCounters;

typedef struct DlumaNativeProductionBackend {
    HMODULE engine;
    HMODULE gamedll;
    BYTE* engine_base;
    BYTE* gamedll_base;
    DWORD engine_image_size;
    DWORD gamedll_image_size;
    DlumaNativeGetObjectByIdFn get_object_by_id;
    DlumaNativeObjectLinkFn get_prev;
    DlumaNativeObjectLinkFn get_next;
    DlumaNativeObjectLevelFn get_level;
    DlumaNativeObjectIdFn get_id;
    DlumaNativeClassNameFn get_class_name;
    DlumaNativeFromClassFn from_class_name;
    DlumaNativeQuestFindFn quest_find;
    DlumaNativeQuestFindPhaseFn quest_find_phase;
    DlumaNativeQuestStartSynchronizedInvokeFn quest_start_synchronized_invoke;
} DlumaNativeProductionBackend;

typedef struct DlumaNativeGameplayState {
    DlumaNativeGameplayBackend backend;
    DlumaNativeProductionBackend production;
    uint32_t readiness;
    uint32_t static_readiness;
    uint32_t generation;
    uint32_t seed_cursor;
    uint32_t index_count;
    uint32_t builder_count;
    uint32_t visited_count;
    uint32_t reject_samples;
    uint32_t accepted_samples;
    uint32_t last_progress_cursor;
    uint32_t recovery_attempts;
    uint32_t subscription_count;
    uint32_t hook_event_head;
    uint32_t hook_event_count;
    uint64_t next_handle_id;
    uint64_t next_subscription_id;
    uint64_t next_subscription_revision;
    uint64_t index_epoch;
    uint64_t last_sensor_poll_ms;
    uint64_t index_not_before_ms;
    uint64_t next_recovery_ms;
    void* level;
    void* live_self_test_seed;
    int32_t live_self_test_id;
    void** interactive_slot;
    void* interactive_original;
    volatile LONG hook_active_calls;
    volatile LONG stopping;
    volatile LONG match_lock;
    volatile LONG hook_event_lock;
    volatile LONG index_refresh_requested;
    DlumaNativeCapabilityState capability_states[DLUMA_NATIVE_CAPABILITY_COUNT];
    DlumaNativeIndexPhase index_phase;
    unsigned char started;
    unsigned char production_backend;
    unsigned char seed_scan_logged;
    unsigned char seed_found_logged;
    unsigned char hook_installed;
    unsigned char hook_overflow_logged;
    unsigned char interaction_summary_logged;
    unsigned char interaction_log_samples;
    unsigned char subtitle_active;
    unsigned char subtitle_force;
    unsigned char subtitle_level_compatible_vtable_logged;
    unsigned char subtitle_movie_compatible_vtable_logged;
    unsigned char subtitle_validation_failure_logged;
    uint64_t subtitle_expires_ms;
    char map[DLUMA_MAX_MAP_NAME];
    DlumaNativeIndexEntry index[DLUMA_NATIVE_MAX_INDEX];
    void* visited[DLUMA_NATIVE_MAX_VISITED];
    DlumaNativeIndexDiagnostics diagnostics;
    DlumaNativeHandle handles[DLUMA_NATIVE_MAX_HANDLES];
    DlumaNativeSubscription subscriptions[DLUMA_NATIVE_MAX_SUBSCRIPTIONS];
    DlumaNativeHookEvent hook_events[DLUMA_NATIVE_MAX_HOOK_EVENTS];
    DlumaNativeInteractionCounters interaction;
} DlumaNativeGameplayState;

static DlumaNativeGameplayState g_dluma_native;
static volatile LONG g_dluma_native_stop_ok;
/* The observer can be fetched from the game vtable immediately before
 * quiesce and entered after normal state teardown.  Pinning the module and
 * retaining this pass-through target outside the resettable state guarantees
 * that such a delayed entry still calls the original exactly once. */
static PVOID volatile g_dluma_interactive_original_lifetime;
static volatile LONG g_dluma_interactive_observation_stopped = 1;
static volatile LONG g_dluma_interactive_lifecycle_lock;

static int dluma_native_interactive_hook_current(void);
static int dluma_native_interactive_hook_owned_locked(void);

static void dluma_native_lock(volatile LONG* lock)
{
    while (dluma_atomic_compare_exchange(lock, 1, 0) != 0) Sleep(0);
}

static int dluma_native_try_lock(volatile LONG* lock)
{
    return dluma_atomic_compare_exchange(lock, 1, 0) == 0;
}

static void dluma_native_unlock(volatile LONG* lock)
{
    dluma_atomic_exchange(lock, 0);
}

static int dluma_native_capability_index(uint32_t readiness_bit)
{
    if (readiness_bit == DLUMA_NATIVE_READY_WORLD_FIND) return 0;
    if (readiness_bit == DLUMA_NATIVE_READY_LOCAL_ENABLE) return 1;
    if (readiness_bit == DLUMA_NATIVE_READY_SENSOR) return 2;
    if (readiness_bit == DLUMA_NATIVE_READY_INTERACT) return 3;
    if (readiness_bit == DLUMA_NATIVE_READY_SUBTITLES) return 4;
    if (readiness_bit == DLUMA_NATIVE_READY_RADIO_DIALOG) return 5;
    if (readiness_bit == DLUMA_NATIVE_READY_ACTION_CHAIN) return 6;
    if (readiness_bit == DLUMA_NATIVE_READY_QUEST_MUTATION) return 7;
    if (readiness_bit == DLUMA_NATIVE_READY_WORLD_POSITION) return 8;
    if (readiness_bit == DLUMA_NATIVE_READY_QUEST_HUD) return 9;
    return -1;
}

static void dluma_native_refresh_readiness(void)
{
    static const uint32_t bits[DLUMA_NATIVE_CAPABILITY_COUNT] = {
        DLUMA_NATIVE_READY_WORLD_FIND,
        DLUMA_NATIVE_READY_LOCAL_ENABLE,
        DLUMA_NATIVE_READY_SENSOR,
        DLUMA_NATIVE_READY_INTERACT,
        DLUMA_NATIVE_READY_SUBTITLES,
        DLUMA_NATIVE_READY_RADIO_DIALOG,
        DLUMA_NATIVE_READY_ACTION_CHAIN,
        DLUMA_NATIVE_READY_QUEST_MUTATION,
        DLUMA_NATIVE_READY_WORLD_POSITION,
        DLUMA_NATIVE_READY_QUEST_HUD
    };
    uint32_t readiness = 0;
    unsigned int i;
    for (i = 0; i < DLUMA_ARRAY_COUNT(bits); ++i)
        if (g_dluma_native.capability_states[i] == DLUMA_NATIVE_CAP_READY)
            readiness |= bits[i];
    dluma_atomic_exchange((volatile LONG*)&g_dluma_native.readiness, (LONG)readiness);
}

static void dluma_native_set_capability_state(
    uint32_t readiness_bit, DlumaNativeCapabilityState state)
{
    int index = dluma_native_capability_index(readiness_bit);
    if (index < 0) return;
    if (!(g_dluma_native.static_readiness & readiness_bit))
        state = DLUMA_NATIVE_CAP_STATIC_UNSUPPORTED;
    g_dluma_native.capability_states[index] = state;
    dluma_native_refresh_readiness();
}

static void dluma_native_set_supported_state(DlumaNativeCapabilityState state)
{
    static const uint32_t bits[DLUMA_NATIVE_CAPABILITY_COUNT] = {
        DLUMA_NATIVE_READY_WORLD_FIND,
        DLUMA_NATIVE_READY_LOCAL_ENABLE,
        DLUMA_NATIVE_READY_SENSOR,
        DLUMA_NATIVE_READY_INTERACT,
        DLUMA_NATIVE_READY_SUBTITLES,
        DLUMA_NATIVE_READY_RADIO_DIALOG,
        DLUMA_NATIVE_READY_ACTION_CHAIN,
        DLUMA_NATIVE_READY_QUEST_MUTATION,
        DLUMA_NATIVE_READY_WORLD_POSITION,
        DLUMA_NATIVE_READY_QUEST_HUD
    };
    unsigned int i;
    for (i = 0; i < DLUMA_ARRAY_COUNT(bits); ++i)
        g_dluma_native.capability_states[i] =
            (g_dluma_native.static_readiness & bits[i])
                ? state : DLUMA_NATIVE_CAP_STATIC_UNSUPPORTED;
    /* The copied HUD adapter has its own exact-profile hook lifecycle and
     * does not depend on the world object index. An index rebuild must not
     * leave an installed HUD adapter stuck in LIVE_SELF_TESTING forever. */
    if ((g_dluma_native.static_readiness & DLUMA_NATIVE_READY_QUEST_HUD) &&
        dluma_quest_hud_is_available())
        g_dluma_native.capability_states[
            dluma_native_capability_index(DLUMA_NATIVE_READY_QUEST_HUD)] =
                DLUMA_NATIVE_CAP_READY;
    dluma_native_refresh_readiness();
}

static void dluma_native_refresh_interaction_capability(void)
{
    DlumaNativeCapabilityState previous;
    int current;
    if (!(g_dluma_native.static_readiness & DLUMA_NATIVE_READY_INTERACT) ||
        g_dluma_native.index_phase != DLUMA_NATIVE_INDEX_COMPLETE) return;
    /* This is a frame-pump health check, never a synchronization barrier.
     * Hook install/restore may briefly own the lifecycle lock on another
     * thread; waiting here would freeze all Lua timers and callbacks. */
    if (!dluma_native_try_lock(&g_dluma_interactive_lifecycle_lock)) return;
    current = dluma_native_interactive_hook_owned_locked();
    if (!current)
        dluma_atomic_exchange(&g_dluma_interactive_observation_stopped, 1);
    if (!dluma_native_try_lock(&g_dluma_native.match_lock)) {
        dluma_native_unlock(&g_dluma_interactive_lifecycle_lock);
        return;
    }
    if (g_dluma_native.index_phase != DLUMA_NATIVE_INDEX_COMPLETE) {
        dluma_native_unlock(&g_dluma_native.match_lock);
        dluma_native_unlock(&g_dluma_interactive_lifecycle_lock);
        return;
    }
    previous = g_dluma_native.capability_states[
        dluma_native_capability_index(DLUMA_NATIVE_READY_INTERACT)];
    dluma_native_set_capability_state(
        DLUMA_NATIVE_READY_INTERACT,
        current ? DLUMA_NATIVE_CAP_READY : DLUMA_NATIVE_CAP_DEGRADED);
    dluma_native_unlock(&g_dluma_native.match_lock);
    if (current)
        dluma_atomic_exchange(&g_dluma_interactive_observation_stopped, 0);
    dluma_native_unlock(&g_dluma_interactive_lifecycle_lock);
    if (previous == DLUMA_NATIVE_CAP_READY && !current)
        dluma_log_line("WARN", "interactive_item_hook_lost; events.interact capability degraded.");
    else if (previous != DLUMA_NATIVE_CAP_READY && current)
        dluma_log_line("INFO", "interactive_item_hook_recovered; events.interact capability ready.");
}

static DWORD dluma_native_image_size_for_base(BYTE* image_base)
{
    if (image_base == g_dluma_native.production.engine_base)
        return g_dluma_native.production.engine_image_size;
    if (image_base == g_dluma_native.production.gamedll_base)
        return g_dluma_native.production.gamedll_image_size;
    return 0;
}

static int dluma_native_is_executable(const void* address, SIZE_T size);

static int dluma_native_image_range(
    BYTE* image_base, DWORD rva, SIZE_T size)
{
    DWORD image_size = dluma_native_image_size_for_base(image_base);
    if (!image_base || !image_size || rva >= image_size ||
        size > (SIZE_T)(image_size - rva)) return 0;
    return dluma_is_readable(image_base + rva, size);
}

/* Gameplay GetRTTI is virtual.  The IGSObject base vtable points at the
 * exported IGSObject::GetRTTI stub, but a derived gameplay class points at
 * its own generated stub.  Every audited x64 gameplay stub has the same
 * side-effect-free shape:
 *
 *     48 8D 05 <rel32> C3    lea rax,[rip+rel32]; ret
 *
 * Requiring the base function address here rejects every derived object.
 * Decode the class-specific stub instead, keep both code and resolved CRTTI
 * inside the exact fingerprinted image, and reject the +0x18 GetEngineRTTI
 * MOV form (48 8B 05 ... C3).
 */
static int dluma_native_decode_gameplay_rtti_getter(
    const BYTE* image_base,
    DWORD image_size,
    const void* target,
    int require_executable,
    void** expected_rtti)
{
    const BYTE* code = (const BYTE*)target;
    uintptr_t base_address = (uintptr_t)image_base;
    uintptr_t code_address = (uintptr_t)code;
    uintptr_t next_instruction;
    uintptr_t rtti_address;
    LONG displacement;
    if (expected_rtti) *expected_rtti = 0;
    if (!image_base || image_size < DLUMA_NATIVE_RTTI_READABLE_SIZE || !code ||
        code_address < base_address ||
        code_address - base_address >
            (uintptr_t)image_size - DLUMA_NATIVE_GAMEPLAY_RTTI_GETTER_SIZE ||
        !dluma_is_readable(code, DLUMA_NATIVE_GAMEPLAY_RTTI_GETTER_SIZE) ||
        (require_executable &&
         !dluma_native_is_executable(
             code, DLUMA_NATIVE_GAMEPLAY_RTTI_GETTER_SIZE)) ||
        code[0] != 0x48 || code[1] != 0x8D || code[2] != 0x05 ||
        code[7] != 0xC3)
        return 0;
    memcpy(&displacement, code + 3, sizeof(displacement));
    next_instruction = code_address + DLUMA_NATIVE_GAMEPLAY_RTTI_GETTER_SIZE - 1u;
    if (displacement >= 0) {
        if (next_instruction > UINTPTR_MAX - (uintptr_t)displacement)
            return 0;
        rtti_address = next_instruction + (uintptr_t)displacement;
    } else {
        uintptr_t magnitude = (uintptr_t)(-(int64_t)displacement);
        if (next_instruction < magnitude) return 0;
        rtti_address = next_instruction - magnitude;
    }
    if (rtti_address < base_address ||
        rtti_address - base_address >
            (uintptr_t)image_size - DLUMA_NATIVE_RTTI_READABLE_SIZE ||
        !dluma_is_readable((const void*)rtti_address,
                         DLUMA_NATIVE_RTTI_READABLE_SIZE))
        return 0;
    if (expected_rtti) *expected_rtti = (void*)rtti_address;
    return 1;
}

static BYTE* dluma_native_known_image_for_code(
    const void* address, DWORD* image_size)
{
    uintptr_t value = (uintptr_t)address;
    uintptr_t engine = (uintptr_t)g_dluma_native.production.engine_base;
    uintptr_t gamedll = (uintptr_t)g_dluma_native.production.gamedll_base;
    if (image_size) *image_size = 0;
    if (engine && value >= engine &&
        value - engine < g_dluma_native.production.engine_image_size) {
        if (image_size)
            *image_size = g_dluma_native.production.engine_image_size;
        return g_dluma_native.production.engine_base;
    }
    if (gamedll && value >= gamedll &&
        value - gamedll < g_dluma_native.production.gamedll_image_size) {
        if (image_size)
            *image_size = g_dluma_native.production.gamedll_image_size;
        return g_dluma_native.production.gamedll_base;
    }
    return 0;
}

static void dluma_native_error(
    DlumaLua54Error* error,
    const char* code,
    const char* message,
    const char* capability,
    const char* permission,
    const char* object_name,
    int retryable)
{
    if (!error) return;
    memset(error, 0, sizeof(*error));
    error->struct_size = sizeof(*error);
    error->retryable = retryable ? 1u : 0u;
    dluma_str_copy(error->code, DLUMA_ARRAY_COUNT(error->code), code ? code : "native_error");
    dluma_str_copy(error->message, DLUMA_ARRAY_COUNT(error->message), message ? message : "native adapter error");
    dluma_str_copy(error->capability, DLUMA_ARRAY_COUNT(error->capability), capability ? capability : "");
    dluma_str_copy(error->permission, DLUMA_ARRAY_COUNT(error->permission), permission ? permission : "");
    dluma_str_copy(error->object_name, DLUMA_ARRAY_COUNT(error->object_name), object_name ? object_name : "");
}

static int dluma_native_exact_name_valid(const char* name)
{
    unsigned int i;
    if (!name || !name[0]) return 0;
    for (i = 0; i < DLUMA_LUA54_MAX_OBJECT_NAME; ++i) {
        unsigned char c = (unsigned char)name[i];
        if (!c) return i > 0;
        if (c < 32u || c == 127u) return 0;
    }
    return 0;
}

static int dluma_native_quest_identifier_valid(const char* value)
{
    unsigned int i;
    if (!value || !value[0]) return 0;
    for (i = 0; i < DLUMA_LUA54_MAX_QUEST_ID; ++i) {
        char c = value[i];
        if (!c) return i > 0;
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '_' || c == '-' ||
              c == '.')) return 0;
    }
    return 0;
}

static int dluma_native_quest_optional_identifier_valid(const char* value)
{
    if (!value || !value[0]) return 1;
    return dluma_native_quest_identifier_valid(value);
}

static int dluma_native_streq(const char* left, const char* right)
{
    unsigned int i = 0;
    if (!left || !right) return left == right;
    while (left[i] && right[i]) {
        if (left[i] != right[i]) return 0;
        ++i;
    }
    return left[i] == right[i];
}

static int dluma_native_bounded_copy_from_memory(
    const char* source, char* out, unsigned int capacity)
{
    unsigned int i;
    if (!source || !out || capacity < 2) return 0;
    for (i = 0; i + 1 < capacity; ++i) {
        unsigned char c;
        if (!dluma_is_readable(source + i, 1)) return 0;
        c = (unsigned char)source[i];
        if (!c) {
            out[i] = 0;
            return i > 0;
        }
        if (c < 32u || c == 127u) return 0;
        out[i] = (char)c;
    }
    out[0] = 0;
    return 0;
}

static int dluma_native_get_pe_identity(
    HMODULE module, DWORD* timestamp, DWORD* image_size)
{
    BYTE* base = (BYTE*)module;
    DWORD pe_offset;
    BYTE* nt;
    if (!base || !timestamp || !image_size || !dluma_is_readable(base, 0x100)) return 0;
    if (base[0] != 'M' || base[1] != 'Z') return 0;
    pe_offset = *(DWORD*)(base + 0x3C);
    nt = base + pe_offset;
    if (!dluma_is_readable(nt, 0x100) || *(DWORD*)nt != 0x00004550UL) return 0;
    *timestamp = *(DWORD*)(nt + 8);
    *image_size = *(DWORD*)(nt + 24 + 56);
    return 1;
}

static int dluma_native_is_executable(const void* address, SIZE_T size)
{
    MEMORY_BASIC_INFORMATION info;
    DWORD protection;
    BYTE* end;
    if (!address || !size || !VirtualQuery(address, &info, sizeof(info))) return 0;
    if (info.State != MEM_COMMIT || (info.Protect & (PAGE_GUARD | PAGE_NOACCESS))) return 0;
    end = (BYTE*)address + size;
    if (end < (BYTE*)address || end > (BYTE*)info.BaseAddress + info.RegionSize) return 0;
    protection = info.Protect & 0xFFu;
    return protection == PAGE_EXECUTE || protection == PAGE_EXECUTE_READ ||
           protection == PAGE_EXECUTE_READWRITE || protection == PAGE_EXECUTE_WRITECOPY;
}

static int dluma_native_prefix_matches(
    const void* address, const BYTE* expected, unsigned int size)
{
    return dluma_native_is_executable(address, size) &&
           dluma_is_readable(address, size) && memcmp(address, expected, size) == 0;
}

static int dluma_native_export_at(
    HMODULE module,
    const char* decorated_name,
    DWORD expected_rva,
    const BYTE* expected_prefix,
    unsigned int expected_prefix_size,
    void** out)
{
    FARPROC proc;
    if (!module || !decorated_name || !expected_prefix ||
        !expected_prefix_size || !out) return 0;
    proc = GetProcAddress(module, decorated_name);
    if (!proc || (BYTE*)proc - (BYTE*)module != expected_rva ||
        !dluma_native_prefix_matches(
            (void*)proc, expected_prefix, expected_prefix_size)) {
        dluma_log_pair("WARN", "native_adapter_world_binding_validation_failed",
                     decorated_name);
        return 0;
    }
    *out = (void*)proc;
    return 1;
}

static BYTE* dluma_native_col_image_base(
    void* interface_pointer,
    DlumaNativeCompleteObjectLocator** col_out)
{
    void** vtable;
    DlumaNativeCompleteObjectLocator* col;
    BYTE* image_base;
    if (!interface_pointer || !dluma_is_readable(interface_pointer, sizeof(void*))) return 0;
    vtable = *(void***)interface_pointer;
    if (!vtable || !dluma_is_readable(vtable - 1, sizeof(void*))) return 0;
    col = (DlumaNativeCompleteObjectLocator*)vtable[-1];
    if (!col || !dluma_is_readable(col, sizeof(*col)) || col->signature != 1 ||
        col->self_rva == 0 || col->cd_offset != 0 ||
        (uintptr_t)col < (uintptr_t)col->self_rva)
        return 0;
    image_base = (BYTE*)col - col->self_rva;
    if (image_base != g_dluma_native.production.engine_base &&
        image_base != g_dluma_native.production.gamedll_base)
        return 0;
    if (!dluma_native_image_range(image_base, col->self_rva, sizeof(*col)) ||
        !dluma_native_image_range(image_base, col->type_descriptor_rva, 17) ||
        !dluma_native_image_range(image_base, col->class_hierarchy_rva,
                                sizeof(DlumaNativeClassHierarchyDescriptor)))
        return 0;
    if (col_out) *col_out = col;
    return image_base;
}

static uint64_t dluma_native_prod_clock(void* user)
{
    (void)user;
    return GetTickCount64();
}

static void* dluma_native_prod_active_level(
    void* user, char* map_out, uint32_t map_capacity)
{
    (void)user;
    if (!map_out || !map_capacity || !g_dluma.engine.ready ||
        !dluma_engine_query_level(&g_dluma.engine, map_out, map_capacity))
        return 0;
    return g_dluma.engine.last_level_interface;
}

static void* dluma_native_prod_object_by_id(void* user, void* level, int32_t object_id)
{
    DlumaNativeProductionBackend* backend = (DlumaNativeProductionBackend*)user;
    if (!backend || !backend->get_object_by_id || !level ||
        !dluma_is_readable(level, sizeof(void*))) return 0;
    return backend->get_object_by_id(level, object_id);
}

static void* dluma_native_prod_object_prev(void* user, void* object)
{
    DlumaNativeProductionBackend* backend = (DlumaNativeProductionBackend*)user;
    if (!backend || !backend->get_prev || !object ||
        !dluma_is_readable(object, sizeof(void*))) return 0;
    return backend->get_prev(object);
}

static void* dluma_native_prod_object_next(void* user, void* object)
{
    DlumaNativeProductionBackend* backend = (DlumaNativeProductionBackend*)user;
    if (!backend || !backend->get_next || !object ||
        !dluma_is_readable(object, sizeof(void*))) return 0;
    return backend->get_next(object);
}

static void* dluma_native_prod_object_level(void* user, void* object)
{
    DlumaNativeProductionBackend* backend = (DlumaNativeProductionBackend*)user;
    if (!backend || !backend->get_level || !object ||
        !dluma_is_readable(object, sizeof(void*))) return 0;
    return backend->get_level(object);
}

static int dluma_native_prod_level_name(
    void* user, void* level, char* out, uint32_t capacity)
{
    const char* raw;
    char bounded[DLUMA_MAX_MAP_NAME * 2];
    (void)user;
    if (!level || !out || !capacity || !g_dluma.engine.get_level_name ||
        !dluma_is_readable(level, sizeof(void*))) return 0;
    raw = g_dluma.engine.get_level_name(level);
    if (!dluma_native_bounded_copy_from_memory(
            raw, bounded, DLUMA_ARRAY_COUNT(bounded))) return 0;
    dluma_normalize_map_name(bounded, out, capacity);
    return out[0] != 0;
}

static int32_t dluma_native_prod_object_id(void* user, void* object)
{
    DlumaNativeProductionBackend* backend = (DlumaNativeProductionBackend*)user;
    if (!backend || !backend->get_id || !object ||
        !dluma_is_readable(object, sizeof(void*))) return -1;
    return backend->get_id(object);
}

static int dluma_native_prod_object_name(
    void* user, void* object, char* out, uint32_t capacity)
{
    void* record;
    const char* name;
    (void)user;
    if (!object || !out || capacity > DLUMA_LUA54_MAX_OBJECT_NAME ||
        !dluma_is_readable((BYTE*)object + DLUMA_NATIVE_OBJECT_NAME_RECORD_OFFSET,
                         sizeof(void*))) return 0;
    record = *(void**)((BYTE*)object + DLUMA_NATIVE_OBJECT_NAME_RECORD_OFFSET);
    if (!record || !dluma_is_readable(
            (BYTE*)record + DLUMA_NATIVE_NAME_POINTER_OFFSET, sizeof(void*))) return 0;
    name = *(const char**)((BYTE*)record + DLUMA_NATIVE_NAME_POINTER_OFFSET);
    return dluma_native_bounded_copy_from_memory(name, out, capacity);
}

static void* dluma_native_prod_igsobject_gameplay_rtti(void* object)
{
    void** vtable;
    DlumaNativeDynamicRttiFn get_rtti;
    BYTE* getter_image;
    DWORD getter_image_size;
    void* expected_rtti;
    void* rtti;
    if (!object || !dluma_is_readable(object, sizeof(void*))) return 0;
    vtable = *(void***)object;
    if (!vtable || !dluma_is_readable(
            (BYTE*)vtable + DLUMA_NATIVE_IGSOBJECT_GET_RTTI_SLOT,
            sizeof(void*))) return 0;
    get_rtti = *(DlumaNativeDynamicRttiFn*)((BYTE*)vtable +
                                          DLUMA_NATIVE_IGSOBJECT_GET_RTTI_SLOT);
    getter_image = dluma_native_known_image_for_code(
        (const void*)get_rtti, &getter_image_size);
    if (!getter_image ||
        !dluma_native_decode_gameplay_rtti_getter(
            getter_image, getter_image_size, (const void*)get_rtti,
            1, &expected_rtti))
        return 0;
    rtti = get_rtti(object);
    return rtti == expected_rtti ? rtti : 0;
}

static int dluma_native_prod_object_class(
    void* user, void* object, char* out, uint32_t capacity)
{
    DlumaNativeProductionBackend* backend = (DlumaNativeProductionBackend*)user;
    void* rtti;
    const char* name;
    if (!backend || !backend->get_class_name || !out ||
        capacity > DLUMA_LUA54_MAX_CLASS_NAME) return 0;
    rtti = dluma_native_prod_igsobject_gameplay_rtti(object);
    if (!rtti || !dluma_is_readable(rtti, sizeof(void*))) return 0;
    name = backend->get_class_name(rtti);
    return dluma_native_bounded_copy_from_memory(name, out, capacity);
}

static int dluma_native_prod_object_is_a(
    void* user, void* object, const char* class_name)
{
    DlumaNativeProductionBackend* backend = (DlumaNativeProductionBackend*)user;
    void* rtti;
    if (!backend || !backend->from_class_name ||
        !dluma_native_exact_name_valid(class_name)) return 0;
    rtti = dluma_native_prod_igsobject_gameplay_rtti(object);
    if (!rtti || !dluma_is_readable(rtti, sizeof(void*))) return 0;
    return backend->from_class_name(rtti, class_name) != 0;
}

static void* dluma_native_prod_complete_object(void* user, void* object)
{
    DlumaNativeCompleteObjectLocator* col = 0;
    (void)user;
    if (!dluma_native_col_image_base(object, &col) ||
        (uintptr_t)object < (uintptr_t)col->offset ||
        !dluma_is_readable((BYTE*)object - col->offset, sizeof(void*))) return 0;
    return (BYTE*)object - col->offset;
}

static BYTE* dluma_native_add_bounded_signed_offset(BYTE* base, LONG offset)
{
    uintptr_t address;
    uintptr_t magnitude;
    if (!base || offset < -0x00100000L || offset > 0x00100000L) return 0;
    address = (uintptr_t)base;
    if (offset >= 0) {
        if (address > UINTPTR_MAX - (uintptr_t)offset) return 0;
        return (BYTE*)(address + (uintptr_t)offset);
    }
    magnitude = (uintptr_t)(-(int64_t)offset);
    if (address < magnitude) return 0;
    return (BYTE*)(address - magnitude);
}

static void* dluma_native_prod_resolve_base_interface(
    void* object, const char* decorated_type_name, SIZE_T decorated_size)
{
    DlumaNativeCompleteObjectLocator* col = 0;
    DlumaNativeClassHierarchyDescriptor* hierarchy;
    DWORD* base_array;
    BYTE* image_base;
    BYTE* complete;
    DWORD i;
    if (!decorated_type_name || decorated_size < 2) return 0;
    image_base = dluma_native_col_image_base(object, &col);
    if (!image_base || col->offset > 0x00100000UL ||
        (uintptr_t)object < (uintptr_t)col->offset) return 0;
    complete = (BYTE*)object - col->offset;
    hierarchy = (DlumaNativeClassHierarchyDescriptor*)(
        image_base + col->class_hierarchy_rva);
    if (!dluma_is_readable(hierarchy, sizeof(*hierarchy)) ||
        hierarchy->base_count == 0 || hierarchy->base_count > 256u ||
        !dluma_native_image_range(image_base, hierarchy->base_array_rva,
                                hierarchy->base_count * sizeof(DWORD))) return 0;
    base_array = (DWORD*)(image_base + hierarchy->base_array_rva);
    for (i = 0; i < hierarchy->base_count; ++i) {
        DlumaNativeBaseClassDescriptor* descriptor;
        const char* type_name;
        BYTE* interface_pointer;
        void** vtable;
        if (!dluma_native_image_range(image_base, base_array[i],
                                    sizeof(*descriptor)))
            continue;
        descriptor = (DlumaNativeBaseClassDescriptor*)(image_base + base_array[i]);
        if (!dluma_native_image_range(image_base, descriptor->type_descriptor_rva,
                                    16u + decorated_size)) continue;
        type_name = (const char*)(image_base + descriptor->type_descriptor_rva + 16);
        if (memcmp(type_name, decorated_type_name, decorated_size) != 0 ||
            descriptor->pmd.member_displacement < -0x00100000L ||
            descriptor->pmd.member_displacement > 0x00100000L)
            continue;
        interface_pointer = dluma_native_add_bounded_signed_offset(
            complete, descriptor->pmd.member_displacement);
        if (!interface_pointer) return 0;
        if (descriptor->pmd.vbtable_displacement != -1) {
            BYTE* vbptr_address;
            BYTE* vbtable;
            BYTE* displacement_address;
            LONG virtual_displacement;
            if (descriptor->pmd.vbtable_displacement < -0x00100000L ||
                descriptor->pmd.vbtable_displacement > 0x00100000L ||
                descriptor->pmd.displacement_in_vbtable < -0x00100000L ||
                descriptor->pmd.displacement_in_vbtable > 0x00100000L) return 0;
            vbptr_address = dluma_native_add_bounded_signed_offset(
                complete, descriptor->pmd.vbtable_displacement);
            if (!vbptr_address ||
                !dluma_is_readable(vbptr_address, sizeof(void*))) return 0;
            vbtable = *(BYTE**)vbptr_address;
            displacement_address = dluma_native_add_bounded_signed_offset(
                vbtable, descriptor->pmd.displacement_in_vbtable);
            if (!displacement_address ||
                !dluma_is_readable(displacement_address, sizeof(LONG))) return 0;
            virtual_displacement = *(LONG*)displacement_address;
            interface_pointer = dluma_native_add_bounded_signed_offset(
                interface_pointer, virtual_displacement);
            if (!interface_pointer) return 0;
        }
        if (!dluma_is_readable(interface_pointer, sizeof(void*))) return 0;
        vtable = *(void***)interface_pointer;
        if (!vtable || !dluma_is_readable(vtable, sizeof(void*))) return 0;
        return interface_pointer;
    }
    return 0;
}

static void* dluma_native_prod_local_enable_interface(void* user, void* object)
{
    (void)user;
    return dluma_native_prod_resolve_base_interface(
        object, ".?AVILocalEnable@@", sizeof(".?AVILocalEnable@@"));
}

static void* dluma_native_prod_control_object_interface(void* user, void* object)
{
    (void)user;
    return dluma_native_prod_resolve_base_interface(
        object, ".?AVIControlObject@@", sizeof(".?AVIControlObject@@"));
}

static int dluma_native_position_is_sane(float value)
{
    uint32_t bits;
    memcpy(&bits, &value, sizeof(bits));
    return (bits & 0x7F800000u) != 0x7F800000u &&
           (bits & 0x7FFFFFFFu) <= 0x49742400u; /* 1,000,000.0f */
}

/* The loader deliberately links without the CRT, so direction normalization
 * must not introduce sqrtf/fabs/isfinite imports.  The source vector is
 * already bounded; a fixed binary search in [0, 10] is deterministic and
 * leaves no platform math-library dependency. */
static float dluma_native_direction_length(float squared_length)
{
    float low = 0.0f;
    float high = 10.0f;
    unsigned int iteration;
    for (iteration = 0; iteration < 28u; ++iteration) {
        float middle = (low + high) * 0.5f;
        if (middle * middle < squared_length) low = middle;
        else high = middle;
    }
    return (low + high) * 0.5f;
}

static int dluma_native_prod_world_get_position(
    void* user, void* control_object_interface, DlumaLua54Vec3* position)
{
    DlumaNativeProductionBackend* production =
        (DlumaNativeProductionBackend*)user;
    DlumaNativeControlGetWorldPositionFn function;
    DlumaNativeVec3 raw;
    void** vtable;
    if (!production || !production->engine_base || !position ||
        !control_object_interface ||
        !dluma_is_readable(control_object_interface, sizeof(void*)) ||
        !dluma_is_readable(position, sizeof(*position))) return 0;
    vtable = *(void***)control_object_interface;
    if (!vtable || !dluma_is_readable(vtable, sizeof(void*)) ||
        !dluma_native_is_executable(*vtable, 1)) return 0;
    function = (DlumaNativeControlGetWorldPositionFn)(
        production->engine_base + DLUMA_NATIVE_CONTROL_GET_WORLD_POSITION_RVA);
    if (!dluma_native_prefix_matches((void*)function,
            g_dluma_control_get_world_position_prefix,
            DLUMA_ARRAY_COUNT(g_dluma_control_get_world_position_prefix))) return 0;
    memset(&raw, 0, sizeof(raw));
    if (function(control_object_interface, &raw) != &raw ||
        !dluma_native_position_is_sane(raw.x) ||
        !dluma_native_position_is_sane(raw.y) ||
        !dluma_native_position_is_sane(raw.z)) return 0;
    position->struct_size = sizeof(*position);
    position->x = raw.x;
    position->y = raw.y;
    position->z = raw.z;
    return 1;
}

static int dluma_native_prod_world_get_forward(
    void* user, void* control_object_interface, DlumaLua54Vec3* forward)
{
    DlumaNativeProductionBackend* production =
        (DlumaNativeProductionBackend*)user;
    DlumaNativeControlGetForwardVectorFn function;
    DlumaNativeVec3 raw;
    void** vtable;
    float length;
    float squared_length;
    if (!production || !production->engine_base || !forward ||
        !control_object_interface ||
        !dluma_is_readable(control_object_interface, sizeof(void*)) ||
        !dluma_is_readable(forward, sizeof(*forward))) return 0;
    vtable = *(void***)control_object_interface;
    if (!vtable || !dluma_is_readable(vtable, sizeof(void*)) ||
        !dluma_native_is_executable(*vtable, 1)) return 0;
    function = (DlumaNativeControlGetForwardVectorFn)(
        production->engine_base + DLUMA_NATIVE_CONTROL_GET_FORWARD_VECTOR_RVA);
    if (!dluma_native_prefix_matches((void*)function,
            g_dluma_control_get_forward_vector_prefix,
            DLUMA_ARRAY_COUNT(g_dluma_control_get_forward_vector_prefix))) return 0;
    memset(&raw, 0, sizeof(raw));
    if (function(control_object_interface, &raw) != &raw ||
        !dluma_native_position_is_sane(raw.x) ||
        !dluma_native_position_is_sane(raw.y) ||
        !dluma_native_position_is_sane(raw.z)) return 0;
    squared_length = raw.x * raw.x + raw.y * raw.y + raw.z * raw.z;
    if (!dluma_native_position_is_sane(squared_length) ||
        squared_length < 0.00000001f || squared_length > 100.0f) return 0;
    length = dluma_native_direction_length(squared_length);
    if (length < 0.0001f || length > 10.0f) return 0;
    forward->struct_size = sizeof(*forward);
    forward->x = raw.x / length;
    forward->y = raw.y / length;
    forward->z = raw.z / length;
    return 1;
}

static void* dluma_native_prod_sensor_area_interface(void* user, void* object)
{
    (void)user;
    return dluma_native_prod_resolve_base_interface(
        object, ".?AVSensorArea@@", sizeof(".?AVSensorArea@@"));
}

static int dluma_native_prod_validate_local_interface(
    void* interface_pointer, void*** vtable_out)
{
    void** vtable;
    void* enable;
    void* disable;
    void* get;
    if (!interface_pointer || !dluma_is_readable(interface_pointer, sizeof(void*))) return 0;
    vtable = *(void***)interface_pointer;
    if (!vtable || !dluma_is_readable(
            (BYTE*)vtable + DLUMA_NATIVE_LOCAL_IS_ENABLED_SLOT, sizeof(void*))) return 0;
    enable = *(void**)((BYTE*)vtable + DLUMA_NATIVE_LOCAL_ENABLE_SLOT);
    disable = *(void**)((BYTE*)vtable + DLUMA_NATIVE_LOCAL_DISABLE_SLOT);
    get = *(void**)((BYTE*)vtable + DLUMA_NATIVE_LOCAL_IS_ENABLED_SLOT);
    if (enable != g_dluma_native.production.gamedll_base +
                  DLUMA_NATIVE_LOCAL_ENABLE_RVA ||
        disable != g_dluma_native.production.gamedll_base +
                   DLUMA_NATIVE_LOCAL_DISABLE_RVA ||
        get != g_dluma_native.production.gamedll_base + DLUMA_NATIVE_LOCAL_IS_ENABLED_RVA)
        return 0;
    if (!dluma_native_prefix_matches(enable, g_dluma_local_enable_prefix,
            DLUMA_ARRAY_COUNT(g_dluma_local_enable_prefix)) ||
        !dluma_native_prefix_matches(disable, g_dluma_local_disable_prefix,
            DLUMA_ARRAY_COUNT(g_dluma_local_disable_prefix)) ||
        !dluma_native_prefix_matches(get, g_dluma_local_is_enabled_prefix,
            DLUMA_ARRAY_COUNT(g_dluma_local_is_enabled_prefix))) return 0;
    if (vtable_out) *vtable_out = vtable;
    return 1;
}

static int dluma_native_prod_local_enable_get(
    void* user, void* interface_pointer, int* enabled)
{
    void** vtable;
    DlumaNativeLocalEnabledFn function;
    (void)user;
    if (!enabled || !dluma_native_prod_validate_local_interface(
            interface_pointer, &vtable)) return 0;
    function = *(DlumaNativeLocalEnabledFn*)((BYTE*)vtable +
                                            DLUMA_NATIVE_LOCAL_IS_ENABLED_SLOT);
    *enabled = function(interface_pointer) ? 1 : 0;
    return 1;
}

static int dluma_native_prod_local_enable_set(
    void* user, void* interface_pointer, int enabled)
{
    void** vtable;
    DlumaNativeLocalSetFn function;
    (void)user;
    if (!dluma_native_prod_validate_local_interface(interface_pointer, &vtable)) return 0;
    function = *(DlumaNativeLocalSetFn*)((BYTE*)vtable +
        (enabled ? DLUMA_NATIVE_LOCAL_ENABLE_SLOT : DLUMA_NATIVE_LOCAL_DISABLE_SLOT));
    function(interface_pointer);
    return 1;
}

static int dluma_native_prod_sensor_inside(
    void* user, void* sensor_area_interface, int* inside)
{
    void** vtable;
    DlumaNativeSensorInsideFn function;
    (void)user;
    if (!inside || !sensor_area_interface ||
        !dluma_is_readable(sensor_area_interface, sizeof(void*))) return 0;
    vtable = *(void***)sensor_area_interface;
    if (!vtable ||
        !dluma_is_readable((BYTE*)vtable + DLUMA_NATIVE_SENSOR_CHECK_INSIDE_SLOT,
                         sizeof(void*))) return 0;
    function = *(DlumaNativeSensorInsideFn*)((BYTE*)vtable +
                                            DLUMA_NATIVE_SENSOR_CHECK_INSIDE_SLOT);
    if ((void*)function != g_dluma_native.production.gamedll_base +
                           DLUMA_NATIVE_SENSOR_CHECK_INSIDE_RVA ||
        !dluma_native_prefix_matches((void*)function,
            g_dluma_sensor_check_inside_prefix,
            DLUMA_ARRAY_COUNT(g_dluma_sensor_check_inside_prefix))) return 0;
    *inside = function(sensor_area_interface) ? 1 : 0;
    return 1;
}

static int dluma_native_prod_action_run_chain(
    void* user, void* complete_action_chain)
{
    DlumaNativeProductionBackend* production =
        (DlumaNativeProductionBackend*)user;
    DlumaNativeActionChainRunFn run;
    void* expected_vtable;
    if (!production || !production->gamedll_base || !complete_action_chain ||
        !dluma_is_readable(complete_action_chain,
                         DLUMA_NATIVE_ACTION_CHAIN_SIZE)) return 0;
    expected_vtable = production->gamedll_base +
                      DLUMA_NATIVE_ACTION_CHAIN_VTABLE_RVA;
    if (*(void**)complete_action_chain != expected_vtable) return 0;
    run = (DlumaNativeActionChainRunFn)(production->gamedll_base +
                                     DLUMA_NATIVE_ACTION_CHAIN_RUN_RVA);
    if (!dluma_native_prefix_matches(
            (void*)run, g_dluma_action_chain_run_prefix,
            DLUMA_ARRAY_COUNT(g_dluma_action_chain_run_prefix))) return 0;
    run(complete_action_chain);
    return 1;
}

static DlumaNativeTtlString dluma_native_ttl_string_view(const char* text);

/* The manager object is a gamedll static, constructed by the game.  This
 * accessor never constructs, owns, or caches it across a map/lifecycle
 * boundary.  A non-empty peer tree is the exact QuestManager collection used
 * by UpdateRemoteStates_Synchronizing; reject it until the host-authority
 * predicate itself has been separately bound. */
static int dluma_native_prod_get_live_quest_manager(
    DlumaNativeProductionBackend* production,
    void** manager_out,
    DlumaLua54Error* error,
    const char* quest_id)
{
    void* manager;
    void* expected_vtable;
    void* client_state;
    if (!production || !production->gamedll_base || !manager_out) return 0;
    manager = production->gamedll_base + DLUMA_NATIVE_QUEST_MANAGER_RVA;
    expected_vtable = production->gamedll_base +
        DLUMA_NATIVE_QUEST_MANAGER_VTABLE_RVA;
    if (!dluma_is_readable(manager,
            DLUMA_NATIVE_QUEST_MANAGER_CLIENT_STATE_OFFSET + sizeof(void*)) ||
        *(void**)manager != expected_vtable) {
        dluma_native_error(error, "quest_manager_not_ready",
            "the exact QuestManager static is not initialized for this map",
            "quests.native", "quest.write", quest_id, 1);
        return 0;
    }
    if (*(void**)((BYTE*)manager + DLUMA_NATIVE_QUEST_MANAGER_PEER_TREE_OFFSET)) {
        dluma_native_error(error, "quest_coop_not_supported",
            "native quest mutation is disabled while QuestManager has remote peers",
            "quests.native", "quest.write", quest_id, 0);
        return 0;
    }
    client_state = *(void**)((BYTE*)manager +
        DLUMA_NATIVE_QUEST_MANAGER_CLIENT_STATE_OFFSET);
    if (!client_state || !dluma_is_readable((BYTE*)client_state +
            DLUMA_NATIVE_QUEST_CLIENT_STATE_STATUS_OFFSET, sizeof(uint32_t))) {
        dluma_native_error(error, "quest_synchronization_unavailable",
            "QuestManager replication state is unavailable",
            "quests.native", "quest.write", quest_id, 1);
        return 0;
    }
    if (*(uint32_t*)((BYTE*)client_state +
            DLUMA_NATIVE_QUEST_CLIENT_STATE_STATUS_OFFSET) != 1u) {
        dluma_native_error(error, "quest_synchronization_busy",
            "QuestManager is already processing a synchronized transition",
            "quests.native", "quest.write", quest_id, 1);
        return 0;
    }
    *manager_out = manager;
    return 1;
}

static int dluma_native_prod_quest_mutate(
    void* user,
    const DlumaLua54QuestRequest* request,
    DlumaLua54QuestResult* result,
    DlumaLua54Error* error)
{
    DlumaNativeProductionBackend* production =
        (DlumaNativeProductionBackend*)user;
    DlumaNativeTtlString quest_name;
    DlumaNativeTtlString phase_name;
    void* manager;
    void* quest;
    void* phase;
    if (!production || !request || !result ||
        !production->quest_find || !production->quest_find_phase ||
        !production->quest_start_synchronized_invoke) return 0;
    if (request->operation != DLUMA_LUA54_QUEST_JUMP_TO_PHASE) {
        dluma_native_error(error, "unsupported_quest_operation",
            "only advance_to_phase uses the validated synchronized QuestManager route",
            "quests.native", "quest.write", request->native_quest_id, 0);
        return 0;
    }
    if (!request->native_phase_id[0]) {
        dluma_native_error(error, "invalid_quest_operation",
            "advance_to_phase requires an exact native phase identifier",
            "quests.native", "quest.write", request->native_quest_id, 0);
        return 0;
    }
    if (!dluma_native_prod_get_live_quest_manager(
            production, &manager, error, request->native_quest_id)) return 0;
    quest_name = dluma_native_ttl_string_view(request->native_quest_id);
    phase_name = dluma_native_ttl_string_view(request->native_phase_id);
    quest = production->quest_find((BYTE*)manager +
        DLUMA_NATIVE_QUEST_MANAGER_QUEST_TREE_OFFSET, &quest_name);
    if (!quest || !dluma_is_readable(quest,
            DLUMA_NATIVE_QUEST_PHASE_OWNER_OFFSET + sizeof(void*))) {
        dluma_native_error(error, "native_quest_not_found",
            "the exact native quest is not loaded by the active custom map",
            "quests.native", "quest.write", request->native_quest_id, 0);
        return 0;
    }
    phase = production->quest_find_phase(quest, &phase_name);
    if (!phase || !dluma_is_readable((BYTE*)phase +
            DLUMA_NATIVE_QUEST_PHASE_OWNER_OFFSET, sizeof(void*)) ||
        *(void**)((BYTE*)phase + DLUMA_NATIVE_QUEST_PHASE_OWNER_OFFSET) != quest) {
        dluma_native_error(error, "native_phase_not_found",
            "the exact native phase is not owned by the requested quest",
            "quests.native", "quest.write", request->native_quest_id, 0);
        return 0;
    }
    /* Exact QuestManager::StartSynchronizedInvoke(manager, phase, 1, 0, 1).
     * The call queues the game-owned replication transition; it does not
     * execute AdvanceToQuest directly or expose a Quest/Phase pointer to Lua. */
    if (!production->quest_start_synchronized_invoke(
            manager, phase, 1, 0, 1)) {
        dluma_native_error(error, "quest_synchronization_rejected",
            "QuestManager rejected the synchronized phase transition",
            "quests.native", "quest.write", request->native_quest_id, 1);
        return 0;
    }
    memset(result, 0, sizeof(*result));
    result->struct_size = sizeof(*result);
    result->operation = request->operation;
    result->changed = 1;
    /* "mirrored" means the exact engine synchronization route accepted the
     * invoke request.  Phase completion continues on QuestManager's update
     * path, so this is intentionally not a fabricated local state update. */
    result->native_state = DLUMA_LUA54_QUEST_NATIVE_MIRRORED;
    dluma_str_copy(result->native_quest_id,
                 DLUMA_ARRAY_COUNT(result->native_quest_id),
                 request->native_quest_id);
    dluma_str_copy(result->native_phase_id,
                 DLUMA_ARRAY_COUNT(result->native_phase_id),
                 request->native_phase_id);
    return 1;
}

static DlumaNativeTtlString dluma_native_ttl_string_view(const char* text)
{
    DlumaNativeTtlString value;
    size_t length = text ? strlen(text) : 0;
    value.data = length ? text : 0;
    value.length = (uint32_t)length;
    value.capacity = (uint32_t)length;
    return value;
}

static int dluma_native_prod_validate_level_subtitles(
    void* level, void*** vtable_out, void** movie_out)
{
    void** vtable;
    void* movie;
    void** movie_vtable;
    void* level_show;
    void* level_hide;
    void* movie_show;
    void* movie_hide;
    if (!level || !dluma_is_readable(level, sizeof(void*)) ||
        !dluma_is_readable((BYTE*)level + DLUMA_NATIVE_LEVEL_MOVIE_OFFSET,
                         sizeof(void*))) {
        if (!g_dluma_native.subtitle_validation_failure_logged) {
            g_dluma_native.subtitle_validation_failure_logged = 1;
            dluma_log_line("WARN", "subtitle_live_validation_failed = level_or_movie_field_unreadable");
        }
        return 0;
    }
    vtable = *(void***)level;
    movie = *(void**)((BYTE*)level + DLUMA_NATIVE_LEVEL_MOVIE_OFFSET);
    if (!vtable ||
        !dluma_is_readable((BYTE*)vtable + DLUMA_NATIVE_LEVEL_HIDE_SUBTITLE_SLOT,
                         sizeof(void*))) {
        if (!g_dluma_native.subtitle_validation_failure_logged) {
            g_dluma_native.subtitle_validation_failure_logged = 1;
            dluma_log_line("WARN", "subtitle_live_validation_failed = level_vtable_unreadable");
        }
        return 0;
    }
    level_show = *(void**)((BYTE*)vtable +
                           DLUMA_NATIVE_LEVEL_SHOW_SUBTITLE_SLOT);
    level_hide = *(void**)((BYTE*)vtable +
                           DLUMA_NATIVE_LEVEL_HIDE_SUBTITLE_SLOT);
    if (level_show != g_dluma_native.production.gamedll_base +
                          DLUMA_NATIVE_LEVEL_SHOW_SUBTITLE_RVA ||
        level_hide != g_dluma_native.production.gamedll_base +
                          DLUMA_NATIVE_LEVEL_HIDE_SUBTITLE_RVA) {
        if (!g_dluma_native.subtitle_validation_failure_logged) {
            g_dluma_native.subtitle_validation_failure_logged = 1;
            dluma_log_line("WARN", "subtitle_live_validation_failed = level_subtitle_slots_mismatch");
        }
        return 0;
    }
    if (vtable != (void**)(g_dluma_native.production.gamedll_base +
                           DLUMA_NATIVE_LEVELDI_VTABLE_RVA) &&
        !g_dluma_native.subtitle_level_compatible_vtable_logged) {
        g_dluma_native.subtitle_level_compatible_vtable_logged = 1;
        dluma_log_line("INFO", "subtitle_live_level_compatible_derived_vtable");
    }
    if (!movie || !dluma_is_readable(movie, sizeof(void*))) {
        if (!g_dluma_native.subtitle_validation_failure_logged) {
            g_dluma_native.subtitle_validation_failure_logged = 1;
            dluma_log_line("WARN", "subtitle_live_validation_failed = game_movie_unavailable");
        }
        return 0;
    }
    movie_vtable = *(void***)movie;
    if (!movie_vtable ||
        !dluma_is_readable((BYTE*)movie_vtable +
                         DLUMA_NATIVE_GAME_MOVIE_HIDE_SUBTITLE_SLOT,
                         sizeof(void*))) {
        if (!g_dluma_native.subtitle_validation_failure_logged) {
            g_dluma_native.subtitle_validation_failure_logged = 1;
            dluma_log_line("WARN", "subtitle_live_validation_failed = game_movie_vtable_unreadable");
        }
        return 0;
    }
    movie_show = *(void**)((BYTE*)movie_vtable +
                           DLUMA_NATIVE_GAME_MOVIE_SHOW_SUBTITLE_SLOT);
    movie_hide = *(void**)((BYTE*)movie_vtable +
                           DLUMA_NATIVE_GAME_MOVIE_HIDE_SUBTITLE_SLOT);
    if (movie_show != g_dluma_native.production.gamedll_base +
                          DLUMA_NATIVE_GAME_MOVIE_SHOW_SUBTITLE_RVA ||
        movie_hide != g_dluma_native.production.gamedll_base +
                          DLUMA_NATIVE_GAME_MOVIE_HIDE_SUBTITLE_RVA) {
        if (!g_dluma_native.subtitle_validation_failure_logged) {
            g_dluma_native.subtitle_validation_failure_logged = 1;
            dluma_log_line("WARN", "subtitle_live_validation_failed = game_movie_subtitle_slots_mismatch");
        }
        return 0;
    }
    if (movie_vtable != (void**)(g_dluma_native.production.gamedll_base +
                                 DLUMA_NATIVE_GAME_MOVIE_VTABLE_RVA) &&
        !g_dluma_native.subtitle_movie_compatible_vtable_logged) {
        g_dluma_native.subtitle_movie_compatible_vtable_logged = 1;
        dluma_log_line("INFO", "subtitle_live_movie_compatible_derived_vtable");
    }
    if (vtable_out) *vtable_out = vtable;
    if (movie_out) *movie_out = movie;
    return 1;
}

static int dluma_native_prod_subtitle_show(
    void* user, void* level, int32_t subtitle_id,
    const char* text, int force)
{
    void** level_vtable;
    void* movie;
    DlumaNativeTtlString text_view;
    DlumaNativeTtlString empty_view;
    (void)user;
    if (!text || !text[0] ||
        !dluma_native_prod_validate_level_subtitles(
            level, &level_vtable, &movie)) return 0;
    text_view = dluma_native_ttl_string_view(text);
    empty_view = dluma_native_ttl_string_view(0);
    if (!force) {
        DlumaNativeLevelShowSubtitleFn show =
            *(DlumaNativeLevelShowSubtitleFn*)((BYTE*)level_vtable +
                DLUMA_NATIVE_LEVEL_SHOW_SUBTITLE_SLOT);
        if ((void*)show != g_dluma_native.production.gamedll_base +
                           DLUMA_NATIVE_LEVEL_SHOW_SUBTITLE_RVA) return 0;
        /* Mode zero is the normal game path: it retains the user subtitle
         * preference and leaves &TEXT_ID& tokens to the loaded XUI
         * text-resource resolver. */
        show(level, &text_view, &empty_view, subtitle_id, 0, 0);
    } else {
        void** movie_vtable = *(void***)movie;
        DlumaNativeMovieShowSubtitleFn show =
            *(DlumaNativeMovieShowSubtitleFn*)((BYTE*)movie_vtable +
                DLUMA_NATIVE_GAME_MOVIE_SHOW_SUBTITLE_SLOT);
        DlumaNativeSubtitleView subtitle;
        if ((void*)show != g_dluma_native.production.gamedll_base +
                           DLUMA_NATIVE_GAME_MOVIE_SHOW_SUBTITLE_RVA) return 0;
        memset(&subtitle, 0, sizeof(subtitle));
        subtitle.id = subtitle_id;
        subtitle.enabled = 1;
        subtitle.speaker = empty_view;
        subtitle.text = text_view;
        subtitle.combined = text_view;
        subtitle.priority = 10;
        /* UIGameMovieBackground deep-copies this view before returning.  The
         * direct movie call bypasses only ILevel's user-setting check. */
        show(movie, &subtitle);
    }
    return 1;
}

static int dluma_native_prod_subtitle_hide(
    void* user, void* level, int32_t subtitle_id)
{
    void** level_vtable;
    void* movie;
    DlumaNativeLevelHideSubtitleFn hide;
    (void)user;
    if (!dluma_native_prod_validate_level_subtitles(
            level, &level_vtable, &movie)) return 0;
    (void)movie;
    hide = *(DlumaNativeLevelHideSubtitleFn*)((BYTE*)level_vtable +
        DLUMA_NATIVE_LEVEL_HIDE_SUBTITLE_SLOT);
    if ((void*)hide != g_dluma_native.production.gamedll_base +
                       DLUMA_NATIVE_LEVEL_HIDE_SUBTITLE_RVA) return 0;
    hide(level, subtitle_id);
    return 1;
}

static int dluma_native_prod_radio_dialog(
    void* user, const char* speaker_name, const char* group_name)
{
    BYTE* manager;
    DlumaNativeDialogManagerFindSpeakerFn find_speaker_from_level;
    DlumaNativeDialogManagerFindGroupFn find_group_from_level;
    void* speaker;
    void** vtable;
    DlumaNativeDialogFindGroupFn find_group;
    DlumaNativeDialogSetQuestFn set_quest;
    DlumaNativeTtlString speaker_view;
    DlumaNativeTtlString group_view;
    DlumaNativeTtlString origin_view;
    const char* raw_level_name;
    char level_name[DLUMA_MAX_MAP_NAME * 2];
    void* group;
    (void)user;
    if (!speaker_name || !speaker_name[0] || !group_name || !group_name[0])
        return 0;
    manager = g_dluma_native.production.gamedll_base +
              DLUMA_NATIVE_DIALOG_MANAGER_RVA;
    if (!g_dluma_native.level || !g_dluma.engine.get_level_name ||
        !dluma_is_readable(g_dluma_native.level, sizeof(void*)) ||
        !dluma_is_readable(manager, sizeof(void*)) ||
        *(void***)manager != (void**)(g_dluma_native.production.gamedll_base +
                                      DLUMA_NATIVE_DIALOG_MANAGER_VTABLE_RVA))
        return 0;
    raw_level_name = g_dluma.engine.get_level_name(g_dluma_native.level);
    if (!dluma_native_bounded_copy_from_memory(
            raw_level_name, level_name, DLUMA_ARRAY_COUNT(level_name))) return 0;
    find_speaker_from_level = (DlumaNativeDialogManagerFindSpeakerFn)(
        g_dluma_native.production.gamedll_base +
        DLUMA_NATIVE_DIALOG_MANAGER_FIND_SPEAKER_RVA);
    find_group_from_level = (DlumaNativeDialogManagerFindGroupFn)(
        g_dluma_native.production.gamedll_base +
        DLUMA_NATIVE_DIALOG_MANAGER_FIND_GROUP_RVA);
    speaker_view = dluma_native_ttl_string_view(speaker_name);
    group_view = dluma_native_ttl_string_view(group_name);
    speaker = find_speaker_from_level(
        manager, &speaker_view, level_name);
    if (!speaker || !dluma_is_readable(speaker, sizeof(void*))) return 0;
    vtable = *(void***)speaker;
    if (!vtable || !dluma_is_readable((BYTE*)vtable +
            DLUMA_NATIVE_DIALOG_SPEAKER_SET_QUEST_SLOT, sizeof(void*))) return 0;
    find_group = *(DlumaNativeDialogFindGroupFn*)((BYTE*)vtable +
        DLUMA_NATIVE_DIALOG_SPEAKER_FIND_GROUP_SLOT);
    set_quest = *(DlumaNativeDialogSetQuestFn*)((BYTE*)vtable +
        DLUMA_NATIVE_DIALOG_SPEAKER_SET_QUEST_SLOT);
    if ((void*)find_group != g_dluma_native.production.gamedll_base +
                            DLUMA_NATIVE_DIALOG_SPEAKER_FIND_GROUP_RVA ||
        (void*)set_quest != g_dluma_native.production.gamedll_base +
                           DLUMA_NATIVE_DIALOG_SPEAKER_SET_QUEST_RVA) return 0;
    origin_view = dluma_native_ttl_string_view(0);
    group = find_group(speaker, &group_view);
    if (!group)
        group = find_group_from_level(
            manager, &group_view, level_name);
    if (!group) return 0;
    /* This is the same source assignment used by set dialog/radio-dialog.
     * voice2d is advanced by DialogSpeakerDI::Update; actor-backed speakers
     * retain their normal actor-use requirements. */
    return set_quest(speaker, group, &origin_view) == 0;
}

static uint64_t dluma_native_now(void)
{
    return g_dluma_native.backend.clock_ms
        ? g_dluma_native.backend.clock_ms(g_dluma_native.backend.user) : 0;
}

static void dluma_native_reset_index(void)
{
    unsigned int i;
    dluma_native_lock(&g_dluma_native.match_lock);
    ++g_dluma_native.index_epoch;
    if (!g_dluma_native.index_epoch) ++g_dluma_native.index_epoch;
    memset(g_dluma_native.index, 0, sizeof(g_dluma_native.index));
    memset(g_dluma_native.visited, 0, sizeof(g_dluma_native.visited));
    for (i = 0; i < DLUMA_NATIVE_MAX_HANDLES; ++i) {
        if (!g_dluma_native.handles[i].active) continue;
        g_dluma_native.handles[i].active = 0;
        g_dluma_native.handles[i].expired = 1;
    }
    g_dluma_native.index_count = 0;
    g_dluma_native.builder_count = 0;
    g_dluma_native.visited_count = 0;
    g_dluma_native.seed_cursor = 0;
    g_dluma_native.reject_samples = 0;
    g_dluma_native.accepted_samples = 0;
    g_dluma_native.last_progress_cursor = 0;
    g_dluma_native.live_self_test_seed = 0;
    g_dluma_native.live_self_test_id = -1;
    g_dluma_native.index_phase = (g_dluma_native.level && g_dluma_native.map[0] &&
        (g_dluma_native.static_readiness & DLUMA_NATIVE_READY_WORLD_FIND))
        ? DLUMA_NATIVE_INDEX_SCANNING_IDS : DLUMA_NATIVE_INDEX_UNINITIALIZED;
    g_dluma_native.index_not_before_ms =
        (g_dluma_native.production_backend &&
         g_dluma_native.index_phase == DLUMA_NATIVE_INDEX_SCANNING_IDS)
            ? dluma_native_now() + DLUMA_NATIVE_INITIAL_STREAMING_DELAY_MS : 0;
    dluma_atomic_exchange(&g_dluma_native.index_refresh_requested, 0);
    memset(&g_dluma_native.diagnostics, 0, sizeof(g_dluma_native.diagnostics));
    g_dluma_native.diagnostics.struct_size = sizeof(g_dluma_native.diagnostics);
    g_dluma_native.diagnostics.phase = g_dluma_native.index_phase;
    g_dluma_native.seed_scan_logged = 0;
    g_dluma_native.seed_found_logged = 0;
    for (i = 0; i < DLUMA_NATIVE_MAX_SUBSCRIPTIONS; ++i) {
        DlumaNativeSubscription* subscription = &g_dluma_native.subscriptions[i];
        if (!subscription->active) continue;
        subscription->generation = g_dluma_native.generation;
        subscription->index_epoch = g_dluma_native.index_epoch;
        subscription->index = 0;
        subscription->resolution_state = DLUMA_NATIVE_SUB_PENDING;
        subscription->initial_sample = 0;
        subscription->inside = 0;
        subscription->next_retry_ms = 0;
    }
    dluma_native_unlock(&g_dluma_native.match_lock);
    dluma_native_lock(&g_dluma_native.hook_event_lock);
    memset(g_dluma_native.hook_events, 0, sizeof(g_dluma_native.hook_events));
    g_dluma_native.hook_event_head = 0;
    g_dluma_native.hook_event_count = 0;
    dluma_native_unlock(&g_dluma_native.hook_event_lock);
    g_dluma_native.hook_overflow_logged = 0;
    g_dluma_native.interaction_log_samples = 0;
    if (g_dluma_native.index_phase == DLUMA_NATIVE_INDEX_SCANNING_IDS)
        dluma_native_set_supported_state(DLUMA_NATIVE_CAP_LIVE_SELF_TESTING);
    else
        dluma_native_set_supported_state(DLUMA_NATIVE_CAP_WAITING_FOR_MAP);
}

static int dluma_native_object_owner_matches_active_map(
    void* object, void** owner_level_out, int* distinct_owner_out,
    int* id_roundtrip_mismatch_out,
    DlumaNativeOwnerRejectReason* reject_reason_out)
{
    char owner_map[DLUMA_MAX_MAP_NAME];
    void* owner_level;
    int32_t object_id;
    if (owner_level_out) *owner_level_out = 0;
    if (distinct_owner_out) *distinct_owner_out = 0;
    if (id_roundtrip_mismatch_out) *id_roundtrip_mismatch_out = 0;
    if (reject_reason_out) *reject_reason_out = DLUMA_NATIVE_OWNER_ACCEPTED;
    if (!object || !g_dluma_native.level || !g_dluma_native.map[0] ||
        !g_dluma_native.backend.object_level ||
        !g_dluma_native.backend.object_id) {
        if (reject_reason_out)
            *reject_reason_out = DLUMA_NATIVE_OWNER_LEVEL_MISSING;
        return 0;
    }
    owner_level = g_dluma_native.backend.object_level(
        g_dluma_native.backend.user, object);
    if (!owner_level) {
        if (reject_reason_out)
            *reject_reason_out = DLUMA_NATIVE_OWNER_LEVEL_MISSING;
        return 0;
    }
    object_id = g_dluma_native.backend.object_id(
        g_dluma_native.backend.user, object);
    if (object_id < 0) {
        if (reject_reason_out)
            *reject_reason_out = DLUMA_NATIVE_OWNER_ID_INVALID;
        return 0;
    }
    /* The exact Windows ITools lookup routines filter ownership only with
     * IGSObject::GetLevel pointer equality.  ILevel::GetObjectByID on the
     * owning child level is not an engine lookup invariant and is therefore
     * diagnostic only.  Requiring that round trip rejected every live map
     * object beneath the active wrapper ILevel. */
    if (id_roundtrip_mismatch_out &&
        g_dluma_native.backend.object_by_id &&
        g_dluma_native.backend.object_by_id(
            g_dluma_native.backend.user, owner_level, object_id) != object)
        *id_roundtrip_mismatch_out = 1;
    if (owner_level != g_dluma_native.level) {
        owner_map[0] = 0;
        if (!g_dluma_native.backend.level_name ||
            !g_dluma_native.backend.level_name(
                g_dluma_native.backend.user, owner_level, owner_map,
                DLUMA_ARRAY_COUNT(owner_map))) {
            if (reject_reason_out)
                *reject_reason_out = DLUMA_NATIVE_OWNER_NAME_UNAVAILABLE;
            return 0;
        }
        if (dluma_stricmp(owner_map, g_dluma_native.map) != 0) {
            if (reject_reason_out)
                *reject_reason_out = DLUMA_NATIVE_OWNER_NAME_MISMATCH;
            return 0;
        }
        if (distinct_owner_out) *distinct_owner_out = 1;
    }
    if (owner_level_out) *owner_level_out = owner_level;
    return 1;
}

static int dluma_native_revalidate_entry(DlumaNativeIndexEntry* entry)
{
    char name[DLUMA_LUA54_MAX_OBJECT_NAME];
    void* complete;
    void* resolved;
    if (!entry || !entry->object || entry->generation != g_dluma_native.generation ||
        entry->index_epoch != g_dluma_native.index_epoch ||
        !g_dluma_native.level || entry->object_id < 0 ||
        !entry->complete ||
        !dluma_is_readable(entry->object, sizeof(void*)) ||
        !dluma_is_readable(entry->complete, sizeof(void*)) ||
        !g_dluma_native.backend.object_name ||
        !g_dluma_native.backend.object_name(
            g_dluma_native.backend.user, entry->object, name, DLUMA_ARRAY_COUNT(name)) ||
        !dluma_native_streq(name, entry->name) ||
        !g_dluma_native.backend.complete_object)
        return 0;
    /* The active ILevel ID lookup is the same bounded operation that built
     * the index and passed the live seed self-test.  It proves root-owned
     * object identity without re-entering GetLevel/GetID/GetRTTI virtuals on
     * a streamed gameplay object.  Distinct same-map child owners cannot use
     * the root lookup contract, so their immutable epoch/generation, readable
     * object/name, and complete-object identity remain the fail-closed guard. */
    if (entry->level == g_dluma_native.level) {
        if (!g_dluma_native.backend.object_by_id) return 0;
        resolved = g_dluma_native.backend.object_by_id(
            g_dluma_native.backend.user, g_dluma_native.level,
            entry->object_id);
        if (resolved != entry->object) return 0;
    }
    complete = g_dluma_native.backend.complete_object(
        g_dluma_native.backend.user, entry->object);
    return complete && complete == entry->complete;
}

static void dluma_native_log_reject_sample(const char* reason)
{
    if (g_dluma_native.reject_samples >= DLUMA_NATIVE_REJECT_SAMPLE_LIMIT) return;
    ++g_dluma_native.reject_samples;
    dluma_log_pair("WARN", "world_index_reject_sample", reason);
}

static int dluma_native_add_index_entry(void* object)
{
    DlumaNativeIndexEntry* entry;
    void* complete;
    void* owner_level;
    int32_t object_id;
    int distinct_owner;
    int id_roundtrip_mismatch;
    DlumaNativeOwnerRejectReason owner_reject_reason;
    uint32_t i;
    if (!object) return 0;
    if (g_dluma_native.builder_count >= DLUMA_NATIVE_MAX_INDEX) {
        ++g_dluma_native.diagnostics.rejected_capacity;
        dluma_native_log_reject_sample("index_capacity");
        return -1;
    }
    if (!dluma_native_object_owner_matches_active_map(
            object, &owner_level, &distinct_owner,
            &id_roundtrip_mismatch, &owner_reject_reason)) {
        ++g_dluma_native.diagnostics.rejected_wrong_level;
        if (owner_reject_reason == DLUMA_NATIVE_OWNER_LEVEL_MISSING) {
            ++g_dluma_native.diagnostics.rejected_owner_level_missing;
            dluma_native_log_reject_sample("owner_level_missing");
        } else if (owner_reject_reason == DLUMA_NATIVE_OWNER_ID_INVALID) {
            ++g_dluma_native.diagnostics.rejected_owner_id_invalid;
            dluma_native_log_reject_sample("owner_object_id_invalid");
        } else if (owner_reject_reason == DLUMA_NATIVE_OWNER_NAME_UNAVAILABLE) {
            ++g_dluma_native.diagnostics.rejected_owner_name_unavailable;
            dluma_native_log_reject_sample("owner_level_name_unavailable");
        } else {
            ++g_dluma_native.diagnostics.rejected_owner_name_mismatch;
            dluma_native_log_reject_sample("owner_level_name_mismatch");
        }
        return 0;
    }
    if (id_roundtrip_mismatch)
        ++g_dluma_native.diagnostics.observed_owner_id_roundtrip_mismatch;
    if (!g_dluma_native.backend.complete_object) {
        ++g_dluma_native.diagnostics.rejected_complete_object;
        dluma_native_log_reject_sample("complete_object_backend_missing");
        return 0;
    }
    complete = g_dluma_native.backend.complete_object(g_dluma_native.backend.user, object);
    if (!complete) {
        ++g_dluma_native.diagnostics.rejected_complete_object;
        dluma_native_log_reject_sample("complete_object_recovery_failed");
        return 0;
    }
    object_id = g_dluma_native.backend.object_id
        ? g_dluma_native.backend.object_id(g_dluma_native.backend.user, object) : -1;
    if (object_id >= 0) {
        for (i = 0; i < g_dluma_native.builder_count; ++i) {
            if (g_dluma_native.index[i].object_id == object_id) {
                ++g_dluma_native.diagnostics.rejected_duplicate_id;
                dluma_native_log_reject_sample("duplicate_object_id");
                return 0;
            }
        }
    }
    entry = &g_dluma_native.index[g_dluma_native.builder_count];
    memset(entry, 0, sizeof(*entry));
    if (!g_dluma_native.backend.object_name ||
        !g_dluma_native.backend.object_name(
            g_dluma_native.backend.user, object, entry->name,
            DLUMA_ARRAY_COUNT(entry->name))) {
        ++g_dluma_native.diagnostics.rejected_name;
        dluma_native_log_reject_sample("invalid_or_unreadable_name");
        memset(entry, 0, sizeof(*entry));
        return 0;
    }
    if (!g_dluma_native.backend.object_class ||
        !g_dluma_native.backend.object_class(
            g_dluma_native.backend.user, object, entry->class_name,
            DLUMA_ARRAY_COUNT(entry->class_name))) {
        ++g_dluma_native.diagnostics.rejected_rtti;
        dluma_native_log_reject_sample("dynamic_rtti_or_class_name_failed");
        memset(entry, 0, sizeof(*entry));
        return 0;
    }
    entry->object = object;
    entry->complete = complete;
    entry->level = owner_level;
    entry->index_epoch = g_dluma_native.index_epoch;
    entry->generation = g_dluma_native.generation;
    entry->object_id = object_id;
    ++g_dluma_native.builder_count;
    ++g_dluma_native.diagnostics.accepted_entries;
    if (distinct_owner)
        ++g_dluma_native.diagnostics.accepted_distinct_owner_level;
    if (g_dluma_native.accepted_samples < DLUMA_NATIVE_ACCEPT_SAMPLE_LIMIT) {
        ++g_dluma_native.accepted_samples;
        dluma_log_pair("INFO", "world_index_accepted_sample", entry->name);
    }
    return 1;
}

static int dluma_native_mark_visited(void* object, uint64_t* cycle_counter)
{
    uint32_t i;
    if (!object) return 0;
    for (i = 0; i < g_dluma_native.visited_count; ++i) {
        if (g_dluma_native.visited[i] != object) continue;
        ++g_dluma_native.diagnostics.rejected_duplicate_pointer;
        if (cycle_counter) ++*cycle_counter;
        return 0;
    }
    if (g_dluma_native.visited_count >= DLUMA_NATIVE_MAX_VISITED) {
        ++g_dluma_native.diagnostics.rejected_capacity;
        if (cycle_counter) ++*cycle_counter;
        dluma_native_log_reject_sample("visited_pointer_capacity");
        return -1;
    }
    g_dluma_native.visited[g_dluma_native.visited_count++] = object;
    ++g_dluma_native.diagnostics.candidate_pointers;
    return 1;
}

static int dluma_native_walk_component_direction(
    void* seed, DlumaNativeBackendLinkFn link, uint64_t* cycle_counter)
{
    void* object = seed;
    uint32_t steps = 0;
    if (!link) return 1;
    while (object && steps < DLUMA_NATIVE_MAX_VISITED) {
        void* linked = link(g_dluma_native.backend.user, object);
        int marked;
        if (!linked) return 1;
        marked = dluma_native_mark_visited(linked, cycle_counter);
        if (marked <= 0) return marked == 0;
        if (dluma_native_add_index_entry(linked) < 0) return 0;
        object = linked;
        ++steps;
    }
    if (cycle_counter) ++*cycle_counter;
    return 0;
}

static void dluma_native_consider_live_self_test_seed(
    void* seed, int32_t seed_id)
{
    uint32_t i;
    if (g_dluma_native.live_self_test_seed || seed_id < 0) return;
    for (i = 0; i < g_dluma_native.builder_count; ++i) {
        if (g_dluma_native.index[i].object == seed &&
            g_dluma_native.index[i].object_id == seed_id) {
            g_dluma_native.live_self_test_seed = seed;
            g_dluma_native.live_self_test_id = seed_id;
            return;
        }
    }
}

static int dluma_native_build_index_from_seed(void* seed, int32_t seed_id)
{
    int marked = dluma_native_mark_visited(seed, 0);
    uint32_t i;
    if (marked < 0) return 0;
    if (!marked) {
        int already_indexed = 0;
        for (i = 0; i < g_dluma_native.builder_count; ++i) {
            if (g_dluma_native.index[i].object != seed) continue;
            already_indexed = 1;
            break;
        }
        /* A linked object can be observed before level streaming assigns its
         * owner.  If its direct ID later resolves in this same bounded scan,
         * revalidate the seed without re-walking its already visited links. */
        if (!already_indexed) {
            int added;
            ++g_dluma_native.diagnostics.reconsidered_direct_seeds;
            added = dluma_native_add_index_entry(seed);
            if (added < 0) return 0;
            if (added > 0)
                ++g_dluma_native.diagnostics.accepted_reconsidered_direct_seeds;
        }
        dluma_native_consider_live_self_test_seed(seed, seed_id);
        return 1;
    }
    ++g_dluma_native.diagnostics.component_walks;
    if (dluma_native_add_index_entry(seed) < 0) return 0;
    dluma_native_consider_live_self_test_seed(seed, seed_id);
    if (!dluma_native_walk_component_direction(
            seed, g_dluma_native.backend.object_prev,
            &g_dluma_native.diagnostics.prev_cycle_or_cap)) return 0;
    if (!dluma_native_walk_component_direction(
            seed, g_dluma_native.backend.object_next,
            &g_dluma_native.diagnostics.next_cycle_or_cap)) return 0;
    return 1;
}

static void dluma_native_append_summary_metric(
    char* text, unsigned int capacity, const char* label, uint64_t value)
{
    char number[32];
    dluma_str_append(text, capacity, " ");
    dluma_str_append(text, capacity, label);
    dluma_str_append(text, capacity, "=");
    dluma_u64_to_text((ULONGLONG)value, number, DLUMA_ARRAY_COUNT(number));
    dluma_str_append(text, capacity, number);
}

static void dluma_native_log_index_summary(void)
{
    char summary[1536];
    summary[0] = 0;
    dluma_str_append(summary, DLUMA_ARRAY_COUNT(summary), "map=");
    dluma_str_append(summary, DLUMA_ARRAY_COUNT(summary), g_dluma_native.map);
    dluma_native_append_summary_metric(summary, DLUMA_ARRAY_COUNT(summary),
        "scan_first_id", 0);
    dluma_native_append_summary_metric(summary, DLUMA_ARRAY_COUNT(summary),
        "scan_final_exclusive", g_dluma_native.seed_cursor);
    dluma_native_append_summary_metric(summary, DLUMA_ARRAY_COUNT(summary),
        "ids_scanned", g_dluma_native.diagnostics.ids_scanned);
    dluma_native_append_summary_metric(summary, DLUMA_ARRAY_COUNT(summary),
        "nonnull_id_results", g_dluma_native.diagnostics.nonnull_id_results);
    dluma_native_append_summary_metric(summary, DLUMA_ARRAY_COUNT(summary),
        "component_walks", g_dluma_native.diagnostics.component_walks);
    dluma_native_append_summary_metric(summary, DLUMA_ARRAY_COUNT(summary),
        "candidate_pointers", g_dluma_native.diagnostics.candidate_pointers);
    dluma_native_append_summary_metric(summary, DLUMA_ARRAY_COUNT(summary),
        "accepted_entries", g_dluma_native.diagnostics.accepted_entries);
    dluma_native_append_summary_metric(summary, DLUMA_ARRAY_COUNT(summary),
        "rejected_wrong_level", g_dluma_native.diagnostics.rejected_wrong_level);
    dluma_native_append_summary_metric(summary, DLUMA_ARRAY_COUNT(summary),
        "rejected_complete_object", g_dluma_native.diagnostics.rejected_complete_object);
    dluma_native_append_summary_metric(summary, DLUMA_ARRAY_COUNT(summary),
        "rejected_name", g_dluma_native.diagnostics.rejected_name);
    dluma_native_append_summary_metric(summary, DLUMA_ARRAY_COUNT(summary),
        "rejected_rtti", g_dluma_native.diagnostics.rejected_rtti);
    dluma_native_append_summary_metric(summary, DLUMA_ARRAY_COUNT(summary),
        "rejected_duplicate_pointer", g_dluma_native.diagnostics.rejected_duplicate_pointer);
    dluma_native_append_summary_metric(summary, DLUMA_ARRAY_COUNT(summary),
        "rejected_duplicate_id", g_dluma_native.diagnostics.rejected_duplicate_id);
    dluma_native_append_summary_metric(summary, DLUMA_ARRAY_COUNT(summary),
        "rejected_capacity", g_dluma_native.diagnostics.rejected_capacity);
    dluma_native_append_summary_metric(summary, DLUMA_ARRAY_COUNT(summary),
        "prev_cycle_or_cap", g_dluma_native.diagnostics.prev_cycle_or_cap);
    dluma_native_append_summary_metric(summary, DLUMA_ARRAY_COUNT(summary),
        "next_cycle_or_cap", g_dluma_native.diagnostics.next_cycle_or_cap);
    dluma_native_append_summary_metric(summary, DLUMA_ARRAY_COUNT(summary),
        "accepted_distinct_owner_level",
        g_dluma_native.diagnostics.accepted_distinct_owner_level);
    dluma_native_append_summary_metric(summary, DLUMA_ARRAY_COUNT(summary),
        "rejected_owner_level_missing",
        g_dluma_native.diagnostics.rejected_owner_level_missing);
    dluma_native_append_summary_metric(summary, DLUMA_ARRAY_COUNT(summary),
        "rejected_owner_id_invalid",
        g_dluma_native.diagnostics.rejected_owner_id_invalid);
    dluma_native_append_summary_metric(summary, DLUMA_ARRAY_COUNT(summary),
        "rejected_owner_name_unavailable",
        g_dluma_native.diagnostics.rejected_owner_name_unavailable);
    dluma_native_append_summary_metric(summary, DLUMA_ARRAY_COUNT(summary),
        "rejected_owner_name_mismatch",
        g_dluma_native.diagnostics.rejected_owner_name_mismatch);
    dluma_native_append_summary_metric(summary, DLUMA_ARRAY_COUNT(summary),
        "observed_owner_id_roundtrip_mismatch",
        g_dluma_native.diagnostics.observed_owner_id_roundtrip_mismatch);
    dluma_native_append_summary_metric(summary, DLUMA_ARRAY_COUNT(summary),
        "reconsidered_direct_seeds",
        g_dluma_native.diagnostics.reconsidered_direct_seeds);
    dluma_native_append_summary_metric(summary, DLUMA_ARRAY_COUNT(summary),
        "accepted_reconsidered_direct_seeds",
        g_dluma_native.diagnostics.accepted_reconsidered_direct_seeds);
    dluma_log_pair("INFO", "world_index_build_summary", summary);
    dluma_log_u64("INFO", "world_index_ids_scanned", g_dluma_native.diagnostics.ids_scanned);
    dluma_log_u64("INFO", "world_index_nonnull_id_results", g_dluma_native.diagnostics.nonnull_id_results);
    dluma_log_u64("INFO", "world_index_component_walks", g_dluma_native.diagnostics.component_walks);
    dluma_log_u64("INFO", "world_index_candidate_pointers", g_dluma_native.diagnostics.candidate_pointers);
    dluma_log_u64("INFO", "world_index_accepted_entries", g_dluma_native.diagnostics.accepted_entries);
    dluma_log_u64("INFO", "world_index_rejected_wrong_level", g_dluma_native.diagnostics.rejected_wrong_level);
    dluma_log_u64("INFO", "world_index_rejected_complete_object", g_dluma_native.diagnostics.rejected_complete_object);
    dluma_log_u64("INFO", "world_index_rejected_name", g_dluma_native.diagnostics.rejected_name);
    dluma_log_u64("INFO", "world_index_rejected_rtti", g_dluma_native.diagnostics.rejected_rtti);
    dluma_log_u64("INFO", "world_index_rejected_duplicate_pointer", g_dluma_native.diagnostics.rejected_duplicate_pointer);
    dluma_log_u64("INFO", "world_index_rejected_duplicate_id", g_dluma_native.diagnostics.rejected_duplicate_id);
    dluma_log_u64("INFO", "world_index_rejected_capacity", g_dluma_native.diagnostics.rejected_capacity);
    dluma_log_u64("INFO", "world_index_prev_cycle_or_cap", g_dluma_native.diagnostics.prev_cycle_or_cap);
    dluma_log_u64("INFO", "world_index_next_cycle_or_cap", g_dluma_native.diagnostics.next_cycle_or_cap);
    dluma_log_u64("INFO", "world_index_accepted_distinct_owner_level",
                g_dluma_native.diagnostics.accepted_distinct_owner_level);
    dluma_log_u64("INFO", "world_index_rejected_owner_level_missing",
                g_dluma_native.diagnostics.rejected_owner_level_missing);
    dluma_log_u64("INFO", "world_index_rejected_owner_id_invalid",
                g_dluma_native.diagnostics.rejected_owner_id_invalid);
    dluma_log_u64("INFO", "world_index_rejected_owner_name_unavailable",
                g_dluma_native.diagnostics.rejected_owner_name_unavailable);
    dluma_log_u64("INFO", "world_index_rejected_owner_name_mismatch",
                g_dluma_native.diagnostics.rejected_owner_name_mismatch);
    dluma_log_u64("INFO", "world_index_observed_owner_id_roundtrip_mismatch",
                g_dluma_native.diagnostics.observed_owner_id_roundtrip_mismatch);
    dluma_log_u64("INFO", "world_index_reconsidered_direct_seeds",
                g_dluma_native.diagnostics.reconsidered_direct_seeds);
    dluma_log_u64("INFO", "world_index_accepted_reconsidered_direct_seeds",
                g_dluma_native.diagnostics.accepted_reconsidered_direct_seeds);
}

static void dluma_native_degrade_index(
    const char* reason, uint64_t expected_epoch)
{
    int applied = 0;
    dluma_native_lock(&g_dluma_native.match_lock);
    if (g_dluma_native.index_epoch == expected_epoch &&
        g_dluma_native.index_phase == DLUMA_NATIVE_INDEX_SCANNING_IDS) {
        g_dluma_native.index_count = 0;
        g_dluma_native.index_phase = DLUMA_NATIVE_INDEX_DEGRADED;
        g_dluma_native.diagnostics.phase = DLUMA_NATIVE_INDEX_DEGRADED;
        g_dluma_native.diagnostics.scan_cursor = g_dluma_native.seed_cursor;
        g_dluma_native.diagnostics.published_entries = 0;
        dluma_native_set_supported_state(DLUMA_NATIVE_CAP_DEGRADED);
        g_dluma_native.next_recovery_ms =
            dluma_native_now() + DLUMA_NATIVE_RECOVERY_DELAY_MS;
        applied = 1;
    }
    dluma_native_unlock(&g_dluma_native.match_lock);
    if (!applied) return;
    dluma_log_pair("ERROR", "world_index_degraded", reason ? reason : "unknown");
    dluma_native_log_index_summary();
}

static void dluma_native_complete_index(uint64_t expected_epoch)
{
    uint32_t published_count;
    int interaction_ready = 0;
    void* self_test_seed;
    if (g_dluma_native.index_epoch != expected_epoch ||
        g_dluma_native.index_phase != DLUMA_NATIVE_INDEX_SCANNING_IDS) return;
    if (!g_dluma_native.builder_count || !g_dluma_native.live_self_test_seed ||
        g_dluma_native.live_self_test_id < 0 ||
        !g_dluma_native.backend.object_by_id ||
        !g_dluma_native.backend.object_id) {
        dluma_native_degrade_index(g_dluma_native.builder_count
            ? "live_object_self_test_seed_missing" : "no_usable_objects",
            expected_epoch);
        return;
    }
    dluma_log_u64("INFO", "world_index_completion_started",
                g_dluma_native.builder_count);
    self_test_seed = g_dluma_native.backend.object_by_id(
        g_dluma_native.backend.user, g_dluma_native.level,
        g_dluma_native.live_self_test_id);
    if (self_test_seed != g_dluma_native.live_self_test_seed ||
        g_dluma_native.backend.object_id(
            g_dluma_native.backend.user, self_test_seed) !=
                g_dluma_native.live_self_test_id) {
        dluma_native_degrade_index(
            "live_getobjectbyid_getid_self_test_failed", expected_epoch);
        return;
    }
    dluma_log_u64("INFO", "world_index_live_seed_verified",
                (uint64_t)g_dluma_native.live_self_test_id);
    /* Every entry was validated while it was admitted to this exact epoch's
     * builder, and every API use revalidates the selected entry before it is
     * committed to a Lua handle.  Do not synchronously replay live virtual
     * calls over the whole snapshot here.  In production, objects owned by a
     * streamed child ILevel can make that redundant sweep block forever and
     * prevent an otherwise complete index from ever being published. */
    dluma_native_lock(&g_dluma_native.match_lock);
    if (g_dluma_native.index_epoch != expected_epoch ||
        g_dluma_native.index_phase != DLUMA_NATIVE_INDEX_SCANNING_IDS) {
        dluma_native_unlock(&g_dluma_native.match_lock);
        return;
    }
    g_dluma_native.index_count = g_dluma_native.builder_count;
    g_dluma_native.index_phase = DLUMA_NATIVE_INDEX_COMPLETE;
    g_dluma_native.diagnostics.phase = DLUMA_NATIVE_INDEX_COMPLETE;
    g_dluma_native.diagnostics.scan_cursor = DLUMA_NATIVE_SEED_SCAN_MAX + 1u;
    g_dluma_native.diagnostics.published_entries = g_dluma_native.index_count;
    published_count = g_dluma_native.index_count;
    g_dluma_native.next_recovery_ms =
        dluma_native_now() + DLUMA_NATIVE_RECOVERY_DELAY_MS;
    dluma_native_set_capability_state(
        DLUMA_NATIVE_READY_WORLD_FIND, DLUMA_NATIVE_CAP_READY);
    if (g_dluma_native.static_readiness & DLUMA_NATIVE_READY_LOCAL_ENABLE)
        dluma_native_set_capability_state(
            DLUMA_NATIVE_READY_LOCAL_ENABLE, DLUMA_NATIVE_CAP_READY);
    if (g_dluma_native.static_readiness & DLUMA_NATIVE_READY_SENSOR)
        dluma_native_set_capability_state(
            DLUMA_NATIVE_READY_SENSOR, DLUMA_NATIVE_CAP_READY);
    if (g_dluma_native.static_readiness & DLUMA_NATIVE_READY_SUBTITLES)
        dluma_native_set_capability_state(
            DLUMA_NATIVE_READY_SUBTITLES, DLUMA_NATIVE_CAP_READY);
    if (g_dluma_native.static_readiness & DLUMA_NATIVE_READY_RADIO_DIALOG)
        dluma_native_set_capability_state(
            DLUMA_NATIVE_READY_RADIO_DIALOG, DLUMA_NATIVE_CAP_READY);
    if (g_dluma_native.static_readiness & DLUMA_NATIVE_READY_ACTION_CHAIN)
        dluma_native_set_capability_state(
            DLUMA_NATIVE_READY_ACTION_CHAIN, DLUMA_NATIVE_CAP_READY);
    if (g_dluma_native.static_readiness & DLUMA_NATIVE_READY_WORLD_POSITION)
        dluma_native_set_capability_state(
            DLUMA_NATIVE_READY_WORLD_POSITION, DLUMA_NATIVE_CAP_READY);
    dluma_native_unlock(&g_dluma_native.match_lock);
    /* Publish and report world readiness before checking an independent
     * interaction observer.  A secondary capability must never hide a valid
     * object index from world.find or sensor subscriptions. */
    dluma_native_log_index_summary();
    dluma_log_u64("INFO", "world_index_ready", published_count);
    if (g_dluma_native.static_readiness & DLUMA_NATIVE_READY_INTERACT)
        interaction_ready = dluma_native_interactive_hook_current();
    dluma_native_lock(&g_dluma_native.match_lock);
    if (g_dluma_native.index_epoch != expected_epoch ||
        g_dluma_native.index_phase != DLUMA_NATIVE_INDEX_COMPLETE) {
        dluma_native_unlock(&g_dluma_native.match_lock);
        return;
    }
    if (g_dluma_native.static_readiness & DLUMA_NATIVE_READY_INTERACT)
        dluma_native_set_capability_state(
            DLUMA_NATIVE_READY_INTERACT,
            interaction_ready ? DLUMA_NATIVE_CAP_READY :
                                DLUMA_NATIVE_CAP_DEGRADED);
    dluma_native_unlock(&g_dluma_native.match_lock);
    if ((g_dluma_native.static_readiness & DLUMA_NATIVE_READY_INTERACT) &&
        !interaction_ready)
        dluma_log_line("WARN", "native_adapter_interaction_live_self_test_failed");
}

static void dluma_native_tick_index(void)
{
    uint64_t expected_epoch;
    uint32_t end;
    uint32_t id;
    if (g_dluma_native.index_phase != DLUMA_NATIVE_INDEX_SCANNING_IDS ||
        !g_dluma_native.level ||
        !g_dluma_native.backend.object_by_id) return;
    if (g_dluma_native.production_backend &&
        dluma_native_now() < g_dluma_native.index_not_before_ms) return;
    expected_epoch = g_dluma_native.index_epoch;
    if (!g_dluma_native.seed_scan_logged) {
        g_dluma_native.seed_scan_logged = 1;
        dluma_log_pair("INFO", "world_index_build_started", g_dluma_native.map);
    }
    end = g_dluma_native.seed_cursor + DLUMA_NATIVE_SEED_SCAN_BATCH;
    if (end > DLUMA_NATIVE_SEED_SCAN_MAX + 1u) end = DLUMA_NATIVE_SEED_SCAN_MAX + 1u;
    for (id = g_dluma_native.seed_cursor; id < end; ++id) {
        void* seed = g_dluma_native.backend.object_by_id(
            g_dluma_native.backend.user, g_dluma_native.level, (int32_t)id);
        ++g_dluma_native.diagnostics.ids_scanned;
        if (!seed) continue;
        ++g_dluma_native.diagnostics.nonnull_id_results;
        g_dluma_native.seed_found_logged = 1;
        if (g_dluma_native.diagnostics.nonnull_id_results <=
                DLUMA_NATIVE_REJECT_SAMPLE_LIMIT)
            dluma_log_u64("INFO", "world_index_seed_candidate", id);
        if (!dluma_native_build_index_from_seed(seed, (int32_t)id)) {
            if (g_dluma_native.index_epoch == expected_epoch)
                g_dluma_native.seed_cursor = id + 1u;
            dluma_native_degrade_index(
                "index_or_visited_capacity", expected_epoch);
            return;
        }
    }
    if (g_dluma_native.index_epoch != expected_epoch ||
        g_dluma_native.index_phase != DLUMA_NATIVE_INDEX_SCANNING_IDS) return;
    g_dluma_native.seed_cursor = end;
    g_dluma_native.diagnostics.scan_cursor = end;
    if (end - g_dluma_native.last_progress_cursor >=
            DLUMA_NATIVE_PROGRESS_INTERVAL || end > DLUMA_NATIVE_SEED_SCAN_MAX) {
        g_dluma_native.last_progress_cursor = end;
        dluma_log_u64("INFO", "world_index_build_progress", end);
    }
    if (end > DLUMA_NATIVE_SEED_SCAN_MAX) {
        dluma_native_complete_index(expected_epoch);
    }
}

static int dluma_native_find_index(
    const char* exact_name, uint32_t* index_out,
    DlumaNativeIndexEntry* entry_out, DlumaLua54Error* error)
{
    uint32_t i;
    uint32_t match = 0;
    uint32_t match_count = 0;
    if (!dluma_native_exact_name_valid(exact_name)) {
        dluma_native_error(error, "invalid_object_name", "object name is empty, too long, or contains control characters",
                         "world.find", "world.query", exact_name, 0);
        return 0;
    }
    dluma_native_lock(&g_dluma_native.match_lock);
    if (!g_dluma_native.level || !g_dluma_native.map[0] ||
        g_dluma_native.index_phase != DLUMA_NATIVE_INDEX_COMPLETE) {
        dluma_native_unlock(&g_dluma_native.match_lock);
        dluma_native_error(error, "world_index_not_ready", "the active map object index is not ready",
                         "world.find", "world.query", exact_name, 1);
        return 0;
    }
    for (i = 0; i < g_dluma_native.index_count; ++i) {
        if (dluma_native_streq(g_dluma_native.index[i].name, exact_name)) {
            match = i;
            ++match_count;
        }
    }
    if (!match_count) {
        dluma_atomic_exchange(&g_dluma_native.index_refresh_requested, 1);
        dluma_native_unlock(&g_dluma_native.match_lock);
        dluma_native_error(error, "object_not_found", "no active-level object has the exact case-sensitive name",
                         "world.find", "world.query", exact_name, 1);
        return 0;
    }
    if (match_count != 1) {
        dluma_native_unlock(&g_dluma_native.match_lock);
        dluma_native_error(error, "duplicate_object_name", "multiple active-level objects have the exact name",
                         "world.find", "world.query", exact_name, 0);
        return 0;
    }
    if (entry_out) *entry_out = g_dluma_native.index[match];
    dluma_native_unlock(&g_dluma_native.match_lock);
    if (entry_out && !dluma_native_revalidate_entry(entry_out)) {
        dluma_native_error(error, "stale_object", "object failed active-level, RTTI, or bounded-name revalidation",
                         "world.find", "world.query", exact_name, 1);
        return 0;
    }
    if (index_out) *index_out = match;
    return 1;
}

static void dluma_native_fill_object(
    uint64_t owner_id, uint64_t handle_id,
    const DlumaNativeIndexEntry* entry, DlumaLua54Object* object)
{
    memset(object, 0, sizeof(*object));
    object->struct_size = sizeof(*object);
    object->map_generation = entry->generation;
    object->handle_id = handle_id;
    object->owner_id = owner_id;
    object->object_id = entry->object_id;
    dluma_str_copy(object->name, DLUMA_ARRAY_COUNT(object->name), entry->name);
    dluma_str_copy(object->class_name, DLUMA_ARRAY_COUNT(object->class_name),
                 entry->class_name);
}

static int dluma_native_find_handle(
    uint64_t owner_id, uint64_t handle_id, uint32_t generation,
    DlumaNativeHandle* handle_out, DlumaLua54Error* error)
{
    uint32_t i;
    DlumaNativeHandle snapshot;
    memset(&snapshot, 0, sizeof(snapshot));
    dluma_native_lock(&g_dluma_native.match_lock);
    for (i = 0; i < DLUMA_NATIVE_MAX_HANDLES; ++i) {
        DlumaNativeHandle* handle = &g_dluma_native.handles[i];
        if ((!handle->active && !handle->expired) ||
            handle->handle_id != handle_id) continue;
        if (handle->owner_id != owner_id) {
            dluma_native_unlock(&g_dluma_native.match_lock);
            dluma_native_error(error, "foreign_object_handle", "object handle belongs to a different Lua state",
                             "world.find", "world.query", "", 0);
            return 0;
        }
        snapshot = *handle;
        dluma_native_unlock(&g_dluma_native.match_lock);
        if (snapshot.expired || snapshot.generation != generation ||
            generation != g_dluma_native.generation ||
            !dluma_native_revalidate_entry(&snapshot.entry)) {
            dluma_native_error(error, "stale_object_handle", "object handle expired at a map-generation boundary",
                             "world.find", "world.query", "", 1);
            return 0;
        }
        if (handle_out) *handle_out = snapshot;
        return handle_out != 0;
    }
    dluma_native_unlock(&g_dluma_native.match_lock);
    dluma_native_error(error, "unknown_object_handle", "object handle is unknown or was released",
                     "world.find", "world.query", "", 0);
    return 0;
}

static void dluma_native_log_resolution(
    const char* name, uint32_t state)
{
    dluma_log_pair(state == DLUMA_NATIVE_SUB_RESOLVED ? "INFO" : "WARN",
        state == DLUMA_NATIVE_SUB_RESOLVED ? "subscription_object_resolved" :
        (state == DLUMA_NATIVE_SUB_WRONG_CLASS ? "subscription_wrong_class" :
                                              "subscription_object_not_found"),
        name);
}

static int dluma_native_subscription_kinds_share_key(
    uint32_t left, uint32_t right)
{
    int left_sensor = left == DLUMA_LUA54_EVENT_SENSOR_ENTER ||
                      left == DLUMA_LUA54_EVENT_SENSOR_LEAVE;
    int right_sensor = right == DLUMA_LUA54_EVENT_SENSOR_ENTER ||
                       right == DLUMA_LUA54_EVENT_SENSOR_LEAVE;
    return (left_sensor && right_sensor) || left == right;
}

/* Caller holds match_lock.  Revisions are per exact event/name key (enter and
 * leave share one sensor-name key):
 * unrelated subscriptions never cancel a valid observation, while
 * remove/re-add and pending/resolved ABA transitions always change the key. */
static uint64_t dluma_native_bump_subscription_key_revision_locked(
    uint32_t kind, const char* name)
{
    uint64_t revision;
    uint32_t i;
    revision = ++g_dluma_native.next_subscription_revision;
    if (!revision)
        revision = ++g_dluma_native.next_subscription_revision;
    for (i = 0; i < DLUMA_NATIVE_MAX_SUBSCRIPTIONS; ++i) {
        DlumaNativeSubscription* subscription =
            &g_dluma_native.subscriptions[i];
        if (subscription->active &&
            dluma_native_subscription_kinds_share_key(
                subscription->kind, kind) &&
            dluma_native_streq(subscription->name, name))
            subscription->key_revision = revision;
    }
    return revision;
}

static int dluma_native_resolve_subscription(uint32_t slot)
{
    uint32_t index;
    uint32_t state;
    uint32_t old_state;
    DlumaLua54Error error;
    DlumaNativeSubscription snapshot;
    DlumaNativeIndexEntry entry;
    const char* expected;
    uint64_t retry_at;
    if (slot >= DLUMA_NATIVE_MAX_SUBSCRIPTIONS) return 0;
    dluma_native_lock(&g_dluma_native.match_lock);
    snapshot = g_dluma_native.subscriptions[slot];
    dluma_native_unlock(&g_dluma_native.match_lock);
    if (!snapshot.active || snapshot.generation != g_dluma_native.generation ||
        snapshot.index_epoch != g_dluma_native.index_epoch)
        return 0;
    retry_at = dluma_native_now() + DLUMA_NATIVE_RESOLVE_RETRY_MS;
    memset(&error, 0, sizeof(error));
    if (!dluma_native_find_index(snapshot.name, &index, &entry, &error)) {
        state = dluma_native_streq(error.code, "object_not_found")
            ? DLUMA_NATIVE_SUB_NOT_FOUND : DLUMA_NATIVE_SUB_PENDING;
    } else {
        expected = (snapshot.kind == DLUMA_LUA54_EVENT_SENSOR_ENTER ||
                    snapshot.kind == DLUMA_LUA54_EVENT_SENSOR_LEAVE)
        ? "SensorArea" : "InteractiveItem";
        state = (!g_dluma_native.backend.object_is_a ||
            !g_dluma_native.backend.object_is_a(
                g_dluma_native.backend.user, entry.object, expected))
            ? DLUMA_NATIVE_SUB_WRONG_CLASS : DLUMA_NATIVE_SUB_RESOLVED;
    }
    dluma_native_lock(&g_dluma_native.match_lock);
    if (!g_dluma_native.subscriptions[slot].active ||
        g_dluma_native.subscriptions[slot].owner_id != snapshot.owner_id ||
        g_dluma_native.subscriptions[slot].token != snapshot.token ||
        g_dluma_native.subscriptions[slot].registration_id !=
            snapshot.registration_id ||
        g_dluma_native.subscriptions[slot].index_epoch != snapshot.index_epoch ||
        snapshot.index_epoch != g_dluma_native.index_epoch ||
        g_dluma_native.subscriptions[slot].generation != snapshot.generation ||
        g_dluma_native.subscriptions[slot].kind != snapshot.kind ||
        !dluma_native_streq(g_dluma_native.subscriptions[slot].name,
                          snapshot.name)) {
        dluma_native_unlock(&g_dluma_native.match_lock);
        return 0;
    }
    old_state = g_dluma_native.subscriptions[slot].resolution_state;
    g_dluma_native.subscriptions[slot].resolution_state = (unsigned char)state;
    g_dluma_native.subscriptions[slot].next_retry_ms =
        state == DLUMA_NATIVE_SUB_RESOLVED ? 0 : retry_at;
    if (state == DLUMA_NATIVE_SUB_RESOLVED) {
        g_dluma_native.subscriptions[slot].index = index;
        g_dluma_native.subscriptions[slot].generation = g_dluma_native.generation;
        g_dluma_native.subscriptions[slot].index_epoch =
            g_dluma_native.index_epoch;
        g_dluma_native.subscriptions[slot].initial_sample = 0;
        g_dluma_native.subscriptions[slot].inside = 0;
    }
    if (old_state != state)
        (void)dluma_native_bump_subscription_key_revision_locked(
            snapshot.kind, snapshot.name);
    dluma_native_unlock(&g_dluma_native.match_lock);
    if (old_state != state) dluma_native_log_resolution(snapshot.name, state);
    return state == DLUMA_NATIVE_SUB_RESOLVED;
}

static void dluma_native_tick_subscription_resolution(void)
{
    uint32_t i;
    uint64_t now = dluma_native_now();
    if (g_dluma_native.index_phase != DLUMA_NATIVE_INDEX_COMPLETE) return;
    for (i = 0; i < DLUMA_NATIVE_MAX_SUBSCRIPTIONS; ++i) {
        DlumaNativeSubscription subscription;
        dluma_native_lock(&g_dluma_native.match_lock);
        subscription = g_dluma_native.subscriptions[i];
        dluma_native_unlock(&g_dluma_native.match_lock);
        if (!subscription.active ||
            subscription.generation != g_dluma_native.generation ||
            subscription.index_epoch != g_dluma_native.index_epoch ||
            subscription.resolution_state == DLUMA_NATIVE_SUB_RESOLVED ||
            (subscription.next_retry_ms && now < subscription.next_retry_ms))
            continue;
        (void)dluma_native_resolve_subscription(i);
    }
}

/* Commits a passive CheckInside result only if the exact registration and
 * map generation that produced the work item are still current.  This keeps
 * a remove/re-add or map reset from applying an old engine sample to a new
 * subscription epoch.  No engine call, Lua dispatch, or logging occurs while
 * the registry lock is held. */
static int dluma_native_commit_sensor_sample(
    uint32_t slot,
    const DlumaNativeSubscription* expected,
    int inside,
    int* baseline_out,
    uint32_t* event_kind_out,
    uint32_t* event_generation_out,
    uint64_t* event_index_epoch_out,
    uint64_t* event_subscription_key_revision_out,
    char* event_name_out,
    uint32_t event_name_capacity)
{
    DlumaNativeSubscription* source;
    uint32_t i;
    uint32_t edge_kind = inside
        ? DLUMA_LUA54_EVENT_SENSOR_ENTER : DLUMA_LUA54_EVENT_SENSOR_LEAVE;
    int was_initial;
    int prior_inside = 0;
    int changed;
    int edge_subscribed = 0;
    uint64_t edge_key_revision = 0;
    if (baseline_out) *baseline_out = 0;
    if (event_kind_out) *event_kind_out = 0;
    if (event_generation_out) *event_generation_out = 0;
    if (event_index_epoch_out) *event_index_epoch_out = 0;
    if (event_subscription_key_revision_out)
        *event_subscription_key_revision_out = 0;
    if (event_name_out && event_name_capacity) event_name_out[0] = 0;
    if (!expected || slot >= DLUMA_NATIVE_MAX_SUBSCRIPTIONS) return 0;
    inside = inside ? 1 : 0;
    dluma_native_lock(&g_dluma_native.match_lock);
    source = &g_dluma_native.subscriptions[slot];
    if (g_dluma_native.generation != expected->generation ||
        g_dluma_native.index_epoch != expected->index_epoch ||
        !source->active ||
        source->owner_id != expected->owner_id ||
        source->token != expected->token ||
        source->registration_id != expected->registration_id ||
        source->index_epoch != expected->index_epoch ||
        source->key_revision != expected->key_revision ||
        source->generation != expected->generation ||
        source->kind != expected->kind ||
        source->resolution_state != DLUMA_NATIVE_SUB_RESOLVED ||
        !dluma_native_streq(source->name, expected->name)) {
        dluma_native_unlock(&g_dluma_native.match_lock);
        return 0;
    }
    was_initial = 0;
    for (i = 0; i < DLUMA_NATIVE_MAX_SUBSCRIPTIONS; ++i) {
        const DlumaNativeSubscription* sibling =
            &g_dluma_native.subscriptions[i];
        if (!sibling->active || !sibling->initial_sample ||
            sibling->generation != expected->generation ||
            sibling->index_epoch != expected->index_epoch ||
            !dluma_native_streq(sibling->name, expected->name) ||
            (sibling->kind != DLUMA_LUA54_EVENT_SENSOR_ENTER &&
             sibling->kind != DLUMA_LUA54_EVENT_SENSOR_LEAVE)) continue;
        was_initial = 1;
        prior_inside = sibling->inside ? 1 : 0;
        break;
    }
    changed = was_initial && (prior_inside != inside);
    for (i = 0; i < DLUMA_NATIVE_MAX_SUBSCRIPTIONS; ++i) {
        DlumaNativeSubscription* sibling = &g_dluma_native.subscriptions[i];
        if (!sibling->active ||
            sibling->generation != expected->generation ||
            sibling->index_epoch != expected->index_epoch ||
            !dluma_native_streq(sibling->name, expected->name) ||
            (sibling->kind != DLUMA_LUA54_EVENT_SENSOR_ENTER &&
             sibling->kind != DLUMA_LUA54_EVENT_SENSOR_LEAVE)) continue;
        sibling->initial_sample = 1;
        sibling->inside = (unsigned char)inside;
        if (changed && sibling->kind == edge_kind &&
            sibling->resolution_state == DLUMA_NATIVE_SUB_RESOLVED) {
            edge_subscribed = 1;
            edge_key_revision = sibling->key_revision;
        }
    }
    if (!was_initial) {
        if (baseline_out) *baseline_out = 1;
    } else if (changed && edge_subscribed) {
        if (event_kind_out) *event_kind_out = edge_kind;
        if (event_generation_out) *event_generation_out = expected->generation;
        if (event_index_epoch_out)
            *event_index_epoch_out = expected->index_epoch;
        if (event_subscription_key_revision_out)
            *event_subscription_key_revision_out = edge_key_revision;
        if (event_name_out && event_name_capacity)
            dluma_str_copy(event_name_out, event_name_capacity, expected->name);
    }
    dluma_native_unlock(&g_dluma_native.match_lock);
    return 1;
}

static int dluma_native_has_subscription(
    const char* name, uint32_t kind, uint32_t generation,
    uint64_t index_epoch, uint64_t key_revision)
{
    uint32_t i;
    int found = 0;
    dluma_native_lock(&g_dluma_native.match_lock);
    for (i = 0; i < DLUMA_NATIVE_MAX_SUBSCRIPTIONS; ++i) {
        const DlumaNativeSubscription* subscription =
            &g_dluma_native.subscriptions[i];
        if (subscription->active && subscription->kind == kind &&
            subscription->generation == generation &&
            subscription->index_epoch == index_epoch &&
            subscription->key_revision == key_revision &&
            subscription->resolution_state == DLUMA_NATIVE_SUB_RESOLVED &&
            dluma_native_streq(subscription->name, name)) {
            found = 1;
            break;
        }
    }
    dluma_native_unlock(&g_dluma_native.match_lock);
    return found;
}

static void dluma_native_tick_sensors(void)
{
    uint32_t i;
    uint32_t polled_count = 0;
    char polled_names[DLUMA_NATIVE_MAX_SUBSCRIPTIONS]
                     [DLUMA_LUA54_MAX_OBJECT_NAME];
    uint64_t now = dluma_native_now();
    if (!(dluma_native_gameplay_readiness() & DLUMA_NATIVE_READY_SENSOR)) return;
    if (g_dluma_native.last_sensor_poll_ms &&
        now - g_dluma_native.last_sensor_poll_ms < DLUMA_NATIVE_SENSOR_POLL_MS) return;
    g_dluma_native.last_sensor_poll_ms = now;
    for (i = 0; i < DLUMA_NATIVE_MAX_SUBSCRIPTIONS; ++i) {
        DlumaNativeSubscription subscription;
        DlumaNativeIndexEntry entry;
        void* sensor_interface;
        uint32_t event_kind = 0;
        uint32_t event_generation = 0;
        uint64_t event_index_epoch = 0;
        uint64_t event_subscription_key_revision = 0;
        char event_name[DLUMA_LUA54_MAX_OBJECT_NAME];
        uint32_t j;
        int already_polled = 0;
        int baseline = 0;
        int inside;
        dluma_native_lock(&g_dluma_native.match_lock);
        subscription = g_dluma_native.subscriptions[i];
        memset(&entry, 0, sizeof(entry));
        if (subscription.index < g_dluma_native.index_count)
            entry = g_dluma_native.index[subscription.index];
        dluma_native_unlock(&g_dluma_native.match_lock);
        if (!subscription.active ||
            subscription.resolution_state != DLUMA_NATIVE_SUB_RESOLVED ||
            (subscription.kind != DLUMA_LUA54_EVENT_SENSOR_ENTER &&
             subscription.kind != DLUMA_LUA54_EVENT_SENSOR_LEAVE) ||
            !entry.object) continue;
        for (j = 0; j < polled_count; ++j) {
            if (dluma_native_streq(polled_names[j], subscription.name)) {
                already_polled = 1;
                break;
            }
        }
        if (already_polled) continue;
        dluma_str_copy(polled_names[polled_count],
                     DLUMA_ARRAY_COUNT(polled_names[polled_count]),
                     subscription.name);
        ++polled_count;
        if (!dluma_native_revalidate_entry(&entry)) {
            sensor_interface = 0;
        } else {
            sensor_interface = g_dluma_native.backend.sensor_area_interface
                ? g_dluma_native.backend.sensor_area_interface(
                    g_dluma_native.backend.user, entry.object) : 0;
        }
        if (!sensor_interface ||
            !g_dluma_native.backend.sensor_inside ||
            !g_dluma_native.backend.sensor_inside(
                g_dluma_native.backend.user, sensor_interface, &inside)) {
            dluma_native_lock(&g_dluma_native.match_lock);
            if (g_dluma_native.subscriptions[i].active &&
                g_dluma_native.subscriptions[i].owner_id == subscription.owner_id &&
                g_dluma_native.subscriptions[i].token == subscription.token &&
                g_dluma_native.subscriptions[i].registration_id ==
                    subscription.registration_id &&
                g_dluma_native.subscriptions[i].index_epoch ==
                    subscription.index_epoch &&
                subscription.index_epoch == g_dluma_native.index_epoch &&
                g_dluma_native.subscriptions[i].generation ==
                    subscription.generation &&
                g_dluma_native.subscriptions[i].resolution_state ==
                    DLUMA_NATIVE_SUB_RESOLVED &&
                g_dluma_native.subscriptions[i].kind == subscription.kind &&
                dluma_native_streq(g_dluma_native.subscriptions[i].name,
                                  subscription.name)) {
                g_dluma_native.subscriptions[i].resolution_state = DLUMA_NATIVE_SUB_PENDING;
                g_dluma_native.subscriptions[i].next_retry_ms =
                    now + DLUMA_NATIVE_RESOLVE_RETRY_MS;
                (void)dluma_native_bump_subscription_key_revision_locked(
                    subscription.kind, subscription.name);
            }
            dluma_native_unlock(&g_dluma_native.match_lock);
            continue;
        }
        inside = inside ? 1 : 0;
        if (!dluma_native_commit_sensor_sample(
                i, &subscription, inside, &baseline, &event_kind,
                &event_generation, &event_index_epoch,
                &event_subscription_key_revision, event_name,
                DLUMA_ARRAY_COUNT(event_name)))
            continue;
        if (baseline) {
            dluma_log_pair("INFO", "sensor_initial_state", subscription.name);
            continue;
        }
        if (event_kind) {
            int native_queued =
                dluma_native_mods_enqueue_game_event(
                    event_kind, event_generation, event_index_epoch,
                    event_subscription_key_revision, event_name,
                    inside, 0.0);
            int managed_queued =
                dluma_managed_host_enqueue_game_event(
                    event_kind, event_generation, event_index_epoch,
                    event_subscription_key_revision, event_name,
                    inside, 0.0);
            int lua_queued =
                dluma_lua_runtime_enqueue_event(
                    event_kind, event_generation, event_index_epoch,
                    event_subscription_key_revision, event_name,
                    inside, 0.0);
            if (native_queued || managed_queued || lua_queued)
                dluma_log_pair(
                    "INFO",
                    inside ? "sensor_enter_edge_queued"
                           : "sensor_leave_edge_queued",
                    event_name);
        }
    }
}

static void dluma_native_drain_hook_events(void)
{
    if (g_dluma_native.hook_overflow_logged == 1) {
        g_dluma_native.hook_overflow_logged = 2;
        dluma_log_line("ERROR", "InteractiveItem observer queue overflow; newest event was dropped.");
    }
    for (;;) {
        DlumaNativeHookEvent event;
        int have_event = 0;
        memset(&event, 0, sizeof(event));
        dluma_native_lock(&g_dluma_native.hook_event_lock);
        if (g_dluma_native.hook_event_count) {
            event = g_dluma_native.hook_events[g_dluma_native.hook_event_head];
            g_dluma_native.hook_event_head =
                (g_dluma_native.hook_event_head + 1u) % DLUMA_NATIVE_MAX_HOOK_EVENTS;
            --g_dluma_native.hook_event_count;
            have_event = 1;
        }
        dluma_native_unlock(&g_dluma_native.hook_event_lock);
        if (!have_event) break;
        if (event.generation != g_dluma_native.generation ||
            event.index_epoch != g_dluma_native.index_epoch) continue;
        if (!dluma_native_has_subscription(
                event.name, DLUMA_LUA54_EVENT_INTERACT_BEGIN,
                event.generation, event.index_epoch,
                event.subscription_key_revision)) {
            dluma_atomic_increment(&g_dluma_native.interaction.events_dropped);
            continue;
        }
        if (g_dluma_native.interaction_log_samples <
                DLUMA_NATIVE_REJECT_SAMPLE_LIMIT) {
            ++g_dluma_native.interaction_log_samples;
            dluma_log_pair("INFO", "interactive_item_original_success", event.name);
        }
        dluma_log_pair("INFO", "interactive_item_execute_observed", event.name);
        {
            int native_queued =
                dluma_native_mods_enqueue_game_event(
                    DLUMA_LUA54_EVENT_INTERACT_BEGIN,
                    event.generation,
                    event.index_epoch,
                    event.subscription_key_revision,
                    event.name,
                    1,
                    0.0);
            int managed_queued =
                dluma_managed_host_enqueue_game_event(
                    DLUMA_LUA54_EVENT_INTERACT_BEGIN,
                    event.generation,
                    event.index_epoch,
                    event.subscription_key_revision,
                    event.name,
                    1,
                    0.0);
            int lua_queued =
                dluma_lua_runtime_enqueue_event(
                    DLUMA_LUA54_EVENT_INTERACT_BEGIN, event.generation,
                    event.index_epoch, event.subscription_key_revision,
                    event.name, 1, 0.0);
            if (native_queued || managed_queued || lua_queued) {
            dluma_atomic_increment(&g_dluma_native.interaction.events_queued);
            dluma_log_pair("INFO", "interactive_item_event_queued", event.name);
            } else {
                dluma_atomic_increment(
                    &g_dluma_native.interaction.events_dropped);
            }
        }
    }
}

static char __fastcall dluma_native_interactive_hook(
    void* item, unsigned int action, __int64* context)
{
    DlumaNativeInteractiveExecuteFn original;
    char result = 0;
    dluma_atomic_increment(&g_dluma_native.hook_active_calls);
    dluma_atomic_increment(&g_dluma_native.interaction.hook_calls);
    if (action == 1u)
        dluma_atomic_increment(&g_dluma_native.interaction.action_execute);
    original = (DlumaNativeInteractiveExecuteFn)dluma_atomic_pointer_load(
        &g_dluma_interactive_original_lifetime);
    if (original) result = original(item, action, context);
    if (result)
        dluma_atomic_increment(&g_dluma_native.interaction.successful_original);
    if (!dluma_atomic_compare_exchange(
            &g_dluma_interactive_observation_stopped, 0, 0) &&
        !dluma_atomic_compare_exchange(&g_dluma_native.stopping, 0, 0) &&
        g_dluma_native.hook_installed &&
        g_dluma_native.interactive_slot &&
        dluma_atomic_pointer_load(
            (PVOID volatile*)g_dluma_native.interactive_slot) ==
                (PVOID)dluma_native_interactive_hook &&
        action == 1u && result)
        (void)dluma_native_gameplay_enqueue_interactive_item_event(
            item, action, result);
    dluma_atomic_decrement(&g_dluma_native.hook_active_calls);
    return result;
}

static int dluma_native_interactive_hook_current(void)
{
    int current;
    if (!dluma_native_try_lock(&g_dluma_interactive_lifecycle_lock)) return 0;
    current = dluma_native_interactive_hook_owned_locked();
    dluma_atomic_exchange(
        &g_dluma_interactive_observation_stopped, current ? 0 : 1);
    dluma_native_unlock(&g_dluma_interactive_lifecycle_lock);
    return current;
}

static int dluma_native_interactive_hook_owned_locked(void)
{
    return g_dluma_native.hook_installed &&
        g_dluma_native.interactive_slot &&
        g_dluma_native.interactive_original &&
        dluma_atomic_pointer_load(&g_dluma_interactive_original_lifetime) ==
            g_dluma_native.interactive_original &&
        !dluma_atomic_compare_exchange(&g_dluma_native.stopping, 0, 0) &&
        dluma_is_readable(g_dluma_native.interactive_slot, sizeof(void*)) &&
        dluma_atomic_pointer_load(
            (PVOID volatile*)g_dluma_native.interactive_slot) ==
                (PVOID)dluma_native_interactive_hook;
}

#if defined(DLUMA_NATIVE_FAKE_TESTS)
int dluma_native_gameplay_fake_validate_get_rtti_stub(
    const unsigned char* image,
    uint32_t image_size,
    uint32_t function_offset,
    uint32_t expected_rtti_offset)
{
    void* expected_rtti = 0;
    if (!image || function_offset >= image_size ||
        expected_rtti_offset >= image_size) return 0;
    return dluma_native_decode_gameplay_rtti_getter(
               image, image_size, image + function_offset, 0,
               &expected_rtti) &&
           expected_rtti == image + expected_rtti_offset;
}

int dluma_native_gameplay_fake_invoke_interaction_observer(
    DlumaNativeFakeInteractiveOriginalFn original,
    void* item,
    uint32_t action,
    __int64* context)
{
    void* saved_original = g_dluma_native.interactive_original;
    void** saved_slot = g_dluma_native.interactive_slot;
    unsigned char saved_installed = g_dluma_native.hook_installed;
    void* observer_slot = (void*)dluma_native_interactive_hook;
    PVOID saved_lifetime = dluma_atomic_pointer_load(
        &g_dluma_interactive_original_lifetime);
    LONG saved_observation = dluma_atomic_compare_exchange(
        &g_dluma_interactive_observation_stopped, 0, 0);
    char result;
    g_dluma_native.interactive_original = (void*)original;
    g_dluma_native.interactive_slot = &observer_slot;
    g_dluma_native.hook_installed = 1;
    dluma_atomic_pointer_exchange(
        &g_dluma_interactive_original_lifetime, (PVOID)original);
    dluma_atomic_exchange(&g_dluma_interactive_observation_stopped, 0);
    result = dluma_native_interactive_hook(item, action, context);
    dluma_atomic_exchange(&g_dluma_interactive_observation_stopped, 1);
    dluma_atomic_pointer_exchange(
        &g_dluma_interactive_original_lifetime, saved_lifetime);
    g_dluma_native.interactive_original = saved_original;
    g_dluma_native.interactive_slot = saved_slot;
    g_dluma_native.hook_installed = saved_installed;
    dluma_atomic_exchange(
        &g_dluma_interactive_observation_stopped, saved_observation);
    return (int)(unsigned char)result;
}

int dluma_native_gameplay_fake_install_interaction_slot(
    void** writable_slot,
    DlumaNativeFakeInteractiveOriginalFn original)
{
    void** saved_slot;
    void* saved_original;
    PVOID saved_lifetime;
    int installed = 0;
    if (!writable_slot || !original || !g_dluma_native.started) return 0;
    dluma_native_lock(&g_dluma_interactive_lifecycle_lock);
    if (dluma_atomic_compare_exchange(&g_dluma_native.stopping, 0, 0)) {
        dluma_native_unlock(&g_dluma_interactive_lifecycle_lock);
        return 0;
    }
    saved_slot = g_dluma_native.interactive_slot;
    saved_original = g_dluma_native.interactive_original;
    saved_lifetime = dluma_atomic_pointer_load(
        &g_dluma_interactive_original_lifetime);
    dluma_atomic_exchange(&g_dluma_interactive_observation_stopped, 1);
    g_dluma_native.interactive_slot = writable_slot;
    g_dluma_native.interactive_original = (void*)original;
    dluma_atomic_pointer_exchange(
        &g_dluma_interactive_original_lifetime, (PVOID)original);
    if (dluma_atomic_pointer_compare_exchange(
            (PVOID volatile*)writable_slot,
            (PVOID)dluma_native_interactive_hook,
            (PVOID)original) != (PVOID)original) {
        dluma_atomic_pointer_exchange(
            &g_dluma_interactive_original_lifetime, saved_lifetime);
        g_dluma_native.interactive_original = saved_original;
        g_dluma_native.interactive_slot = saved_slot;
        dluma_native_unlock(&g_dluma_interactive_lifecycle_lock);
        return 0;
    }
    g_dluma_native.hook_installed = 1;
    if (!dluma_atomic_compare_exchange(&g_dluma_native.stopping, 0, 0)) {
        dluma_atomic_exchange(&g_dluma_interactive_observation_stopped, 0);
        installed = 1;
    }
    dluma_native_unlock(&g_dluma_interactive_lifecycle_lock);
    return installed;
}

void dluma_native_gameplay_fake_set_quiesce_lock_contention(
    int match_locked,
    int lifecycle_locked)
{
    dluma_atomic_exchange(&g_dluma_native.match_lock, match_locked ? 1 : 0);
    dluma_atomic_exchange(
        &g_dluma_interactive_lifecycle_lock, lifecycle_locked ? 1 : 0);
}
#endif

static int dluma_native_install_interactive_hook(void)
{
    void** vtable;
    void** slot;
    void* expected;
    DWORD old_protect;
    DWORD restored_protect;
    HMODULE pinned_module = 0;
    PVOID previous_slot;
    int installed = 0;
    vtable = (void**)(g_dluma_native.production.gamedll_base +
                      DLUMA_NATIVE_INTERACTIVE_ITEM_VTABLE_RVA);
    slot = (void**)((BYTE*)vtable + DLUMA_NATIVE_INTERACTIVE_EXECUTE_SLOT);
    expected = g_dluma_native.production.gamedll_base +
               DLUMA_NATIVE_INTERACTIVE_EXECUTE_RVA;
    dluma_native_lock(&g_dluma_interactive_lifecycle_lock);
    if (dluma_atomic_compare_exchange(&g_dluma_native.stopping, 0, 0) ||
        !dluma_is_readable(vtable - 1, sizeof(void*)) ||
        !dluma_is_readable(slot, sizeof(void*)) || *slot != expected ||
        !dluma_native_prefix_matches(expected, g_dluma_interactive_execute_prefix,
                                   DLUMA_ARRAY_COUNT(g_dluma_interactive_execute_prefix)) ||
        !GetModuleHandleExW(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                GET_MODULE_HANDLE_EX_FLAG_PIN,
            (LPCWSTR)(ULONG_PTR)dluma_native_interactive_hook,
            &pinned_module) ||
        !VirtualProtect(slot, sizeof(*slot), PAGE_READWRITE, &old_protect))
        goto done;
    g_dluma_native.interactive_slot = slot;
    g_dluma_native.interactive_original = expected;
    dluma_atomic_pointer_exchange(
        &g_dluma_interactive_original_lifetime, expected);
    dluma_atomic_exchange(&g_dluma_interactive_observation_stopped, 1);
    previous_slot = dluma_atomic_pointer_compare_exchange(
        (PVOID volatile*)slot,
        (PVOID)dluma_native_interactive_hook,
        expected);
    if (previous_slot != expected) {
        if (!VirtualProtect(slot, sizeof(*slot), old_protect,
                            &restored_protect))
            dluma_log_line("WARN", "InteractiveItem vtable protection could not be restored after hook ownership changed.");
        goto done;
    }
    if (!VirtualProtect(slot, sizeof(*slot), old_protect,
                        &restored_protect)) {
        previous_slot = dluma_atomic_pointer_compare_exchange(
            (PVOID volatile*)slot, expected,
            (PVOID)dluma_native_interactive_hook);
        if (previous_slot != (PVOID)dluma_native_interactive_hook)
            dluma_log_line("WARN", "InteractiveItem hook ownership changed during installation rollback; foreign slot value preserved.");
        FlushInstructionCache(GetCurrentProcess(), slot, sizeof(*slot));
        if (!VirtualProtect(slot, sizeof(*slot), old_protect,
                            &restored_protect))
            dluma_log_line("WARN", "InteractiveItem vtable protection could not be restored after hook installation rollback.");
        goto done;
    }
    FlushInstructionCache(GetCurrentProcess(), slot, sizeof(*slot));
    g_dluma_native.hook_installed = 1;
    if (!dluma_atomic_compare_exchange(&g_dluma_native.stopping, 0, 0)) {
        dluma_atomic_exchange(&g_dluma_interactive_observation_stopped, 0);
        installed = 1;
    }
done:
    dluma_native_unlock(&g_dluma_interactive_lifecycle_lock);
    if (installed)
        dluma_log_pair("INFO", "interactive_item_hook_installed", "InteractiveItem::OnExecuteTrigger");
    return installed;
}

static int dluma_native_validate_sensor_adapter(void)
{
    void** vtable = (void**)(g_dluma_native.production.gamedll_base +
                             DLUMA_NATIVE_SENSOR_VTABLE_RVA);
    void** slot = (void**)((BYTE*)vtable + DLUMA_NATIVE_SENSOR_CHECK_INSIDE_SLOT);
    void* check = g_dluma_native.production.gamedll_base + DLUMA_NATIVE_SENSOR_CHECK_RVA;
    void* check_inside = g_dluma_native.production.gamedll_base +
                         DLUMA_NATIVE_SENSOR_CHECK_INSIDE_RVA;
    return dluma_is_readable(vtable - 1, sizeof(void*)) &&
           dluma_is_readable(slot, sizeof(void*)) && *slot == check_inside &&
           dluma_native_is_executable(check, 1) &&
           dluma_native_prefix_matches(check_inside, g_dluma_sensor_check_inside_prefix,
                                     DLUMA_ARRAY_COUNT(g_dluma_sensor_check_inside_prefix));
}

static int dluma_native_validate_action_chain_adapter(void)
{
    void** vtable = (void**)(g_dluma_native.production.gamedll_base +
                             DLUMA_NATIVE_ACTION_CHAIN_VTABLE_RVA);
    void* run = g_dluma_native.production.gamedll_base +
                DLUMA_NATIVE_ACTION_CHAIN_RUN_RVA;
    return dluma_is_readable(vtable - 1, sizeof(void*)) &&
           dluma_is_readable(vtable, sizeof(void*)) &&
           dluma_native_is_executable(*vtable, 1) &&
           dluma_native_is_executable(run, 1) &&
           dluma_native_prefix_matches(
               run, g_dluma_action_chain_run_prefix,
               DLUMA_ARRAY_COUNT(g_dluma_action_chain_run_prefix));
}

static int dluma_native_validate_quest_adapter(void)
{
    DlumaNativeProductionBackend* production = &g_dluma_native.production;
    void** vtable;
    void* find_quest;
    void* find_phase;
    void* start_synchronized_invoke;
    if (!production->gamedll_base ||
        DLUMA_NATIVE_QUEST_MANAGER_RVA +
            DLUMA_NATIVE_QUEST_MANAGER_CLIENT_STATE_OFFSET + sizeof(void*) >
            production->gamedll_image_size ||
        DLUMA_NATIVE_QUEST_MANAGER_VTABLE_RVA + sizeof(void*) >
            production->gamedll_image_size ||
        DLUMA_NATIVE_QUEST_FIND_RVA + DLUMA_ARRAY_COUNT(g_dluma_quest_find_prefix) >
            production->gamedll_image_size ||
        DLUMA_NATIVE_QUEST_FIND_PHASE_RVA +
            DLUMA_ARRAY_COUNT(g_dluma_quest_find_phase_prefix) >
            production->gamedll_image_size ||
        DLUMA_NATIVE_QUEST_START_SYNCHRONIZED_INVOKE_RVA +
            DLUMA_ARRAY_COUNT(g_dluma_quest_start_synchronized_invoke_prefix) >
            production->gamedll_image_size)
        return 0;
    vtable = (void**)(production->gamedll_base +
        DLUMA_NATIVE_QUEST_MANAGER_VTABLE_RVA);
    find_quest = production->gamedll_base + DLUMA_NATIVE_QUEST_FIND_RVA;
    find_phase = production->gamedll_base + DLUMA_NATIVE_QUEST_FIND_PHASE_RVA;
    start_synchronized_invoke = production->gamedll_base +
        DLUMA_NATIVE_QUEST_START_SYNCHRONIZED_INVOKE_RVA;
    return dluma_is_readable(production->gamedll_base +
               DLUMA_NATIVE_QUEST_MANAGER_RVA,
               DLUMA_NATIVE_QUEST_MANAGER_CLIENT_STATE_OFFSET + sizeof(void*)) &&
           dluma_is_readable(vtable - 1, sizeof(void*)) &&
           dluma_is_readable(vtable, sizeof(void*)) &&
           dluma_native_is_executable(*vtable, 1) &&
           dluma_native_prefix_matches(find_quest, g_dluma_quest_find_prefix,
               DLUMA_ARRAY_COUNT(g_dluma_quest_find_prefix)) &&
           dluma_native_prefix_matches(find_phase, g_dluma_quest_find_phase_prefix,
               DLUMA_ARRAY_COUNT(g_dluma_quest_find_phase_prefix)) &&
           dluma_native_prefix_matches(start_synchronized_invoke,
               g_dluma_quest_start_synchronized_invoke_prefix,
               DLUMA_ARRAY_COUNT(g_dluma_quest_start_synchronized_invoke_prefix));
}

static int dluma_native_validate_local_adapter(void)
{
    BYTE* enable = g_dluma_native.production.gamedll_base + DLUMA_NATIVE_LOCAL_ENABLE_RVA;
    BYTE* disable = g_dluma_native.production.gamedll_base + DLUMA_NATIVE_LOCAL_DISABLE_RVA;
    BYTE* get = g_dluma_native.production.gamedll_base + DLUMA_NATIVE_LOCAL_IS_ENABLED_RVA;
    return dluma_native_prefix_matches(enable, g_dluma_local_enable_prefix,
                                     DLUMA_ARRAY_COUNT(g_dluma_local_enable_prefix)) &&
           dluma_native_prefix_matches(disable, g_dluma_local_disable_prefix,
                                     DLUMA_ARRAY_COUNT(g_dluma_local_disable_prefix)) &&
           dluma_native_prefix_matches(get, g_dluma_local_is_enabled_prefix,
                                     DLUMA_ARRAY_COUNT(g_dluma_local_is_enabled_prefix));
}

static int dluma_native_validate_world_position_adapter(void)
{
    void* position = 0;
    void* forward = 0;
    return dluma_native_export_at(g_dluma_native.production.engine,
               "?GetWorldPosition@IControlObject@@QEBA?AVvec3@@XZ",
               DLUMA_NATIVE_CONTROL_GET_WORLD_POSITION_RVA,
               g_dluma_control_get_world_position_prefix,
               DLUMA_ARRAY_COUNT(g_dluma_control_get_world_position_prefix),
               &position) &&
           dluma_native_export_at(g_dluma_native.production.engine,
               "?GetForwardVector@IControlObject@@QEBA?AVvec3@@XZ",
               DLUMA_NATIVE_CONTROL_GET_FORWARD_VECTOR_RVA,
               g_dluma_control_get_forward_vector_prefix,
               DLUMA_ARRAY_COUNT(g_dluma_control_get_forward_vector_prefix),
               &forward) &&
           position == g_dluma_native.production.engine_base +
               DLUMA_NATIVE_CONTROL_GET_WORLD_POSITION_RVA &&
           forward == g_dluma_native.production.engine_base +
               DLUMA_NATIVE_CONTROL_GET_FORWARD_VECTOR_RVA;
}

static int dluma_native_validate_subtitle_adapter(void)
{
    void** level_vtable = (void**)(g_dluma_native.production.gamedll_base +
                                   DLUMA_NATIVE_LEVELDI_VTABLE_RVA);
    void** movie_vtable = (void**)(g_dluma_native.production.gamedll_base +
                                   DLUMA_NATIVE_GAME_MOVIE_VTABLE_RVA);
    void* level_show = g_dluma_native.production.gamedll_base +
                       DLUMA_NATIVE_LEVEL_SHOW_SUBTITLE_RVA;
    void* level_hide = g_dluma_native.production.gamedll_base +
                       DLUMA_NATIVE_LEVEL_HIDE_SUBTITLE_RVA;
    void* movie_show = g_dluma_native.production.gamedll_base +
                       DLUMA_NATIVE_GAME_MOVIE_SHOW_SUBTITLE_RVA;
    void* movie_hide = g_dluma_native.production.gamedll_base +
                       DLUMA_NATIVE_GAME_MOVIE_HIDE_SUBTITLE_RVA;
    return dluma_is_readable(level_vtable - 1, sizeof(void*)) &&
           dluma_is_readable((BYTE*)level_vtable +
               DLUMA_NATIVE_LEVEL_HIDE_SUBTITLE_SLOT, sizeof(void*)) &&
           dluma_is_readable(movie_vtable - 1, sizeof(void*)) &&
           dluma_is_readable((BYTE*)movie_vtable +
               DLUMA_NATIVE_GAME_MOVIE_HIDE_SUBTITLE_SLOT, sizeof(void*)) &&
           *(void**)((BYTE*)level_vtable +
               DLUMA_NATIVE_LEVEL_SHOW_SUBTITLE_SLOT) == level_show &&
           *(void**)((BYTE*)level_vtable +
               DLUMA_NATIVE_LEVEL_HIDE_SUBTITLE_SLOT) == level_hide &&
           *(void**)((BYTE*)movie_vtable +
               DLUMA_NATIVE_GAME_MOVIE_SHOW_SUBTITLE_SLOT) == movie_show &&
           *(void**)((BYTE*)movie_vtable +
               DLUMA_NATIVE_GAME_MOVIE_HIDE_SUBTITLE_SLOT) == movie_hide &&
           dluma_native_prefix_matches(level_show,
               g_dluma_level_show_subtitle_prefix,
               DLUMA_ARRAY_COUNT(g_dluma_level_show_subtitle_prefix)) &&
           dluma_native_prefix_matches(level_hide,
               g_dluma_level_hide_subtitle_prefix,
               DLUMA_ARRAY_COUNT(g_dluma_level_hide_subtitle_prefix)) &&
           dluma_native_prefix_matches(movie_show,
               g_dluma_movie_show_subtitle_prefix,
               DLUMA_ARRAY_COUNT(g_dluma_movie_show_subtitle_prefix)) &&
           dluma_native_prefix_matches(movie_hide,
               g_dluma_movie_hide_subtitle_prefix,
               DLUMA_ARRAY_COUNT(g_dluma_movie_hide_subtitle_prefix));
}

static int dluma_native_validate_radio_dialog_adapter(void)
{
    void** manager_vtable = (void**)(g_dluma_native.production.gamedll_base +
                                     DLUMA_NATIVE_DIALOG_MANAGER_VTABLE_RVA);
    void** vtable = (void**)(g_dluma_native.production.gamedll_base +
                             DLUMA_NATIVE_DIALOG_SPEAKER_VTABLE_RVA);
    void* manager_init = g_dluma_native.production.gamedll_base +
                         DLUMA_NATIVE_DIALOG_MANAGER_INIT_RVA;
    void* manager_find_speaker = g_dluma_native.production.gamedll_base +
        DLUMA_NATIVE_DIALOG_MANAGER_FIND_SPEAKER_RVA;
    void* manager_find_group = g_dluma_native.production.gamedll_base +
        DLUMA_NATIVE_DIALOG_MANAGER_FIND_GROUP_RVA;
    void* find_group = g_dluma_native.production.gamedll_base +
                       DLUMA_NATIVE_DIALOG_SPEAKER_FIND_GROUP_RVA;
    void* set_quest = g_dluma_native.production.gamedll_base +
                      DLUMA_NATIVE_DIALOG_SPEAKER_SET_QUEST_RVA;
    void* get_level_name = g_dluma_native.production.engine_base +
                           DLUMA_NATIVE_ILEVEL_GET_LEVEL_NAME_RVA;
    return g_dluma.engine.get_level_name ==
               (const char* (__fastcall *)(void*))get_level_name &&
           dluma_native_prefix_matches(get_level_name,
               g_dluma_get_level_name_prefix,
               DLUMA_ARRAY_COUNT(g_dluma_get_level_name_prefix)) &&
           dluma_is_readable(manager_vtable - 1, sizeof(void*)) &&
           dluma_is_readable(g_dluma_native.production.gamedll_base +
               DLUMA_NATIVE_DIALOG_MANAGER_RVA, sizeof(void*)) &&
           dluma_native_prefix_matches(manager_init,
               g_dluma_dialog_manager_init_prefix,
               DLUMA_ARRAY_COUNT(g_dluma_dialog_manager_init_prefix)) &&
           dluma_native_prefix_matches(manager_find_speaker,
               g_dluma_dialog_manager_find_speaker_prefix,
               DLUMA_ARRAY_COUNT(g_dluma_dialog_manager_find_speaker_prefix)) &&
           dluma_native_prefix_matches(manager_find_group,
               g_dluma_dialog_manager_find_group_prefix,
               DLUMA_ARRAY_COUNT(g_dluma_dialog_manager_find_group_prefix)) &&
           dluma_is_readable(vtable - 1, sizeof(void*)) &&
           dluma_is_readable((BYTE*)vtable +
               DLUMA_NATIVE_DIALOG_SPEAKER_SET_QUEST_SLOT, sizeof(void*)) &&
           *(void**)((BYTE*)vtable +
               DLUMA_NATIVE_DIALOG_SPEAKER_FIND_GROUP_SLOT) == find_group &&
           *(void**)((BYTE*)vtable +
               DLUMA_NATIVE_DIALOG_SPEAKER_SET_QUEST_SLOT) == set_quest &&
           dluma_native_prefix_matches(find_group,
               g_dluma_dialog_find_group_prefix,
               DLUMA_ARRAY_COUNT(g_dluma_dialog_find_group_prefix)) &&
           dluma_native_prefix_matches(set_quest,
               g_dluma_dialog_set_quest_prefix,
               DLUMA_ARRAY_COUNT(g_dluma_dialog_set_quest_prefix));
}

static int dluma_native_validate_igsobject_rtti_slot(void)
{
    void** vtable = (void**)(g_dluma_native.production.engine_base +
                             DLUMA_NATIVE_IGSOBJECT_VTABLE_RVA);
    void* get_rtti;
    void* get_engine_rtti;
    if (!dluma_is_readable(vtable - 1, sizeof(void*)) ||
        !dluma_is_readable((BYTE*)vtable +
            DLUMA_NATIVE_IGSOBJECT_GET_ENGINE_RTTI_SLOT, sizeof(void*))) return 0;
    get_rtti = *(void**)((BYTE*)vtable + DLUMA_NATIVE_IGSOBJECT_GET_RTTI_SLOT);
    get_engine_rtti = *(void**)((BYTE*)vtable +
                                DLUMA_NATIVE_IGSOBJECT_GET_ENGINE_RTTI_SLOT);
    return get_rtti == g_dluma_native.production.engine_base +
                       DLUMA_NATIVE_IGSOBJECT_GET_RTTI_RVA &&
           get_engine_rtti == g_dluma_native.production.engine_base +
                              DLUMA_NATIVE_IGSOBJECT_GET_ENGINE_RTTI_RVA &&
           get_rtti != get_engine_rtti &&
           dluma_native_prefix_matches(get_rtti, g_dluma_get_rtti_prefix,
                                     DLUMA_ARRAY_COUNT(g_dluma_get_rtti_prefix)) &&
           dluma_native_decode_gameplay_rtti_getter(
               g_dluma_native.production.engine_base,
               g_dluma_native.production.engine_image_size,
               get_rtti, 1, 0);
}

static int dluma_native_prepare_production_backend(void)
{
    DWORD engine_timestamp;
    DWORD engine_size;
    DWORD game_timestamp;
    DWORD game_size;
    void* proc;
    DlumaNativeGameplayBackend* backend = &g_dluma_native.backend;
    DlumaNativeProductionBackend* production = &g_dluma_native.production;
    production->engine = GetModuleHandleW(L"engine_x64_rwdi.dll");
    production->gamedll = GetModuleHandleW(L"gamedll_x64_rwdi.dll");
    if (!production->engine || !production->gamedll) {
        dluma_log_line("WARN", "native_adapter_modules_unavailable");
        g_dluma_native.started = 1;
        g_dluma_native.production_backend = 1;
        return 1;
    }
    production->engine_base = (BYTE*)production->engine;
    production->gamedll_base = (BYTE*)production->gamedll;
    if (!dluma_native_get_pe_identity(production->engine, &engine_timestamp, &engine_size) ||
        !dluma_native_get_pe_identity(production->gamedll, &game_timestamp, &game_size) ||
        engine_timestamp != DLUMA_NATIVE_ENGINE_TIMESTAMP ||
        engine_size != DLUMA_NATIVE_ENGINE_IMAGE_SIZE ||
        game_timestamp != DLUMA_NATIVE_GAMEDLL_TIMESTAMP ||
        game_size != DLUMA_NATIVE_GAMEDLL_IMAGE_SIZE) {
        dluma_log_line("WARN", "native_adapter_fingerprint_mismatch");
        g_dluma_native.started = 1;
        g_dluma_native.production_backend = 1;
        return 1;
    }
    production->engine_image_size = engine_size;
    production->gamedll_image_size = game_size;
    dluma_log_line("INFO", "native_adapter_fingerprint_match");
    /* A matching PE pair is audited at most once.  Any later target mismatch
     * leaves every dependent readiness bit false instead of retrying or
     * partially advertising the adapter. */
    g_dluma_native.started = 1;
    g_dluma_native.production_backend = 1;
    if (!dluma_native_export_at(production->engine,
            "?GetObjectByID@ILevel@@QEBAPEAVIGSObject@@H@Z",
            DLUMA_NATIVE_ILEVEL_GET_OBJECT_BY_ID_RVA,
            g_dluma_get_object_by_id_prefix,
            DLUMA_ARRAY_COUNT(g_dluma_get_object_by_id_prefix), &proc)) return 1;
    production->get_object_by_id = (DlumaNativeGetObjectByIdFn)proc;
    if (!dluma_native_export_at(production->engine,
            "?GetPrev@IGSObject@@QEBAPEAV1@XZ",
            DLUMA_NATIVE_IGSOBJECT_GET_PREV_RVA,
            g_dluma_get_prev_prefix,
            DLUMA_ARRAY_COUNT(g_dluma_get_prev_prefix), &proc)) return 1;
    production->get_prev = (DlumaNativeObjectLinkFn)proc;
    if (!dluma_native_export_at(production->engine,
            "?GetNext@IGSObject@@QEBAPEAV1@XZ",
            DLUMA_NATIVE_IGSOBJECT_GET_NEXT_RVA,
            g_dluma_get_next_prefix,
            DLUMA_ARRAY_COUNT(g_dluma_get_next_prefix), &proc)) return 1;
    production->get_next = (DlumaNativeObjectLinkFn)proc;
    if (!dluma_native_export_at(production->engine,
            "?GetLevel@IGSObject@@QEBAPEAVILevel@@XZ",
            DLUMA_NATIVE_IGSOBJECT_GET_LEVEL_RVA,
            g_dluma_get_level_prefix,
            DLUMA_ARRAY_COUNT(g_dluma_get_level_prefix), &proc)) return 1;
    production->get_level = (DlumaNativeObjectLevelFn)proc;
    if (!dluma_native_export_at(production->engine,
            "?GetID@IGSObject@@QEBAHXZ",
            DLUMA_NATIVE_IGSOBJECT_GET_ID_RVA,
            g_dluma_get_id_prefix,
            DLUMA_ARRAY_COUNT(g_dluma_get_id_prefix), &proc)) return 1;
    production->get_id = (DlumaNativeObjectIdFn)proc;
    if (!dluma_native_export_at(production->engine,
            "?GetClassNameA@CRTTI@@UEBAPEBDXZ",
            DLUMA_NATIVE_CRTTI_GET_CLASS_NAME_A_RVA,
            g_dluma_get_class_name_prefix,
            DLUMA_ARRAY_COUNT(g_dluma_get_class_name_prefix), &proc)) return 1;
    production->get_class_name = (DlumaNativeClassNameFn)proc;
    if (!dluma_native_export_at(production->engine,
            "?FromClass@CRTTI@@QEBA_NPEBD@Z",
            DLUMA_NATIVE_CRTTI_FROM_CLASS_NAME_RVA,
            g_dluma_from_class_prefix,
            DLUMA_ARRAY_COUNT(g_dluma_from_class_prefix), &proc)) return 1;
    production->from_class_name = (DlumaNativeFromClassFn)proc;
    if (!dluma_native_validate_igsobject_rtti_slot()) {
        dluma_log_line("WARN", "native_adapter_get_rtti_slot_validation_failed");
        return 1;
    }

    memset(backend, 0, sizeof(*backend));
    backend->struct_size = sizeof(*backend);
    backend->user = production;
    backend->clock_ms = dluma_native_prod_clock;
    backend->active_level = dluma_native_prod_active_level;
    backend->object_by_id = dluma_native_prod_object_by_id;
    backend->object_prev = dluma_native_prod_object_prev;
    backend->object_next = dluma_native_prod_object_next;
    backend->object_level = dluma_native_prod_object_level;
    backend->level_name = dluma_native_prod_level_name;
    backend->object_id = dluma_native_prod_object_id;
    backend->object_name = dluma_native_prod_object_name;
    backend->object_class = dluma_native_prod_object_class;
    backend->object_is_a = dluma_native_prod_object_is_a;
    backend->complete_object = dluma_native_prod_complete_object;
    backend->control_object_interface = dluma_native_prod_control_object_interface;
    backend->world_get_position = dluma_native_prod_world_get_position;
    backend->world_get_forward = dluma_native_prod_world_get_forward;
    backend->local_enable_interface = dluma_native_prod_local_enable_interface;
    backend->local_enable_get = dluma_native_prod_local_enable_get;
    backend->local_enable_set = dluma_native_prod_local_enable_set;
    backend->sensor_area_interface = dluma_native_prod_sensor_area_interface;
    backend->sensor_inside = dluma_native_prod_sensor_inside;
    backend->subtitle_show = dluma_native_prod_subtitle_show;
    backend->subtitle_hide = dluma_native_prod_subtitle_hide;
    backend->radio_dialog = dluma_native_prod_radio_dialog;
    backend->action_run_chain = dluma_native_prod_action_run_chain;
    backend->readiness = DLUMA_NATIVE_READY_WORLD_FIND;
    if (dluma_native_validate_world_position_adapter()) {
        backend->readiness |= DLUMA_NATIVE_READY_WORLD_POSITION;
        dluma_log_line("INFO", "world_position_ready");
    } else {
        dluma_log_line("WARN", "native_adapter_world_position_static_validation_failed");
        dluma_log_line("WARN", "world_position_failed");
    }
    if (dluma_native_validate_local_adapter())
        backend->readiness |= DLUMA_NATIVE_READY_LOCAL_ENABLE;
    else
        dluma_log_line("WARN", "native_adapter_local_enable_static_validation_failed");
    if (dluma_native_validate_sensor_adapter())
        backend->readiness |= DLUMA_NATIVE_READY_SENSOR;
    else
        dluma_log_line("WARN", "native_adapter_sensor_static_validation_failed");
    if (dluma_native_install_interactive_hook())
        backend->readiness |= DLUMA_NATIVE_READY_INTERACT;
    else
        dluma_log_line("WARN", "native_adapter_interaction_static_validation_failed");
    if (dluma_native_validate_subtitle_adapter())
        backend->readiness |= DLUMA_NATIVE_READY_SUBTITLES;
    else
        dluma_log_line("WARN", "native_adapter_subtitle_static_validation_failed");
    if (dluma_native_validate_radio_dialog_adapter())
        backend->readiness |= DLUMA_NATIVE_READY_RADIO_DIALOG;
    else
        dluma_log_line("WARN", "native_adapter_radio_dialog_static_validation_failed");
    if (dluma_native_validate_action_chain_adapter())
        backend->readiness |= DLUMA_NATIVE_READY_ACTION_CHAIN;
    else
        dluma_log_line("WARN", "native_adapter_action_chain_static_validation_failed");
    if (dluma_native_validate_quest_adapter()) {
        production->quest_find = (DlumaNativeQuestFindFn)(
            production->gamedll_base + DLUMA_NATIVE_QUEST_FIND_RVA);
        production->quest_find_phase = (DlumaNativeQuestFindPhaseFn)(
            production->gamedll_base + DLUMA_NATIVE_QUEST_FIND_PHASE_RVA);
        production->quest_start_synchronized_invoke =
            (DlumaNativeQuestStartSynchronizedInvokeFn)(
                production->gamedll_base +
                DLUMA_NATIVE_QUEST_START_SYNCHRONIZED_INVOKE_RVA);
        backend->quest_mutate = dluma_native_prod_quest_mutate;
        backend->readiness |= DLUMA_NATIVE_READY_QUEST_MUTATION;
        dluma_log_line("INFO", "native_adapter_quest_synchronization_bound");
    } else {
        dluma_log_line("WARN", "native_adapter_quest_static_validation_failed");
    }
    g_dluma_native.static_readiness = backend->readiness;
    dluma_native_set_supported_state(DLUMA_NATIVE_CAP_STATIC_VALIDATED);
    dluma_native_set_supported_state(DLUMA_NATIVE_CAP_WAITING_FOR_MAP);
    return 1;
}

int dluma_native_gameplay_start(void)
{
    uint32_t generation;
    void* level;
    char map[DLUMA_MAX_MAP_NAME];
    if (g_dluma_native.started) return 1;
    generation = g_dluma_native.generation;
    level = g_dluma_native.level;
    dluma_str_copy(map, DLUMA_ARRAY_COUNT(map), g_dluma_native.map);
    memset(&g_dluma_native, 0, sizeof(g_dluma_native));
    g_dluma_native.generation = generation;
    g_dluma_native.level = level;
    dluma_str_copy(g_dluma_native.map, DLUMA_ARRAY_COUNT(g_dluma_native.map), map);
    g_dluma_native.next_handle_id = 1;
    if (!dluma_native_prepare_production_backend()) return 0;
    if (g_dluma_native.production_backend && dluma_quest_hud_start()) {
        g_dluma_native.backend.readiness |= DLUMA_NATIVE_READY_QUEST_HUD;
        g_dluma_native.static_readiness |= DLUMA_NATIVE_READY_QUEST_HUD;
        dluma_native_set_capability_state(
            DLUMA_NATIVE_READY_QUEST_HUD, DLUMA_NATIVE_CAP_READY);
    }
    if (g_dluma_native.level && g_dluma_native.map[0]) dluma_native_reset_index();
    return 1;
}

int dluma_native_gameplay_start_with_backend(
    const DlumaNativeGameplayBackend* backend)
{
    if (!backend || backend->struct_size < sizeof(*backend)) return 0;
    if (g_dluma_native.started) {
        dluma_native_gameplay_stop();
        if (g_dluma_native.started || g_dluma_native.hook_installed) return 0;
    }
    memset(&g_dluma_native, 0, sizeof(g_dluma_native));
    g_dluma_native.backend = *backend;
    g_dluma_native.static_readiness = backend->readiness;
    g_dluma_native.next_handle_id = 1;
    g_dluma_native.started = 1;
    dluma_native_set_supported_state(DLUMA_NATIVE_CAP_STATIC_VALIDATED);
    dluma_native_set_supported_state(DLUMA_NATIVE_CAP_WAITING_FOR_MAP);
    return 1;
}

uint32_t dluma_native_gameplay_readiness(void)
{
    return g_dluma_native.started &&
           !dluma_atomic_compare_exchange(&g_dluma_native.stopping, 0, 0)
        ? (uint32_t)dluma_atomic_compare_exchange(
            (volatile LONG*)&g_dluma_native.readiness, 0, 0) : 0;
}

uint32_t dluma_native_gameplay_supported_capabilities(void)
{
    uint32_t readiness = g_dluma_native.started ? g_dluma_native.static_readiness : 0;
    uint32_t capabilities = 0;
    if (readiness & DLUMA_NATIVE_READY_WORLD_FIND)
        capabilities |= DLUMA_LUA54_CAP_WORLD_FIND;
    if (readiness & DLUMA_NATIVE_READY_LOCAL_ENABLE)
        capabilities |= DLUMA_LUA54_CAP_WORLD_LOCAL_ENABLE;
    if (readiness & DLUMA_NATIVE_READY_SENSOR)
        capabilities |= DLUMA_LUA54_CAP_EVENTS_SENSOR;
    if (readiness & DLUMA_NATIVE_READY_INTERACT)
        capabilities |= DLUMA_LUA54_CAP_EVENTS_INTERACT;
    if (readiness & DLUMA_NATIVE_READY_SUBTITLES)
        capabilities |= DLUMA_LUA54_CAP_UI_SUBTITLES;
    if (readiness & DLUMA_NATIVE_READY_RADIO_DIALOG)
        capabilities |= DLUMA_LUA54_CAP_AUDIO_RADIO_DIALOG;
    if (readiness & DLUMA_NATIVE_READY_ACTION_CHAIN)
        capabilities |= DLUMA_LUA54_CAP_ACTION_CHAIN;
    if (readiness & DLUMA_NATIVE_READY_QUEST_MUTATION)
        capabilities |= DLUMA_LUA54_CAP_QUEST_NATIVE_CONTROL;
    if (readiness & DLUMA_NATIVE_READY_WORLD_POSITION)
        capabilities |= DLUMA_LUA54_CAP_WORLD_POSITION;
    if (readiness & DLUMA_NATIVE_READY_QUEST_HUD)
        capabilities |= DLUMA_LUA54_CAP_UI_QUEST_HUD;
    return capabilities;
}

DlumaNativeCapabilityState dluma_native_gameplay_capability_state(
    uint32_t readiness_bit)
{
    int index = dluma_native_capability_index(readiness_bit);
    if (index < 0 || !g_dluma_native.started)
        return DLUMA_NATIVE_CAP_STATIC_UNSUPPORTED;
    return g_dluma_native.capability_states[index];
}

DlumaNativeIndexPhase dluma_native_gameplay_index_phase(void)
{
    return g_dluma_native.index_phase;
}

uint64_t dluma_native_gameplay_index_epoch(void)
{
    uint64_t epoch;
    dluma_native_lock(&g_dluma_native.match_lock);
    epoch = g_dluma_native.index_epoch;
    dluma_native_unlock(&g_dluma_native.match_lock);
    return epoch;
}

int dluma_native_gameplay_event_subscription_current(
    uint32_t kind,
    uint32_t map_generation,
    uint64_t index_epoch,
    uint64_t key_revision,
    const char* exact_name)
{
    if (!exact_name || !key_revision ||
        map_generation != g_dluma_native.generation ||
        index_epoch != g_dluma_native.index_epoch) return 0;
    return dluma_native_has_subscription(
        exact_name, kind, map_generation, index_epoch, key_revision);
}

uint32_t dluma_native_gameplay_snapshot_event_owners(
    uint32_t kind,
    uint32_t map_generation,
    uint64_t index_epoch,
    uint64_t key_revision,
    const char* exact_name,
    uint64_t* owner_ids,
    uint32_t owner_capacity)
{
    uint32_t i;
    uint32_t count = 0;
    if (!exact_name || !owner_ids || !owner_capacity || !key_revision)
        return 0;
    dluma_native_lock(&g_dluma_native.match_lock);
    if (map_generation != g_dluma_native.generation ||
        index_epoch != g_dluma_native.index_epoch) {
        dluma_native_unlock(&g_dluma_native.match_lock);
        return 0;
    }
    for (i = 0; i < DLUMA_NATIVE_MAX_SUBSCRIPTIONS; ++i) {
        const DlumaNativeSubscription* subscription =
            &g_dluma_native.subscriptions[i];
        uint32_t j;
        if (!subscription->active || subscription->kind != kind ||
            subscription->generation != map_generation ||
            subscription->index_epoch != index_epoch ||
            subscription->key_revision != key_revision ||
            subscription->resolution_state != DLUMA_NATIVE_SUB_RESOLVED ||
            !dluma_native_streq(subscription->name, exact_name))
            continue;
        for (j = 0; j < count; ++j) {
            if (owner_ids[j] == subscription->owner_id) break;
        }
        if (j == count && count < owner_capacity)
            owner_ids[count++] = subscription->owner_id;
    }
    dluma_native_unlock(&g_dluma_native.match_lock);
    return count;
}

uint32_t dluma_native_gameplay_snapshot_event_targets(
    uint32_t kind,
    uint32_t map_generation,
    uint64_t index_epoch,
    uint64_t key_revision,
    const char* exact_name,
    DlumaNativeEventTarget* targets,
    uint32_t target_capacity)
{
    uint32_t i;
    uint32_t count = 0;
    if (!exact_name || !targets || !target_capacity || !key_revision)
        return 0;
    dluma_native_lock(&g_dluma_native.match_lock);
    if (map_generation != g_dluma_native.generation ||
        index_epoch != g_dluma_native.index_epoch) {
        dluma_native_unlock(&g_dluma_native.match_lock);
        return 0;
    }
    for (i = 0; i < DLUMA_NATIVE_MAX_SUBSCRIPTIONS; ++i) {
        const DlumaNativeSubscription* subscription =
            &g_dluma_native.subscriptions[i];
        uint32_t scan;
        if (!subscription->active || subscription->kind != kind ||
            subscription->generation != map_generation ||
            subscription->index_epoch != index_epoch ||
            subscription->key_revision != key_revision ||
            subscription->resolution_state != DLUMA_NATIVE_SUB_RESOLVED ||
            !dluma_native_streq(subscription->name, exact_name))
            continue;
        for (scan = 0; scan < count; ++scan) {
            if (targets[scan].owner_id == subscription->owner_id &&
                targets[scan].callback_id ==
                    subscription->callback_id)
                break;
        }
        if (scan == count && count < target_capacity) {
            targets[count].owner_id = subscription->owner_id;
            targets[count].callback_id =
                subscription->callback_id;
            targets[count].registration_token =
                subscription->token;
            ++count;
        }
    }
    dluma_native_unlock(&g_dluma_native.match_lock);
    return count;
}

int dluma_native_gameplay_index_diagnostics(
    DlumaNativeIndexDiagnostics* diagnostics)
{
    if (!diagnostics || diagnostics->struct_size < sizeof(*diagnostics)) return 0;
    dluma_native_lock(&g_dluma_native.match_lock);
    *diagnostics = g_dluma_native.diagnostics;
    dluma_native_unlock(&g_dluma_native.match_lock);
    return 1;
}

uint32_t dluma_native_gameplay_capabilities(void)
{
    uint32_t readiness = dluma_native_gameplay_readiness();
    uint32_t capabilities = 0;
    if (readiness & DLUMA_NATIVE_READY_WORLD_FIND)
        capabilities |= DLUMA_LUA54_CAP_WORLD_FIND;
    if (readiness & DLUMA_NATIVE_READY_LOCAL_ENABLE)
        capabilities |= DLUMA_LUA54_CAP_WORLD_LOCAL_ENABLE;
    if (readiness & DLUMA_NATIVE_READY_SENSOR)
        capabilities |= DLUMA_LUA54_CAP_EVENTS_SENSOR;
    if (readiness & DLUMA_NATIVE_READY_INTERACT)
        capabilities |= DLUMA_LUA54_CAP_EVENTS_INTERACT;
    if (readiness & DLUMA_NATIVE_READY_SUBTITLES)
        capabilities |= DLUMA_LUA54_CAP_UI_SUBTITLES;
    if (readiness & DLUMA_NATIVE_READY_RADIO_DIALOG)
        capabilities |= DLUMA_LUA54_CAP_AUDIO_RADIO_DIALOG;
    if (readiness & DLUMA_NATIVE_READY_ACTION_CHAIN)
        capabilities |= DLUMA_LUA54_CAP_ACTION_CHAIN;
    if (readiness & DLUMA_NATIVE_READY_QUEST_MUTATION)
        capabilities |= DLUMA_LUA54_CAP_QUEST_NATIVE_CONTROL;
    if (readiness & DLUMA_NATIVE_READY_WORLD_POSITION)
        capabilities |= DLUMA_LUA54_CAP_WORLD_POSITION;
    if (readiness & DLUMA_NATIVE_READY_QUEST_HUD)
        capabilities |= DLUMA_LUA54_CAP_UI_QUEST_HUD;
    return capabilities;
}

void dluma_native_gameplay_on_map_generation_changed(
    uint32_t map_generation,
    const char* normalized_map,
    void* active_level)
{
    g_dluma_native.subtitle_active = 0;
    g_dluma_native.subtitle_force = 0;
    g_dluma_native.subtitle_level_compatible_vtable_logged = 0;
    g_dluma_native.subtitle_movie_compatible_vtable_logged = 0;
    g_dluma_native.subtitle_validation_failure_logged = 0;
    g_dluma_native.subtitle_expires_ms = 0;
    dluma_quest_hud_on_map_generation_changed(map_generation);
    dluma_native_lock(&g_dluma_native.match_lock);
    g_dluma_native.generation = map_generation;
    g_dluma_native.level = active_level;
    dluma_str_copy(g_dluma_native.map, DLUMA_ARRAY_COUNT(g_dluma_native.map),
                 normalized_map ? normalized_map : "");
    dluma_native_unlock(&g_dluma_native.match_lock);
    g_dluma_native.last_sensor_poll_ms = 0;
    g_dluma_native.recovery_attempts = 0;
    g_dluma_native.next_recovery_ms = 0;
    dluma_native_reset_index();
}

static void dluma_native_maybe_recover_index(void)
{
    uint64_t now = dluma_native_now();
    int requested = dluma_atomic_compare_exchange(
        &g_dluma_native.index_refresh_requested, 0, 0) != 0;
    if (g_dluma_native.recovery_attempts >=
            DLUMA_NATIVE_MAX_RECOVERY_ATTEMPTS ||
        now < g_dluma_native.next_recovery_ms) return;
    if (g_dluma_native.index_phase != DLUMA_NATIVE_INDEX_DEGRADED &&
        !(g_dluma_native.index_phase == DLUMA_NATIVE_INDEX_COMPLETE &&
          requested)) return;
    ++g_dluma_native.recovery_attempts;
    dluma_log_u64("INFO", "world_index_recovery_attempt",
                g_dluma_native.recovery_attempts);
    dluma_native_reset_index();
}

void dluma_native_gameplay_game_thread_tick(void)
{
    char observed_map[DLUMA_MAX_MAP_NAME];
    void* level;
    if (!g_dluma_native.started) (void)dluma_native_gameplay_start();
    if (!g_dluma_native.started ||
        dluma_atomic_compare_exchange(&g_dluma_native.stopping, 0, 0)) return;
    if (!g_dluma_native.map[0] || !g_dluma_native.backend.active_level) return;
    observed_map[0] = 0;
    level = g_dluma_native.backend.active_level(
        g_dluma_native.backend.user, observed_map, DLUMA_ARRAY_COUNT(observed_map));
    /* IGame::GetActiveLevel transiently returns null for long stretches while
     * the confirmed Workshop map, indexed objects, and Lua states remain live.
     * Direct Lua queries already use that retained context.  Keep the native
     * edge/event pump on the same lifecycle contract so a null-only snapshot
     * cannot strand successful interaction events or suppress sensor edges.
     * A positive observation of another map still fails closed until the
     * lifecycle owner commits the next generation. */
    if (level && dluma_stricmp(observed_map, g_dluma_native.map) != 0) return;
    if (!level && !g_dluma_native.level) return;
    if (level && level != g_dluma_native.level) {
        dluma_native_lock(&g_dluma_native.match_lock);
        g_dluma_native.level = level;
        dluma_native_unlock(&g_dluma_native.match_lock);
        g_dluma_native.recovery_attempts = 0;
        g_dluma_native.next_recovery_ms = 0;
        dluma_native_reset_index();
    }
    dluma_native_maybe_recover_index();
    dluma_native_tick_index();
    dluma_native_refresh_interaction_capability();
    dluma_native_drain_hook_events();
    dluma_native_tick_subscription_resolution();
    dluma_native_tick_sensors();
    if (g_dluma_native.subtitle_active &&
        dluma_native_now() >= g_dluma_native.subtitle_expires_ms) {
        if (g_dluma_native.backend.subtitle_hide)
            (void)g_dluma_native.backend.subtitle_hide(
                g_dluma_native.backend.user, g_dluma_native.level,
                DLUMA_NATIVE_SUBTITLE_ID);
        g_dluma_native.subtitle_active = 0;
        g_dluma_native.subtitle_force = 0;
        g_dluma_native.subtitle_expires_ms = 0;
    }
}

static int dluma_native_require_capability(
    uint32_t readiness_bit,
    const char* capability,
    const char* permission,
    const char* object_name,
    DlumaLua54Error* error)
{
    if (!(g_dluma_native.static_readiness & readiness_bit)) {
        dluma_native_error(error, "unsupported_capability",
            "the exact executable binding for this capability is unavailable",
            capability, permission, object_name, 0);
        return 0;
    }
    if (readiness_bit != DLUMA_NATIVE_READY_WORLD_FIND &&
        (g_dluma_native.static_readiness & DLUMA_NATIVE_READY_WORLD_FIND) &&
        !(dluma_native_gameplay_readiness() & DLUMA_NATIVE_READY_WORLD_FIND)) {
        dluma_native_error(error, "world_index_not_ready",
            "the active map object index and live RTTI self-test are not ready",
            capability, permission, object_name, 1);
        return 0;
    }
    if (!(dluma_native_gameplay_readiness() & readiness_bit)) {
        dluma_native_error(error,
            readiness_bit == DLUMA_NATIVE_READY_WORLD_FIND
                ? "world_index_not_ready" : "capability_not_ready",
            "the active-map live self-test for this capability is not ready",
            capability, permission, object_name, 1);
        return 0;
    }
    return 1;
}

int dluma_native_gameplay_subscription_add(
    uint64_t owner_id,
    uint32_t map_generation,
    uint32_t token,
    uint32_t kind,
    const char* exact_name,
    DlumaLua54Error* error)
{
    return dluma_native_gameplay_subscription_add_ex(
        owner_id,
        map_generation,
        (uint64_t)token,
        (uint64_t)token,
        kind,
        exact_name,
        error);
}

int dluma_native_gameplay_subscription_add_ex(
    uint64_t owner_id,
    uint32_t map_generation,
    uint64_t registration_token,
    uint64_t callback_id,
    uint32_t kind,
    const char* exact_name,
    DlumaLua54Error* error)
{
    uint32_t slot = DLUMA_NATIVE_MAX_SUBSCRIPTIONS;
    uint32_t i;
    uint32_t required;
    if (kind != DLUMA_LUA54_EVENT_SENSOR_ENTER &&
        kind != DLUMA_LUA54_EVENT_SENSOR_LEAVE &&
        kind != DLUMA_LUA54_EVENT_INTERACT_BEGIN)
        return 1;
    required = (kind == DLUMA_LUA54_EVENT_INTERACT_BEGIN)
        ? DLUMA_NATIVE_READY_INTERACT : DLUMA_NATIVE_READY_SENSOR;
    if (!(g_dluma_native.static_readiness & required)) {
        dluma_native_error(error, "unsupported_capability",
            "the exact executable binding for this subscription is unavailable",
            kind == DLUMA_LUA54_EVENT_INTERACT_BEGIN ? "events.interact" : "events.sensor",
            kind == DLUMA_LUA54_EVENT_INTERACT_BEGIN ? "events.interact" : "events.sensor",
            exact_name, 0);
        return 0;
    }
    if (!owner_id || !registration_token || !callback_id ||
        !dluma_native_exact_name_valid(exact_name)) {
        dluma_native_error(error, "invalid_subscription", "subscription owner, token, or exact name is invalid",
                         "lua.runtime", "", exact_name, 0);
        return 0;
    }
    dluma_native_lock(&g_dluma_native.match_lock);
    for (i = 0; i < DLUMA_NATIVE_MAX_SUBSCRIPTIONS; ++i) {
        if (g_dluma_native.subscriptions[i].active &&
            g_dluma_native.subscriptions[i].owner_id == owner_id &&
            g_dluma_native.subscriptions[i].token ==
                registration_token) {
            dluma_native_unlock(&g_dluma_native.match_lock);
            dluma_native_error(error, "duplicate_subscription_token",
                "subscription token is already registered", "lua.runtime", "",
                exact_name, 0);
            return 0;
        }
        if (slot == DLUMA_NATIVE_MAX_SUBSCRIPTIONS &&
            !g_dluma_native.subscriptions[i].active) slot = i;
    }
    if (slot == DLUMA_NATIVE_MAX_SUBSCRIPTIONS) {
        dluma_native_unlock(&g_dluma_native.match_lock);
        dluma_native_error(error, "native_subscription_limit", "native subscription limit reached",
                         "lua.runtime", "", exact_name, 0);
        return 0;
    }
    memset(&g_dluma_native.subscriptions[slot], 0,
           sizeof(g_dluma_native.subscriptions[slot]));
    g_dluma_native.subscriptions[slot].owner_id = owner_id;
    g_dluma_native.subscriptions[slot].registration_id =
        ++g_dluma_native.next_subscription_id;
    if (!g_dluma_native.subscriptions[slot].registration_id)
        g_dluma_native.subscriptions[slot].registration_id =
            ++g_dluma_native.next_subscription_id;
    g_dluma_native.subscriptions[slot].index_epoch =
        g_dluma_native.index_epoch;
    g_dluma_native.subscriptions[slot].generation = map_generation;
    g_dluma_native.subscriptions[slot].token =
        registration_token;
    g_dluma_native.subscriptions[slot].callback_id =
        callback_id;
    g_dluma_native.subscriptions[slot].kind = kind;
    g_dluma_native.subscriptions[slot].active = 1;
    g_dluma_native.subscriptions[slot].resolution_state = DLUMA_NATIVE_SUB_PENDING;
    dluma_str_copy(g_dluma_native.subscriptions[slot].name,
                 DLUMA_ARRAY_COUNT(g_dluma_native.subscriptions[slot].name), exact_name);
    ++g_dluma_native.subscription_count;
    (void)dluma_native_bump_subscription_key_revision_locked(kind, exact_name);
    dluma_native_unlock(&g_dluma_native.match_lock);
    dluma_log_pair("INFO", "subscription_registered_pending_resolution", exact_name);
    if (g_dluma_native.index_phase == DLUMA_NATIVE_INDEX_COMPLETE &&
        map_generation == g_dluma_native.generation)
        (void)dluma_native_resolve_subscription(slot);
    return 1;
}

int dluma_native_gameplay_subscription_remove(
    uint64_t owner_id,
    uint32_t token,
    uint32_t kind,
    const char* exact_name,
    DlumaLua54Error* error)
{
    (void)kind;
    (void)exact_name;
    return dluma_native_gameplay_subscription_remove_ex(
        owner_id, (uint64_t)token, error);
}

int dluma_native_gameplay_subscription_remove_ex(
    uint64_t owner_id,
    uint64_t registration_token,
    DlumaLua54Error* error)
{
    uint32_t i;
    dluma_native_lock(&g_dluma_native.match_lock);
    for (i = 0; i < DLUMA_NATIVE_MAX_SUBSCRIPTIONS; ++i) {
        DlumaNativeSubscription* subscription = &g_dluma_native.subscriptions[i];
        if (!subscription->active || subscription->owner_id != owner_id ||
            subscription->token != registration_token) continue;
        {
            uint32_t removed_kind = subscription->kind;
            char removed_name[DLUMA_LUA54_MAX_OBJECT_NAME];
            dluma_str_copy(removed_name, DLUMA_ARRAY_COUNT(removed_name),
                         subscription->name);
            memset(subscription, 0, sizeof(*subscription));
            if (g_dluma_native.subscription_count)
                --g_dluma_native.subscription_count;
            (void)dluma_native_bump_subscription_key_revision_locked(
                removed_kind, removed_name);
        }
        dluma_native_unlock(&g_dluma_native.match_lock);
        return 1;
    }
    dluma_native_unlock(&g_dluma_native.match_lock);
    dluma_native_error(error, "unknown_subscription", "subscription token is already absent or unknown",
                     "dluma.events", "", "", 0);
    return 0;
}

int dluma_native_gameplay_world_find(
    uint64_t owner_id,
    uint32_t map_generation,
    const char* exact_name,
    const char* expected_class,
    DlumaLua54Object* object,
    DlumaLua54Error* error)
{
    uint32_t index;
    uint32_t i;
    uint32_t handle_slot = DLUMA_NATIVE_MAX_HANDLES;
    uint32_t expired_slot = DLUMA_NATIVE_MAX_HANDLES;
    DlumaNativeIndexEntry entry;
    DlumaNativeHandle handle;
    if (!dluma_native_require_capability(DLUMA_NATIVE_READY_WORLD_FIND,
            "world.find", "world.query", exact_name, error)) return 0;
    if (!owner_id || !object) {
        dluma_native_error(error, "invalid_world_find", "owner and output object are required",
                         "world.find", "world.query", exact_name, 0);
        return 0;
    }
    if (map_generation != g_dluma_native.generation) {
        dluma_native_error(error, "stale_map_generation", "world.find map generation is stale",
                         "world.find", "world.query", exact_name, 1);
        return 0;
    }
    memset(&entry, 0, sizeof(entry));
    if (!dluma_native_find_index(exact_name, &index, &entry, error)) return 0;
    if (expected_class && expected_class[0] &&
        (!dluma_native_exact_name_valid(expected_class) ||
         !g_dluma_native.backend.object_is_a ||
         !g_dluma_native.backend.object_is_a(
             g_dluma_native.backend.user, entry.object,
             expected_class))) {
        dluma_native_error(error, "wrong_object_class", "object does not derive from the requested class",
                         "world.find", "world.query", exact_name, 0);
        return 0;
    }
    memset(&handle, 0, sizeof(handle));
    dluma_native_lock(&g_dluma_native.match_lock);
    if (g_dluma_native.generation != map_generation ||
        g_dluma_native.index_epoch != entry.index_epoch ||
        g_dluma_native.index_phase != DLUMA_NATIVE_INDEX_COMPLETE) {
        dluma_native_unlock(&g_dluma_native.match_lock);
        dluma_native_error(error, "stale_map_generation",
            "the map or published world index changed before handle creation",
            "world.find", "world.query", exact_name, 1);
        return 0;
    }
    for (i = 0; i < DLUMA_NATIVE_MAX_HANDLES; ++i) {
        if (g_dluma_native.handles[i].active &&
            g_dluma_native.handles[i].owner_id == owner_id &&
            g_dluma_native.handles[i].generation == map_generation &&
            g_dluma_native.handles[i].entry.complete == entry.complete) {
            handle_slot = i;
            break;
        }
        if (handle_slot == DLUMA_NATIVE_MAX_HANDLES &&
            !g_dluma_native.handles[i].active &&
            !g_dluma_native.handles[i].expired) handle_slot = i;
        if (g_dluma_native.handles[i].expired &&
            expired_slot == DLUMA_NATIVE_MAX_HANDLES) expired_slot = i;
    }
    if (handle_slot == DLUMA_NATIVE_MAX_HANDLES) handle_slot = expired_slot;
    if (handle_slot == DLUMA_NATIVE_MAX_HANDLES) {
        dluma_native_unlock(&g_dluma_native.match_lock);
        dluma_native_error(error, "object_handle_limit", "object handle limit reached",
                         "world.find", "world.query", exact_name, 0);
        return 0;
    }
    if (!g_dluma_native.handles[handle_slot].active) {
        DlumaNativeHandle* created = &g_dluma_native.handles[handle_slot];
        memset(created, 0, sizeof(*created));
        created->handle_id = g_dluma_native.next_handle_id++;
        if (!created->handle_id) created->handle_id = g_dluma_native.next_handle_id++;
        created->owner_id = owner_id;
        created->generation = map_generation;
        created->entry = entry;
        created->active = 1;
    }
    handle = g_dluma_native.handles[handle_slot];
    dluma_native_unlock(&g_dluma_native.match_lock);
    dluma_native_fill_object(owner_id, handle.handle_id, &handle.entry, object);
    return 1;
}

int dluma_native_gameplay_world_get_info(
    uint64_t owner_id,
    uint64_t handle_id,
    uint32_t map_generation,
    DlumaLua54Object* object,
    DlumaLua54Error* error)
{
    DlumaNativeHandle handle;
    if (!dluma_native_find_handle(
            owner_id, handle_id, map_generation, &handle, error) || !object) return 0;
    dluma_native_fill_object(owner_id, handle_id, &handle.entry, object);
    return 1;
}

int dluma_native_gameplay_world_get_enabled(
    uint64_t owner_id,
    uint64_t handle_id,
    uint32_t map_generation,
    int* enabled,
    DlumaLua54Error* error)
{
    DlumaNativeHandle handle;
    DlumaNativeIndexEntry* entry = &handle.entry;
    void* interface_pointer;
    if (!dluma_native_require_capability(DLUMA_NATIVE_READY_LOCAL_ENABLE,
            "world.local_enable", "world.query", "", error)) return 0;
    if (!dluma_native_find_handle(
            owner_id, handle_id, map_generation, &handle, error) || !enabled) return 0;
    interface_pointer = g_dluma_native.backend.local_enable_interface
        ? g_dluma_native.backend.local_enable_interface(
            g_dluma_native.backend.user, entry->object) : 0;
    if (!interface_pointer || !g_dluma_native.backend.local_enable_get ||
        !g_dluma_native.backend.local_enable_get(
            g_dluma_native.backend.user, interface_pointer, enabled)) {
        dluma_native_error(error, "local_enable_unavailable", "object has no validated ILocalEnable interface",
                         "world.local_enable", "world.query", entry->name, 0);
        return 0;
    }
    return 1;
}

int dluma_native_gameplay_world_get_position(
    uint64_t owner_id,
    uint64_t handle_id,
    uint32_t map_generation,
    DlumaLua54Vec3* position,
    DlumaLua54Error* error)
{
    DlumaNativeHandle handle;
    DlumaNativeIndexEntry* entry = &handle.entry;
    void* interface_pointer;
    if (!position) {
        dluma_native_error(error, "invalid_position_output",
            "world.position requires a copied output record",
            "world.position", "world.query", "", 0);
        return 0;
    }
    memset(position, 0, sizeof(*position));
    position->struct_size = sizeof(*position);
    if (!dluma_native_find_handle(
            owner_id, handle_id, map_generation, &handle, error)) return 0;
    if (!dluma_native_require_capability(DLUMA_NATIVE_READY_WORLD_POSITION,
            "world.position", "world.query", entry->name, error)) return 0;
    interface_pointer = g_dluma_native.backend.control_object_interface
        ? g_dluma_native.backend.control_object_interface(
            g_dluma_native.backend.user, entry->object) : 0;
    if (!interface_pointer || !g_dluma_native.backend.world_get_position ||
        !g_dluma_native.backend.world_get_position(
            g_dluma_native.backend.user, interface_pointer, position) ||
        position->struct_size < sizeof(*position) ||
        !dluma_native_position_is_sane(position->x) ||
        !dluma_native_position_is_sane(position->y) ||
        !dluma_native_position_is_sane(position->z)) {
        dluma_native_error(error, "world_position_unavailable",
            "object has no validated readable IControlObject position",
            "world.position", "world.query", entry->name, 1);
        return 0;
    }
    return 1;
}

int dluma_native_gameplay_world_get_forward(
    uint64_t owner_id,
    uint64_t handle_id,
    uint32_t map_generation,
    DlumaLua54Vec3* forward,
    DlumaLua54Error* error)
{
    DlumaNativeHandle handle;
    DlumaNativeIndexEntry* entry = &handle.entry;
    void* interface_pointer;
    if (!forward) {
        dluma_native_error(error, "invalid_forward_output",
            "world.forward requires a copied output record",
            "world.position", "world.query", "", 0);
        return 0;
    }
    memset(forward, 0, sizeof(*forward));
    forward->struct_size = sizeof(*forward);
    if (!dluma_native_find_handle(
            owner_id, handle_id, map_generation, &handle, error)) return 0;
    if (!dluma_native_require_capability(DLUMA_NATIVE_READY_WORLD_POSITION,
            "world.position", "world.query", entry->name, error)) return 0;
    interface_pointer = g_dluma_native.backend.control_object_interface
        ? g_dluma_native.backend.control_object_interface(
            g_dluma_native.backend.user, entry->object) : 0;
    if (!interface_pointer || !g_dluma_native.backend.world_get_forward ||
        !g_dluma_native.backend.world_get_forward(
            g_dluma_native.backend.user, interface_pointer, forward) ||
        forward->struct_size < sizeof(*forward) ||
        !dluma_native_position_is_sane(forward->x) ||
        !dluma_native_position_is_sane(forward->y) ||
        !dluma_native_position_is_sane(forward->z)) {
        dluma_native_error(error, "world_forward_unavailable",
            "object has no validated readable IControlObject forward vector",
            "world.position", "world.query", entry->name, 1);
        return 0;
    }
    return 1;
}

int dluma_native_gameplay_world_set_enabled(
    uint64_t owner_id,
    const DlumaLua54SetEnabledRequest* request,
    DlumaLua54SetEnabledResult* result,
    DlumaLua54Error* error)
{
    typedef struct DlumaNativePreflight {
        DlumaNativeIndexEntry entry;
        void* interface_pointer;
        int before;
    } DlumaNativePreflight;
    DlumaNativePreflight preflight[DLUMA_LUA54_MAX_BULK_OBJECTS];
    uint32_t i;
    uint32_t j;
    if (!request || request->struct_size < sizeof(*request) || !result ||
        request->owner_id != owner_id || !owner_id ||
        request->map_generation != g_dluma_native.generation ||
        request->requested_count == 0 ||
        request->requested_count > DLUMA_LUA54_MAX_BULK_OBJECTS ||
        !request->atomic || request->best_effort) {
        dluma_native_error(error, "invalid_atomic_request", "set_enabled_many requires a bounded atomic request with best_effort=false",
                         "world.local_enable", "world.write", "", 0);
        return 0;
    }
    memset(result, 0, sizeof(*result));
    result->struct_size = sizeof(*result);
    result->requested_count = request->requested_count;
    result->name_count = 0;
    memset(preflight, 0, sizeof(preflight));
    if (!dluma_native_require_capability(DLUMA_NATIVE_READY_LOCAL_ENABLE,
            "world.local_enable", "world.write", "", error)) return 0;
    /* Validate all names and duplicates before the first engine/native call. */
    for (i = 0; i < request->requested_count; ++i) {
        if (!dluma_native_exact_name_valid(request->names[i])) {
            dluma_native_error(error, "invalid_object_name", "atomic request contains an invalid exact name",
                             "world.local_enable", "world.write", request->names[i], 0);
            return 0;
        }
        for (j = 0; j < i; ++j) {
            if (dluma_native_streq(request->names[j], request->names[i])) {
                dluma_native_error(error, "duplicate_request_name", "atomic request contains a duplicate exact name",
                                 "world.local_enable", "world.write", request->names[i], 0);
                return 0;
            }
        }
    }
    for (i = 0; i < request->requested_count; ++i) {
        uint32_t index;
        if (!dluma_native_find_index(
                request->names[i], &index, &preflight[i].entry, error)) return 0;
        preflight[i].interface_pointer = g_dluma_native.backend.local_enable_interface
            ? g_dluma_native.backend.local_enable_interface(
                g_dluma_native.backend.user, preflight[i].entry.object) : 0;
        if (!preflight[i].interface_pointer ||
            !g_dluma_native.backend.local_enable_get ||
            !g_dluma_native.backend.local_enable_get(
                g_dluma_native.backend.user, preflight[i].interface_pointer,
                &preflight[i].before)) {
            dluma_native_error(error, "local_enable_unavailable", "atomic preflight found no validated ILocalEnable interface",
                             "world.local_enable", "world.write", request->names[i], 0);
            return 0;
        }
    }
    dluma_log_u64("INFO", "local_enable_preflight_ok", request->requested_count);
    for (i = 0; i < request->requested_count; ++i) {
        void* current_interface;
        int current_before;
        int after;
        if (request->map_generation != g_dluma_native.generation ||
            !dluma_native_revalidate_entry(&preflight[i].entry)) {
            result->partial = result->changed_count ? 1u : 0u;
            dluma_native_error(error, "stale_object",
                "active level or object generation changed after atomic preflight",
                "world.local_enable", "world.write", request->names[i], 1);
            return 0;
        }
        current_interface = g_dluma_native.backend.local_enable_interface
            ? g_dluma_native.backend.local_enable_interface(
                g_dluma_native.backend.user, preflight[i].entry.object) : 0;
        if (!current_interface ||
            current_interface != preflight[i].interface_pointer ||
            !g_dluma_native.backend.local_enable_get ||
            !g_dluma_native.backend.local_enable_get(
                g_dluma_native.backend.user, current_interface,
                &current_before) || current_before != preflight[i].before) {
            result->partial = result->changed_count ? 1u : 0u;
            dluma_native_error(error, "local_enable_commit_failed",
                "ILocalEnable interface or state changed after atomic preflight; no rollback is claimed",
                "world.local_enable", "world.write", request->names[i], 0);
            return 0;
        }
        if (preflight[i].before == (request->enabled ? 1 : 0)) {
            ++result->unchanged_count;
            dluma_str_copy(result->names[result->name_count],
                         DLUMA_ARRAY_COUNT(result->names[result->name_count]),
                         request->names[i]);
            ++result->name_count;
            continue;
        }
        if (!g_dluma_native.backend.local_enable_set ||
            !g_dluma_native.backend.local_enable_set(
                g_dluma_native.backend.user, preflight[i].interface_pointer,
                request->enabled ? 1 : 0)) {
            result->partial = result->changed_count ? 1u : 0u;
            dluma_native_error(error, "local_enable_commit_failed", "post-preflight ILocalEnable commit failed; no rollback is claimed",
                             "world.local_enable", "world.write", request->names[i], 0);
            return 0;
        }
        if (!g_dluma_native.backend.local_enable_get(
                g_dluma_native.backend.user, preflight[i].interface_pointer,
                &after) || after != (request->enabled ? 1 : 0)) {
            /* The setter reported success, so a failed/incorrect readback is
             * an explicitly partial mutation even for the first name. */
            result->partial = 1u;
            dluma_native_error(error, "local_enable_commit_failed", "post-preflight ILocalEnable commit failed; no rollback is claimed",
                             "world.local_enable", "world.write", request->names[i], 0);
            return 0;
        }
        ++result->changed_count;
        dluma_str_copy(result->names[result->name_count],
                     DLUMA_ARRAY_COUNT(result->names[result->name_count]),
                     request->names[i]);
        ++result->name_count;
    }
    dluma_log_u64("INFO", "local_enable_commit_ok", result->changed_count);
    return 1;
}

int dluma_native_gameplay_sensor_inside(
    uint64_t owner_id,
    uint32_t map_generation,
    const char* exact_name,
    int* inside,
    DlumaLua54Error* error)
{
    uint32_t index;
    DlumaNativeIndexEntry entry;
    void* sensor_interface;
    (void)owner_id;
    if (!dluma_native_require_capability(DLUMA_NATIVE_READY_SENSOR,
            "events.sensor", "events.sensor", exact_name, error)) return 0;
    if (map_generation != g_dluma_native.generation || !inside) {
        dluma_native_error(error, "stale_map_generation",
            "sensor query map generation is stale or output is invalid",
            "events.sensor", "events.sensor", exact_name, 1);
        return 0;
    }
    if (!dluma_native_find_index(exact_name, &index, &entry, error)) return 0;
    if (!g_dluma_native.backend.object_is_a ||
        !g_dluma_native.backend.object_is_a(
            g_dluma_native.backend.user, entry.object, "SensorArea")) {
        dluma_native_error(error, "wrong_object_class", "object does not derive from SensorArea",
                         "events.sensor", "events.sensor", exact_name, 0);
        return 0;
    }
    sensor_interface = g_dluma_native.backend.sensor_area_interface
        ? g_dluma_native.backend.sensor_area_interface(
            g_dluma_native.backend.user, entry.object) : 0;
    if (!sensor_interface) {
        dluma_native_error(error, "sensor_base_unavailable",
            "SensorArea RTTI base-subobject adjustment failed",
            "events.sensor", "events.sensor", exact_name, 0);
        return 0;
    }
    if (!g_dluma_native.backend.sensor_inside ||
        !g_dluma_native.backend.sensor_inside(
            g_dluma_native.backend.user, sensor_interface, inside)) {
        dluma_native_error(error, "sensor_checkinside_unvalidated",
            "SensorArea CheckInside slot or target failed exact validation",
            "events.sensor", "events.sensor", exact_name, 0);
        return 0;
    }
    *inside = *inside ? 1 : 0;
    return 1;
}

static int dluma_native_bounded_request_text(
    const char* text, uint32_t capacity, int allow_newline)
{
    uint32_t i;
    if (!text || !capacity || !text[0]) return 0;
    for (i = 0; i < capacity; ++i) {
        unsigned char value = (unsigned char)text[i];
        if (!value) return i > 0;
        if (value < 0x20 && !(allow_newline && (value == '\n' || value == '\r' || value == '\t')))
            return 0;
    }
    return 0;
}

int dluma_native_gameplay_subtitle_show(
    const DlumaLua54SubtitleRequest* request,
    DlumaLua54Error* error)
{
    uint64_t duration_ms;
    if (!dluma_native_require_capability(
            DLUMA_NATIVE_READY_SUBTITLES, "ui.subtitles", "ui.overlay",
            "", error)) return 0;
    if (!request || request->struct_size < sizeof(*request) ||
        !dluma_native_bounded_request_text(
            request->text, DLUMA_ARRAY_COUNT(request->text), 1) ||
        !(request->duration_seconds >= 0.05) ||
        !(request->duration_seconds <= 600.0) ||
        request->force > 1u) {
        dluma_native_error(error, "invalid_subtitle",
            "subtitle text, duration (0.05..600 seconds), or force flag is invalid",
            "ui.subtitles", "ui.overlay", "", 0);
        return 0;
    }
    if (!g_dluma_native.backend.subtitle_show ||
        !g_dluma_native.backend.subtitle_hide || !g_dluma_native.level) {
        dluma_native_error(error, "subtitle_adapter_unavailable",
            "the validated active-level subtitle adapter is unavailable",
            "ui.subtitles", "ui.overlay", "", 1);
        return 0;
    }
    if (g_dluma_native.subtitle_active)
        (void)g_dluma_native.backend.subtitle_hide(
            g_dluma_native.backend.user, g_dluma_native.level,
            DLUMA_NATIVE_SUBTITLE_ID);
    if (!g_dluma_native.backend.subtitle_show(
            g_dluma_native.backend.user, g_dluma_native.level,
            DLUMA_NATIVE_SUBTITLE_ID, request->text,
            request->force != 0)) {
        g_dluma_native.subtitle_active = 0;
        dluma_native_error(error, "subtitle_show_failed",
            "the exact subtitle slots or active game movie failed validation",
            "ui.subtitles", "ui.overlay", "", 1);
        return 0;
    }
    duration_ms = (uint64_t)(request->duration_seconds * 1000.0 + 0.5);
    g_dluma_native.subtitle_active = 1;
    g_dluma_native.subtitle_force = request->force ? 1 : 0;
    g_dluma_native.subtitle_expires_ms = dluma_native_now() + duration_ms;
    return 1;
}

int dluma_native_gameplay_subtitle_update(
    const DlumaLua54SubtitleUpdateRequest* request,
    DlumaLua54Error* error)
{
    if (!dluma_native_require_capability(
            DLUMA_NATIVE_READY_SUBTITLES, "ui.subtitles", "ui.overlay",
            "", error)) return 0;
    if (!request || request->struct_size < sizeof(*request) ||
        !dluma_native_bounded_request_text(
            request->text, DLUMA_ARRAY_COUNT(request->text), 1) ||
        request->fade > 1u) {
        dluma_native_error(error, "invalid_subtitle_update",
            "subtitle update text or fade flag is invalid",
            "ui.subtitles", "ui.overlay", "", 0);
        return 0;
    }
    if (!g_dluma_native.subtitle_active) {
        dluma_native_error(error, "no_active_subtitle",
            "there is no loader-owned active subtitle to update",
            "ui.subtitles", "ui.overlay", "", 0);
        return 0;
    }
    if (!g_dluma_native.backend.subtitle_show ||
        !g_dluma_native.backend.subtitle_hide || !g_dluma_native.level) {
        dluma_native_error(error, "subtitle_adapter_unavailable",
            "the validated active-level subtitle adapter is unavailable",
            "ui.subtitles", "ui.overlay", "", 1);
        return 0;
    }
    if (request->fade && !g_dluma_native.backend.subtitle_hide(
            g_dluma_native.backend.user, g_dluma_native.level,
            DLUMA_NATIVE_SUBTITLE_ID)) {
        dluma_native_error(error, "subtitle_fade_restart_failed",
            "the active subtitle could not be hidden before its fresh-line transition",
            "ui.subtitles", "ui.overlay", "", 1);
        return 0;
    }
    if (!g_dluma_native.backend.subtitle_show(
            g_dluma_native.backend.user, g_dluma_native.level,
            DLUMA_NATIVE_SUBTITLE_ID, request->text,
            g_dluma_native.subtitle_force != 0)) {
        dluma_native_error(error, "subtitle_update_failed",
            "the active subtitle could not be updated",
            "ui.subtitles", "ui.overlay", "", 1);
        return 0;
    }
    return 1;
}

int dluma_native_gameplay_radio_dialog(
    const DlumaLua54RadioDialogRequest* request,
    DlumaLua54Error* error)
{
    if (!dluma_native_require_capability(
            DLUMA_NATIVE_READY_RADIO_DIALOG, "audio.radio_dialog", "audio.play",
            request ? request->speaker : "", error)) return 0;
    if (!request || request->struct_size < sizeof(*request) ||
        !dluma_native_bounded_request_text(
            request->speaker, DLUMA_ARRAY_COUNT(request->speaker), 0) ||
        !dluma_native_bounded_request_text(
            request->group, DLUMA_ARRAY_COUNT(request->group), 0)) {
        dluma_native_error(error, "invalid_radio_dialog",
            "radio-dialog speaker or group is empty, too long, or contains controls",
            "audio.radio_dialog", "audio.play",
            request ? request->speaker : "", 0);
        return 0;
    }
    if (!g_dluma_native.backend.radio_dialog ||
        !g_dluma_native.backend.radio_dialog(
            g_dluma_native.backend.user, request->speaker, request->group)) {
        dluma_native_error(error, "radio_dialog_failed",
            "the dialog group was not found or the exact speaker binding rejected it",
            "audio.radio_dialog", "audio.play", request->speaker, 1);
        return 0;
    }
    return 1;
}

int dluma_native_gameplay_action_run_chain(
    uint64_t owner_id,
    uint32_t map_generation,
    const char* exact_name,
    DlumaLua54Error* error)
{
    uint32_t index;
    DlumaNativeIndexEntry entry;
    if (!dluma_native_require_capability(
            DLUMA_NATIVE_READY_ACTION_CHAIN, "action.chain", "world.write",
            exact_name, error)) return 0;
    if (!owner_id || map_generation != g_dluma_native.generation ||
        !dluma_native_exact_name_valid(exact_name)) {
        dluma_native_error(error, "invalid_action_chain",
            "ActionChain owner, map generation, or exact name is invalid",
            "action.chain", "world.write", exact_name, 0);
        return 0;
    }
    memset(&entry, 0, sizeof(entry));
    if (!dluma_native_find_index(exact_name, &index, &entry, error)) {
        if (error && error->struct_size >= sizeof(*error)) {
            dluma_str_copy(error->capability,
                DLUMA_ARRAY_COUNT(error->capability), "action.chain");
            dluma_str_copy(error->permission,
                DLUMA_ARRAY_COUNT(error->permission), "world.write");
        }
        return 0;
    }
    if (!dluma_native_streq(entry.class_name, "ActionChain") ||
        !g_dluma_native.backend.object_is_a ||
        !g_dluma_native.backend.object_is_a(
            g_dluma_native.backend.user, entry.object, "ActionChain")) {
        dluma_native_error(error, "wrong_action_chain_class",
            "the exact named object is not an ActionChain",
            "action.chain", "world.write", exact_name, 0);
        return 0;
    }
    dluma_native_lock(&g_dluma_native.match_lock);
    if (map_generation != g_dluma_native.generation ||
        entry.index_epoch != g_dluma_native.index_epoch ||
        g_dluma_native.index_phase != DLUMA_NATIVE_INDEX_COMPLETE) {
        dluma_native_unlock(&g_dluma_native.match_lock);
        dluma_native_error(error, "stale_map_generation",
            "the map or published world index changed before ActionChain execution",
            "action.chain", "world.write", exact_name, 1);
        return 0;
    }
    dluma_native_unlock(&g_dluma_native.match_lock);
    if (!g_dluma_native.backend.action_run_chain ||
        !g_dluma_native.backend.action_run_chain(
            g_dluma_native.backend.user, entry.complete)) {
        dluma_native_error(error, "action_chain_run_failed",
            "the live ActionChain object failed exact vtable or run-target validation",
            "action.chain", "world.write", exact_name, 1);
        return 0;
    }
    return 1;
}

int dluma_native_gameplay_quest_mutate(
    uint64_t owner_id,
    const DlumaLua54QuestRequest* request,
    DlumaLua54QuestResult* result,
    DlumaLua54Error* error)
{
    const char* quest_id = request ? request->native_quest_id : "";
    int needs_phase;
    if (result) {
        memset(result, 0, sizeof(*result));
        result->struct_size = sizeof(*result);
    }
    if (!owner_id || !request || request->struct_size < sizeof(*request) ||
        !result || request->owner_id != owner_id || request->reserved != 0 ||
        !dluma_native_quest_identifier_valid(request->native_quest_id) ||
        !dluma_native_quest_optional_identifier_valid(request->native_phase_id)) {
        dluma_native_error(error, "invalid_quest_request",
            "quest control accepts only bounded exact quest and phase identifiers",
            "quests.native", "quest.write", quest_id, 0);
        return 0;
    }
    if (request->map_generation != g_dluma_native.generation) {
        dluma_native_error(error, "stale_map_generation",
            "quest control request belongs to a previous map generation",
            "quests.native", "quest.write", request->native_quest_id, 1);
        return 0;
    }
    needs_phase = request->operation == DLUMA_LUA54_QUEST_JUMP_TO_PHASE;
    if ((request->operation != DLUMA_LUA54_QUEST_START &&
         request->operation != DLUMA_LUA54_QUEST_COMPLETE_CURRENT_PHASE &&
         request->operation != DLUMA_LUA54_QUEST_JUMP_TO_PHASE &&
         request->operation != DLUMA_LUA54_QUEST_COMPLETE) ||
        (needs_phase && !request->native_phase_id[0])) {
        dluma_native_error(error, "invalid_quest_operation",
            "quest operation is invalid or requires an exact phase identifier",
            "quests.native", "quest.write", request->native_quest_id, 0);
        return 0;
    }
    if (!dluma_native_require_capability(DLUMA_NATIVE_READY_QUEST_MUTATION,
            "quests.native", "quest.write", request->native_quest_id,
            error)) return 0;
    if (!g_dluma_native.production_backend ||
        !g_dluma_native.backend.quest_mutate) {
        dluma_native_error(error, "quest_backend_unavailable",
            "the exact Windows QuestManager backend is unavailable for this build or map",
            "quests.native", "quest.write", request->native_quest_id, 0);
        return 0;
    }
    if (!g_dluma_native.backend.quest_mutate(
            g_dluma_native.backend.user, request, result, error)) {
        if (error && !error->code[0])
            dluma_native_error(error, "quest_transition_failed",
                "the validated native quest transition did not complete",
                "quests.native", "quest.write", request->native_quest_id,
                1);
        return 0;
    }
    if (!result->struct_size) result->struct_size = sizeof(*result);
    return 1;
}

int dluma_native_gameplay_enqueue_interactive_item_event(
    void* complete_item,
    uint32_t action,
    int original_result)
{
    char exact_name[DLUMA_LUA54_MAX_OBJECT_NAME];
    uint64_t index_epoch = 0;
    uint64_t subscription_key_revision = 0;
    uint32_t generation = 0;
    uint32_t i;
    int subscribed = 0;
    uint32_t queue_index;
    if (!complete_item || action != 1u || !original_result ||
        dluma_atomic_compare_exchange(&g_dluma_native.stopping, 0, 0)) return 0;
    exact_name[0] = 0;
    dluma_native_lock(&g_dluma_native.match_lock);
    for (i = 0; i < g_dluma_native.index_count; ++i) {
        if (g_dluma_native.index[i].complete == complete_item &&
            g_dluma_native.index[i].generation == g_dluma_native.generation &&
            g_dluma_native.index[i].index_epoch == g_dluma_native.index_epoch) {
            dluma_str_copy(exact_name, DLUMA_ARRAY_COUNT(exact_name),
                         g_dluma_native.index[i].name);
            generation = g_dluma_native.index[i].generation;
            index_epoch = g_dluma_native.index[i].index_epoch;
            break;
        }
    }
    if (exact_name[0]) {
        for (i = 0; i < DLUMA_NATIVE_MAX_SUBSCRIPTIONS; ++i) {
            DlumaNativeSubscription* subscription = &g_dluma_native.subscriptions[i];
            if (subscription->active &&
                subscription->kind == DLUMA_LUA54_EVENT_INTERACT_BEGIN &&
                subscription->resolution_state == DLUMA_NATIVE_SUB_RESOLVED &&
                subscription->generation == generation &&
                subscription->index_epoch == index_epoch &&
                dluma_native_streq(subscription->name, exact_name)) {
                subscribed = 1;
                subscription_key_revision = subscription->key_revision;
                break;
            }
        }
    }
    dluma_native_unlock(&g_dluma_native.match_lock);
    if (exact_name[0])
        dluma_atomic_increment(&g_dluma_native.interaction.complete_object_matches);
    if (!subscribed) return 0;
    dluma_atomic_increment(&g_dluma_native.interaction.subscription_matches);
    dluma_native_lock(&g_dluma_native.hook_event_lock);
    if (g_dluma_native.hook_event_count >= DLUMA_NATIVE_MAX_HOOK_EVENTS) {
        if (!g_dluma_native.hook_overflow_logged)
            g_dluma_native.hook_overflow_logged = 1;
        dluma_native_unlock(&g_dluma_native.hook_event_lock);
        dluma_atomic_increment(&g_dluma_native.interaction.events_dropped);
        return 0;
    }
    queue_index = (g_dluma_native.hook_event_head + g_dluma_native.hook_event_count) %
                  DLUMA_NATIVE_MAX_HOOK_EVENTS;
    g_dluma_native.hook_events[queue_index].index_epoch = index_epoch;
    g_dluma_native.hook_events[queue_index].subscription_key_revision =
        subscription_key_revision;
    g_dluma_native.hook_events[queue_index].generation = generation;
    dluma_str_copy(g_dluma_native.hook_events[queue_index].name,
                 DLUMA_ARRAY_COUNT(g_dluma_native.hook_events[queue_index].name),
                 exact_name);
    ++g_dluma_native.hook_event_count;
    dluma_native_unlock(&g_dluma_native.hook_event_lock);
    return 1;
}

void dluma_native_gameplay_quiesce(void)
{
    DWORD old_protect;
    DWORD restored_protect;
    PVOID previous_slot;
    /* Close observation before touching the slot.  A thread that fetched the
     * hook immediately before restoration still reaches the immutable
     * lifetime original, but can no longer enqueue a callback.
     *
     * This function runs synchronously inside IGame::ExitGame before the
     * stock function is allowed to close the process.  Never wait for a lock
     * that may belong to an interrupted or re-entrant game-thread callback:
     * readiness is already suppressed by `stopping`, and a contended observer
     * restore can be retried by the final ShutdownGameScriptDLL path.  The
     * module-pinned pass-through hook and lifetime original remain safe if
     * final shutdown never arrives. */
    dluma_atomic_exchange(&g_dluma_interactive_observation_stopped, 1);
    dluma_atomic_exchange(&g_dluma_native.stopping, 1);
    dluma_quest_hud_quiesce();
    if (!dluma_native_try_lock(&g_dluma_interactive_lifecycle_lock)) return;
    dluma_atomic_exchange(&g_dluma_interactive_observation_stopped, 1);
    if (!g_dluma_native.hook_installed || !g_dluma_native.interactive_slot)
        goto done;
    if (!dluma_is_readable(g_dluma_native.interactive_slot, sizeof(void*)))
        goto done;
    if (*g_dluma_native.interactive_slot != (void*)dluma_native_interactive_hook) {
        dluma_log_line("WARN", "InteractiveItem vtable slot changed ownership; original callback is retained in pass-through mode.");
        goto done;
    }
    if (!VirtualProtect(g_dluma_native.interactive_slot, sizeof(void*),
                        PAGE_READWRITE, &old_protect)) goto done;
    previous_slot = dluma_atomic_pointer_compare_exchange(
        (PVOID volatile*)g_dluma_native.interactive_slot,
        g_dluma_native.interactive_original,
        (PVOID)dluma_native_interactive_hook);
    if (!VirtualProtect(g_dluma_native.interactive_slot, sizeof(void*),
                        old_protect, &restored_protect))
        dluma_log_line("WARN", "InteractiveItem vtable slot was restored, but its original page protection could not be reapplied.");
    FlushInstructionCache(GetCurrentProcess(), g_dluma_native.interactive_slot,
                          sizeof(void*));
    if (previous_slot != (PVOID)dluma_native_interactive_hook) {
        dluma_log_line("WARN", "InteractiveItem vtable slot changed ownership during restore; foreign slot value preserved and observer retained in pass-through mode.");
        goto done;
    }
    g_dluma_native.hook_installed = 0;
    dluma_log_pair("INFO", "interactive_item_hook_restored", "InteractiveItem::OnExecuteTrigger");
done:
    dluma_native_unlock(&g_dluma_interactive_lifecycle_lock);
}

void dluma_native_gameplay_stop(void)
{
    uint64_t started;
    dluma_atomic_exchange(&g_dluma_native_stop_ok, 0);
    if (!g_dluma_native.started) {
        dluma_atomic_exchange(&g_dluma_native_stop_ok, 1);
        return;
    }
    dluma_native_gameplay_quiesce();
    started = GetTickCount64();
    while (dluma_atomic_compare_exchange(
               &g_dluma_native.hook_active_calls, 0, 0) != 0 &&
           GetTickCount64() - started < 2000u) Sleep(1);
    if (dluma_atomic_compare_exchange(
            &g_dluma_native.hook_active_calls, 0, 0) != 0) {
        dluma_log_line("WARN", "Timed out draining InteractiveItem callbacks; original pointer is retained for shutdown safety.");
        return;
    }
    if (g_dluma_native.hook_installed) {
        dluma_log_line("WARN", "InteractiveItem hook could not be restored; native adapter state is retained.");
        return;
    }
    if (!dluma_quest_hud_stop()) {
        dluma_log_line(
            "WARN",
            "Quest HUD hook could not be drained; native adapter state is retained.");
        return;
    }
    dluma_log_line("INFO", "interactive_item_observer_summary");
    dluma_log_u64("INFO", "interactive_item_hook_call",
        (ULONGLONG)dluma_atomic_compare_exchange(
            &g_dluma_native.interaction.hook_calls, 0, 0));
    dluma_log_u64("INFO", "interactive_item_action_observed",
        (ULONGLONG)dluma_atomic_compare_exchange(
            &g_dluma_native.interaction.action_execute, 0, 0));
    dluma_log_u64("INFO", "interactive_item_original_success",
        (ULONGLONG)dluma_atomic_compare_exchange(
            &g_dluma_native.interaction.successful_original, 0, 0));
    dluma_log_u64("INFO", "interactive_item_index_match",
        (ULONGLONG)dluma_atomic_compare_exchange(
            &g_dluma_native.interaction.complete_object_matches, 0, 0));
    dluma_log_u64("INFO", "interactive_item_subscription_match",
        (ULONGLONG)dluma_atomic_compare_exchange(
            &g_dluma_native.interaction.subscription_matches, 0, 0));
    dluma_log_u64("INFO", "interactive_item_event_queued",
        (ULONGLONG)dluma_atomic_compare_exchange(
            &g_dluma_native.interaction.events_queued, 0, 0));
    dluma_log_u64("INFO", "interactive_item_event_dropped",
        (ULONGLONG)dluma_atomic_compare_exchange(
            &g_dluma_native.interaction.events_dropped, 0, 0));
    memset(&g_dluma_native, 0, sizeof(g_dluma_native));
    dluma_atomic_exchange(&g_dluma_native_stop_ok, 1);
}

int dluma_native_gameplay_stop_succeeded(void)
{
    return dluma_atomic_compare_exchange(&g_dluma_native_stop_ok, 0, 0) != 0;
}
