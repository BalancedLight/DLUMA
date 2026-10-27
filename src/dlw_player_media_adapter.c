#include "dlw_player_media_adapter.h"
#include "dlw_vehicle_gate_core.h"
#include "dlw_internal_hook_attestation.h"
#include "dlw_path_security.h"
#include "bindings/dlw_player_actor_binding.h"

#define DLUMA_DL_ENGINE_TIMESTAMP 0x623395D0UL
#define DLUMA_DL_ENGINE_IMAGE_SIZE 0x00E8D000UL
#define DLUMA_DL_GAMEDLL_TIMESTAMP 0x623397AAUL
#define DLUMA_DL_GAMEDLL_IMAGE_SIZE 0x02082000UL

#define DLUMA_DL_LOCAL_BUGGY_RVA 0x004B3260UL
#define DLUMA_DL_LOCAL_ROOT_SLOT_RVA 0x01D07FD0UL
#define DLUMA_DL_ACTOR_IS_DRIVER_RVA 0x00E76300UL
#define DLUMA_DL_RTDYNAMICCAST_THUNK_RVA 0x0130A87EUL
#define DLUMA_DL_PLAYERDI_TYPE_RVA 0x01CA3FB0UL
#define DLUMA_DL_PLAYERDI_PRIMARY_COL_RVA 0x01AD6198UL
#define DLUMA_DL_PLAYERDI_PRIMARY_VTABLE_RVA 0x017DD738UL
#define DLUMA_DL_PLAYERDI_ACTOR_COL_RVA 0x01AD6148UL
#define DLUMA_DL_PLAYERDI_ACTOR_VTABLE_RVA 0x017DE468UL
#define DLUMA_DL_PLAYERDI_ACTOR_SUBOBJECT_OFFSET 0x0028U
#define DLUMA_DL_PLAYERDI_HIERARCHY_RVA 0x01AA4EC8UL
#define DLUMA_DL_PLAYERDI_BASE_ARRAY_RVA 0x01AA4EE0UL
#define DLUMA_DL_PLAYERDI_BASE_DESCRIPTOR_RVA 0x01AA4EA0UL
#define DLUMA_DL_PLAYERDI_BASE_COUNT 42U
#define DLUMA_DL_ACTORDI_TYPE_RVA 0x01CA9818UL
#define DLUMA_DL_ACTORDI_HIERARCHY_RVA 0x01A1C648UL
#define DLUMA_DL_ACTORDI_BASE_ARRAY_RVA 0x01A1C660UL
#define DLUMA_DL_ACTORDI_BASE_DESCRIPTOR_RVA 0x01A1C620UL
#define DLUMA_DL_ACTORDI_BASE_COUNT 28U
#define DLUMA_DL_PLAYERDI_CURRENT_VEHICLE_SLOT 0x08D0U
#define DLUMA_DL_PLAYERDI_CURRENT_VEHICLE_RVA 0x00B3B830UL
#define DLUMA_DL_PLAYERDI_LOCAL_PREDICATE_SLOT 0x0690U
#define DLUMA_DL_PLAYERDI_LOCAL_PREDICATE_RVA 0x00937E80UL
#define DLUMA_DL_CAR_BUGGY_TYPE_RVA 0x01CA61A8UL
#define DLUMA_DL_VEHICLE_BASE_TYPE_RVA 0x01CB2BB8UL
#define DLUMA_DL_CAR_BUGGY_COL_RVA 0x01B0C770UL
#define DLUMA_DL_CAR_BUGGY_VTABLE_RVA 0x018C37C8UL
#define DLUMA_DL_CAR_BUGGY_CONTROL_COL_RVA 0x01B0C748UL
#define DLUMA_DL_CAR_BUGGY_CONTROL_VTABLE_RVA 0x018C3D28UL
#define DLUMA_DL_CAR_BUGGY_HIERARCHY_RVA 0x01B0C798UL
#define DLUMA_DL_CAR_BUGGY_BASE_ARRAY_RVA 0x01B0C7B0UL
#define DLUMA_DL_CAR_BUGGY_BASE_DESCRIPTOR_RVA 0x01B0C958UL
#define DLUMA_DL_CAR_BUGGY_BASE_COUNT 20U
#define DLUMA_DL_CAR_BUGGY_LINEAR_VELOCITY_SLOT 0x01B8U
#define DLUMA_DL_CAR_BUGGY_LINEAR_VELOCITY_RVA 0x00E63160UL
#define DLUMA_DL_CAR_BUGGY_PHYSICS_BIND_IAT_RVA 0x013CD360UL
#define DLUMA_DL_CAR_BUGGY_LINEAR_VELOCITY_IAT_RVA 0x013CD7A0UL
#define DLUMA_DL_VEHICLE_BASE_HIERARCHY_RVA 0x01B0C8A8UL
#define DLUMA_DL_VEHICLE_BASE_ARRAY_RVA 0x01B0C8C0UL
#define DLUMA_DL_VEHICLE_BASE_DESCRIPTOR_RVA 0x01B0C880UL
#define DLUMA_DL_VEHICLE_BASE_COUNT 18U
#define DLUMA_DL_VEHICLE_BASE_GET_DRIVER_RVA 0x00F8C310UL
#define DLUMA_DL_VEHICLE_DRIVER_OWNER_CALLSITE_RVA 0x0035B501UL
#define DLUMA_DL_LOCAL_DRIVER_QUERY_CALLSITE_RVA 0x00F5A8DBUL
#define DLUMA_DL_PHYSICS_BIND_RVA 0x00141870UL
#define DLUMA_DL_PHYSICS_BIND_TYPE_RVA 0x00AB0678UL
#define DLUMA_DL_PHYSICS_BIND_VTABLE_RVA 0x008B1158UL
#define DLUMA_DL_PHYSICS_INTERFACE_RVA 0x00383090UL
#define DLUMA_DL_PHYSICS_BODY_AT_RVA 0x003833B0UL
#define DLUMA_DL_LINEAR_VELOCITY_RVA 0x0034A3B0UL

#define DLUMA_DL_AUDIO_SINGLETON_RVA 0x00AC8D78UL
#define DLUMA_DL_AUDIO_VTABLE_RVA 0x008A9628UL
#define DLUMA_DL_LOAD_WAVE_BANK_RVA 0x000FA3B0UL
#define DLUMA_DL_LOAD_WAVE_BANK_CALL_RVA 0x000FA44DUL
#define DLUMA_DL_SOUND_BANK_LOAD_RVA 0x00100920UL
#define DLUMA_DL_PLAY_WAVE_2D_RVA 0x0027C890UL
#define DLUMA_DL_STOP_WAVE_RVA 0x0027D7C0UL
#define DLUMA_DL_IS_WAVE_PLAYING_RVA 0x0027DA30UL
#define DLUMA_DL_SET_WAVE_POSITION_SECONDS_RVA 0x0027DAD0UL
#define DLUMA_DL_SET_WAVE_VOLUME_RVA 0x0027DE10UL
#define DLUMA_DL_GET_WAVE_TIME_SECONDS_RVA 0x002B1040UL
#define DLUMA_DL_GET_WAVE_DURATION_NAME_RVA 0x0027E740UL
#define DLUMA_DL_IGSOBJECT_GET_LEVEL_RVA 0x00277A00UL

#define DLUMA_DL_MUSIC_MANAGER_SINGLETON_RVA 0x01D074E8UL
#define DLUMA_DL_MUSIC_MANAGER_CTOR_RVA 0x00977A50UL
#define DLUMA_DL_MUSIC_MANAGER_STOP_RVA 0x00979BE0UL
#define DLUMA_DL_MUSIC_MANAGER_START_RVA 0x00979C60UL
#define DLUMA_DL_MUSIC_MANAGER_DELAYED_START_RVA 0x00979CC0UL
#define DLUMA_DL_MUSIC_MANAGER_VTABLE_RVA 0x0170D318UL
#define DLUMA_DL_MUSIC_MANAGER_VTABLE_8_RVA 0x0170DAB0UL
#define DLUMA_DL_MUSIC_MANAGER_VTABLE_16_RVA 0x0170DAD8UL
#define DLUMA_DL_MUSIC_MANAGER_VTABLE_48_RVA 0x0170DAE8UL
#define DLUMA_DL_MUSIC_MANAGER_TYPE_RVA 0x01CD2B48UL
#define DLUMA_DL_MUSIC_MANAGER_COL_RVA 0x01AA7060UL
#define DLUMA_DL_MUSIC_MANAGER_HIERARCHY_RVA 0x01AA7088UL
#define DLUMA_DL_MUSIC_MANAGER_BASE_ARRAY_RVA 0x01AA70A0UL
#define DLUMA_DL_MUSIC_MANAGER_IGSOBJECT_BASE_DESCRIPTOR_RVA 0x01A05248UL
#define DLUMA_DL_GAMEDI_TYPE_RVA 0x01CB60C8UL
#define DLUMA_DL_GAMEDI_PRIMARY_COL_RVA 0x01A530D8UL
#define DLUMA_DL_GAMEDI_PRIMARY_VTABLE_RVA 0x0157D5F8UL
#define DLUMA_DL_GAMEDI_FRAME_COL_RVA 0x01A530B0UL
#define DLUMA_DL_GAMEDI_FRAME_VTABLE_RVA 0x0157E2E8UL
#define DLUMA_DL_GAMEDI_FRAME_SUBOBJECT_OFFSET 0x0010U
#define DLUMA_DL_GAMEDI_GLOBAL_RVA 0x01D07F88UL
#define DLUMA_DL_GAMEDI_SESSION_OFFSET 0x0508U
#define DLUMA_DL_DRIVING_CONDITION_RVA 0x0037F4F0UL
#define DLUMA_DL_SESSIONDI_TYPE_RVA 0x01CBCBB8UL
#define DLUMA_DL_SESSIONDI_COL_RVA 0x01A57B08UL
#define DLUMA_DL_SESSIONDI_VTABLE_RVA 0x01591FC8UL
#define DLUMA_DL_SESSIONDI_HIERARCHY_RVA 0x01A573A0UL
#define DLUMA_DL_SESSIONDI_BASE_ARRAY_RVA 0x01A573C0UL
#define DLUMA_DL_SESSIONDI_BASE_DESCRIPTOR_RVA 0x01A57378UL
#define DLUMA_DL_SESSION_COOPERATIVE_TYPE_RVA 0x01CBCBD8UL
#define DLUMA_DL_SESSION_COOPERATIVE_COL_RVA 0x01A572E0UL
#define DLUMA_DL_SESSION_COOPERATIVE_VTABLE_RVA 0x0158E488UL
#define DLUMA_DL_SESSION_COOPERATIVE_HIERARCHY_RVA 0x01A57308UL
#define DLUMA_DL_SESSION_COOPERATIVE_BASE_ARRAY_RVA 0x01A57320UL
#define DLUMA_DL_SESSION_COOPERATIVE_BASE_DESCRIPTOR_RVA 0x01A57498UL
#define DLUMA_DL_SESSION_COOPERATIVE_DI_TYPE_RVA 0x01CBCC50UL
#define DLUMA_DL_SESSION_COOPERATIVE_DI_COL_RVA 0x01A575B8UL
#define DLUMA_DL_SESSION_COOPERATIVE_DI_VTABLE_RVA 0x0158F838UL
#define DLUMA_DL_SESSION_COOPERATIVE_DI_HIERARCHY_RVA 0x01A575E0UL
#define DLUMA_DL_SESSION_COOPERATIVE_DI_BASE_ARRAY_RVA 0x01A57600UL
#define DLUMA_DL_SESSION_COOPERATIVE_DI_BASE_DESCRIPTOR_RVA 0x01A57660UL
#define DLUMA_DL_SESSION_LEVEL_SLOT 0x07A0U
#define DLUMA_DL_SESSION_LEVEL_RVA 0x006AD140UL
#define DLUMA_DL_ILEVEL_TYPE_RVA 0x01CBB4C0UL
#define DLUMA_DL_LEVELDI_TYPE_RVA 0x01CA2F30UL
#define DLUMA_DL_LEVELDI_COL_RVA 0x01A54368UL
#define DLUMA_DL_LEVELDI_VTABLE_RVA 0x01581C98UL
#define DLUMA_DL_LEVELDI_HIERARCHY_RVA 0x01A54390UL
#define DLUMA_DL_LEVELDI_BASE_ARRAY_RVA 0x01A543B0UL
#define DLUMA_DL_LEVELDI_IGSOBJECT_OFFSET 0x0010U
#define DLUMA_DL_LEVELDI_IGSOBJECT_COL_RVA 0x01A54340UL
#define DLUMA_DL_LEVELDI_IGSOBJECT_VTABLE_RVA 0x015822E8UL
#define DLUMA_DL_LEVELDI_IGSOBJECT_BASE_DESCRIPTOR_RVA 0x01A05128UL
#define DLUMA_DL_IGSOBJECT_TYPE_RVA 0x01C952A8UL
#define DLUMA_DL_IGSOBJECT_HIERARCHY_RVA 0x01A05270UL
#define DLUMA_DL_IGSOBJECT_BASE_ARRAY_RVA 0x01A05288UL
#define DLUMA_DL_MUSIC_MANAGER_CREATE_TYPE_RVA 0x01D6CC50UL
#define DLUMA_DL_LEVELDI_MUSIC_MANAGER_OFFSET 0x09D0U
#define DLUMA_DL_LEVELDI_MUSIC_CREATE_EVIDENCE_RVA 0x003E925AUL
#define DLUMA_DL_LEVELDI_MUSIC_RTTI_LEA_RVA 0x003E9267UL
#define DLUMA_DL_LEVELDI_MUSIC_STORE_RVA 0x003E927AUL
#define DLUMA_DL_MUSIC_MANAGER_WANTED_TYPE_OFFSET 0x58U
#define DLUMA_DL_MUSIC_MANAGER_DELAY_OFFSET 0xC4U
#define DLUMA_DL_MUSIC_MANAGER_FLAGS_OFFSET 0xC9U
#define DLUMA_DL_MUSIC_MANAGER_MIN_SIZE 0xD0U
#define DLUMA_DL_MUSIC_MANAGER_PAUSED 0x20U

#define DLUMA_DL_FRONTEND_MUSICS_MANAGER_MUTE_RVA 0x0097AF20UL
#define DLUMA_DL_FRONTEND_MUSICS_MANAGER_UNMUTE_RVA 0x0097B060UL
#define DLUMA_DL_FRONTEND_MUSICS_MANAGER_VTABLE_RVA 0x0170DBC8UL
#define DLUMA_DL_FRONTEND_MUSICS_MANAGER_VTABLE_8_RVA 0x0170E358UL
#define DLUMA_DL_FRONTEND_MUSICS_MANAGER_VTABLE_16_RVA 0x0170E380UL
#define DLUMA_DL_FRONTEND_MUSICS_MANAGER_VTABLE_48_RVA 0x0170E390UL
#define DLUMA_DL_FRONTEND_MUSICS_MANAGER_TYPE_RVA 0x01CD2BB0UL
#define DLUMA_DL_FRONTEND_MUSICS_MANAGER_COL_RVA 0x01AA7208UL
#define DLUMA_DL_FRONTEND_MUSICS_MANAGER_MUTED_OFFSET 0xB0U
#define DLUMA_DL_FRONTEND_MUSICS_MANAGER_MIN_SIZE 0xD0U

#define DLUMA_DL_MAIN_MENU_MODULE_PLAY_MUSIC_RVA 0x011B70F0UL
#define DLUMA_DL_MAIN_MENU_MODULE_STOP_MUSIC_RVA 0x011B7380UL
#define DLUMA_DL_MAIN_MENU_MODULE_VTABLE_RVA 0x01678C38UL
#define DLUMA_DL_MAIN_MENU_MODULE_TYPE_RVA 0x01CCD3A8UL
#define DLUMA_DL_MAIN_MENU_MODULE_COL_RVA 0x01A90A40UL
#define DLUMA_DL_MAIN_MENU_MODULE_IGSOBJECT_VTABLE_RVA 0x01679718UL
#define DLUMA_DL_MAIN_MENU_MODULE_IGSOBJECT_OFFSET 0x0010U
#define DLUMA_DL_MAIN_MENU_MODULE_MUSIC_HANDLE_OFFSET 0x0B18U
#define DLUMA_DL_MAIN_MENU_MODULE_MIN_SIZE 0x0B4CU

#define DLUMA_DL_PLAYER_MEDIA_MAX_WAVES 32U
#define DLUMA_DL_PLAYER_MEDIA_MAX_POSITION_MS 86400000ULL
#define DLUMA_DL_AUDIO_UPDATE_RVA 0x000F86A0UL
#define DLUMA_DL_AUDIO_SET_FILTER_RVA 0x000FCE80UL
#define DLUMA_DL_AUDIO_TRY_LOCK_RVA 0x000F4D50UL
#define DLUMA_DL_AUDIO_UNLOCK_RVA 0x000F4D40UL
#define DLUMA_DL_AUDIO_SYSTEM_OFFSET 0x1D70UL
#define DLUMA_DL_AUDIO_GROUP_VECTOR_OFFSET 0x1EE8UL
#define DLUMA_DL_AUDIO_GROUP_COUNT_OFFSET 0x1EF0UL
#define DLUMA_DL_AUDIO_BUS_COUNT_OFFSET 0x3B94UL
#define DLUMA_DL_AUDIO_BUS_RECORDS_OFFSET 0x3BA8UL
#define DLUMA_DL_AUDIO_BUS_RECORD_STRIDE 0x2E4UL
#define DLUMA_DL_AUDIO_FILTER_TYPE_OFFSET 0x254UL
#define DLUMA_DL_AUDIO_FILTER_RESONANCE_OFFSET 0x258UL
#define DLUMA_DL_AUDIO_FILTER_FREQUENCY_OFFSET 0x25CUL
#define DLUMA_DL_AUDIO_FILTER_STATE_OFFSET 0x260UL
#define DLUMA_DL_AUDIO_LOWPASS_DSP_OFFSET 0x6420UL
#define DLUMA_DL_AUDIO_FILTER_DIRTY_OFFSET 0x6570UL
#define DLUMA_DL_AUDIO_INITIALIZED_OFFSET 0x66A0UL
#define DLUMA_DL_AUDIO_BUS_LIMIT 14U
#define DLUMA_DL_AUDIO_BUS_NAME_LIMIT 64U
#define DLUMA_DL_FMOD_LOWPASS_TYPE 3

#define DLUMA_DL_FMOD_GET_BYPASS_RVA 0x0008AFC0UL
#define DLUMA_DL_FMOD_SET_BYPASS_RVA 0x0008AF80UL
#define DLUMA_DL_FMOD_GET_PARAMETER_RVA 0x0008B110UL
#define DLUMA_DL_FMOD_SET_PARAMETER_RVA 0x0008B0D0UL
#define DLUMA_DL_FMOD_GET_TYPE_RVA 0x0008B2D0UL
#define DLUMA_DL_LIVE_READINESS_PROBE_MS 100ULL
#define DLUMA_DL_FILTER_DIAGNOSTIC_STABLE_PROBES 5U

typedef enum DlumaPlayerMediaLiveReason {
    DLUMA_DL_LIVE_READY = 0,
    DLUMA_DL_LIVE_PROFILE_INVALID = 1,
    DLUMA_DL_LIVE_SINGLETON_SLOT_OUT_OF_RANGE = 2,
    DLUMA_DL_LIVE_SINGLETON_SLOT_UNREADABLE = 3,
    DLUMA_DL_LIVE_SINGLETON_UNAVAILABLE = 4,
    DLUMA_DL_LIVE_OBJECT_UNREADABLE = 5,
    DLUMA_DL_LIVE_PRIMARY_VTABLE_MISMATCH = 6,
    DLUMA_DL_LIVE_AUDIO_SYSTEM_UNAVAILABLE = 7,
    DLUMA_DL_LIVE_AUDIO_NOT_INITIALIZED = 8,
    DLUMA_DL_LIVE_AUDIO_VTABLE_SLOT_MISMATCH = 9,
    DLUMA_DL_LIVE_SECONDARY_VTABLE_MISMATCH = 10,
    DLUMA_DL_LIVE_VTABLE_TARGET_INVALID = 11,
    DLUMA_DL_LIVE_ACTIVE_LEVEL_UNAVAILABLE = 12,
    DLUMA_DL_LIVE_LEVEL_CAST_FAILED = 13,
    DLUMA_DL_LIVE_LEVEL_RTTI_INVALID = 14,
    DLUMA_DL_LIVE_MANAGER_FIELD_UNREADABLE = 15,
    DLUMA_DL_LIVE_MANAGER_FIELD_UNAVAILABLE = 16,
    DLUMA_DL_LIVE_FILTER_SNAPSHOT_UNAVAILABLE = 17,
    DLUMA_DL_LIVE_OWNER_TRANSITION_RETAINED = 18,
    DLUMA_DL_LIVE_FILTER_BINDING_UNAVAILABLE = 19,
    DLUMA_DL_LIVE_MANAGER_OWNER_QUERY_FAILED = 20,
    DLUMA_DL_LIVE_MANAGER_OWNER_LEVEL_MISMATCH = 21,
    DLUMA_DL_LIVE_GAME_FRAME_RECEIVER_INVALID = 22,
    DLUMA_DL_LIVE_GAME_SESSION_FIELD_UNREADABLE = 23,
    DLUMA_DL_LIVE_GAME_SESSION_UNAVAILABLE = 24,
    DLUMA_DL_LIVE_GAME_SESSION_RTTI_INVALID = 25,
    DLUMA_DL_LIVE_SESSION_LEVEL_QUERY_FAILED = 26,
    DLUMA_DL_LIVE_SESSION_LEVEL_OWNER_MISMATCH = 27,
    DLUMA_DL_LIVE_GAME_GLOBAL_SLOT_UNREADABLE = 28,
    DLUMA_DL_LIVE_GAME_GLOBAL_UNAVAILABLE = 29,
    DLUMA_DL_LIVE_GAME_GLOBAL_RTTI_INVALID = 30,
    DLUMA_DL_LIVE_SESSION_LEVEL_OWNER_QUERY_FAILED = 31,
    DLUMA_DL_LIVE_SESSION_MANAGER_OWNER_QUERY_FAILED = 32,
    DLUMA_DL_LIVE_SESSION_MANAGER_OWNER_MISMATCH = 33,
    DLUMA_DL_LIVE_ACTIVE_LEVEL_SESSION_OWNER_MISMATCH = 34,
    DLUMA_DL_LIVE_GAME_SESSION_OBJECT_UNREADABLE = 35,
    DLUMA_DL_LIVE_GAME_SESSION_CLASS_UNSUPPORTED = 36,
    DLUMA_DL_LIVE_GAME_SESSION_LEVEL_SLOT_MISMATCH = 37,
    DLUMA_DL_LIVE_LEVELDI_IGSOBJECT_INVALID = 38,
    DLUMA_DL_LIVE_RESOLVER_ARGUMENT_INVALID = 39,
    DLUMA_DL_LIVE_RT_DYNAMIC_CAST_UNBOUND = 40,
    DLUMA_DL_LIVE_GET_SESSION_LEVEL_UNBOUND = 41,
    DLUMA_DL_LIVE_GET_OBJECT_LEVEL_UNBOUND = 42,
    DLUMA_DL_LIVE_MANAGER_OBJECT_UNREADABLE = 43,
    DLUMA_DL_LIVE_MANAGER_PRIMARY_VTABLE_MISMATCH = 44,
    DLUMA_DL_LIVE_MANAGER_SECONDARY_VTABLE_8_MISMATCH = 45,
    DLUMA_DL_LIVE_MANAGER_SECONDARY_VTABLE_16_MISMATCH = 46,
    DLUMA_DL_LIVE_MANAGER_SECONDARY_VTABLE_48_MISMATCH = 47,
    DLUMA_DL_LIVE_MANAGER_PRIMARY_TARGET_INVALID = 48,
    DLUMA_DL_LIVE_MANAGER_OWNER_MISMATCH = 49,
    DLUMA_DL_LIVE_READINESS_INVARIANT_VIOLATION = 50
} DlumaPlayerMediaLiveReason;

typedef enum DlumaPlayerMediaWaveBindingReason {
    DLUMA_DL_WAVE_BIND_READY_ORIGINAL_ENTRY = 0,
    DLUMA_DL_WAVE_BIND_READY_LOADER_OWNED_DETOUR = 1,
    DLUMA_DL_WAVE_BIND_PROFILE_INVALID = 2,
    DLUMA_DL_WAVE_BIND_PREFIX_MISMATCH = 3,
    DLUMA_DL_WAVE_BIND_CALL_TARGET_MISMATCH = 4,
    DLUMA_DL_WAVE_BIND_VTABLE_SLOT_MISMATCH = 5
} DlumaPlayerMediaWaveBindingReason;

typedef enum DlumaPlayerMediaMusicBindingReason {
    DLUMA_DL_MUSIC_BIND_READY = 0,
    DLUMA_DL_MUSIC_BIND_PROFILE_INVALID = 1,
    DLUMA_DL_MUSIC_BIND_CTOR_PREFIX_MISMATCH = 2,
    DLUMA_DL_MUSIC_BIND_STOP_PREFIX_MISMATCH = 3,
    DLUMA_DL_MUSIC_BIND_START_PREFIX_MISMATCH = 4,
    DLUMA_DL_MUSIC_BIND_DELAYED_START_PREFIX_MISMATCH = 5,
    DLUMA_DL_MUSIC_BIND_RTDYNAMICCAST_PREFIX_MISMATCH = 6,
    DLUMA_DL_MUSIC_BIND_ILEVEL_TYPE_NAME_MISMATCH = 7,
    DLUMA_DL_MUSIC_BIND_LEVELDI_TYPE_NAME_MISMATCH = 8,
    DLUMA_DL_MUSIC_BIND_LEVELDI_PRIMARY_RTTI_MISMATCH = 9,
    DLUMA_DL_MUSIC_BIND_CREATE_PREFIX_MISMATCH = 10,
    DLUMA_DL_MUSIC_BIND_CREATE_TYPE_TARGET_MISMATCH = 11,
    DLUMA_DL_MUSIC_BIND_STORE_PREFIX_MISMATCH = 12,
    DLUMA_DL_MUSIC_BIND_OWNER_LEVEL_EXPORT_MISMATCH = 13,
    DLUMA_DL_MUSIC_BIND_GAMEDI_PRIMARY_RTTI_MISMATCH = 14,
    DLUMA_DL_MUSIC_BIND_GAMEDI_FRAME_RTTI_MISMATCH = 15,
    DLUMA_DL_MUSIC_BIND_SESSIONDI_RTTI_MISMATCH = 16,
    DLUMA_DL_MUSIC_BIND_SESSION_LEVEL_PREFIX_MISMATCH = 17,
    DLUMA_DL_MUSIC_BIND_SESSION_LEVEL_SLOT_MISMATCH = 18,
    DLUMA_DL_MUSIC_BIND_GAMEDI_GLOBAL_PREFIX_MISMATCH = 19,
    DLUMA_DL_MUSIC_BIND_GAMEDI_GLOBAL_TARGET_MISMATCH = 20,
    DLUMA_DL_MUSIC_BIND_GAMEDI_GLOBAL_SLOT_OUT_OF_RANGE = 21,
    DLUMA_DL_MUSIC_BIND_SESSION_COOPERATIVE_RTTI_MISMATCH = 22,
    DLUMA_DL_MUSIC_BIND_SESSION_COOPERATIVE_DI_RTTI_MISMATCH = 23,
    DLUMA_DL_MUSIC_BIND_SESSION_COOPERATIVE_LEVEL_SLOT_MISMATCH = 24,
    DLUMA_DL_MUSIC_BIND_SESSION_COOPERATIVE_DI_LEVEL_SLOT_MISMATCH = 25,
    DLUMA_DL_MUSIC_BIND_LEVELDI_IGSOBJECT_RTTI_MISMATCH = 26,
    DLUMA_DL_MUSIC_BIND_MANAGER_IGSOBJECT_RTTI_MISMATCH = 27
} DlumaPlayerMediaMusicBindingReason;

typedef enum DlumaPlayerMediaSessionClass {
    DLUMA_DL_SESSION_CLASS_NONE = 0,
    DLUMA_DL_SESSION_CLASS_SESSION_DI = 1,
    DLUMA_DL_SESSION_CLASS_SESSION_COOPERATIVE = 2,
    DLUMA_DL_SESSION_CLASS_SESSION_COOPERATIVE_DI = 3
} DlumaPlayerMediaSessionClass;

typedef enum DlumaPlayerMediaMusicSource {
    DLUMA_DL_MUSIC_SOURCE_NONE = 0,
    DLUMA_DL_MUSIC_SOURCE_ACTIVE_LEVEL_SINGLETON_NULL = 1,
    DLUMA_DL_MUSIC_SOURCE_ACTIVE_LEVEL_CORROBORATED = 2,
    DLUMA_DL_MUSIC_SOURCE_ACTIVE_LEVEL_STALE_SINGLETON_IGNORED = 3,
    DLUMA_DL_MUSIC_SOURCE_SINGLETON_OWNER_LEVEL_CORROBORATED = 4,
    DLUMA_DL_MUSIC_SOURCE_GAME_SESSION_LEVEL_CORROBORATED = 5,
    DLUMA_DL_MUSIC_SOURCE_GAME_GLOBAL_SESSION_MANAGER_OWNER_CORROBORATED = 6,
    DLUMA_DL_MUSIC_SOURCE_CUSTOM_FRONTEND_BACKGROUND_OWNER_CORROBORATED = 7
} DlumaPlayerMediaMusicSource;

typedef enum DlumaPlayerMediaMusicOwnershipRoute {
    DLUMA_DL_MUSIC_OWNERSHIP_NONE = 0,
    DLUMA_DL_MUSIC_OWNERSHIP_STOCK_MANAGER = 1,
    DLUMA_DL_MUSIC_OWNERSHIP_FRONTEND_MUSICS_MANAGER = 2,
    DLUMA_DL_MUSIC_OWNERSHIP_FRONTEND_MAIN_MENU = 3
} DlumaPlayerMediaMusicOwnershipRoute;

typedef struct DlumaPlayerMediaCompleteObjectLocator {
    DWORD signature;
    DWORD offset;
    DWORD cd_offset;
    DWORD type_descriptor_rva;
    DWORD class_hierarchy_rva;
    DWORD self_rva;
} DlumaPlayerMediaCompleteObjectLocator;

typedef struct DlumaPlayerMediaClassHierarchyDescriptor {
    DWORD signature;
    DWORD attributes;
    DWORD base_count;
    DWORD base_array_rva;
} DlumaPlayerMediaClassHierarchyDescriptor;

typedef struct DlumaPlayerMediaBaseClassDescriptor {
    DWORD type_descriptor_rva;
    DWORD contained_base_count;
    LONG member_displacement;
    LONG vbtable_displacement;
    LONG displacement_inside_vbtable;
    DWORD attributes;
    DWORD class_hierarchy_rva;
} DlumaPlayerMediaBaseClassDescriptor;

typedef char DlumaPlayerMediaBaseClassDescriptorSize[
    sizeof(DlumaPlayerMediaBaseClassDescriptor) == 28U ? 1 : -1];

typedef struct DlumaPlayerMediaSessionClassProfile {
    unsigned int session_class;
    const char* diagnostic_name;
    const char* type_name;
    DWORD type_rva;
    DWORD col_rva;
    DWORD vtable_rva;
    DWORD hierarchy_rva;
    DWORD base_array_rva;
    DWORD self_base_descriptor_rva;
    DWORD base_count;
    DWORD sessiondi_base_index;
    DWORD base_prefix_count;
    DWORD base_prefix[3];
} DlumaPlayerMediaSessionClassProfile;

static const DlumaPlayerMediaSessionClassProfile
g_dluma_player_media_session_classes[] = {
    {
        DLUMA_DL_SESSION_CLASS_SESSION_DI,
        "SessionDI",
        ".?AVSessionDI@@",
        DLUMA_DL_SESSIONDI_TYPE_RVA,
        DLUMA_DL_SESSIONDI_COL_RVA,
        DLUMA_DL_SESSIONDI_VTABLE_RVA,
        DLUMA_DL_SESSIONDI_HIERARCHY_RVA,
        DLUMA_DL_SESSIONDI_BASE_ARRAY_RVA,
        DLUMA_DL_SESSIONDI_BASE_DESCRIPTOR_RVA,
        9U,
        0U,
        1U,
        {
            DLUMA_DL_SESSIONDI_BASE_DESCRIPTOR_RVA,
            0U,
            0U
        }
    },
    {
        DLUMA_DL_SESSION_CLASS_SESSION_COOPERATIVE,
        "SessionCooperative",
        ".?AVSessionCooperative@@",
        DLUMA_DL_SESSION_COOPERATIVE_TYPE_RVA,
        DLUMA_DL_SESSION_COOPERATIVE_COL_RVA,
        DLUMA_DL_SESSION_COOPERATIVE_VTABLE_RVA,
        DLUMA_DL_SESSION_COOPERATIVE_HIERARCHY_RVA,
        DLUMA_DL_SESSION_COOPERATIVE_BASE_ARRAY_RVA,
        DLUMA_DL_SESSION_COOPERATIVE_BASE_DESCRIPTOR_RVA,
        10U,
        1U,
        2U,
        {
            DLUMA_DL_SESSION_COOPERATIVE_BASE_DESCRIPTOR_RVA,
            DLUMA_DL_SESSIONDI_BASE_DESCRIPTOR_RVA,
            0U
        }
    },
    {
        DLUMA_DL_SESSION_CLASS_SESSION_COOPERATIVE_DI,
        "SessionCooperativeDI",
        ".?AVSessionCooperativeDI@@",
        DLUMA_DL_SESSION_COOPERATIVE_DI_TYPE_RVA,
        DLUMA_DL_SESSION_COOPERATIVE_DI_COL_RVA,
        DLUMA_DL_SESSION_COOPERATIVE_DI_VTABLE_RVA,
        DLUMA_DL_SESSION_COOPERATIVE_DI_HIERARCHY_RVA,
        DLUMA_DL_SESSION_COOPERATIVE_DI_BASE_ARRAY_RVA,
        DLUMA_DL_SESSION_COOPERATIVE_DI_BASE_DESCRIPTOR_RVA,
        11U,
        2U,
        3U,
        {
            DLUMA_DL_SESSION_COOPERATIVE_DI_BASE_DESCRIPTOR_RVA,
            DLUMA_DL_SESSION_COOPERATIVE_BASE_DESCRIPTOR_RVA,
            DLUMA_DL_SESSIONDI_BASE_DESCRIPTOR_RVA
        }
    }
};

typedef char DlumaPlayerMediaSessionClassProfileCount[
    DLUMA_ARRAY_COUNT(g_dluma_player_media_session_classes) == 3U ? 1 : -1];

typedef void* (__fastcall *DlumaPlayerMediaLocalBuggyFn)(void);
typedef void* (__fastcall *DlumaPlayerMediaRtDynamicCastFn)(
    void* object, LONG source_offset, const void* source_type,
    const void* target_type, int is_reference);
typedef unsigned char (__fastcall *DlumaPlayerMediaActorIsDriverFn)(void* actor);
typedef void* (__fastcall *DlumaPlayerMediaCurrentVehicleFn)(void* actor);
typedef void* (__fastcall *DlumaPlayerMediaVehicleDriverFn)(void* vehicle);
typedef float* (__fastcall *DlumaPlayerMediaVehicleVelocityFn)(
    void* vehicle, float* velocity_out);
typedef unsigned char (__fastcall *DlumaPlayerMediaAudioTryLockFn)(void);
typedef void (__fastcall *DlumaPlayerMediaAudioUnlockFn)(void);
typedef int (__fastcall *DlumaPlayerMediaFmodGetTypeFn)(void* dsp, int* type_out);
typedef int (__fastcall *DlumaPlayerMediaFmodGetBypassFn)(
    void* dsp, unsigned char* bypass_out);
typedef int (__fastcall *DlumaPlayerMediaFmodGetParameterFn)(
    void* dsp, int index, float* value_out, char* text_out, int text_capacity);
typedef int (__fastcall *DlumaPlayerMediaFmodSetBypassFn)(
    void* dsp, unsigned char bypass);
typedef int (__fastcall *DlumaPlayerMediaFmodSetParameterFn)(
    void* dsp, int index, float value);

typedef struct DlumaPlayerMediaTtlStringView {
    const char* data;
    unsigned int size;
    unsigned int capacity;
} DlumaPlayerMediaTtlStringView;

typedef char DlumaPlayerMediaTtlStringViewSize[
    sizeof(DlumaPlayerMediaTtlStringView) == 16U ? 1 : -1];

typedef int (__fastcall *DlumaPlayerMediaLoadWaveBankFn)(
    void* audio, const char* bank_name, unsigned char localized);
typedef int (__fastcall *DlumaPlayerMediaPlayWave2DFn)(
    void* owner, const DlumaPlayerMediaTtlStringView* wave_name,
    unsigned char loop, float fade_in_seconds, float fade_out_seconds,
    unsigned int group, float start_time_seconds, int start_sample,
    unsigned int routing_id, unsigned char routing_flag);
typedef void (__fastcall *DlumaPlayerMediaStopWaveFn)(
    void* owner, int handle, float fade_out_seconds);
typedef unsigned char (__fastcall *DlumaPlayerMediaIsWavePlayingFn)(
    void* owner, int handle);
typedef unsigned char (__fastcall *DlumaPlayerMediaSetWavePositionFn)(
    void* owner, int handle, float seconds);
typedef unsigned char (__fastcall *DlumaPlayerMediaSetWaveVolumeFn)(
    void* owner, int handle, float gain);
typedef float (__fastcall *DlumaPlayerMediaGetWaveTimeFn)(
    void* owner, int handle);
typedef float (__fastcall *DlumaPlayerMediaGetWaveDurationFn)(
    void* owner, const DlumaPlayerMediaTtlStringView* wave_name);
typedef void* (__fastcall *DlumaPlayerMediaObjectLevelFn)(void* owner);
typedef void* (__fastcall *DlumaPlayerMediaSessionLevelFn)(void* session);
typedef void (__fastcall *DlumaPlayerMediaMusicStopFn)(void* manager);
typedef void (__fastcall *DlumaPlayerMediaMusicStartFn)(
    void* manager, float delay_seconds);
typedef void (__fastcall *DlumaPlayerMediaFrontendMusicFn)(
    void* manager, float fade_seconds);
typedef void (__fastcall *DlumaPlayerMediaMainMenuMusicFn)(
    void* main_menu_module);

typedef struct DlumaPlayerMediaWaveSlot {
    int engine_handle;
    float duration_seconds;
    void* owner;
    DWORD generation;
    char cue_name[DLUMA_COMPANION_ARCHIVE_MAX_CUE_NAME];
    unsigned char in_use;
    unsigned char stopping;
    unsigned char reserved[2];
} DlumaPlayerMediaWaveSlot;

typedef struct DlumaPlayerMediaBankSlot {
    const DlumaCompanionArchive* pack;
    DlumaCompanionArchiveAudioManifest manifest;
    DlumaMediaPreparedBankDescriptor prepared_descriptor;
    DWORD generation;
    unsigned char in_use;
    unsigned char prepared;
    unsigned char reserved[2];
} DlumaPlayerMediaBankSlot;

static void dluma_player_media_wave_clear(DlumaPlayerMediaWaveSlot* slot);

typedef struct DlumaPlayerMediaMusicSnapshot {
    void* audio;
    void* channel_group;
    void* lowpass_dsp;
    int bus_index;
    int dsp_type;
    DWORD cached_type;
    DWORD cached_state;
    float cached_resonance;
    float cached_frequency;
    float dsp_parameter0;
    float dsp_parameter1;
    unsigned char dsp_bypass;
    unsigned char valid;
    unsigned char reserved[6];
} DlumaPlayerMediaMusicSnapshot;

typedef struct DlumaPlayerMediaManagerDiagnostic {
    void* manager;
    DWORD primary_vtable_rva;
    DWORD vtable_8_rva;
    DWORD vtable_16_rva;
    DWORD vtable_48_rva;
    unsigned int stage;
} DlumaPlayerMediaManagerDiagnostic;

typedef struct DlumaPlayerMediaPlayerProfile {
    HMODULE player;
    HMODULE engine;
    HMODULE gamedll;
    HMODULE filesystem;
    HMODULE fmod;
    DWORD engine_size;
    DWORD gamedll_size;
    DWORD fmod_size;
    void* game_script_manager;
    void* game_subobject;
    void* observed_active_level;
    void* live_owner_level;
    void* live_level_di;
    void* live_music_manager;
    void* owned_music_manager;
    void* owned_stock_music_controller;
    void* published_frontend_owner_level;
    void* published_frontend_music_manager;
    void* published_frontend_main_menu_module;
    DlumaPlayerMediaLocalBuggyFn local_buggy;
    DlumaPlayerMediaRtDynamicCastFn rt_dynamic_cast;
    DlumaPlayerMediaActorIsDriverFn actor_is_driver;
    DlumaPlayerMediaCurrentVehicleFn current_vehicle;
    DlumaPlayerMediaVehicleDriverFn vehicle_driver;
    DlumaPlayerMediaVehicleVelocityFn vehicle_velocity;
    DlumaPlayerMediaLoadWaveBankFn load_wave_bank;
    DlumaPlayerMediaPlayWave2DFn play_wave_2d;
    DlumaPlayerMediaStopWaveFn stop_wave;
    DlumaPlayerMediaIsWavePlayingFn is_wave_playing;
    DlumaPlayerMediaSetWavePositionFn set_wave_position_seconds;
    DlumaPlayerMediaSetWaveVolumeFn set_wave_volume;
    DlumaPlayerMediaGetWaveTimeFn get_wave_time_seconds;
    DlumaPlayerMediaGetWaveDurationFn get_wave_duration_name;
    DlumaPlayerMediaObjectLevelFn get_object_level;
    DlumaPlayerMediaSessionLevelFn get_session_level;
    DlumaPlayerMediaMusicStopFn music_stop;
    DlumaPlayerMediaMusicStartFn music_start;
    DlumaPlayerMediaFrontendMusicFn frontend_music_mute;
    DlumaPlayerMediaFrontendMusicFn frontend_music_unmute;
    DlumaPlayerMediaMainMenuMusicFn main_menu_music_stop;
    DlumaPlayerMediaMainMenuMusicFn main_menu_music_start;
    DlumaPlayerMediaAudioTryLockFn audio_try_lock;
    DlumaPlayerMediaAudioUnlockFn audio_unlock;
    DlumaPlayerMediaFmodGetTypeFn fmod_get_type;
    DlumaPlayerMediaFmodGetBypassFn fmod_get_bypass;
    DlumaPlayerMediaFmodGetParameterFn fmod_get_parameter;
    DlumaPlayerMediaFmodSetBypassFn fmod_set_bypass;
    DlumaPlayerMediaFmodSetParameterFn fmod_set_parameter;
    DlumaPlayerMediaMusicSnapshot music_snapshot;
    DlumaPlayerMediaBankSlot banks[DLUMA_COMPANION_ARCHIVE_MAX_BANKS];
    DlumaPlayerMediaWaveSlot waves[DLUMA_DL_PLAYER_MEDIA_MAX_WAVES];
    DlumaMediaToken music_token;
    DWORD music_generation;
    unsigned int capabilities;
    unsigned int live_audio_reason;
    unsigned int live_music_reason;
    unsigned int live_filter_reason;
    unsigned int reported_filter_reason;
    unsigned int pending_filter_reason;
    unsigned int filter_reason_streak;
    unsigned int local_buggy_reason;
    unsigned int local_buggy_route;
    unsigned int wave_binding_reason;
    unsigned int music_binding_reason;
    unsigned int live_music_source;
    unsigned int live_session_class;
    unsigned int bank_diagnostic_reason;
    DlumaPlayerMediaManagerDiagnostic manager_diagnostic;
    ULONGLONG next_live_readiness_probe_ms;
    unsigned char exact_profile;
    unsigned char local_buggy_ready;
    unsigned char wave_bank_ready;
    unsigned char wave_exports_ready;
    unsigned char music_manager_ready;
    unsigned char frontend_music_ready;
    unsigned char main_menu_music_ready;
    unsigned char music_filter_ready;
    unsigned char music_fade_ready;
    unsigned char music_wet_mix_ready;
    unsigned char music_owned;
    unsigned char owned_music_route;
    unsigned char stock_music_was_playing;
    unsigned char logged;
    unsigned char live_readiness_initialized;
    unsigned char music_filter_binding_ready;
    unsigned char manager_diagnostic_initialized;
    unsigned char readiness_invariant_violated;
    unsigned char filter_diagnostic_initialized;
    unsigned char local_buggy_diagnostic_initialized;
} DlumaPlayerMediaPlayerProfile;

enum DlumaPlayerMediaBankDiagnosticReason {
    DLUMA_DL_BANK_DIAGNOSTIC_NONE = 0U,
    DLUMA_DL_BANK_DIAGNOSTIC_LOAD_DELIVERY_FAILED,
    DLUMA_DL_BANK_DIAGNOSTIC_LOAD_RESULT_FAILED,
    DLUMA_DL_BANK_DIAGNOSTIC_CUE_PROBE_FAILED,
    DLUMA_DL_BANK_DIAGNOSTIC_READY
};

static DlumaPlayerMediaPlayerProfile g_dluma_player_media_player;
static int
dluma_player_media_bind_wave_bank_detailed(
    DlumaPlayerMediaPlayerProfile* profile, unsigned int* reason_out);
static int dluma_player_media_music_snapshot_take(
    DlumaPlayerMediaPlayerProfile* profile,
    DlumaPlayerMediaMusicSnapshot* snapshot);
static int dluma_player_media_bind_music_filter(
    DlumaPlayerMediaPlayerProfile* profile);

static const char g_dluma_player_media_player_sha256[] =
    "61D9DFB20CFA179C6D097537DA46BA9DBE3D58797938762EF4EACFF5DFCC4B3A";
static const char g_dluma_player_media_engine_sha256[] =
    "A18CA1103614947AE7FF956C6852F45123B440563A967D9E1E496C42FA574A3C";
static const char g_dluma_player_media_gamedll_sha256[] =
    "CBC7DD7923778936341CDFB755474A0C714079D910849DF16FFC5EC88A103B95";
static const char g_dluma_player_media_filesystem_sha256[] =
    "CB6FF4B0C9AA62C8A35742D463AE3C34FE803767E8E2AC9B33E4604886B9846D";
static const char g_dluma_player_media_fmod_sha256[] =
    "1BB15CC4F8062235F128822FC6F29E72A2E1EC6B6E40F41122A372FB9713BB17";

static const char g_dluma_dl_play_wave_2d_export[] =
    "?PlayWave2D@IGSObject@@QEBAHAEBV?$string_base@D@ttl@@"
    "_NMMIMHI1@Z";
static const char g_dluma_dl_stop_wave_export[] =
    "?StopWave@IGSObject@@QEAAXHM@Z";
static const char g_dluma_dl_is_wave_playing_export[] =
    "?IsWavePlaying@IGSObject@@QEAA_NH@Z";
static const char g_dluma_dl_set_wave_position_seconds_export[] =
    "?SetPlayingWavePos@IGSObject@@QEAA_NHM@Z";
static const char g_dluma_dl_set_wave_volume_export[] =
    "?SetWaveVolume@IGSObject@@QEAA_NHM@Z";
static const char g_dluma_dl_get_wave_time_seconds_export[] =
    "?GetPlayingWaveTimePos@IGSObject@@QEAAMH@Z";
static const char g_dluma_dl_get_wave_duration_name_export[] =
    "?GetWaveDuration@IGSObject@@QEBAMAEBV?$string_base@D@ttl@@@Z";
static const char g_dluma_dl_get_object_level_export[] =
    "?GetLevel@IGSObject@@QEBAPEAVILevel@@XZ";

static const BYTE g_dluma_dl_load_wave_bank_prefix[] = {
    0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x6C,
    0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x48
};
static const BYTE g_dluma_dl_play_wave_2d_prefix[] = {
    0x48,0x8B,0xC4,0x48,0x89,0x58,0x08,0x48,
    0x89,0x70,0x10,0x48,0x89,0x78,0x18,0x55
};
static const BYTE g_dluma_dl_stop_wave_prefix[] = {
    0x48,0x8B,0xC4,0x48,0x89,0x58,0x10,0x48,
    0x89,0x68,0x18,0x57,0x48,0x81,0xEC,0xA0
};
static const BYTE g_dluma_dl_is_wave_playing_prefix[] = {
    0x48,0x83,0xEC,0x48,0x48,0x8B,0x0D,0x3D,
    0xB3,0x84,0x00,0x48,0x85,0xC9,0x75,0x07
};
static const BYTE g_dluma_dl_set_wave_position_seconds_prefix[] = {
    0x48,0x8B,0xC4,0x48,0x89,0x58,0x10,0x48,
    0x89,0x68,0x18,0x57,0x48,0x81,0xEC,0xB0
};
static const BYTE g_dluma_dl_set_wave_volume_prefix[] = {
    0x48,0x8B,0xC4,0x48,0x89,0x58,0x10,0x48,
    0x89,0x68,0x18,0x57,0x48,0x81,0xEC,0xB0
};
static const BYTE g_dluma_dl_get_wave_time_seconds_prefix[] = {
    0x48,0x83,0xEC,0x58,0x48,0x8B,0x0D,0x2D,
    0x7D,0x81,0x00,0x48,0x85,0xC9,0x75,0x08
};
static const BYTE g_dluma_dl_get_wave_duration_name_prefix[] = {
    0x48,0x8B,0x02,0x48,0x8B,0x0D,0x2E,0xA6,
    0x84,0x00,0x48,0x8D,0x15,0xF0,0x8B,0x5E
};
static const BYTE g_dluma_dl_get_object_level_prefix[] = {
    0x48,0x8B,0x41,0x20,0x48,0x8B,0x40,0x48,
    0x48,0x85,0xC0,0x74,0x08,0x48,0x8B,0x80
};
static const BYTE g_dluma_dl_session_level_prefix[] = {
    0x48,0x8B,0x81,0xB0,0x00,0x00,0x00,0xC3
};
static const BYTE g_dluma_dl_driving_condition_prefix[] = {
    0x48,0x83,0xEC,0x28,0x48,0x8B,0x05,0x8D,
    0x8A,0x98,0x01,0x48,0x8B,0x80,0x08,0x05,
    0x00,0x00,0x48,0x85,0xC0,0x74,0x2C,0x48,
    0x8B,0x80,0xB8,0x00,0x00,0x00,0x48,0x85
};
static const BYTE g_dluma_dl_music_manager_ctor_prefix[] = {
    0x40,0x53,0x48,0x83,0xEC,0x20,0x48,0x8B,
    0xD9,0xFF,0x15,0xC9,0x82,0xA5,0x00,0x33
};
static const BYTE g_dluma_dl_music_manager_stop_prefix[] = {
    0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x74,
    0x24,0x10,0x57,0x48,0x83,0xEC,0x20,0x48
};
static const BYTE g_dluma_dl_music_manager_start_prefix[] = {
    0x40,0x53,0x48,0x83,0xEC,0x30,0x0F,0x29,
    0x74,0x24,0x20,0x0F,0x28,0xF1,0x48,0x8B
};
static const BYTE g_dluma_dl_music_manager_delayed_start_prefix[] = {
    0x80,0xA1,0xC9,0x00,0x00,0x00,0xDF,0xC7,
    0x81,0xC4,0x00,0x00,0x00,0x00,0x00,0x00
};
static const BYTE g_dluma_dl_local_buggy_prefix[] = {
    0x40,0x53,0x48,0x83,0xEC,0x30,0x48,0x8B,
    0x05,0x63,0x4D,0x85,0x01,0x48,0x85,0xC0,
    0x74,0x4A,0x48,0x8B,0x98,0x80,0x07,0x00,
    0x00,0x48,0x85,0xDB,0x74,0x3E,0x48,0x8B
};
static const BYTE g_dluma_dl_actor_is_driver_prefix[] = {
    0x40,0x53,0x48,0x83,0xEC,0x20,0x48,0x8B,
    0x41,0x28,0x48,0x8B,0xD9,0x48,0x83,0xC1,
    0x28,0xFF,0x90,0x90,0x06,0x00,0x00,0x84,
    0xC0,0x75,0x2F,0x48,0x83,0xBB,0x58,0x07
};
static const BYTE g_dluma_dl_frontend_musics_manager_mute_prefix[] = {
    0x40,0x53,0x48,0x83,0xEC,0x60,0x0F,0x29,
    0x7C,0x24,0x40,0x48,0x89,0x74,0x24,0x70,
    0x33,0xF6,0x0F,0x28,0xF9,0x48,0x8B,0xD9
};
static const BYTE g_dluma_dl_frontend_musics_manager_unmute_prefix[] = {
    0x40,0x57,0x48,0x83,0xEC,0x50,0x80,0xB9,
    0xB0,0x00,0x00,0x00,0x00,0x48,0x8B,0xF9,
    0x0F,0x84,0xA8,0x00,0x00,0x00,0x83,0x79
};
static const BYTE g_dluma_dl_main_menu_module_play_music_prefix[] = {
    0x48,0x89,0x6C,0x24,0x18,0x48,0x89,0x74,
    0x24,0x20,0x57,0x48,0x81,0xEC,0x90,0x00
};
static const BYTE g_dluma_dl_main_menu_module_stop_music_prefix[] = {
    0x48,0x89,0x5C,0x24,0x08,0x57,0x48,0x83,
    0xEC,0x20,0x48,0x8B,0xD9,0x48,0x8D,0x15
};
static const BYTE g_dluma_dl_playerdi_current_vehicle_prefix[] = {
    0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x74,
    0x24,0x10,0x57,0x48,0x83,0xEC,0x20,0x48,
    0x8B,0xB9,0x00,0x0C,0x00,0x00,0x33,0xDB,
    0x48,0x8B,0xF1,0x48,0x85,0xFF,0x74,0x79
};
static const BYTE g_dluma_dl_playerdi_local_predicate_prefix[] = {
    0xB0,0x01,0xC3
};
static const BYTE g_dluma_dl_vehicle_get_driver_prefix[] = {
    0x48,0x8B,0x81,0xD0,0x01,0x00,0x00,0x33,
    0xD2,0x8B,0xCA,0x48,0x85,0xC0,0x74,0x3D,
    0x39,0x50,0xF0,0x7D,0x06,0x48,0x8B,0x40,
    0x10,0xEB,0x06,0x48,0x8B,0xC8,0x48,0x8B,
    0x00,0x48,0x85,0xC0,0x75,0xEA,0x48,0x85,
    0xC9,0x74,0x22,0x39,0x51,0xF0,0x7F,0x1D,
    0x48,0x8B,0x41,0xF8,0x48,0x85,0xC0,0x74,
    0x14,0x48,0x39,0x90,0x48,0x03,0x00,0x00
};
static const BYTE g_dluma_dl_car_buggy_linear_velocity[] = {
    0x40,0x53,0x48,0x83,0xEC,0x20,0x48,0x83,
    0xC1,0x18,0x48,0x8B,0xDA,0xFF,0x15,0xED,
    0xA1,0x56,0x00,0x48,0x8B,0xC8,0x4C,0x8B,
    0x00,0x41,0xFF,0x90,0xA0,0x00,0x00,0x00,
    0x48,0x85,0xC0,0x74,0x22,0x4C,0x8B,0x00,
    0x33,0xD2,0x48,0x8B,0xC8,0x41,0xFF,0x90,
    0x10,0x01,0x00,0x00,0x48,0x85,0xC0,0x74,
    0x0E,0x48,0x8B,0xC8,0xFF,0x15,0xFE,0xA5,
    0x56,0x00,0x4C,0x8B,0xD8,0xEB,0x07,0x4C,
    0x8D,0x1D,0x82,0x51,0xEA,0x00,0x41,0x8B,
    0x03,0x89,0x03,0x41,0x8B,0x43,0x04,0x89,
    0x43,0x04,0x41,0x8B,0x43,0x08,0x89,0x43,
    0x08,0x48,0x8B,0xC3,0x48,0x83,0xC4,0x20,
    0x5B,0xC3
};
static const BYTE g_dluma_dl_rtdynamiccast_prefix[] = {
    0xFF,0x25,0x64,0x19,0x0C,0x00
};
static const BYTE g_dluma_dl_leveldi_music_create_evidence_prefix[] = {
    0x4C,0x39,0xAF,0xD0,0x09,0x00,0x00,0x75,
    0x1E,0x48,0x8D,0x4F,0x10
};
static const BYTE g_dluma_dl_leveldi_music_store_prefix[] = {
    0x48,0x89,0x87,0xD0,0x09,0x00,0x00,0x48,
    0x8B,0x8F,0xD0,0x09,0x00,0x00
};
static const BYTE g_dluma_dl_physics_bind_prefix[] = {
    0x48,0x8B,0x41,0x08,0x48,0x05,0x58,0x02,0x00,0x00,0xC3
};
static const BYTE g_dluma_dl_physics_interface_prefix[] = {
    0x48,0x83,0xEC,0x28,0x48,0x8B,0x49,0x08,
    0x48,0x8B,0x01,0xFF,0x90,0x38,0x06,0x00,
    0x00,0x48,0x85,0xC0,0x74,0x0E,0x48,0x8B,
    0x10,0x48,0x8B,0xC8,0x48,0x83,0xC4,0x28
};
static const BYTE g_dluma_dl_physics_body_at_prefix[] = {
    0x48,0x8B,0x41,0x08,0x48,0x8B,0x80,0x78,
    0x02,0x00,0x00,0x48,0x85,0xC0,0x74,0x0F,
    0x3B,0x50,0x08,0x73,0x0A,0x48,0x8B,0x00,
    0x8B,0xCA,0x48,0x8B,0x04,0xC8,0xC3,0x33,
    0xC0,0xC3
};
static const BYTE g_dluma_dl_linear_velocity_prefix[] = {
    0x48,0x8D,0x81,0xD8,0x00,0x00,0x00,0xC3
};
static const BYTE g_dluma_dl_audio_try_lock_prefix[] = {
    0x48,0x83,0xEC,0x28,0x48,0x8D,0x0D,0x05,
    0x04,0x9E,0x00,0xFF,0x15,0x0F,0x14,0x76,
    0x00,0x85,0xC0,0x0F,0x95,0xC0,0x48,0x83,
    0xC4,0x28,0xC3
};
static const BYTE g_dluma_dl_audio_unlock_prefix[] = {
    0x48,0x8D,0x0D,0x19,0x04,0x9E,0x00,0x48,
    0xFF,0x25,0x82,0x14,0x76,0x00
};
static const BYTE g_dluma_dl_audio_update_prefix[] = {
    0x40,0x55,0x53,0x56,0x41,0x56,0x48,0x8D,
    0x6C,0x24,0xC1,0x48,0x81,0xEC,0xD8,0x00,
    0x00,0x00,0x48,0x8B,0xF1,0x48,0x8D,0x4D,
    0x77,0x44,0x0F,0x29,0x54,0x24,0x60,0xFF
};
static const BYTE g_dluma_dl_audio_set_filter_prefix[] = {
    0x48,0x8B,0xC4,0x48,0x89,0x58,0x08,0x48,
    0x89,0x70,0x10,0x57,0x48,0x81,0xEC,0x90,
    0x00,0x00,0x00,0x0F,0x29,0x70,0xE8,0x0F,
    0x28,0xF3,0x0F,0x29,0x78,0xD8,0x41,0x8B
};
static const BYTE g_dluma_dl_fmod_get_type_prefix[] = {
    0x40,0x53,0x48,0x83,0xEC,0x20,0x48,0x8B,
    0xDA,0x48,0x8D,0x54,0x24,0x40,0xE8,0xFD,
    0x02,0xFD,0xFF,0x85,0xC0,0x75,0x11,0x48,
    0x8B,0x4C,0x24,0x40,0x48,0x8B,0xD3,0x48
};
static const BYTE g_dluma_dl_fmod_get_bypass_prefix[] = {
    0x40,0x53,0x48,0x83,0xEC,0x20,0x48,0x8B,
    0xDA,0x48,0x8D,0x54,0x24,0x40,0xE8,0x0D,
    0x06,0xFD,0xFF,0x85,0xC0,0x75,0x16,0x48,
    0x8B,0x44,0x24,0x40,0x0F,0xB6,0x88,0xC0
};
static const BYTE g_dluma_dl_fmod_get_parameter_prefix[] = {
    0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x74,
    0x24,0x10,0x57,0x48,0x83,0xEC,0x40,0x8B,
    0xF2,0x48,0x8D,0x54,0x24,0x30,0x49,0x8B,
    0xD9,0x49,0x8B,0xF8,0xE8,0xAF,0x04,0xFD
};
static const BYTE g_dluma_dl_fmod_set_bypass_prefix[] = {
    0x40,0x53,0x48,0x83,0xEC,0x20,0x0F,0xB6,
    0xDA,0x48,0x8D,0x54,0x24,0x40,0xE8,0x4D,
    0x06,0xFD,0xFF,0x85,0xC0,0x75,0x21,0x48,
    0x8B,0x44,0x24,0x40,0x84,0xDB,0x74,0x0F
};
static const BYTE g_dluma_dl_fmod_set_parameter_prefix[] = {
    0x40,0x53,0x48,0x83,0xEC,0x30,0x8B,0xDA,
    0x48,0x8D,0x54,0x24,0x58,0x0F,0x29,0x74,
    0x24,0x20,0x0F,0x28,0xF2,0xE8,0xF6,0x04,
    0xFD,0xFF,0x85,0xC0,0x75,0x10,0x48,0x8B
};

static int dluma_player_media_module_sha256(
    HMODULE module, ULONGLONG max_bytes, const char* expected)
{
    WCHAR path[DLUMA_MAX_WPATH];
    char actual[DLUMA_SHA256_HEX_CAPACITY];
    return module && expected &&
           dluma_get_module_path_checked(
               module, path, DLUMA_ARRAY_COUNT(path)) &&
           dluma_sha256_file_hex(path, max_bytes, actual) &&
           dluma_stricmp(actual, expected) == 0;
}

static int dluma_player_media_pe_identity(
    HMODULE module, DWORD* timestamp_out, DWORD* image_size_out)
{
    BYTE* base = (BYTE*)module;
    DWORD pe_offset;
    BYTE* nt;
    if (!base || !timestamp_out || !image_size_out ||
        !dluma_is_readable(base, 0x100U) ||
        base[0] != 'M' || base[1] != 'Z') return 0;
    pe_offset = *(DWORD*)(base + 0x3CU);
    if (pe_offset < 0x40U || pe_offset > 0x10000000UL) return 0;
    nt = base + pe_offset;
    if (!dluma_is_readable(nt, 0x100U) ||
        *(DWORD*)nt != 0x00004550UL ||
        *(WORD*)(nt + 4U) != 0x8664U ||
        *(WORD*)(nt + 24U) != 0x020BU) return 0;
    *timestamp_out = *(DWORD*)(nt + 8U);
    *image_size_out = *(DWORD*)(nt + 24U + 56U);
    return *image_size_out >= 0x1000U;
}

static int dluma_player_media_module_range(
    HMODULE module, DWORD image_size, const void* address, SIZE_T size)
{
    ULONG_PTR base = (ULONG_PTR)module;
    ULONG_PTR value = (ULONG_PTR)address;
    SIZE_T offset;
    if (!module || !image_size || !address || !size || value < base)
        return 0;
    offset = (SIZE_T)(value - base);
    return offset <= (SIZE_T)image_size &&
           size <= (SIZE_T)image_size - offset;
}

static int dluma_player_media_readonly_range(
    HMODULE module, DWORD image_size, const void* address, SIZE_T size)
{
    const BYTE* cursor = (const BYTE*)address;
    SIZE_T remaining = size;
    if (!dluma_player_media_module_range(
            module, image_size, address, size) ||
        !dluma_is_readable(address, size)) return 0;
    while (remaining) {
        MEMORY_BASIC_INFORMATION info;
        SIZE_T offset;
        SIZE_T available;
        DWORD protection;
        if (VirtualQuery(cursor, &info, sizeof(info)) != sizeof(info) ||
            info.State != MEM_COMMIT ||
            (info.Protect & (PAGE_GUARD | PAGE_NOACCESS))) return 0;
        protection = info.Protect & 0xFFU;
        if (protection != PAGE_READONLY &&
            protection != PAGE_EXECUTE_READ) return 0;
        offset = (SIZE_T)(cursor - (const BYTE*)info.BaseAddress);
        if (offset >= info.RegionSize) return 0;
        available = info.RegionSize - offset;
        if (available >= remaining) return 1;
        cursor += available;
        remaining -= available;
    }
    return 1;
}

static int dluma_player_media_is_executable(
    HMODULE module, DWORD image_size, const void* address, SIZE_T size)
{
    MEMORY_BASIC_INFORMATION info;
    DWORD protection;
    SIZE_T offset;
    if (!dluma_player_media_module_range(
            module, image_size, address, size) ||
        VirtualQuery(address, &info, sizeof(info)) != sizeof(info) ||
        info.State != MEM_COMMIT ||
        (info.Protect & (PAGE_GUARD | PAGE_NOACCESS))) return 0;
    offset = (SIZE_T)((const BYTE*)address - (const BYTE*)info.BaseAddress);
    if (offset >= info.RegionSize || size > info.RegionSize - offset) return 0;
    protection = info.Protect & 0xFFU;
    return protection == PAGE_EXECUTE ||
           protection == PAGE_EXECUTE_READ ||
           protection == PAGE_EXECUTE_READWRITE ||
           protection == PAGE_EXECUTE_WRITECOPY;
}

static int dluma_player_media_prefix_matches(
    HMODULE module, DWORD image_size, DWORD rva,
    const BYTE* prefix, unsigned int prefix_size)
{
    const BYTE* address = (const BYTE*)module + rva;
    return prefix && prefix_size &&
           dluma_player_media_is_executable(
               module, image_size, address, prefix_size) &&
           memcmp(address, prefix, prefix_size) == 0;
}

static int dluma_player_media_relative_call_matches(
    HMODULE module, DWORD image_size, DWORD call_rva, DWORD target_rva)
{
    BYTE* call = (BYTE*)module + call_rva;
    LONG displacement;
    BYTE* target;
    if (!dluma_player_media_is_executable(
            module, image_size, call, 5U) || call[0] != 0xE8U)
        return 0;
    memcpy(&displacement, call + 1U, sizeof(displacement));
    target = call + 5U + displacement;
    return target == (BYTE*)module + target_rva;
}

static int dluma_player_media_rip_relative_matches(
    HMODULE module, DWORD image_size, DWORD instruction_rva,
    unsigned int displacement_offset, unsigned int instruction_size,
    DWORD target_rva)
{
    BYTE* instruction = (BYTE*)module + instruction_rva;
    LONG displacement;
    BYTE* target;
    if (displacement_offset > instruction_size ||
        sizeof(displacement) > instruction_size - displacement_offset ||
        !dluma_player_media_is_executable(
            module, image_size, instruction, instruction_size))
        return 0;
    memcpy(
        &displacement, instruction + displacement_offset,
        sizeof(displacement));
    target = instruction + instruction_size + displacement;
    return target == (BYTE*)module + target_rva;
}

static int dluma_player_media_exact_text(
    HMODULE module, DWORD image_size, DWORD rva, const char* expected)
{
    SIZE_T length;
    const char* text;
    if (!expected) return 0;
    length = strlen(expected) + 1U;
    text = (const char*)module + rva;
    /*
     * MSVC x64 type descriptors are emitted in the image's writable .data
     * section.  The exact loaded bytes still have to remain inside the
     * fingerprinted module and be readable, but requiring PAGE_READONLY here
     * rejects valid RTTI on the supported Player build.
     */
    return dluma_player_media_module_range(
               module, image_size, text, length) &&
           dluma_is_readable(text, length) &&
           memcmp(text, expected, length) == 0;
}

static int dluma_player_media_validate_rtti_vtable(
    HMODULE module, DWORD image_size, DWORD vtable_rva,
    DWORD expected_col_rva, DWORD expected_offset,
    DWORD expected_type_rva, const char* expected_type_name)
{
    void** vtable = (void**)((BYTE*)module + vtable_rva);
    DlumaPlayerMediaCompleteObjectLocator* col;
    DlumaPlayerMediaClassHierarchyDescriptor* hierarchy;
    if (!dluma_player_media_readonly_range(
            module, image_size, vtable - 1, sizeof(void*) * 2U))
        return 0;
    col = (DlumaPlayerMediaCompleteObjectLocator*)vtable[-1];
    if (!dluma_player_media_readonly_range(
            module, image_size, col, sizeof(*col)) ||
        (expected_col_rva &&
         col != (DlumaPlayerMediaCompleteObjectLocator*)((BYTE*)module +
                                                        expected_col_rva)) ||
        col->signature != 1U || col->offset != expected_offset ||
        col->cd_offset != 0U ||
        col->type_descriptor_rva != expected_type_rva ||
        col->self_rva != (DWORD)((BYTE*)col - (BYTE*)module) ||
        !dluma_player_media_exact_text(
            module, image_size, expected_type_rva + 16U,
            expected_type_name)) return 0;
    hierarchy = (DlumaPlayerMediaClassHierarchyDescriptor*)(
        (BYTE*)module + col->class_hierarchy_rva);
    return dluma_player_media_readonly_range(
               module, image_size, hierarchy, sizeof(*hierarchy)) &&
           hierarchy->base_count > 0U && hierarchy->base_count <= 256U;
}

static int dluma_player_media_base_descriptor_matches(
    HMODULE module, DWORD image_size, DWORD descriptor_rva,
    DWORD expected_type_rva, const char* expected_type_name,
    DWORD expected_contained_bases, DWORD expected_hierarchy_rva)
{
    DlumaPlayerMediaBaseClassDescriptor* descriptor;
    descriptor = (DlumaPlayerMediaBaseClassDescriptor*)(
        (BYTE*)module + descriptor_rva);
    return dluma_player_media_readonly_range(
               module, image_size, descriptor, sizeof(*descriptor)) &&
           descriptor->type_descriptor_rva == expected_type_rva &&
           descriptor->contained_base_count == expected_contained_bases &&
           descriptor->member_displacement == 0 &&
           descriptor->vbtable_displacement == -1 &&
           descriptor->displacement_inside_vbtable == 0 &&
           descriptor->attributes == 0x40U &&
           descriptor->class_hierarchy_rva == expected_hierarchy_rva &&
           dluma_player_media_exact_text(
               module, image_size, expected_type_rva + 16U,
               expected_type_name);
}

static int dluma_player_media_hierarchy_matches(
    HMODULE module, DWORD image_size, DWORD hierarchy_rva,
    DWORD expected_attributes, DWORD expected_base_count,
    DWORD expected_base_array_rva)
{
    DlumaPlayerMediaClassHierarchyDescriptor* hierarchy =
        (DlumaPlayerMediaClassHierarchyDescriptor*)(
            (BYTE*)module + hierarchy_rva);
    return expected_base_count > 0U &&
           expected_base_count <= 256U &&
           dluma_player_media_readonly_range(
               module, image_size, hierarchy, sizeof(*hierarchy)) &&
           hierarchy->signature == 0U &&
           hierarchy->attributes == expected_attributes &&
           hierarchy->base_count == expected_base_count &&
           hierarchy->base_array_rva == expected_base_array_rva &&
           dluma_player_media_readonly_range(
               module, image_size,
               (BYTE*)module + expected_base_array_rva,
               (SIZE_T)expected_base_count * sizeof(DWORD));
}

static int dluma_player_media_validate_session_profile_metadata(
    HMODULE module, DWORD image_size,
    const DlumaPlayerMediaSessionClassProfile* session_profile)
{
    DlumaPlayerMediaCompleteObjectLocator* col;
    DlumaPlayerMediaClassHierarchyDescriptor* hierarchy;
    DWORD* base_array;
    void** vtable;
    DWORD index;
    if (!module || !session_profile ||
        !session_profile->base_count ||
        session_profile->base_count > 256U ||
        !session_profile->base_prefix_count ||
        session_profile->base_prefix_count >
            DLUMA_ARRAY_COUNT(session_profile->base_prefix) ||
        session_profile->base_prefix_count >
            session_profile->base_count ||
        session_profile->sessiondi_base_index >=
            session_profile->base_count ||
        !dluma_player_media_validate_rtti_vtable(
            module, image_size,
            session_profile->vtable_rva,
            session_profile->col_rva, 0U,
            session_profile->type_rva,
            session_profile->type_name))
        return 0;
    vtable = (void**)((BYTE*)module +
                      session_profile->vtable_rva);
    col = (DlumaPlayerMediaCompleteObjectLocator*)vtable[-1];
    if (col->class_hierarchy_rva !=
            session_profile->hierarchy_rva ||
        !dluma_player_media_is_executable(
            module, image_size, vtable[0], 1U))
        return 0;
    hierarchy = (DlumaPlayerMediaClassHierarchyDescriptor*)(
        (BYTE*)module + session_profile->hierarchy_rva);
    if (!dluma_player_media_readonly_range(
            module, image_size, hierarchy, sizeof(*hierarchy)) ||
        hierarchy->signature != 0U ||
        hierarchy->attributes != 1U ||
        hierarchy->base_count != session_profile->base_count ||
        hierarchy->base_array_rva !=
            session_profile->base_array_rva)
        return 0;
    base_array = (DWORD*)((BYTE*)module +
                          session_profile->base_array_rva);
    if (!dluma_player_media_readonly_range(
            module, image_size, base_array,
            (SIZE_T)session_profile->base_count * sizeof(*base_array)))
        return 0;
    for (index = 0U;
         index < session_profile->base_prefix_count;
         ++index) {
        if (base_array[index] !=
            session_profile->base_prefix[index])
            return 0;
    }
    if (base_array[0] !=
            session_profile->self_base_descriptor_rva ||
        base_array[session_profile->sessiondi_base_index] !=
            DLUMA_DL_SESSIONDI_BASE_DESCRIPTOR_RVA ||
        !dluma_player_media_base_descriptor_matches(
            module, image_size,
            session_profile->self_base_descriptor_rva,
            session_profile->type_rva,
            session_profile->type_name,
            session_profile->base_count - 1U,
            session_profile->hierarchy_rva) ||
        !dluma_player_media_base_descriptor_matches(
            module, image_size,
            DLUMA_DL_SESSIONDI_BASE_DESCRIPTOR_RVA,
            DLUMA_DL_SESSIONDI_TYPE_RVA,
            ".?AVSessionDI@@",
            8U,
            DLUMA_DL_SESSIONDI_HIERARCHY_RVA))
        return 0;
    if (session_profile->session_class ==
            DLUMA_DL_SESSION_CLASS_SESSION_COOPERATIVE_DI &&
        !dluma_player_media_base_descriptor_matches(
            module, image_size,
            DLUMA_DL_SESSION_COOPERATIVE_BASE_DESCRIPTOR_RVA,
            DLUMA_DL_SESSION_COOPERATIVE_TYPE_RVA,
            ".?AVSessionCooperative@@",
            9U,
            DLUMA_DL_SESSION_COOPERATIVE_HIERARCHY_RVA))
        return 0;
    return 1;
}

static int dluma_player_media_validate_igsobject_route_metadata(
    HMODULE module, DWORD image_size,
    DWORD vtable_rva, DWORD col_rva, DWORD subobject_offset,
    DWORD class_type_rva, const char* class_type_name,
    DWORD hierarchy_rva, DWORD hierarchy_attributes,
    DWORD base_count, DWORD base_array_rva,
    DWORD igsobject_base_index, DWORD igsobject_base_descriptor_rva,
    LONG igsobject_member_displacement)
{
    DlumaPlayerMediaCompleteObjectLocator* col;
    DlumaPlayerMediaClassHierarchyDescriptor* hierarchy;
    DlumaPlayerMediaClassHierarchyDescriptor* igsobject_hierarchy;
    DlumaPlayerMediaBaseClassDescriptor* descriptor;
    DWORD* base_array;
    DWORD* igsobject_base_array;
    void** vtable;
    if (!module || !class_type_name || !base_count ||
        base_count > 256U || igsobject_base_index >= base_count ||
        !dluma_player_media_validate_rtti_vtable(
            module, image_size, vtable_rva, col_rva,
            subobject_offset, class_type_rva, class_type_name))
        return 0;
    vtable = (void**)((BYTE*)module + vtable_rva);
    col = (DlumaPlayerMediaCompleteObjectLocator*)vtable[-1];
    if (col->class_hierarchy_rva != hierarchy_rva ||
        !dluma_player_media_is_executable(
            module, image_size, vtable[0], 1U))
        return 0;
    hierarchy = (DlumaPlayerMediaClassHierarchyDescriptor*)(
        (BYTE*)module + hierarchy_rva);
    if (!dluma_player_media_readonly_range(
            module, image_size, hierarchy, sizeof(*hierarchy)) ||
        hierarchy->signature != 0U ||
        hierarchy->attributes != hierarchy_attributes ||
        hierarchy->base_count != base_count ||
        hierarchy->base_array_rva != base_array_rva)
        return 0;
    base_array = (DWORD*)((BYTE*)module + base_array_rva);
    if (!dluma_player_media_readonly_range(
            module, image_size, base_array,
            (SIZE_T)base_count * sizeof(*base_array)) ||
        base_array[igsobject_base_index] !=
            igsobject_base_descriptor_rva)
        return 0;
    descriptor = (DlumaPlayerMediaBaseClassDescriptor*)(
        (BYTE*)module + igsobject_base_descriptor_rva);
    if (!dluma_player_media_readonly_range(
            module, image_size, descriptor, sizeof(*descriptor)) ||
        descriptor->type_descriptor_rva !=
            DLUMA_DL_IGSOBJECT_TYPE_RVA ||
        descriptor->contained_base_count != 4U ||
        descriptor->member_displacement !=
            igsobject_member_displacement ||
        descriptor->vbtable_displacement != -1 ||
        descriptor->displacement_inside_vbtable != 0 ||
        descriptor->attributes != 0x40U ||
        descriptor->class_hierarchy_rva !=
            DLUMA_DL_IGSOBJECT_HIERARCHY_RVA ||
        !dluma_player_media_exact_text(
            module, image_size,
            DLUMA_DL_IGSOBJECT_TYPE_RVA + 16U,
            ".?AVIGSObject@@"))
        return 0;
    igsobject_hierarchy =
        (DlumaPlayerMediaClassHierarchyDescriptor*)(
            (BYTE*)module + DLUMA_DL_IGSOBJECT_HIERARCHY_RVA);
    if (!dluma_player_media_readonly_range(
            module, image_size, igsobject_hierarchy,
            sizeof(*igsobject_hierarchy)) ||
        igsobject_hierarchy->signature != 0U ||
        igsobject_hierarchy->attributes != 1U ||
        igsobject_hierarchy->base_count != 5U ||
        igsobject_hierarchy->base_array_rva !=
            DLUMA_DL_IGSOBJECT_BASE_ARRAY_RVA)
        return 0;
    igsobject_base_array = (DWORD*)(
        (BYTE*)module + DLUMA_DL_IGSOBJECT_BASE_ARRAY_RVA);
    return dluma_player_media_readonly_range(
               module, image_size, igsobject_base_array,
               5U * sizeof(*igsobject_base_array)) &&
           igsobject_base_array[0] ==
               DLUMA_DL_MUSIC_MANAGER_IGSOBJECT_BASE_DESCRIPTOR_RVA;
}

static int dluma_player_media_validate_live_rtti_object(
    HMODULE module, DWORD image_size, void* object,
    DWORD expected_type_rva, const char* expected_type_name)
{
    void** vtable;
    DlumaPlayerMediaCompleteObjectLocator* col;
    DlumaPlayerMediaClassHierarchyDescriptor* hierarchy;
    if (!object || !dluma_is_readable(object, sizeof(void*)))
        return 0;
    vtable = *(void***)object;
    if (!dluma_player_media_readonly_range(
            module, image_size, vtable - 1, sizeof(void*) * 2U))
        return 0;
    col = (DlumaPlayerMediaCompleteObjectLocator*)vtable[-1];
    if (!dluma_player_media_readonly_range(
            module, image_size, col, sizeof(*col)) ||
        col->signature != 1U || col->offset != 0U ||
        col->cd_offset != 0U ||
        col->type_descriptor_rva != expected_type_rva ||
        col->self_rva != (DWORD)((BYTE*)col - (BYTE*)module) ||
        !dluma_player_media_exact_text(
            module, image_size, expected_type_rva + 16U,
            expected_type_name))
        return 0;
    hierarchy = (DlumaPlayerMediaClassHierarchyDescriptor*)(
        (BYTE*)module + col->class_hierarchy_rva);
    return dluma_player_media_readonly_range(
               module, image_size, hierarchy, sizeof(*hierarchy)) &&
           hierarchy->base_count > 0U && hierarchy->base_count <= 256U &&
           dluma_player_media_is_executable(
               module, image_size, vtable[0], 1U);
}

static int dluma_player_media_live_vtable_in_module(
    void* object, HMODULE module, DWORD image_size,
    DWORD required_slot, DWORD expected_target_rva)
{
    void** vtable;
    void* target;
    if (!object || !dluma_is_readable(object, sizeof(void*))) return 0;
    vtable = *(void***)object;
    if (!dluma_player_media_readonly_range(
            module, image_size, vtable,
            (SIZE_T)required_slot + sizeof(void*))) return 0;
    target = *(void**)((BYTE*)vtable + required_slot);
    if (expected_target_rva)
        return target == (BYTE*)module + expected_target_rva &&
               dluma_player_media_is_executable(
                   module, image_size, target, 1U);
    return dluma_player_media_is_executable(
        module, image_size, target, 1U);
}

static int dluma_player_media_leveldi_igsobject(
    DlumaPlayerMediaPlayerProfile* profile, void* level_di,
    void** igsobject_out)
{
    void* igsobject;
    if (igsobject_out) *igsobject_out = 0;
    if (!profile || !level_di || !igsobject_out ||
        !dluma_is_readable(
            level_di,
            DLUMA_DL_LEVELDI_IGSOBJECT_OFFSET + sizeof(void*)))
        return 0;
    igsobject = (BYTE*)level_di +
                DLUMA_DL_LEVELDI_IGSOBJECT_OFFSET;
    if (*(void**)igsobject !=
            (BYTE*)profile->gamedll +
                DLUMA_DL_LEVELDI_IGSOBJECT_VTABLE_RVA ||
        !dluma_player_media_live_vtable_in_module(
            igsobject, profile->gamedll, profile->gamedll_size,
            0U, 0U))
        return 0;
    *igsobject_out = igsobject;
    return 1;
}

static int dluma_player_media_float_finite(float value)
{
    DWORD bits;
    memcpy(&bits, &value, sizeof(bits));
    return (bits & 0x7F800000UL) != 0x7F800000UL;
}

static int dluma_player_media_double_finite(double value)
{
    ULONGLONG bits;
    memcpy(&bits, &value, sizeof(bits));
    return (bits & 0x7FF0000000000000ULL) !=
           0x7FF0000000000000ULL;
}

static int dluma_player_media_float_equal(float first, float second)
{
    DWORD first_bits;
    DWORD second_bits;
    memcpy(&first_bits, &first, sizeof(first_bits));
    memcpy(&second_bits, &second, sizeof(second_bits));
    return first_bits == second_bits;
}

static double dluma_player_media_vector_length(
    float x, float y, float z)
{
    DWORD x_bits;
    DWORD y_bits;
    DWORD z_bits;
    float maximum;
    double squared;
    double low;
    double high;
    unsigned int iteration;
    memcpy(&x_bits, &x, sizeof(x_bits));
    memcpy(&y_bits, &y, sizeof(y_bits));
    memcpy(&z_bits, &z, sizeof(z_bits));
    x_bits &= 0x7FFFFFFFUL;
    y_bits &= 0x7FFFFFFFUL;
    z_bits &= 0x7FFFFFFFUL;
    memcpy(&x, &x_bits, sizeof(x));
    memcpy(&y, &y_bits, sizeof(y));
    memcpy(&z, &z_bits, sizeof(z));
    maximum = x;
    if (y > maximum) maximum = y;
    if (z > maximum) maximum = z;
    if (maximum == 0.0f) return 0.0;
    squared = (double)x * (double)x +
              (double)y * (double)y +
              (double)z * (double)z;
    low = 0.0;
    high = (double)maximum * 2.0;
    for (iteration = 0; iteration < 64U; ++iteration) {
        double middle = (low + high) * 0.5;
        if (middle * middle < squared) low = middle;
        else high = middle;
    }
    return (low + high) * 0.5;
}

static int dluma_player_media_vtable_slot_matches(
    HMODULE module, DWORD image_size, DWORD vtable_rva,
    DWORD slot, DWORD target_rva)
{
    void** entry = (void**)((BYTE*)module + vtable_rva + slot);
    return dluma_player_media_readonly_range(
               module, image_size, entry, sizeof(*entry)) &&
           *entry == (BYTE*)module + target_rva &&
           dluma_player_media_is_executable(
               module, image_size, *entry, 1U);
}

static int dluma_player_media_import_slot_matches(
    HMODULE owner, DWORD owner_size, DWORD slot_rva,
    HMODULE target_module, DWORD target_size, DWORD target_rva)
{
    void** entry;
    void* expected;
    if (!owner || !target_module ||
        slot_rva > owner_size ||
        sizeof(*entry) > owner_size - slot_rva)
        return 0;
    entry = (void**)((BYTE*)owner + slot_rva);
    expected = (BYTE*)target_module + target_rva;
    return dluma_is_readable(entry, sizeof(*entry)) &&
           *entry == expected &&
           dluma_player_media_is_executable(
               target_module, target_size, expected, 1U);
}

static const DlumaPlayerMediaSessionClassProfile*
dluma_player_media_session_profile_for_vtable(
    HMODULE module, void** vtable)
{
    unsigned int index;
    if (!module || !vtable) return 0;
    for (index = 0U;
         index < DLUMA_ARRAY_COUNT(g_dluma_player_media_session_classes);
         ++index) {
        const DlumaPlayerMediaSessionClassProfile* session_profile =
            &g_dluma_player_media_session_classes[index];
        if (vtable == (void**)((BYTE*)module +
                               session_profile->vtable_rva))
            return session_profile;
    }
    return 0;
}

static int dluma_player_media_validate_live_session_object(
    DlumaPlayerMediaPlayerProfile* profile, void* session,
    unsigned int* session_class_out, unsigned int* reason_out)
{
    const DlumaPlayerMediaSessionClassProfile* session_profile;
    void** vtable;
    if (session_class_out)
        *session_class_out = DLUMA_DL_SESSION_CLASS_NONE;
    if (reason_out)
        *reason_out = DLUMA_DL_LIVE_GAME_SESSION_RTTI_INVALID;
    if (!profile || !session || !session_class_out)
        return 0;
    if (!dluma_is_readable(session, 0xB8U)) {
        if (reason_out)
            *reason_out =
                DLUMA_DL_LIVE_GAME_SESSION_OBJECT_UNREADABLE;
        return 0;
    }
    vtable = *(void***)session;
    session_profile =
        dluma_player_media_session_profile_for_vtable(
            profile->gamedll, vtable);
    if (!session_profile) {
        if (reason_out)
            *reason_out =
                DLUMA_DL_LIVE_GAME_SESSION_CLASS_UNSUPPORTED;
        return 0;
    }
    if (!dluma_player_media_validate_session_profile_metadata(
            profile->gamedll, profile->gamedll_size,
            session_profile) ||
        !dluma_player_media_validate_live_rtti_object(
            profile->gamedll, profile->gamedll_size,
            session, session_profile->type_rva,
            session_profile->type_name)) {
        if (reason_out)
            *reason_out =
                DLUMA_DL_LIVE_GAME_SESSION_RTTI_INVALID;
        return 0;
    }
    if (!dluma_player_media_live_vtable_in_module(
            session, profile->gamedll, profile->gamedll_size,
            DLUMA_DL_SESSION_LEVEL_SLOT,
            DLUMA_DL_SESSION_LEVEL_RVA)) {
        if (reason_out)
            *reason_out =
                DLUMA_DL_LIVE_GAME_SESSION_LEVEL_SLOT_MISMATCH;
        return 0;
    }
    *session_class_out = session_profile->session_class;
    if (reason_out) *reason_out = DLUMA_DL_LIVE_READY;
    return 1;
}

static int dluma_player_media_engine_export(
    DlumaPlayerMediaPlayerProfile* profile, const char* name,
    DWORD expected_rva, const BYTE* prefix, unsigned int prefix_size,
    void** out)
{
    FARPROC proc;
    if (out) *out = 0;
    if (!profile || !name || !out) return 0;
    proc = GetProcAddress(profile->engine, name);
    if (!proc || (BYTE*)proc != (BYTE*)profile->engine + expected_rva ||
        !dluma_player_media_prefix_matches(
            profile->engine, profile->engine_size, expected_rva,
            prefix, prefix_size))
        return 0;
    *out = (void*)proc;
    return 1;
}

static int dluma_player_media_validate_audio_object_detailed(
    DlumaPlayerMediaPlayerProfile* profile, void** audio_out,
    unsigned int* reason_out)
{
    void** singleton;
    void* audio;
    if (audio_out) *audio_out = 0;
    if (reason_out) *reason_out = DLUMA_DL_LIVE_PROFILE_INVALID;
    if (!profile || !audio_out) return 0;
    if (!dluma_player_media_module_range(
            profile->engine, profile->engine_size,
            (BYTE*)profile->engine + DLUMA_DL_AUDIO_SINGLETON_RVA,
            sizeof(void*))) {
        if (reason_out)
            *reason_out = DLUMA_DL_LIVE_SINGLETON_SLOT_OUT_OF_RANGE;
        return 0;
    }
    singleton = (void**)((BYTE*)profile->engine +
                         DLUMA_DL_AUDIO_SINGLETON_RVA);
    if (!dluma_is_readable(singleton, sizeof(*singleton))) {
        if (reason_out)
            *reason_out = DLUMA_DL_LIVE_SINGLETON_SLOT_UNREADABLE;
        return 0;
    }
    audio = *singleton;
    if (!audio) {
        if (reason_out)
            *reason_out = DLUMA_DL_LIVE_SINGLETON_UNAVAILABLE;
        return 0;
    }
    if (!dluma_is_readable(audio, 0x66A1U)) {
        if (reason_out)
            *reason_out = DLUMA_DL_LIVE_OBJECT_UNREADABLE;
        return 0;
    }
    if (*(void**)audio !=
        (BYTE*)profile->engine + DLUMA_DL_AUDIO_VTABLE_RVA) {
        if (reason_out)
            *reason_out = DLUMA_DL_LIVE_PRIMARY_VTABLE_MISMATCH;
        return 0;
    }
    if (!*(void**)((BYTE*)audio + DLUMA_DL_AUDIO_SYSTEM_OFFSET)) {
        if (reason_out)
            *reason_out = DLUMA_DL_LIVE_AUDIO_SYSTEM_UNAVAILABLE;
        return 0;
    }
    if (*(unsigned char*)((BYTE*)audio +
                          DLUMA_DL_AUDIO_INITIALIZED_OFFSET) == 0U) {
        if (reason_out)
            *reason_out = DLUMA_DL_LIVE_AUDIO_NOT_INITIALIZED;
        return 0;
    }
    if (!dluma_player_media_live_vtable_in_module(
            audio, profile->engine, profile->engine_size,
            0x28U, DLUMA_DL_LOAD_WAVE_BANK_RVA)) {
        if (reason_out)
            *reason_out = DLUMA_DL_LIVE_AUDIO_VTABLE_SLOT_MISMATCH;
        return 0;
    }
    *audio_out = audio;
    if (reason_out) *reason_out = DLUMA_DL_LIVE_READY;
    return 1;
}

static int dluma_player_media_validate_audio_object(
    DlumaPlayerMediaPlayerProfile* profile, void** audio_out)
{
    return dluma_player_media_validate_audio_object_detailed(
        profile, audio_out, 0);
}

static DWORD dluma_player_media_pointer_rva(
    HMODULE module, DWORD module_size, const void* pointer)
{
    ULONG_PTR delta;
    if (!pointer) return 0U;
    if (!dluma_player_media_module_range(
            module, module_size, pointer, 1U))
        return 0xffffffffUL;
    delta = (ULONG_PTR)((const BYTE*)pointer - (const BYTE*)module);
    if (delta > 0xffffffffULL) return 0xffffffffUL;
    return (DWORD)delta;
}

static void dluma_player_media_manager_diagnostic_reset(
    DlumaPlayerMediaManagerDiagnostic* diagnostic)
{
    if (!diagnostic) return;
    memset(diagnostic, 0, sizeof(*diagnostic));
    diagnostic->stage = DLUMA_DL_LIVE_PROFILE_INVALID;
}

static int dluma_player_media_manager_validation_failed(
    unsigned int* reason_out,
    DlumaPlayerMediaManagerDiagnostic* diagnostic,
    unsigned int reason)
{
    if (reason_out) *reason_out = reason;
    if (diagnostic) diagnostic->stage = reason;
    return 0;
}

static void dluma_player_media_manager_diagnostic_capture(
    DlumaPlayerMediaPlayerProfile* profile, void* manager,
    DlumaPlayerMediaManagerDiagnostic* diagnostic)
{
    if (!profile || !diagnostic) return;
    diagnostic->manager = manager;
    if (!manager ||
        !dluma_is_readable(manager, DLUMA_DL_MUSIC_MANAGER_MIN_SIZE))
        return;
    diagnostic->primary_vtable_rva =
        dluma_player_media_pointer_rva(
            profile->gamedll, profile->gamedll_size,
            *(void**)manager);
    diagnostic->vtable_8_rva =
        dluma_player_media_pointer_rva(
            profile->gamedll, profile->gamedll_size,
            *(void**)((BYTE*)manager + 8U));
    diagnostic->vtable_16_rva =
        dluma_player_media_pointer_rva(
            profile->gamedll, profile->gamedll_size,
            *(void**)((BYTE*)manager + 16U));
    diagnostic->vtable_48_rva =
        dluma_player_media_pointer_rva(
            profile->gamedll, profile->gamedll_size,
            *(void**)((BYTE*)manager + 48U));
}

static int dluma_player_media_manager_diagnostic_equal(
    const DlumaPlayerMediaManagerDiagnostic* left,
    const DlumaPlayerMediaManagerDiagnostic* right)
{
    return left && right &&
           left->manager == right->manager &&
           left->primary_vtable_rva == right->primary_vtable_rva &&
           left->vtable_8_rva == right->vtable_8_rva &&
           left->vtable_16_rva == right->vtable_16_rva &&
           left->vtable_48_rva == right->vtable_48_rva &&
           left->stage == right->stage;
}

static int dluma_player_media_validate_music_manager_object_detailed(
    DlumaPlayerMediaPlayerProfile* profile, void* manager,
    unsigned int* reason_out,
    DlumaPlayerMediaManagerDiagnostic* diagnostic)
{
    void** primary_vtable;
    if (diagnostic) {
        dluma_player_media_manager_diagnostic_reset(diagnostic);
        diagnostic->manager = manager;
    }
    if (reason_out) *reason_out = DLUMA_DL_LIVE_RESOLVER_ARGUMENT_INVALID;
    if (!profile || !manager)
        return dluma_player_media_manager_validation_failed(
            reason_out, diagnostic,
            DLUMA_DL_LIVE_RESOLVER_ARGUMENT_INVALID);
    dluma_player_media_manager_diagnostic_capture(
        profile, manager, diagnostic);
    if (!dluma_is_readable(manager, DLUMA_DL_MUSIC_MANAGER_MIN_SIZE)) {
        return dluma_player_media_manager_validation_failed(
            reason_out, diagnostic,
            DLUMA_DL_LIVE_MANAGER_OBJECT_UNREADABLE);
    }
    if (*(void**)manager !=
            (BYTE*)profile->gamedll +
                DLUMA_DL_MUSIC_MANAGER_VTABLE_RVA) {
        return dluma_player_media_manager_validation_failed(
            reason_out, diagnostic,
            DLUMA_DL_LIVE_MANAGER_PRIMARY_VTABLE_MISMATCH);
    }
    if (*(void**)((BYTE*)manager + 8U) !=
            (BYTE*)profile->gamedll +
                DLUMA_DL_MUSIC_MANAGER_VTABLE_8_RVA) {
        return dluma_player_media_manager_validation_failed(
            reason_out, diagnostic,
            DLUMA_DL_LIVE_MANAGER_SECONDARY_VTABLE_8_MISMATCH);
    }
    if (*(void**)((BYTE*)manager + 16U) !=
            (BYTE*)profile->gamedll +
                DLUMA_DL_MUSIC_MANAGER_VTABLE_16_RVA) {
        return dluma_player_media_manager_validation_failed(
            reason_out, diagnostic,
            DLUMA_DL_LIVE_MANAGER_SECONDARY_VTABLE_16_MISMATCH);
    }
    if (*(void**)((BYTE*)manager + 48U) !=
            (BYTE*)profile->gamedll +
                DLUMA_DL_MUSIC_MANAGER_VTABLE_48_RVA) {
        return dluma_player_media_manager_validation_failed(
            reason_out, diagnostic,
            DLUMA_DL_LIVE_MANAGER_SECONDARY_VTABLE_48_MISMATCH);
    }
    primary_vtable = *(void***)manager;
    if (!dluma_player_media_readonly_range(
            profile->gamedll, profile->gamedll_size,
            primary_vtable, sizeof(void*) * 2U) ||
        !dluma_player_media_is_executable(
            profile->gamedll, profile->gamedll_size,
            primary_vtable[0], 1U)) {
        return dluma_player_media_manager_validation_failed(
            reason_out, diagnostic,
            DLUMA_DL_LIVE_MANAGER_PRIMARY_TARGET_INVALID);
    }
    if (reason_out) *reason_out = DLUMA_DL_LIVE_READY;
    if (diagnostic) diagnostic->stage = DLUMA_DL_LIVE_READY;
    return 1;
}

static int
dluma_player_media_validate_frontend_musics_manager_object_detailed(
    DlumaPlayerMediaPlayerProfile* profile, void* manager,
    unsigned int* reason_out,
    DlumaPlayerMediaManagerDiagnostic* diagnostic)
{
    void** primary_vtable;
    if (diagnostic) {
        dluma_player_media_manager_diagnostic_reset(diagnostic);
        diagnostic->manager = manager;
    }
    if (reason_out) *reason_out = DLUMA_DL_LIVE_RESOLVER_ARGUMENT_INVALID;
    if (!profile || !manager)
        return dluma_player_media_manager_validation_failed(
            reason_out, diagnostic,
            DLUMA_DL_LIVE_RESOLVER_ARGUMENT_INVALID);
    dluma_player_media_manager_diagnostic_capture(
        profile, manager, diagnostic);
    if (!dluma_is_readable(
            manager, DLUMA_DL_FRONTEND_MUSICS_MANAGER_MIN_SIZE))
        return dluma_player_media_manager_validation_failed(
            reason_out, diagnostic,
            DLUMA_DL_LIVE_MANAGER_OBJECT_UNREADABLE);
    if (*(void**)manager !=
            (BYTE*)profile->gamedll +
                DLUMA_DL_FRONTEND_MUSICS_MANAGER_VTABLE_RVA)
        return dluma_player_media_manager_validation_failed(
            reason_out, diagnostic,
            DLUMA_DL_LIVE_MANAGER_PRIMARY_VTABLE_MISMATCH);
    if (*(void**)((BYTE*)manager + 8U) !=
            (BYTE*)profile->gamedll +
                DLUMA_DL_FRONTEND_MUSICS_MANAGER_VTABLE_8_RVA)
        return dluma_player_media_manager_validation_failed(
            reason_out, diagnostic,
            DLUMA_DL_LIVE_MANAGER_SECONDARY_VTABLE_8_MISMATCH);
    if (*(void**)((BYTE*)manager + 16U) !=
            (BYTE*)profile->gamedll +
                DLUMA_DL_FRONTEND_MUSICS_MANAGER_VTABLE_16_RVA)
        return dluma_player_media_manager_validation_failed(
            reason_out, diagnostic,
            DLUMA_DL_LIVE_MANAGER_SECONDARY_VTABLE_16_MISMATCH);
    if (*(void**)((BYTE*)manager + 48U) !=
            (BYTE*)profile->gamedll +
                DLUMA_DL_FRONTEND_MUSICS_MANAGER_VTABLE_48_RVA)
        return dluma_player_media_manager_validation_failed(
            reason_out, diagnostic,
            DLUMA_DL_LIVE_MANAGER_SECONDARY_VTABLE_48_MISMATCH);
    primary_vtable = *(void***)manager;
    if (!dluma_player_media_readonly_range(
            profile->gamedll, profile->gamedll_size,
            primary_vtable, sizeof(void*) * 2U) ||
        !dluma_player_media_is_executable(
            profile->gamedll, profile->gamedll_size,
            primary_vtable[0], 1U))
        return dluma_player_media_manager_validation_failed(
            reason_out, diagnostic,
            DLUMA_DL_LIVE_MANAGER_PRIMARY_TARGET_INVALID);
    if (reason_out) *reason_out = DLUMA_DL_LIVE_READY;
    if (diagnostic) diagnostic->stage = DLUMA_DL_LIVE_READY;
    return 1;
}

static int dluma_player_media_validate_frontend_main_menu_module(
    DlumaPlayerMediaPlayerProfile* profile, void* module)
{
    void** primary_vtable;
    if (!profile || !profile->exact_profile ||
        !profile->main_menu_music_ready || !module ||
        !dluma_is_readable(module, DLUMA_DL_MAIN_MENU_MODULE_MIN_SIZE) ||
        *(void**)module !=
            (BYTE*)profile->gamedll +
                DLUMA_DL_MAIN_MENU_MODULE_VTABLE_RVA)
        return 0;
    primary_vtable = *(void***)module;
    return dluma_player_media_readonly_range(
               profile->gamedll, profile->gamedll_size,
               primary_vtable, sizeof(void*) * 2U) &&
           dluma_player_media_is_executable(
               profile->gamedll, profile->gamedll_size,
               primary_vtable[0], 1U) &&
           dluma_is_readable(
               (BYTE*)module +
                   DLUMA_DL_MAIN_MENU_MODULE_MUSIC_HANDLE_OFFSET,
               sizeof(int));
}

static int dluma_player_media_validate_main_menu_audio_owner(
    DlumaPlayerMediaPlayerProfile* profile, void* module)
{
    return dluma_player_media_validate_frontend_main_menu_module(
               profile, module) &&
           dluma_is_readable(
               (BYTE*)module +
                   DLUMA_DL_MAIN_MENU_MODULE_IGSOBJECT_OFFSET,
               sizeof(void*)) &&
           *(void**)((BYTE*)module +
                     DLUMA_DL_MAIN_MENU_MODULE_IGSOBJECT_OFFSET) ==
               (BYTE*)profile->gamedll +
                   DLUMA_DL_MAIN_MENU_MODULE_IGSOBJECT_VTABLE_RVA;
}

static int dluma_player_media_resolve_published_frontend_music_manager(
    DlumaPlayerMediaPlayerProfile* profile,
    void** owner_level_out, void** manager_out,
    unsigned int* reason_out, unsigned int* source_out,
    DlumaPlayerMediaManagerDiagnostic* diagnostic)
{
    void* owner_level;
    void* manager;
    void* manager_owner = 0;
    int query_delivered = 1;
    if (owner_level_out) *owner_level_out = 0;
    if (manager_out) *manager_out = 0;
    if (source_out) *source_out = DLUMA_DL_MUSIC_SOURCE_NONE;
    if (!profile || !owner_level_out || !manager_out ||
        !reason_out || !source_out || !profile->get_object_level) {
        if (reason_out)
            *reason_out = DLUMA_DL_LIVE_RESOLVER_ARGUMENT_INVALID;
        return 0;
    }
    owner_level = profile->published_frontend_owner_level;
    manager = profile->published_frontend_music_manager;
    if (!owner_level || !manager) {
        *reason_out = DLUMA_DL_LIVE_READINESS_INVARIANT_VIOLATION;
        if (diagnostic)
            diagnostic->stage =
                DLUMA_DL_LIVE_READINESS_INVARIANT_VIOLATION;
        return 0;
    }
    if (!dluma_is_readable(owner_level, sizeof(void*)) ||
        !dluma_player_media_validate_frontend_musics_manager_object_detailed(
            profile, manager, reason_out, diagnostic))
        return 0;
    __try {
        manager_owner = profile->get_object_level(manager);
    }
    __except(1) {
        query_delivered = 0;
    }
    if (!query_delivered || !manager_owner ||
        !dluma_is_readable(manager_owner, sizeof(void*))) {
        (void)dluma_player_media_manager_validation_failed(
            reason_out, diagnostic,
            DLUMA_DL_LIVE_MANAGER_OWNER_QUERY_FAILED);
        return 0;
    }
    if (manager_owner != owner_level) {
        (void)dluma_player_media_manager_validation_failed(
            reason_out, diagnostic,
            DLUMA_DL_LIVE_MANAGER_OWNER_MISMATCH);
        return 0;
    }
    *owner_level_out = owner_level;
    *manager_out = manager;
    *reason_out = DLUMA_DL_LIVE_READY;
    *source_out =
        DLUMA_DL_MUSIC_SOURCE_CUSTOM_FRONTEND_BACKGROUND_OWNER_CORROBORATED;
    if (diagnostic) diagnostic->stage = DLUMA_DL_LIVE_READY;
    return 1;
}

static int dluma_player_media_resolve_game_session_music_manager(
    DlumaPlayerMediaPlayerProfile* profile, void* observed_active_level,
    void** owner_level_out, void** level_di_out, void** manager_out,
    unsigned int* reason_out, unsigned int* source_out,
    unsigned int* session_class_out,
    DlumaPlayerMediaManagerDiagnostic* diagnostic)
{
    BYTE* frame_game_primary;
    BYTE* game_primary;
    void** game_global_slot;
    void** session_field;
    void* session;
    void* level_di = 0;
    void* level_igsobject = 0;
    void* level_owner = 0;
    void* manager_owner = 0;
    void** manager_field;
    void* manager;
    int query_delivered = 1;
    if (owner_level_out) *owner_level_out = 0;
    if (level_di_out) *level_di_out = 0;
    if (manager_out) *manager_out = 0;
    if (source_out) *source_out = DLUMA_DL_MUSIC_SOURCE_NONE;
    if (session_class_out)
        *session_class_out = DLUMA_DL_SESSION_CLASS_NONE;
    if (!profile || !owner_level_out || !level_di_out || !manager_out ||
        !session_class_out ||
        !profile->game_subobject || !profile->get_session_level ||
        !profile->get_object_level) {
        if (reason_out)
            *reason_out = DLUMA_DL_LIVE_GAME_FRAME_RECEIVER_INVALID;
        return 0;
    }
    if ((ULONG_PTR)profile->game_subobject <
        DLUMA_DL_GAMEDI_FRAME_SUBOBJECT_OFFSET) {
        if (reason_out)
            *reason_out = DLUMA_DL_LIVE_GAME_FRAME_RECEIVER_INVALID;
        return 0;
    }
    frame_game_primary = (BYTE*)profile->game_subobject -
                         DLUMA_DL_GAMEDI_FRAME_SUBOBJECT_OFFSET;
    if (!dluma_is_readable(
            frame_game_primary,
            DLUMA_DL_GAMEDI_FRAME_SUBOBJECT_OFFSET + sizeof(void*)) ||
        *(void**)frame_game_primary !=
            (BYTE*)profile->gamedll +
                DLUMA_DL_GAMEDI_PRIMARY_VTABLE_RVA ||
        *(void**)(frame_game_primary +
                  DLUMA_DL_GAMEDI_FRAME_SUBOBJECT_OFFSET) !=
            (BYTE*)profile->gamedll +
                DLUMA_DL_GAMEDI_FRAME_VTABLE_RVA ||
        !dluma_player_media_validate_live_rtti_object(
            profile->gamedll, profile->gamedll_size,
            frame_game_primary,
            DLUMA_DL_GAMEDI_TYPE_RVA, ".?AVGameDI@@")) {
        if (reason_out)
            *reason_out = DLUMA_DL_LIVE_GAME_FRAME_RECEIVER_INVALID;
        return 0;
    }
    /*
     * The exact Player invokes the validated OnFrame slot on a live GameDI
     * instance that is not necessarily the authoritative gameplay GameDI
     * stored in the process global.  The July 26 custom-map trace proved that
     * the frame receiver's +0x508 remained null while the active SessionDI
     * completed its normal OnDestroy path.  Resolve the session only through
     * the exact global loaded by CScriptConditionIsDriving.  GetActiveLevel
     * is additional corroboration when positive, not a prerequisite: this
     * Player can return null for an entire live Workshop-map session.
     */
    game_global_slot = (void**)((BYTE*)profile->gamedll +
                                DLUMA_DL_GAMEDI_GLOBAL_RVA);
    if (!dluma_player_media_module_range(
            profile->gamedll, profile->gamedll_size,
            game_global_slot, sizeof(*game_global_slot)) ||
        !dluma_is_readable(game_global_slot,
                         sizeof(*game_global_slot))) {
        if (reason_out)
            *reason_out =
                DLUMA_DL_LIVE_GAME_GLOBAL_SLOT_UNREADABLE;
        return 0;
    }
    game_primary = (BYTE*)*game_global_slot;
    if (!game_primary) {
        if (reason_out)
            *reason_out = DLUMA_DL_LIVE_GAME_GLOBAL_UNAVAILABLE;
        return 0;
    }
    if (!dluma_is_readable(
            game_primary, DLUMA_DL_GAMEDI_SESSION_OFFSET + sizeof(void*)) ||
        *(void**)game_primary !=
            (BYTE*)profile->gamedll +
                DLUMA_DL_GAMEDI_PRIMARY_VTABLE_RVA ||
        *(void**)(game_primary +
                  DLUMA_DL_GAMEDI_FRAME_SUBOBJECT_OFFSET) !=
            (BYTE*)profile->gamedll +
                DLUMA_DL_GAMEDI_FRAME_VTABLE_RVA ||
        !dluma_player_media_validate_live_rtti_object(
            profile->gamedll, profile->gamedll_size, game_primary,
            DLUMA_DL_GAMEDI_TYPE_RVA, ".?AVGameDI@@")) {
        if (reason_out)
            *reason_out = DLUMA_DL_LIVE_GAME_GLOBAL_RTTI_INVALID;
        return 0;
    }
    session_field = (void**)(game_primary +
                             DLUMA_DL_GAMEDI_SESSION_OFFSET);
    if (!dluma_is_readable(session_field, sizeof(*session_field))) {
        if (reason_out)
            *reason_out = DLUMA_DL_LIVE_GAME_SESSION_FIELD_UNREADABLE;
        return 0;
    }
    session = *session_field;
    if (!session) {
        if (reason_out)
            *reason_out = DLUMA_DL_LIVE_GAME_SESSION_UNAVAILABLE;
        return 0;
    }
    if (!dluma_player_media_validate_live_session_object(
            profile, session, session_class_out, reason_out))
        return 0;
    __try {
        level_di = profile->get_session_level(session);
    }
    __except(1) {
        query_delivered = 0;
    }
    if (!query_delivered || !level_di ||
        !dluma_player_media_validate_live_rtti_object(
            profile->gamedll, profile->gamedll_size, level_di,
            DLUMA_DL_LEVELDI_TYPE_RVA, ".?AVLevelDI@@")) {
        if (reason_out)
            *reason_out = DLUMA_DL_LIVE_SESSION_LEVEL_QUERY_FAILED;
        return 0;
    }
    if (!dluma_player_media_leveldi_igsobject(
            profile, level_di, &level_igsobject)) {
        if (reason_out)
            *reason_out =
                DLUMA_DL_LIVE_LEVELDI_IGSOBJECT_INVALID;
        return 0;
    }
    query_delivered = 1;
    __try {
        level_owner = profile->get_object_level(level_igsobject);
    }
    __except(1) {
        query_delivered = 0;
    }
    if (!query_delivered || !level_owner ||
        !dluma_is_readable(level_owner, sizeof(void*))) {
        if (reason_out)
            *reason_out =
                DLUMA_DL_LIVE_SESSION_LEVEL_OWNER_QUERY_FAILED;
        return 0;
    }
    if (observed_active_level &&
        (!dluma_is_readable(observed_active_level, sizeof(void*)) ||
         level_owner != observed_active_level)) {
        if (reason_out)
            *reason_out =
                DLUMA_DL_LIVE_ACTIVE_LEVEL_SESSION_OWNER_MISMATCH;
        return 0;
    }
    manager_field = (void**)((BYTE*)level_di +
                             DLUMA_DL_LEVELDI_MUSIC_MANAGER_OFFSET);
    if (!dluma_is_readable(manager_field, sizeof(*manager_field))) {
        if (reason_out)
            *reason_out = DLUMA_DL_LIVE_MANAGER_FIELD_UNREADABLE;
        return 0;
    }
    manager = *manager_field;
    if (!manager) {
        if (reason_out)
            *reason_out = DLUMA_DL_LIVE_MANAGER_FIELD_UNAVAILABLE;
        return 0;
    }
    if (!dluma_player_media_validate_music_manager_object_detailed(
            profile, manager, reason_out, diagnostic))
        return 0;
    query_delivered = 1;
    __try {
        manager_owner = profile->get_object_level(manager);
    }
    __except(1) {
        query_delivered = 0;
    }
    if (!query_delivered || !manager_owner ||
        !dluma_is_readable(manager_owner, sizeof(void*))) {
        (void)dluma_player_media_manager_validation_failed(
            reason_out, diagnostic,
            DLUMA_DL_LIVE_MANAGER_OWNER_QUERY_FAILED);
        return 0;
    }
    if (manager_owner != level_owner) {
        (void)dluma_player_media_manager_validation_failed(
            reason_out, diagnostic,
            DLUMA_DL_LIVE_MANAGER_OWNER_MISMATCH);
        return 0;
    }
    *owner_level_out = level_owner;
    *level_di_out = level_di;
    *manager_out = manager;
    if (reason_out) *reason_out = DLUMA_DL_LIVE_READY;
    if (diagnostic) diagnostic->stage = DLUMA_DL_LIVE_READY;
    if (source_out)
        *source_out =
            DLUMA_DL_MUSIC_SOURCE_GAME_GLOBAL_SESSION_MANAGER_OWNER_CORROBORATED;
    return 1;
}

static int dluma_player_media_resolve_owned_singleton_music_manager(
    DlumaPlayerMediaPlayerProfile* profile, void* level_interface,
    void** owner_level_out, void** manager_out, unsigned int* reason_out,
    unsigned int* source_out,
    DlumaPlayerMediaManagerDiagnostic* diagnostic)
{
    void** singleton;
    void* manager;
    void* owner_level = 0;
    int query_delivered = 1;
    if (owner_level_out) *owner_level_out = 0;
    if (manager_out) *manager_out = 0;
    if (source_out) *source_out = DLUMA_DL_MUSIC_SOURCE_NONE;
    if (!profile || !level_interface || !owner_level_out || !manager_out ||
        !profile->get_object_level) {
        if (reason_out) *reason_out = DLUMA_DL_LIVE_PROFILE_INVALID;
        return 0;
    }
    singleton = (void**)((BYTE*)profile->gamedll +
                         DLUMA_DL_MUSIC_MANAGER_SINGLETON_RVA);
    if (!dluma_player_media_module_range(
            profile->gamedll, profile->gamedll_size,
            singleton, sizeof(*singleton))) {
        if (reason_out)
            *reason_out = DLUMA_DL_LIVE_SINGLETON_SLOT_OUT_OF_RANGE;
        return 0;
    }
    if (!dluma_is_readable(singleton, sizeof(*singleton))) {
        if (reason_out)
            *reason_out = DLUMA_DL_LIVE_SINGLETON_SLOT_UNREADABLE;
        return 0;
    }
    manager = *singleton;
    if (!manager) {
        if (reason_out)
            *reason_out = DLUMA_DL_LIVE_SINGLETON_UNAVAILABLE;
        return 0;
    }
    if (!dluma_player_media_validate_music_manager_object_detailed(
            profile, manager, reason_out, diagnostic))
        return 0;
    /*
     * MusicManager's fingerprinted primary RTTI hierarchy places GameObject
     * and IGSObject at offset zero.  The exact engine GetLevel export can
     * therefore prove that the process-global manager belongs to the same
     * live ILevel returned by IGame::GetActiveLevel.  This is the supported
     * Workshop Player route when that active ILevel is a proxy rather than
     * the gameplay LevelDI complete object.
     */
    __try {
        owner_level = profile->get_object_level(manager);
    }
    __except(1) {
        query_delivered = 0;
    }
    if (!query_delivered || !owner_level ||
        !dluma_is_readable(owner_level, sizeof(void*))) {
        (void)dluma_player_media_manager_validation_failed(
            reason_out, diagnostic,
            DLUMA_DL_LIVE_MANAGER_OWNER_QUERY_FAILED);
        return 0;
    }
    if (owner_level != level_interface) {
        (void)dluma_player_media_manager_validation_failed(
            reason_out, diagnostic,
            DLUMA_DL_LIVE_MANAGER_OWNER_MISMATCH);
        return 0;
    }
    *owner_level_out = owner_level;
    *manager_out = manager;
    if (reason_out) *reason_out = DLUMA_DL_LIVE_READY;
    if (diagnostic) diagnostic->stage = DLUMA_DL_LIVE_READY;
    if (source_out)
        *source_out =
            DLUMA_DL_MUSIC_SOURCE_SINGLETON_OWNER_LEVEL_CORROBORATED;
    return 1;
}

static int dluma_player_media_music_result_complete(
    unsigned int reason, void* owner_level, void* manager,
    unsigned int source)
{
    return reason == DLUMA_DL_LIVE_READY &&
           owner_level != 0 &&
           manager != 0 &&
           source != DLUMA_DL_MUSIC_SOURCE_NONE;
}

static int dluma_player_media_resolve_music_manager_detailed(
    DlumaPlayerMediaPlayerProfile* profile, void* observed_active_level,
    void** owner_level_out, void** level_di_out, void** manager_out,
    unsigned int* reason_out, unsigned int* source_out,
    unsigned int* session_class_out,
    DlumaPlayerMediaManagerDiagnostic* diagnostic)
{
    void* owner_level = 0;
    void* level_di = 0;
    void* manager = 0;
    void* manager_owner = 0;
    void** manager_field;
    void** singleton;
    void* singleton_manager = 0;
    unsigned int reason =
        DLUMA_DL_LIVE_GAME_FRAME_RECEIVER_INVALID;
    unsigned int session_reason =
        DLUMA_DL_LIVE_GAME_FRAME_RECEIVER_INVALID;
    unsigned int source = DLUMA_DL_MUSIC_SOURCE_NONE;
    unsigned int session_class = DLUMA_DL_SESSION_CLASS_NONE;
    DlumaPlayerMediaManagerDiagnostic session_diagnostic;
    int cast_delivered = 1;
    int owner_query_delivered = 1;
    if (owner_level_out) *owner_level_out = 0;
    if (level_di_out) *level_di_out = 0;
    if (manager_out) *manager_out = 0;
    if (reason_out)
        *reason_out = DLUMA_DL_LIVE_RESOLVER_ARGUMENT_INVALID;
    if (source_out) *source_out = DLUMA_DL_MUSIC_SOURCE_NONE;
    if (session_class_out)
        *session_class_out = DLUMA_DL_SESSION_CLASS_NONE;
    dluma_player_media_manager_diagnostic_reset(diagnostic);
    dluma_player_media_manager_diagnostic_reset(
        &session_diagnostic);
    if (!profile || !owner_level_out || !level_di_out || !manager_out ||
        !reason_out || !source_out || !session_class_out) {
        if (diagnostic)
            diagnostic->stage =
                DLUMA_DL_LIVE_RESOLVER_ARGUMENT_INVALID;
        return 0;
    }
    if (!profile->get_object_level) {
        *reason_out = DLUMA_DL_LIVE_GET_OBJECT_LEVEL_UNBOUND;
        if (diagnostic)
            diagnostic->stage =
                DLUMA_DL_LIVE_GET_OBJECT_LEVEL_UNBOUND;
        return 0;
    }
    if (!profile->rt_dynamic_cast) {
        *reason_out = DLUMA_DL_LIVE_RT_DYNAMIC_CAST_UNBOUND;
        if (diagnostic)
            diagnostic->stage =
                DLUMA_DL_LIVE_RT_DYNAMIC_CAST_UNBOUND;
        return 0;
    }
    if (!profile->get_session_level) {
        *reason_out = DLUMA_DL_LIVE_GET_SESSION_LEVEL_UNBOUND;
        if (diagnostic)
            diagnostic->stage =
                DLUMA_DL_LIVE_GET_SESSION_LEVEL_UNBOUND;
        return 0;
    }
    if (observed_active_level &&
        !dluma_is_readable(observed_active_level, sizeof(void*))) {
        *reason_out = DLUMA_DL_LIVE_ACTIVE_LEVEL_UNAVAILABLE;
        if (diagnostic)
            diagnostic->stage =
                DLUMA_DL_LIVE_ACTIVE_LEVEL_UNAVAILABLE;
        return 0;
    }
    /*
     * Exact LevelDI is the common Player case. Its validated primary COL has
     * offset zero, so the ILevel returned by GetActiveLevel is already the
     * complete LevelDI address. Accept that exact primary vtable directly;
     * retain __RTDynamicCast for a future exact-profile subclass.
     */
    if (observed_active_level) {
        if (*(void**)observed_active_level ==
                (BYTE*)profile->gamedll +
                    DLUMA_DL_LEVELDI_VTABLE_RVA) {
            level_di = observed_active_level;
        }
        else {
            __try {
                level_di = profile->rt_dynamic_cast(
                    observed_active_level, 0,
                    (BYTE*)profile->gamedll + DLUMA_DL_ILEVEL_TYPE_RVA,
                    (BYTE*)profile->gamedll + DLUMA_DL_LEVELDI_TYPE_RVA,
                    0);
            }
            __except(1) {
                cast_delivered = 0;
            }
        }
    }
    if (cast_delivered && level_di) {
        if (!dluma_player_media_validate_live_rtti_object(
                profile->gamedll, profile->gamedll_size, level_di,
                DLUMA_DL_LEVELDI_TYPE_RVA, ".?AVLevelDI@@")) {
            *reason_out = DLUMA_DL_LIVE_LEVEL_RTTI_INVALID;
            if (diagnostic)
                diagnostic->stage =
                    DLUMA_DL_LIVE_LEVEL_RTTI_INVALID;
            return 0;
        }
        manager_field = (void**)((BYTE*)level_di +
                                 DLUMA_DL_LEVELDI_MUSIC_MANAGER_OFFSET);
        if (!dluma_is_readable(manager_field, sizeof(*manager_field))) {
            *reason_out = DLUMA_DL_LIVE_MANAGER_FIELD_UNREADABLE;
            if (diagnostic)
                diagnostic->stage =
                    DLUMA_DL_LIVE_MANAGER_FIELD_UNREADABLE;
            return 0;
        }
        manager = *manager_field;
        if (!manager) {
            *reason_out = DLUMA_DL_LIVE_MANAGER_FIELD_UNAVAILABLE;
            if (diagnostic)
                diagnostic->stage =
                    DLUMA_DL_LIVE_MANAGER_FIELD_UNAVAILABLE;
            return 0;
        }
        if (!dluma_player_media_validate_music_manager_object_detailed(
                profile, manager, &reason, diagnostic)) {
            *reason_out = reason;
            return 0;
        }
        __try {
            manager_owner = profile->get_object_level(manager);
        }
        __except(1) {
            owner_query_delivered = 0;
        }
        if (!owner_query_delivered || !manager_owner ||
            !dluma_is_readable(manager_owner, sizeof(void*))) {
            (void)dluma_player_media_manager_validation_failed(
                reason_out, diagnostic,
                DLUMA_DL_LIVE_MANAGER_OWNER_QUERY_FAILED);
            return 0;
        }
        if (manager_owner != observed_active_level) {
            (void)dluma_player_media_manager_validation_failed(
                reason_out, diagnostic,
                DLUMA_DL_LIVE_MANAGER_OWNER_MISMATCH);
            return 0;
        }
        owner_level = manager_owner;
        singleton = (void**)((BYTE*)profile->gamedll +
                             DLUMA_DL_MUSIC_MANAGER_SINGLETON_RVA);
        if (dluma_player_media_module_range(
                profile->gamedll, profile->gamedll_size,
                singleton, sizeof(*singleton)) &&
            dluma_is_readable(singleton, sizeof(*singleton)))
            singleton_manager = *singleton;
        if (!singleton_manager)
            source =
                DLUMA_DL_MUSIC_SOURCE_ACTIVE_LEVEL_SINGLETON_NULL;
        else if (singleton_manager == manager)
            source =
                DLUMA_DL_MUSIC_SOURCE_ACTIVE_LEVEL_CORROBORATED;
        else
            source =
                DLUMA_DL_MUSIC_SOURCE_ACTIVE_LEVEL_STALE_SINGLETON_IGNORED;
        reason = DLUMA_DL_LIVE_READY;
        if (!dluma_player_media_music_result_complete(
                reason, owner_level, manager, source)) {
            *reason_out =
                DLUMA_DL_LIVE_READINESS_INVARIANT_VIOLATION;
            if (diagnostic)
                diagnostic->stage =
                    DLUMA_DL_LIVE_READINESS_INVARIANT_VIOLATION;
            return 0;
        }
        *owner_level_out = owner_level;
        *level_di_out = level_di;
        *manager_out = manager;
        *reason_out = reason;
        *source_out = source;
        if (diagnostic) diagnostic->stage = reason;
        return 1;
    }
    if (dluma_player_media_resolve_game_session_music_manager(
            profile, observed_active_level, &owner_level,
            &level_di, &manager, &reason, &source,
            &session_class, diagnostic)) {
        if (!dluma_player_media_music_result_complete(
                reason, owner_level, manager, source)) {
            *reason_out =
                DLUMA_DL_LIVE_READINESS_INVARIANT_VIOLATION;
            if (diagnostic)
                diagnostic->stage =
                    DLUMA_DL_LIVE_READINESS_INVARIANT_VIOLATION;
            return 0;
        }
        *owner_level_out = owner_level;
        *level_di_out = level_di;
        *manager_out = manager;
        *reason_out = reason;
        *source_out = source;
        *session_class_out = session_class;
        return 1;
    }
    session_reason = reason;
    if (diagnostic) session_diagnostic = *diagnostic;
    owner_level = 0;
    level_di = 0;
    manager = 0;
    source = DLUMA_DL_MUSIC_SOURCE_NONE;
    session_class = DLUMA_DL_SESSION_CLASS_NONE;
    if (observed_active_level &&
        dluma_player_media_resolve_owned_singleton_music_manager(
            profile, observed_active_level, &owner_level,
            &manager, &reason, &source, diagnostic)) {
        if (!dluma_player_media_music_result_complete(
                reason, owner_level, manager, source)) {
            *reason_out =
                DLUMA_DL_LIVE_READINESS_INVARIANT_VIOLATION;
            if (diagnostic)
                diagnostic->stage =
                    DLUMA_DL_LIVE_READINESS_INVARIANT_VIOLATION;
            return 0;
        }
        *owner_level_out = owner_level;
        *manager_out = manager;
        *reason_out = reason;
        *source_out = source;
        return 1;
    }
    *reason_out = session_reason;
    if (diagnostic) *diagnostic = session_diagnostic;
    return 0;
}

static int dluma_player_media_validate_music_manager(
    DlumaPlayerMediaPlayerProfile* profile, void** manager_out)
{
    void* owner_level = 0;
    void* level_di = 0;
    void* manager = 0;
    unsigned int reason = DLUMA_DL_LIVE_PROFILE_INVALID;
    unsigned int source = DLUMA_DL_MUSIC_SOURCE_NONE;
    unsigned int session_class = DLUMA_DL_SESSION_CLASS_NONE;
    DlumaPlayerMediaManagerDiagnostic diagnostic;
    if (manager_out) *manager_out = 0;
    if (!profile || !manager_out || !profile->live_owner_level ||
        !profile->live_music_manager)
        return 0;
    if (!dluma_player_media_resolve_music_manager_detailed(
            profile, profile->observed_active_level,
            &owner_level, &level_di, &manager, &reason, &source,
            &session_class, &diagnostic) ||
        owner_level != profile->live_owner_level ||
        level_di != profile->live_level_di ||
        manager != profile->live_music_manager ||
        session_class != profile->live_session_class)
        return 0;
    *manager_out = manager;
    return 1;
}

static int dluma_player_media_validate_wave_owner(
    DlumaPlayerMediaPlayerProfile* profile, void** owner_out)
{
    void* owner_level = 0;
    void* manager = 0;
    unsigned int reason = DLUMA_DL_LIVE_PROFILE_INVALID;
    unsigned int source = DLUMA_DL_MUSIC_SOURCE_NONE;
    DlumaPlayerMediaManagerDiagnostic diagnostic;
    if (owner_out) *owner_out = 0;
    if (!profile || !owner_out)
        return 0;
    if (profile->published_frontend_owner_level ||
        profile->published_frontend_music_manager ||
        profile->published_frontend_main_menu_module) {
        if (!dluma_player_media_resolve_published_frontend_music_manager(
                profile, &owner_level, &manager, &reason, &source,
                &diagnostic) ||
            source !=
                DLUMA_DL_MUSIC_SOURCE_CUSTOM_FRONTEND_BACKGROUND_OWNER_CORROBORATED)
            return 0;
        *owner_out = manager;
        return 1;
    }
    return dluma_player_media_validate_music_manager(profile, owner_out);
}

static const char* dluma_player_media_live_reason_name(
    unsigned int reason)
{
    switch (reason) {
    case DLUMA_DL_LIVE_READY:
        return "1";
    case DLUMA_DL_LIVE_PROFILE_INVALID:
        return "profile_invalid";
    case DLUMA_DL_LIVE_SINGLETON_SLOT_OUT_OF_RANGE:
        return "singleton_slot_out_of_range";
    case DLUMA_DL_LIVE_SINGLETON_SLOT_UNREADABLE:
        return "singleton_slot_unreadable";
    case DLUMA_DL_LIVE_SINGLETON_UNAVAILABLE:
        return "singleton_unavailable";
    case DLUMA_DL_LIVE_OBJECT_UNREADABLE:
        return "object_unreadable";
    case DLUMA_DL_LIVE_PRIMARY_VTABLE_MISMATCH:
        return "primary_vtable_mismatch";
    case DLUMA_DL_LIVE_AUDIO_SYSTEM_UNAVAILABLE:
        return "audio_system_unavailable";
    case DLUMA_DL_LIVE_AUDIO_NOT_INITIALIZED:
        return "audio_not_initialized";
    case DLUMA_DL_LIVE_AUDIO_VTABLE_SLOT_MISMATCH:
        return "audio_vtable_slot_mismatch";
    case DLUMA_DL_LIVE_SECONDARY_VTABLE_MISMATCH:
        return "secondary_vtable_mismatch";
    case DLUMA_DL_LIVE_VTABLE_TARGET_INVALID:
        return "vtable_target_invalid";
    case DLUMA_DL_LIVE_ACTIVE_LEVEL_UNAVAILABLE:
        return "active_level_unavailable";
    case DLUMA_DL_LIVE_LEVEL_CAST_FAILED:
        return "active_level_dynamic_cast_failed";
    case DLUMA_DL_LIVE_LEVEL_RTTI_INVALID:
        return "active_level_rtti_invalid";
    case DLUMA_DL_LIVE_MANAGER_FIELD_UNREADABLE:
        return "active_level_manager_field_unreadable";
    case DLUMA_DL_LIVE_MANAGER_FIELD_UNAVAILABLE:
        return "active_level_manager_unavailable";
    case DLUMA_DL_LIVE_FILTER_SNAPSHOT_UNAVAILABLE:
        return "music_filter_snapshot_unavailable";
    case DLUMA_DL_LIVE_OWNER_TRANSITION_RETAINED:
        return "owner_transition_cleanup_failed_state_retained";
    case DLUMA_DL_LIVE_FILTER_BINDING_UNAVAILABLE:
        return "music_filter_static_binding_unavailable";
    case DLUMA_DL_LIVE_MANAGER_OWNER_QUERY_FAILED:
        return "manager_owner_query_failed";
    case DLUMA_DL_LIVE_MANAGER_OWNER_LEVEL_MISMATCH:
        return "music_manager_owner_level_mismatch";
    case DLUMA_DL_LIVE_GAME_FRAME_RECEIVER_INVALID:
        return "game_frame_receiver_invalid";
    case DLUMA_DL_LIVE_GAME_SESSION_FIELD_UNREADABLE:
        return "game_session_field_unreadable";
    case DLUMA_DL_LIVE_GAME_SESSION_UNAVAILABLE:
        return "game_session_unavailable";
    case DLUMA_DL_LIVE_GAME_SESSION_RTTI_INVALID:
        return "game_session_rtti_invalid";
    case DLUMA_DL_LIVE_SESSION_LEVEL_QUERY_FAILED:
        return "game_session_level_query_failed";
    case DLUMA_DL_LIVE_SESSION_LEVEL_OWNER_MISMATCH:
        return "game_session_level_owner_mismatch";
    case DLUMA_DL_LIVE_GAME_GLOBAL_SLOT_UNREADABLE:
        return "game_global_slot_unreadable";
    case DLUMA_DL_LIVE_GAME_GLOBAL_UNAVAILABLE:
        return "game_global_unavailable";
    case DLUMA_DL_LIVE_GAME_GLOBAL_RTTI_INVALID:
        return "game_global_rtti_invalid";
    case DLUMA_DL_LIVE_SESSION_LEVEL_OWNER_QUERY_FAILED:
        return "game_session_level_owner_query_failed";
    case DLUMA_DL_LIVE_SESSION_MANAGER_OWNER_QUERY_FAILED:
        return "game_session_manager_owner_query_failed";
    case DLUMA_DL_LIVE_SESSION_MANAGER_OWNER_MISMATCH:
        return "game_session_manager_owner_mismatch";
    case DLUMA_DL_LIVE_ACTIVE_LEVEL_SESSION_OWNER_MISMATCH:
        return "active_level_session_owner_mismatch";
    case DLUMA_DL_LIVE_GAME_SESSION_OBJECT_UNREADABLE:
        return "game_session_object_unreadable";
    case DLUMA_DL_LIVE_GAME_SESSION_CLASS_UNSUPPORTED:
        return "game_session_class_unsupported";
    case DLUMA_DL_LIVE_GAME_SESSION_LEVEL_SLOT_MISMATCH:
        return "game_session_level_slot_mismatch";
    case DLUMA_DL_LIVE_LEVELDI_IGSOBJECT_INVALID:
        return "leveldi_igsobject_subobject_invalid";
    case DLUMA_DL_LIVE_RESOLVER_ARGUMENT_INVALID:
        return "resolver_argument_invalid";
    case DLUMA_DL_LIVE_RT_DYNAMIC_CAST_UNBOUND:
        return "rt_dynamic_cast_unbound";
    case DLUMA_DL_LIVE_GET_SESSION_LEVEL_UNBOUND:
        return "get_session_level_unbound";
    case DLUMA_DL_LIVE_GET_OBJECT_LEVEL_UNBOUND:
        return "get_object_level_unbound";
    case DLUMA_DL_LIVE_MANAGER_OBJECT_UNREADABLE:
        return "manager_object_unreadable";
    case DLUMA_DL_LIVE_MANAGER_PRIMARY_VTABLE_MISMATCH:
        return "manager_primary_vtable_mismatch";
    case DLUMA_DL_LIVE_MANAGER_SECONDARY_VTABLE_8_MISMATCH:
        return "manager_secondary_vtable_8_mismatch";
    case DLUMA_DL_LIVE_MANAGER_SECONDARY_VTABLE_16_MISMATCH:
        return "manager_secondary_vtable_16_mismatch";
    case DLUMA_DL_LIVE_MANAGER_SECONDARY_VTABLE_48_MISMATCH:
        return "manager_secondary_vtable_48_mismatch";
    case DLUMA_DL_LIVE_MANAGER_PRIMARY_TARGET_INVALID:
        return "manager_primary_target_invalid";
    case DLUMA_DL_LIVE_MANAGER_OWNER_MISMATCH:
        return "manager_owner_mismatch";
    case DLUMA_DL_LIVE_READINESS_INVARIANT_VIOLATION:
        return "readiness_invariant_violation";
    default:
        return "unknown_validation_failure";
    }
}

static const char* dluma_player_media_wave_binding_reason_name(
    unsigned int reason)
{
    switch (reason) {
    case DLUMA_DL_WAVE_BIND_READY_ORIGINAL_ENTRY:
        return "original_entry";
    case DLUMA_DL_WAVE_BIND_READY_LOADER_OWNED_DETOUR:
        return "loader_owned_detour";
    case DLUMA_DL_WAVE_BIND_PROFILE_INVALID:
        return "profile_invalid";
    case DLUMA_DL_WAVE_BIND_PREFIX_MISMATCH:
        return "load_wave_bank_prefix_mismatch";
    case DLUMA_DL_WAVE_BIND_CALL_TARGET_MISMATCH:
        return "sound_bank_call_target_mismatch";
    case DLUMA_DL_WAVE_BIND_VTABLE_SLOT_MISMATCH:
        return "audio_vtable_slot_mismatch";
    default:
        return "unknown_binding_failure";
    }
}

static int dluma_player_media_wave_binding_reason_ready(
    unsigned int reason)
{
    return reason == DLUMA_DL_WAVE_BIND_READY_ORIGINAL_ENTRY ||
           reason == DLUMA_DL_WAVE_BIND_READY_LOADER_OWNED_DETOUR;
}

static const char* dluma_player_media_music_binding_reason_name(
    unsigned int reason)
{
    switch (reason) {
    case DLUMA_DL_MUSIC_BIND_READY:
        return "1";
    case DLUMA_DL_MUSIC_BIND_PROFILE_INVALID:
        return "profile_invalid";
    case DLUMA_DL_MUSIC_BIND_CTOR_PREFIX_MISMATCH:
        return "constructor_prefix_mismatch";
    case DLUMA_DL_MUSIC_BIND_STOP_PREFIX_MISMATCH:
        return "stop_prefix_mismatch";
    case DLUMA_DL_MUSIC_BIND_START_PREFIX_MISMATCH:
        return "start_prefix_mismatch";
    case DLUMA_DL_MUSIC_BIND_DELAYED_START_PREFIX_MISMATCH:
        return "delayed_start_prefix_mismatch";
    case DLUMA_DL_MUSIC_BIND_RTDYNAMICCAST_PREFIX_MISMATCH:
        return "rtdynamiccast_prefix_mismatch";
    case DLUMA_DL_MUSIC_BIND_ILEVEL_TYPE_NAME_MISMATCH:
        return "ilevel_type_name_mismatch";
    case DLUMA_DL_MUSIC_BIND_LEVELDI_TYPE_NAME_MISMATCH:
        return "leveldi_type_name_mismatch";
    case DLUMA_DL_MUSIC_BIND_LEVELDI_PRIMARY_RTTI_MISMATCH:
        return "leveldi_primary_rtti_mismatch";
    case DLUMA_DL_MUSIC_BIND_CREATE_PREFIX_MISMATCH:
        return "manager_create_prefix_mismatch";
    case DLUMA_DL_MUSIC_BIND_CREATE_TYPE_TARGET_MISMATCH:
        return "manager_create_type_target_mismatch";
    case DLUMA_DL_MUSIC_BIND_STORE_PREFIX_MISMATCH:
        return "manager_store_prefix_mismatch";
    case DLUMA_DL_MUSIC_BIND_OWNER_LEVEL_EXPORT_MISMATCH:
        return "manager_owner_level_export_mismatch";
    case DLUMA_DL_MUSIC_BIND_GAMEDI_PRIMARY_RTTI_MISMATCH:
        return "gamedi_primary_rtti_mismatch";
    case DLUMA_DL_MUSIC_BIND_GAMEDI_FRAME_RTTI_MISMATCH:
        return "gamedi_frame_rtti_mismatch";
    case DLUMA_DL_MUSIC_BIND_SESSIONDI_RTTI_MISMATCH:
        return "sessiondi_primary_rtti_mismatch";
    case DLUMA_DL_MUSIC_BIND_SESSION_LEVEL_PREFIX_MISMATCH:
        return "session_level_prefix_mismatch";
    case DLUMA_DL_MUSIC_BIND_SESSION_LEVEL_SLOT_MISMATCH:
        return "session_level_slot_mismatch";
    case DLUMA_DL_MUSIC_BIND_GAMEDI_GLOBAL_PREFIX_MISMATCH:
        return "gamedi_global_prefix_mismatch";
    case DLUMA_DL_MUSIC_BIND_GAMEDI_GLOBAL_TARGET_MISMATCH:
        return "gamedi_global_target_mismatch";
    case DLUMA_DL_MUSIC_BIND_GAMEDI_GLOBAL_SLOT_OUT_OF_RANGE:
        return "gamedi_global_slot_out_of_range";
    case DLUMA_DL_MUSIC_BIND_SESSION_COOPERATIVE_RTTI_MISMATCH:
        return "session_cooperative_rtti_mismatch";
    case DLUMA_DL_MUSIC_BIND_SESSION_COOPERATIVE_DI_RTTI_MISMATCH:
        return "session_cooperative_di_rtti_mismatch";
    case DLUMA_DL_MUSIC_BIND_SESSION_COOPERATIVE_LEVEL_SLOT_MISMATCH:
        return "session_cooperative_level_slot_mismatch";
    case DLUMA_DL_MUSIC_BIND_SESSION_COOPERATIVE_DI_LEVEL_SLOT_MISMATCH:
        return "session_cooperative_di_level_slot_mismatch";
    case DLUMA_DL_MUSIC_BIND_LEVELDI_IGSOBJECT_RTTI_MISMATCH:
        return "leveldi_igsobject_rtti_mismatch";
    case DLUMA_DL_MUSIC_BIND_MANAGER_IGSOBJECT_RTTI_MISMATCH:
        return "music_manager_igsobject_rtti_mismatch";
    default:
        return "unknown_binding_failure";
    }
}

static const char* dluma_player_media_music_source_name(
    unsigned int source)
{
    switch (source) {
    case DLUMA_DL_MUSIC_SOURCE_ACTIVE_LEVEL_SINGLETON_NULL:
        return "active_level_singleton_null";
    case DLUMA_DL_MUSIC_SOURCE_ACTIVE_LEVEL_CORROBORATED:
        return "active_level_singleton_corroborated";
    case DLUMA_DL_MUSIC_SOURCE_ACTIVE_LEVEL_STALE_SINGLETON_IGNORED:
        return "active_level_stale_singleton_ignored";
    case DLUMA_DL_MUSIC_SOURCE_SINGLETON_OWNER_LEVEL_CORROBORATED:
        return "singleton_owner_level_corroborated";
    case DLUMA_DL_MUSIC_SOURCE_GAME_SESSION_LEVEL_CORROBORATED:
        return "game_session_level_corroborated";
    case DLUMA_DL_MUSIC_SOURCE_GAME_GLOBAL_SESSION_MANAGER_OWNER_CORROBORATED:
        return "game_global_session_manager_owner_corroborated";
    case DLUMA_DL_MUSIC_SOURCE_CUSTOM_FRONTEND_BACKGROUND_OWNER_CORROBORATED:
        return "custom_frontend_background_owner_corroborated";
    default:
        return "none";
    }
}

static const char* dluma_player_media_session_class_name(
    unsigned int session_class)
{
    switch (session_class) {
    case DLUMA_DL_SESSION_CLASS_SESSION_DI:
        return "SessionDI";
    case DLUMA_DL_SESSION_CLASS_SESSION_COOPERATIVE:
        return "SessionCooperative";
    case DLUMA_DL_SESSION_CLASS_SESSION_COOPERATIVE_DI:
        return "SessionCooperativeDI";
    default:
        return "none";
    }
}

static const char* dluma_vehicle_gate_reason_name(
    unsigned int reason)
{
    switch (reason) {
    case DLUMA_DL_BUGGY_GATE_READY:
        return "ready";
    case DLUMA_DL_BUGGY_GATE_STATIC_ROUTES_CHANGED:
        return "static_routes_changed";
    case DLUMA_DL_BUGGY_GATE_ROOT_SLOT_UNREADABLE:
        return "root_slot_unreadable";
    case DLUMA_DL_BUGGY_GATE_ROOT_UNAVAILABLE:
        return "root_unavailable";
    case DLUMA_DL_BUGGY_GATE_ACTOR_FIELD_UNREADABLE:
        return "local_actor_field_unreadable";
    case DLUMA_DL_BUGGY_GATE_LOCAL_PLAYER_UNAVAILABLE:
        return "local_player_unavailable";
    case DLUMA_DL_BUGGY_GATE_PLAYERDI_RTTI_INVALID:
        return "playerdi_exact_rtti_invalid";
    case DLUMA_DL_BUGGY_GATE_CURRENT_VEHICLE_CALL_FAILED:
        return "current_vehicle_call_failed";
    case DLUMA_DL_BUGGY_GATE_CURRENT_VEHICLE_UNAVAILABLE:
        return "current_vehicle_unavailable";
    case DLUMA_DL_BUGGY_GATE_CURRENT_VEHICLE_CAST_FAILED:
        return "current_vehicle_cast_failed";
    case DLUMA_DL_BUGGY_GATE_CURRENT_VEHICLE_NOT_BUGGY:
        return "current_vehicle_not_buggy";
    case DLUMA_DL_BUGGY_GATE_CAR_BUGGY_VTABLE_MISMATCH:
        return "car_buggy_exact_vtable_mismatch";
    case DLUMA_DL_BUGGY_GATE_DRIVER_QUERY_CALL_FAILED:
        return "vehicle_driver_query_call_failed";
    case DLUMA_DL_BUGGY_GATE_DRIVER_UNAVAILABLE:
        return "vehicle_driver_unavailable";
    case DLUMA_DL_BUGGY_GATE_DRIVER_OWNER_MISMATCH:
        return "vehicle_driver_owner_mismatch";
    case DLUMA_DL_BUGGY_GATE_STOCK_DRIVER_CALL_FAILED:
        return "stock_actor_is_driver_call_failed";
    case DLUMA_DL_BUGGY_GATE_STOCK_BUGGY_UNAVAILABLE:
        return "stock_buggy_unavailable";
    case DLUMA_DL_BUGGY_GATE_STOCK_BUGGY_DISAGREEMENT:
        return "stock_buggy_vehicle_disagreement";
    case DLUMA_DL_BUGGY_GATE_IDENTITY_CHANGED:
        return "transaction_identity_changed";
    case DLUMA_DL_BUGGY_GATE_VELOCITY_UNAVAILABLE:
        return "buggy_velocity_unavailable";
    case DLUMA_DL_BUGGY_GATE_VELOCITY_INVALID:
        return "buggy_velocity_invalid";
    default:
        return "unknown_buggy_gate_failure";
    }
}

static const char* dluma_vehicle_gate_route_name(
    unsigned int route)
{
    switch (route) {
    case DLUMA_DL_BUGGY_ROUTE_STOCK_AND_VEHICLE_OWNER_AGREE:
        return "stock_and_vehicle_owner_agree";
    case DLUMA_DL_BUGGY_ROUTE_VEHICLE_DRIVER_OWNER_CORROBORATED:
        return "vehicle_driver_owner_corroborated";
    default:
        return "none";
    }
}

static int dluma_vehicle_gate_reason_is_normal(
    unsigned int reason)
{
    return reason == DLUMA_DL_BUGGY_GATE_READY ||
           reason == DLUMA_DL_BUGGY_GATE_ROOT_UNAVAILABLE ||
           reason == DLUMA_DL_BUGGY_GATE_LOCAL_PLAYER_UNAVAILABLE ||
           reason == DLUMA_DL_BUGGY_GATE_CURRENT_VEHICLE_UNAVAILABLE ||
           reason == DLUMA_DL_BUGGY_GATE_CURRENT_VEHICLE_NOT_BUGGY ||
           reason == DLUMA_DL_BUGGY_GATE_DRIVER_UNAVAILABLE ||
           reason == DLUMA_DL_BUGGY_GATE_DRIVER_OWNER_MISMATCH;
}

static void dluma_player_media_publish_buggy_gate_diagnostic(
    DlumaPlayerMediaPlayerProfile* profile,
    const DlumaVehicleGateDecision* decision)
{
    int reason_changed;
    int route_changed;
    if (!profile || !decision) return;
    reason_changed =
        !profile->local_buggy_diagnostic_initialized ||
        profile->local_buggy_reason != decision->reason;
    route_changed =
        !profile->local_buggy_diagnostic_initialized ||
        profile->local_buggy_route != decision->route;
    profile->local_buggy_reason = decision->reason;
    profile->local_buggy_route = decision->route;
    profile->local_buggy_diagnostic_initialized = 1U;
    if (reason_changed)
        dluma_log_pair(
            dluma_vehicle_gate_reason_is_normal(
                decision->reason) ? "INFO" : "WARN",
            "player_media_local_buggy_state",
            dluma_vehicle_gate_reason_name(
                decision->reason));
    if (route_changed)
        dluma_log_pair(
            "INFO", "player_media_local_buggy_route",
            dluma_vehicle_gate_route_name(
                decision->route));
}

static int dluma_player_media_filter_diagnostic_should_log(
    DlumaPlayerMediaPlayerProfile* profile, unsigned int reason)
{
    if (!profile) return 0;
    if (!profile->filter_diagnostic_initialized) {
        profile->reported_filter_reason = reason;
        profile->pending_filter_reason = reason;
        profile->filter_reason_streak = 0U;
        profile->filter_diagnostic_initialized = 1U;
        return 1;
    }
    if (reason == profile->reported_filter_reason) {
        profile->pending_filter_reason = reason;
        profile->filter_reason_streak = 0U;
        return 0;
    }
    if (reason != profile->pending_filter_reason) {
        profile->pending_filter_reason = reason;
        profile->filter_reason_streak = 1U;
        return 0;
    }
    if (profile->filter_reason_streak <
            DLUMA_DL_FILTER_DIAGNOSTIC_STABLE_PROBES)
        ++profile->filter_reason_streak;
    if (profile->filter_reason_streak <
            DLUMA_DL_FILTER_DIAGNOSTIC_STABLE_PROBES)
        return 0;
    profile->reported_filter_reason = reason;
    profile->filter_reason_streak = 0U;
    return 1;
}

static void dluma_player_media_probe_live_readiness(
    DlumaPlayerMediaPlayerProfile* profile, void* observed_active_level,
    ULONGLONG now_ms)
{
    void* audio = 0;
    void* owner_level = 0;
    void* level_di = 0;
    void* manager = 0;
    DlumaPlayerMediaMusicSnapshot filter_probe;
    DlumaPlayerMediaManagerDiagnostic manager_diagnostic;
    unsigned int audio_reason = DLUMA_DL_LIVE_PROFILE_INVALID;
    unsigned int music_reason = DLUMA_DL_LIVE_PROFILE_INVALID;
    unsigned int filter_reason =
        DLUMA_DL_LIVE_FILTER_BINDING_UNAVAILABLE;
    unsigned int wave_reason = DLUMA_DL_WAVE_BIND_PROFILE_INVALID;
    unsigned int music_source = DLUMA_DL_MUSIC_SOURCE_NONE;
    unsigned int session_class = DLUMA_DL_SESSION_CLASS_NONE;
    int audio_changed;
    int music_changed;
    int filter_changed;
    int wave_changed;
    int source_changed;
    int session_class_changed;
    int manager_diagnostic_changed;
    int readiness_invariant;
    int manager_partial_state;
    int wave_ready;
    int resources_owned;
    if (!profile || !profile->exact_profile ||
        (profile->live_readiness_initialized &&
         now_ms < profile->next_live_readiness_probe_ms))
        return;
    if (!profile->music_filter_binding_ready)
        profile->music_filter_binding_ready =
            (unsigned char)dluma_player_media_bind_music_filter(profile);
    wave_ready = dluma_player_media_bind_wave_bank_detailed(
        profile, &wave_reason);
    (void)dluma_player_media_validate_audio_object_detailed(
        profile, &audio, &audio_reason);
    (void)dluma_player_media_resolve_music_manager_detailed(
        profile, observed_active_level, &owner_level, &level_di, &manager,
        &music_reason, &music_source, &session_class,
        &manager_diagnostic);
    manager_partial_state =
        owner_level != 0 ||
        level_di != 0 ||
        manager != 0 ||
        music_source != DLUMA_DL_MUSIC_SOURCE_NONE ||
        session_class != DLUMA_DL_SESSION_CLASS_NONE;
    readiness_invariant =
        (music_reason == DLUMA_DL_LIVE_READY &&
         !dluma_player_media_music_result_complete(
             music_reason, owner_level, manager, music_source)) ||
        (music_reason != DLUMA_DL_LIVE_READY &&
         manager_partial_state);
    if (readiness_invariant) {
        music_reason =
            DLUMA_DL_LIVE_READINESS_INVARIANT_VIOLATION;
        manager_diagnostic.stage = music_reason;
        owner_level = 0;
        level_di = 0;
        manager = 0;
        music_source = DLUMA_DL_MUSIC_SOURCE_NONE;
        session_class = DLUMA_DL_SESSION_CLASS_NONE;
    }
    if (readiness_invariant &&
        !profile->readiness_invariant_violated)
        dluma_log_pair(
            "ERROR", "player_media_readiness_invariant_violation",
            "partial_manager_state_cleared");
    else if (!readiness_invariant &&
             profile->readiness_invariant_violated)
        dluma_log_pair(
            "INFO", "player_media_readiness_invariant_violation",
            "cleared");
    profile->readiness_invariant_violated =
        readiness_invariant ? 1U : 0U;
    resources_owned = profile->music_owned ||
        profile->music_snapshot.valid;
    if (!resources_owned) {
        unsigned int i;
        for (i = 0; i < DLUMA_DL_PLAYER_MEDIA_MAX_WAVES; ++i) {
            if (profile->waves[i].in_use) {
                resources_owned = 1;
                break;
            }
        }
    }
    if (profile->live_readiness_initialized &&
        (profile->live_owner_level != owner_level ||
         profile->live_level_di != level_di ||
         profile->live_music_manager != manager ||
         profile->live_session_class != session_class ||
         (profile->wave_bank_ready && !wave_ready)) &&
        resources_owned &&
        !profile->published_frontend_music_manager &&
        !dluma_player_media_adapter_force_cleanup()) {
        music_reason = DLUMA_DL_LIVE_OWNER_TRANSITION_RETAINED;
        owner_level = 0;
        manager = 0;
        level_di = 0;
        music_source = DLUMA_DL_MUSIC_SOURCE_NONE;
        session_class = DLUMA_DL_SESSION_CLASS_NONE;
        manager_diagnostic.stage = music_reason;
    }
    /*
     * Live readiness is a published proof, not retained cleanup authority.
     * Even when owner-bound cleanup must be retained until process exit, a
     * failed or partial proof must withdraw every public live pointer.
     */
    profile->live_owner_level = owner_level;
    profile->live_level_di = level_di;
    profile->live_music_manager = manager;
    memset(&filter_probe, 0, sizeof(filter_probe));
    if (profile->music_filter_binding_ready &&
        audio_reason == DLUMA_DL_LIVE_READY &&
        music_reason == DLUMA_DL_LIVE_READY &&
        dluma_player_media_music_snapshot_take(
            profile, &filter_probe))
        filter_reason = DLUMA_DL_LIVE_READY;
    else if (profile->music_filter_binding_ready)
        filter_reason = DLUMA_DL_LIVE_FILTER_SNAPSHOT_UNAVAILABLE;
    memset(&filter_probe, 0, sizeof(filter_probe));
    audio_changed = !profile->live_readiness_initialized ||
        profile->live_audio_reason != audio_reason;
    music_changed = !profile->live_readiness_initialized ||
        profile->live_music_reason != music_reason;
    filter_changed =
        dluma_player_media_filter_diagnostic_should_log(
            profile, filter_reason);
    wave_changed = !profile->live_readiness_initialized ||
        profile->wave_binding_reason != wave_reason;
    source_changed = !profile->live_readiness_initialized ||
        profile->live_music_source != music_source;
    session_class_changed =
        (!profile->live_readiness_initialized &&
         session_class != DLUMA_DL_SESSION_CLASS_NONE) ||
        (profile->live_readiness_initialized &&
         profile->live_session_class != session_class);
    manager_diagnostic_changed =
        !profile->manager_diagnostic_initialized ||
        !dluma_player_media_manager_diagnostic_equal(
            &profile->manager_diagnostic,
            &manager_diagnostic);
    profile->live_audio_reason = audio_reason;
    profile->live_music_reason = music_reason;
    profile->live_filter_reason = filter_reason;
    profile->wave_binding_reason = wave_reason;
    profile->live_music_source = music_source;
    profile->live_session_class = session_class;
    profile->manager_diagnostic = manager_diagnostic;
    profile->manager_diagnostic_initialized = 1U;
    profile->wave_bank_ready = wave_ready ? 1U : 0U;
    profile->music_filter_ready =
        filter_reason == DLUMA_DL_LIVE_READY ? 1U : 0U;
    profile->live_readiness_initialized = 1U;
    profile->next_live_readiness_probe_ms =
        now_ms + DLUMA_DL_LIVE_READINESS_PROBE_MS;
    if (audio_changed)
        dluma_log_pair(
            audio_reason == DLUMA_DL_LIVE_READY ? "INFO" : "WARN",
            "player_media_wave_bank_live_ready",
            dluma_player_media_live_reason_name(audio_reason));
    if (wave_changed)
        dluma_log_pair(
            dluma_player_media_wave_binding_reason_ready(wave_reason)
                ? "INFO" : "WARN",
            "player_media_wave_bank_binding_ready",
            dluma_player_media_wave_binding_reason_name(wave_reason));
    if (music_changed)
        dluma_log_pair(
            music_reason == DLUMA_DL_LIVE_READY ? "INFO" : "WARN",
            "player_media_music_manager_handoff_ready",
            dluma_player_media_live_reason_name(music_reason));
    if (manager_diagnostic_changed) {
        dluma_log_u64(
            "INFO", "player_media_music_manager_pointer",
            (ULONGLONG)(ULONG_PTR)manager_diagnostic.manager);
        dluma_log_hex(
            "INFO", "player_media_music_manager_primary_vtable_rva",
            manager_diagnostic.primary_vtable_rva);
        dluma_log_hex(
            "INFO", "player_media_music_manager_vtable_8_rva",
            manager_diagnostic.vtable_8_rva);
        dluma_log_hex(
            "INFO", "player_media_music_manager_vtable_16_rva",
            manager_diagnostic.vtable_16_rva);
        dluma_log_hex(
            "INFO", "player_media_music_manager_vtable_48_rva",
            manager_diagnostic.vtable_48_rva);
        dluma_log_hex(
            "INFO", "player_media_music_manager_expected_primary_rva",
            DLUMA_DL_MUSIC_MANAGER_VTABLE_RVA);
        dluma_log_hex(
            "INFO", "player_media_music_manager_expected_vtable_8_rva",
            DLUMA_DL_MUSIC_MANAGER_VTABLE_8_RVA);
        dluma_log_hex(
            "INFO", "player_media_music_manager_expected_vtable_16_rva",
            DLUMA_DL_MUSIC_MANAGER_VTABLE_16_RVA);
        dluma_log_hex(
            "INFO", "player_media_music_manager_expected_vtable_48_rva",
            DLUMA_DL_MUSIC_MANAGER_VTABLE_48_RVA);
        dluma_log_pair(
            manager_diagnostic.stage == DLUMA_DL_LIVE_READY
                ? "INFO" : "WARN",
            "player_media_music_manager_validation_stage",
            dluma_player_media_live_reason_name(
                manager_diagnostic.stage));
    }
    if (source_changed && music_source != DLUMA_DL_MUSIC_SOURCE_NONE)
        dluma_log_pair(
            "INFO", "player_media_music_manager_source",
            dluma_player_media_music_source_name(music_source));
    if (session_class_changed)
        dluma_log_pair(
            session_class == DLUMA_DL_SESSION_CLASS_NONE
                ? "WARN" : "INFO",
            "player_media_game_session_class",
            dluma_player_media_session_class_name(session_class));
    if (filter_changed)
        dluma_log_pair(
            filter_reason == DLUMA_DL_LIVE_READY ? "INFO" : "WARN",
            "player_media_music_filter_ready",
            dluma_player_media_live_reason_name(filter_reason));
    if (audio_changed || music_changed || wave_changed)
        dluma_log_pair(
            dluma_player_media_wave_binding_reason_ready(wave_reason) &&
            audio_reason == DLUMA_DL_LIVE_READY &&
                    music_reason == DLUMA_DL_LIVE_READY
                ? "INFO" : "WARN",
            "player_media_audio_profile_ready",
            dluma_player_media_wave_binding_reason_ready(wave_reason) &&
            audio_reason == DLUMA_DL_LIVE_READY &&
                    music_reason == DLUMA_DL_LIVE_READY
                ? "1" : "waiting_for_live_objects");
}

static int dluma_player_media_bind_wave_bank_detailed(
    DlumaPlayerMediaPlayerProfile* profile, unsigned int* reason_out)
{
    DlumaInternalFunctionDetourEvidenceV1 detour_evidence;
    int original_prefix_matches;
    int detour_snapshot_ready = 0;
    int entry_state;
    if (reason_out)
        *reason_out = DLUMA_DL_WAVE_BIND_PROFILE_INVALID;
    if (!profile) return 0;
    memset(&detour_evidence, 0, sizeof(detour_evidence));
    original_prefix_matches = dluma_player_media_prefix_matches(
        profile->engine, profile->engine_size,
        DLUMA_DL_LOAD_WAVE_BANK_RVA,
        g_dluma_dl_load_wave_bank_prefix,
        DLUMA_ARRAY_COUNT(g_dluma_dl_load_wave_bank_prefix));
    if (!original_prefix_matches)
        detour_snapshot_ready =
            dluma_custom_map_asset_isolation_snapshot_audio_fmod_load_banks_detour(
                (BYTE*)profile->engine + DLUMA_DL_LOAD_WAVE_BANK_RVA,
                DLUMA_DL_LOAD_WAVE_BANK_RVA,
                g_dluma_dl_load_wave_bank_prefix,
                DLUMA_ARRAY_COUNT(g_dluma_dl_load_wave_bank_prefix),
                &detour_evidence);
    entry_state = dluma_internal_classify_wave_bank_entry(
        original_prefix_matches,
        detour_snapshot_ready ? &detour_evidence : 0);
    if (entry_state == DLUMA_INTERNAL_WAVE_BANK_ENTRY_INVALID) {
        if (reason_out)
            *reason_out = DLUMA_DL_WAVE_BIND_PREFIX_MISMATCH;
        return 0;
    }
    if (!dluma_player_media_relative_call_matches(
            profile->engine, profile->engine_size,
            DLUMA_DL_LOAD_WAVE_BANK_CALL_RVA,
            DLUMA_DL_SOUND_BANK_LOAD_RVA)) {
        if (reason_out)
            *reason_out = DLUMA_DL_WAVE_BIND_CALL_TARGET_MISMATCH;
        return 0;
    }
    if (!dluma_player_media_vtable_slot_matches(
            profile->engine, profile->engine_size,
            DLUMA_DL_AUDIO_VTABLE_RVA, 0x28U,
            DLUMA_DL_LOAD_WAVE_BANK_RVA)) {
        if (reason_out)
            *reason_out = DLUMA_DL_WAVE_BIND_VTABLE_SLOT_MISMATCH;
        return 0;
    }
    profile->load_wave_bank = (DlumaPlayerMediaLoadWaveBankFn)(
        (BYTE*)profile->engine + DLUMA_DL_LOAD_WAVE_BANK_RVA);
    if (reason_out)
        *reason_out =
            entry_state ==
                    DLUMA_INTERNAL_WAVE_BANK_ENTRY_LOADER_OWNED_DETOUR
                ? DLUMA_DL_WAVE_BIND_READY_LOADER_OWNED_DETOUR
                : DLUMA_DL_WAVE_BIND_READY_ORIGINAL_ENTRY;
    return 1;
}

static int dluma_player_media_bind_wave_exports(
    DlumaPlayerMediaPlayerProfile* profile)
{
    void* function = 0;
    if (!profile ||
        !dluma_player_media_engine_export(
            profile, g_dluma_dl_play_wave_2d_export,
            DLUMA_DL_PLAY_WAVE_2D_RVA,
            g_dluma_dl_play_wave_2d_prefix,
            DLUMA_ARRAY_COUNT(g_dluma_dl_play_wave_2d_prefix), &function))
        return 0;
    profile->play_wave_2d = (DlumaPlayerMediaPlayWave2DFn)function;
    if (!dluma_player_media_engine_export(
            profile, g_dluma_dl_stop_wave_export,
            DLUMA_DL_STOP_WAVE_RVA, g_dluma_dl_stop_wave_prefix,
            DLUMA_ARRAY_COUNT(g_dluma_dl_stop_wave_prefix), &function))
        return 0;
    profile->stop_wave = (DlumaPlayerMediaStopWaveFn)function;
    if (!dluma_player_media_engine_export(
            profile, g_dluma_dl_is_wave_playing_export,
            DLUMA_DL_IS_WAVE_PLAYING_RVA,
            g_dluma_dl_is_wave_playing_prefix,
            DLUMA_ARRAY_COUNT(g_dluma_dl_is_wave_playing_prefix), &function))
        return 0;
    profile->is_wave_playing =
        (DlumaPlayerMediaIsWavePlayingFn)function;
    if (!dluma_player_media_engine_export(
            profile, g_dluma_dl_set_wave_position_seconds_export,
            DLUMA_DL_SET_WAVE_POSITION_SECONDS_RVA,
            g_dluma_dl_set_wave_position_seconds_prefix,
            DLUMA_ARRAY_COUNT(
                g_dluma_dl_set_wave_position_seconds_prefix), &function))
        return 0;
    profile->set_wave_position_seconds =
        (DlumaPlayerMediaSetWavePositionFn)function;
    if (!dluma_player_media_engine_export(
            profile, g_dluma_dl_set_wave_volume_export,
            DLUMA_DL_SET_WAVE_VOLUME_RVA,
            g_dluma_dl_set_wave_volume_prefix,
            DLUMA_ARRAY_COUNT(g_dluma_dl_set_wave_volume_prefix), &function))
        return 0;
    profile->set_wave_volume =
        (DlumaPlayerMediaSetWaveVolumeFn)function;
    if (!dluma_player_media_engine_export(
            profile, g_dluma_dl_get_wave_time_seconds_export,
            DLUMA_DL_GET_WAVE_TIME_SECONDS_RVA,
            g_dluma_dl_get_wave_time_seconds_prefix,
            DLUMA_ARRAY_COUNT(
                g_dluma_dl_get_wave_time_seconds_prefix), &function))
        return 0;
    profile->get_wave_time_seconds =
        (DlumaPlayerMediaGetWaveTimeFn)function;
    if (!dluma_player_media_engine_export(
            profile, g_dluma_dl_get_wave_duration_name_export,
            DLUMA_DL_GET_WAVE_DURATION_NAME_RVA,
            g_dluma_dl_get_wave_duration_name_prefix,
            DLUMA_ARRAY_COUNT(
                g_dluma_dl_get_wave_duration_name_prefix), &function))
        return 0;
    profile->get_wave_duration_name =
        (DlumaPlayerMediaGetWaveDurationFn)function;
    return 1;
}

static int dluma_player_media_bind_frontend_musics_manager(
    DlumaPlayerMediaPlayerProfile* profile)
{
    if (!profile ||
        !dluma_player_media_prefix_matches(
            profile->gamedll, profile->gamedll_size,
            DLUMA_DL_FRONTEND_MUSICS_MANAGER_MUTE_RVA,
            g_dluma_dl_frontend_musics_manager_mute_prefix,
            DLUMA_ARRAY_COUNT(
                g_dluma_dl_frontend_musics_manager_mute_prefix)) ||
        !dluma_player_media_prefix_matches(
            profile->gamedll, profile->gamedll_size,
            DLUMA_DL_FRONTEND_MUSICS_MANAGER_UNMUTE_RVA,
            g_dluma_dl_frontend_musics_manager_unmute_prefix,
            DLUMA_ARRAY_COUNT(
                g_dluma_dl_frontend_musics_manager_unmute_prefix)) ||
        !dluma_player_media_validate_rtti_vtable(
            profile->gamedll, profile->gamedll_size,
            DLUMA_DL_FRONTEND_MUSICS_MANAGER_VTABLE_RVA,
            DLUMA_DL_FRONTEND_MUSICS_MANAGER_COL_RVA, 0U,
            DLUMA_DL_FRONTEND_MUSICS_MANAGER_TYPE_RVA,
            ".?AVManager@Musics@@"))
        return 0;
    profile->frontend_music_mute =
        (DlumaPlayerMediaFrontendMusicFn)(
            (BYTE*)profile->gamedll +
            DLUMA_DL_FRONTEND_MUSICS_MANAGER_MUTE_RVA);
    profile->frontend_music_unmute =
        (DlumaPlayerMediaFrontendMusicFn)(
            (BYTE*)profile->gamedll +
            DLUMA_DL_FRONTEND_MUSICS_MANAGER_UNMUTE_RVA);
    return 1;
}

static int dluma_player_media_bind_frontend_main_menu_music(
    DlumaPlayerMediaPlayerProfile* profile)
{
    if (!profile ||
        !dluma_player_media_prefix_matches(
            profile->gamedll, profile->gamedll_size,
            DLUMA_DL_MAIN_MENU_MODULE_PLAY_MUSIC_RVA,
            g_dluma_dl_main_menu_module_play_music_prefix,
            DLUMA_ARRAY_COUNT(
                g_dluma_dl_main_menu_module_play_music_prefix)) ||
        !dluma_player_media_prefix_matches(
            profile->gamedll, profile->gamedll_size,
            DLUMA_DL_MAIN_MENU_MODULE_STOP_MUSIC_RVA,
            g_dluma_dl_main_menu_module_stop_music_prefix,
            DLUMA_ARRAY_COUNT(
                g_dluma_dl_main_menu_module_stop_music_prefix)) ||
        !dluma_player_media_validate_rtti_vtable(
            profile->gamedll, profile->gamedll_size,
            DLUMA_DL_MAIN_MENU_MODULE_VTABLE_RVA,
            DLUMA_DL_MAIN_MENU_MODULE_COL_RVA, 0U,
            DLUMA_DL_MAIN_MENU_MODULE_TYPE_RVA,
            ".?AVMainMenuModuleDI@@"))
        return 0;
    profile->main_menu_music_start =
        (DlumaPlayerMediaMainMenuMusicFn)(
            (BYTE*)profile->gamedll +
            DLUMA_DL_MAIN_MENU_MODULE_PLAY_MUSIC_RVA);
    profile->main_menu_music_stop =
        (DlumaPlayerMediaMainMenuMusicFn)(
            (BYTE*)profile->gamedll +
            DLUMA_DL_MAIN_MENU_MODULE_STOP_MUSIC_RVA);
    return 1;
}

static int dluma_player_media_bind_music_manager_detailed(
    DlumaPlayerMediaPlayerProfile* profile, unsigned int* reason_out)
{
    const DlumaPlayerMediaSessionClassProfile* session_di =
        &g_dluma_player_media_session_classes[0];
    const DlumaPlayerMediaSessionClassProfile* session_cooperative =
        &g_dluma_player_media_session_classes[1];
    const DlumaPlayerMediaSessionClassProfile* session_cooperative_di =
        &g_dluma_player_media_session_classes[2];
    void* function = 0;
    if (reason_out)
        *reason_out = DLUMA_DL_MUSIC_BIND_PROFILE_INVALID;
    if (!profile) return 0;
    if (!dluma_player_media_prefix_matches(
            profile->gamedll, profile->gamedll_size,
            DLUMA_DL_MUSIC_MANAGER_CTOR_RVA,
            g_dluma_dl_music_manager_ctor_prefix,
            DLUMA_ARRAY_COUNT(g_dluma_dl_music_manager_ctor_prefix))) {
        if (reason_out)
            *reason_out = DLUMA_DL_MUSIC_BIND_CTOR_PREFIX_MISMATCH;
        return 0;
    }
    if (!dluma_player_media_prefix_matches(
            profile->gamedll, profile->gamedll_size,
            DLUMA_DL_MUSIC_MANAGER_STOP_RVA,
            g_dluma_dl_music_manager_stop_prefix,
            DLUMA_ARRAY_COUNT(g_dluma_dl_music_manager_stop_prefix))) {
        if (reason_out)
            *reason_out = DLUMA_DL_MUSIC_BIND_STOP_PREFIX_MISMATCH;
        return 0;
    }
    if (!dluma_player_media_prefix_matches(
            profile->gamedll, profile->gamedll_size,
            DLUMA_DL_MUSIC_MANAGER_START_RVA,
            g_dluma_dl_music_manager_start_prefix,
            DLUMA_ARRAY_COUNT(g_dluma_dl_music_manager_start_prefix))) {
        if (reason_out)
            *reason_out = DLUMA_DL_MUSIC_BIND_START_PREFIX_MISMATCH;
        return 0;
    }
    if (!dluma_player_media_prefix_matches(
            profile->gamedll, profile->gamedll_size,
            DLUMA_DL_MUSIC_MANAGER_DELAYED_START_RVA,
            g_dluma_dl_music_manager_delayed_start_prefix,
            DLUMA_ARRAY_COUNT(
                g_dluma_dl_music_manager_delayed_start_prefix))) {
        if (reason_out)
            *reason_out =
                DLUMA_DL_MUSIC_BIND_DELAYED_START_PREFIX_MISMATCH;
        return 0;
    }
    if (!dluma_player_media_prefix_matches(
            profile->gamedll, profile->gamedll_size,
            DLUMA_DL_RTDYNAMICCAST_THUNK_RVA,
            g_dluma_dl_rtdynamiccast_prefix,
            DLUMA_ARRAY_COUNT(g_dluma_dl_rtdynamiccast_prefix))) {
        if (reason_out)
            *reason_out =
                DLUMA_DL_MUSIC_BIND_RTDYNAMICCAST_PREFIX_MISMATCH;
        return 0;
    }
    if (!dluma_player_media_prefix_matches(
            profile->gamedll, profile->gamedll_size,
            DLUMA_DL_DRIVING_CONDITION_RVA,
            g_dluma_dl_driving_condition_prefix,
            DLUMA_ARRAY_COUNT(g_dluma_dl_driving_condition_prefix))) {
        if (reason_out)
            *reason_out =
                DLUMA_DL_MUSIC_BIND_GAMEDI_GLOBAL_PREFIX_MISMATCH;
        return 0;
    }
    if (!dluma_player_media_rip_relative_matches(
            profile->gamedll, profile->gamedll_size,
            DLUMA_DL_DRIVING_CONDITION_RVA + 4U,
            3U, 7U, DLUMA_DL_GAMEDI_GLOBAL_RVA)) {
        if (reason_out)
            *reason_out =
                DLUMA_DL_MUSIC_BIND_GAMEDI_GLOBAL_TARGET_MISMATCH;
        return 0;
    }
    if (!dluma_player_media_module_range(
            profile->gamedll, profile->gamedll_size,
            (BYTE*)profile->gamedll + DLUMA_DL_GAMEDI_GLOBAL_RVA,
            sizeof(void*))) {
        if (reason_out)
            *reason_out =
                DLUMA_DL_MUSIC_BIND_GAMEDI_GLOBAL_SLOT_OUT_OF_RANGE;
        return 0;
    }
    if (!dluma_player_media_exact_text(
            profile->gamedll, profile->gamedll_size,
            DLUMA_DL_ILEVEL_TYPE_RVA + 16U, ".?AVILevel@@")) {
        if (reason_out)
            *reason_out =
                DLUMA_DL_MUSIC_BIND_ILEVEL_TYPE_NAME_MISMATCH;
        return 0;
    }
    if (!dluma_player_media_exact_text(
            profile->gamedll, profile->gamedll_size,
            DLUMA_DL_LEVELDI_TYPE_RVA + 16U, ".?AVLevelDI@@")) {
        if (reason_out)
            *reason_out =
                DLUMA_DL_MUSIC_BIND_LEVELDI_TYPE_NAME_MISMATCH;
        return 0;
    }
    if (!dluma_player_media_validate_rtti_vtable(
            profile->gamedll, profile->gamedll_size,
            DLUMA_DL_LEVELDI_VTABLE_RVA,
            DLUMA_DL_LEVELDI_COL_RVA, 0U,
            DLUMA_DL_LEVELDI_TYPE_RVA, ".?AVLevelDI@@")) {
        if (reason_out)
            *reason_out =
                DLUMA_DL_MUSIC_BIND_LEVELDI_PRIMARY_RTTI_MISMATCH;
        return 0;
    }
    if (!dluma_player_media_validate_igsobject_route_metadata(
            profile->gamedll, profile->gamedll_size,
            DLUMA_DL_LEVELDI_IGSOBJECT_VTABLE_RVA,
            DLUMA_DL_LEVELDI_IGSOBJECT_COL_RVA,
            DLUMA_DL_LEVELDI_IGSOBJECT_OFFSET,
            DLUMA_DL_LEVELDI_TYPE_RVA, ".?AVLevelDI@@",
            DLUMA_DL_LEVELDI_HIERARCHY_RVA, 5U, 13U,
            DLUMA_DL_LEVELDI_BASE_ARRAY_RVA, 4U,
            DLUMA_DL_LEVELDI_IGSOBJECT_BASE_DESCRIPTOR_RVA,
            (LONG)DLUMA_DL_LEVELDI_IGSOBJECT_OFFSET)) {
        if (reason_out)
            *reason_out =
                DLUMA_DL_MUSIC_BIND_LEVELDI_IGSOBJECT_RTTI_MISMATCH;
        return 0;
    }
    if (!dluma_player_media_validate_igsobject_route_metadata(
            profile->gamedll, profile->gamedll_size,
            DLUMA_DL_MUSIC_MANAGER_VTABLE_RVA,
            DLUMA_DL_MUSIC_MANAGER_COL_RVA, 0U,
            DLUMA_DL_MUSIC_MANAGER_TYPE_RVA,
            ".?AVMusicManager@@",
            DLUMA_DL_MUSIC_MANAGER_HIERARCHY_RVA, 1U, 8U,
            DLUMA_DL_MUSIC_MANAGER_BASE_ARRAY_RVA, 2U,
            DLUMA_DL_MUSIC_MANAGER_IGSOBJECT_BASE_DESCRIPTOR_RVA,
            0)) {
        if (reason_out)
            *reason_out =
                DLUMA_DL_MUSIC_BIND_MANAGER_IGSOBJECT_RTTI_MISMATCH;
        return 0;
    }
    if (!dluma_player_media_validate_rtti_vtable(
            profile->gamedll, profile->gamedll_size,
            DLUMA_DL_GAMEDI_PRIMARY_VTABLE_RVA,
            DLUMA_DL_GAMEDI_PRIMARY_COL_RVA, 0U,
            DLUMA_DL_GAMEDI_TYPE_RVA, ".?AVGameDI@@")) {
        if (reason_out)
            *reason_out =
                DLUMA_DL_MUSIC_BIND_GAMEDI_PRIMARY_RTTI_MISMATCH;
        return 0;
    }
    if (!dluma_player_media_validate_rtti_vtable(
            profile->gamedll, profile->gamedll_size,
            DLUMA_DL_GAMEDI_FRAME_VTABLE_RVA,
            DLUMA_DL_GAMEDI_FRAME_COL_RVA,
            DLUMA_DL_GAMEDI_FRAME_SUBOBJECT_OFFSET,
            DLUMA_DL_GAMEDI_TYPE_RVA, ".?AVGameDI@@")) {
        if (reason_out)
            *reason_out =
                DLUMA_DL_MUSIC_BIND_GAMEDI_FRAME_RTTI_MISMATCH;
        return 0;
    }
    if (!dluma_player_media_validate_session_profile_metadata(
            profile->gamedll, profile->gamedll_size,
            session_di)) {
        if (reason_out)
            *reason_out =
                DLUMA_DL_MUSIC_BIND_SESSIONDI_RTTI_MISMATCH;
        return 0;
    }
    if (!dluma_player_media_validate_session_profile_metadata(
            profile->gamedll, profile->gamedll_size,
            session_cooperative)) {
        if (reason_out)
            *reason_out =
                DLUMA_DL_MUSIC_BIND_SESSION_COOPERATIVE_RTTI_MISMATCH;
        return 0;
    }
    if (!dluma_player_media_validate_session_profile_metadata(
            profile->gamedll, profile->gamedll_size,
            session_cooperative_di)) {
        if (reason_out)
            *reason_out =
                DLUMA_DL_MUSIC_BIND_SESSION_COOPERATIVE_DI_RTTI_MISMATCH;
        return 0;
    }
    if (!dluma_player_media_prefix_matches(
            profile->gamedll, profile->gamedll_size,
            DLUMA_DL_SESSION_LEVEL_RVA,
            g_dluma_dl_session_level_prefix,
            DLUMA_ARRAY_COUNT(g_dluma_dl_session_level_prefix))) {
        if (reason_out)
            *reason_out =
                DLUMA_DL_MUSIC_BIND_SESSION_LEVEL_PREFIX_MISMATCH;
        return 0;
    }
    if (!dluma_player_media_vtable_slot_matches(
            profile->gamedll, profile->gamedll_size,
            session_di->vtable_rva,
            DLUMA_DL_SESSION_LEVEL_SLOT,
            DLUMA_DL_SESSION_LEVEL_RVA)) {
        if (reason_out)
            *reason_out =
                DLUMA_DL_MUSIC_BIND_SESSION_LEVEL_SLOT_MISMATCH;
        return 0;
    }
    if (!dluma_player_media_vtable_slot_matches(
            profile->gamedll, profile->gamedll_size,
            session_cooperative->vtable_rva,
            DLUMA_DL_SESSION_LEVEL_SLOT,
            DLUMA_DL_SESSION_LEVEL_RVA)) {
        if (reason_out)
            *reason_out =
                DLUMA_DL_MUSIC_BIND_SESSION_COOPERATIVE_LEVEL_SLOT_MISMATCH;
        return 0;
    }
    if (!dluma_player_media_vtable_slot_matches(
            profile->gamedll, profile->gamedll_size,
            session_cooperative_di->vtable_rva,
            DLUMA_DL_SESSION_LEVEL_SLOT,
            DLUMA_DL_SESSION_LEVEL_RVA)) {
        if (reason_out)
            *reason_out =
                DLUMA_DL_MUSIC_BIND_SESSION_COOPERATIVE_DI_LEVEL_SLOT_MISMATCH;
        return 0;
    }
    if (!dluma_player_media_prefix_matches(
            profile->gamedll, profile->gamedll_size,
            DLUMA_DL_LEVELDI_MUSIC_CREATE_EVIDENCE_RVA,
            g_dluma_dl_leveldi_music_create_evidence_prefix,
            DLUMA_ARRAY_COUNT(
                g_dluma_dl_leveldi_music_create_evidence_prefix))) {
        if (reason_out)
            *reason_out =
                DLUMA_DL_MUSIC_BIND_CREATE_PREFIX_MISMATCH;
        return 0;
    }
    if (!dluma_player_media_rip_relative_matches(
            profile->gamedll, profile->gamedll_size,
            DLUMA_DL_LEVELDI_MUSIC_RTTI_LEA_RVA,
            3U, 7U, DLUMA_DL_MUSIC_MANAGER_CREATE_TYPE_RVA)) {
        if (reason_out)
            *reason_out =
                DLUMA_DL_MUSIC_BIND_CREATE_TYPE_TARGET_MISMATCH;
        return 0;
    }
    if (!dluma_player_media_prefix_matches(
            profile->gamedll, profile->gamedll_size,
            DLUMA_DL_LEVELDI_MUSIC_STORE_RVA,
            g_dluma_dl_leveldi_music_store_prefix,
            DLUMA_ARRAY_COUNT(
                g_dluma_dl_leveldi_music_store_prefix))) {
        if (reason_out)
            *reason_out =
                DLUMA_DL_MUSIC_BIND_STORE_PREFIX_MISMATCH;
        return 0;
    }
    if (!dluma_player_media_engine_export(
            profile, g_dluma_dl_get_object_level_export,
            DLUMA_DL_IGSOBJECT_GET_LEVEL_RVA,
            g_dluma_dl_get_object_level_prefix,
            DLUMA_ARRAY_COUNT(g_dluma_dl_get_object_level_prefix),
            &function)) {
        if (reason_out)
            *reason_out =
                DLUMA_DL_MUSIC_BIND_OWNER_LEVEL_EXPORT_MISMATCH;
        return 0;
    }
    profile->rt_dynamic_cast = (DlumaPlayerMediaRtDynamicCastFn)(
        (BYTE*)profile->gamedll + DLUMA_DL_RTDYNAMICCAST_THUNK_RVA);
    profile->get_object_level =
        (DlumaPlayerMediaObjectLevelFn)function;
    profile->get_session_level =
        (DlumaPlayerMediaSessionLevelFn)(
            (BYTE*)profile->gamedll + DLUMA_DL_SESSION_LEVEL_RVA);
    profile->music_stop = (DlumaPlayerMediaMusicStopFn)(
        (BYTE*)profile->gamedll + DLUMA_DL_MUSIC_MANAGER_STOP_RVA);
    profile->music_start = (DlumaPlayerMediaMusicStartFn)(
        (BYTE*)profile->gamedll + DLUMA_DL_MUSIC_MANAGER_START_RVA);
    if (reason_out) *reason_out = DLUMA_DL_MUSIC_BIND_READY;
    return 1;
}

static int dluma_player_media_validate_playerdi_metadata(
    DlumaPlayerMediaPlayerProfile* profile)
{
    DlumaPlayerMediaCompleteObjectLocator* primary_col;
    DlumaPlayerMediaCompleteObjectLocator* actor_col;
    DWORD* player_bases;
    DWORD* actor_bases;
    void** primary_vtable;
    void** actor_vtable;
    if (!profile ||
        !dluma_player_media_validate_rtti_vtable(
            profile->gamedll, profile->gamedll_size,
            DLUMA_DL_PLAYERDI_PRIMARY_VTABLE_RVA,
            DLUMA_DL_PLAYERDI_PRIMARY_COL_RVA, 0U,
            DLUMA_DL_PLAYERDI_TYPE_RVA, ".?AVPlayerDI@@") ||
        !dluma_player_media_validate_rtti_vtable(
            profile->gamedll, profile->gamedll_size,
            DLUMA_DL_PLAYERDI_ACTOR_VTABLE_RVA,
            DLUMA_DL_PLAYERDI_ACTOR_COL_RVA,
            DLUMA_DL_PLAYERDI_ACTOR_SUBOBJECT_OFFSET,
            DLUMA_DL_PLAYERDI_TYPE_RVA, ".?AVPlayerDI@@") ||
        !dluma_player_media_hierarchy_matches(
            profile->gamedll, profile->gamedll_size,
            DLUMA_DL_PLAYERDI_HIERARCHY_RVA, 5U,
            DLUMA_DL_PLAYERDI_BASE_COUNT,
            DLUMA_DL_PLAYERDI_BASE_ARRAY_RVA) ||
        !dluma_player_media_hierarchy_matches(
            profile->gamedll, profile->gamedll_size,
            DLUMA_DL_ACTORDI_HIERARCHY_RVA, 5U,
            DLUMA_DL_ACTORDI_BASE_COUNT,
            DLUMA_DL_ACTORDI_BASE_ARRAY_RVA) ||
        !dluma_player_media_base_descriptor_matches(
            profile->gamedll, profile->gamedll_size,
            DLUMA_DL_PLAYERDI_BASE_DESCRIPTOR_RVA,
            DLUMA_DL_PLAYERDI_TYPE_RVA, ".?AVPlayerDI@@",
            DLUMA_DL_PLAYERDI_BASE_COUNT - 1U,
            DLUMA_DL_PLAYERDI_HIERARCHY_RVA) ||
        !dluma_player_media_base_descriptor_matches(
            profile->gamedll, profile->gamedll_size,
            DLUMA_DL_ACTORDI_BASE_DESCRIPTOR_RVA,
            DLUMA_DL_ACTORDI_TYPE_RVA, ".?AVActorDI@@",
            DLUMA_DL_ACTORDI_BASE_COUNT - 1U,
            DLUMA_DL_ACTORDI_HIERARCHY_RVA) ||
        !dluma_player_media_vtable_slot_matches(
            profile->gamedll, profile->gamedll_size,
            DLUMA_DL_PLAYERDI_PRIMARY_VTABLE_RVA,
            DLUMA_DL_PLAYERDI_CURRENT_VEHICLE_SLOT,
            DLUMA_DL_PLAYERDI_CURRENT_VEHICLE_RVA) ||
        !dluma_player_media_vtable_slot_matches(
            profile->gamedll, profile->gamedll_size,
            DLUMA_DL_PLAYERDI_ACTOR_VTABLE_RVA,
            DLUMA_DL_PLAYERDI_LOCAL_PREDICATE_SLOT,
            DLUMA_DL_PLAYERDI_LOCAL_PREDICATE_RVA) ||
        !dluma_player_media_prefix_matches(
            profile->gamedll, profile->gamedll_size,
            DLUMA_DL_PLAYERDI_CURRENT_VEHICLE_RVA,
            g_dluma_dl_playerdi_current_vehicle_prefix,
            DLUMA_ARRAY_COUNT(
                g_dluma_dl_playerdi_current_vehicle_prefix)) ||
        !dluma_player_media_prefix_matches(
            profile->gamedll, profile->gamedll_size,
            DLUMA_DL_PLAYERDI_LOCAL_PREDICATE_RVA,
            g_dluma_dl_playerdi_local_predicate_prefix,
            DLUMA_ARRAY_COUNT(
                g_dluma_dl_playerdi_local_predicate_prefix)))
        return 0;
    primary_vtable = (void**)((BYTE*)profile->gamedll +
                              DLUMA_DL_PLAYERDI_PRIMARY_VTABLE_RVA);
    actor_vtable = (void**)((BYTE*)profile->gamedll +
                            DLUMA_DL_PLAYERDI_ACTOR_VTABLE_RVA);
    primary_col = (DlumaPlayerMediaCompleteObjectLocator*)
        primary_vtable[-1];
    actor_col = (DlumaPlayerMediaCompleteObjectLocator*)
        actor_vtable[-1];
    if (primary_col->class_hierarchy_rva !=
            DLUMA_DL_PLAYERDI_HIERARCHY_RVA ||
        actor_col->class_hierarchy_rva !=
            DLUMA_DL_PLAYERDI_HIERARCHY_RVA)
        return 0;
    player_bases = (DWORD*)((BYTE*)profile->gamedll +
                            DLUMA_DL_PLAYERDI_BASE_ARRAY_RVA);
    actor_bases = (DWORD*)((BYTE*)profile->gamedll +
                           DLUMA_DL_ACTORDI_BASE_ARRAY_RVA);
    return player_bases[0] ==
               DLUMA_DL_PLAYERDI_BASE_DESCRIPTOR_RVA &&
           player_bases[1] ==
               DLUMA_DL_ACTORDI_BASE_DESCRIPTOR_RVA &&
           actor_bases[0] ==
               DLUMA_DL_ACTORDI_BASE_DESCRIPTOR_RVA;
}

static int dluma_player_media_validate_car_buggy_metadata(
    DlumaPlayerMediaPlayerProfile* profile)
{
    DlumaPlayerMediaCompleteObjectLocator* primary_col;
    DlumaPlayerMediaCompleteObjectLocator* control_col;
    DWORD* car_bases;
    DWORD* vehicle_bases;
    void** primary_vtable;
    void** control_vtable;
    if (!profile ||
        !dluma_player_media_validate_rtti_vtable(
            profile->gamedll, profile->gamedll_size,
            DLUMA_DL_CAR_BUGGY_VTABLE_RVA,
            DLUMA_DL_CAR_BUGGY_COL_RVA, 0U,
            DLUMA_DL_CAR_BUGGY_TYPE_RVA, ".?AVCarBuggy@@") ||
        !dluma_player_media_validate_rtti_vtable(
            profile->gamedll, profile->gamedll_size,
            DLUMA_DL_CAR_BUGGY_CONTROL_VTABLE_RVA,
            DLUMA_DL_CAR_BUGGY_CONTROL_COL_RVA, 0x18U,
            DLUMA_DL_CAR_BUGGY_TYPE_RVA, ".?AVCarBuggy@@") ||
        !dluma_player_media_hierarchy_matches(
            profile->gamedll, profile->gamedll_size,
            DLUMA_DL_CAR_BUGGY_HIERARCHY_RVA, 1U,
            DLUMA_DL_CAR_BUGGY_BASE_COUNT,
            DLUMA_DL_CAR_BUGGY_BASE_ARRAY_RVA) ||
        !dluma_player_media_hierarchy_matches(
            profile->gamedll, profile->gamedll_size,
            DLUMA_DL_VEHICLE_BASE_HIERARCHY_RVA, 5U,
            DLUMA_DL_VEHICLE_BASE_COUNT,
            DLUMA_DL_VEHICLE_BASE_ARRAY_RVA) ||
        !dluma_player_media_base_descriptor_matches(
            profile->gamedll, profile->gamedll_size,
            DLUMA_DL_CAR_BUGGY_BASE_DESCRIPTOR_RVA,
            DLUMA_DL_CAR_BUGGY_TYPE_RVA, ".?AVCarBuggy@@",
            DLUMA_DL_CAR_BUGGY_BASE_COUNT - 1U,
            DLUMA_DL_CAR_BUGGY_HIERARCHY_RVA) ||
        !dluma_player_media_base_descriptor_matches(
            profile->gamedll, profile->gamedll_size,
            DLUMA_DL_VEHICLE_BASE_DESCRIPTOR_RVA,
            DLUMA_DL_VEHICLE_BASE_TYPE_RVA, ".?AVVehicleBase@@",
            DLUMA_DL_VEHICLE_BASE_COUNT - 1U,
            DLUMA_DL_VEHICLE_BASE_HIERARCHY_RVA) ||
        !dluma_player_media_vtable_slot_matches(
            profile->gamedll, profile->gamedll_size,
            DLUMA_DL_CAR_BUGGY_VTABLE_RVA,
            DLUMA_DL_CAR_BUGGY_LINEAR_VELOCITY_SLOT,
            DLUMA_DL_CAR_BUGGY_LINEAR_VELOCITY_RVA) ||
        !dluma_player_media_prefix_matches(
            profile->gamedll, profile->gamedll_size,
            DLUMA_DL_CAR_BUGGY_LINEAR_VELOCITY_RVA,
            g_dluma_dl_car_buggy_linear_velocity,
            DLUMA_ARRAY_COUNT(
                g_dluma_dl_car_buggy_linear_velocity)) ||
        !dluma_player_media_import_slot_matches(
            profile->gamedll, profile->gamedll_size,
            DLUMA_DL_CAR_BUGGY_PHYSICS_BIND_IAT_RVA,
            profile->engine, profile->engine_size,
            DLUMA_DL_PHYSICS_BIND_RVA) ||
        !dluma_player_media_import_slot_matches(
            profile->gamedll, profile->gamedll_size,
            DLUMA_DL_CAR_BUGGY_LINEAR_VELOCITY_IAT_RVA,
            profile->engine, profile->engine_size,
            DLUMA_DL_LINEAR_VELOCITY_RVA))
        return 0;
    primary_vtable = (void**)((BYTE*)profile->gamedll +
                              DLUMA_DL_CAR_BUGGY_VTABLE_RVA);
    control_vtable = (void**)((BYTE*)profile->gamedll +
                              DLUMA_DL_CAR_BUGGY_CONTROL_VTABLE_RVA);
    primary_col = (DlumaPlayerMediaCompleteObjectLocator*)
        primary_vtable[-1];
    control_col = (DlumaPlayerMediaCompleteObjectLocator*)
        control_vtable[-1];
    if (primary_col->class_hierarchy_rva !=
            DLUMA_DL_CAR_BUGGY_HIERARCHY_RVA ||
        control_col->class_hierarchy_rva !=
            DLUMA_DL_CAR_BUGGY_HIERARCHY_RVA)
        return 0;
    car_bases = (DWORD*)((BYTE*)profile->gamedll +
                         DLUMA_DL_CAR_BUGGY_BASE_ARRAY_RVA);
    vehicle_bases = (DWORD*)((BYTE*)profile->gamedll +
                             DLUMA_DL_VEHICLE_BASE_ARRAY_RVA);
    return car_bases[0] ==
               DLUMA_DL_CAR_BUGGY_BASE_DESCRIPTOR_RVA &&
           car_bases[1] ==
               DLUMA_DL_VEHICLE_BASE_DESCRIPTOR_RVA &&
           vehicle_bases[0] ==
               DLUMA_DL_VEHICLE_BASE_DESCRIPTOR_RVA;
}

static int dluma_player_media_validate_local_buggy_game_routes(
    DlumaPlayerMediaPlayerProfile* profile)
{
    return profile &&
        dluma_player_media_prefix_matches(
            profile->gamedll, profile->gamedll_size,
            DLUMA_DL_LOCAL_BUGGY_RVA, g_dluma_dl_local_buggy_prefix,
            DLUMA_ARRAY_COUNT(g_dluma_dl_local_buggy_prefix)) &&
        dluma_player_media_prefix_matches(
            profile->gamedll, profile->gamedll_size,
            DLUMA_DL_ACTOR_IS_DRIVER_RVA, g_dluma_dl_actor_is_driver_prefix,
            DLUMA_ARRAY_COUNT(g_dluma_dl_actor_is_driver_prefix)) &&
        dluma_player_media_prefix_matches(
            profile->gamedll, profile->gamedll_size,
            DLUMA_DL_RTDYNAMICCAST_THUNK_RVA,
            g_dluma_dl_rtdynamiccast_prefix,
            DLUMA_ARRAY_COUNT(g_dluma_dl_rtdynamiccast_prefix)) &&
        dluma_player_media_prefix_matches(
            profile->gamedll, profile->gamedll_size,
            DLUMA_DL_VEHICLE_BASE_GET_DRIVER_RVA,
            g_dluma_dl_vehicle_get_driver_prefix,
            DLUMA_ARRAY_COUNT(g_dluma_dl_vehicle_get_driver_prefix)) &&
        dluma_player_media_relative_call_matches(
            profile->gamedll, profile->gamedll_size,
            DLUMA_DL_LOCAL_BUGGY_RVA + 0x21U,
            DLUMA_DL_ACTOR_IS_DRIVER_RVA) &&
        dluma_player_media_relative_call_matches(
            profile->gamedll, profile->gamedll_size,
            DLUMA_DL_LOCAL_BUGGY_RVA + 0x51U,
            DLUMA_DL_RTDYNAMICCAST_THUNK_RVA) &&
        dluma_player_media_relative_call_matches(
            profile->gamedll, profile->gamedll_size,
            DLUMA_DL_VEHICLE_DRIVER_OWNER_CALLSITE_RVA,
            DLUMA_DL_VEHICLE_BASE_GET_DRIVER_RVA) &&
        dluma_player_media_relative_call_matches(
            profile->gamedll, profile->gamedll_size,
            DLUMA_DL_LOCAL_DRIVER_QUERY_CALLSITE_RVA,
            DLUMA_DL_VEHICLE_BASE_GET_DRIVER_RVA) &&
        dluma_player_media_module_range(
            profile->gamedll, profile->gamedll_size,
            (BYTE*)profile->gamedll + DLUMA_DL_LOCAL_ROOT_SLOT_RVA,
            sizeof(void*)) &&
        dluma_player_media_validate_playerdi_metadata(profile) &&
        dluma_player_media_validate_car_buggy_metadata(profile);
}

static int dluma_player_media_bind_local_buggy(
    DlumaPlayerMediaPlayerProfile* profile)
{
    if (!profile ||
        !dluma_player_media_validate_local_buggy_game_routes(profile) ||
        !dluma_player_media_prefix_matches(
            profile->engine, profile->engine_size,
            DLUMA_DL_PHYSICS_BIND_RVA, g_dluma_dl_physics_bind_prefix,
            DLUMA_ARRAY_COUNT(g_dluma_dl_physics_bind_prefix)) ||
        !dluma_player_media_validate_rtti_vtable(
            profile->engine, profile->engine_size,
            DLUMA_DL_PHYSICS_BIND_VTABLE_RVA, 0U, 0U,
            DLUMA_DL_PHYSICS_BIND_TYPE_RVA, ".?AVCPhysicsBind@@") ||
        !dluma_player_media_vtable_slot_matches(
            profile->engine, profile->engine_size,
            DLUMA_DL_PHYSICS_BIND_VTABLE_RVA, 0xA0U,
            DLUMA_DL_PHYSICS_INTERFACE_RVA) ||
        !dluma_player_media_prefix_matches(
            profile->engine, profile->engine_size,
            DLUMA_DL_PHYSICS_INTERFACE_RVA,
            g_dluma_dl_physics_interface_prefix,
            DLUMA_ARRAY_COUNT(g_dluma_dl_physics_interface_prefix)) ||
        !dluma_player_media_prefix_matches(
            profile->engine, profile->engine_size,
            DLUMA_DL_PHYSICS_BODY_AT_RVA,
            g_dluma_dl_physics_body_at_prefix,
            DLUMA_ARRAY_COUNT(g_dluma_dl_physics_body_at_prefix)) ||
        !dluma_player_media_prefix_matches(
            profile->engine, profile->engine_size,
            DLUMA_DL_LINEAR_VELOCITY_RVA,
            g_dluma_dl_linear_velocity_prefix,
            DLUMA_ARRAY_COUNT(g_dluma_dl_linear_velocity_prefix)))
        return 0;
    profile->local_buggy = (DlumaPlayerMediaLocalBuggyFn)(
        (BYTE*)profile->gamedll + DLUMA_DL_LOCAL_BUGGY_RVA);
    profile->actor_is_driver = (DlumaPlayerMediaActorIsDriverFn)(
        (BYTE*)profile->gamedll + DLUMA_DL_ACTOR_IS_DRIVER_RVA);
    profile->rt_dynamic_cast = (DlumaPlayerMediaRtDynamicCastFn)(
        (BYTE*)profile->gamedll + DLUMA_DL_RTDYNAMICCAST_THUNK_RVA);
    profile->current_vehicle = (DlumaPlayerMediaCurrentVehicleFn)(
        (BYTE*)profile->gamedll +
        DLUMA_DL_PLAYERDI_CURRENT_VEHICLE_RVA);
    profile->vehicle_driver = (DlumaPlayerMediaVehicleDriverFn)(
        (BYTE*)profile->gamedll +
        DLUMA_DL_VEHICLE_BASE_GET_DRIVER_RVA);
    profile->vehicle_velocity =
        (DlumaPlayerMediaVehicleVelocityFn)(
            (BYTE*)profile->gamedll +
            DLUMA_DL_CAR_BUGGY_LINEAR_VELOCITY_RVA);
    return 1;
}

static int dluma_player_media_validate_playerdi(
    DlumaPlayerMediaPlayerProfile* profile, void* actor)
{
    void* actor_interface;
    if (!profile || !actor || !dluma_is_readable(actor, 0xC08U) ||
        *(void**)actor !=
            (BYTE*)profile->gamedll +
                DLUMA_DL_PLAYERDI_PRIMARY_VTABLE_RVA ||
        !dluma_player_media_validate_live_rtti_object(
            profile->gamedll, profile->gamedll_size, actor,
            DLUMA_DL_PLAYERDI_TYPE_RVA, ".?AVPlayerDI@@") ||
        !dluma_player_media_live_vtable_in_module(
            actor, profile->gamedll, profile->gamedll_size,
            DLUMA_DL_PLAYERDI_CURRENT_VEHICLE_SLOT,
            DLUMA_DL_PLAYERDI_CURRENT_VEHICLE_RVA))
        return 0;
    actor_interface = (BYTE*)actor + 0x28U;
    return *(void**)actor_interface ==
               (BYTE*)profile->gamedll +
                   DLUMA_DL_PLAYERDI_ACTOR_VTABLE_RVA &&
           dluma_player_media_live_vtable_in_module(
               actor_interface, profile->gamedll,
               profile->gamedll_size,
               DLUMA_DL_PLAYERDI_LOCAL_PREDICATE_SLOT,
               DLUMA_DL_PLAYERDI_LOCAL_PREDICATE_RVA);
}

/* Exact DevTools Player gamedll cbc7dd79...: GetHealth, computed GetMaxHealth,
 * IsActingAsDead and its virtual IsDead dependency. See the player actor audit.
 * Keep these reads independent of vehicle availability and raw PlayerVariables. */
#define DLUMA_DL_PLAYER_HEALTH_RVA 0x00969DB0UL
#define DLUMA_DL_PLAYER_MAX_HEALTH_RVA 0x00B17CC0UL
#define DLUMA_DL_PLAYER_ACTING_DEAD_RVA 0x00969D20UL
#define DLUMA_DL_PLAYER_DEAD_RVA 0x00B41A30UL

int dluma_player_actor_binding_vitals_ready(void)
{
    DlumaPlayerMediaPlayerProfile* profile = &g_dluma_player_media_player;
    static const BYTE health[] = {0xF3,0x0F,0x10,0x81,0x9C,0x12,0,0,0xC3};
    static const BYTE maximum[] = {
        0x48,0x89,0x5C,0x24,0x08,0x57,0x48,0x83,0xEC,0x20,0x48,0x8B,0x01,
        0x48,0x8B,0xF9,0xBB,0xBE,0x02,0,0,0xFF,0x90,0x78,0x08,0,0};
    static const BYTE acting_dead[] = {
        0x40,0x53,0x48,0x83,0xEC,0x20,0x48,0x8B,0x01,0x48,0x8B,0xD9,
        0xFF,0x90,0x28,0x01,0,0};
    static const BYTE dead[] = {
        0x48,0x83,0xB9,0x70,0x2F,0,0,0,0x74,0x0A,
        0x48,0x83,0xB9,0x78,0x2F,0,0,0,0x75,0x26};
    return profile->exact_profile &&
        dluma_player_media_prefix_matches(profile->gamedll, profile->gamedll_size,
            DLUMA_DL_PLAYER_HEALTH_RVA, health, sizeof(health)) &&
        dluma_player_media_prefix_matches(profile->gamedll, profile->gamedll_size,
            DLUMA_DL_PLAYER_MAX_HEALTH_RVA, maximum, sizeof(maximum)) &&
        dluma_player_media_prefix_matches(profile->gamedll, profile->gamedll_size,
            DLUMA_DL_PLAYER_ACTING_DEAD_RVA, acting_dead, sizeof(acting_dead)) &&
        dluma_player_media_prefix_matches(profile->gamedll, profile->gamedll_size,
            DLUMA_DL_PLAYER_DEAD_RVA, dead, sizeof(dead));
}

int dluma_player_actor_binding_read_vitals(DlumaPlayerVitalsSnapshot* snapshot)
{
    typedef float (__fastcall *DlumaPlayerFloatGetter)(void*);
    typedef unsigned char (__fastcall *DlumaPlayerDeadGetter)(void*);
    DlumaPlayerMediaPlayerProfile* profile = &g_dluma_player_media_player;
    DlumaPlayerVitalsSnapshot value = {0};
    void** root_slot;
    void* root;
    void* actor;
    void* session_level = 0;
    void* current_level = 0;
    int valid = 0;
    if (!snapshot) return 0;
    memset(snapshot, 0, sizeof(*snapshot));
    if (!dluma_engine_game_frame_dispatch_active_on_current_thread(&g_dluma.engine) ||
        !dluma_player_actor_binding_vitals_ready()) return 0;
    /* Re-resolve the owner on every call. No PlayerDI pointer survives a frame,
     * respawn, level transition, or teardown through this interface. */
    __try {
        if (!dluma_player_media_adapter_get_session_level(&session_level)) __leave;
        root_slot = (void**)((BYTE*)profile->gamedll + DLUMA_DL_LOCAL_ROOT_SLOT_RVA);
        if (!dluma_is_readable(root_slot, sizeof(*root_slot))) __leave;
        root = *root_slot;
        if (!root || !dluma_is_readable((BYTE*)root + 0x780U, sizeof(void*))) __leave;
        actor = *(void**)((BYTE*)root + 0x780U);
        if (!dluma_player_media_validate_playerdi(profile, actor) ||
            !dluma_player_media_live_vtable_in_module(actor, profile->gamedll,
                profile->gamedll_size, 0x638U, DLUMA_DL_PLAYER_HEALTH_RVA) ||
            !dluma_player_media_live_vtable_in_module(actor, profile->gamedll,
                profile->gamedll_size, 0x148U, DLUMA_DL_PLAYER_MAX_HEALTH_RVA) ||
            !dluma_player_media_live_vtable_in_module(actor, profile->gamedll,
                profile->gamedll_size, 0x128U, DLUMA_DL_PLAYER_DEAD_RVA)) __leave;
        value.health = ((DlumaPlayerFloatGetter)((BYTE*)profile->gamedll +
            DLUMA_DL_PLAYER_HEALTH_RVA))(actor);
        value.max_health = ((DlumaPlayerFloatGetter)((BYTE*)profile->gamedll +
            DLUMA_DL_PLAYER_MAX_HEALTH_RVA))(actor);
        value.alive = ((DlumaPlayerDeadGetter)((BYTE*)profile->gamedll +
            DLUMA_DL_PLAYER_ACTING_DEAD_RVA))(actor) ? 0U : 1U;
        if (!dluma_player_media_float_finite(value.health) ||
            !dluma_player_media_float_finite(value.max_health) ||
            value.health < 0.0f || value.max_health <= 0.0f ||
            *root_slot != root || *(void**)((BYTE*)root + 0x780U) != actor ||
            !dluma_player_media_validate_playerdi(profile, actor) ||
            !dluma_player_media_adapter_get_session_level(&current_level) ||
            current_level != session_level) __leave;
        valid = 1;
    } __except (1) {
        valid = 0;
    }
    if (valid) *snapshot = value;
    return valid;
}

static int dluma_player_media_validate_car_buggy(
    DlumaPlayerMediaPlayerProfile* profile, void* buggy)
{
    return profile && buggy && dluma_is_readable(buggy, 0x20U) &&
           *(void**)buggy ==
               (BYTE*)profile->gamedll +
                   DLUMA_DL_CAR_BUGGY_VTABLE_RVA &&
           *(void**)((BYTE*)buggy + 0x18U) ==
               (BYTE*)profile->gamedll +
                   DLUMA_DL_CAR_BUGGY_CONTROL_VTABLE_RVA &&
           dluma_player_media_validate_live_rtti_object(
               profile->gamedll, profile->gamedll_size, buggy,
               DLUMA_DL_CAR_BUGGY_TYPE_RVA, ".?AVCarBuggy@@");
}

static int dluma_player_media_buggy_velocity(
    DlumaPlayerMediaPlayerProfile* profile, void* buggy,
    double* velocity_out, float* x_out, float* y_out, float* z_out)
{
    float components[3];
    float* result;
    float x;
    float y;
    float z;
    double magnitude;
    int valid = 0;
    if (!profile || !buggy || !velocity_out ||
        !x_out || !y_out || !z_out ||
        !profile->vehicle_velocity ||
        !dluma_is_readable(buggy, sizeof(void*)) ||
        *(void**)buggy != (BYTE*)profile->gamedll +
                          DLUMA_DL_CAR_BUGGY_VTABLE_RVA ||
        !dluma_player_media_live_vtable_in_module(
            buggy, profile->gamedll, profile->gamedll_size,
            DLUMA_DL_CAR_BUGGY_LINEAR_VELOCITY_SLOT,
            DLUMA_DL_CAR_BUGGY_LINEAR_VELOCITY_RVA))
        return 0;
    components[0] = 0.0f;
    components[1] = 0.0f;
    components[2] = 0.0f;
    __try {
        result = profile->vehicle_velocity(buggy, components);
        if (result != components) __leave;
        x = components[0];
        y = components[1];
        z = components[2];
        if (!dluma_player_media_float_finite(x) ||
            !dluma_player_media_float_finite(y) ||
            !dluma_player_media_float_finite(z))
            __leave;
        magnitude = dluma_player_media_vector_length(x, y, z);
        if (!dluma_player_media_double_finite(magnitude)) __leave;
        *velocity_out = magnitude;
        *x_out = x;
        *y_out = y;
        *z_out = z;
        valid = 1;
    }
    __except(1) {
        valid = 0;
    }
    return valid;
}

static int dluma_player_media_fmod_export(
    DlumaPlayerMediaPlayerProfile* profile, const char* name,
    DWORD expected_rva, const BYTE* prefix, unsigned int prefix_size,
    void** out)
{
    FARPROC proc;
    if (!profile || !name || !out) return 0;
    proc = GetProcAddress(profile->fmod, name);
    if (!proc || (BYTE*)proc != (BYTE*)profile->fmod + expected_rva ||
        !dluma_player_media_prefix_matches(
            profile->fmod, profile->fmod_size, expected_rva,
            prefix, prefix_size)) return 0;
    *out = (void*)proc;
    return 1;
}

static int dluma_player_media_bind_music_filter(
    DlumaPlayerMediaPlayerProfile* profile)
{
    void** audio_vtable;
    void* function = 0;
    if (!profile ||
        !dluma_player_media_prefix_matches(
            profile->engine, profile->engine_size,
            DLUMA_DL_AUDIO_TRY_LOCK_RVA, g_dluma_dl_audio_try_lock_prefix,
            DLUMA_ARRAY_COUNT(g_dluma_dl_audio_try_lock_prefix)) ||
        !dluma_player_media_prefix_matches(
            profile->engine, profile->engine_size,
            DLUMA_DL_AUDIO_UNLOCK_RVA, g_dluma_dl_audio_unlock_prefix,
            DLUMA_ARRAY_COUNT(g_dluma_dl_audio_unlock_prefix)) ||
        !dluma_player_media_prefix_matches(
            profile->engine, profile->engine_size,
            DLUMA_DL_AUDIO_UPDATE_RVA, g_dluma_dl_audio_update_prefix,
            DLUMA_ARRAY_COUNT(g_dluma_dl_audio_update_prefix)) ||
        !dluma_player_media_prefix_matches(
            profile->engine, profile->engine_size,
            DLUMA_DL_AUDIO_SET_FILTER_RVA,
            g_dluma_dl_audio_set_filter_prefix,
            DLUMA_ARRAY_COUNT(g_dluma_dl_audio_set_filter_prefix)) ||
        !dluma_player_media_module_range(
            profile->engine, profile->engine_size,
            (BYTE*)profile->engine + DLUMA_DL_AUDIO_SINGLETON_RVA,
            sizeof(void*))) return 0;
    audio_vtable = (void**)((BYTE*)profile->engine +
                            DLUMA_DL_AUDIO_VTABLE_RVA);
    if (!dluma_player_media_readonly_range(
            profile->engine, profile->engine_size, audio_vtable,
            0x1B0U + sizeof(void*)) ||
        audio_vtable[0x18U / sizeof(void*)] !=
            (BYTE*)profile->engine + DLUMA_DL_AUDIO_UPDATE_RVA ||
        audio_vtable[0x1B0U / sizeof(void*)] !=
            (BYTE*)profile->engine + DLUMA_DL_AUDIO_SET_FILTER_RVA)
        return 0;
    if (!dluma_player_media_fmod_export(
            profile,
            "?getType@DSP@FMOD@@QEAA?AW4FMOD_RESULT@@PEAW4FMOD_DSP_TYPE@@@Z",
            DLUMA_DL_FMOD_GET_TYPE_RVA, g_dluma_dl_fmod_get_type_prefix,
            DLUMA_ARRAY_COUNT(g_dluma_dl_fmod_get_type_prefix), &function))
        return 0;
    profile->fmod_get_type = (DlumaPlayerMediaFmodGetTypeFn)function;
    if (!dluma_player_media_fmod_export(
            profile, "?getBypass@DSP@FMOD@@QEAA?AW4FMOD_RESULT@@PEA_N@Z",
            DLUMA_DL_FMOD_GET_BYPASS_RVA, g_dluma_dl_fmod_get_bypass_prefix,
            DLUMA_ARRAY_COUNT(g_dluma_dl_fmod_get_bypass_prefix), &function))
        return 0;
    profile->fmod_get_bypass = (DlumaPlayerMediaFmodGetBypassFn)function;
    if (!dluma_player_media_fmod_export(
            profile,
            "?getParameter@DSP@FMOD@@QEAA?AW4FMOD_RESULT@@HPEAMPEADH@Z",
            DLUMA_DL_FMOD_GET_PARAMETER_RVA,
            g_dluma_dl_fmod_get_parameter_prefix,
            DLUMA_ARRAY_COUNT(g_dluma_dl_fmod_get_parameter_prefix), &function))
        return 0;
    profile->fmod_get_parameter =
        (DlumaPlayerMediaFmodGetParameterFn)function;
    if (!dluma_player_media_fmod_export(
            profile, "?setBypass@DSP@FMOD@@QEAA?AW4FMOD_RESULT@@_N@Z",
            DLUMA_DL_FMOD_SET_BYPASS_RVA, g_dluma_dl_fmod_set_bypass_prefix,
            DLUMA_ARRAY_COUNT(g_dluma_dl_fmod_set_bypass_prefix), &function))
        return 0;
    profile->fmod_set_bypass = (DlumaPlayerMediaFmodSetBypassFn)function;
    if (!dluma_player_media_fmod_export(
            profile, "?setParameter@DSP@FMOD@@QEAA?AW4FMOD_RESULT@@HM@Z",
            DLUMA_DL_FMOD_SET_PARAMETER_RVA,
            g_dluma_dl_fmod_set_parameter_prefix,
            DLUMA_ARRAY_COUNT(g_dluma_dl_fmod_set_parameter_prefix), &function))
        return 0;
    profile->fmod_set_parameter =
        (DlumaPlayerMediaFmodSetParameterFn)function;
    profile->audio_try_lock = (DlumaPlayerMediaAudioTryLockFn)(
        (BYTE*)profile->engine + DLUMA_DL_AUDIO_TRY_LOCK_RVA);
    profile->audio_unlock = (DlumaPlayerMediaAudioUnlockFn)(
        (BYTE*)profile->engine + DLUMA_DL_AUDIO_UNLOCK_RVA);
    return 1;
}

static int dluma_player_media_copy_bus_name(
    const char* source, char out[DLUMA_DL_AUDIO_BUS_NAME_LIMIT])
{
    unsigned int i;
    if (!source || !out) return 0;
    for (i = 0; i < DLUMA_DL_AUDIO_BUS_NAME_LIMIT; ++i) {
        unsigned char value;
        if (!dluma_is_readable(source + i, 1U)) return 0;
        value = (unsigned char)source[i];
        if (!value) {
            out[i] = 0;
            return i > 0U;
        }
        if (value < 32U || value == 127U ||
            i + 1U == DLUMA_DL_AUDIO_BUS_NAME_LIMIT)
            return 0;
        out[i] = (char)value;
    }
    return 0;
}

static int dluma_player_media_resolve_music_layout_locked(
    DlumaPlayerMediaPlayerProfile* profile,
    DlumaPlayerMediaMusicSnapshot* snapshot)
{
    void* audio;
    void*** groups_slot;
    void** groups;
    unsigned int group_count;
    int bus_count;
    int music_bus = -1;
    int i;
    BYTE* record;
    void* group;
    void* dsp;
    char name[DLUMA_DL_AUDIO_BUS_NAME_LIMIT];
    if (!profile || !snapshot) return 0;
    audio = *(void**)((BYTE*)profile->engine +
                      DLUMA_DL_AUDIO_SINGLETON_RVA);
    if (!audio || !dluma_is_readable(audio, 0x66A1U) ||
        *(void**)audio !=
            (BYTE*)profile->engine + DLUMA_DL_AUDIO_VTABLE_RVA ||
        !*(void**)((BYTE*)audio + DLUMA_DL_AUDIO_SYSTEM_OFFSET) ||
        *(unsigned char*)((BYTE*)audio +
                          DLUMA_DL_AUDIO_INITIALIZED_OFFSET) == 0U ||
        *(unsigned char*)((BYTE*)audio +
                          DLUMA_DL_AUDIO_FILTER_DIRTY_OFFSET) != 0U)
        return 0;
    bus_count = *(int*)((BYTE*)audio + DLUMA_DL_AUDIO_BUS_COUNT_OFFSET);
    if (bus_count <= 0 || bus_count > (int)DLUMA_DL_AUDIO_BUS_LIMIT)
        return 0;
    groups_slot = (void***)((BYTE*)audio +
                            DLUMA_DL_AUDIO_GROUP_VECTOR_OFFSET);
    groups = *groups_slot;
    group_count = *(unsigned int*)((BYTE*)audio +
                                  DLUMA_DL_AUDIO_GROUP_COUNT_OFFSET);
    if (!groups || group_count < (unsigned int)bus_count ||
        group_count > DLUMA_DL_AUDIO_BUS_LIMIT ||
        !dluma_is_readable(groups, sizeof(void*) * group_count))
        return 0;
    for (i = 0; i < bus_count; ++i) {
        record = (BYTE*)audio + DLUMA_DL_AUDIO_BUS_RECORDS_OFFSET +
                 (SIZE_T)i * DLUMA_DL_AUDIO_BUS_RECORD_STRIDE;
        if (!dluma_player_media_copy_bus_name((const char*)record, name))
            return 0;
        if (dluma_stricmp(name, "music") == 0) {
            if (music_bus >= 0) return 0;
            music_bus = i;
        }
    }
    if (music_bus < 0) return 0;
    group = groups[music_bus];
    dsp = *(void**)((BYTE*)audio + DLUMA_DL_AUDIO_LOWPASS_DSP_OFFSET +
                    (SIZE_T)music_bus * sizeof(void*));
    if (!group || !dsp || !dluma_is_readable(group, sizeof(void*)) ||
        !dluma_is_readable(dsp, sizeof(void*)) ||
        !dluma_player_media_readonly_range(
            profile->fmod, profile->fmod_size,
            *(void***)group, sizeof(void*)) ||
        !dluma_player_media_readonly_range(
            profile->fmod, profile->fmod_size,
            *(void***)dsp, sizeof(void*)) ||
        !dluma_player_media_is_executable(
            profile->fmod, profile->fmod_size,
            **(void***)dsp, 1U)) return 0;
    record = (BYTE*)audio + DLUMA_DL_AUDIO_BUS_RECORDS_OFFSET +
             (SIZE_T)music_bus * DLUMA_DL_AUDIO_BUS_RECORD_STRIDE;
    memset(snapshot, 0, sizeof(*snapshot));
    snapshot->audio = audio;
    snapshot->channel_group = group;
    snapshot->lowpass_dsp = dsp;
    snapshot->bus_index = music_bus;
    snapshot->cached_type = *(DWORD*)(record +
        DLUMA_DL_AUDIO_FILTER_TYPE_OFFSET);
    snapshot->cached_resonance = *(float*)(record +
        DLUMA_DL_AUDIO_FILTER_RESONANCE_OFFSET);
    snapshot->cached_frequency = *(float*)(record +
        DLUMA_DL_AUDIO_FILTER_FREQUENCY_OFFSET);
    snapshot->cached_state = *(DWORD*)(record +
        DLUMA_DL_AUDIO_FILTER_STATE_OFFSET);
    return dluma_player_media_float_finite(snapshot->cached_resonance) &&
           dluma_player_media_float_finite(snapshot->cached_frequency);
}

static int dluma_player_media_audio_try_lock(
    DlumaPlayerMediaPlayerProfile* profile)
{
    int locked = 0;
    if (!profile || !profile->audio_try_lock) return 0;
    __try {
        locked = profile->audio_try_lock() ? 1 : 0;
    }
    __except(1) {
        locked = 0;
    }
    return locked;
}

static int dluma_player_media_audio_unlock(
    DlumaPlayerMediaPlayerProfile* profile)
{
    int unlocked = 1;
    if (!profile || !profile->audio_unlock) return 0;
    __try {
        profile->audio_unlock();
    }
    __except(1) {
        unlocked = 0;
    }
    return unlocked;
}

static int dluma_player_media_music_layout_equal(
    const DlumaPlayerMediaMusicSnapshot* first,
    const DlumaPlayerMediaMusicSnapshot* second)
{
    return first && second &&
           first->audio == second->audio &&
           first->channel_group == second->channel_group &&
           first->lowpass_dsp == second->lowpass_dsp &&
           first->bus_index == second->bus_index &&
           first->cached_type == second->cached_type &&
           first->cached_state == second->cached_state &&
           dluma_player_media_float_equal(
               first->cached_resonance, second->cached_resonance) &&
           dluma_player_media_float_equal(
               first->cached_frequency, second->cached_frequency);
}

static int dluma_player_media_music_snapshot_take(
    DlumaPlayerMediaPlayerProfile* profile,
    DlumaPlayerMediaMusicSnapshot* snapshot)
{
    DlumaPlayerMediaMusicSnapshot captured;
    DlumaPlayerMediaMusicSnapshot confirmed;
    char parameter_text[32];
    int dsp_type = 0;
    unsigned char bypass = 0;
    float parameter0 = 0.0f;
    float parameter1 = 0.0f;
    int valid = 0;
    if (!profile || !snapshot || !profile->fmod_get_type ||
        !profile->fmod_get_bypass || !profile->fmod_get_parameter)
        return 0;
    memset(snapshot, 0, sizeof(*snapshot));
    memset(&captured, 0, sizeof(captured));
    memset(&confirmed, 0, sizeof(confirmed));
    if (!dluma_player_media_audio_try_lock(profile)) return 0;
    __try {
        valid = dluma_player_media_resolve_music_layout_locked(
            profile, &captured);
    }
    __except(1) {
        valid = 0;
    }
    if (!dluma_player_media_audio_unlock(profile)) valid = 0;
    if (!valid) return 0;
    memset(parameter_text, 0, sizeof(parameter_text));
    __try {
        valid =
            profile->fmod_get_type(
                captured.lowpass_dsp, &dsp_type) == 0 &&
            dsp_type == DLUMA_DL_FMOD_LOWPASS_TYPE &&
            profile->fmod_get_bypass(
                captured.lowpass_dsp, &bypass) == 0 &&
            bypass <= 1U &&
            profile->fmod_get_parameter(
                captured.lowpass_dsp, 0, &parameter0,
                parameter_text, (int)sizeof(parameter_text)) == 0 &&
            profile->fmod_get_parameter(
                captured.lowpass_dsp, 1, &parameter1,
                parameter_text, (int)sizeof(parameter_text)) == 0 &&
            dluma_player_media_float_finite(parameter0) &&
            dluma_player_media_float_finite(parameter1);
    }
    __except(1) {
        valid = 0;
    }
    if (!valid || !dluma_player_media_audio_try_lock(profile)) return 0;
    __try {
        valid = dluma_player_media_resolve_music_layout_locked(
                    profile, &confirmed) &&
                dluma_player_media_music_layout_equal(
                    &captured, &confirmed);
    }
    __except(1) {
        valid = 0;
    }
    if (!dluma_player_media_audio_unlock(profile)) valid = 0;
    if (!valid) return 0;
    captured.dsp_type = dsp_type;
    captured.dsp_bypass = bypass;
    captured.dsp_parameter0 = parameter0;
    captured.dsp_parameter1 = parameter1;
    captured.valid = 1U;
    *snapshot = captured;
    return 1;
}

static int dluma_player_media_music_snapshot_restore(
    DlumaPlayerMediaPlayerProfile* profile,
    const DlumaPlayerMediaMusicSnapshot* snapshot)
{
    DlumaPlayerMediaMusicSnapshot current;
    DlumaPlayerMediaMusicSnapshot final_state;
    char parameter_text[32];
    int dsp_type = 0;
    unsigned char bypass = 0;
    float parameter0 = 0.0f;
    float parameter1 = 0.0f;
    BYTE* record;
    int valid = 0;
    if (!profile || !snapshot || !snapshot->valid ||
        !profile->fmod_set_parameter || !profile->fmod_set_bypass ||
        !profile->fmod_get_type || !profile->fmod_get_bypass ||
        !profile->fmod_get_parameter) return 0;
    memset(&current, 0, sizeof(current));
    memset(&final_state, 0, sizeof(final_state));
    if (!dluma_player_media_audio_try_lock(profile)) return 0;
    __try {
        valid = dluma_player_media_resolve_music_layout_locked(
                    profile, &current) &&
                dluma_player_media_music_layout_equal(snapshot, &current);
    }
    __except(1) {
        valid = 0;
    }
    if (!dluma_player_media_audio_unlock(profile)) valid = 0;
    if (!valid) return 0;
    __try {
        valid =
            profile->fmod_set_parameter(
                snapshot->lowpass_dsp, 0,
                snapshot->dsp_parameter0) == 0 &&
            profile->fmod_set_parameter(
                snapshot->lowpass_dsp, 1,
                snapshot->dsp_parameter1) == 0 &&
            profile->fmod_set_bypass(
                snapshot->lowpass_dsp,
                snapshot->dsp_bypass) == 0;
        if (valid) {
            memset(parameter_text, 0, sizeof(parameter_text));
            valid =
                profile->fmod_get_type(
                    snapshot->lowpass_dsp, &dsp_type) == 0 &&
                profile->fmod_get_bypass(
                    snapshot->lowpass_dsp, &bypass) == 0 &&
                profile->fmod_get_parameter(
                    snapshot->lowpass_dsp, 0, &parameter0,
                    parameter_text, (int)sizeof(parameter_text)) == 0 &&
                profile->fmod_get_parameter(
                    snapshot->lowpass_dsp, 1, &parameter1,
                    parameter_text, (int)sizeof(parameter_text)) == 0 &&
                dsp_type == snapshot->dsp_type &&
                bypass == snapshot->dsp_bypass &&
                dluma_player_media_float_equal(
                    parameter0, snapshot->dsp_parameter0) &&
                dluma_player_media_float_equal(
                    parameter1, snapshot->dsp_parameter1);
        }
    }
    __except(1) {
        valid = 0;
    }
    if (!valid || !dluma_player_media_audio_try_lock(profile)) return 0;
    __try {
        valid = dluma_player_media_resolve_music_layout_locked(
                    profile, &final_state) &&
                dluma_player_media_music_layout_equal(snapshot, &final_state);
        if (valid) {
            record = (BYTE*)snapshot->audio +
                DLUMA_DL_AUDIO_BUS_RECORDS_OFFSET +
                (SIZE_T)snapshot->bus_index *
                    DLUMA_DL_AUDIO_BUS_RECORD_STRIDE;
            *(DWORD*)(record + DLUMA_DL_AUDIO_FILTER_TYPE_OFFSET) =
                snapshot->cached_type;
            *(float*)(record + DLUMA_DL_AUDIO_FILTER_RESONANCE_OFFSET) =
                snapshot->cached_resonance;
            *(float*)(record + DLUMA_DL_AUDIO_FILTER_FREQUENCY_OFFSET) =
                snapshot->cached_frequency;
            *(DWORD*)(record + DLUMA_DL_AUDIO_FILTER_STATE_OFFSET) =
                snapshot->cached_state;
            *(unsigned char*)((BYTE*)snapshot->audio +
                DLUMA_DL_AUDIO_FILTER_DIRTY_OFFSET) = 0U;
        }
    }
    __except(1) {
        valid = 0;
    }
    if (!dluma_player_media_audio_unlock(profile)) valid = 0;
    return valid;
}

void dluma_player_media_adapter_reset(void)
{
    DlumaPlayerMediaPlayerProfile* profile =
        &g_dluma_player_media_player;
    unsigned int i;
    if (profile->published_frontend_owner_level ||
        profile->published_frontend_music_manager ||
        profile->published_frontend_main_menu_module) {
        dluma_log_line(
            "ERROR",
            "PlayerMedia adapter reset refused while a frontend manager is published");
        return;
    }
    if (profile->music_owned || profile->music_snapshot.valid) {
        dluma_log_line(
            "ERROR",
            "PlayerMedia adapter reset refused while music state is owned");
        return;
    }
    for (i = 0; i < DLUMA_DL_PLAYER_MEDIA_MAX_WAVES; ++i) {
        if (profile->waves[i].in_use) {
            dluma_log_line(
                "ERROR",
                "PlayerMedia adapter reset refused while waves are owned");
            return;
        }
    }
    memset(profile, 0, sizeof(*profile));
}
void dluma_player_media_adapter_bind(
    HMODULE original_gamedll, void* game_script_manager)
{
    DlumaPlayerMediaPlayerProfile* profile =
        &g_dluma_player_media_player;
    DWORD timestamp = 0;
    DWORD image_size = 0;
    unsigned char was_logged = profile->logged;
    unsigned int i;
    if (profile->published_frontend_owner_level ||
        profile->published_frontend_music_manager) {
        dluma_log_line(
            "ERROR",
            "PlayerMedia adapter rebind refused while a frontend manager is published");
        return;
    }
    if (profile->music_owned || profile->music_snapshot.valid) {
        dluma_log_line(
            "ERROR",
            "PlayerMedia adapter rebind refused while music state is owned");
        return;
    }
    for (i = 0; i < DLUMA_DL_PLAYER_MEDIA_MAX_WAVES; ++i) {
        if (profile->waves[i].in_use) {
            dluma_log_line(
                "ERROR",
                "PlayerMedia adapter rebind refused while waves are owned");
            return;
        }
    }
    memset(profile, 0, sizeof(*profile));
    profile->logged = was_logged;
    profile->wave_binding_reason =
        DLUMA_DL_WAVE_BIND_PROFILE_INVALID;
    profile->music_binding_reason =
        DLUMA_DL_MUSIC_BIND_PROFILE_INVALID;
    profile->player = GetModuleHandleW(0);
    profile->engine = GetModuleHandleW(L"engine_x64_rwdi.dll");
    profile->gamedll = GetModuleHandleW(L"gamedll_x64_rwdi.dll");
    profile->filesystem =
        GetModuleHandleW(L"filesystem_x64_rwdi.dll");
    profile->fmod = GetModuleHandleW(L"fmodex64.dll");
    profile->game_script_manager = game_script_manager;
    profile->exact_profile =
        original_gamedll && original_gamedll == profile->gamedll &&
        dluma_player_media_module_sha256(
            profile->player, 8ULL * 1024ULL * 1024ULL,
            g_dluma_player_media_player_sha256) &&
        dluma_player_media_module_sha256(
            profile->engine, 32ULL * 1024ULL * 1024ULL,
            g_dluma_player_media_engine_sha256) &&
        dluma_player_media_module_sha256(
            profile->gamedll, 64ULL * 1024ULL * 1024ULL,
            g_dluma_player_media_gamedll_sha256) &&
        dluma_player_media_module_sha256(
            profile->filesystem, 16ULL * 1024ULL * 1024ULL,
            g_dluma_player_media_filesystem_sha256) &&
        dluma_player_media_module_sha256(
            profile->fmod, 16ULL * 1024ULL * 1024ULL,
            g_dluma_player_media_fmod_sha256) &&
        dluma_player_media_pe_identity(
            profile->engine, &timestamp, &image_size) &&
        timestamp == DLUMA_DL_ENGINE_TIMESTAMP &&
        image_size == DLUMA_DL_ENGINE_IMAGE_SIZE;
    profile->engine_size = image_size;
    if (profile->exact_profile) {
        profile->exact_profile = dluma_player_media_pe_identity(
            profile->gamedll, &timestamp, &image_size) &&
            timestamp == DLUMA_DL_GAMEDLL_TIMESTAMP &&
            image_size == DLUMA_DL_GAMEDLL_IMAGE_SIZE;
        profile->gamedll_size = image_size;
    }
    if (profile->exact_profile) {
        profile->exact_profile = (unsigned char)dluma_player_media_pe_identity(
            profile->fmod, &timestamp, &image_size);
        profile->fmod_size = image_size;
    }
    if (profile->exact_profile) {
        profile->local_buggy_ready =
            (unsigned char)dluma_player_media_bind_local_buggy(profile);
        profile->wave_bank_ready =
            (unsigned char)dluma_player_media_bind_wave_bank_detailed(
                profile, &profile->wave_binding_reason);
        profile->wave_exports_ready =
            (unsigned char)dluma_player_media_bind_wave_exports(profile);
        profile->music_manager_ready =
            (unsigned char)dluma_player_media_bind_music_manager_detailed(
                profile, &profile->music_binding_reason);
        profile->frontend_music_ready =
            (unsigned char)dluma_player_media_bind_frontend_musics_manager(
                profile);
        profile->main_menu_music_ready =
            (unsigned char)dluma_player_media_bind_frontend_main_menu_music(
                profile);
        if (profile->local_buggy_ready)
            profile->capabilities |= DLUMA_PLAYER_MEDIA_CAP_VEHICLE_STATE;
        profile->music_filter_binding_ready =
            (unsigned char)dluma_player_media_bind_music_filter(profile);
    }
    profile->music_fade_ready = profile->music_manager_ready;
    profile->music_wet_mix_ready = 0U;
    if (!profile->logged) {
        if (!profile->exact_profile) {
            dluma_log_pair(
                "WARN", "player_media_audio_profile_ready",
                "unsupported_module_identity_services_disabled");
        } else {
            dluma_log_pair(
                profile->local_buggy_ready ? "INFO" : "WARN",
                "player_media_local_buggy_ready",
                profile->local_buggy_ready ? "1" : "0");
            dluma_log_pair(
                profile->wave_bank_ready ? "INFO" : "WARN",
                "player_media_wave_bank_binding_ready",
                dluma_player_media_wave_binding_reason_name(
                    profile->wave_binding_reason));
            dluma_log_pair(
                profile->wave_exports_ready ? "INFO" : "WARN",
                "player_media_wave_exports_ready",
                profile->wave_exports_ready ? "1" : "0");
            dluma_log_pair(
                profile->music_manager_ready ? "INFO" : "WARN",
                "player_media_music_manager_binding_ready",
                dluma_player_media_music_binding_reason_name(
                    profile->music_binding_reason));
            dluma_log_pair(
                profile->frontend_music_ready ? "INFO" : "WARN",
                "player_media_frontend_music_binding_ready",
                profile->frontend_music_ready ? "1" : "0");
            dluma_log_pair(
                profile->main_menu_music_ready ? "INFO" : "WARN",
                "player_media_main_menu_music_binding_ready",
                profile->main_menu_music_ready ? "1" : "0");
            dluma_log_pair(
                profile->music_filter_binding_ready ? "INFO" : "WARN",
                "player_media_music_filter_binding_ready",
                profile->music_filter_binding_ready
                    ? "optional_filter_binding_ready"
                    : "optional_filter_binding_unavailable");
            dluma_log_pair(
                profile->wave_bank_ready &&
                    profile->wave_exports_ready &&
                    profile->music_manager_ready
                    ? "INFO" : "WARN",
                "player_media_audio_profile_ready",
                profile->wave_bank_ready &&
                    profile->wave_exports_ready &&
                    profile->music_manager_ready
                    ? "static_bindings_ready_live_objects_pending"
                    : "static_binding_failed");
        }
        profile->logged = 1U;
    }
}
void dluma_player_media_adapter_tick(
    void* game_subobject, void* level_interface,
    ULONGLONG now_ms, float delta_seconds)
{
    DlumaPlayerMediaPlayerProfile* profile =
        &g_dluma_player_media_player;
    void* manager = 0;
    unsigned int i;
    (void)delta_seconds;
    profile->game_subobject = game_subobject;
    profile->observed_active_level = level_interface;
    dluma_player_media_probe_live_readiness(
        profile, level_interface, now_ms);
    if (!profile->wave_exports_ready ||
        !dluma_player_media_validate_wave_owner(profile, &manager) ||
        (profile->music_owned &&
         manager != profile->owned_music_manager))
        return;
    for (i = 0; i < DLUMA_DL_PLAYER_MEDIA_MAX_WAVES; ++i) {
        DlumaPlayerMediaWaveSlot* slot = &profile->waves[i];
        unsigned char playing = 1U;
        if (!slot->in_use || !slot->stopping ||
            slot->owner != manager)
            continue;
        __try {
            playing = profile->is_wave_playing(
                manager, slot->engine_handle);
        }
        __except(1) {
            playing = 1U;
        }
        if (!playing) dluma_player_media_wave_clear(slot);
    }
}

int dluma_player_media_adapter_publish_frontend_music_manager(
    void* owner_level, void* music_manager)
{
    DlumaPlayerMediaPlayerProfile* profile =
        &g_dluma_player_media_player;
    DlumaPlayerMediaManagerDiagnostic diagnostic;
    void* manager_owner = 0;
    unsigned int reason = DLUMA_DL_LIVE_PROFILE_INVALID;
    unsigned int index;
    int query_delivered = 1;
    if (!profile->exact_profile || !profile->frontend_music_ready ||
        !profile->get_object_level || !owner_level || !music_manager ||
        !dluma_is_readable(owner_level, sizeof(void*)))
        return 0;
    if ((profile->published_frontend_owner_level &&
         profile->published_frontend_owner_level != owner_level) ||
        (profile->published_frontend_music_manager &&
         profile->published_frontend_music_manager != music_manager))
        return 0;
    if (profile->music_owned || profile->music_snapshot.valid)
        return 0;
    for (index = 0; index < DLUMA_DL_PLAYER_MEDIA_MAX_WAVES; ++index)
        if (profile->waves[index].in_use)
            return 0;
    if (!dluma_player_media_validate_frontend_musics_manager_object_detailed(
            profile, music_manager, &reason, &diagnostic))
        return 0;
    __try {
        manager_owner = profile->get_object_level(music_manager);
    }
    __except(1) {
        query_delivered = 0;
    }
    if (!query_delivered || manager_owner != owner_level)
        return 0;
    profile->published_frontend_owner_level = owner_level;
    profile->published_frontend_music_manager = music_manager;
    dluma_log_pair(
        "INFO", "player_media_frontend_music_manager",
        "published_owner_corroborated");
    return 1;
}

int dluma_player_media_adapter_clear_frontend_music_manager(
    void* owner_level, void* music_manager)
{
    DlumaPlayerMediaPlayerProfile* profile =
        &g_dluma_player_media_player;
    DlumaPlayerMediaManagerDiagnostic diagnostic;
    void* resolved_owner = 0;
    void* resolved_manager = 0;
    unsigned int reason = DLUMA_DL_LIVE_PROFILE_INVALID;
    unsigned int source = DLUMA_DL_MUSIC_SOURCE_NONE;
    unsigned int index;
    if (!profile->published_frontend_owner_level &&
        !profile->published_frontend_music_manager)
        return 1;
    if (!owner_level || !music_manager ||
        profile->published_frontend_owner_level != owner_level ||
        profile->published_frontend_music_manager != music_manager ||
        profile->music_owned || profile->music_snapshot.valid)
        return 0;
    for (index = 0; index < DLUMA_DL_PLAYER_MEDIA_MAX_WAVES; ++index)
        if (profile->waves[index].in_use)
            return 0;
    if (!dluma_player_media_resolve_published_frontend_music_manager(
            profile, &resolved_owner, &resolved_manager,
            &reason, &source, &diagnostic) ||
        resolved_owner != owner_level || resolved_manager != music_manager ||
        source !=
            DLUMA_DL_MUSIC_SOURCE_CUSTOM_FRONTEND_BACKGROUND_OWNER_CORROBORATED)
        return 0;
    profile->published_frontend_owner_level = 0;
    profile->published_frontend_music_manager = 0;
    dluma_log_pair(
        "INFO", "player_media_frontend_music_manager",
        "cleared_before_background_unload");
    return 1;
}

int dluma_player_media_adapter_publish_frontend_main_menu_module(
    void* main_menu_module)
{
    DlumaPlayerMediaPlayerProfile* profile =
        &g_dluma_player_media_player;
    unsigned int index;
    if (!dluma_player_media_validate_frontend_main_menu_module(
            profile, main_menu_module))
        return 0;
    if (profile->published_frontend_main_menu_module &&
        profile->published_frontend_main_menu_module != main_menu_module)
        return 0;
    if (profile->music_owned || profile->music_snapshot.valid)
        return 0;
    for (index = 0; index < DLUMA_DL_PLAYER_MEDIA_MAX_WAVES; ++index)
        if (profile->waves[index].in_use)
            return 0;
    profile->published_frontend_main_menu_module = main_menu_module;
    dluma_log_pair(
        "INFO", "player_media_frontend_main_menu_module",
        "published_exact_main_menu_module_di");
    return 1;
}

int dluma_player_media_adapter_clear_frontend_main_menu_module(
    void* main_menu_module)
{
    DlumaPlayerMediaPlayerProfile* profile =
        &g_dluma_player_media_player;
    unsigned int index;
    if (!profile->published_frontend_main_menu_module)
        return 1;
    if (!main_menu_module ||
        profile->published_frontend_main_menu_module !=
            main_menu_module ||
        profile->music_owned || profile->music_snapshot.valid)
        return 0;
    for (index = 0; index < DLUMA_DL_PLAYER_MEDIA_MAX_WAVES; ++index)
        if (profile->waves[index].in_use)
            return 0;
    if (!dluma_player_media_validate_frontend_main_menu_module(
            profile, main_menu_module))
        return 0;
    profile->published_frontend_main_menu_module = 0;
    dluma_log_pair(
        "INFO", "player_media_frontend_main_menu_module",
        "cleared_before_background_unload");
    return 1;
}

int dluma_player_media_adapter_get_session_level(
    void** level_interface_out)
{
    DlumaPlayerMediaPlayerProfile* profile =
        &g_dluma_player_media_player;
    void* owner_level = 0;
    void* level_di = 0;
    void* manager = 0;
    unsigned int reason = DLUMA_DL_LIVE_PROFILE_INVALID;
    unsigned int source = DLUMA_DL_MUSIC_SOURCE_NONE;
    unsigned int session_class = DLUMA_DL_SESSION_CLASS_NONE;
    DlumaPlayerMediaManagerDiagnostic diagnostic;
    if (level_interface_out) *level_interface_out = 0;
    if (!level_interface_out || !profile->exact_profile ||
        !profile->game_subobject)
        return 0;
    /*
     * The exact Player can return null from IGame::GetActiveLevel for the
     * complete lifetime of a Workshop session. Reuse the independently
     * validated game-global GameDI -> SessionDI -> LevelDI -> ILevel owner
     * route. A positive current active level remains corroborating
     * authority: a stale session whose owner differs from it is rejected by
     * the resolver.
     */
    if (!dluma_player_media_resolve_game_session_music_manager(
            profile, profile->observed_active_level,
            &owner_level, &level_di, &manager, &reason, &source,
            &session_class, &diagnostic) ||
        !owner_level || !level_di || !manager ||
        session_class == DLUMA_DL_SESSION_CLASS_NONE ||
        source !=
            DLUMA_DL_MUSIC_SOURCE_GAME_GLOBAL_SESSION_MANAGER_OWNER_CORROBORATED)
        return 0;
    *level_interface_out = owner_level;
    return 1;
}

unsigned int dluma_player_media_adapter_capabilities(void)
{
    DlumaPlayerMediaPlayerProfile* profile =
        &g_dluma_player_media_player;
    unsigned int capabilities = 0U;
    void* wave_owner = 0;
    void* stock_manager = 0;
    if (!profile->exact_profile) return 0U;
    if (profile->local_buggy_ready)
        capabilities |= DLUMA_PLAYER_MEDIA_CAP_VEHICLE_STATE;
    if (!dluma_player_media_validate_wave_owner(profile, &wave_owner) ||
        (profile->music_owned &&
         wave_owner != profile->owned_music_manager))
        return capabilities;
    if (profile->wave_exports_ready)
        capabilities |= DLUMA_PLAYER_MEDIA_CAP_CUE_PLAYBACK;
    if (profile->wave_bank_ready &&
        profile->wave_exports_ready) {
        void* audio = 0;
        if (dluma_player_media_validate_audio_object(profile, &audio))
            capabilities |= DLUMA_PLAYER_MEDIA_CAP_WAVE_BANK;
    }
    if (profile->published_frontend_music_manager) {
        if (profile->published_frontend_main_menu_module) {
            if (profile->frontend_music_ready &&
                profile->frontend_music_mute &&
                profile->frontend_music_unmute &&
                profile->main_menu_music_ready &&
                profile->main_menu_music_stop &&
                profile->main_menu_music_start &&
                dluma_player_media_validate_frontend_main_menu_module(
                    profile,
                    profile->published_frontend_main_menu_module))
                capabilities |= DLUMA_PLAYER_MEDIA_CAP_MUSIC_EXCLUSIVE;
        } else if (profile->frontend_music_ready &&
                   profile->frontend_music_mute &&
                   profile->frontend_music_unmute) {
            capabilities |= DLUMA_PLAYER_MEDIA_CAP_MUSIC_EXCLUSIVE;
        }
    } else if (dluma_player_media_validate_music_manager(
                   profile, &stock_manager) &&
               stock_manager == wave_owner) {
        if (profile->music_manager_ready &&
            profile->music_stop &&
            profile->music_start)
            capabilities |= DLUMA_PLAYER_MEDIA_CAP_MUSIC_EXCLUSIVE;
        if (profile->music_filter_binding_ready &&
            profile->music_filter_ready)
            capabilities |= DLUMA_PLAYER_MEDIA_CAP_MUSIC_FILTER;
    }
    return capabilities;
}
int dluma_player_media_adapter_get_vehicle_state(
    DlumaVehicleAdapterState* state)
{
    DlumaPlayerMediaPlayerProfile* profile =
        &g_dluma_player_media_player;
    DlumaVehicleGateEvidence evidence;
    DlumaVehicleGateDecision decision;
    void** root_slot;
    void* root = 0;
    void* actor = 0;
    void* vehicle = 0;
    void* buggy = 0;
    void* driver_owner = 0;
    void* stock_buggy = 0;
    void* revalidated_vehicle = 0;
    void* revalidated_driver = 0;
    unsigned char stock_driver = 0;
    float velocity_x = 0.0f;
    float velocity_y = 0.0f;
    float velocity_z = 0.0f;
    double velocity = 0.0;
    double mph = 0.0;
    int identity_stable = 0;
    if (!state) return DLUMA_MEDIA_RESULT_INVALID;
    memset(state, 0, sizeof(*state));
    memset(&evidence, 0, sizeof(evidence));
    memset(&decision, 0, sizeof(decision));
    state->size = sizeof(*state);
    state->version = DLUMA_MEDIA_STRUCT_VERSION;
    if (!profile->exact_profile || !profile->local_buggy_ready ||
        !(profile->capabilities & DLUMA_PLAYER_MEDIA_CAP_VEHICLE_STATE))
        return DLUMA_MEDIA_RESULT_UNAVAILABLE;
    evidence.static_routes_intact =
        dluma_player_media_validate_local_buggy_game_routes(profile) &&
        profile->local_buggy ==
            (DlumaPlayerMediaLocalBuggyFn)(
                (BYTE*)profile->gamedll + DLUMA_DL_LOCAL_BUGGY_RVA) &&
        profile->actor_is_driver ==
            (DlumaPlayerMediaActorIsDriverFn)(
                (BYTE*)profile->gamedll +
                DLUMA_DL_ACTOR_IS_DRIVER_RVA) &&
        profile->rt_dynamic_cast ==
            (DlumaPlayerMediaRtDynamicCastFn)(
                (BYTE*)profile->gamedll +
                DLUMA_DL_RTDYNAMICCAST_THUNK_RVA) &&
        profile->current_vehicle ==
            (DlumaPlayerMediaCurrentVehicleFn)(
                (BYTE*)profile->gamedll +
                DLUMA_DL_PLAYERDI_CURRENT_VEHICLE_RVA) &&
        profile->vehicle_driver ==
            (DlumaPlayerMediaVehicleDriverFn)(
                (BYTE*)profile->gamedll +
                DLUMA_DL_VEHICLE_BASE_GET_DRIVER_RVA) &&
        profile->vehicle_velocity ==
            (DlumaPlayerMediaVehicleVelocityFn)(
                (BYTE*)profile->gamedll +
                DLUMA_DL_CAR_BUGGY_LINEAR_VELOCITY_RVA) &&
        dluma_player_media_import_slot_matches(
            profile->gamedll, profile->gamedll_size,
            DLUMA_DL_CAR_BUGGY_PHYSICS_BIND_IAT_RVA,
            profile->engine, profile->engine_size,
            DLUMA_DL_PHYSICS_BIND_RVA) &&
        dluma_player_media_import_slot_matches(
            profile->gamedll, profile->gamedll_size,
            DLUMA_DL_CAR_BUGGY_LINEAR_VELOCITY_IAT_RVA,
            profile->engine, profile->engine_size,
            DLUMA_DL_LINEAR_VELOCITY_RVA);
    if (!evidence.static_routes_intact) goto publish;
    root_slot = (void**)((BYTE*)profile->gamedll +
                         DLUMA_DL_LOCAL_ROOT_SLOT_RVA);
    evidence.root_slot_readable =
        dluma_is_readable(root_slot, sizeof(*root_slot)) ? 1U : 0U;
    if (!evidence.root_slot_readable) goto publish;
    root = *root_slot;
    evidence.root_present = root ? 1U : 0U;
    if (!evidence.root_present) goto publish;
    evidence.actor_field_readable =
        dluma_is_readable(
            (BYTE*)root + 0x780U, sizeof(void*)) ? 1U : 0U;
    if (!evidence.actor_field_readable) goto publish;
    actor = *(void**)((BYTE*)root + 0x780U);
    evidence.actor_present = actor ? 1U : 0U;
    if (!evidence.actor_present) goto publish;
    evidence.playerdi_exact =
        dluma_player_media_validate_playerdi(
            profile, actor) ? 1U : 0U;
    if (!evidence.playerdi_exact) goto publish;
    __try {
        vehicle = profile->current_vehicle(actor);
        evidence.current_vehicle_call_valid = 1U;
    }
    __except(1) {
        evidence.current_vehicle_call_valid = 0U;
    }
    if (!evidence.current_vehicle_call_valid) goto publish;
    evidence.current_vehicle_present = vehicle ? 1U : 0U;
    if (!evidence.current_vehicle_present) goto publish;
    __try {
        buggy = profile->rt_dynamic_cast(
            vehicle, 0,
            (BYTE*)profile->gamedll +
                DLUMA_DL_VEHICLE_BASE_TYPE_RVA,
            (BYTE*)profile->gamedll +
                DLUMA_DL_CAR_BUGGY_TYPE_RVA,
            0);
        evidence.current_vehicle_cast_call_valid = 1U;
    }
    __except(1) {
        evidence.current_vehicle_cast_call_valid = 0U;
    }
    if (!evidence.current_vehicle_cast_call_valid) goto publish;
    evidence.current_vehicle_is_buggy = buggy ? 1U : 0U;
    if (!evidence.current_vehicle_is_buggy) goto publish;
    evidence.car_buggy_exact =
        buggy == vehicle &&
        dluma_player_media_validate_car_buggy(profile, buggy)
            ? 1U : 0U;
    if (!evidence.car_buggy_exact) goto publish;
    __try {
        driver_owner = profile->vehicle_driver(buggy);
        evidence.driver_query_call_valid = 1U;
    }
    __except(1) {
        evidence.driver_query_call_valid = 0U;
    }
    if (!evidence.driver_query_call_valid) goto publish;
    evidence.driver_present = driver_owner ? 1U : 0U;
    if (!evidence.driver_present) goto publish;
    evidence.driver_matches_local_player =
        driver_owner == actor ? 1U : 0U;
    if (!evidence.driver_matches_local_player) goto publish;
    __try {
        stock_driver =
            profile->actor_is_driver(actor) ? 1U : 0U;
        if (stock_driver)
            stock_buggy = profile->local_buggy();
        evidence.stock_driver_call_valid = 1U;
    }
    __except(1) {
        evidence.stock_driver_call_valid = 0U;
    }
    if (!evidence.stock_driver_call_valid) goto publish;
    evidence.stock_is_driver = stock_driver ? 1U : 0U;
    evidence.stock_buggy_present = stock_buggy ? 1U : 0U;
    evidence.stock_buggy_matches_vehicle =
        stock_buggy == buggy ? 1U : 0U;
    if (stock_driver &&
        (!evidence.stock_buggy_present ||
         !evidence.stock_buggy_matches_vehicle))
        goto publish;
    evidence.velocity_available =
        dluma_player_media_buggy_velocity(
            profile, buggy, &velocity,
            &velocity_x, &velocity_y, &velocity_z) ? 1U : 0U;
    if (evidence.velocity_available) {
        mph = velocity * 2.236936;
        evidence.velocity_finite =
            dluma_player_media_double_finite(mph) ? 1U : 0U;
    }
    __try {
        if (dluma_is_readable(root_slot, sizeof(*root_slot)) &&
            *root_slot == root &&
            dluma_is_readable(
                (BYTE*)root + 0x780U, sizeof(void*)) &&
            *(void**)((BYTE*)root + 0x780U) == actor &&
            dluma_player_media_validate_playerdi(profile, actor) &&
            dluma_player_media_validate_car_buggy(profile, buggy)) {
            revalidated_vehicle =
                profile->current_vehicle(actor);
            revalidated_driver =
                profile->vehicle_driver(buggy);
            identity_stable =
                revalidated_vehicle == vehicle &&
                revalidated_vehicle == buggy &&
                revalidated_driver == actor;
        }
    }
    __except(1) {
        identity_stable = 0;
    }
    evidence.identities_stable =
        identity_stable ? 1U : 0U;

publish:
    dluma_vehicle_gate_evaluate(
        &evidence, &decision);
    state->available = decision.available;
    state->local_player_present =
        decision.local_player_present;
    state->vehicle_present =
        evidence.current_vehicle_present;
    state->is_local_driver = decision.is_local_driver;
    state->is_buggy = decision.is_buggy;
    if (decision.reason == DLUMA_DL_BUGGY_GATE_READY) {
        state->velocity_x = velocity_x;
        state->velocity_y = velocity_y;
        state->velocity_z = velocity_z;
        state->speed_mps = velocity;
        state->speed_mph = mph;
    }
    dluma_player_media_publish_buggy_gate_diagnostic(
        profile, &decision);
    return DLUMA_MEDIA_RESULT_SUCCESS;
}

static DWORD dluma_player_media_next_generation(DWORD generation)
{
    ++generation;
    if (!generation) ++generation;
    return generation;
}

static DlumaMediaToken dluma_player_media_bank_token(
    DWORD generation, unsigned int index)
{
    return ((DlumaMediaToken)generation << 32) |
           (DlumaMediaToken)(index + 1U);
}

static DlumaPlayerMediaBankSlot* dluma_player_media_bank_resolve(
    DlumaPlayerMediaPlayerProfile* profile, DlumaMediaToken token,
    unsigned int* index_out)
{
    unsigned int low = (unsigned int)(token & 0xffffffffULL);
    DWORD generation = (DWORD)(token >> 32);
    unsigned int index;
    if (index_out) *index_out = 0;
    if (!profile || !low ||
        low > DLUMA_COMPANION_ARCHIVE_MAX_BANKS || !generation)
        return 0;
    index = low - 1U;
    if (!profile->banks[index].in_use ||
        profile->banks[index].generation != generation ||
        ((!profile->banks[index].prepared &&
          (!profile->banks[index].pack ||
           !profile->banks[index].manifest.valid)) ||
         (profile->banks[index].prepared &&
          profile->banks[index].prepared_descriptor.version !=
              DLUMA_MEDIA_STRUCT_VERSION)))
        return 0;
    if (index_out) *index_out = index;
    return &profile->banks[index];
}

static DlumaMediaToken dluma_player_media_wave_token(
    DWORD generation, unsigned int index)
{
    return ((DlumaMediaToken)generation << 32) |
           (DlumaMediaToken)(index + 1U);
}

static DlumaPlayerMediaWaveSlot* dluma_player_media_wave_resolve(
    DlumaPlayerMediaPlayerProfile* profile, DlumaMediaToken token,
    unsigned int* index_out)
{
    unsigned int low = (unsigned int)(token & 0xffffffffULL);
    DWORD generation = (DWORD)(token >> 32);
    unsigned int index;
    if (index_out) *index_out = 0;
    if (!profile || !low || low > DLUMA_DL_PLAYER_MEDIA_MAX_WAVES ||
        !generation)
        return 0;
    index = low - 1U;
    if (!profile->waves[index].in_use ||
        profile->waves[index].generation != generation ||
        profile->waves[index].engine_handle <= 0)
        return 0;
    if (index_out) *index_out = index;
    return &profile->waves[index];
}

static void dluma_player_media_wave_clear(DlumaPlayerMediaWaveSlot* slot)
{
    DWORD generation;
    if (!slot) return;
    generation = slot->generation;
    memset(slot, 0, sizeof(*slot));
    slot->generation = generation;
}

static int dluma_player_media_wave_name_view(
    const char* name, DlumaPlayerMediaTtlStringView* view)
{
    unsigned int length = 0;
    if (!name || !view) return 0;
    while (length < DLUMA_COMPANION_ARCHIVE_MAX_CUE_NAME) {
        unsigned char value;
        if (!dluma_is_readable(name + length, 1U)) return 0;
        value = (unsigned char)name[length];
        if (!value) {
            if (!length) return 0;
            view->data = name;
            view->size = length;
            view->capacity = length;
            return 1;
        }
        ++length;
    }
    return 0;
}

static int dluma_player_media_prepared_alias_valid(const char* value)
{
    unsigned int index;
    unsigned int segment_length = 0;
    if (!value || !value[0]) return 0;
    for (index = 0;
         index < DLUMA_MEDIA_PREPARED_BANK_ALIAS_CAPACITY;
         ++index) {
        unsigned char character;
        if (!dluma_is_readable(value + index, 1U)) return 0;
        character = (unsigned char)value[index];
        if (!character)
            return index > 0U && segment_length > 0U;
        if (character == (unsigned char)'/') {
            if (!segment_length) return 0;
            segment_length = 0;
            continue;
        }
        if (!((character >= (unsigned char)'a' &&
               character <= (unsigned char)'z') ||
              (character >= (unsigned char)'A' &&
               character <= (unsigned char)'Z') ||
              (character >= (unsigned char)'0' &&
               character <= (unsigned char)'9') ||
              character == (unsigned char)'_' ||
              character == (unsigned char)'-'))
            return 0;
        ++segment_length;
    }
    return 0;
}

static int dluma_player_media_prepared_cue_valid(const char* value)
{
    unsigned int index;
    if (!value || !value[0]) return 0;
    for (index = 0; index < DLUMA_MEDIA_PREPARED_CUE_CAPACITY; ++index) {
        unsigned char character;
        if (!dluma_is_readable(value + index, 1U)) return 0;
        character = (unsigned char)value[index];
        if (!character) return index > 0U;
        if (!((character >= (unsigned char)'a' &&
               character <= (unsigned char)'z') ||
              (character >= (unsigned char)'A' &&
               character <= (unsigned char)'Z') ||
              (character >= (unsigned char)'0' &&
               character <= (unsigned char)'9') ||
              character == (unsigned char)'_' ||
              character == (unsigned char)'-'))
            return 0;
    }
    return 0;
}

static int dluma_player_media_prepared_fingerprint_valid(
    const char* value)
{
    unsigned int index;
    if (!value) return 0;
    for (index = 0; index < 64U; ++index) {
        unsigned char character;
        if (!dluma_is_readable(value + index, 1U)) return 0;
        character = (unsigned char)value[index];
        if (!((character >= (unsigned char)'0' &&
               character <= (unsigned char)'9') ||
              (character >= (unsigned char)'a' &&
               character <= (unsigned char)'f') ||
              (character >= (unsigned char)'A' &&
               character <= (unsigned char)'F')))
            return 0;
    }
    return dluma_is_readable(value + 64U, 1U) && !value[64];
}

static int dluma_player_media_prepared_descriptor_valid(
    const DlumaMediaPreparedBankDescriptor* descriptor)
{
    if (!descriptor ||
        !dluma_is_readable(descriptor, sizeof(*descriptor)) ||
        descriptor->size < sizeof(*descriptor) ||
        descriptor->version != DLUMA_MEDIA_STRUCT_VERSION ||
        descriptor->source_mounted != 1U ||
        descriptor->source_locked != 1U)
        return 0;
    return dluma_player_media_prepared_alias_valid(
               descriptor->engine_bank_alias) &&
           dluma_player_media_prepared_cue_valid(descriptor->cue_name) &&
           dluma_player_media_prepared_fingerprint_valid(
               descriptor->content_fingerprint);
}

static int dluma_player_media_prepared_bank_identity_equal(
    const DlumaMediaPreparedBankDescriptor* left,
    const DlumaMediaPreparedBankDescriptor* right)
{
    return left && right &&
           dluma_stricmp(left->engine_bank_alias,
                       right->engine_bank_alias) == 0 &&
           dluma_stricmp(left->content_fingerprint,
                       right->content_fingerprint) == 0;
}

static int dluma_player_media_cue_probe_owner_internal(
    DlumaPlayerMediaPlayerProfile* profile,
    void* owner,
    const char* cue_name,
    float* duration_seconds_out,
    DlumaMediaToken* duration_ms_out)
{
    DlumaPlayerMediaTtlStringView view;
    float duration_seconds = 0.0f;
    int delivered = 1;
    if (duration_seconds_out) *duration_seconds_out = 0.0f;
    if (duration_ms_out) *duration_ms_out = 0;
    if (!profile || !cue_name || !duration_seconds_out ||
        !duration_ms_out || !owner ||
        !dluma_is_readable(owner, sizeof(void*)) ||
        !profile->get_wave_duration_name ||
        !dluma_player_media_wave_name_view(cue_name, &view))
        return 0;
    __try {
        duration_seconds =
            profile->get_wave_duration_name(owner, &view);
    }
    __except(1) {
        delivered = 0;
    }
    if (!delivered ||
        !dluma_player_media_float_finite(duration_seconds) ||
        duration_seconds <= 0.0f ||
        duration_seconds >
            (float)(DLUMA_DL_PLAYER_MEDIA_MAX_POSITION_MS / 1000ULL))
        return 0;
    *duration_seconds_out = duration_seconds;
    *duration_ms_out = (DlumaMediaToken)(duration_seconds * 1000.0f + 0.5f);
    return *duration_ms_out > 0 &&
           *duration_ms_out <= DLUMA_DL_PLAYER_MEDIA_MAX_POSITION_MS;
}

static int dluma_player_media_cue_probe_internal(
    DlumaPlayerMediaPlayerProfile* profile,
    const char* cue_name,
    float* duration_seconds_out,
    DlumaMediaToken* duration_ms_out)
{
    void* manager = 0;
    if (!profile ||
        !dluma_player_media_validate_wave_owner(profile, &manager) ||
        (profile->music_owned &&
         manager != profile->owned_music_manager))
        return 0;
    return dluma_player_media_cue_probe_owner_internal(
        profile, manager, cue_name,
        duration_seconds_out, duration_ms_out);
}

static int dluma_player_media_duration_probe_internal(
    DlumaPlayerMediaPlayerProfile* profile,
    const DlumaCompanionArchive* pack,
    const DlumaCompanionArchiveAudioManifest* manifest,
    const char* cue_name,
    float* duration_seconds_out, DlumaMediaToken* duration_ms_out)
{
    DWORD expected_duration_ms = 0;
    float duration_seconds = 0.0f;
    DlumaMediaToken actual_duration_ms = 0;
    float expected_seconds;
    float difference;
    if (duration_seconds_out) *duration_seconds_out = 0.0f;
    if (duration_ms_out) *duration_ms_out = 0;
    if (!profile || !pack || !manifest || !manifest->valid ||
        !cue_name || !duration_seconds_out ||
        !duration_ms_out || !profile->get_wave_duration_name ||
        !dluma_companion_archive_manifest_has_cue(
            pack, manifest->bank_identifier,
            cue_name, &expected_duration_ms) ||
        !expected_duration_ms ||
        !dluma_player_media_cue_probe_internal(
            profile, cue_name, &duration_seconds,
            &actual_duration_ms))
        return 0;
    expected_seconds = (float)expected_duration_ms * 0.001f;
    difference = duration_seconds - expected_seconds;
    if (difference < 0.0f) difference = -difference;
    if (!dluma_player_media_float_finite(difference) ||
        difference > 0.0011f)
        return 0;
    *duration_seconds_out = duration_seconds;
    *duration_ms_out = (DlumaMediaToken)expected_duration_ms;
    return 1;
}

static void dluma_player_media_report_bank_diagnostic(
    DlumaPlayerMediaPlayerProfile* profile, unsigned int reason,
    const char* cue_name)
{
    const char* value;
    const char* level;
    if (!profile || profile->bank_diagnostic_reason == reason)
        return;
    profile->bank_diagnostic_reason = reason;
    level = reason == DLUMA_DL_BANK_DIAGNOSTIC_READY ? "INFO" : "ERROR";
    switch (reason) {
    case DLUMA_DL_BANK_DIAGNOSTIC_LOAD_DELIVERY_FAILED:
        value = "load_wave_bank_delivery_failed";
        break;
    case DLUMA_DL_BANK_DIAGNOSTIC_LOAD_RESULT_FAILED:
        value = "load_wave_bank_result_failed";
        break;
    case DLUMA_DL_BANK_DIAGNOSTIC_CUE_PROBE_FAILED:
        value = "cue_missing_or_duration_mismatch";
        break;
    case DLUMA_DL_BANK_DIAGNOSTIC_READY:
        value = "manifest_cues_validated";
        break;
    default:
        return;
    }
    dluma_log_pair(level, "player_media_bank_probe_ready", value);
    if (reason == DLUMA_DL_BANK_DIAGNOSTIC_CUE_PROBE_FAILED &&
        cue_name)
        dluma_log_pair("ERROR", "player_media_bank_probe_cue", cue_name);
}

int dluma_player_media_adapter_open_bank(
    DlumaCompanionArchive* pack,
    const DlumaCompanionArchiveEntry* bank_entry,
    DlumaMediaToken* bank_out)
{
    DlumaPlayerMediaPlayerProfile* profile =
        &g_dluma_player_media_player;
    const DlumaCompanionArchiveAudioManifest* bank_manifest = 0;
    DlumaPlayerMediaBankSlot* slot = 0;
    void* audio = 0;
    unsigned int i;
    unsigned int slot_index = 0;
    char engine_bank_name[DLUMA_COMPANION_ARCHIVE_MAX_ENGINE_BANK_NAME];
    int load_result = -1;
    int delivered = 1;
    engine_bank_name[0] = 0;
    if (bank_out) *bank_out = 0;
    if (pack && bank_entry && pack->audio_catalog.valid) {
        for (i = 0; i < pack->audio_catalog.bank_count; ++i) {
            if (pack->audio_catalog.banks[i].valid &&
                dluma_stricmp(
                    bank_entry->virtual_path,
                    pack->audio_catalog.banks[i].bank_path) == 0) {
                bank_manifest = &pack->audio_catalog.banks[i];
                break;
            }
        }
    }
    if (!pack || !bank_entry || !bank_out || !pack->validated ||
        !bank_manifest || !bank_entry->is_csb ||
        !(dluma_player_media_adapter_capabilities() &
          DLUMA_PLAYER_MEDIA_CAP_WAVE_BANK) ||
        !profile->load_wave_bank ||
        !dluma_player_media_validate_audio_object(profile, &audio))
        return DLUMA_MEDIA_RESULT_UNAVAILABLE;
    for (i = 0; i < DLUMA_COMPANION_ARCHIVE_MAX_BANKS; ++i) {
        DlumaPlayerMediaBankSlot* existing = &profile->banks[i];
        if (existing->in_use && existing->pack == pack &&
            dluma_stricmp(
                existing->manifest.bank_identifier,
                bank_manifest->bank_identifier) == 0) {
            *bank_out = dluma_player_media_bank_token(
                existing->generation, i);
            return DLUMA_MEDIA_RESULT_SUCCESS;
        }
        if (!slot && !existing->in_use) {
            slot = existing;
            slot_index = i;
        }
    }
    if (!slot) return DLUMA_MEDIA_RESULT_BUSY;
    pack->audio_manifest = *bank_manifest;
    if (!dluma_companion_archive_prepare_bank(
            pack, bank_entry->virtual_path,
            engine_bank_name, DLUMA_ARRAY_COUNT(engine_bank_name)))
        return DLUMA_MEDIA_RESULT_UNAVAILABLE;
    __try {
        load_result = profile->load_wave_bank(
            audio, engine_bank_name, 0U);
    }
    __except(1) {
        delivered = 0;
    }
    if (!delivered) {
        dluma_player_media_report_bank_diagnostic(
            profile, DLUMA_DL_BANK_DIAGNOSTIC_LOAD_DELIVERY_FAILED, 0);
        return DLUMA_MEDIA_RESULT_UNAVAILABLE;
    }
    if (load_result != 1) {
        dluma_player_media_report_bank_diagnostic(
            profile, DLUMA_DL_BANK_DIAGNOSTIC_LOAD_RESULT_FAILED, 0);
        return DLUMA_MEDIA_RESULT_UNAVAILABLE;
    }
    for (i = 0; i < bank_manifest->cue_count; ++i) {
        float duration_seconds = 0.0f;
        DlumaMediaToken duration_ms = 0;
        if (!dluma_player_media_duration_probe_internal(
                profile, pack, bank_manifest,
                bank_manifest->cues[i].name,
                &duration_seconds, &duration_ms)) {
            dluma_player_media_report_bank_diagnostic(
                profile, DLUMA_DL_BANK_DIAGNOSTIC_CUE_PROBE_FAILED,
                bank_manifest->cues[i].name);
            return DLUMA_MEDIA_RESULT_UNAVAILABLE;
        }
    }
    slot->generation = dluma_player_media_next_generation(slot->generation);
    slot->pack = pack;
    slot->manifest = *bank_manifest;
    memset(&slot->prepared_descriptor, 0,
           sizeof(slot->prepared_descriptor));
    slot->prepared = 0U;
    slot->in_use = 1U;
    *bank_out = dluma_player_media_bank_token(
        slot->generation, slot_index);
    dluma_log_pair(
        "INFO", "player_media_wave_bank_load",
        "load_delivered_and_cues_validated");
    dluma_player_media_report_bank_diagnostic(
        profile, DLUMA_DL_BANK_DIAGNOSTIC_READY, 0);
    return DLUMA_MEDIA_RESULT_SUCCESS;
}

static int dluma_player_media_open_prepared_bank_for_owner(
    DlumaPlayerMediaPlayerProfile* profile,
    const DlumaMediaPreparedBankDescriptor* descriptor,
    void* probe_owner,
    DlumaMediaToken* bank_out)
{
    DlumaPlayerMediaBankSlot* slot = 0;
    void* audio = 0;
    float duration_seconds = 0.0f;
    DlumaMediaToken duration_ms = 0;
    unsigned int index;
    unsigned int slot_index = 0;
    int load_result = -1;
    int delivered = 1;
    if (bank_out) *bank_out = 0;
    if (!profile || !bank_out || !probe_owner ||
        !dluma_player_media_prepared_descriptor_valid(descriptor))
        return DLUMA_MEDIA_RESULT_INVALID;
    if (!profile->load_wave_bank ||
        !profile->get_wave_duration_name ||
        !dluma_player_media_validate_audio_object(profile, &audio))
        return DLUMA_MEDIA_RESULT_UNAVAILABLE;
    for (index = 0; index < DLUMA_COMPANION_ARCHIVE_MAX_BANKS; ++index) {
        DlumaPlayerMediaBankSlot* existing = &profile->banks[index];
        if (existing->in_use && existing->prepared &&
            dluma_stricmp(
                existing->prepared_descriptor.engine_bank_alias,
                descriptor->engine_bank_alias) == 0) {
            if (!dluma_player_media_prepared_bank_identity_equal(
                    &existing->prepared_descriptor, descriptor))
                return DLUMA_MEDIA_RESULT_BUSY;
            if (!dluma_player_media_cue_probe_owner_internal(
                    profile, probe_owner, descriptor->cue_name,
                    &duration_seconds, &duration_ms)) {
                /* The engine may have unloaded this bank with the outgoing
                 * level while the loader retained its descriptor. Reload it
                 * and validate the cue before issuing a fresh token. */
                slot = existing;
                slot_index = index;
                dluma_log_pair("INFO", "player_media_prepared_bank_reload",
                             descriptor->engine_bank_alias);
                break;
            }
            *bank_out = dluma_player_media_bank_token(
                existing->generation, index);
            dluma_log_pair(
                "INFO", "player_media_prepared_bank_cue_validated",
                descriptor->cue_name);
            return DLUMA_MEDIA_RESULT_SUCCESS;
        }
        if (!slot && !existing->in_use) {
            slot = existing;
            slot_index = index;
        }
    }
    if (!slot) return DLUMA_MEDIA_RESULT_BUSY;
    __try {
        load_result = profile->load_wave_bank(
            audio, descriptor->engine_bank_alias, 0U);
    }
    __except(1) {
        delivered = 0;
    }
    if (!delivered || load_result != 1) {
        dluma_log_pair(
            "ERROR", "player_media_prepared_bank_load",
            !delivered ? "load_delivery_failed" : "load_result_failed");
        return DLUMA_MEDIA_RESULT_UNAVAILABLE;
    }
    if (!dluma_player_media_cue_probe_owner_internal(
            profile, probe_owner, descriptor->cue_name,
            &duration_seconds, &duration_ms)) {
        dluma_log_pair(
            "ERROR", "player_media_prepared_bank_probe",
            descriptor->cue_name);
        return DLUMA_MEDIA_RESULT_UNAVAILABLE;
    }
    slot->generation = dluma_player_media_next_generation(slot->generation);
    slot->pack = 0;
    memset(&slot->manifest, 0, sizeof(slot->manifest));
    slot->prepared_descriptor = *descriptor;
    slot->prepared = 1U;
    slot->in_use = 1U;
    *bank_out = dluma_player_media_bank_token(
        slot->generation, slot_index);
    dluma_log_pair(
        "INFO", "player_media_prepared_bank_load",
        descriptor->engine_bank_alias);
    dluma_log_pair(
        "INFO", "player_media_prepared_bank_cue_validated",
        descriptor->cue_name);
    return DLUMA_MEDIA_RESULT_SUCCESS;
}

int dluma_player_media_adapter_open_prepared_bank(
    const DlumaMediaPreparedBankDescriptor* descriptor,
    DlumaMediaToken* bank_out)
{
    DlumaPlayerMediaPlayerProfile* profile =
        &g_dluma_player_media_player;
    void* owner = 0;
    if (bank_out) *bank_out = 0;
    if (!bank_out ||
        !dluma_player_media_prepared_descriptor_valid(descriptor))
        return DLUMA_MEDIA_RESULT_INVALID;
    if (!(dluma_player_media_adapter_capabilities() &
          DLUMA_PLAYER_MEDIA_CAP_WAVE_BANK) ||
        !dluma_player_media_validate_wave_owner(profile, &owner) ||
        (profile->music_owned &&
         owner != profile->owned_music_manager))
        return DLUMA_MEDIA_RESULT_UNAVAILABLE;
    return dluma_player_media_open_prepared_bank_for_owner(
        profile, descriptor, owner, bank_out);
}

int dluma_player_media_adapter_prepare_main_menu_bank(
    const DlumaMediaPreparedBankDescriptor* descriptor,
    void* main_menu_module,
    DlumaMediaToken* bank_out)
{
    DlumaPlayerMediaPlayerProfile* profile =
        &g_dluma_player_media_player;
    if (bank_out) *bank_out = 0;
    if (!bank_out ||
        !dluma_player_media_prepared_descriptor_valid(descriptor))
        return DLUMA_MEDIA_RESULT_INVALID;
    if (!profile->exact_profile || !profile->wave_bank_ready ||
        !profile->wave_exports_ready || !profile->load_wave_bank ||
        !profile->get_wave_duration_name ||
        !dluma_player_media_validate_main_menu_audio_owner(
            profile, main_menu_module))
        return DLUMA_MEDIA_RESULT_UNAVAILABLE;
    return dluma_player_media_open_prepared_bank_for_owner(
        profile, descriptor,
        (BYTE*)main_menu_module +
            DLUMA_DL_MAIN_MENU_MODULE_IGSOBJECT_OFFSET,
        bank_out);
}

int dluma_player_media_adapter_bank_loaded_for_pack(
    const DlumaCompanionArchive* pack)
{
    DlumaPlayerMediaPlayerProfile* profile =
        &g_dluma_player_media_player;
    unsigned int index;
    if (!pack) return 0;
    for (index = 0; index < DLUMA_COMPANION_ARCHIVE_MAX_BANKS; ++index)
        if (profile->banks[index].in_use &&
            profile->banks[index].pack == pack)
            return 1;
    return 0;
}

static int dluma_player_media_bank_cue_probe(
    DlumaPlayerMediaPlayerProfile* profile,
    const DlumaPlayerMediaBankSlot* slot,
    const char* cue_name,
    float* duration_seconds_out,
    DlumaMediaToken* duration_ms_out)
{
    if (!profile || !slot || !cue_name)
        return 0;
    if (slot->prepared) {
        /* Prepared-bank tokens are loader-internal.  The owning subsystem
         * authorizes cue names against its immutable catalog; this layer
         * then verifies that the selected name resolves in the loaded engine
         * registry.  Restricting a token to its initial probe cue prevented
         * one validated CSB from serving multiple XUI sounds. */
        if (!dluma_player_media_prepared_cue_valid(cue_name))
            return 0;
        return dluma_player_media_cue_probe_internal(
            profile, cue_name, duration_seconds_out,
            duration_ms_out);
    }
    return dluma_player_media_duration_probe_internal(
        profile, slot->pack, &slot->manifest, cue_name,
        duration_seconds_out, duration_ms_out);
}

int dluma_player_media_adapter_probe_duration(
    DlumaMediaToken bank, const char* bank_entry,
    DlumaMediaToken* duration_ms_out)
{
    DlumaPlayerMediaPlayerProfile* profile =
        &g_dluma_player_media_player;
    DlumaPlayerMediaBankSlot* slot;
    float duration_seconds = 0.0f;
    if (duration_ms_out) *duration_ms_out = 0;
    slot = dluma_player_media_bank_resolve(profile, bank, 0);
    if (!duration_ms_out || !slot)
        return DLUMA_MEDIA_RESULT_STALE;
    if (!dluma_player_media_bank_cue_probe(
            profile, slot, bank_entry,
            &duration_seconds, duration_ms_out))
        return DLUMA_MEDIA_RESULT_UNAVAILABLE;
    return DLUMA_MEDIA_RESULT_SUCCESS;
}

int dluma_player_media_adapter_play(
    DlumaMediaToken bank, const char* bank_entry,
    const DlumaMediaCuePlayRequest* request,
    DlumaMediaToken* wave_out)
{
    DlumaPlayerMediaPlayerProfile* profile =
        &g_dluma_player_media_player;
    DlumaPlayerMediaBankSlot* bank_slot;
    DlumaPlayerMediaTtlStringView view;
    DlumaPlayerMediaWaveSlot* slot = 0;
    DlumaMediaToken duration_ms = 0;
    float duration_seconds = 0.0f;
    float fade_in_seconds = 0.0f;
    float fade_out_seconds = 0.0f;
    float start_seconds;
    unsigned int index;
    unsigned int minimum_request_size =
        (unsigned int)sizeof(*request) - 2U * sizeof(unsigned int);
    int engine_handle = 0;
    int delivered = 1;
    int volume_set = 0;
    int playing = 0;
    void* manager = 0;
    if (wave_out) *wave_out = 0;
    bank_slot = dluma_player_media_bank_resolve(profile, bank, 0);
    if (!request || !wave_out ||
        !dluma_is_readable(request, minimum_request_size) ||
        request->size < minimum_request_size ||
        request->version != DLUMA_MEDIA_STRUCT_VERSION ||
        request->route != DLUMA_MEDIA_ROUTE_MUSIC ||
        (request->flags & ~DLUMA_MEDIA_CUE_FLAG_LOOP) != 0U ||
        !dluma_player_media_float_finite(request->initial_gain) ||
        request->initial_gain < 0.0f || request->initial_gain > 4.0f ||
        request->start_ms > DLUMA_DL_PLAYER_MEDIA_MAX_POSITION_MS ||
        !bank_slot ||
        !dluma_player_media_bank_cue_probe(
            profile, bank_slot, bank_entry,
            &duration_seconds, &duration_ms) ||
        request->start_ms > duration_ms ||
        !dluma_player_media_wave_name_view(bank_entry, &view) ||
        !dluma_player_media_validate_wave_owner(profile, &manager) ||
        (profile->music_owned &&
         manager != profile->owned_music_manager))
        return DLUMA_MEDIA_RESULT_INVALID;
    if (request->size >= sizeof(*request)) {
        if (!dluma_is_readable(request, sizeof(*request)) ||
            request->fade_in_ms > 60000U ||
            request->fade_out_ms > 60000U)
            return DLUMA_MEDIA_RESULT_INVALID;
        fade_in_seconds = (float)request->fade_in_ms * 0.001f;
        fade_out_seconds = (float)request->fade_out_ms * 0.001f;
    }
    for (index = 0; index < DLUMA_DL_PLAYER_MEDIA_MAX_WAVES; ++index) {
        if (!profile->waves[index].in_use) {
            slot = &profile->waves[index];
            break;
        }
    }
    if (!slot) return DLUMA_MEDIA_RESULT_BUSY;
    start_seconds = (float)request->start_ms * 0.001f;
    __try {
        engine_handle = profile->play_wave_2d(
            manager, &view,
            (request->flags & DLUMA_MEDIA_CUE_FLAG_LOOP) ? 1U : 0U,
            fade_in_seconds, fade_out_seconds,
            1U, start_seconds, 0, 0U, 0U);
        if (engine_handle > 0) {
            volume_set = profile->set_wave_volume(
                manager, engine_handle, request->initial_gain) ? 1 : 0;
            playing = profile->is_wave_playing(
                manager, engine_handle) ? 1 : 0;
        }
    }
    __except(1) {
        delivered = 0;
    }
    if (!delivered || engine_handle <= 0 || !volume_set || !playing) {
        if (engine_handle > 0) {
            __try {
                profile->stop_wave(manager, engine_handle, 0.0f);
            }
            __except(1) {
            }
        }
        dluma_log_pair(
            "ERROR", "player_media_stem_created",
            engine_handle <= 0 ? "stem_create_failed" :
            (!volume_set ? "initial_volume_failed" : "stem_not_playing"));
        return DLUMA_MEDIA_RESULT_UNAVAILABLE;
    }
    slot->generation = dluma_player_media_next_generation(slot->generation);
    slot->engine_handle = engine_handle;
    slot->duration_seconds = duration_seconds;
    slot->owner = manager;
    dluma_str_copy(
        slot->cue_name, DLUMA_ARRAY_COUNT(slot->cue_name), bank_entry);
    slot->stopping = 0U;
    slot->in_use = 1U;
    *wave_out = dluma_player_media_wave_token(slot->generation, index);
    dluma_log_pair(
        "INFO", "player_media_stem_created", slot->cue_name);
    return DLUMA_MEDIA_RESULT_SUCCESS;
}

int dluma_player_media_adapter_wave_state(
    DlumaMediaToken wave, DlumaMediaCueState* state)
{
    DlumaPlayerMediaPlayerProfile* profile =
        &g_dluma_player_media_player;
    DlumaPlayerMediaWaveSlot* slot;
    float position_seconds = 0.0f;
    DlumaMediaToken duration_ms;
    unsigned int playing = 0;
    void* manager = 0;
    int delivered = 1;
    if (!state) return DLUMA_MEDIA_RESULT_INVALID;
    memset(state, 0, sizeof(*state));
    state->size = sizeof(*state);
    state->version = DLUMA_MEDIA_STRUCT_VERSION;
    slot = dluma_player_media_wave_resolve(profile, wave, 0);
    if (!slot) return DLUMA_MEDIA_RESULT_STALE;
    if (!dluma_player_media_validate_wave_owner(profile, &manager) ||
        manager != slot->owner ||
        (profile->music_owned &&
         manager != profile->owned_music_manager))
        return DLUMA_MEDIA_RESULT_UNAVAILABLE;
    duration_ms = (DlumaMediaToken)(
        slot->duration_seconds * 1000.0f + 0.5f);
    __try {
        playing = profile->is_wave_playing(
            manager, slot->engine_handle) ? 1U : 0U;
        if (playing)
            position_seconds = profile->get_wave_time_seconds(
                manager, slot->engine_handle);
    }
    __except(1) {
        delivered = 0;
    }
    if (!delivered ||
        (playing &&
         (!dluma_player_media_float_finite(position_seconds) ||
          position_seconds < 0.0f ||
          position_seconds * 1000.0f >
              (float)DLUMA_DL_PLAYER_MEDIA_MAX_POSITION_MS)))
        return DLUMA_MEDIA_RESULT_UNAVAILABLE;
    state->playing = playing;
    state->duration_ms = duration_ms;
    if (playing)
        state->position_ms = (DlumaMediaToken)(
            position_seconds * 1000.0f + 0.5f);
    if (!playing) dluma_player_media_wave_clear(slot);
    return DLUMA_MEDIA_RESULT_SUCCESS;
}

int dluma_player_media_adapter_seek(
    DlumaMediaToken wave, DlumaMediaToken position_ms)
{
    DlumaPlayerMediaPlayerProfile* profile =
        &g_dluma_player_media_player;
    DlumaPlayerMediaWaveSlot* slot =
        dluma_player_media_wave_resolve(profile, wave, 0);
    void* manager = 0;
    unsigned char moved = 0;
    int delivered = 1;
    if (!slot || slot->stopping) return DLUMA_MEDIA_RESULT_STALE;
    if (position_ms > DLUMA_DL_PLAYER_MEDIA_MAX_POSITION_MS ||
        (float)position_ms > slot->duration_seconds * 1000.0f + 1.0f)
        return DLUMA_MEDIA_RESULT_INVALID;
    if (!dluma_player_media_validate_wave_owner(profile, &manager) ||
        manager != slot->owner ||
        (profile->music_owned &&
         manager != profile->owned_music_manager))
        return DLUMA_MEDIA_RESULT_UNAVAILABLE;
    __try {
        moved = profile->set_wave_position_seconds(
            manager, slot->engine_handle,
            (float)position_ms * 0.001f);
    }
    __except(1) {
        delivered = 0;
    }
    return delivered && moved
        ? DLUMA_MEDIA_RESULT_SUCCESS
        : DLUMA_MEDIA_RESULT_UNAVAILABLE;
}

int dluma_player_media_adapter_gain(
    DlumaMediaToken wave, float gain)
{
    DlumaPlayerMediaPlayerProfile* profile =
        &g_dluma_player_media_player;
    DlumaPlayerMediaWaveSlot* slot =
        dluma_player_media_wave_resolve(profile, wave, 0);
    void* manager = 0;
    unsigned char changed = 0;
    int delivered = 1;
    if (!slot || slot->stopping) return DLUMA_MEDIA_RESULT_STALE;
    if (!dluma_player_media_float_finite(gain) ||
        gain < 0.0f || gain > 4.0f)
        return DLUMA_MEDIA_RESULT_INVALID;
    if (!dluma_player_media_validate_wave_owner(profile, &manager) ||
        manager != slot->owner ||
        (profile->music_owned &&
         manager != profile->owned_music_manager))
        return DLUMA_MEDIA_RESULT_UNAVAILABLE;
    __try {
        changed = profile->set_wave_volume(
            manager, slot->engine_handle, gain);
    }
    __except(1) {
        delivered = 0;
    }
    return delivered && changed
        ? DLUMA_MEDIA_RESULT_SUCCESS
        : DLUMA_MEDIA_RESULT_UNAVAILABLE;
}

int dluma_player_media_adapter_wave_filter(
    DlumaMediaToken wave, float wet_mix, float cutoff_hz)
{
    (void)wave;
    (void)wet_mix;
    (void)cutoff_hz;
    return DLUMA_MEDIA_RESULT_UNAVAILABLE;
}

int dluma_player_media_adapter_stop_wave_fade(
    DlumaMediaToken wave, unsigned int fade_out_ms)
{
    DlumaPlayerMediaPlayerProfile* profile =
        &g_dluma_player_media_player;
    DlumaPlayerMediaWaveSlot* slot =
        dluma_player_media_wave_resolve(profile, wave, 0);
    void* manager = 0;
    int delivered = 1;
    if (!slot) return DLUMA_MEDIA_RESULT_STALE;
    if (fade_out_ms > 60000U) return DLUMA_MEDIA_RESULT_INVALID;
    if (!dluma_player_media_validate_wave_owner(profile, &manager) ||
        manager != slot->owner ||
        (profile->music_owned &&
         manager != profile->owned_music_manager))
        return DLUMA_MEDIA_RESULT_UNAVAILABLE;
    __try {
        profile->stop_wave(
            manager, slot->engine_handle,
            (float)fade_out_ms * 0.001f);
    }
    __except(1) {
        delivered = 0;
    }
    if (!delivered) return DLUMA_MEDIA_RESULT_UNAVAILABLE;
    if (fade_out_ms) {
        slot->stopping = 1U;
    } else {
        dluma_player_media_wave_clear(slot);
    }
    return DLUMA_MEDIA_RESULT_SUCCESS;
}

int dluma_player_media_adapter_stop_wave(DlumaMediaToken wave)
{
    return dluma_player_media_adapter_stop_wave_fade(wave, 0U);
}

int dluma_player_media_adapter_music_acquire(
    unsigned int fade_out_ms, DlumaMediaToken* ownership_out)
{
    DlumaPlayerMediaPlayerProfile* profile =
        &g_dluma_player_media_player;
    void* manager = 0;
    void* main_menu_module = 0;
    int delivered = 1;
    int confirmed = 0;
    int recovery_delivered = 1;
    int recovered = 0;
    int recovery_filter_restored = 1;
    int stock_was_playing = 0;
    int stock_stopped = 0;
    int background_muted = 0;
    int stock_restarted = 0;
    int background_unmuted = 0;
    int frontend_route;
    if (ownership_out) *ownership_out = 0;
    if (!ownership_out || fade_out_ms > 60000U)
        return DLUMA_MEDIA_RESULT_INVALID;
    if (!(dluma_player_media_adapter_capabilities() &
          DLUMA_PLAYER_MEDIA_CAP_MUSIC_EXCLUSIVE))
        return DLUMA_MEDIA_RESULT_UNAVAILABLE;
    if (profile->music_owned) return DLUMA_MEDIA_RESULT_BUSY;
    frontend_route =
        profile->published_frontend_music_manager != 0;
    main_menu_module =
        profile->published_frontend_main_menu_module;
    if (frontend_route && main_menu_module) {
        if (!profile->frontend_music_ready ||
            !profile->frontend_music_mute ||
            !profile->frontend_music_unmute ||
            !profile->main_menu_music_ready ||
            !profile->main_menu_music_stop ||
            !profile->main_menu_music_start ||
            !dluma_player_media_validate_wave_owner(profile, &manager) ||
            manager != profile->published_frontend_music_manager ||
            !dluma_player_media_validate_frontend_main_menu_module(
                profile, main_menu_module))
            return DLUMA_MEDIA_RESULT_UNAVAILABLE;
        profile->music_generation = dluma_player_media_next_generation(
            profile->music_generation);
        profile->music_token =
            ((DlumaMediaToken)profile->music_generation << 32) | 1ULL;
        profile->owned_music_manager = manager;
        profile->owned_stock_music_controller = main_menu_module;
        profile->owned_music_route =
            DLUMA_DL_MUSIC_OWNERSHIP_FRONTEND_MAIN_MENU;
        profile->music_owned = 1U;
        __try {
            stock_was_playing =
                *(int*)((BYTE*)main_menu_module +
                    DLUMA_DL_MAIN_MENU_MODULE_MUSIC_HANDLE_OFFSET) != 0;
            profile->stock_music_was_playing =
                stock_was_playing ? 1U : 0U;
            profile->main_menu_music_stop(main_menu_module);
            stock_stopped =
                *(int*)((BYTE*)main_menu_module +
                    DLUMA_DL_MAIN_MENU_MODULE_MUSIC_HANDLE_OFFSET) == 0;
        }
        __except(1) {
            delivered = 0;
        }
        if (delivered && stock_stopped) {
            __try {
                profile->frontend_music_mute(
                    manager, (float)fade_out_ms * 0.001f);
                background_muted =
                    *(unsigned char*)((BYTE*)manager +
                        DLUMA_DL_FRONTEND_MUSICS_MANAGER_MUTED_OFFSET) != 0U;
            }
            __except(1) {
                delivered = 0;
            }
        }
        if (!delivered || !stock_stopped || !background_muted) {
            __try {
                profile->frontend_music_unmute(manager, 0.0f);
                background_unmuted =
                    *(unsigned char*)((BYTE*)manager +
                        DLUMA_DL_FRONTEND_MUSICS_MANAGER_MUTED_OFFSET) == 0U;
            }
            __except(1) {
                recovery_delivered = 0;
            }
            if (recovery_delivered) {
                if (stock_was_playing) {
                    __try {
                        profile->main_menu_music_start(main_menu_module);
                        stock_restarted =
                            *(int*)((BYTE*)main_menu_module +
                                DLUMA_DL_MAIN_MENU_MODULE_MUSIC_HANDLE_OFFSET) !=
                            0;
                    }
                    __except(1) {
                        recovery_delivered = 0;
                    }
                } else {
                    stock_restarted = 1;
                }
            }
            if (recovery_delivered && background_unmuted &&
                stock_restarted) {
                profile->music_token = 0;
                profile->owned_music_manager = 0;
                profile->owned_stock_music_controller = 0;
                profile->owned_music_route = DLUMA_DL_MUSIC_OWNERSHIP_NONE;
                profile->music_owned = 0U;
                profile->stock_music_was_playing = 0U;
            }
            dluma_log_pair(
                "ERROR", "player_media_music_suspend_acquired",
                recovery_delivered && background_unmuted &&
                        stock_restarted
                    ? "frontend_combined_stop_not_confirmed_stock_restored"
                    : "frontend_combined_stop_not_confirmed_ownership_retained");
            return recovery_delivered && background_unmuted &&
                    stock_restarted
                ? DLUMA_MEDIA_RESULT_UNAVAILABLE
                : DLUMA_MEDIA_RESULT_BUSY;
        }
        *ownership_out = profile->music_token;
        dluma_log_pair(
            "INFO", "player_media_music_suspend_acquired",
            "main_menu_dw_menu_stopped_background_manager_muted");
        return DLUMA_MEDIA_RESULT_SUCCESS;
    }
    if (frontend_route) {
        if (!profile->frontend_music_ready ||
            !profile->frontend_music_mute ||
            !profile->frontend_music_unmute ||
            !dluma_player_media_validate_wave_owner(profile, &manager) ||
            manager != profile->published_frontend_music_manager)
            return DLUMA_MEDIA_RESULT_UNAVAILABLE;
        profile->music_generation = dluma_player_media_next_generation(
            profile->music_generation);
        profile->music_token =
            ((DlumaMediaToken)profile->music_generation << 32) | 1ULL;
        profile->owned_music_manager = manager;
        profile->owned_stock_music_controller = 0;
        profile->owned_music_route =
            DLUMA_DL_MUSIC_OWNERSHIP_FRONTEND_MUSICS_MANAGER;
        profile->music_owned = 1U;
        profile->stock_music_was_playing = 0U;
        __try {
            profile->frontend_music_mute(
                manager, (float)fade_out_ms * 0.001f);
            confirmed =
                *(unsigned char*)((BYTE*)manager +
                    DLUMA_DL_FRONTEND_MUSICS_MANAGER_MUTED_OFFSET) != 0U;
        }
        __except(1) {
            delivered = 0;
        }
        if (!delivered || !confirmed) {
            __try {
                profile->frontend_music_unmute(manager, 0.0f);
                recovered =
                    *(unsigned char*)((BYTE*)manager +
                        DLUMA_DL_FRONTEND_MUSICS_MANAGER_MUTED_OFFSET) == 0U;
            }
            __except(1) {
                recovery_delivered = 0;
            }
            if (recovery_delivered && recovered) {
                profile->music_token = 0;
                profile->owned_music_manager = 0;
                profile->owned_stock_music_controller = 0;
                profile->owned_music_route = DLUMA_DL_MUSIC_OWNERSHIP_NONE;
                profile->music_owned = 0U;
                profile->stock_music_was_playing = 0U;
            }
            dluma_log_pair(
                "ERROR", "player_media_music_suspend_acquired",
                recovery_delivered && recovered
                    ? "frontend_mute_not_confirmed_stock_unmuted"
                    : "frontend_mute_not_confirmed_ownership_retained");
            return recovery_delivered && recovered
                ? DLUMA_MEDIA_RESULT_UNAVAILABLE
                : DLUMA_MEDIA_RESULT_BUSY;
        }
        *ownership_out = profile->music_token;
        dluma_log_pair(
            "INFO", "player_media_music_suspend_acquired",
            "frontend_musics_manager");
        return DLUMA_MEDIA_RESULT_SUCCESS;
    }
    if (!profile->music_stop || !profile->music_start ||
        !dluma_player_media_validate_music_manager(profile, &manager))
        return DLUMA_MEDIA_RESULT_UNAVAILABLE;
    memset(&profile->music_snapshot, 0,
           sizeof(profile->music_snapshot));
    if ((dluma_player_media_adapter_capabilities() &
         DLUMA_PLAYER_MEDIA_CAP_MUSIC_FILTER) &&
        !dluma_player_media_music_snapshot_take(
            profile, &profile->music_snapshot)) {
        profile->music_filter_ready = 0U;
        memset(&profile->music_snapshot, 0,
               sizeof(profile->music_snapshot));
        dluma_log_pair(
            "WARN", "player_media_music_filter_ready",
            "music_filter_snapshot_unavailable_handoff_continues");
    }

    profile->music_generation = dluma_player_media_next_generation(
        profile->music_generation);
    profile->music_token =
        ((DlumaMediaToken)profile->music_generation << 32) | 1ULL;
    profile->owned_music_manager = manager;
    profile->owned_stock_music_controller = 0;
    profile->owned_music_route =
        DLUMA_DL_MUSIC_OWNERSHIP_STOCK_MANAGER;
    profile->music_owned = 1U;
    profile->stock_music_was_playing = 0U;
    __try {
        profile->music_stop(manager);
        confirmed =
            ((*(unsigned char*)((BYTE*)manager +
                DLUMA_DL_MUSIC_MANAGER_FLAGS_OFFSET) &
              DLUMA_DL_MUSIC_MANAGER_PAUSED) != 0U) &&
            *(int*)((BYTE*)manager +
                DLUMA_DL_MUSIC_MANAGER_WANTED_TYPE_OFFSET) == -1;
    }
    __except(1) {
        delivered = 0;
    }
    if (!delivered || !confirmed) {
        __try {
            profile->music_start(manager, 0.0f);
            recovered =
                (*(unsigned char*)((BYTE*)manager +
                    DLUMA_DL_MUSIC_MANAGER_FLAGS_OFFSET) &
                 DLUMA_DL_MUSIC_MANAGER_PAUSED) == 0U;
        }
        __except(1) {
            recovery_delivered = 0;
        }
        if (recovery_delivered && recovered &&
            profile->music_snapshot.valid)
            recovery_filter_restored =
                dluma_player_media_music_snapshot_restore(
                    profile, &profile->music_snapshot);
        if (recovery_delivered && recovered &&
            recovery_filter_restored) {
            profile->music_token = 0;
            profile->owned_music_manager = 0;
            profile->owned_stock_music_controller = 0;
            profile->owned_music_route = DLUMA_DL_MUSIC_OWNERSHIP_NONE;
            profile->music_owned = 0U;
            profile->stock_music_was_playing = 0U;
            memset(&profile->music_snapshot, 0,
                   sizeof(profile->music_snapshot));
        }
        dluma_log_pair(
            "ERROR", "player_media_music_suspend_acquired",
            recovery_delivered && recovered &&
                    recovery_filter_restored
                ? "music_manager_stop_not_confirmed_stock_restarted"
                : "music_manager_stop_not_confirmed_ownership_retained");
        return recovery_delivered && recovered &&
                recovery_filter_restored
            ? DLUMA_MEDIA_RESULT_UNAVAILABLE
            : DLUMA_MEDIA_RESULT_BUSY;
    }
    *ownership_out = profile->music_token;
    dluma_log_pair(
        "INFO", "player_media_music_suspend_acquired", "1");
    return DLUMA_MEDIA_RESULT_SUCCESS;
}
int dluma_player_media_adapter_music_state(
    DlumaMediaToken ownership, DlumaMediaMusicState* state)
{
    DlumaPlayerMediaPlayerProfile* profile =
        &g_dluma_player_media_player;
    void* manager = 0;
    void* main_menu_module = 0;
    int delivered = 1;
    int confirmed = 0;
    if (!state) return DLUMA_MEDIA_RESULT_INVALID;
    memset(state, 0, sizeof(*state));
    state->size = sizeof(*state);
    state->version = DLUMA_MEDIA_STRUCT_VERSION;
    if (!profile->music_owned || !ownership ||
        ownership != profile->music_token)
        return DLUMA_MEDIA_RESULT_STALE;
    if (profile->owned_music_route ==
            DLUMA_DL_MUSIC_OWNERSHIP_FRONTEND_MAIN_MENU) {
        main_menu_module = profile->owned_stock_music_controller;
        if (!dluma_player_media_validate_wave_owner(profile, &manager) ||
            manager != profile->owned_music_manager ||
            manager != profile->published_frontend_music_manager ||
            main_menu_module !=
                profile->published_frontend_main_menu_module ||
            !dluma_player_media_validate_frontend_main_menu_module(
                profile, main_menu_module))
            return DLUMA_MEDIA_RESULT_UNAVAILABLE;
        __try {
            confirmed =
                *(unsigned char*)((BYTE*)manager +
                    DLUMA_DL_FRONTEND_MUSICS_MANAGER_MUTED_OFFSET) != 0U &&
                *(int*)((BYTE*)main_menu_module +
                    DLUMA_DL_MAIN_MENU_MODULE_MUSIC_HANDLE_OFFSET) == 0;
        }
        __except(1) {
            delivered = 0;
        }
        if (!delivered || !confirmed)
            return DLUMA_MEDIA_RESULT_UNAVAILABLE;
        state->phase = DLUMA_MEDIA_MUSIC_OWNED;
        return DLUMA_MEDIA_RESULT_SUCCESS;
    }
    if (profile->owned_music_route ==
            DLUMA_DL_MUSIC_OWNERSHIP_FRONTEND_MUSICS_MANAGER) {
        if (!dluma_player_media_validate_wave_owner(profile, &manager) ||
            manager != profile->owned_music_manager)
            return DLUMA_MEDIA_RESULT_UNAVAILABLE;
        __try {
            confirmed =
                *(unsigned char*)((BYTE*)manager +
                    DLUMA_DL_FRONTEND_MUSICS_MANAGER_MUTED_OFFSET) != 0U;
        }
        __except(1) {
            delivered = 0;
        }
        if (!delivered || !confirmed)
            return DLUMA_MEDIA_RESULT_UNAVAILABLE;
        state->phase = DLUMA_MEDIA_MUSIC_OWNED;
        return DLUMA_MEDIA_RESULT_SUCCESS;
    }
    if (profile->owned_music_route !=
            DLUMA_DL_MUSIC_OWNERSHIP_STOCK_MANAGER)
        return DLUMA_MEDIA_RESULT_UNAVAILABLE;
    if (!dluma_player_media_validate_music_manager(profile, &manager) ||
        manager != profile->owned_music_manager)
        return DLUMA_MEDIA_RESULT_UNAVAILABLE;
    __try {
        confirmed =
            ((*(unsigned char*)((BYTE*)manager +
                DLUMA_DL_MUSIC_MANAGER_FLAGS_OFFSET) &
              DLUMA_DL_MUSIC_MANAGER_PAUSED) != 0U) &&
            *(int*)((BYTE*)manager +
                DLUMA_DL_MUSIC_MANAGER_WANTED_TYPE_OFFSET) == -1;
    }
    __except(1) {
        delivered = 0;
    }
    if (!delivered || !confirmed)
        return DLUMA_MEDIA_RESULT_UNAVAILABLE;
    state->phase = DLUMA_MEDIA_MUSIC_OWNED;
    return DLUMA_MEDIA_RESULT_SUCCESS;
}
int dluma_player_media_adapter_music_maintain(
    DlumaMediaToken ownership)
{
    DlumaPlayerMediaPlayerProfile* profile =
        &g_dluma_player_media_player;
    void* manager = 0;
    void* main_menu_module = 0;
    int delivered = 1;
    int confirmed = 0;
    int reasserted = 0;
    if (!profile->music_owned || !ownership ||
        ownership != profile->music_token)
        return DLUMA_MEDIA_RESULT_STALE;
    if (profile->owned_music_route ==
            DLUMA_DL_MUSIC_OWNERSHIP_FRONTEND_MAIN_MENU) {
        main_menu_module = profile->owned_stock_music_controller;
        if (!profile->frontend_music_mute ||
            !profile->main_menu_music_stop ||
            !dluma_player_media_validate_wave_owner(profile, &manager) ||
            manager != profile->owned_music_manager ||
            manager != profile->published_frontend_music_manager ||
            main_menu_module !=
                profile->published_frontend_main_menu_module ||
            !dluma_player_media_validate_frontend_main_menu_module(
                profile, main_menu_module))
            return DLUMA_MEDIA_RESULT_UNAVAILABLE;
        __try {
            if (*(int*)((BYTE*)main_menu_module +
                    DLUMA_DL_MAIN_MENU_MODULE_MUSIC_HANDLE_OFFSET) != 0) {
                profile->main_menu_music_stop(main_menu_module);
                reasserted = 1;
            }
            if (*(unsigned char*)((BYTE*)manager +
                    DLUMA_DL_FRONTEND_MUSICS_MANAGER_MUTED_OFFSET) == 0U) {
                profile->frontend_music_mute(manager, 0.0f);
                reasserted = 1;
            }
            confirmed =
                *(int*)((BYTE*)main_menu_module +
                    DLUMA_DL_MAIN_MENU_MODULE_MUSIC_HANDLE_OFFSET) == 0 &&
                *(unsigned char*)((BYTE*)manager +
                    DLUMA_DL_FRONTEND_MUSICS_MANAGER_MUTED_OFFSET) != 0U;
        }
        __except(1) {
            delivered = 0;
        }
        if (!delivered || !confirmed)
            return DLUMA_MEDIA_RESULT_UNAVAILABLE;
        if (reasserted)
            dluma_log_pair(
                "INFO", "player_media_music_suspend_maintained",
                "restarted_main_menu_music_stopped_without_custom_wave_restart");
        return DLUMA_MEDIA_RESULT_SUCCESS;
    }
    if (profile->owned_music_route ==
            DLUMA_DL_MUSIC_OWNERSHIP_FRONTEND_MUSICS_MANAGER) {
        if (!profile->frontend_music_mute ||
            !dluma_player_media_validate_wave_owner(profile, &manager) ||
            manager != profile->owned_music_manager)
            return DLUMA_MEDIA_RESULT_UNAVAILABLE;
        __try {
            if (*(unsigned char*)((BYTE*)manager +
                    DLUMA_DL_FRONTEND_MUSICS_MANAGER_MUTED_OFFSET) == 0U) {
                profile->frontend_music_mute(manager, 0.0f);
                reasserted = 1;
            }
            confirmed =
                *(unsigned char*)((BYTE*)manager +
                    DLUMA_DL_FRONTEND_MUSICS_MANAGER_MUTED_OFFSET) != 0U;
        }
        __except(1) {
            delivered = 0;
        }
        if (!delivered || !confirmed)
            return DLUMA_MEDIA_RESULT_UNAVAILABLE;
        if (reasserted)
            dluma_log_pair(
                "INFO", "player_media_music_suspend_maintained",
                "frontend_music_manager_remuted");
        return DLUMA_MEDIA_RESULT_SUCCESS;
    }
    if (profile->owned_music_route !=
            DLUMA_DL_MUSIC_OWNERSHIP_STOCK_MANAGER ||
        !profile->music_stop ||
        !dluma_player_media_validate_music_manager(profile, &manager) ||
        manager != profile->owned_music_manager)
        return DLUMA_MEDIA_RESULT_UNAVAILABLE;
    __try {
        confirmed =
            ((*(unsigned char*)((BYTE*)manager +
                DLUMA_DL_MUSIC_MANAGER_FLAGS_OFFSET) &
              DLUMA_DL_MUSIC_MANAGER_PAUSED) != 0U) &&
            *(int*)((BYTE*)manager +
                DLUMA_DL_MUSIC_MANAGER_WANTED_TYPE_OFFSET) == -1;
        if (!confirmed) {
            profile->music_stop(manager);
            reasserted = 1;
            confirmed =
                ((*(unsigned char*)((BYTE*)manager +
                    DLUMA_DL_MUSIC_MANAGER_FLAGS_OFFSET) &
                  DLUMA_DL_MUSIC_MANAGER_PAUSED) != 0U) &&
                *(int*)((BYTE*)manager +
                    DLUMA_DL_MUSIC_MANAGER_WANTED_TYPE_OFFSET) == -1;
        }
    }
    __except(1) {
        delivered = 0;
    }
    if (!delivered || !confirmed)
        return DLUMA_MEDIA_RESULT_UNAVAILABLE;
    if (reasserted)
        dluma_log_pair(
            "INFO", "player_media_music_suspend_maintained",
            "stock_music_manager_repaused");
    return DLUMA_MEDIA_RESULT_SUCCESS;
}
int dluma_player_media_adapter_music_filter(
    DlumaMediaToken ownership, float wet_mix, float cutoff_hz)
{
    DlumaPlayerMediaPlayerProfile* profile =
        &g_dluma_player_media_player;
    (void)wet_mix;
    (void)cutoff_hz;
    if (!profile->music_owned || ownership != profile->music_token)
        return DLUMA_MEDIA_RESULT_STALE;
    if (!(dluma_player_media_adapter_capabilities() &
          DLUMA_PLAYER_MEDIA_CAP_MUSIC_FILTER) ||
        !profile->music_snapshot.valid)
        return DLUMA_MEDIA_RESULT_UNAVAILABLE;
    return DLUMA_MEDIA_RESULT_UNAVAILABLE;
}

int dluma_player_media_adapter_music_release_after(
    DlumaMediaToken ownership, unsigned int resume_delay_ms)
{
    DlumaPlayerMediaPlayerProfile* profile =
        &g_dluma_player_media_player;
    void* manager = 0;
    void* main_menu_module = 0;
    float expected = (float)resume_delay_ms * 0.001f;
    float scheduled = 0.0f;
    float difference = 0.0f;
    int delivered = 1;
    int confirmed = 0;
    if (resume_delay_ms > 60000U)
        return DLUMA_MEDIA_RESULT_INVALID;
    if (!profile->music_owned || !ownership ||
        ownership != profile->music_token)
        return DLUMA_MEDIA_RESULT_STALE;
    if (profile->owned_music_route ==
            DLUMA_DL_MUSIC_OWNERSHIP_FRONTEND_MAIN_MENU) {
        main_menu_module = profile->owned_stock_music_controller;
        if (!profile->frontend_music_unmute ||
            !profile->main_menu_music_start ||
            !dluma_player_media_validate_wave_owner(profile, &manager) ||
            manager != profile->owned_music_manager ||
            manager != profile->published_frontend_music_manager ||
            main_menu_module !=
                profile->published_frontend_main_menu_module ||
            !dluma_player_media_validate_frontend_main_menu_module(
                profile, main_menu_module))
            return DLUMA_MEDIA_RESULT_UNAVAILABLE;
        __try {
            profile->frontend_music_unmute(manager, expected);
            if (profile->stock_music_was_playing)
                profile->main_menu_music_start(main_menu_module);
            confirmed =
                *(unsigned char*)((BYTE*)manager +
                    DLUMA_DL_FRONTEND_MUSICS_MANAGER_MUTED_OFFSET) == 0U &&
                (profile->stock_music_was_playing
                    ? *(int*)((BYTE*)main_menu_module +
                          DLUMA_DL_MAIN_MENU_MODULE_MUSIC_HANDLE_OFFSET) != 0
                    : *(int*)((BYTE*)main_menu_module +
                          DLUMA_DL_MAIN_MENU_MODULE_MUSIC_HANDLE_OFFSET) == 0);
        }
        __except(1) {
            delivered = 0;
        }
        if (!delivered || !confirmed)
            return DLUMA_MEDIA_RESULT_BUSY;
        profile->music_token = 0;
        profile->owned_music_manager = 0;
        profile->owned_stock_music_controller = 0;
        profile->owned_music_route = DLUMA_DL_MUSIC_OWNERSHIP_NONE;
        profile->music_owned = 0U;
        profile->stock_music_was_playing = 0U;
        profile->stock_music_was_playing = 0U;
        memset(&profile->music_snapshot, 0,
               sizeof(profile->music_snapshot));
        dluma_log_pair(
            "INFO", "player_media_music_suspend_released",
            "main_menu_dw_menu_restored_background_manager_unmuted");
        return DLUMA_MEDIA_RESULT_SUCCESS;
    }
    if (profile->owned_music_route ==
            DLUMA_DL_MUSIC_OWNERSHIP_FRONTEND_MUSICS_MANAGER) {
        if (!profile->frontend_music_unmute ||
            !dluma_player_media_validate_wave_owner(profile, &manager) ||
            manager != profile->owned_music_manager)
            return DLUMA_MEDIA_RESULT_UNAVAILABLE;
        __try {
            profile->frontend_music_unmute(manager, expected);
            confirmed =
                *(unsigned char*)((BYTE*)manager +
                    DLUMA_DL_FRONTEND_MUSICS_MANAGER_MUTED_OFFSET) == 0U;
        }
        __except(1) {
            delivered = 0;
        }
        if (!delivered || !confirmed)
            return DLUMA_MEDIA_RESULT_BUSY;
        profile->music_token = 0;
        profile->owned_music_manager = 0;
        profile->owned_stock_music_controller = 0;
        profile->owned_music_route = DLUMA_DL_MUSIC_OWNERSHIP_NONE;
        profile->music_owned = 0U;
        memset(&profile->music_snapshot, 0,
               sizeof(profile->music_snapshot));
        dluma_log_pair(
            "INFO", "player_media_music_suspend_released",
            "frontend_musics_manager");
        return DLUMA_MEDIA_RESULT_SUCCESS;
    }
    if (profile->owned_music_route !=
            DLUMA_DL_MUSIC_OWNERSHIP_STOCK_MANAGER)
        return DLUMA_MEDIA_RESULT_UNAVAILABLE;
    if (!dluma_player_media_validate_music_manager(profile, &manager) ||
        manager != profile->owned_music_manager)
        return DLUMA_MEDIA_RESULT_UNAVAILABLE;
    if (profile->music_snapshot.valid &&
        !dluma_player_media_music_snapshot_restore(
            profile, &profile->music_snapshot))
        return DLUMA_MEDIA_RESULT_BUSY;
    __try {
        profile->music_start(manager, expected);
        if (!resume_delay_ms) {
            confirmed =
                (*(unsigned char*)((BYTE*)manager +
                    DLUMA_DL_MUSIC_MANAGER_FLAGS_OFFSET) &
                 DLUMA_DL_MUSIC_MANAGER_PAUSED) == 0U;
        } else {
            scheduled = *(float*)((BYTE*)manager +
                DLUMA_DL_MUSIC_MANAGER_DELAY_OFFSET);
            difference = scheduled - expected;
            if (difference < 0.0f) difference = -difference;
            confirmed =
                dluma_player_media_float_finite(scheduled) &&
                dluma_player_media_float_finite(difference) &&
                difference <= 0.0011f &&
                ((*(unsigned char*)((BYTE*)manager +
                    DLUMA_DL_MUSIC_MANAGER_FLAGS_OFFSET) &
                  DLUMA_DL_MUSIC_MANAGER_PAUSED) != 0U);
        }
    }
    __except(1) {
        delivered = 0;
    }
    if (!delivered || !confirmed)
        return DLUMA_MEDIA_RESULT_BUSY;
    profile->music_token = 0;
    profile->owned_music_manager = 0;
    profile->owned_stock_music_controller = 0;
    profile->owned_music_route = DLUMA_DL_MUSIC_OWNERSHIP_NONE;
    profile->music_owned = 0U;
    profile->stock_music_was_playing = 0U;
    memset(&profile->music_snapshot, 0,
           sizeof(profile->music_snapshot));
    dluma_log_pair(
        "INFO", "player_media_music_suspend_released", "1");
    return DLUMA_MEDIA_RESULT_SUCCESS;
}
int dluma_player_media_adapter_music_release(
    DlumaMediaToken ownership)
{
    return dluma_player_media_adapter_music_release_after(ownership, 0U);
}

int dluma_player_media_adapter_force_cleanup(void)
{
    DlumaPlayerMediaPlayerProfile* profile =
        &g_dluma_player_media_player;
    void* manager = 0;
    unsigned int i;
    int cleanup_ok = 1;
    int delivered;
    if (!dluma_player_media_validate_wave_owner(profile, &manager)) {
        for (i = 0; i < DLUMA_DL_PLAYER_MEDIA_MAX_WAVES; ++i)
            if (profile->waves[i].in_use) return 0;
        if (profile->music_owned || profile->music_snapshot.valid)
            return 0;
        return 1;
    }
    for (i = 0; i < DLUMA_DL_PLAYER_MEDIA_MAX_WAVES; ++i) {
        DlumaPlayerMediaWaveSlot* slot = &profile->waves[i];
        if (!slot->in_use) continue;
        if (slot->owner != manager) {
            cleanup_ok = 0;
            continue;
        }
        delivered = 1;
        __try {
            profile->stop_wave(manager, slot->engine_handle, 0.0f);
        }
        __except(1) {
            delivered = 0;
        }
        if (!delivered) {
            cleanup_ok = 0;
        } else {
            dluma_player_media_wave_clear(slot);
        }
    }
    if (profile->music_owned &&
        dluma_player_media_adapter_music_release_after(
            profile->music_token, 0U) !=
                DLUMA_MEDIA_RESULT_SUCCESS)
        cleanup_ok = 0;
    else if (!profile->music_owned &&
             profile->music_snapshot.valid) {
        if (!dluma_player_media_music_snapshot_restore(
                profile, &profile->music_snapshot)) {
            cleanup_ok = 0;
        } else {
            memset(&profile->music_snapshot, 0,
                   sizeof(profile->music_snapshot));
        }
    }
    return cleanup_ok;
}
