#include "dlw_custom_main_menu_runtime.h"

#include "dlw_custom_main_menu.h"
#include "dlw_custom_main_menu_music.h"
#include "dlw_asset_mod_runtime.h"
#include "dlw_custom_main_menu_runtime_core.h"
#include "dlw_frontend_level.h"
#include "dlw_path_security.h"
#include "dlw_pack.h"
#include "dlw_player_media_adapter.h"
#include "dlw_process_lifetime_sources.h"

#include <excpt.h>
#include <intrin.h>

#pragma intrinsic(_InterlockedCompareExchange128)

#define DLUMA_CMM_LOAD_RVA 0x007547D0UL
#define DLUMA_CMM_UNLOAD_RVA 0x00754A70UL
#define DLUMA_CMM_ACTIVATE_RVA 0x00757100UL
#define DLUMA_CMM_LOAD_SLOT_RVA 0x016792D0UL
#define DLUMA_CMM_UNLOAD_SLOT_RVA 0x016792D8UL
#define DLUMA_CMM_SCREEN_SHOWN_RVA 0x011E17F0UL
#define DLUMA_CMM_SCREEN_HIDDEN_RVA 0x011E1840UL
#define DLUMA_CMM_SCREEN_SHOWN_SLOT_RVA 0x01678DB0UL
#define DLUMA_CMM_SCREEN_HIDDEN_SLOT_RVA 0x01678DB8UL
#define DLUMA_CMM_MAIN_INITIALIZE_UI_RVA 0x011B8E60UL
#define DLUMA_CMM_MAIN_REGISTER_UI_ELEMENTS_RVA 0x0074EB50UL
#define DLUMA_CMM_MAIN_INITIALIZE_UI_SLOT_RVA 0x01678FE0UL
#define DLUMA_CMM_MAIN_REGISTER_UI_ELEMENTS_SLOT_RVA 0x01678FE8UL
#define DLUMA_CMM_DYNAMIC_CAST_RVA 0x0130A87EUL
#define DLUMA_CMM_GAME_SINGLETON_RVA 0x01D07F88UL
#define DLUMA_CMM_STOCK_PREMOUNT_RVA 0x0091C6C0UL
#define DLUMA_CMM_TOOLS_GLOBAL_RVA 0x01D03428UL
#define DLUMA_CMM_TOOLS_VTABLE_RVA 0x008D55C8UL
#define DLUMA_CMM_TOOLS_GET_LOCALE_SLOT 0x0158UL
#define DLUMA_CMM_TOOLS_GET_LOCALE_RVA 0x0028DF00UL
#define DLUMA_CMM_TOOLS_GET_SPEECH_SLOT 0x0160UL
#define DLUMA_CMM_TOOLS_GET_SPEECH_RVA 0x0028DFB0UL
#define DLUMA_CMM_TOOLS_PUBLISH_CONTENT_SLOT 0x1408UL
#define DLUMA_CMM_TOOLS_PUBLISH_CONTENT_RVA 0x00299B50UL
#define DLUMA_CMM_TOOLS_PREMOUNT_PREPARE_SLOT 0x1AC8UL
#define DLUMA_CMM_TOOLS_PREMOUNT_PREPARE_RVA 0x0029CA00UL
#define DLUMA_CMM_TOOLS_PREMOUNT_PACKAGES_SLOT 0x1AE8UL
#define DLUMA_CMM_TOOLS_PREMOUNT_PACKAGES_RVA 0x0029CB70UL

#define DLUMA_CMM_MUSIC_ASSIGN_BLOCK_RVA 0x011B7200UL
#define DLUMA_CMM_MUSIC_ASSIGN_CALL_RVA 0x011B720BUL
#define DLUMA_CMM_MUSIC_ASSIGN_TARGET_RVA 0x0005AF0UL
#define DLUMA_CMM_MUSIC_ASSIGN_CALL_OFFSET 11U
#define DLUMA_CMM_MUSIC_ASSIGN_BLOCK_SIZE 16U
#define DLUMA_CMM_MUSIC_MAIN_RVA 0x01DCF1B0UL
#define DLUMA_CMM_MUSIC_INGAME_RVA 0x01DCF1C0UL
#define DLUMA_CMM_PLAY_BACKGROUND_MUSIC_RVA 0x011B70F0UL
#define DLUMA_CMM_STOP_BACKGROUND_MUSIC_RVA 0x011B7380UL
#define DLUMA_CMM_MAIN_MENU_VTABLE_RVA 0x01678C38UL
#define DLUMA_CMM_MAIN_MENU_IGSOBJECT_VTABLE_RVA 0x01679718UL
#define DLUMA_CMM_MAIN_MENU_IGSOBJECT_OFFSET 0x0010UL
#define DLUMA_CMM_MAIN_MENU_MUSIC_HANDLE_OFFSET 0x0B18UL
#define DLUMA_CMM_MAIN_MENU_MUSIC_NAME_OFFSET 0x0B20UL
#define DLUMA_CMM_RELAY_PAGE_SIZE 0x1000U
#define DLUMA_CMM_RELAY_GRANULARITY 0x10000ULL
#define DLUMA_CMM_RELAY_MAX_DISTANCE 0x70000000ULL

#define DLUMA_CMM_LEVEL_TYPE_RVA 0x01CA2F10UL
#define DLUMA_CMM_MENU_BACKGROUND_TYPE_RVA 0x01CCD200UL
#define DLUMA_CMM_IGS_OBJECT_TYPE_RVA 0x01C952A8UL
#define DLUMA_CMM_CONTROL_OBJECT_TYPE_RVA 0x01C952F0UL
#define DLUMA_CMM_CAMERA_POSITION_HELPER_TYPE_RVA 0x01CCD128UL
#define DLUMA_CMM_MENU_CAMERA_HELPER_TYPE_RVA 0x01CCD1D0UL

#define DLUMA_CMM_GET_OBJECT_BY_ID_RVA 0x002D3C30UL
#define DLUMA_CMM_GET_PREV_RVA 0x002772D0UL
#define DLUMA_CMM_GET_NEXT_RVA 0x002772F0UL
#define DLUMA_CMM_GET_LEVEL_RVA 0x00277A00UL
#define DLUMA_CMM_GET_ID_RVA 0x00277880UL
#define DLUMA_CMM_GET_WORLD_XFORM_RVA 0x00135830UL
#define DLUMA_CMM_ACTIVATE_VIEW_RVA 0x002D9280UL
#define DLUMA_CMM_GET_VIEW_CAMERA_RVA 0x002D9C80UL
#define DLUMA_CMM_GET_WORLD_POSITION_RVA 0x00136AF0UL
#define DLUMA_CMM_GET_UP_VECTOR_RVA 0x00137150UL
#define DLUMA_CMM_GET_FORWARD_VECTOR_RVA 0x00137170UL
#define DLUMA_CMM_GET_FOV_RVA 0x00126560UL
#define DLUMA_CMM_FROM_FORWARD_UP_POS_RVA 0x00125B00UL
#define DLUMA_CMM_SET_FOV_RVA 0x001270B0UL
#define DLUMA_CMM_FORCE_FRAME_RVA 0x00127750UL
#define DLUMA_CMM_FX_ENABLE_RVA 0x0018CE20UL
#define DLUMA_CMM_GET_FX_MANAGER_RVA 0x00281C00UL
#define DLUMA_CMM_LOAD_UI_TEXTURES_RVA 0x002DA470UL
#define DLUMA_CMM_UNLOAD_UI_TEXTURES_RVA 0x002DA4A0UL
#define DLUMA_CMM_GET_SCREEN_ID_RVA 0x005EB1D0UL
#define DLUMA_CMM_GET_SCRIPT_NAME_RVA 0x005EB1F0UL

#define DLUMA_CMM_GAME_MENU_NAME_OFFSET 0x0790UL
#define DLUMA_CMM_MODULE_BACKGROUND_OFFSET 0x0DE8UL
#define DLUMA_CMM_BACKGROUND_VIEW_OFFSET 0x09C0UL
#define DLUMA_CMM_BACKGROUND_CAMERA_OFFSET 0x09C8UL
#define DLUMA_CMM_BACKGROUND_MUSIC_MANAGER_OFFSET 0x09E0UL
#define DLUMA_CMM_HELPER_FOV_OFFSET 0x00B0UL
#define DLUMA_CMM_OBJECT_NAME_RECORD_OFFSET 0x0020UL
#define DLUMA_CMM_NAME_POINTER_OFFSET 0x0060UL
#define DLUMA_CMM_FX_EMITTER_PRIMARY_VTABLE_RVA 0x01905738UL
#define DLUMA_CMM_FX_EMITTER_IGSOBJECT_VTABLE_RVA 0x01905A08UL
#define DLUMA_CMM_FX_EMITTER_LIFECYCLE_VTABLE_RVA 0x01906258UL
#define DLUMA_CMM_FX_EMITTER_IGSOBJECT_OFFSET 0x0010UL
#define DLUMA_CMM_FX_EMITTER_LIFECYCLE_OFFSET 0x00A8UL
#define DLUMA_CMM_FX_EMITTER_DEFINITION_A_OFFSET 0x00C8UL
#define DLUMA_CMM_FX_EMITTER_DEFINITION_B_OFFSET 0x00D0UL
#define DLUMA_CMM_FX_EMITTER_DEFINITION_C_OFFSET 0x00D8UL
#define DLUMA_CMM_FX_EMITTER_INSTANCE_OFFSET 0x00E0UL
#define DLUMA_CMM_FX_EMITTER_DELAY_OFFSET 0x00F8UL
#define DLUMA_CMM_FX_EMITTER_ON_ENABLED_RVA 0x010E09B0UL
#define DLUMA_CMM_FX_EMITTER_ON_DISABLED_RVA 0x010E0AA0UL
#define DLUMA_CMM_FX_OBJECT_LIMIT 4096U
#define DLUMA_CMM_FX_STARTED_EMITTER_CAPACITY 256U
#define DLUMA_CMM_ACTIVATE_STOLEN_SIZE 13U
#define DLUMA_CMM_ABSOLUTE_JUMP_SIZE 12U
#define DLUMA_CMM_DRAIN_TIMEOUT_MS 2000ULL
#define DLUMA_CMM_STABLE_MS 2000ULL
#define DLUMA_CMM_CAMERA_RETRY_MS 500ULL
#define DLUMA_CMM_MUSIC_MANAGER_RETRY_MS 500ULL
#define DLUMA_CMM_MAIN_MUSIC_RETRY_MS 500ULL
#define DLUMA_CMM_EXCEPTION_EXECUTE_HANDLER 1
#define DLUMA_CMM_VISIBLE_SCREEN_CAPACITY 32U
#define DLUMA_CMM_EMPTY_STACK_GRACE_MS 100ULL
#define DLUMA_CMM_CONTENT_FAILED (-1)
#define DLUMA_CMM_CONTENT_NAME_CAPACITY (DLUMA_MAX_WPATH * 2U)

typedef struct DlumaCmmTtlString
{
    char *data;
    unsigned int size;
    unsigned int capacity;
} DlumaCmmTtlString;

typedef void (__fastcall *DlumaCmmLoadFn)(void *module);
typedef __int64 (__fastcall *DlumaCmmUnloadFn)(void *module);
typedef void (__fastcall *DlumaCmmActivateFn)(void *module);
typedef __int64 (__fastcall *DlumaCmmInitializeUiFn)(void *module);
typedef void (__fastcall *DlumaCmmUiPhaseFn)(void *level);
typedef DlumaCmmTtlString *(__fastcall *DlumaCmmStringAssignFn)(
    DlumaCmmTtlString *destination,
    const DlumaCmmTtlString *source);
typedef void (__fastcall *DlumaCmmMainMenuMusicFn)(void *module);
typedef void *(__cdecl *DlumaCmmDynamicCastFn)(
    void *object, long vf_delta, void *source_type,
    void *target_type, int is_reference);
typedef void *(__fastcall *DlumaCmmGetObjectByIdFn)(void *level, int id);
typedef void *(__fastcall *DlumaCmmObjectLinkFn)(void *object);
typedef void *(__fastcall *DlumaCmmGetLevelFn)(void *object);
typedef int (__fastcall *DlumaCmmGetIdFn)(void *object);
typedef const float *(__fastcall *DlumaCmmGetWorldXformFn)(void *object);
typedef void (__fastcall *DlumaCmmActivateViewFn)(
    void *level, void *level_view);
typedef void *(__fastcall *DlumaCmmGetViewCameraFn)(
    void *level, void *level_view);
typedef float *(__fastcall *DlumaCmmGetControlVectorFn)(
    void *control_object, float *value_out);
typedef float (__fastcall *DlumaCmmGetFovFn)(void *camera);
typedef void (__fastcall *DlumaCmmFromForwardUpPosFn)(
    void *camera, const float *forward,
    const float *up, const float *position);
typedef void (__fastcall *DlumaCmmSetFovFn)(void *camera, float fov);
typedef void (__fastcall *DlumaCmmForceFrameFn)(void *camera);
typedef void (__fastcall *DlumaCmmFxEnableFn)(void *level, unsigned char enabled);
typedef void *(__fastcall *DlumaCmmGetFxManagerFn)(void *igs_object);
typedef void (__fastcall *DlumaCmmFxEmitterLifecycleFn)(
    void *lifecycle_subobject);
typedef void (__fastcall *DlumaCmmLoadUiTexturesFn)(
    void *level, const DlumaCmmTtlString *definition);
typedef void (__fastcall *DlumaCmmUnloadUiTexturesFn)(void *level);
typedef __int64 (__fastcall *DlumaCmmScreenEventFn)(
    void *screen_manager, void *screen);
typedef int (__fastcall *DlumaCmmGetScreenIdFn)(void *screen);
typedef DlumaCmmTtlString *(__fastcall *DlumaCmmGetScriptNameFn)(void *screen);
typedef void (__fastcall *DlumaCmmToolsPremountPrepareFn)(
    void *tools, DlumaCmmTtlString *project_root,
    ULONGLONG content_identity);
typedef void (__fastcall *DlumaCmmToolsPremountPackagesFn)(
    void *tools, DlumaCmmTtlString *project_root);
typedef DlumaCmmTtlString *(__fastcall *DlumaCmmToolsGetTextFn)(
    void *tools, DlumaCmmTtlString *output);
typedef void (__fastcall *DlumaCmmToolsPublishContentFn)(
    void *tools, DlumaCmmTtlString *locale, DlumaCmmTtlString *speech);
typedef void (__cdecl *DlumaCmmEngineFreeFn)(void *memory);

typedef struct DlumaCmmVisibleScreen
{
    void *screen;
    int screen_id;
    char script[DLUMA_CUSTOM_MENU_XUI_NAME_CAPACITY];
} DlumaCmmVisibleScreen;

typedef struct DlumaCmmStartedFxEmitter
{
    void *emitter;
    void *level;
    void *background;
    ULONGLONG generation;
} DlumaCmmStartedFxEmitter;

typedef enum DlumaCmmCustomLoadResult
{
    DLUMA_CMM_CUSTOM_LOAD_NOT_ENTERED = 0,
    DLUMA_CMM_CUSTOM_LOAD_COMPLETED = 1,
    DLUMA_CMM_CUSTOM_LOAD_ENTERED_FAILED = 2
} DlumaCmmCustomLoadResult;

typedef struct DlumaCustomMainMenuRuntimeState
{
    HMODULE engine;
    HMODULE gamedll;
    BYTE *engine_base;
    BYTE *gamedll_base;
    DWORD engine_image_size;
    DWORD gamedll_image_size;
    void **load_slot;
    void **unload_slot;
    void **screen_shown_slot;
    void **screen_hidden_slot;
    DlumaCmmLoadFn load_original;
    DlumaCmmUnloadFn unload_original;
    DlumaCmmScreenEventFn screen_shown_original;
    DlumaCmmScreenEventFn screen_hidden_original;
    DlumaCmmActivateFn activate_original;
    BYTE *activate_target;
    BYTE activate_original_bytes[DLUMA_CMM_ACTIVATE_STOLEN_SIZE];
    BYTE activate_patch[DLUMA_CMM_ACTIVATE_STOLEN_SIZE];
    void *activate_trampoline;
    void **main_initialize_ui_slot;
    void **main_register_ui_elements_slot;
    DlumaCmmInitializeUiFn main_initialize_ui_original;
    DlumaCmmUiPhaseFn main_register_ui_elements_original;
    BYTE *music_assign_block;
    BYTE music_assign_original[DLUMA_CMM_MUSIC_ASSIGN_BLOCK_SIZE];
    BYTE music_assign_patch[DLUMA_CMM_MUSIC_ASSIGN_BLOCK_SIZE];
    void *music_assign_relay;
    DlumaCmmStringAssignFn music_assign_original_fn;
    DlumaCmmMainMenuMusicFn play_background_music;
    DlumaCmmMainMenuMusicFn stop_background_music;
    DWORD music_assign_original_protect;
    DlumaCmmDynamicCastFn dynamic_cast;
    DlumaCmmGetObjectByIdFn get_object_by_id;
    DlumaCmmObjectLinkFn get_prev;
    DlumaCmmObjectLinkFn get_next;
    DlumaCmmGetLevelFn get_level;
    DlumaCmmGetIdFn get_id;
    DlumaCmmGetWorldXformFn get_world_xform;
    DlumaCmmActivateViewFn activate_view;
    DlumaCmmGetViewCameraFn get_view_camera;
    DlumaCmmGetControlVectorFn get_world_position;
    DlumaCmmGetControlVectorFn get_up_vector;
    DlumaCmmGetControlVectorFn get_forward_vector;
    DlumaCmmGetFovFn get_fov;
    DlumaCmmFromForwardUpPosFn from_forward_up_pos;
    DlumaCmmSetFovFn set_fov;
    DlumaCmmForceFrameFn force_frame;
    DlumaCmmFxEnableFn fx_enable;
    DlumaCmmGetFxManagerFn get_fx_manager;
    DlumaCmmFxEmitterLifecycleFn fx_emitter_on_enabled;
    DlumaCmmFxEmitterLifecycleFn fx_emitter_on_disabled;
    DlumaCmmLoadUiTexturesFn load_ui_textures;
    DlumaCmmUnloadUiTexturesFn unload_ui_textures;
    volatile LONG *fx_enable_flag;
    DlumaCmmGetScreenIdFn get_screen_id;
    DlumaCmmGetScriptNameFn get_script_name;
    void **tools_global;
    DlumaCmmToolsPremountPrepareFn premount_prepare;
    DlumaCmmToolsPremountPackagesFn premount_packages;
    DlumaCmmToolsGetTextFn get_locale_id;
    DlumaCmmToolsGetTextFn get_speech_id;
    DlumaCmmToolsPublishContentFn publish_content;
    DlumaCmmEngineFreeFn engine_free;
    char content_name_storage[DLUMA_CMM_CONTENT_NAME_CAPACITY];
    DlumaCmmTtlString content_name;
    ULONGLONG content_identity;
    char package_name_storage[DLUMA_CMM_CONTENT_NAME_CAPACITY];
    DlumaCmmTtlString package_name;
    const char *content_failure_phase;
    ULONG_PTR content_exception_address;
    DWORD content_exception_code;
    unsigned int content_prepare_attempt_count;
    void *publication_module;
    void *restoration_module;
    void *restoration_ui_level;
    void *fx_level;
    void *fx_background_owner;
    LONG fx_previous_flag;
    DlumaCmmStartedFxEmitter
        fx_started_emitters[DLUMA_CMM_FX_STARTED_EMITTER_CAPACITY];
    unsigned int fx_started_emitter_count;
    ULONGLONG fx_start_generation;
    void *fx_start_background;
    void *fx_start_level;
    void *loaded_module;
    void *texture_level;
    void *active_module;
    void *active_background;
    void *published_music_manager;
    void *published_main_menu_module;
    ULONGLONG stable_since_ms;
    ULONGLONG frontend_generation;
    ULONGLONG next_camera_retry_ms;
    ULONGLONG next_music_manager_retry_ms;
    ULONGLONG next_main_music_refresh_ms;
    volatile LONG lifecycle_lock;
    volatile LONG generation_lock;
    volatile LONG menu_name_lock;
    volatile LONG visible_screen_lock;
    volatile LONG ui_prepare_lock;
    volatile LONG active_calls;
    volatile LONG main_music_refresh_state;
    volatile LONG publication_state;
    volatile LONG publication_processing;
    volatile LONG restoration_processing;
    volatile LONG restoration_pending;
    volatile LONG frontend_return_ready;
    volatile LONG frontend_committed;
    volatile LONG return_audio_ready;
    volatile LONG restoration_ui_requested;
    volatile LONG restoration_load_requested;
    volatile LONG restoration_activate_requested;
    volatile LONG source_retirement_pending;
    ULONGLONG source_retirement_generation;
    LONG source_retirement_publication_state;
    volatile LONG screen_shown_events;
    volatile LONG screen_hidden_events;
    volatile LONG stopping;
    unsigned char started;
    unsigned char load_installed;
    unsigned char unload_installed;
    unsigned char screen_shown_installed;
    unsigned char screen_hidden_installed;
    unsigned char activate_installed;
    unsigned char main_initialize_ui_installed;
    unsigned char main_texture_hook_installed;
    unsigned char music_assign_installed;
    unsigned char music_assign_substitution_logged;
    unsigned char camera_applied;
    unsigned char texture_definitions_loaded;
    unsigned char custom_presentation_disabled;
    unsigned char music_manager_published;
    unsigned char main_menu_module_published;
    unsigned char receipt_good;
    unsigned char fx_available;
    unsigned char fx_lifecycle_available;
    unsigned char fx_state_captured;
    unsigned char fx_changed_by_runtime;
    unsigned char fx_start_attempted;
    unsigned char content_route_available;
    unsigned char content_publish_called;
    unsigned char content_terminal_failure;
    unsigned char content_failure_logged;
    unsigned char content_activated;
    unsigned char content_sources_mounted;
    unsigned char content_engine_sources_observed;
    unsigned char custom_load_complete;
    unsigned char custom_background_substituted;
    unsigned char generation_active;
    unsigned char gameplay_generation_retired;
    unsigned char restoration_background_required;
    unsigned char restoration_wait_logged;
    unsigned char teardown_failed;
    DlumaCmmVisibleScreen visible_screens[DLUMA_CMM_VISIBLE_SCREEN_CAPACITY];
    unsigned int visible_screen_count;
    ULONGLONG empty_stack_since_ms;
    char logged_top_script[DLUMA_CUSTOM_MENU_XUI_NAME_CAPACITY];
    unsigned int logged_visible_screen_count;
    LONG logged_screen_shown_events;
    LONG logged_screen_hidden_events;
    unsigned char screen_observation_logged;
    char texture_fingerprint[DLUMA_SHA256_HEX_CAPACITY];
} DlumaCustomMainMenuRuntimeState;

static DlumaCustomMainMenuRuntimeState g_dluma_cmm_runtime;

extern int dluma_custom_main_menu_bind_live_source_inventory(void);

static int dluma_cmm_finish_hook_mutation(
    int ownership_preserved,
    int protection_restored,
    const void *address,
    SIZE_T size)
{
    int cache_flushed = address && size &&
        FlushInstructionCache(GetCurrentProcess(), address, size) != 0;
    int complete =
        dluma_custom_main_menu_runtime_hook_mutation_complete(
            ownership_preserved, protection_restored, cache_flushed);
    if (!complete)
        g_dluma_cmm_runtime.teardown_failed = 1U;
    return complete;
}

static const BYTE g_dluma_cmm_load_prefix[] = {
    0x40, 0x55, 0x41, 0x54, 0x48, 0x8D, 0xAC, 0x24,
    0x78, 0xFD, 0xFF, 0xFF, 0x48, 0x81, 0xEC, 0x88,
    0x03, 0x00, 0x00
};
static const BYTE g_dluma_cmm_unload_prefix[] = {
    0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B,
    0xD9, 0x48, 0x8B, 0x89, 0xE8, 0x0D, 0x00, 0x00
};
static const BYTE g_dluma_cmm_screen_shown_prefix[] = {
    0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83,
    0xEC, 0x20, 0x48, 0x8B, 0xD9, 0x48, 0x8B, 0xCA
};
static const BYTE g_dluma_cmm_screen_hidden_prefix[] = {
    0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74,
    0x24, 0x10, 0x57, 0x48, 0x83, 0xEC, 0x30, 0x48
};
static const BYTE g_dluma_cmm_get_screen_id_prefix[] = {
    0x48, 0x83, 0x79, 0x38, 0x00, 0x74, 0x0E, 0x48,
    0x8B, 0x49, 0x38, 0x48, 0x8B, 0x01, 0x48, 0xFF,
    0xA0, 0xD0, 0x07, 0x00, 0x00
};
static const BYTE g_dluma_cmm_get_script_name_prefix[] = {
    0x48, 0x83, 0x79, 0x38, 0x00, 0x74, 0x0E, 0x48,
    0x8B, 0x49, 0x38, 0x48, 0x8B, 0x01, 0x48, 0xFF,
    0xA0, 0xE0, 0x07, 0x00, 0x00
};
static const BYTE g_dluma_cmm_activate_prefix[DLUMA_CMM_ACTIVATE_STOLEN_SIZE] = {
    0x40, 0x53, 0x48, 0x83, 0xEC, 0x30, 0x48, 0x8B,
    0x89, 0xE8, 0x0D, 0x00, 0x00
};
static const BYTE g_dluma_cmm_main_initialize_ui_prefix[] = {
    0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B,
    0x01, 0x48, 0x8B, 0xD9, 0xFF, 0x90, 0x38, 0x0A,
    0x00, 0x00, 0x48, 0x8B, 0xCB, 0x48, 0x83, 0xC4,
    0x20, 0x5B, 0xE9, 0x71, 0x50, 0xF4, 0xFF, 0xCC
};
static const BYTE g_dluma_cmm_main_register_ui_elements_prefix[] = {
    0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74,
    0x24, 0x10, 0x48, 0x89, 0x7C, 0x24, 0x18, 0x55,
    0x41, 0x54, 0x41, 0x55, 0x48, 0x8B, 0xEC, 0x48,
    0x83, 0xEC, 0x50, 0x48, 0x8B, 0xF1, 0xE8, 0x3D,
    0xA4, 0xA6, 0x00
};
static const BYTE g_dluma_cmm_music_assign_block[
    DLUMA_CMM_MUSIC_ASSIGN_BLOCK_SIZE] = {
    0x20, 0x0B, 0x00, 0x00, 0x48, 0x0F, 0x45, 0xD1,
    0x48, 0x8B, 0xCB, 0xE8, 0xE0, 0xE8, 0xE4, 0xFE
};
static const BYTE g_dluma_cmm_play_background_music_prefix[] = {
    0x48, 0x89, 0x6C, 0x24, 0x18, 0x48, 0x89, 0x74,
    0x24, 0x20, 0x57, 0x48, 0x81, 0xEC, 0x90, 0x00
};
static const BYTE g_dluma_cmm_stop_background_music_prefix[] = {
    0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83,
    0xEC, 0x20, 0x48, 0x8B, 0xD9, 0x48, 0x8D, 0x15
};
static const BYTE g_dluma_cmm_dynamic_cast_prefix[] = {
    0xFF, 0x25, 0x64, 0x19, 0x0C, 0x00
};
static const BYTE g_dluma_cmm_get_object_prefix[] = {
    0x48, 0x83, 0xEC, 0x28, 0x48, 0x83, 0x79, 0x08,
    0x00, 0x74, 0x1B, 0x48, 0x8B, 0x49, 0x08, 0x48
};
static const BYTE g_dluma_cmm_get_prev_prefix[] = {
    0x48, 0x8B, 0x41, 0x20, 0x48, 0x8B, 0x40, 0x28,
    0x48, 0x85, 0xC0, 0x74, 0x05, 0x48, 0x8B, 0x40
};
static const BYTE g_dluma_cmm_get_next_prefix[] = {
    0x48, 0x8B, 0x41, 0x20, 0x48, 0x8B, 0x40, 0x30,
    0x48, 0x85, 0xC0, 0x74, 0x05, 0x48, 0x8B, 0x40
};
static const BYTE g_dluma_cmm_get_level_prefix[] = {
    0x48, 0x8B, 0x41, 0x20, 0x48, 0x8B, 0x40, 0x48,
    0x48, 0x85, 0xC0, 0x74, 0x08, 0x48, 0x8B, 0x80
};
static const BYTE g_dluma_cmm_get_id_prefix[] = {
    0x48, 0x8B, 0x41, 0x20, 0x8B, 0x40, 0x50, 0xC3
};
static const BYTE g_dluma_cmm_get_world_xform_prefix[] = {
    0x48, 0x8B, 0x41, 0x08, 0x48, 0x8B, 0x88, 0xF0,
    0x00, 0x00, 0x00, 0xE9, 0xA0, 0xF9, 0x3B, 0x00
};
static const BYTE g_dluma_cmm_activate_view_prefix[] = {
    0x48, 0x8B, 0x49, 0x08, 0x48, 0x81, 0xC1, 0x08,
    0x08, 0x00, 0x00, 0xE9, 0xF0, 0x4A, 0xD5, 0xFF
};
static const BYTE g_dluma_cmm_get_view_camera_prefix[] = {
    0x48, 0x85, 0xD2, 0x74, 0x14, 0x48, 0x8B, 0x82,
    0x90, 0x03, 0x09, 0x00, 0x48, 0x85, 0xC0, 0x74,
    0x08, 0x48, 0x8B, 0x80, 0xB8, 0x02, 0x00, 0x00
};
static const BYTE g_dluma_cmm_get_world_position_prefix[] = {
    0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B,
    0x41, 0x08, 0x48, 0x8B, 0xDA, 0x48, 0x8B, 0x88,
    0xF0, 0x00, 0x00, 0x00
};
static const BYTE g_dluma_cmm_get_up_vector_prefix[] = {
    0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B,
    0x49, 0x08, 0x48, 0x8B, 0xDA, 0xE8, 0x1E, 0xE5,
    0xFF, 0xFF
};
static const BYTE g_dluma_cmm_get_forward_vector_prefix[] = {
    0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B,
    0x49, 0x08, 0x48, 0x8B, 0xDA, 0xE8, 0x9E, 0xE5,
    0xFF, 0xFF
};
static const BYTE g_dluma_cmm_get_fov_prefix[] = {
    0x48, 0x8B, 0x41, 0x08, 0xF3, 0x0F, 0x10, 0x80,
    0xDC, 0x02, 0x00, 0x00, 0xF3, 0x0F, 0x59, 0x05
};
static const BYTE g_dluma_cmm_from_forward_up_pos_prefix[] = {
    0x48, 0x8B, 0xC4, 0x48, 0x89, 0x58, 0x08, 0x48,
    0x89, 0x70, 0x10, 0x48, 0x89, 0x78, 0x18, 0x55,
    0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57
};
static const BYTE g_dluma_cmm_set_fov_prefix[] = {
    0x48, 0x8B, 0xC4, 0x57, 0x48, 0x81, 0xEC, 0xA0,
    0x00, 0x00, 0x00, 0x0F, 0x29, 0x70, 0xE8, 0x0F
};
static const BYTE g_dluma_cmm_force_frame_prefix[] = {
    0x48, 0x83, 0xEC, 0x28, 0x48, 0x83, 0x79, 0x08,
    0x00, 0x74, 0x0A, 0x48, 0x8B, 0x49, 0x08, 0x48,
    0x8B, 0x01, 0xFF, 0x50, 0x28
};
static const BYTE g_dluma_cmm_load_ui_textures_prefix[] = {
    0x48, 0x83, 0xEC, 0x28, 0x83, 0x7A, 0x08, 0x00,
    0x76, 0x17, 0x48, 0x8B, 0x41, 0x08, 0x48, 0x8B,
    0x88, 0x70, 0x05, 0x00, 0x00
};
static const BYTE g_dluma_cmm_unload_ui_textures_prefix[] = {
    0x48, 0x8B, 0x41, 0x08, 0x48, 0x8B, 0x88, 0x70,
    0x05, 0x00, 0x00, 0x48, 0x81, 0xC1, 0xB0, 0x03,
    0x00, 0x00
};
static const BYTE g_dluma_cmm_fx_enable_prefix[] = {
    0x0F, 0xB6, 0xC2, 0x89, 0x05,
    0xFB, 0x47, 0x90, 0x00, 0xC3
};
static const BYTE g_dluma_cmm_get_fx_manager_prefix[] = {
    0x48, 0x8B, 0x41, 0x20, 0x48, 0x8B, 0x40, 0x48,
    0x48, 0x85, 0xC0, 0x75, 0x02, 0xF3, 0xC3, 0x48,
    0x05, 0xB8, 0x00, 0x00, 0x00, 0xC3
};
static const BYTE g_dluma_cmm_fx_emitter_on_enabled_prefix[] = {
    0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83,
    0xEC, 0x20, 0x48, 0x8B, 0xD9, 0x48, 0x8D, 0x15,
    0x9C, 0x64, 0xCD, 0x00, 0x48, 0x81, 0xC1, 0x68,
    0xFF, 0xFF, 0xFF
};
static const BYTE g_dluma_cmm_fx_emitter_on_disabled_prefix[] = {
    0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74,
    0x24, 0x10, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x48,
    0x8B, 0xF9, 0x48, 0x8D, 0x15, 0x7F, 0x63, 0xCD,
    0x00, 0x48, 0x81, 0xC1, 0x68, 0xFF, 0xFF, 0xFF
};
static const BYTE g_dluma_cmm_stock_premount_prefix[83] = {
    0x48, 0x89, 0x5C, 0x24, 0x18, 0x55, 0x41, 0x54,
    0x41, 0x55, 0x48, 0x8D, 0x6C, 0x24, 0xB9, 0x48,
    0x81, 0xEC, 0x90, 0x00, 0x00, 0x00, 0x4C, 0x8B,
    0xE1, 0x48, 0x8B, 0x0D, 0x48, 0x6D, 0x3E, 0x01,
    0x48, 0x89, 0xB4, 0x24, 0xB0, 0x00, 0x00, 0x00,
    0x48, 0x8B, 0x01, 0x48, 0x8B, 0xDA, 0xFF, 0x90,
    0xC8, 0x1A, 0x00, 0x00, 0x48, 0x8B, 0x0D, 0x2D,
    0x6D, 0x3E, 0x01, 0x48, 0x8B, 0xD3, 0x48, 0x8B,
    0x01, 0xFF, 0x90, 0xE8, 0x1A, 0x00, 0x00, 0x48,
    0x8B, 0x0D, 0x7A, 0xB8, 0x3E, 0x01, 0xE8, 0xED,
    0x10, 0xAC, 0xFF
};
static const BYTE g_dluma_cmm_tools_premount_prepare_prefix[] = {
    0x48, 0x83, 0xEC, 0x28, 0x48, 0x8B, 0x0D, 0xA5,
    0x06, 0x83, 0x00, 0x48, 0x85, 0xC9, 0x74, 0x09,
    0x48, 0x8B, 0x01, 0xFF, 0x90, 0x10, 0x01, 0x00,
    0x00, 0x48, 0x83, 0xC4, 0x28, 0xC3
};
static const BYTE g_dluma_cmm_tools_premount_packages_prefix[] = {
    0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B,
    0xDA, 0xE8, 0xE2, 0xF2, 0x3E, 0x00, 0x48, 0x8B,
    0xD3, 0x48, 0x8B, 0xC8, 0xE8, 0xC7, 0xF3, 0x3E,
    0x00, 0xE8, 0xD2, 0xF2, 0x3E, 0x00, 0x48, 0x8B,
    0xC8, 0xE8, 0xBA, 0xF4, 0x3E, 0x00, 0xE8, 0xC5,
    0xF2, 0x3E, 0x00, 0x48, 0x8B, 0xC8, 0x48, 0x83,
    0xC4, 0x20, 0x5B, 0xE9, 0x78, 0xFC, 0x3E, 0x00
};
static const BYTE g_dluma_cmm_tools_get_locale_prefix[] = {
    0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x6C,
    0x24, 0x18, 0x56, 0x48, 0x83, 0xEC, 0x20, 0x48,
    0x8B, 0x0D, 0x02, 0xC9, 0x83, 0x00, 0x48, 0x8B,
    0xF2, 0x48, 0x8D, 0xA9, 0x58, 0x04, 0x00, 0x00
};
static const BYTE g_dluma_cmm_tools_get_speech_prefix[] = {
    0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x6C,
    0x24, 0x18, 0x56, 0x48, 0x83, 0xEC, 0x20, 0x48,
    0x8B, 0x0D, 0x52, 0xC8, 0x83, 0x00, 0x48, 0x8B,
    0xEA, 0x48, 0x8D, 0xB1, 0x68, 0x04, 0x00, 0x00
};
static const BYTE g_dluma_cmm_tools_publish_content_prefix[] = {
    0x48, 0x83, 0xEC, 0x28, 0x48, 0x8B, 0x0D, 0x55,
    0x35, 0x83, 0x00, 0x48, 0x85, 0xC9, 0x74, 0x09,
    0x48, 0x8B, 0x01, 0xFF, 0x90, 0xD8, 0x00, 0x00,
    0x00, 0x48, 0x83, 0xC4, 0x28, 0xC3
};

static void dluma_cmm_lock(volatile LONG *lock)
{
    while (dluma_atomic_compare_exchange(lock, 1, 0) != 0)
        Sleep(0);
}

static void dluma_cmm_unlock(volatile LONG *lock)
{
    dluma_atomic_exchange(lock, 0);
}

static int dluma_cmm_try_lock(volatile LONG *lock)
{
    return dluma_atomic_compare_exchange(lock, 1, 0) == 0;
}

static int dluma_cmm_get_pe_identity(
    HMODULE module, DWORD *timestamp, DWORD *image_size)
{
    BYTE *base = (BYTE *)module;
    DWORD pe_offset;
    BYTE *nt;
    if (!base || !timestamp || !image_size ||
        !dluma_is_readable(base, 0x100U) ||
        base[0] != 'M' || base[1] != 'Z')
        return 0;
    pe_offset = *(DWORD *)(base + 0x3CU);
    if (pe_offset < 0x40U || pe_offset > 0x100000U)
        return 0;
    nt = base + pe_offset;
    if (!dluma_is_readable(nt, 0x100U) ||
        *(DWORD *)nt != 0x00004550UL ||
        *(WORD *)(nt + 0x18U) != 0x020BU)
        return 0;
    *timestamp = *(DWORD *)(nt + 0x08U);
    *image_size = *(DWORD *)(nt + 0x50U);
    return 1;
}

static int dluma_cmm_executable(const void *address, SIZE_T size)
{
    MEMORY_BASIC_INFORMATION information;
    BYTE *end;
    DWORD protection;
    if (!address || !size ||
        !VirtualQuery(address, &information, sizeof(information)) ||
        information.State != MEM_COMMIT ||
        (information.Protect & (PAGE_GUARD | PAGE_NOACCESS)))
        return 0;
    end = (BYTE *)address + size;
    if (end < (BYTE *)address ||
        end > (BYTE *)information.BaseAddress + information.RegionSize)
        return 0;
    protection = information.Protect & 0xFFU;
    return protection == PAGE_EXECUTE ||
           protection == PAGE_EXECUTE_READ ||
           protection == PAGE_EXECUTE_READWRITE ||
           protection == PAGE_EXECUTE_WRITECOPY;
}

static int dluma_cmm_writable(const void *address, SIZE_T size)
{
    MEMORY_BASIC_INFORMATION information;
    BYTE *end;
    DWORD protection;
    if (!address || !size ||
        !VirtualQuery(address, &information, sizeof(information)) ||
        information.State != MEM_COMMIT ||
        (information.Protect & (PAGE_GUARD | PAGE_NOACCESS)))
        return 0;
    end = (BYTE *)address + size;
    if (end < (BYTE *)address ||
        end > (BYTE *)information.BaseAddress + information.RegionSize)
        return 0;
    protection = information.Protect & 0xFFU;
    return protection == PAGE_READWRITE ||
           protection == PAGE_WRITECOPY ||
           protection == PAGE_EXECUTE_READWRITE ||
           protection == PAGE_EXECUTE_WRITECOPY;
}

static int dluma_cmm_bytes(
    const void *address, const BYTE *expected, unsigned int size)
{
    return address && expected && size &&
           dluma_cmm_executable(address, size) &&
           dluma_is_readable(address, size) &&
           memcmp(address, expected, size) == 0;
}

static int dluma_cmm_type_descriptor(
    BYTE *base, DWORD image_size, DWORD rva, const char *name)
{
    SIZE_T length = name ? strlen(name) : 0U;
    const char *stored;
    if (!base || !length || rva > image_size ||
        length + 17U > image_size - rva)
        return 0;
    stored = (const char *)(base + rva + 16U);
    return dluma_is_readable(stored, length + 1U) &&
           memcmp(stored, name, length + 1U) == 0;
}

static int dluma_cmm_export(
    HMODULE module, DWORD image_size, const char *name,
    DWORD expected_rva, const BYTE *prefix, unsigned int prefix_size,
    void **output)
{
    FARPROC procedure;
    if (output)
        *output = 0;
    if (!module || !name || !output ||
        expected_rva >= image_size || prefix_size > image_size - expected_rva)
        return 0;
    procedure = GetProcAddress(module, name);
    if (!procedure ||
        (BYTE *)procedure - (BYTE *)module != expected_rva ||
        !dluma_cmm_bytes((const void *)procedure, prefix, prefix_size))
        return 0;
    *output = (void *)procedure;
    return 1;
}

static int dluma_cmm_bind_fx_enable(DWORD engine_image_size)
{
    void *procedure = 0;
    unsigned long backing_rva = 0;
    volatile LONG *flag;
    LONG value;
    g_dluma_cmm_runtime.fx_available = 0;
    g_dluma_cmm_runtime.fx_enable = 0;
    g_dluma_cmm_runtime.fx_enable_flag = 0;
    if (!dluma_cmm_export(
            g_dluma_cmm_runtime.engine, engine_image_size,
            "?FXEnable@ILevel@@QEAAX_N@Z",
            DLUMA_CMM_FX_ENABLE_RVA, g_dluma_cmm_fx_enable_prefix,
            DLUMA_ARRAY_COUNT(g_dluma_cmm_fx_enable_prefix), &procedure) ||
        !dluma_custom_main_menu_runtime_fx_enable_contract_valid(
            g_dluma_cmm_fx_enable_prefix,
            DLUMA_ARRAY_COUNT(g_dluma_cmm_fx_enable_prefix),
            DLUMA_CMM_FX_ENABLE_RVA, engine_image_size,
            &backing_rva))
        return 0;
    flag = (volatile LONG *)(g_dluma_cmm_runtime.engine_base + backing_rva);
    if (!dluma_cmm_writable((const void *)flag, sizeof(*flag)))
        return 0;
    value = dluma_atomic_compare_exchange(flag, 0, 0);
    if (value != 0 && value != 1)
        return 0;
    g_dluma_cmm_runtime.fx_enable = (DlumaCmmFxEnableFn)procedure;
    g_dluma_cmm_runtime.fx_enable_flag = flag;
    g_dluma_cmm_runtime.fx_available = 1U;
    dluma_log_hex("INFO", "custom_main_menu_fx_export_rva",
                DLUMA_CMM_FX_ENABLE_RVA);
    dluma_log_hex("INFO", "custom_main_menu_fx_flag_rva", backing_rva);
    return 1;
}

static int dluma_cmm_bind_fx_lifecycle(
    DWORD engine_image_size, DWORD gamedll_image_size)
{
    void *procedure = 0;
    BYTE *primary_vtable;
    BYTE *igs_vtable;
    BYTE *lifecycle_vtable;
    void *enabled_target;
    void *disabled_target;
    g_dluma_cmm_runtime.fx_lifecycle_available = 0U;
    g_dluma_cmm_runtime.get_fx_manager = 0;
    g_dluma_cmm_runtime.fx_emitter_on_enabled = 0;
    g_dluma_cmm_runtime.fx_emitter_on_disabled = 0;
    if (!dluma_cmm_export(
            g_dluma_cmm_runtime.engine, engine_image_size,
            "?GetFXManager@IGSObject@@QEAAPEAVIFXManager@@XZ",
            DLUMA_CMM_GET_FX_MANAGER_RVA,
            g_dluma_cmm_get_fx_manager_prefix,
            DLUMA_ARRAY_COUNT(g_dluma_cmm_get_fx_manager_prefix),
            &procedure) ||
        DLUMA_CMM_FX_EMITTER_ON_ENABLED_RVA >= gamedll_image_size ||
        DLUMA_ARRAY_COUNT(g_dluma_cmm_fx_emitter_on_enabled_prefix) >
            gamedll_image_size - DLUMA_CMM_FX_EMITTER_ON_ENABLED_RVA ||
        DLUMA_CMM_FX_EMITTER_ON_DISABLED_RVA >= gamedll_image_size ||
        DLUMA_ARRAY_COUNT(g_dluma_cmm_fx_emitter_on_disabled_prefix) >
            gamedll_image_size - DLUMA_CMM_FX_EMITTER_ON_DISABLED_RVA ||
        DLUMA_CMM_FX_EMITTER_PRIMARY_VTABLE_RVA >= gamedll_image_size ||
        sizeof(void *) >
            gamedll_image_size - DLUMA_CMM_FX_EMITTER_PRIMARY_VTABLE_RVA ||
        DLUMA_CMM_FX_EMITTER_IGSOBJECT_VTABLE_RVA >= gamedll_image_size ||
        sizeof(void *) >
            gamedll_image_size - DLUMA_CMM_FX_EMITTER_IGSOBJECT_VTABLE_RVA ||
        DLUMA_CMM_FX_EMITTER_LIFECYCLE_VTABLE_RVA >= gamedll_image_size ||
        0x30U >
            gamedll_image_size - DLUMA_CMM_FX_EMITTER_LIFECYCLE_VTABLE_RVA)
        return 0;
    enabled_target = g_dluma_cmm_runtime.gamedll_base +
        DLUMA_CMM_FX_EMITTER_ON_ENABLED_RVA;
    disabled_target = g_dluma_cmm_runtime.gamedll_base +
        DLUMA_CMM_FX_EMITTER_ON_DISABLED_RVA;
    if (!dluma_cmm_bytes(
            enabled_target, g_dluma_cmm_fx_emitter_on_enabled_prefix,
            DLUMA_ARRAY_COUNT(g_dluma_cmm_fx_emitter_on_enabled_prefix)) ||
        !dluma_cmm_bytes(
            disabled_target, g_dluma_cmm_fx_emitter_on_disabled_prefix,
            DLUMA_ARRAY_COUNT(g_dluma_cmm_fx_emitter_on_disabled_prefix)))
        return 0;
    primary_vtable = g_dluma_cmm_runtime.gamedll_base +
        DLUMA_CMM_FX_EMITTER_PRIMARY_VTABLE_RVA;
    igs_vtable = g_dluma_cmm_runtime.gamedll_base +
        DLUMA_CMM_FX_EMITTER_IGSOBJECT_VTABLE_RVA;
    lifecycle_vtable = g_dluma_cmm_runtime.gamedll_base +
        DLUMA_CMM_FX_EMITTER_LIFECYCLE_VTABLE_RVA;
    if (!dluma_is_readable(primary_vtable, sizeof(void *)) ||
        !dluma_is_readable(igs_vtable, sizeof(void *)) ||
        !dluma_is_readable(lifecycle_vtable, 0x30U) ||
        *(void **)(lifecycle_vtable + 0x20U) != enabled_target ||
        *(void **)(lifecycle_vtable + 0x28U) != disabled_target)
        return 0;
    g_dluma_cmm_runtime.get_fx_manager = (DlumaCmmGetFxManagerFn)procedure;
    g_dluma_cmm_runtime.fx_emitter_on_enabled =
        (DlumaCmmFxEmitterLifecycleFn)enabled_target;
    g_dluma_cmm_runtime.fx_emitter_on_disabled =
        (DlumaCmmFxEmitterLifecycleFn)disabled_target;
    g_dluma_cmm_runtime.fx_lifecycle_available = 1U;
    dluma_log_hex(
        "INFO", "custom_main_menu_fx_manager_export_rva",
        DLUMA_CMM_GET_FX_MANAGER_RVA);
    dluma_log_hex(
        "INFO", "custom_main_menu_fx_emitter_start_rva",
        DLUMA_CMM_FX_EMITTER_ON_ENABLED_RVA);
    dluma_log_hex(
        "INFO", "custom_main_menu_fx_emitter_stop_rva",
        DLUMA_CMM_FX_EMITTER_ON_DISABLED_RVA);
    return 1;
}

static int dluma_cmm_bind_content_publication(
    DWORD engine_image_size, DWORD gamedll_image_size)
{
    unsigned long tools_global_rva = 0;
    void **prepare_slot;
    void **packages_slot;
    void **locale_slot;
    void **speech_slot;
    void **publish_slot;
    HMODULE crt;
    g_dluma_cmm_runtime.content_route_available = 0;
    g_dluma_cmm_runtime.tools_global = 0;
    g_dluma_cmm_runtime.premount_prepare = 0;
    g_dluma_cmm_runtime.premount_packages = 0;
    g_dluma_cmm_runtime.get_locale_id = 0;
    g_dluma_cmm_runtime.get_speech_id = 0;
    g_dluma_cmm_runtime.publish_content = 0;
    g_dluma_cmm_runtime.engine_free = 0;
    if (DLUMA_CMM_STOCK_PREMOUNT_RVA >= gamedll_image_size ||
        DLUMA_ARRAY_COUNT(g_dluma_cmm_stock_premount_prefix) >
            gamedll_image_size - DLUMA_CMM_STOCK_PREMOUNT_RVA) {
        dluma_log_pair("WARN", "custom_main_menu_content_route_failure",
                     "stock_premount_range");
        return 0;
    }
    if (DLUMA_CMM_TOOLS_PREMOUNT_PREPARE_RVA >= engine_image_size ||
        DLUMA_ARRAY_COUNT(g_dluma_cmm_tools_premount_prepare_prefix) >
            engine_image_size - DLUMA_CMM_TOOLS_PREMOUNT_PREPARE_RVA) {
        dluma_log_pair("WARN", "custom_main_menu_content_route_failure",
                     "prepare_range");
        return 0;
    }
    if (DLUMA_CMM_TOOLS_PREMOUNT_PACKAGES_RVA >= engine_image_size ||
        DLUMA_ARRAY_COUNT(g_dluma_cmm_tools_premount_packages_prefix) >
            engine_image_size - DLUMA_CMM_TOOLS_PREMOUNT_PACKAGES_RVA) {
        dluma_log_pair("WARN", "custom_main_menu_content_route_failure",
                     "packages_range");
        return 0;
    }
    if (!dluma_cmm_bytes(
            g_dluma_cmm_runtime.gamedll_base + DLUMA_CMM_STOCK_PREMOUNT_RVA,
            g_dluma_cmm_stock_premount_prefix,
            DLUMA_ARRAY_COUNT(g_dluma_cmm_stock_premount_prefix))) {
        dluma_log_pair("WARN", "custom_main_menu_content_route_failure",
                     "stock_premount_bytes");
        return 0;
    }
    if (!dluma_cmm_bytes(
            g_dluma_cmm_runtime.engine_base +
                DLUMA_CMM_TOOLS_PREMOUNT_PREPARE_RVA,
            g_dluma_cmm_tools_premount_prepare_prefix,
            DLUMA_ARRAY_COUNT(g_dluma_cmm_tools_premount_prepare_prefix))) {
        dluma_log_pair("WARN", "custom_main_menu_content_route_failure",
                     "prepare_bytes");
        return 0;
    }
    if (DLUMA_CMM_TOOLS_GET_LOCALE_RVA >= engine_image_size ||
        DLUMA_ARRAY_COUNT(g_dluma_cmm_tools_get_locale_prefix) >
            engine_image_size - DLUMA_CMM_TOOLS_GET_LOCALE_RVA ||
        !dluma_cmm_bytes(
            g_dluma_cmm_runtime.engine_base + DLUMA_CMM_TOOLS_GET_LOCALE_RVA,
            g_dluma_cmm_tools_get_locale_prefix,
            DLUMA_ARRAY_COUNT(g_dluma_cmm_tools_get_locale_prefix))) {
        dluma_log_pair("WARN", "custom_main_menu_content_route_failure",
                     "locale_binding");
        return 0;
    }
    if (DLUMA_CMM_TOOLS_GET_SPEECH_RVA >= engine_image_size ||
        DLUMA_ARRAY_COUNT(g_dluma_cmm_tools_get_speech_prefix) >
            engine_image_size - DLUMA_CMM_TOOLS_GET_SPEECH_RVA ||
        !dluma_cmm_bytes(
            g_dluma_cmm_runtime.engine_base + DLUMA_CMM_TOOLS_GET_SPEECH_RVA,
            g_dluma_cmm_tools_get_speech_prefix,
            DLUMA_ARRAY_COUNT(g_dluma_cmm_tools_get_speech_prefix))) {
        dluma_log_pair("WARN", "custom_main_menu_content_route_failure",
                     "speech_binding");
        return 0;
    }
    if (DLUMA_CMM_TOOLS_PUBLISH_CONTENT_RVA >= engine_image_size ||
        DLUMA_ARRAY_COUNT(g_dluma_cmm_tools_publish_content_prefix) >
            engine_image_size - DLUMA_CMM_TOOLS_PUBLISH_CONTENT_RVA ||
        !dluma_cmm_bytes(
            g_dluma_cmm_runtime.engine_base +
                DLUMA_CMM_TOOLS_PUBLISH_CONTENT_RVA,
            g_dluma_cmm_tools_publish_content_prefix,
            DLUMA_ARRAY_COUNT(g_dluma_cmm_tools_publish_content_prefix))) {
        dluma_log_pair("WARN", "custom_main_menu_content_route_failure",
                     "content_publication_binding");
        return 0;
    }
    if (!dluma_cmm_bytes(
            g_dluma_cmm_runtime.engine_base +
                DLUMA_CMM_TOOLS_PREMOUNT_PACKAGES_RVA,
            g_dluma_cmm_tools_premount_packages_prefix,
            DLUMA_ARRAY_COUNT(g_dluma_cmm_tools_premount_packages_prefix))) {
        dluma_log_pair("WARN", "custom_main_menu_content_route_failure",
                     "packages_bytes");
        return 0;
    }
    if (!dluma_custom_main_menu_runtime_stock_premount_contract_valid(
            g_dluma_cmm_stock_premount_prefix,
            DLUMA_ARRAY_COUNT(g_dluma_cmm_stock_premount_prefix),
            DLUMA_CMM_STOCK_PREMOUNT_RVA,
            gamedll_image_size, &tools_global_rva)) {
        dluma_log_pair("WARN", "custom_main_menu_content_route_failure",
                     "stock_premount_contract");
        return 0;
    }
    if (tools_global_rva != DLUMA_CMM_TOOLS_GLOBAL_RVA) {
        dluma_log_pair("WARN", "custom_main_menu_content_route_failure",
                     "tools_global_rva");
        return 0;
    }
    if (DLUMA_CMM_TOOLS_VTABLE_RVA >= engine_image_size ||
        DLUMA_CMM_TOOLS_PUBLISH_CONTENT_SLOT + sizeof(void *) >
            engine_image_size - DLUMA_CMM_TOOLS_VTABLE_RVA) {
        dluma_log_pair("WARN", "custom_main_menu_content_route_failure",
                     "tools_vtable_range");
        return 0;
    }
    if (!dluma_is_readable(
            g_dluma_cmm_runtime.gamedll_base + tools_global_rva,
            sizeof(void *))) {
        dluma_log_pair("WARN", "custom_main_menu_content_route_failure",
                     "tools_global_unreadable");
        return 0;
    }
    prepare_slot =
        (void **)(g_dluma_cmm_runtime.engine_base +
                  DLUMA_CMM_TOOLS_VTABLE_RVA +
                  DLUMA_CMM_TOOLS_PREMOUNT_PREPARE_SLOT);
    packages_slot =
        (void **)(g_dluma_cmm_runtime.engine_base +
                  DLUMA_CMM_TOOLS_VTABLE_RVA +
                  DLUMA_CMM_TOOLS_PREMOUNT_PACKAGES_SLOT);
    locale_slot =
        (void **)(g_dluma_cmm_runtime.engine_base +
                  DLUMA_CMM_TOOLS_VTABLE_RVA +
                  DLUMA_CMM_TOOLS_GET_LOCALE_SLOT);
    speech_slot =
        (void **)(g_dluma_cmm_runtime.engine_base +
                  DLUMA_CMM_TOOLS_VTABLE_RVA +
                  DLUMA_CMM_TOOLS_GET_SPEECH_SLOT);
    publish_slot =
        (void **)(g_dluma_cmm_runtime.engine_base +
                  DLUMA_CMM_TOOLS_VTABLE_RVA +
                  DLUMA_CMM_TOOLS_PUBLISH_CONTENT_SLOT);
    if (!dluma_is_readable(prepare_slot, sizeof(void *))) {
        dluma_log_pair("WARN", "custom_main_menu_content_route_failure",
                     "prepare_slot_unreadable");
        return 0;
    }
    if (!dluma_is_readable(packages_slot, sizeof(void *))) {
        dluma_log_pair("WARN", "custom_main_menu_content_route_failure",
                     "packages_slot_unreadable");
        return 0;
    }
    if (!dluma_is_readable(locale_slot, sizeof(void *)) ||
        !dluma_is_readable(speech_slot, sizeof(void *)) ||
        !dluma_is_readable(publish_slot, sizeof(void *))) {
        dluma_log_pair("WARN", "custom_main_menu_content_route_failure",
                     "resource_publication_slot_unreadable");
        return 0;
    }
    if (*prepare_slot !=
            g_dluma_cmm_runtime.engine_base +
                DLUMA_CMM_TOOLS_PREMOUNT_PREPARE_RVA) {
        dluma_log_pair("WARN", "custom_main_menu_content_route_failure",
                     "prepare_slot_target");
        return 0;
    }
    if (*packages_slot !=
            g_dluma_cmm_runtime.engine_base +
                DLUMA_CMM_TOOLS_PREMOUNT_PACKAGES_RVA) {
        dluma_log_pair("WARN", "custom_main_menu_content_route_failure",
                     "packages_slot_target");
        return 0;
    }
    if (*locale_slot != g_dluma_cmm_runtime.engine_base +
                            DLUMA_CMM_TOOLS_GET_LOCALE_RVA ||
        *speech_slot != g_dluma_cmm_runtime.engine_base +
                            DLUMA_CMM_TOOLS_GET_SPEECH_RVA ||
        *publish_slot != g_dluma_cmm_runtime.engine_base +
                             DLUMA_CMM_TOOLS_PUBLISH_CONTENT_RVA) {
        dluma_log_pair("WARN", "custom_main_menu_content_route_failure",
                     "resource_publication_slot_target");
        return 0;
    }
    if (!dluma_cmm_executable(*prepare_slot, 1U)) {
        dluma_log_pair("WARN", "custom_main_menu_content_route_failure",
                     "prepare_slot_unexecutable");
        return 0;
    }
    if (!dluma_cmm_executable(*packages_slot, 1U)) {
        dluma_log_pair("WARN", "custom_main_menu_content_route_failure",
                     "packages_slot_unexecutable");
        return 0;
    }
    if (!dluma_cmm_executable(*locale_slot, 1U) ||
        !dluma_cmm_executable(*speech_slot, 1U) ||
        !dluma_cmm_executable(*publish_slot, 1U)) {
        dluma_log_pair("WARN", "custom_main_menu_content_route_failure",
                     "resource_publication_slot_unexecutable");
        return 0;
    }
    g_dluma_cmm_runtime.tools_global =
        (void **)(g_dluma_cmm_runtime.gamedll_base + tools_global_rva);
    g_dluma_cmm_runtime.premount_prepare =
        (DlumaCmmToolsPremountPrepareFn)(
            g_dluma_cmm_runtime.engine_base +
                DLUMA_CMM_TOOLS_PREMOUNT_PREPARE_RVA);
    g_dluma_cmm_runtime.premount_packages =
        (DlumaCmmToolsPremountPackagesFn)(
            g_dluma_cmm_runtime.engine_base +
                DLUMA_CMM_TOOLS_PREMOUNT_PACKAGES_RVA);
    g_dluma_cmm_runtime.get_locale_id =
        (DlumaCmmToolsGetTextFn)*locale_slot;
    g_dluma_cmm_runtime.get_speech_id =
        (DlumaCmmToolsGetTextFn)*speech_slot;
    g_dluma_cmm_runtime.publish_content =
        (DlumaCmmToolsPublishContentFn)*publish_slot;
    /* The exact Player engine allocates these ttl::string buffers through its
     * imported MSVCR100 allocator.  A different CRT heap is not compatible. */
    crt = GetModuleHandleW(L"MSVCR100.dll");
    if (crt)
        g_dluma_cmm_runtime.engine_free =
            (DlumaCmmEngineFreeFn)GetProcAddress(crt, "free");
    if (!g_dluma_cmm_runtime.engine_free) {
        dluma_log_pair("WARN", "custom_main_menu_content_route_failure",
                     "engine_crt_free_unavailable");
        return 0;
    }
    g_dluma_cmm_runtime.content_route_available = 1U;
    dluma_log_hex(
        "INFO", "custom_main_menu_stock_premount_rva",
        DLUMA_CMM_STOCK_PREMOUNT_RVA);
    dluma_log_hex(
        "INFO", "custom_main_menu_stock_premount_prepare_rva",
        DLUMA_CMM_TOOLS_PREMOUNT_PREPARE_RVA);
    dluma_log_hex(
        "INFO", "custom_main_menu_stock_premount_packages_rva",
        DLUMA_CMM_TOOLS_PREMOUNT_PACKAGES_RVA);
    dluma_log_hex(
        "INFO", "custom_main_menu_resource_publish_rva",
        DLUMA_CMM_TOOLS_PUBLISH_CONTENT_RVA);
    dluma_log_pair(
        "INFO", "custom_main_menu_content_route",
        "stock_prepare_packages_locale_speech_resource_publication");
    return 1;
}

static void dluma_cmm_restore_fx(void *background)
{
    LONG observed;
    int should_restore;
    if (!g_dluma_cmm_runtime.fx_state_captured)
        return;
    if (!g_dluma_cmm_runtime.fx_available ||
        !g_dluma_cmm_runtime.fx_enable ||
        !g_dluma_cmm_runtime.fx_enable_flag)
        goto cleared;
    observed = dluma_atomic_compare_exchange(
        g_dluma_cmm_runtime.fx_enable_flag, 0, 0);
    should_restore =
        dluma_custom_main_menu_runtime_fx_restore_should_apply(
            g_dluma_cmm_runtime.fx_changed_by_runtime,
            background &&
                background == g_dluma_cmm_runtime.fx_background_owner,
            observed, 1);
    if (should_restore)
    {
        __try
        {
            g_dluma_cmm_runtime.fx_enable(
                g_dluma_cmm_runtime.fx_level,
                (unsigned char)g_dluma_cmm_runtime.fx_previous_flag);
        }
        __except (DLUMA_CMM_EXCEPTION_EXECUTE_HANDLER)
        {
            dluma_log_pair(
                "WARN", "custom_main_menu_fx_restore",
                "engine_call_raised_exception");
            goto cleared;
        }
        observed = dluma_atomic_compare_exchange(
            g_dluma_cmm_runtime.fx_enable_flag, 0, 0);
        dluma_log_u64(
            observed == g_dluma_cmm_runtime.fx_previous_flag
                ? "INFO" : "WARN",
            "custom_main_menu_fx_restored_state",
            (ULONGLONG)(unsigned int)observed);
    }
    else if (g_dluma_cmm_runtime.fx_changed_by_runtime)
        dluma_log_pair(
            "WARN", "custom_main_menu_fx_restore",
            "ownership_lost_state_left_unchanged");
cleared:
    g_dluma_cmm_runtime.fx_background_owner = 0;
    g_dluma_cmm_runtime.fx_level = 0;
    g_dluma_cmm_runtime.fx_previous_flag = 0;
    g_dluma_cmm_runtime.fx_state_captured = 0;
    g_dluma_cmm_runtime.fx_changed_by_runtime = 0;
}

static void dluma_cmm_enable_fx(void *level, void *background)
{
    LONG previous;
    LONG resulting;
    if (!level || !background || !g_dluma_cmm_runtime.fx_available ||
        !g_dluma_cmm_runtime.fx_enable ||
        !g_dluma_cmm_runtime.fx_enable_flag)
        return;
    if (g_dluma_cmm_runtime.fx_state_captured)
    {
        if (g_dluma_cmm_runtime.fx_background_owner == background)
            return;
        dluma_cmm_restore_fx(g_dluma_cmm_runtime.fx_background_owner);
    }
    previous = dluma_atomic_compare_exchange(
        g_dluma_cmm_runtime.fx_enable_flag, 0, 0);
    if (previous != 0 && previous != 1)
    {
        dluma_log_pair(
            "WARN", "custom_main_menu_fx_global_gate",
            "backing_state_invalid");
        return;
    }
    g_dluma_cmm_runtime.fx_background_owner = background;
    g_dluma_cmm_runtime.fx_level = level;
    g_dluma_cmm_runtime.fx_previous_flag = previous;
    g_dluma_cmm_runtime.fx_state_captured = 1U;
    if (!previous)
    {
        __try
        {
            g_dluma_cmm_runtime.fx_enable(level, 1U);
        }
        __except (DLUMA_CMM_EXCEPTION_EXECUTE_HANDLER)
        {
            dluma_log_pair(
                "WARN", "custom_main_menu_fx_global_gate",
                "engine_call_raised_exception");
            dluma_cmm_restore_fx(background);
            return;
        }
    }
    resulting = dluma_atomic_compare_exchange(
        g_dluma_cmm_runtime.fx_enable_flag, 0, 0);
    g_dluma_cmm_runtime.fx_changed_by_runtime =
        (unsigned char)(!previous && resulting == 1);
    dluma_log_u64(
        "INFO", "custom_main_menu_fx_global_gate_previous",
        (ULONGLONG)(unsigned int)previous);
    dluma_log_u64(
        resulting == 1 ? "INFO" : "WARN",
        "custom_main_menu_fx_global_enabled",
        (ULONGLONG)(unsigned int)resulting);
}

static int dluma_cmm_fx_emitter_state(
    void *object, void *level, int *catalog_resolved, int *spawned)
{
    BYTE *emitter;
    ULONG_PTR definition_a;
    ULONG_PTR definition_b;
    ULONG_PTR definition_c;
    DWORD instance;
    void *object_level;
    if (!object || !level || !catalog_resolved || !spawned ||
        !g_dluma_cmm_runtime.gamedll_base ||
        !g_dluma_cmm_runtime.get_level ||
        !dluma_is_readable(object, sizeof(void *)) ||
        *(void **)object !=
            g_dluma_cmm_runtime.gamedll_base +
                DLUMA_CMM_FX_EMITTER_IGSOBJECT_VTABLE_RVA)
        return 0;
    object_level = g_dluma_cmm_runtime.get_level(object);
    if (object_level != level)
        return 0;
    emitter = (BYTE *)object - DLUMA_CMM_FX_EMITTER_IGSOBJECT_OFFSET;
    if (!dluma_is_readable(
            emitter + DLUMA_CMM_FX_EMITTER_DEFINITION_A_OFFSET,
            sizeof(definition_a)) ||
        !dluma_is_readable(
            emitter + DLUMA_CMM_FX_EMITTER_DEFINITION_B_OFFSET,
            sizeof(definition_b)) ||
        !dluma_is_readable(
            emitter + DLUMA_CMM_FX_EMITTER_DEFINITION_C_OFFSET,
            sizeof(definition_c)) ||
        !dluma_is_readable(
            emitter + DLUMA_CMM_FX_EMITTER_INSTANCE_OFFSET,
            sizeof(instance)))
        return 0;
    definition_a = *(ULONG_PTR *)(
        emitter + DLUMA_CMM_FX_EMITTER_DEFINITION_A_OFFSET);
    definition_b = *(ULONG_PTR *)(
        emitter + DLUMA_CMM_FX_EMITTER_DEFINITION_B_OFFSET);
    definition_c = *(ULONG_PTR *)(
        emitter + DLUMA_CMM_FX_EMITTER_DEFINITION_C_OFFSET);
    instance = *(DWORD *)(emitter + DLUMA_CMM_FX_EMITTER_INSTANCE_OFFSET);
    *catalog_resolved = definition_a || definition_b || definition_c;
    *spawned = instance != 0U;
    return 1;
}

static void dluma_cmm_clear_started_fx_emitters(void)
{
    memset(
        g_dluma_cmm_runtime.fx_started_emitters, 0,
        sizeof(g_dluma_cmm_runtime.fx_started_emitters));
    g_dluma_cmm_runtime.fx_started_emitter_count = 0U;
    g_dluma_cmm_runtime.fx_start_generation = 0;
    g_dluma_cmm_runtime.fx_start_background = 0;
    g_dluma_cmm_runtime.fx_start_level = 0;
    g_dluma_cmm_runtime.fx_start_attempted = 0U;
}

static int dluma_cmm_fx_emitter_start_contract(
    void *object, void *level, int require_unstarted,
    BYTE **emitter_out)
{
    BYTE *emitter;
    BYTE *igs_object;
    BYTE *lifecycle;
    float delay;
    DWORD instance;
    void *manager;
    void *lifecycle_vtable;
    int catalog_resolved = 0;
    int spawned = 0;
    if (emitter_out)
        *emitter_out = 0;
    if (!object || !level || !emitter_out ||
        !g_dluma_cmm_runtime.fx_lifecycle_available ||
        !g_dluma_cmm_runtime.get_fx_manager ||
        !g_dluma_cmm_runtime.fx_emitter_on_enabled ||
        !g_dluma_cmm_runtime.fx_emitter_on_disabled ||
        !dluma_cmm_fx_emitter_state(
            object, level, &catalog_resolved, &spawned) ||
        !catalog_resolved)
        return 0;
    emitter = (BYTE *)object - DLUMA_CMM_FX_EMITTER_IGSOBJECT_OFFSET;
    igs_object = emitter + DLUMA_CMM_FX_EMITTER_IGSOBJECT_OFFSET;
    lifecycle = emitter + DLUMA_CMM_FX_EMITTER_LIFECYCLE_OFFSET;
    if (!dluma_is_readable(emitter, sizeof(void *)) ||
        *(void **)emitter !=
            g_dluma_cmm_runtime.gamedll_base +
                DLUMA_CMM_FX_EMITTER_PRIMARY_VTABLE_RVA ||
        !dluma_is_readable(igs_object, sizeof(void *)) ||
        *(void **)igs_object !=
            g_dluma_cmm_runtime.gamedll_base +
                DLUMA_CMM_FX_EMITTER_IGSOBJECT_VTABLE_RVA ||
        !dluma_is_readable(lifecycle, sizeof(void *)) ||
        *(void **)lifecycle !=
            g_dluma_cmm_runtime.gamedll_base +
                DLUMA_CMM_FX_EMITTER_LIFECYCLE_VTABLE_RVA ||
        !dluma_is_readable(
            *(void **)lifecycle, 0x30U) ||
        !dluma_is_readable(
            emitter + DLUMA_CMM_FX_EMITTER_DELAY_OFFSET, sizeof(delay)) ||
        !dluma_is_readable(
            emitter + DLUMA_CMM_FX_EMITTER_INSTANCE_OFFSET,
            sizeof(instance)))
        return 0;
    lifecycle_vtable = *(void **)lifecycle;
    if (*(void **)((BYTE *)lifecycle_vtable + 0x20U) !=
            (void *)g_dluma_cmm_runtime.fx_emitter_on_enabled ||
        *(void **)((BYTE *)lifecycle_vtable + 0x28U) !=
            (void *)g_dluma_cmm_runtime.fx_emitter_on_disabled)
        return 0;
    delay = *(float *)(emitter + DLUMA_CMM_FX_EMITTER_DELAY_OFFSET);
    instance = *(DWORD *)(emitter + DLUMA_CMM_FX_EMITTER_INSTANCE_OFFSET);
    if (!dluma_custom_main_menu_runtime_float_finite(delay) ||
        (require_unstarted && instance != 0U))
        return 0;
    manager = g_dluma_cmm_runtime.get_fx_manager(igs_object);
    if (!manager || !dluma_is_readable(manager, sizeof(void *)))
        return 0;
    *emitter_out = emitter;
    return 1;
}

static void dluma_cmm_start_fx_emitters_after_activation(
    void *level, void *background)
{
    const DlumaCustomMainMenuManifest *manifest;
    void *seed = 0;
    void *head = 0;
    void *object = 0;
    void *link = 0;
    unsigned int steps = 0U;
    unsigned int candidates = 0U;
    unsigned int dispatched = 0U;
    unsigned int spawned_after_start = 0U;
    ULONGLONG generation = g_dluma_cmm_runtime.frontend_generation;
    const char *status = "emitter_missing";
    if (!level || !background || !generation)
        return;
    if (g_dluma_cmm_runtime.fx_start_attempted &&
        g_dluma_cmm_runtime.fx_start_generation == generation &&
        g_dluma_cmm_runtime.fx_start_background == background)
        return;
    if (g_dluma_cmm_runtime.fx_started_emitter_count)
    {
        dluma_log_pair(
            "WARN", "custom_main_menu_fx_start",
            "previous_generation_tracking_retired_without_dereference");
        dluma_cmm_clear_started_fx_emitters();
    }
    g_dluma_cmm_runtime.fx_start_attempted = 1U;
    g_dluma_cmm_runtime.fx_start_generation = generation;
    g_dluma_cmm_runtime.fx_start_background = background;
    g_dluma_cmm_runtime.fx_start_level = level;
    if (!g_dluma_cmm_runtime.fx_lifecycle_available)
    {
        dluma_log_pair(
            "WARN", "custom_main_menu_fx_lifecycle",
            "exact_emitter_lifecycle_unavailable");
        return;
    }
    manifest = dluma_custom_main_menu_selected_manifest();
    if (!manifest || !g_dluma_cmm_runtime.get_object_by_id ||
        !g_dluma_cmm_runtime.get_prev || !g_dluma_cmm_runtime.get_next ||
        !g_dluma_cmm_runtime.get_level)
    {
        dluma_log_pair(
            "WARN", "custom_main_menu_fx_lifecycle",
            "start_contract_unavailable");
        return;
    }
    __try
    {
        seed = g_dluma_cmm_runtime.get_object_by_id(
            level, manifest->camera_id);
        if (!seed || g_dluma_cmm_runtime.get_level(seed) != level)
        {
            status = "object_seed_missing";
            __leave;
        }
        head = seed;
        for (steps = 0U;
             steps < DLUMA_CMM_FX_OBJECT_LIMIT;
             ++steps)
        {
            link = g_dluma_cmm_runtime.get_prev(head);
            if (!link || link == head || link == seed ||
                g_dluma_cmm_runtime.get_level(link) != level)
                break;
            head = link;
        }
        object = head;
        for (steps = 0U;
             object && steps < DLUMA_CMM_FX_OBJECT_LIMIT;
             ++steps)
        {
            BYTE *emitter = 0;
            if (g_dluma_cmm_runtime.get_level(object) != level)
                break;
            if (dluma_cmm_fx_emitter_start_contract(
                    object, level, 1, &emitter))
            {
                int catalog_resolved = 0;
                int spawned = 0;
                ++candidates;
                if (g_dluma_cmm_runtime.fx_started_emitter_count <
                        DLUMA_CMM_FX_STARTED_EMITTER_CAPACITY)
                {
                    DlumaCmmStartedFxEmitter *entry =
                        &g_dluma_cmm_runtime.fx_started_emitters[
                            g_dluma_cmm_runtime.fx_started_emitter_count];
                    entry->emitter = emitter;
                    entry->level = level;
                    entry->background = background;
                    entry->generation = generation;
                    ++g_dluma_cmm_runtime.fx_started_emitter_count;
                    ++dispatched;
                    g_dluma_cmm_runtime.fx_emitter_on_enabled(
                        emitter + DLUMA_CMM_FX_EMITTER_LIFECYCLE_OFFSET);
                    if (dluma_cmm_fx_emitter_state(
                            emitter +
                                DLUMA_CMM_FX_EMITTER_IGSOBJECT_OFFSET,
                            level, &catalog_resolved, &spawned) &&
                        spawned)
                        ++spawned_after_start;
                }
            }
            link = g_dluma_cmm_runtime.get_next(object);
            if (!link || link == object || link == head)
                break;
            object = link;
        }
        if (!candidates)
            status = "emitter_missing_or_contract_rejected";
        else if (!dispatched)
            status = "start_capacity_exhausted";
        else if (spawned_after_start)
            status = "ready";
        else
            status = "start_dispatched_authored_delay_or_handle_pending";
    }
    __except (DLUMA_CMM_EXCEPTION_EXECUTE_HANDLER)
    {
        status = "start_exception";
    }
    dluma_log_u64(
        candidates ? "INFO" : "WARN",
        "custom_main_menu_fx_start_candidate_count",
        (ULONGLONG)candidates);
    dluma_log_u64(
        dispatched ? "INFO" : "WARN",
        "custom_main_menu_fx_start_dispatched_count",
        (ULONGLONG)dispatched);
    dluma_log_u64(
        spawned_after_start ? "INFO" : "WARN",
        "custom_main_menu_fx_spawned_after_start_count",
        (ULONGLONG)spawned_after_start);
    dluma_log_pair(
        dispatched ? "INFO" : "WARN",
        "custom_main_menu_fx_lifecycle", status);
}

static void dluma_cmm_stop_started_fx_emitters(void *background)
{
    unsigned int index;
    unsigned int stopped = 0U;
    unsigned int skipped = 0U;
    ULONGLONG generation = g_dluma_cmm_runtime.frontend_generation;
    if (!g_dluma_cmm_runtime.fx_started_emitter_count)
    {
        dluma_cmm_clear_started_fx_emitters();
        return;
    }
    if (!background || background != g_dluma_cmm_runtime.fx_start_background ||
        !generation || generation != g_dluma_cmm_runtime.fx_start_generation ||
        !g_dluma_cmm_runtime.fx_lifecycle_available)
    {
        dluma_log_pair(
            "WARN", "custom_main_menu_fx_stop",
            "ownership_lost_tracking_retired_without_dereference");
        dluma_cmm_clear_started_fx_emitters();
        return;
    }
    for (index = 0U;
         index < g_dluma_cmm_runtime.fx_started_emitter_count;
         ++index)
    {
        DlumaCmmStartedFxEmitter *entry =
            &g_dluma_cmm_runtime.fx_started_emitters[index];
        BYTE *validated_emitter = 0;
        __try
        {
            if (!entry->emitter || entry->background != background ||
                entry->generation != generation ||
                entry->level != g_dluma_cmm_runtime.fx_start_level ||
                !dluma_cmm_fx_emitter_start_contract(
                    (BYTE *)entry->emitter +
                        DLUMA_CMM_FX_EMITTER_IGSOBJECT_OFFSET,
                    entry->level, 0, &validated_emitter) ||
                validated_emitter != entry->emitter)
            {
                ++skipped;
                __leave;
            }
            g_dluma_cmm_runtime.fx_emitter_on_disabled(
                (BYTE *)entry->emitter +
                    DLUMA_CMM_FX_EMITTER_LIFECYCLE_OFFSET);
            ++stopped;
        }
        __except (DLUMA_CMM_EXCEPTION_EXECUTE_HANDLER)
        {
            ++skipped;
        }
    }
    dluma_log_u64(
        "INFO", "custom_main_menu_fx_stop_dispatched_count",
        (ULONGLONG)stopped);
    if (skipped)
        dluma_log_u64(
            "WARN", "custom_main_menu_fx_stop_skipped_count",
            (ULONGLONG)skipped);
    dluma_cmm_clear_started_fx_emitters();
}

static int dluma_cmm_object_name_is(void *object, const char *expected)
{
    void *record;
    const char *name;
    SIZE_T length;
    if (!object || !expected || !expected[0] ||
        !dluma_is_readable((BYTE *)object + DLUMA_CMM_OBJECT_NAME_RECORD_OFFSET,
                         sizeof(void *)))
        return 0;
    record = *(void **)((BYTE *)object + DLUMA_CMM_OBJECT_NAME_RECORD_OFFSET);
    if (!record ||
        !dluma_is_readable((BYTE *)record + DLUMA_CMM_NAME_POINTER_OFFSET,
                         sizeof(void *)))
        return 0;
    name = *(const char **)((BYTE *)record + DLUMA_CMM_NAME_POINTER_OFFSET);
    length = strlen(expected);
    return name && length < DLUMA_CUSTOM_MENU_OBJECT_CAPACITY &&
           dluma_is_readable(name, length + 1U) &&
           memcmp(name, expected, length + 1U) == 0;
}

static void dluma_cmm_absolute_jump(BYTE *output, ULONG_PTR target)
{
    ULONGLONG address = (ULONGLONG)target;
    output[0] = 0x48;
    output[1] = 0xB8;
    memcpy(output + 2, &address, sizeof(address));
    output[10] = 0xFF;
    output[11] = 0xE0;
}

static int dluma_cmm_texture_definitions_unload(void)
{
    int success = 1;
    if (!g_dluma_cmm_runtime.texture_definitions_loaded)
        return 1;
    if (!g_dluma_cmm_runtime.texture_level ||
        !g_dluma_cmm_runtime.unload_ui_textures)
        return 0;
    __try
    {
        /* The decorated export retains the historical string signature, but
         * the exact Player implementation consumes only RCX and unloads the
         * frontend level's complete UI-texture set. */
        g_dluma_cmm_runtime.unload_ui_textures(
            g_dluma_cmm_runtime.texture_level);
    }
    __except (DLUMA_CMM_EXCEPTION_EXECUTE_HANDLER)
    {
        success = 0;
    }
    if (success)
    {
        g_dluma_cmm_runtime.texture_definitions_loaded = 0U;
        g_dluma_cmm_runtime.texture_level = 0;
        g_dluma_cmm_runtime.texture_fingerprint[0] = 0;
        dluma_log_pair(
            "INFO", "custom_main_menu_texture_definitions",
            "unloaded");
    }
    return success;
}

static int dluma_cmm_texture_definitions_load(void *level)
{
    const DlumaCustomMainMenuManifest *manifest =
        dluma_custom_main_menu_selected_manifest();
    const char *fingerprint =
        dluma_custom_main_menu_selected_fingerprint();
    unsigned int index;
    unsigned int loaded = 0U;
    if (!manifest || !manifest->use_custom_textures)
        return 1;
    if (!level || !fingerprint || !fingerprint[0] ||
        !g_dluma_cmm_runtime.load_ui_textures ||
        !g_dluma_cmm_runtime.unload_ui_textures)
        return 0;
    if (dluma_custom_main_menu_runtime_texture_registration_matches(
            g_dluma_cmm_runtime.texture_level, level,
            g_dluma_cmm_runtime.texture_fingerprint, fingerprint,
            g_dluma_cmm_runtime.texture_definitions_loaded))
        return 1;
    if (g_dluma_cmm_runtime.texture_definitions_loaded &&
        !dluma_cmm_texture_definitions_unload())
        return 0;
    for (index = 0; index < manifest->texture_def_count; ++index)
    {
        const DlumaCustomMainMenuTextureDef *definition =
            &manifest->texture_defs[index];
        DlumaCmmTtlString value;
        value.data = (char *)definition->name;
        value.size = (unsigned int)strlen(definition->name);
        value.capacity = value.size;
        __try
        {
            g_dluma_cmm_runtime.load_ui_textures(level, &value);
            ++loaded;
        }
        __except (DLUMA_CMM_EXCEPTION_EXECUTE_HANDLER)
        {
            break;
        }
    }
    if (loaded != manifest->texture_def_count)
    {
        if (loaded > 0U)
        {
            __try
            {
                g_dluma_cmm_runtime.unload_ui_textures(level);
            }
            __except (DLUMA_CMM_EXCEPTION_EXECUTE_HANDLER)
            {
                g_dluma_cmm_runtime.teardown_failed = 1U;
            }
        }
        return 0;
    }
    g_dluma_cmm_runtime.texture_level = level;
    g_dluma_cmm_runtime.texture_definitions_loaded = 1U;
    dluma_str_copy(
        g_dluma_cmm_runtime.texture_fingerprint,
        DLUMA_ARRAY_COUNT(g_dluma_cmm_runtime.texture_fingerprint),
        fingerprint);
    dluma_log_u64(
        "INFO", "custom_main_menu_texture_definition_count",
        manifest->texture_def_count);
    dluma_log_pair(
        "INFO", "custom_main_menu_texture_definitions",
        "loaded_before_ui_registration");
    return 1;
}

static int dluma_cmm_ttl_valid(const DlumaCmmTtlString *value)
{
    return value && value->size <= value->capacity &&
           value->capacity < 4096U &&
           ((!value->size && (!value->data ||
                              dluma_is_readable(value->data, 1U))) ||
            (value->data &&
             dluma_is_readable(value->data, (SIZE_T)value->size + 1U) &&
                              value->data[value->size] == 0));
}

static int dluma_cmm_prepare_content_descriptor(void)
{
    const WCHAR *project_root =
        dluma_custom_main_menu_selected_project_root();
    const char *fingerprint =
        dluma_custom_main_menu_selected_fingerprint();
    char canonical_path[DLUMA_CMM_CONTENT_NAME_CAPACITY];
    SIZE_T length;
    if (!project_root || !project_root[0] ||
        !dluma_wide_to_utf8(
            project_root, canonical_path,
            DLUMA_ARRAY_COUNT(canonical_path)) ||
        !dluma_custom_main_menu_runtime_engine_content_name(
            canonical_path,
            g_dluma_cmm_runtime.content_name_storage,
            DLUMA_ARRAY_COUNT(g_dluma_cmm_runtime.content_name_storage)) ||
        !dluma_custom_main_menu_runtime_content_identity(
            fingerprint, &g_dluma_cmm_runtime.content_identity))
        return 0;
    length = strlen(g_dluma_cmm_runtime.content_name_storage);
    if (!length ||
        length >= DLUMA_ARRAY_COUNT(
            g_dluma_cmm_runtime.content_name_storage) ||
        length >= 4096U ||
        length > 0xFFFFFFFFUL)
        return 0;
    g_dluma_cmm_runtime.content_name.data =
        g_dluma_cmm_runtime.content_name_storage;
    g_dluma_cmm_runtime.content_name.size = (unsigned int)length;
    g_dluma_cmm_runtime.content_name.capacity =
        (unsigned int)length;
    memcpy(
        g_dluma_cmm_runtime.package_name_storage,
        g_dluma_cmm_runtime.content_name_storage, length + 1U);
    g_dluma_cmm_runtime.package_name.data =
        g_dluma_cmm_runtime.package_name_storage;
    g_dluma_cmm_runtime.package_name.size = (unsigned int)length;
    g_dluma_cmm_runtime.package_name.capacity =
        (unsigned int)(DLUMA_ARRAY_COUNT(
            g_dluma_cmm_runtime.package_name_storage) - 1U);
    if (!dluma_cmm_ttl_valid(&g_dluma_cmm_runtime.content_name))
        return 0;
    dluma_log_pair(
        "INFO", "custom_main_menu_content_descriptor",
        "immutable_process_lifetime_project_root_ready");
    dluma_log_pair(
        "INFO", "custom_main_menu_content_root",
        g_dluma_cmm_runtime.content_name_storage);
    dluma_log_pair(
        "INFO", "custom_main_menu_content_root_form",
        "engine_dos_path_without_extended_prefix");
    dluma_log_u64(
        "INFO", "custom_main_menu_content_identity",
        g_dluma_cmm_runtime.content_identity);
    return 1;
}

static const char *dluma_cmm_publication_state_name(int state)
{
    switch (state)
    {
    case DLUMA_CMM_PUBLICATION_AWAITING_UI_INITIALIZER:
        return "awaiting_ui_initializer";
    case DLUMA_CMM_PUBLICATION_STOCK_PREMOUNT:
        return "stock_premount";
    case DLUMA_CMM_PUBLICATION_AWAITING_STOCK_COMPLETION:
        return "awaiting_stock_completion";
    case DLUMA_CMM_PUBLICATION_CATALOG_VERIFIED:
        return "catalog_verified";
    case DLUMA_CMM_PUBLICATION_READY_BEFORE_UI:
        return "ready_before_ui";
    case DLUMA_CMM_PUBLICATION_LOADING_BACKGROUND:
        return "loading_background";
    case DLUMA_CMM_PUBLICATION_ACTIVE:
        return "active";
    case DLUMA_CMM_PUBLICATION_STOCK_FALLBACK:
        return "stock_fallback";
    default:
        return "invalid";
    }
}

static int dluma_cmm_publication_transition(int event)
{
    LONG current;
    int next;
    for (;;)
    {
        current = dluma_atomic_compare_exchange(
            &g_dluma_cmm_runtime.publication_state, 0, 0);
        if (!dluma_custom_main_menu_runtime_publication_transition(
                (int)current, event, &next))
            return 0;
        if (dluma_atomic_compare_exchange(
                &g_dluma_cmm_runtime.publication_state,
                (LONG)next, current) == current)
            break;
    }
    dluma_log_pair(
        "INFO", "custom_main_menu_publication_state_from",
        dluma_cmm_publication_state_name((int)current));
    dluma_log_pair(
        "INFO", "custom_main_menu_publication_state_to",
        dluma_cmm_publication_state_name(next));
    return 1;
}

static int dluma_cmm_main_menu_module_valid(void *module)
{
    return module &&
           dluma_is_readable(module, sizeof(void *)) &&
           dluma_is_readable(
               (BYTE *)module + DLUMA_CMM_MAIN_MENU_IGSOBJECT_OFFSET,
               sizeof(void *)) &&
           *(void **)module ==
               g_dluma_cmm_runtime.gamedll_base +
                   DLUMA_CMM_MAIN_MENU_VTABLE_RVA &&
           *(void **)((BYTE *)module +
                      DLUMA_CMM_MAIN_MENU_IGSOBJECT_OFFSET) ==
               g_dluma_cmm_runtime.gamedll_base +
                   DLUMA_CMM_MAIN_MENU_IGSOBJECT_VTABLE_RVA;
}

static int dluma_cmm_content_exception_filter(
    EXCEPTION_POINTERS *information)
{
    g_dluma_cmm_runtime.content_exception_code = 0;
    g_dluma_cmm_runtime.content_exception_address = 0;
    if (information && information->ExceptionRecord)
    {
        g_dluma_cmm_runtime.content_exception_code =
            information->ExceptionRecord->ExceptionCode;
        g_dluma_cmm_runtime.content_exception_address =
            (ULONG_PTR)information->ExceptionRecord->ExceptionAddress;
    }
    return DLUMA_CMM_EXCEPTION_EXECUTE_HANDLER;
}

static void dluma_cmm_log_content_exception(void)
{
    ULONG_PTR address = g_dluma_cmm_runtime.content_exception_address;
    if (!g_dluma_cmm_runtime.content_exception_code)
        return;
    dluma_log_hex(
        "WARN", "custom_main_menu_content_exception_code",
        g_dluma_cmm_runtime.content_exception_code);
    if (address >= (ULONG_PTR)g_dluma_cmm_runtime.gamedll_base &&
        address - (ULONG_PTR)g_dluma_cmm_runtime.gamedll_base <
            g_dluma_cmm_runtime.gamedll_image_size)
    {
        dluma_log_pair(
            "WARN", "custom_main_menu_content_exception_module",
            "gamedll_x64_rwdi.dll");
        dluma_log_hex(
            "WARN", "custom_main_menu_content_exception_rva",
            (DWORD)(address -
                    (ULONG_PTR)g_dluma_cmm_runtime.gamedll_base));
    }
    else if (address >= (ULONG_PTR)g_dluma_cmm_runtime.engine_base &&
             address - (ULONG_PTR)g_dluma_cmm_runtime.engine_base <
                 g_dluma_cmm_runtime.engine_image_size)
    {
        dluma_log_pair(
            "WARN", "custom_main_menu_content_exception_module",
            "engine_x64_rwdi.dll");
        dluma_log_hex(
            "WARN", "custom_main_menu_content_exception_rva",
            (DWORD)(address -
                    (ULONG_PTR)g_dluma_cmm_runtime.engine_base));
    }
    else
        dluma_log_u64(
            "WARN", "custom_main_menu_content_exception_address",
            (ULONGLONG)address);
}

static int dluma_cmm_content_fail(const char *phase)
{
    if (!g_dluma_cmm_runtime.content_terminal_failure)
    {
        (void)dluma_cmm_publication_transition(
            DLUMA_CMM_PUBLICATION_EVENT_FAIL);
        g_dluma_cmm_runtime.content_terminal_failure = 1U;
        g_dluma_cmm_runtime.content_failure_phase =
            phase ? phase : "unknown_content_publication_failure";
        g_dluma_cmm_runtime.content_failure_logged = 1U;
        dluma_log_pair(
            "WARN", "custom_main_menu_content_failure_phase",
            g_dluma_cmm_runtime.content_failure_phase);
        dluma_cmm_log_content_exception();
        dluma_log_pair(
            "WARN", "custom_main_menu_content_publication",
            "stock_project_publication_failed_stock_frontend_retained");
    }
    return DLUMA_CMM_CONTENT_FAILED;
}

static int dluma_cmm_screen_snapshot(
    void *screen, int *screen_id_out,
    char *script_out, unsigned int script_capacity)
{
    DlumaCmmTtlString *script = 0;
    int screen_id = 0;
    int valid = 0;
    if (screen_id_out)
        *screen_id_out = 0;
    if (script_out && script_capacity)
        script_out[0] = 0;
    if (!screen || !screen_id_out || !script_out ||
        script_capacity < 2U || !g_dluma_cmm_runtime.get_screen_id ||
        !g_dluma_cmm_runtime.get_script_name)
        return 0;
    __try
    {
        screen_id = g_dluma_cmm_runtime.get_screen_id(screen);
        script = g_dluma_cmm_runtime.get_script_name(screen);
        if (dluma_cmm_ttl_valid(script) && script->size > 0U &&
            script->size + 1U <= script_capacity)
        {
            memcpy(script_out, script->data, script->size);
            script_out[script->size] = 0;
            valid = 1;
        }
    }
    __except (DLUMA_CMM_EXCEPTION_EXECUTE_HANDLER)
    {
        valid = 0;
    }
    if (!valid)
    {
        script_out[0] = 0;
        return 0;
    }
    *screen_id_out = screen_id;
    return 1;
}

static void dluma_cmm_visible_screen_shown(
    void *screen, int screen_id, const char *script)
{
    unsigned int index;
    if (!screen || !script || !script[0])
        return;
    dluma_cmm_lock(&g_dluma_cmm_runtime.visible_screen_lock);
    for (index = 0; index < g_dluma_cmm_runtime.visible_screen_count; ++index)
    {
        if (g_dluma_cmm_runtime.visible_screens[index].screen == screen)
        {
            unsigned int move;
            for (move = index + 1U;
                 move < g_dluma_cmm_runtime.visible_screen_count; ++move)
                g_dluma_cmm_runtime.visible_screens[move - 1U] =
                    g_dluma_cmm_runtime.visible_screens[move];
            --g_dluma_cmm_runtime.visible_screen_count;
            break;
        }
    }
    if (g_dluma_cmm_runtime.visible_screen_count ==
        DLUMA_CMM_VISIBLE_SCREEN_CAPACITY)
    {
        for (index = 1U; index < DLUMA_CMM_VISIBLE_SCREEN_CAPACITY; ++index)
            g_dluma_cmm_runtime.visible_screens[index - 1U] =
                g_dluma_cmm_runtime.visible_screens[index];
        --g_dluma_cmm_runtime.visible_screen_count;
    }
    index = g_dluma_cmm_runtime.visible_screen_count++;
    memset(&g_dluma_cmm_runtime.visible_screens[index], 0,
           sizeof(g_dluma_cmm_runtime.visible_screens[index]));
    g_dluma_cmm_runtime.visible_screens[index].screen = screen;
    g_dluma_cmm_runtime.visible_screens[index].screen_id = screen_id;
    dluma_str_copy(
        g_dluma_cmm_runtime.visible_screens[index].script,
        DLUMA_ARRAY_COUNT(g_dluma_cmm_runtime.visible_screens[index].script),
        script);
    g_dluma_cmm_runtime.empty_stack_since_ms = 0;
    dluma_cmm_unlock(&g_dluma_cmm_runtime.visible_screen_lock);
}

static void dluma_cmm_visible_screen_hidden(void *screen)
{
    unsigned int index;
    if (!screen)
        return;
    dluma_cmm_lock(&g_dluma_cmm_runtime.visible_screen_lock);
    for (index = g_dluma_cmm_runtime.visible_screen_count; index > 0U; --index)
    {
        unsigned int item = index - 1U;
        if (g_dluma_cmm_runtime.visible_screens[item].screen == screen)
        {
            unsigned int move;
            for (move = item + 1U;
                 move < g_dluma_cmm_runtime.visible_screen_count; ++move)
                g_dluma_cmm_runtime.visible_screens[move - 1U] =
                    g_dluma_cmm_runtime.visible_screens[move];
            --g_dluma_cmm_runtime.visible_screen_count;
            memset(
                &g_dluma_cmm_runtime.visible_screens[
                    g_dluma_cmm_runtime.visible_screen_count],
                0, sizeof(g_dluma_cmm_runtime.visible_screens[0]));
            break;
        }
    }
    if (!g_dluma_cmm_runtime.visible_screen_count &&
        !g_dluma_cmm_runtime.empty_stack_since_ms)
        g_dluma_cmm_runtime.empty_stack_since_ms = GetTickCount64();
    dluma_cmm_unlock(&g_dluma_cmm_runtime.visible_screen_lock);
}

static void dluma_cmm_visible_screens_clear(void)
{
    dluma_cmm_lock(&g_dluma_cmm_runtime.visible_screen_lock);
    memset(g_dluma_cmm_runtime.visible_screens, 0,
           sizeof(g_dluma_cmm_runtime.visible_screens));
    g_dluma_cmm_runtime.visible_screen_count = 0;
    g_dluma_cmm_runtime.empty_stack_since_ms = GetTickCount64();
    dluma_cmm_unlock(&g_dluma_cmm_runtime.visible_screen_lock);
}

static int dluma_cmm_same_ttl(
    const DlumaCmmTtlString *left, const DlumaCmmTtlString *right)
{
    return left && right &&
           dluma_custom_main_menu_runtime_ttl_unchanged(
               left->data, left->size, left->capacity,
               right->data, right->size, right->capacity);
}

static DlumaCmmTtlString * __fastcall dluma_cmm_music_assign_hook(
    DlumaCmmTtlString *destination,
    const DlumaCmmTtlString *source)
{
    DlumaCmmStringAssignFn original =
        g_dluma_cmm_runtime.music_assign_original_fn;
    const DlumaCmmTtlString *selected_source = source;
    DlumaCmmTtlString replacement;
    const char *cue_name = 0;
    void *module = 0;
    int source_is_music_main = 0;
    int destination_matches = 0;
    int primary_vtable_matches = 0;
    int igsobject_vtable_matches = 0;
    int selection_valid = 0;
    int prepared_bank_valid = 0;
    DlumaCmmTtlString *result = destination;
    dluma_atomic_increment(&g_dluma_cmm_runtime.active_calls);
    __try
    {
        memset(&replacement, 0, sizeof(replacement));
        __try
        {
            source_is_music_main =
                source == (const DlumaCmmTtlString *)(
                    g_dluma_cmm_runtime.gamedll_base +
                    DLUMA_CMM_MUSIC_MAIN_RVA);
            /* MUSIC_INGAME and every unrelated ttl assignment pass through
             * unchanged.  Only the exact static MUSIC_MAIN object reaches
             * the prepared-bank route. */
            if (source_is_music_main && destination &&
                (ULONG_PTR)destination >=
                    DLUMA_CMM_MAIN_MENU_MUSIC_NAME_OFFSET)
            {
                module = (BYTE *)destination -
                         DLUMA_CMM_MAIN_MENU_MUSIC_NAME_OFFSET;
                destination_matches =
                    destination == (DlumaCmmTtlString *)(
                        (BYTE *)module +
                        DLUMA_CMM_MAIN_MENU_MUSIC_NAME_OFFSET) &&
                    dluma_cmm_writable(destination, sizeof(*destination));
                if (destination_matches &&
                    dluma_is_readable(module, sizeof(void *)) &&
                    dluma_is_readable(
                        (BYTE *)module +
                            DLUMA_CMM_MAIN_MENU_IGSOBJECT_OFFSET,
                        sizeof(void *)))
                {
                    primary_vtable_matches =
                        *(void **)module ==
                            g_dluma_cmm_runtime.gamedll_base +
                                DLUMA_CMM_MAIN_MENU_VTABLE_RVA;
                    igsobject_vtable_matches =
                        *(void **)((BYTE *)module +
                                   DLUMA_CMM_MAIN_MENU_IGSOBJECT_OFFSET) ==
                            g_dluma_cmm_runtime.gamedll_base +
                                DLUMA_CMM_MAIN_MENU_IGSOBJECT_VTABLE_RVA;
                }
                selection_valid =
                    dluma_custom_main_menu_selected_ready() &&
                    !g_dluma_cmm_runtime.custom_presentation_disabled &&
                    dluma_atomic_compare_exchange(
                        &g_dluma_cmm_runtime.stopping, 0, 0) == 0;
                if (primary_vtable_matches &&
                    igsobject_vtable_matches && selection_valid)
                    prepared_bank_valid =
                        dluma_custom_main_menu_music_resolve_main_cue(
                            module, &cue_name);
            }
            if (dluma_custom_main_menu_runtime_music_route_should_substitute(
                    source_is_music_main,
                    destination_matches,
                    primary_vtable_matches,
                    igsobject_vtable_matches,
                    selection_valid,
                    prepared_bank_valid) &&
                cue_name && cue_name[0])
            {
                replacement.data = (char *)cue_name;
                replacement.size = (unsigned int)strlen(cue_name);
                replacement.capacity = replacement.size;
                if (dluma_cmm_ttl_valid(&replacement))
                {
                    selected_source = &replacement;
                    if (!g_dluma_cmm_runtime.music_assign_substitution_logged)
                    {
                        dluma_log_pair(
                            "INFO",
                            "custom_main_menu_music_substitution",
                            cue_name);
                        g_dluma_cmm_runtime.music_assign_substitution_logged =
                            1U;
                    }
                }
            }
            if (source_is_music_main)
                dluma_custom_main_menu_music_note_normal_cue(
                    selected_source == &replacement && cue_name
                        ? cue_name : "dw_menu");
        }
        __except (DLUMA_CMM_EXCEPTION_EXECUTE_HANDLER)
        {
            selected_source = source;
            if (source_is_music_main)
                dluma_custom_main_menu_music_note_normal_cue("dw_menu");
            dluma_log_pair(
                "WARN", "custom_main_menu_music_substitution",
                "validation_guarded_failure_stock_dw_menu_retained");
        }
        if (original)
            result = original(destination, selected_source);
    }
    __finally
    {
        dluma_atomic_decrement(&g_dluma_cmm_runtime.active_calls);
    }
    return result;
}

static void *dluma_cmm_allocate_relay_near(BYTE *target)
{
    ULONG_PTR maximum = (ULONG_PTR)-1;
    ULONG_PTR anchor;
    ULONGLONG distance;
    if (!target)
        return 0;
    anchor = (ULONG_PTR)target &
             ~((ULONG_PTR)DLUMA_CMM_RELAY_GRANULARITY - 1U);
    for (distance = DLUMA_CMM_RELAY_GRANULARITY;
         distance < DLUMA_CMM_RELAY_MAX_DISTANCE;
         distance += DLUMA_CMM_RELAY_GRANULARITY)
    {
        ULONG_PTR candidates[2] = {0, 0};
        unsigned int index;
        if ((ULONGLONG)anchor <=
            (ULONGLONG)maximum - distance)
            candidates[0] = anchor + (ULONG_PTR)distance;
        if ((ULONGLONG)anchor > distance)
            candidates[1] = anchor - (ULONG_PTR)distance;
        for (index = 0; index < 2U; ++index)
        {
            void *allocation;
            if (!candidates[index])
                continue;
            allocation = VirtualAlloc(
                (void *)candidates[index],
                DLUMA_CMM_RELAY_PAGE_SIZE,
                MEM_COMMIT | MEM_RESERVE,
                PAGE_READWRITE);
            if (allocation)
                return allocation;
        }
    }
    return 0;
}

static int dluma_cmm_prepare_music_assign_relay(
    void *relay, const void *target)
{
    BYTE bytes[14];
    ULONGLONG address = (ULONGLONG)(ULONG_PTR)target;
    DWORD old_protect;
    if (!relay || !target)
        return 0;
    memset(bytes, 0, sizeof(bytes));
    bytes[0] = 0xFFU;
    bytes[1] = 0x25U;
    memcpy(bytes + 6U, &address, sizeof(address));
    memcpy(relay, bytes, sizeof(bytes));
    if (!VirtualProtect(
            relay, DLUMA_CMM_RELAY_PAGE_SIZE,
            PAGE_EXECUTE_READ, &old_protect) ||
        !FlushInstructionCache(
            GetCurrentProcess(), relay, sizeof(bytes)))
        return 0;
    return 1;
}

static int dluma_cmm_atomic_music_block_exchange(
    const BYTE expected[DLUMA_CMM_MUSIC_ASSIGN_BLOCK_SIZE],
    const BYTE desired[DLUMA_CMM_MUSIC_ASSIGN_BLOCK_SIZE],
    DWORD final_protection,
    DWORD *observed_protection_out,
    int *exchanged_out,
    int *protection_restored_out,
    int *cache_flushed_out)
{
    __declspec(align(16)) LONGLONG comparand[2];
    __declspec(align(16)) LONGLONG replacement[2];
    DWORD old_protect = 0;
    DWORD ignored = 0;
    int exchanged;
    int cache_flushed;
    int protection_restored;
    DWORD restore_protection;
    if (observed_protection_out) *observed_protection_out = 0;
    if (exchanged_out) *exchanged_out = 0;
    if (protection_restored_out) *protection_restored_out = 0;
    if (cache_flushed_out) *cache_flushed_out = 0;
    if (!expected || !desired ||
        !g_dluma_cmm_runtime.music_assign_block ||
        ((ULONG_PTR)g_dluma_cmm_runtime.music_assign_block & 0xFU) != 0U ||
        !VirtualProtect(
            g_dluma_cmm_runtime.music_assign_block,
            DLUMA_CMM_MUSIC_ASSIGN_BLOCK_SIZE,
            PAGE_EXECUTE_READWRITE, &old_protect))
        return 0;
    memcpy(comparand, expected, sizeof(comparand));
    memcpy(replacement, desired, sizeof(replacement));
    if (observed_protection_out)
        *observed_protection_out = old_protect;
    restore_protection = final_protection ?
        final_protection : old_protect;
    exchanged = _InterlockedCompareExchange128(
        (volatile LONGLONG *)g_dluma_cmm_runtime.music_assign_block,
        replacement[1], replacement[0], comparand) != 0;
    cache_flushed = !exchanged ||
        FlushInstructionCache(
            GetCurrentProcess(),
            g_dluma_cmm_runtime.music_assign_block,
            DLUMA_CMM_MUSIC_ASSIGN_BLOCK_SIZE) != 0;
    protection_restored = VirtualProtect(
        g_dluma_cmm_runtime.music_assign_block,
        DLUMA_CMM_MUSIC_ASSIGN_BLOCK_SIZE,
        restore_protection, &ignored) != 0;
    if (exchanged_out) *exchanged_out = exchanged;
    if (protection_restored_out)
        *protection_restored_out = protection_restored;
    if (cache_flushed_out) *cache_flushed_out = cache_flushed;
    return exchanged && protection_restored && cache_flushed;
}

static int dluma_cmm_restore_music_assign_hook(void)
{
    int exchanged = 0;
    int protection_restored = 0;
    int cache_flushed = 0;
    if (!g_dluma_cmm_runtime.music_assign_installed)
        return 1;
    if (dluma_cmm_atomic_music_block_exchange(
            g_dluma_cmm_runtime.music_assign_patch,
            g_dluma_cmm_runtime.music_assign_original,
            g_dluma_cmm_runtime.music_assign_original_protect,
            0,
            &exchanged, &protection_restored,
            &cache_flushed))
    {
        g_dluma_cmm_runtime.music_assign_installed = 0U;
        return 1;
    }
    g_dluma_cmm_runtime.teardown_failed = 1U;
    dluma_log_pair(
        "WARN", "custom_main_menu_music_hook_restore",
        !exchanged ? "ownership_lost_relay_retained" :
        (!protection_restored ?
            "protection_restore_failed_relay_retained" :
            "instruction_cache_flush_failed_relay_retained"));
    return 0;
}

static int dluma_cmm_install_music_assign_hook(void)
{
    int displacement = 0;
    int exchanged = 0;
    int protection_restored = 0;
    int cache_flushed = 0;
    DWORD observed_protection = 0;
    DWORD ignored = 0;
    BYTE *block = g_dluma_cmm_runtime.gamedll_base +
                  DLUMA_CMM_MUSIC_ASSIGN_BLOCK_RVA;
    void *relay;
    if (!dluma_cmm_bytes(
            block, g_dluma_cmm_music_assign_block,
            DLUMA_CMM_MUSIC_ASSIGN_BLOCK_SIZE) ||
        !dluma_custom_main_menu_runtime_relative_call_valid(
            g_dluma_cmm_music_assign_block,
            DLUMA_CMM_MUSIC_ASSIGN_BLOCK_SIZE,
            DLUMA_CMM_MUSIC_ASSIGN_CALL_OFFSET,
            DLUMA_CMM_MUSIC_ASSIGN_BLOCK_RVA,
            DLUMA_CMM_MUSIC_ASSIGN_TARGET_RVA) ||
        !dluma_cmm_executable(
            g_dluma_cmm_runtime.gamedll_base +
                DLUMA_CMM_MUSIC_ASSIGN_TARGET_RVA,
            1U))
        return 0;
    relay = dluma_cmm_allocate_relay_near(block);
    if (!relay)
        return 0;
    if (!dluma_custom_main_menu_runtime_relative_call_displacement(
            (ULONGLONG)(ULONG_PTR)(block +
                DLUMA_CMM_MUSIC_ASSIGN_BLOCK_SIZE),
            (ULONGLONG)(ULONG_PTR)relay,
            &displacement) ||
        !dluma_cmm_prepare_music_assign_relay(
            relay, (const void *)dluma_cmm_music_assign_hook))
    {
        VirtualFree(relay, 0, MEM_RELEASE);
        return 0;
    }
    g_dluma_cmm_runtime.music_assign_block = block;
    g_dluma_cmm_runtime.music_assign_relay = relay;
    g_dluma_cmm_runtime.music_assign_original_fn =
        (DlumaCmmStringAssignFn)(
            g_dluma_cmm_runtime.gamedll_base +
            DLUMA_CMM_MUSIC_ASSIGN_TARGET_RVA);
    memcpy(
        g_dluma_cmm_runtime.music_assign_original,
        g_dluma_cmm_music_assign_block,
        DLUMA_CMM_MUSIC_ASSIGN_BLOCK_SIZE);
    memcpy(
        g_dluma_cmm_runtime.music_assign_patch,
        g_dluma_cmm_music_assign_block,
        DLUMA_CMM_MUSIC_ASSIGN_BLOCK_SIZE);
    memcpy(
        g_dluma_cmm_runtime.music_assign_patch +
            DLUMA_CMM_MUSIC_ASSIGN_CALL_OFFSET + 1U,
        &displacement, sizeof(displacement));
    if (dluma_cmm_atomic_music_block_exchange(
            g_dluma_cmm_runtime.music_assign_original,
            g_dluma_cmm_runtime.music_assign_patch,
            0, &observed_protection,
            &exchanged, &protection_restored,
            &cache_flushed))
    {
        g_dluma_cmm_runtime.music_assign_original_protect =
            observed_protection;
        g_dluma_cmm_runtime.music_assign_installed = 1U;
        return 1;
    }
    g_dluma_cmm_runtime.music_assign_original_protect =
        observed_protection;
    if (exchanged)
    {
        g_dluma_cmm_runtime.music_assign_installed = 1U;
        if (!dluma_cmm_restore_music_assign_hook())
            return 0;
    }
    else if (!protection_restored && observed_protection &&
             !VirtualProtect(
                 g_dluma_cmm_runtime.music_assign_block,
                 DLUMA_CMM_MUSIC_ASSIGN_BLOCK_SIZE,
                 observed_protection, &ignored))
    {
        g_dluma_cmm_runtime.teardown_failed = 1U;
        dluma_log_pair(
            "WARN", "custom_main_menu_music_hook_install",
            "protection_restore_failed_relay_retained");
        return 0;
    }
    if (!g_dluma_cmm_runtime.music_assign_installed)
    {
        VirtualFree(
            g_dluma_cmm_runtime.music_assign_relay,
            0, MEM_RELEASE);
        g_dluma_cmm_runtime.music_assign_relay = 0;
        g_dluma_cmm_runtime.music_assign_original_fn = 0;
    }
    return 0;
}

static int dluma_cmm_prepare_temporary_menu_name(
    const DlumaCustomMainMenuManifest *manifest,
    DlumaCmmTtlString **game_name_out,
    DlumaCmmTtlString *saved,
    DlumaCmmTtlString *replacement)
{
    int prepared = 0;
    if (game_name_out)
        *game_name_out = 0;
    if (!manifest || !game_name_out || !saved || !replacement)
        return 0;
    memset(saved, 0, sizeof(*saved));
    memset(replacement, 0, sizeof(*replacement));
    __try
    {
        void **singleton =
            (void **)(g_dluma_cmm_runtime.gamedll_base +
                      DLUMA_CMM_GAME_SINGLETON_RVA);
        void *game;
        DlumaCmmTtlString *game_name;
        if (!dluma_is_readable(singleton, sizeof(void *)))
            __leave;
        game = *singleton;
        if (!game)
            __leave;
        game_name = (DlumaCmmTtlString *)((BYTE *)game +
                                        DLUMA_CMM_GAME_MENU_NAME_OFFSET);
        if (!dluma_cmm_writable(game_name, sizeof(*game_name)))
            __leave;
        *saved = *game_name;
        if (!dluma_cmm_ttl_valid(saved))
            __leave;
        /* The selected menu is published under the stock frontend level name,
         * so the frontend already asks for the right level and this write is
         * an identity.  It is retained while the publication path is proven;
         * once it is, the whole substitution can go. */
        replacement->data = (char *)DLUMA_CUSTOM_MENU_STOCK_LEVEL;
        replacement->size =
            (unsigned int)strlen(DLUMA_CUSTOM_MENU_STOCK_LEVEL);
        replacement->capacity = replacement->size;
        *game_name = *replacement;
        *game_name_out = game_name;
        prepared = 1;
    }
    __except (DLUMA_CMM_EXCEPTION_EXECUTE_HANDLER)
    {
        dluma_log_pair(
            "WARN", "custom_main_menu_background_substitution",
            "temporary_string_setup_guarded_failure");
        prepared = 0;
    }
    return prepared;
}

static void dluma_cmm_restore_temporary_menu_name(
    DlumaCmmTtlString *game_name,
    const DlumaCmmTtlString *saved,
    const DlumaCmmTtlString *replacement)
{
    int restored = 0;
    __try
    {
        if (game_name && saved && replacement &&
            dluma_cmm_writable(game_name, sizeof(*game_name)) &&
            dluma_cmm_same_ttl(game_name, replacement))
        {
            *game_name = *saved;
            restored = 1;
        }
    }
    __except (DLUMA_CMM_EXCEPTION_EXECUTE_HANDLER)
    {
        restored = 0;
    }
    if (!restored)
        dluma_log_pair(
            "WARN", "custom_main_menu_background_substitution",
            "temporary_string_ownership_lost");
}

static int dluma_cmm_vector_valid(const float *value, int direction)
{
    unsigned int index;
    float magnitude = 0.0f;
    if (!value)
        return 0;
    for (index = 0; index < 3U; ++index)
    {
        float component = value[index];
        float absolute = component < 0.0f ? -component : component;
        if (!dluma_custom_main_menu_runtime_float_finite(component) ||
            absolute > 10000000.0f)
            return 0;
        magnitude += absolute;
    }
    return !direction || magnitude > 0.0001f;
}

static int dluma_cmm_publish_music_manager(
    void *module, void *background)
{
    void *manager;
    if (g_dluma_cmm_runtime.music_manager_published ||
        g_dluma_cmm_runtime.main_menu_module_published)
        return g_dluma_cmm_runtime.active_module == module &&
               g_dluma_cmm_runtime.active_background == background &&
               g_dluma_cmm_runtime.published_music_manager != 0 &&
               g_dluma_cmm_runtime.published_main_menu_module == module &&
               g_dluma_cmm_runtime.music_manager_published &&
               g_dluma_cmm_runtime.main_menu_module_published;
    if (!module || !background ||
        !dluma_is_readable(
            (BYTE *)background +
                DLUMA_CMM_BACKGROUND_MUSIC_MANAGER_OFFSET,
            sizeof(void *)))
    {
        dluma_log_pair(
            "WARN", "custom_main_menu_music_manager",
            "background_manager_slot_unreadable");
        return 0;
    }
    manager = *(void **)((BYTE *)background +
                         DLUMA_CMM_BACKGROUND_MUSIC_MANAGER_OFFSET);
    if (!manager)
    {
        dluma_log_pair(
            "WARN", "custom_main_menu_music_manager",
            "background_manager_unavailable");
        return 0;
    }
    if (!dluma_player_media_adapter_publish_frontend_main_menu_module(
            module))
    {
        dluma_log_pair(
            "WARN", "custom_main_menu_music_manager",
            "main_menu_module_validation_rejected");
        return 0;
    }
    if (!dluma_player_media_adapter_publish_frontend_music_manager(
            background, manager))
    {
        (void)dluma_player_media_adapter_clear_frontend_main_menu_module(
            module);
        dluma_log_pair(
            "WARN", "custom_main_menu_music_manager",
            "background_manager_validation_rejected");
        return 0;
    }
    g_dluma_cmm_runtime.published_music_manager = manager;
    g_dluma_cmm_runtime.published_main_menu_module = module;
    g_dluma_cmm_runtime.music_manager_published = 1U;
    g_dluma_cmm_runtime.main_menu_module_published = 1U;
    dluma_log_pair(
        "INFO", "custom_main_menu_music_manager",
        "main_menu_and_background_owners_published");
    return 1;
}

static int dluma_cmm_refresh_main_music_once(void *module)
{
    const char *cue_name = 0;
    DWORD *music_handle;
    ULONGLONG now_ms = GetTickCount64();
    LONG state;
    int resolve_state = DLUMA_CMM_MUSIC_RESOLVE_RETRY;
    int engine_attempted = 0;
    int completed = 0;
    if (!dluma_custom_main_menu_runtime_audio_ready())
        return 0;
    state = dluma_atomic_compare_exchange(
        &g_dluma_cmm_runtime.main_music_refresh_state, 0, 0);
    if (state != 0)
        return state == 2;
    if (now_ms < g_dluma_cmm_runtime.next_main_music_refresh_ms)
        return 0;
    if (!module || !g_dluma_cmm_runtime.music_assign_installed ||
        !g_dluma_cmm_runtime.play_background_music ||
        !g_dluma_cmm_runtime.stop_background_music ||
        !dluma_cmm_main_menu_module_valid(module) ||
        !dluma_custom_main_menu_selected_ready() ||
        g_dluma_cmm_runtime.custom_presentation_disabled ||
        dluma_atomic_compare_exchange(
            &g_dluma_cmm_runtime.stopping, 0, 0) != 0)
        return 0;
    music_handle = (DWORD *)((BYTE *)module +
                             DLUMA_CMM_MAIN_MENU_MUSIC_HANDLE_OFFSET);
    if (!dluma_is_readable(music_handle, sizeof(*music_handle)) ||
        !dluma_cmm_writable(music_handle, sizeof(*music_handle)))
        return 0;
    if (dluma_atomic_compare_exchange(
            &g_dluma_cmm_runtime.main_music_refresh_state, 1, 0) != 0)
        return dluma_atomic_compare_exchange(
                   &g_dluma_cmm_runtime.main_music_refresh_state, 0, 0) == 2;
    __try
    {
        /* Resolve first so the declared bank and cue are ready before the
         * engine-owned stock handle is stopped.  PlayBackgroundMusic then
         * traverses its normal MUSIC_MAIN assignment, where the exact
         * assignment hook substitutes the prepared authored cue. */
        resolve_state = dluma_custom_main_menu_music_resolve_main_cue_state(
            module, &cue_name);
        if (resolve_state != DLUMA_CMM_MUSIC_RESOLVE_READY ||
            !cue_name || !cue_name[0])
            __leave;
        engine_attempted = 1;
        g_dluma_cmm_runtime.stop_background_music(module);
        if (*music_handle != 0U)
            __leave;
        g_dluma_cmm_runtime.play_background_music(module);
        if (*music_handle == 0U)
            __leave;
        completed = 1;
    }
    __except (DLUMA_CMM_EXCEPTION_EXECUTE_HANDLER)
    {
        completed = 0;
    }
    if (!engine_attempted)
    {
        if (resolve_state == DLUMA_CMM_MUSIC_RESOLVE_PERMANENT)
        {
            dluma_atomic_exchange(
                &g_dluma_cmm_runtime.main_music_refresh_state, 3);
            dluma_log_pair(
                "WARN", "custom_main_menu_main_music_refresh",
                "declared_cue_permanently_unavailable_custom_visual_retained");
        }
        else
        {
            g_dluma_cmm_runtime.next_main_music_refresh_ms =
                now_ms + DLUMA_CMM_MAIN_MUSIC_RETRY_MS;
            dluma_atomic_exchange(
                &g_dluma_cmm_runtime.main_music_refresh_state, 0);
            dluma_log_pair(
                "INFO", "custom_main_menu_main_music_refresh",
                "bank_or_audio_owner_not_ready_retrying");
        }
        return 0;
    }
    if (!completed)
        dluma_custom_main_menu_music_retry_preparation();
    g_dluma_cmm_runtime.next_main_music_refresh_ms = completed
        ? 0 : now_ms + DLUMA_CMM_MAIN_MUSIC_RETRY_MS;
    dluma_atomic_exchange(
        &g_dluma_cmm_runtime.main_music_refresh_state,
        completed ? 2 : 0);
    dluma_log_pair(
        completed ? "INFO" : "WARN",
        "custom_main_menu_main_music_refresh",
        completed ? "engine_stop_play_complete" :
                    "engine_stop_play_not_ready_retrying");
    return completed;
}

static int dluma_cmm_clear_music_manager(void)
{
    if (!g_dluma_cmm_runtime.music_manager_published &&
        !g_dluma_cmm_runtime.main_menu_module_published)
    {
        g_dluma_cmm_runtime.published_music_manager = 0;
        g_dluma_cmm_runtime.published_main_menu_module = 0;
        return 1;
    }
    if (!g_dluma_cmm_runtime.active_background ||
        !g_dluma_cmm_runtime.active_module ||
        !g_dluma_cmm_runtime.published_music_manager ||
        g_dluma_cmm_runtime.published_main_menu_module !=
            g_dluma_cmm_runtime.active_module ||
        !g_dluma_cmm_runtime.music_manager_published ||
        !g_dluma_cmm_runtime.main_menu_module_published ||
        !dluma_player_media_adapter_clear_frontend_music_manager(
            g_dluma_cmm_runtime.active_background,
            g_dluma_cmm_runtime.published_music_manager))
        return 0;
    if (!dluma_player_media_adapter_clear_frontend_main_menu_module(
            g_dluma_cmm_runtime.active_module))
    {
        if (!dluma_player_media_adapter_publish_frontend_music_manager(
                g_dluma_cmm_runtime.active_background,
                g_dluma_cmm_runtime.published_music_manager))
            dluma_log_pair(
                "ERROR", "custom_main_menu_music_manager",
                "clear_rollback_failed_dependencies_retained");
        return 0;
    }
    g_dluma_cmm_runtime.music_manager_published = 0U;
    g_dluma_cmm_runtime.main_menu_module_published = 0U;
    g_dluma_cmm_runtime.published_music_manager = 0;
    g_dluma_cmm_runtime.published_main_menu_module = 0;
    return 1;
}

static void dluma_cmm_apply_camera(void *module)
{
    const DlumaCustomMainMenuManifest *manifest =
        dluma_custom_main_menu_selected_manifest();
    void *background = 0;
    void *menu_background = 0;
    void *camera_object = 0;
    void *position_helper = 0;
    void *scene_helper = 0;
    void *control_object = 0;
    void *private_camera = 0;
    void *level_view = 0;
    void *view_camera = 0;
    void *target_camera = 0;
    const char *camera_target = "background_private_camera";
    const float *matrix = 0;
    float position[3] = {0.0f, 0.0f, 0.0f};
    float up[3] = {0.0f, 0.0f, 0.0f};
    float forward[3] = {0.0f, 0.0f, 0.0f};
    float fov = 0.0f;
    int camera_id = 0;
    int movie_camera_owned = 0;
    void *camera_level = 0;
    const char *failure = "initial_contract_unavailable";
    if (!manifest || !module ||
        !dluma_is_readable((BYTE *)module +
                             DLUMA_CMM_MODULE_BACKGROUND_OFFSET,
                         sizeof(void *)))
        goto failed;
    background = *(void **)((BYTE *)module +
                            DLUMA_CMM_MODULE_BACKGROUND_OFFSET);
    if (!background)
        goto failed;
    __try
    {
        menu_background = g_dluma_cmm_runtime.dynamic_cast(
            background, 0,
            g_dluma_cmm_runtime.gamedll_base + DLUMA_CMM_LEVEL_TYPE_RVA,
            g_dluma_cmm_runtime.gamedll_base +
                DLUMA_CMM_MENU_BACKGROUND_TYPE_RVA,
            0);
        if (!menu_background)
            goto failed;
        dluma_cmm_enable_fx(menu_background, background);
        dluma_cmm_start_fx_emitters_after_activation(
            menu_background, background);
        dluma_log_pair(
            "INFO", "custom_main_menu_background_activation",
            "rtti_validated_stock_activation_complete_fx_gate_and_emitters_started_before_camera");
        if (!dluma_custom_main_menu_runtime_static_camera_bridge_enabled(
                manifest->does_use_3d_camera_movement))
        {
            movie_camera_owned = 1;
            __leave;
        }
        camera_object = g_dluma_cmm_runtime.get_object_by_id(
            background, manifest->camera_id);
        if (!camera_object)
            goto failed;
        camera_id = g_dluma_cmm_runtime.get_id(camera_object);
        camera_level = g_dluma_cmm_runtime.get_level(camera_object);
        position_helper = g_dluma_cmm_runtime.dynamic_cast(
            camera_object, 0,
            g_dluma_cmm_runtime.gamedll_base +
                DLUMA_CMM_IGS_OBJECT_TYPE_RVA,
            g_dluma_cmm_runtime.gamedll_base +
                DLUMA_CMM_CAMERA_POSITION_HELPER_TYPE_RVA,
            0);
        scene_helper = g_dluma_cmm_runtime.dynamic_cast(
            camera_object, 0,
            g_dluma_cmm_runtime.gamedll_base +
                DLUMA_CMM_IGS_OBJECT_TYPE_RVA,
            g_dluma_cmm_runtime.gamedll_base +
                DLUMA_CMM_MENU_CAMERA_HELPER_TYPE_RVA,
            0);
        control_object = g_dluma_cmm_runtime.dynamic_cast(
            camera_object, 0,
            g_dluma_cmm_runtime.gamedll_base +
                DLUMA_CMM_IGS_OBJECT_TYPE_RVA,
            g_dluma_cmm_runtime.gamedll_base +
                DLUMA_CMM_CONTROL_OBJECT_TYPE_RVA,
            0);
        if ((!position_helper && !scene_helper) || !control_object)
            goto failed;
        if (!dluma_is_readable(
                (BYTE *)menu_background +
                    DLUMA_CMM_BACKGROUND_VIEW_OFFSET,
                sizeof(void *)))
        {
            failure = "background_view_slot_unreadable";
            goto failed;
        }
        if (!dluma_is_readable(
                (BYTE *)menu_background +
                    DLUMA_CMM_BACKGROUND_CAMERA_OFFSET,
                sizeof(void *)))
        {
            failure = "background_private_camera_slot_unreadable";
            goto failed;
        }
        level_view = *(void **)((BYTE *)menu_background +
                                DLUMA_CMM_BACKGROUND_VIEW_OFFSET);
        private_camera = *(void **)((BYTE *)menu_background +
                                    DLUMA_CMM_BACKGROUND_CAMERA_OFFSET);
        if (!private_camera ||
            !dluma_is_readable(private_camera, sizeof(void *)))
        {
            failure = "background_private_camera_unavailable";
            goto failed;
        }
        if (!level_view ||
            !dluma_is_readable(level_view, sizeof(void *)))
        {
            failure = "background_view_unavailable";
            goto failed;
        }
        g_dluma_cmm_runtime.activate_view(menu_background, level_view);
        dluma_log_pair(
            "INFO", "custom_main_menu_background_view",
            "activation_reinforced");
        view_camera = g_dluma_cmm_runtime.get_view_camera(
            menu_background, level_view);
        if (view_camera &&
            dluma_is_readable(view_camera, sizeof(void *)))
        {
            target_camera = view_camera;
            camera_target = "background_view_camera";
        }
        if (!target_camera)
            target_camera = private_camera;
        if (scene_helper)
        {
            if (!dluma_is_readable(
                    (BYTE *)scene_helper + DLUMA_CMM_HELPER_FOV_OFFSET,
                    sizeof(float)))
                goto failed;
            fov = *(float *)((BYTE *)scene_helper +
                            DLUMA_CMM_HELPER_FOV_OFFSET);
        }
        else
        {
            fov = g_dluma_cmm_runtime.get_fov(private_camera);
        }
        matrix = g_dluma_cmm_runtime.get_world_xform(control_object);
        if (!g_dluma_cmm_runtime.get_world_position(
                control_object, position))
        {
            failure = "helper_world_position_query_failed";
            goto failed;
        }
        if (!g_dluma_cmm_runtime.get_up_vector(control_object, up))
        {
            failure = "helper_up_vector_query_failed";
            goto failed;
        }
        if (!g_dluma_cmm_runtime.get_forward_vector(
                control_object, forward))
        {
            failure = "helper_forward_vector_query_failed";
            goto failed;
        }
        if (!dluma_custom_main_menu_runtime_editor_camera_forward(
                forward, forward))
        {
            failure = "scene_helper_forward_conversion_failed";
            goto failed;
        }
        if (!matrix || !dluma_is_readable(matrix, 12U * sizeof(float)) ||
            !dluma_cmm_vector_valid(position, 0) ||
            !dluma_cmm_vector_valid(up, 1) ||
            !dluma_cmm_vector_valid(forward, 1))
        {
            failure = "helper_transform_values_invalid";
            goto failed;
        }
        if (!dluma_custom_main_menu_runtime_camera_values_valid(
                menu_background && (position_helper || scene_helper) &&
                    control_object,
                manifest->camera_id, camera_id,
                dluma_cmm_object_name_is(
                    camera_object, manifest->camera_name),
                camera_level == background,
                target_camera != 0,
                matrix, 12U, fov))
        {
            failure = "camera_identity_or_fov_validation_failed";
            goto failed;
        }
        /* Menu3DBackgroundDI::OnFrame uses the active CLevelView camera when
         * one exists and otherwise deliberately falls back to its private
         * +0x9C8 camera.  Mirror that exact choice.  A view camera is copied
         * into the private camera by the engine; a background with no active
         * view is driven directly by the private camera. */
        g_dluma_cmm_runtime.from_forward_up_pos(
            target_camera, forward, up, position);
        g_dluma_cmm_runtime.set_fov(target_camera, fov);
        g_dluma_cmm_runtime.force_frame(target_camera);
    }
    __except (DLUMA_CMM_EXCEPTION_EXECUTE_HANDLER)
    {
        goto failed;
    }
    g_dluma_cmm_runtime.active_module = module;
    g_dluma_cmm_runtime.active_background = background;
    g_dluma_cmm_runtime.stable_since_ms = GetTickCount64();
    g_dluma_cmm_runtime.camera_applied = 1U;
    if (!dluma_cmm_publish_music_manager(module, background))
        dluma_log_pair(
            "WARN", "custom_main_menu_music_manager",
            "background_manager_not_ready_retrying");
    else
        (void)dluma_cmm_refresh_main_music_once(module);
    dluma_log_pair("INFO", "custom_main_menu_background_rtti",
                 "Menu3DBackgroundDI");
    if (movie_camera_owned)
    {
        dluma_log_pair(
            "INFO", "custom_main_menu_camera_contract",
            "authored_movie_actions_static_bridge_suppressed");
        dluma_log_pair(
            "INFO", "custom_main_menu_camera_application",
            "engine_owned");
        return;
    }
    dluma_log_pair("INFO", "custom_main_menu_camera_name",
                 manifest->camera_name);
    dluma_log_u64("INFO", "custom_main_menu_camera_id",
                (ULONGLONG)(unsigned int)manifest->camera_id);
    dluma_log_pair(
        "INFO", "custom_main_menu_camera_contract",
        scene_helper ? "CameraHelperMenuScene3dDI_background_camera_bridge" :
                       "CameraPostionHelper_background_camera_bridge");
    dluma_log_pair(
        "INFO", "custom_main_menu_camera_orientation",
        "editor_helper_negative_world_z");
    dluma_log_pair(
        "INFO", "custom_main_menu_camera_target",
        camera_target);
    dluma_log_pair("INFO", "custom_main_menu_camera_application", "accepted");
    return;

failed:
    g_dluma_cmm_runtime.active_module = module;
    g_dluma_cmm_runtime.active_background = background;
    g_dluma_cmm_runtime.stable_since_ms = 0;
    g_dluma_cmm_runtime.camera_applied = 0;
    if (background && dluma_cmm_publish_music_manager(module, background))
        (void)dluma_cmm_refresh_main_music_once(module);
    dluma_log_pair("WARN", "custom_main_menu_camera_application",
                 failure);
}

static void dluma_cmm_observe_background_after_load(void *module)
{
    void *background = 0;
    void *menu_background = 0;
    g_dluma_cmm_runtime.loaded_module = module;
    if (!module ||
        !dluma_is_readable(
            (BYTE *)module + DLUMA_CMM_MODULE_BACKGROUND_OFFSET,
            sizeof(void *)))
    {
        dluma_log_pair(
            "WARN", "custom_main_menu_background_load",
            "module_background_slot_unreadable");
        return;
    }
    background = *(void **)((BYTE *)module +
                            DLUMA_CMM_MODULE_BACKGROUND_OFFSET);
    if (!background)
    {
        dluma_log_pair(
            "WARN", "custom_main_menu_background_load",
            "load_module_returned_no_level");
        return;
    }
    __try
    {
        menu_background = g_dluma_cmm_runtime.dynamic_cast(
            background, 0,
            g_dluma_cmm_runtime.gamedll_base + DLUMA_CMM_LEVEL_TYPE_RVA,
            g_dluma_cmm_runtime.gamedll_base +
                DLUMA_CMM_MENU_BACKGROUND_TYPE_RVA,
            0);
    }
    __except (DLUMA_CMM_EXCEPTION_EXECUTE_HANDLER)
    {
        menu_background = 0;
    }
    dluma_log_pair(
        menu_background ? "INFO" : "WARN",
        "custom_main_menu_background_load",
        menu_background ? "level_present_rtti_validated" :
                          "level_present_wrong_rtti");
}

static __int64 __fastcall dluma_cmm_screen_shown_hook(
    void *screen_manager, void *screen)
{
    DlumaCmmScreenEventFn original =
        g_dluma_cmm_runtime.screen_shown_original;
    __int64 result = 0;
    int screen_id = 0;
    char script[DLUMA_CUSTOM_MENU_XUI_NAME_CAPACITY];
    script[0] = 0;
    dluma_atomic_increment(&g_dluma_cmm_runtime.active_calls);
    __try
    {
        if (original)
            result = original(screen_manager, screen);
        if (dluma_atomic_compare_exchange(
                &g_dluma_cmm_runtime.stopping, 0, 0) == 0 &&
            dluma_cmm_screen_snapshot(
                screen, &screen_id, script, DLUMA_ARRAY_COUNT(script)))
        {
            dluma_atomic_increment(&g_dluma_cmm_runtime.screen_shown_events);
            if (!dluma_custom_main_menu_runtime_screen_script_is_cursor(script))
                dluma_cmm_visible_screen_shown(screen, screen_id, script);
        }
    }
    __finally
    {
        dluma_atomic_decrement(&g_dluma_cmm_runtime.active_calls);
    }
    return result;
}

static __int64 __fastcall dluma_cmm_screen_hidden_hook(
    void *screen_manager, void *screen)
{
    DlumaCmmScreenEventFn original =
        g_dluma_cmm_runtime.screen_hidden_original;
    __int64 result = 0;
    int screen_id = 0;
    char script[DLUMA_CUSTOM_MENU_XUI_NAME_CAPACITY];
    script[0] = 0;
    dluma_atomic_increment(&g_dluma_cmm_runtime.active_calls);
    __try
    {
        (void)dluma_cmm_screen_snapshot(
            screen, &screen_id, script, DLUMA_ARRAY_COUNT(script));
        if (original)
            result = original(screen_manager, screen);
        dluma_atomic_increment(&g_dluma_cmm_runtime.screen_hidden_events);
        dluma_cmm_visible_screen_hidden(screen);
    }
    __finally
    {
        dluma_atomic_decrement(&g_dluma_cmm_runtime.active_calls);
    }
    return result;
}

static int dluma_cmm_run_stock_premount(void);
static int dluma_cmm_validate_stock_publication(void);
static int dluma_cmm_mount_overlay_after_catalogs(void);


static void dluma_cmm_disable_custom_presentation(const char *reason)
{
    ULONGLONG generation;
    dluma_cmm_lock(&g_dluma_cmm_runtime.generation_lock);
    generation = g_dluma_cmm_runtime.frontend_generation;
    g_dluma_cmm_runtime.frontend_generation = 0;
    g_dluma_cmm_runtime.generation_active = 0U;
    g_dluma_cmm_runtime.restoration_module = 0;
    g_dluma_cmm_runtime.restoration_ui_level = 0;
    g_dluma_cmm_runtime.restoration_background_required = 0U;
    dluma_atomic_exchange(&g_dluma_cmm_runtime.restoration_pending, 0);
    dluma_atomic_exchange(&g_dluma_cmm_runtime.frontend_return_ready, 0);
    dluma_atomic_exchange(&g_dluma_cmm_runtime.frontend_committed, 0);
    dluma_atomic_exchange(&g_dluma_cmm_runtime.return_audio_ready, 0);
    dluma_atomic_exchange(
        &g_dluma_cmm_runtime.restoration_ui_requested, 0);
    dluma_atomic_exchange(
        &g_dluma_cmm_runtime.restoration_load_requested, 0);
    dluma_atomic_exchange(
        &g_dluma_cmm_runtime.restoration_activate_requested, 0);
    g_dluma_cmm_runtime.source_retirement_generation = 0;
    g_dluma_cmm_runtime.source_retirement_publication_state = 0;
    dluma_atomic_exchange(
        &g_dluma_cmm_runtime.source_retirement_pending, 0);
    dluma_atomic_exchange(
        &g_dluma_cmm_runtime.restoration_processing, 0);
    dluma_cmm_unlock(&g_dluma_cmm_runtime.generation_lock);
    if (generation)
        dluma_custom_main_menu_invalidate_frontend_generation(generation);
    g_dluma_cmm_runtime.content_sources_mounted = 0U;
    g_dluma_cmm_runtime.content_engine_sources_observed = 0U;
    (void)dluma_custom_main_menu_music_retire_generation();
    dluma_custom_main_menu_retire_generation();
    g_dluma_cmm_runtime.custom_presentation_disabled = 1U;
    (void)dluma_cmm_content_fail(reason);
    dluma_ui_restore_main_menu_redirect();
    dluma_log_pair(
        "WARN", "custom_main_menu_stock_fallback_reason",
        reason ? reason : "unknown_publication_failure");
}

static void dluma_cmm_retire_frontend_generation_locked(
    const char *reason, int gameplay_started,
    ULONGLONG expected_generation)
{
    ULONGLONG generation = g_dluma_cmm_runtime.frontend_generation;
    void *restoration_candidate = g_dluma_cmm_runtime.restoration_module;
    void *restoration_ui_candidate = g_dluma_cmm_runtime.restoration_ui_level;
    int retain_background_owner = 0;
    int music_released;
    if (expected_generation && generation != expected_generation)
    {
        dluma_log_pair(
            "INFO", "custom_main_menu_generation_retirement",
            "stale_generation_ignored");
        return;
    }
    if (!generation && !g_dluma_cmm_runtime.generation_active &&
        !g_dluma_cmm_runtime.content_sources_mounted)
    {
        if (gameplay_started)
        {
            dluma_atomic_exchange(&g_dluma_cmm_runtime.return_audio_ready, 0);
            g_dluma_cmm_runtime.gameplay_generation_retired = 1U;
            dluma_atomic_exchange(&g_dluma_cmm_runtime.restoration_pending, 1);
            dluma_atomic_exchange(&g_dluma_cmm_runtime.frontend_return_ready, 0);
        }
        dluma_atomic_exchange(
            &g_dluma_cmm_runtime.source_retirement_pending, 0);
        g_dluma_cmm_runtime.source_retirement_generation = 0;
        g_dluma_cmm_runtime.source_retirement_publication_state = 0;
        return;
    }

    if (generation)
        dluma_custom_main_menu_invalidate_frontend_generation(generation);
    music_released = dluma_custom_main_menu_music_retire_generation();
    dluma_custom_main_menu_retire_generation();
    dluma_cmm_visible_screens_clear();

    g_dluma_cmm_runtime.frontend_generation = 0;
    g_dluma_cmm_runtime.generation_active = 0U;
    dluma_atomic_exchange(&g_dluma_cmm_runtime.restoration_pending, 1);
    dluma_atomic_exchange(&g_dluma_cmm_runtime.return_audio_ready, 0);
    if (gameplay_started)
        dluma_atomic_exchange(&g_dluma_cmm_runtime.frontend_return_ready, 0);
    g_dluma_cmm_runtime.gameplay_generation_retired =
        (unsigned char)(
            g_dluma_cmm_runtime.gameplay_generation_retired ||
            gameplay_started != 0);
    if (!restoration_candidate)
        restoration_candidate = g_dluma_cmm_runtime.publication_module;
    if (!restoration_candidate)
        restoration_candidate = g_dluma_cmm_runtime.active_module;
    if (!restoration_candidate)
        restoration_candidate = g_dluma_cmm_runtime.loaded_module;
    if (!restoration_ui_candidate)
        restoration_ui_candidate = g_dluma_cmm_runtime.texture_level;
    /* This vtable slot unloads the background owned by MainMenuModuleDI; it
     * does not destroy the module or the UI controller.  Preserve the exact
     * validated owner so a persistent frontend can receive a fresh background
     * even when none of its UI/load/activate hooks run again on return. */
    retain_background_owner = restoration_candidate &&
        (gameplay_started ||
         g_dluma_cmm_runtime.restoration_background_required) &&
        dluma_cmm_main_menu_module_valid(restoration_candidate);
    g_dluma_cmm_runtime.restoration_module =
        retain_background_owner ? restoration_candidate : 0;
    g_dluma_cmm_runtime.restoration_background_required =
        (unsigned char)(retain_background_owner != 0);
    /* Retain only a receiver actually observed in the UI callback and equal
     * to the still-valid module owner.  Other level pointers expire. */
    g_dluma_cmm_runtime.restoration_ui_level =
        retain_background_owner &&
        restoration_ui_candidate == restoration_candidate
            ? restoration_ui_candidate : 0;
    dluma_atomic_exchange(
        &g_dluma_cmm_runtime.restoration_ui_requested, 0);
    dluma_atomic_exchange(
        &g_dluma_cmm_runtime.restoration_load_requested, 0);
    dluma_atomic_exchange(
        &g_dluma_cmm_runtime.restoration_activate_requested, 0);
    g_dluma_cmm_runtime.restoration_wait_logged = 0U;
    g_dluma_cmm_runtime.publication_module = 0;
    g_dluma_cmm_runtime.loaded_module = 0;
    g_dluma_cmm_runtime.texture_level = 0;
    g_dluma_cmm_runtime.active_module = 0;
    g_dluma_cmm_runtime.active_background = 0;
    g_dluma_cmm_runtime.published_music_manager = 0;
    g_dluma_cmm_runtime.published_main_menu_module = 0;
    g_dluma_cmm_runtime.stable_since_ms = 0;
    g_dluma_cmm_runtime.next_camera_retry_ms = 0;
    g_dluma_cmm_runtime.next_music_manager_retry_ms = 0;
    g_dluma_cmm_runtime.next_main_music_refresh_ms = 0;
    dluma_atomic_exchange(
        &g_dluma_cmm_runtime.main_music_refresh_state, 0);
    g_dluma_cmm_runtime.camera_applied = 0U;
    g_dluma_cmm_runtime.texture_definitions_loaded = 0U;
    g_dluma_cmm_runtime.music_manager_published = 0U;
    g_dluma_cmm_runtime.main_menu_module_published = 0U;
    g_dluma_cmm_runtime.receipt_good = 0U;
    dluma_cmm_clear_started_fx_emitters();
    g_dluma_cmm_runtime.fx_level = 0;
    g_dluma_cmm_runtime.fx_background_owner = 0;
    g_dluma_cmm_runtime.fx_state_captured = 0U;
    g_dluma_cmm_runtime.fx_changed_by_runtime = 0U;
    g_dluma_cmm_runtime.content_publish_called = 0U;
    g_dluma_cmm_runtime.content_terminal_failure = 0U;
    g_dluma_cmm_runtime.content_failure_logged = 0U;
    g_dluma_cmm_runtime.content_activated = 0U;
    g_dluma_cmm_runtime.content_sources_mounted = 0U;
    g_dluma_cmm_runtime.content_engine_sources_observed = 0U;
    g_dluma_cmm_runtime.custom_load_complete = 0U;
    g_dluma_cmm_runtime.custom_background_substituted = 0U;
    g_dluma_cmm_runtime.custom_presentation_disabled = 0U;
    g_dluma_cmm_runtime.content_failure_phase = 0;
    g_dluma_cmm_runtime.content_exception_address = 0;
    g_dluma_cmm_runtime.content_exception_code = 0;
    g_dluma_cmm_runtime.content_prepare_attempt_count = 0;
    g_dluma_cmm_runtime.texture_fingerprint[0] = 0;
    dluma_atomic_exchange(
        &g_dluma_cmm_runtime.publication_processing, 0);
    dluma_atomic_exchange(
        &g_dluma_cmm_runtime.restoration_processing, 0);
    dluma_atomic_exchange(
        &g_dluma_cmm_runtime.publication_state,
        DLUMA_CMM_PUBLICATION_AWAITING_UI_INITIALIZER);
    dluma_atomic_exchange(
        &g_dluma_cmm_runtime.source_retirement_pending, 0);
    g_dluma_cmm_runtime.source_retirement_generation = 0;
    g_dluma_cmm_runtime.source_retirement_publication_state = 0;
    if (retain_background_owner)
        dluma_log_pair(
            "INFO", "custom_main_menu_frontend_restoration",
            "retained_exact_background_owner_for_frontend_return");
    dluma_log_u64(
        "INFO", "custom_main_menu_generation_retired", generation);
    dluma_log_pair(
        music_released ? "INFO" : "WARN",
        "custom_main_menu_generation_retirement_reason",
        reason && reason[0] ? reason : "unknown");
}

static void dluma_cmm_retire_frontend_generation(
    const char *reason, int gameplay_started)
{
    ULONGLONG generation;
    dluma_cmm_lock(&g_dluma_cmm_runtime.generation_lock);
    generation = g_dluma_cmm_runtime.frontend_generation;
    dluma_cmm_retire_frontend_generation_locked(
        reason, gameplay_started, generation);
    dluma_cmm_unlock(&g_dluma_cmm_runtime.generation_lock);
}

static LONG dluma_cmm_consume_source_retirement(int gameplay_started)
{
    LONG reason;
    LONG retirement_publication_state;
    ULONGLONG retirement_generation;
    ULONGLONG active_generation;
    dluma_cmm_lock(&g_dluma_cmm_runtime.generation_lock);
    reason = dluma_atomic_exchange(
        &g_dluma_cmm_runtime.source_retirement_pending, 0);
    retirement_generation =
        g_dluma_cmm_runtime.source_retirement_generation;
    g_dluma_cmm_runtime.source_retirement_generation = 0;
    retirement_publication_state =
        g_dluma_cmm_runtime.source_retirement_publication_state;
    g_dluma_cmm_runtime.source_retirement_publication_state = 0;
    active_generation = g_dluma_cmm_runtime.frontend_generation;
    if (reason && retirement_generation &&
        retirement_generation == active_generation)
    {
        /* Stock custom-content publication removes its temporary project/data
         * scan sources after their packages and catalogs have been published.
         * That native removal is real registry state, but it is not a frontend
         * lifetime transition.  Treat it as advisory until a positive owning
         * transition is observed: gameplay retirement is handled below and
         * the exact MainMenuModuleDI unload queues reason 2. */
        if (reason == 1 && !gameplay_started &&
            (retirement_publication_state ==
                 DLUMA_CMM_PUBLICATION_READY_BEFORE_UI ||
             retirement_publication_state ==
                 DLUMA_CMM_PUBLICATION_LOADING_BACKGROUND))
        {
            dluma_log_pair(
                "INFO", "custom_main_menu_generation_retirement",
                "transient_publication_cleanup_ignored");
            reason = 0;
        }
        else
            dluma_cmm_retire_frontend_generation_locked(
                reason == 1 ? "native_source_removed_after_publication" :
                reason == 2 ? "background_unloaded" :
                "source_generation_retired",
                gameplay_started, retirement_generation);
    }
    else if (reason)
    {
        dluma_log_pair(
            "INFO", "custom_main_menu_generation_retirement",
            "stale_notification_ignored");
        reason = 0;
    }
    dluma_cmm_unlock(&g_dluma_cmm_runtime.generation_lock);
    return reason;
}

void dluma_custom_main_menu_runtime_note_source_retired(
    unsigned int reason, ULONGLONG frontend_generation)
{
    LONG state;
    dluma_cmm_lock(&g_dluma_cmm_runtime.generation_lock);
    state = dluma_atomic_compare_exchange(
        &g_dluma_cmm_runtime.publication_state, 0, 0);
    if (g_dluma_cmm_runtime.started &&
        dluma_atomic_compare_exchange(
            &g_dluma_cmm_runtime.stopping, 0, 0) == 0 &&
        g_dluma_cmm_runtime.generation_active &&
        frontend_generation &&
        frontend_generation == g_dluma_cmm_runtime.frontend_generation &&
        state >= DLUMA_CMM_PUBLICATION_READY_BEFORE_UI &&
        state != DLUMA_CMM_PUBLICATION_STOCK_FALLBACK)
    {
        g_dluma_cmm_runtime.source_retirement_generation =
            frontend_generation;
        g_dluma_cmm_runtime.source_retirement_publication_state = state;
        dluma_atomic_exchange(
            &g_dluma_cmm_runtime.source_retirement_pending,
            reason ? (LONG)reason : 1);
    }
    else if (frontend_generation)
        dluma_log_pair(
            "INFO", "custom_main_menu_generation_retirement",
            "stale_notification_ignored");
    dluma_cmm_unlock(&g_dluma_cmm_runtime.generation_lock);
}

int dluma_custom_main_menu_runtime_audio_ready(void)
{
    return dluma_atomic_compare_exchange(
               &g_dluma_cmm_runtime.restoration_pending, 0, 0) == 0 ||
           dluma_atomic_compare_exchange(
               &g_dluma_cmm_runtime.return_audio_ready, 0, 0) != 0;
}

static int dluma_cmm_frontend_restore_allowed(void)
{
    if (!g_dluma.config.custom_map_asset_isolation_enabled)
        return 1;
    return dluma_custom_map_content_frontend_restore_allowed();
}

void dluma_custom_main_menu_runtime_frontend_ready(void)
{
    if (!g_dluma_cmm_runtime.started ||
        !dluma_custom_main_menu_selected_ready() ||
        g_dluma_cmm_runtime.custom_presentation_disabled ||
        dluma_atomic_compare_exchange(
            &g_dluma_cmm_runtime.stopping, 0, 0) != 0)
        return;
    if (!g_dluma_cmm_runtime.gameplay_generation_retired &&
        dluma_atomic_compare_exchange(
            &g_dluma_cmm_runtime.restoration_pending, 0, 0) == 0 &&
        dluma_atomic_compare_exchange(
            &g_dluma_cmm_runtime.source_retirement_pending, 0, 0) == 0)
        return;
    if (dluma_atomic_exchange(
            &g_dluma_cmm_runtime.frontend_return_ready, 1) == 0)
        dluma_log_pair(
            "INFO", "custom_main_menu_frontend_restoration",
            "frontend_owner_return_requested");
}

static int dluma_cmm_restoration_request_pending(LONG state)
{
    if (state == DLUMA_CMM_PUBLICATION_STOCK_FALLBACK)
        return 0;
    /* Source publication and scene restoration have separate lifetimes.
     * Coalesce callbacks until the serialized owner has loaded the scene. */
    if (dluma_atomic_compare_exchange(
            &g_dluma_cmm_runtime.restoration_processing, 0, 0) != 0)
        return 1;
    if (state >= DLUMA_CMM_PUBLICATION_ACTIVE)
        return 0;
    return dluma_atomic_compare_exchange(
               &g_dluma_cmm_runtime.restoration_pending, 0, 0) != 0 ||
           dluma_atomic_compare_exchange(
               &g_dluma_cmm_runtime.restoration_ui_requested, 0, 0) != 0 ||
           dluma_atomic_compare_exchange(
               &g_dluma_cmm_runtime.restoration_load_requested, 0, 0) != 0 ||
           dluma_atomic_compare_exchange(
               &g_dluma_cmm_runtime.restoration_activate_requested, 0, 0) != 0;
}

static int dluma_cmm_capture_restoration_module(void *module)
{
    if (!dluma_cmm_main_menu_module_valid(module))
        return 0;
    dluma_cmm_lock(&g_dluma_cmm_runtime.generation_lock);
    if (g_dluma_cmm_runtime.restoration_module != module)
        g_dluma_cmm_runtime.restoration_ui_level = 0;
    g_dluma_cmm_runtime.restoration_module = module;
    dluma_cmm_unlock(&g_dluma_cmm_runtime.generation_lock);
    return 1;
}

/* Both the first UI initializer and a gameplay-to-frontend restoration use
 * the same exact-build stock publication transaction.  The caller owns
 * publication_processing, and no UI/background operation may be replayed
 * until this helper reaches READY_BEFORE_UI. */
static const char *dluma_cmm_publish_frontend_generation(
    void *module, const char *origin)
{
    const char *failure = 0;
    LONG state = dluma_atomic_compare_exchange(
        &g_dluma_cmm_runtime.publication_state, 0, 0);
    if (state != DLUMA_CMM_PUBLICATION_AWAITING_UI_INITIALIZER)
        return "frontend_publication_state_not_awaiting";
    if (dluma_atomic_compare_exchange(
            &g_dluma_cmm_runtime.restoration_pending, 0, 0) != 0)
    {
        if (!g_dluma.config.custom_map_asset_isolation_enabled &&
            dluma_asset_mod_runtime_pending() &&
            !dluma_asset_mod_runtime_apply(1))
            dluma_log_pair("WARN", "asset_mod_stack_change",
                         "menu_return_kept_previous_asset_generation");
        if (!dluma_custom_main_menu_refresh_asset_sources())
            return "frontend_asset_staging_refresh_failed";
        if (!dluma_cmm_prepare_content_descriptor())
            return "frontend_asset_content_descriptor_invalid";
    }
    if (!dluma_cmm_capture_restoration_module(module))
        return "frontend_publication_module_contract_invalid";
    g_dluma_cmm_runtime.publication_module = module;
    dluma_cmm_lock(&g_dluma_cmm_runtime.generation_lock);
    if (!g_dluma_cmm_runtime.frontend_generation)
        g_dluma_cmm_runtime.frontend_generation =
            dluma_custom_main_menu_begin_frontend_generation();
    if (!g_dluma_cmm_runtime.frontend_generation)
        failure = "frontend_generation_begin_failed";
    dluma_cmm_unlock(&g_dluma_cmm_runtime.generation_lock);
    if (!failure &&
        (!dluma_cmm_publication_transition(
             DLUMA_CMM_PUBLICATION_EVENT_ENTER_UI_INITIALIZER) ||
         !dluma_cmm_run_stock_premount() ||
         !dluma_cmm_publication_transition(
             DLUMA_CMM_PUBLICATION_EVENT_PREMOUNT_COMPLETE)))
        failure = "synchronous_stock_premount_failed";
    else if (!failure &&
             (!dluma_cmm_validate_stock_publication() ||
              !dluma_cmm_publication_transition(
                  DLUMA_CMM_PUBLICATION_EVENT_CATALOGS_VALID)))
        failure = "synchronous_stock_source_validation_failed";
    else if (!failure &&
             (!dluma_cmm_mount_overlay_after_catalogs() ||
              !dluma_cmm_publication_transition(
                  DLUMA_CMM_PUBLICATION_EVENT_OVERLAY_MOUNTED)))
        failure = "generated_overlay_mount_failed";
    if (!failure)
    {
        g_dluma_cmm_runtime.restoration_wait_logged = 0U;
        dluma_log_pair(
            "INFO", "custom_main_menu_frontend_publication",
            origin ? origin : "unknown_owner");
    }
    return failure;
}

/* MainMenuModuleDI's +0x3A8 initializer is the first owning frontend seam
 * after ITools is live and before the original initializer starts UI
 * construction.  Publication therefore completes before any selected XUI or
 * background resource is resolved. */
static __int64 __fastcall dluma_cmm_main_initialize_ui_hook(void *module)
{
    DlumaCmmInitializeUiFn original =
        g_dluma_cmm_runtime.main_initialize_ui_original;
    const char *failure = 0;
    LONG state;
    int owner = 0;
    __int64 result = 0;
    dluma_atomic_increment(&g_dluma_cmm_runtime.active_calls);
    __try
    {
        if (!original)
            __leave;
        /* Source removal and background unload can complete after the last
         * gameplay frame but before the returning frontend receives another
         * GameDI tick.  Consume that retirement at the owning UI seam so a
         * new MainMenuModuleDI can never inherit the previous generation's
         * ACTIVE publication state. */
        (void)dluma_cmm_consume_source_retirement(0);
        if (dluma_custom_main_menu_selected_ready() &&
            !g_dluma_cmm_runtime.custom_presentation_disabled &&
            dluma_atomic_compare_exchange(
                &g_dluma_cmm_runtime.stopping, 0, 0) == 0)
        {
            state = dluma_atomic_compare_exchange(
                &g_dluma_cmm_runtime.publication_state, 0, 0);
            if (state == DLUMA_CMM_PUBLICATION_AWAITING_UI_INITIALIZER)
            {
                if (dluma_atomic_compare_exchange(
                        &g_dluma_cmm_runtime.restoration_pending, 0, 0) != 0)
                {
                    if (!dluma_cmm_capture_restoration_module(module))
                        failure = "returning_ui_initializer_module_invalid";
                    else if (!dluma_cmm_frontend_restore_allowed())
                        failure = "returning_ui_initializer_cleanup_incomplete";
                    else
                        /* The exact MainMenuModule initializer is an owning
                         * frontend seam once outgoing cleanup has finished.
                         * Publish before original constructs UI; scene and
                         * audio completion remain pending for the tick. */
                        dluma_custom_main_menu_runtime_frontend_ready();
                }
                if (!failure && dluma_atomic_compare_exchange(
                        &g_dluma_cmm_runtime.publication_processing, 1, 0) != 0)
                    failure = "ui_initializer_publication_owner_unavailable";
                else if (!failure)
                {
                    owner = 1;
                    failure = dluma_cmm_publish_frontend_generation(
                        module,
                        "synchronous_completion_before_original_initializer");
                }
                if (owner)
                {
                    dluma_atomic_exchange(
                        &g_dluma_cmm_runtime.publication_processing, 0);
                    owner = 0;
                }
                if (failure)
                    dluma_cmm_disable_custom_presentation(failure);
            }
            else if (state < DLUMA_CMM_PUBLICATION_READY_BEFORE_UI &&
                     state != DLUMA_CMM_PUBLICATION_STOCK_FALLBACK &&
                     dluma_atomic_compare_exchange(
                         &g_dluma_cmm_runtime.restoration_pending, 0, 0) == 0)
                dluma_cmm_disable_custom_presentation(
                    "ui_initializer_entered_during_incomplete_publication");
        }

        /* The pass-through owner never replays UI phases.  Regardless of
         * publication success or stock fallback, this invocation delegates to
         * the exact original initializer once and only once. */
        result = original(module);
    }
    __finally
    {
        if (owner)
            dluma_atomic_exchange(
                &g_dluma_cmm_runtime.publication_processing, 0);
        dluma_atomic_decrement(&g_dluma_cmm_runtime.active_calls);
    }
    return result;
}

/* +0x3B0 is the universal common UI-registration seam reached after +0x3A8.
 * When custom texture definitions are declared, add them exactly once
 * immediately before stock registration and always preserve the stock call. */
static void __fastcall dluma_cmm_main_register_ui_elements_hook(void *level)
{
    DlumaCmmUiPhaseFn original =
        g_dluma_cmm_runtime.main_register_ui_elements_original;
    const DlumaCustomMainMenuManifest *manifest =
        dluma_custom_main_menu_selected_manifest();
    LONG state;
    dluma_atomic_increment(&g_dluma_cmm_runtime.active_calls);
    __try
    {
        if (!original)
            __leave;
        state = dluma_atomic_compare_exchange(
            &g_dluma_cmm_runtime.publication_state, 0, 0);
        if (dluma_cmm_restoration_request_pending(state) && level &&
            dluma_is_readable(level, sizeof(void *)))
        {
            dluma_cmm_lock(&g_dluma_cmm_runtime.generation_lock);
            g_dluma_cmm_runtime.restoration_ui_level = level;
            dluma_cmm_unlock(&g_dluma_cmm_runtime.generation_lock);
            if (dluma_atomic_exchange(
                    &g_dluma_cmm_runtime.restoration_ui_requested, 1) == 0)
                dluma_log_pair(
                    "INFO", "custom_main_menu_frontend_restoration",
                    "ui_registration_deferred_until_generation_ready");
            __leave;
        }
        if (manifest && manifest->use_custom_textures &&
            dluma_custom_main_menu_selected_ready() &&
            !g_dluma_cmm_runtime.custom_presentation_disabled &&
            state >= DLUMA_CMM_PUBLICATION_READY_BEFORE_UI &&
            state < DLUMA_CMM_PUBLICATION_STOCK_FALLBACK &&
            dluma_atomic_compare_exchange(
                &g_dluma_cmm_runtime.stopping, 0, 0) == 0 &&
            !dluma_cmm_texture_definitions_load(level))
        {
            dluma_log_pair(
                "WARN", "custom_main_menu_texture_definitions",
                "load_failed_custom_presentation_retained");
        }
        original(level);
    }
    __finally
    {
        dluma_atomic_decrement(&g_dluma_cmm_runtime.active_calls);
    }
}

static int dluma_cmm_call_custom_load(void *module)
{
    DlumaCmmLoadFn original = g_dluma_cmm_runtime.load_original;
    const DlumaCustomMainMenuManifest *manifest =
        dluma_custom_main_menu_selected_manifest();
    DlumaCmmTtlString *game_name = 0;
    DlumaCmmTtlString saved;
    DlumaCmmTtlString replacement;
    DlumaCmmCustomLoadResult result = DLUMA_CMM_CUSTOM_LOAD_NOT_ENTERED;
    if (!original || !manifest || !dluma_cmm_main_menu_module_valid(module) ||
        !dluma_cmm_try_lock(&g_dluma_cmm_runtime.menu_name_lock))
        return 0;
    __try
    {
        if (!dluma_cmm_prepare_temporary_menu_name(
                manifest, &game_name, &saved, &replacement))
            __leave;
        /* The crash receipt begins at the first operation that can hand the
         * promoted custom scene to an engine worker.  Publication failures
         * before this point remain retryable and never quarantine a valid
         * manifest. */
        dluma_custom_main_menu_mark_activation_pending();
        g_dluma_cmm_runtime.custom_background_substituted = 1U;
        dluma_log_pair(
            "INFO", "custom_main_menu_background_level",
            DLUMA_CUSTOM_MENU_STOCK_LEVEL);
        dluma_log_pair(
            "INFO", "custom_main_menu_background_project_map",
            manifest->map);
        __try
        {
            result = DLUMA_CMM_CUSTOM_LOAD_ENTERED_FAILED;
            original(module);
            dluma_cmm_observe_background_after_load(module);
            result = DLUMA_CMM_CUSTOM_LOAD_COMPLETED;
        }
        __finally
        {
            dluma_cmm_restore_temporary_menu_name(
                game_name, &saved, &replacement);
        }
    }
    __except (dluma_cmm_content_exception_filter(GetExceptionInformation()))
    {
        if (result != DLUMA_CMM_CUSTOM_LOAD_NOT_ENTERED)
            result = DLUMA_CMM_CUSTOM_LOAD_ENTERED_FAILED;
    }
    dluma_cmm_unlock(&g_dluma_cmm_runtime.menu_name_lock);
    return (int)result;
}



static void dluma_cmm_note_native_frontend_request(void *module)
{
    if (!g_dluma_cmm_runtime.started ||
        dluma_atomic_compare_exchange(&g_dluma_cmm_runtime.stopping, 0, 0) != 0 ||
        dluma_atomic_compare_exchange(
            &g_dluma_cmm_runtime.publication_processing, 0, 0) != 0 ||
        dluma_atomic_compare_exchange(
            &g_dluma_cmm_runtime.restoration_processing, 0, 0) != 0)
        return;
    (void)dluma_cmm_consume_source_retirement(0);
    if (module == g_dluma_cmm_runtime.restoration_module &&
        g_dluma_cmm_runtime.restoration_background_required &&
        dluma_cmm_main_menu_module_valid(module) &&
        dluma_atomic_compare_exchange(
            &g_dluma_cmm_runtime.restoration_pending, 0, 0) != 0 &&
        dluma_cmm_frontend_restore_allowed())
    {
        dluma_log_pair("INFO", "custom_main_menu_frontend_restoration",
                     "native_background_request_authorized_return");
        dluma_custom_main_menu_runtime_frontend_ready();
    }
}

static void __fastcall dluma_cmm_load_hook(void *module)
{
    DlumaCmmLoadFn original = g_dluma_cmm_runtime.load_original;
    LONG state;
    int load_result;
    const char *publication_failure = 0;
    dluma_atomic_increment(&g_dluma_cmm_runtime.active_calls);
    __try
    {
        if (!original)
            __leave;
        dluma_cmm_note_native_frontend_request(module);
        state = dluma_atomic_compare_exchange(
            &g_dluma_cmm_runtime.publication_state, 0, 0);
        /* SwitchMenuState continues native UI setup immediately after this
         * callback. Keep its synchronous load contract on frontend return. */
        if (state == DLUMA_CMM_PUBLICATION_AWAITING_UI_INITIALIZER &&
            module == g_dluma_cmm_runtime.restoration_module &&
            dluma_custom_main_menu_selected_ready() &&
            !g_dluma_cmm_runtime.custom_presentation_disabled &&
            dluma_atomic_compare_exchange(
                &g_dluma_cmm_runtime.frontend_return_ready, 0, 0) != 0 &&
            dluma_atomic_compare_exchange(
                &g_dluma_cmm_runtime.restoration_processing, 0, 0) == 0 &&
            dluma_cmm_frontend_restore_allowed() &&
            dluma_atomic_compare_exchange(
                &g_dluma_cmm_runtime.publication_processing, 1, 0) == 0)
        {
            __try
            {
                publication_failure = dluma_cmm_publish_frontend_generation(
                    module, "synchronous_return_before_native_background_load");
            }
            __finally
            {
                dluma_atomic_exchange(
                    &g_dluma_cmm_runtime.publication_processing, 0);
            }
            if (publication_failure)
                dluma_cmm_disable_custom_presentation(publication_failure);
            state = dluma_atomic_compare_exchange(
                &g_dluma_cmm_runtime.publication_state, 0, 0);
        }
        if (dluma_custom_main_menu_selected_ready() &&
            !g_dluma_cmm_runtime.custom_presentation_disabled &&
            dluma_atomic_compare_exchange(
                &g_dluma_cmm_runtime.stopping, 0, 0) == 0 &&
            dluma_cmm_restoration_request_pending(state) &&
            !(state == DLUMA_CMM_PUBLICATION_READY_BEFORE_UI &&
              module == g_dluma_cmm_runtime.publication_module &&
              dluma_atomic_compare_exchange(
                  &g_dluma_cmm_runtime.frontend_return_ready, 0, 0) != 0 &&
              dluma_atomic_compare_exchange(
                  &g_dluma_cmm_runtime.restoration_processing, 0, 0) == 0 &&
              dluma_atomic_compare_exchange(
                  &g_dluma_cmm_runtime.publication_processing, 0, 0) == 0) &&
            dluma_cmm_capture_restoration_module(module))
        {
            if (dluma_atomic_exchange(
                    &g_dluma_cmm_runtime.restoration_load_requested, 1) == 0)
                dluma_log_pair(
                    "INFO", "custom_main_menu_frontend_restoration",
                    "background_load_deferred_until_generation_ready");
            __leave;
        }
        if (!dluma_custom_main_menu_selected_ready() ||
            g_dluma_cmm_runtime.custom_presentation_disabled ||
            dluma_atomic_compare_exchange(
                &g_dluma_cmm_runtime.stopping, 0, 0) != 0 ||
            state < DLUMA_CMM_PUBLICATION_READY_BEFORE_UI ||
            state == DLUMA_CMM_PUBLICATION_STOCK_FALLBACK)
        {
            original(module);
            __leave;
        }
        if (state == DLUMA_CMM_PUBLICATION_READY_BEFORE_UI)
        {
            if (!dluma_cmm_publication_transition(
                    DLUMA_CMM_PUBLICATION_EVENT_LOAD_STARTED))
            {
                dluma_cmm_disable_custom_presentation(
                    "custom_load_after_initializer_publication_failed");
                original(module);
                __leave;
            }
            load_result = dluma_cmm_call_custom_load(module);
            if (load_result != DLUMA_CMM_CUSTOM_LOAD_COMPLETED)
            {
                dluma_cmm_disable_custom_presentation(
                    load_result == DLUMA_CMM_CUSTOM_LOAD_ENTERED_FAILED
                        ? "custom_load_entered_then_failed_no_replay"
                        : "custom_load_not_entered_stock_replay");
                if (load_result == DLUMA_CMM_CUSTOM_LOAD_NOT_ENTERED)
                    original(module);
                else
                    dluma_log_pair(
                        "WARN", "custom_main_menu_background_load",
                        "engine_load_entered_failure_not_replayed");
                __leave;
            }
            g_dluma_cmm_runtime.custom_load_complete = 1U;
            dluma_log_pair(
                "INFO", "custom_main_menu_background_load",
                dluma_atomic_compare_exchange(
                    &g_dluma_cmm_runtime.frontend_return_ready, 0, 0)
                    ? "completed_before_native_return_continues"
                    : "completed_after_synchronous_initializer_publication");
            __leave;
        }
        if ((state == DLUMA_CMM_PUBLICATION_LOADING_BACKGROUND ||
             state == DLUMA_CMM_PUBLICATION_ACTIVE) &&
            module == g_dluma_cmm_runtime.publication_module)
        {
            dluma_log_pair(
                "INFO", "custom_main_menu_background_load",
                "repeated_generation_request_coalesced");
            __leave;
        }
        original(module);
    }
    __finally
    {
        dluma_atomic_decrement(&g_dluma_cmm_runtime.active_calls);
    }
}

static __int64 __fastcall dluma_cmm_unload_hook(void *module)
{
    __int64 result = 0;
    DlumaCmmUnloadFn original = g_dluma_cmm_runtime.unload_original;
    ULONGLONG owned_generation = 0;
    void *owned_background = 0;
    int owns_generation = 0;
    int pending_restoration_owner = 0;
    int music_released = 1;
    int textures_unloaded = 1;
    int manager_cleared = 1;
    dluma_atomic_increment(&g_dluma_cmm_runtime.active_calls);
    dluma_cmm_lock(&g_dluma_cmm_runtime.generation_lock);
    owned_generation = g_dluma_cmm_runtime.frontend_generation;
    pending_restoration_owner = module &&
        module == g_dluma_cmm_runtime.restoration_module &&
        g_dluma_cmm_runtime.restoration_background_required;
    owns_generation = module && owned_generation &&
        g_dluma_cmm_runtime.generation_active &&
        (module == g_dluma_cmm_runtime.publication_module ||
         module == g_dluma_cmm_runtime.active_module ||
         module == g_dluma_cmm_runtime.loaded_module);
    if (owns_generation)
    {
        owned_background = g_dluma_cmm_runtime.active_background;
        if (g_dluma_cmm_runtime.texture_level == module)
            g_dluma_cmm_runtime.restoration_ui_level = module;
    }
    dluma_cmm_unlock(&g_dluma_cmm_runtime.generation_lock);
    __try
    {
        if (owns_generation)
        {
            dluma_atomic_exchange(&g_dluma_cmm_runtime.frontend_committed, 0);
            dluma_cmm_visible_screens_clear();
            music_released =
                dluma_custom_main_menu_music_retire_generation();
            textures_unloaded = dluma_cmm_texture_definitions_unload();
            dluma_cmm_stop_started_fx_emitters(owned_background);
            dluma_cmm_restore_fx(owned_background);
            manager_cleared = dluma_cmm_clear_music_manager();
            if (!music_released || !textures_unloaded || !manager_cleared)
                dluma_log_pair(
                    "WARN", "custom_main_menu_background_unload",
                    "best_effort_dependency_cleanup_failed_original_unload_preserved");
            g_dluma_cmm_runtime.camera_applied = 0;
            g_dluma_cmm_runtime.stable_since_ms = 0;
            g_dluma_cmm_runtime.next_camera_retry_ms = 0;
            g_dluma_cmm_runtime.next_music_manager_retry_ms = 0;
            g_dluma_cmm_runtime.next_main_music_refresh_ms = 0;
            g_dluma_cmm_runtime.active_background = 0;
            g_dluma_cmm_runtime.active_module = 0;
            g_dluma_cmm_runtime.loaded_module = 0;
            g_dluma_cmm_runtime.custom_background_substituted = 0U;
        }
        if (original)
            result = original(module);
    }
    __finally
    {
        if ((owns_generation || pending_restoration_owner) &&
            dluma_cmm_main_menu_module_valid(module))
        {
            dluma_cmm_lock(&g_dluma_cmm_runtime.generation_lock);
            g_dluma_cmm_runtime.restoration_module = module;
            g_dluma_cmm_runtime.restoration_background_required = 1U;
            g_dluma_cmm_runtime.restoration_wait_logged = 0U;
            dluma_cmm_unlock(&g_dluma_cmm_runtime.generation_lock);
            dluma_log_pair(
                "INFO", "custom_main_menu_frontend_restoration",
                "background_unloaded_exact_module_owner_retained");
        }
        if (owns_generation)
        {
            dluma_custom_main_menu_runtime_note_source_retired(
                2U, owned_generation);
            dluma_log_pair(
                "INFO", "custom_main_menu_background_unload", "complete");
        }
        dluma_atomic_decrement(&g_dluma_cmm_runtime.active_calls);
    }
    return result;
}


static void __fastcall dluma_cmm_activate_hook(void *module)
{
    DlumaCmmActivateFn original = g_dluma_cmm_runtime.activate_original;
    LONG state;
    dluma_atomic_increment(&g_dluma_cmm_runtime.active_calls);
    __try
    {
        if (!original)
            __leave;
        state = dluma_atomic_compare_exchange(
            &g_dluma_cmm_runtime.publication_state, 0, 0);
        if (dluma_custom_main_menu_selected_ready() &&
            !g_dluma_cmm_runtime.custom_presentation_disabled &&
            dluma_atomic_compare_exchange(
                &g_dluma_cmm_runtime.stopping, 0, 0) == 0 &&
            dluma_cmm_restoration_request_pending(state) &&
            !(state == DLUMA_CMM_PUBLICATION_LOADING_BACKGROUND &&
              g_dluma_cmm_runtime.custom_load_complete &&
              module == g_dluma_cmm_runtime.publication_module &&
              dluma_atomic_compare_exchange(
                  &g_dluma_cmm_runtime.frontend_return_ready, 0, 0) != 0 &&
              dluma_atomic_compare_exchange(
                  &g_dluma_cmm_runtime.restoration_processing, 0, 0) == 0 &&
              dluma_atomic_compare_exchange(
                  &g_dluma_cmm_runtime.publication_processing, 0, 0) == 0) &&
            dluma_cmm_capture_restoration_module(module))
        {
            if (dluma_atomic_exchange(
                    &g_dluma_cmm_runtime.restoration_activate_requested, 1) == 0)
                dluma_log_pair(
                    "INFO", "custom_main_menu_frontend_restoration",
                    "background_activation_deferred_until_generation_ready");
            __leave;
        }
        if (state == DLUMA_CMM_PUBLICATION_ACTIVE &&
            module == g_dluma_cmm_runtime.publication_module &&
            g_dluma_cmm_runtime.active_background)
        {
            dluma_log_pair(
                "INFO", "custom_main_menu_background_activation",
                "repeated_generation_request_coalesced");
            __leave;
        }
        if (!dluma_custom_main_menu_selected_ready() ||
            g_dluma_cmm_runtime.custom_presentation_disabled ||
            dluma_atomic_compare_exchange(
                &g_dluma_cmm_runtime.stopping, 0, 0) != 0 ||
            state < DLUMA_CMM_PUBLICATION_LOADING_BACKGROUND ||
            module != g_dluma_cmm_runtime.publication_module ||
            !g_dluma_cmm_runtime.custom_load_complete)
        {
            original(module);
            __leave;
        }
        original(module);
        dluma_cmm_apply_camera(module);
        if (state == DLUMA_CMM_PUBLICATION_LOADING_BACKGROUND)
            (void)dluma_cmm_publication_transition(
                DLUMA_CMM_PUBLICATION_EVENT_ACTIVATE_COMPLETE);
    }
    __finally
    {
        dluma_atomic_decrement(&g_dluma_cmm_runtime.active_calls);
    }
}

static void dluma_cmm_replay_deferred_stock_frontend(
    void *module, void *ui_level,
    int ui_requested, int load_requested, int activate_requested)
{
    if (ui_requested && ui_level &&
        g_dluma_cmm_runtime.main_register_ui_elements_original)
    {
        __try
        {
            g_dluma_cmm_runtime.main_register_ui_elements_original(ui_level);
        }
        __except (dluma_cmm_content_exception_filter(GetExceptionInformation()))
        {
            dluma_log_pair(
                "WARN", "custom_main_menu_stock_fallback",
                "deferred_stock_ui_registration_failed");
        }
    }
    if (!module || !dluma_cmm_main_menu_module_valid(module))
        return;
    if (load_requested && g_dluma_cmm_runtime.load_original)
    {
        __try
        {
            g_dluma_cmm_runtime.load_original(module);
        }
        __except (dluma_cmm_content_exception_filter(GetExceptionInformation()))
        {
            dluma_log_pair(
                "WARN", "custom_main_menu_stock_fallback",
                "deferred_stock_background_load_failed");
            return;
        }
    }
    if (activate_requested && g_dluma_cmm_runtime.activate_original)
    {
        __try
        {
            g_dluma_cmm_runtime.activate_original(module);
        }
        __except (dluma_cmm_content_exception_filter(GetExceptionInformation()))
        {
            dluma_log_pair(
                "WARN", "custom_main_menu_stock_fallback",
                "deferred_stock_background_activation_failed");
        }
    }
}

/* GameDI::OnFrame is the authoritative serialized transition observer.  The
 * one-time MainMenuModuleDI UI initializer is not guaranteed to run after a
 * gameplay epoch, so restoration retains the exact surviving module owner and
 * can renew its background without waiting for any UI hook to run again.  All
 * nested requests remain coalesced until publication and overlay precedence
 * are complete. */
static void dluma_cmm_restore_frontend_generation(void *level)
{
    const DlumaCustomMainMenuManifest *manifest;
    const char *failure = 0;
    void *module;
    void *ui_level;
    LONG state;
    int ui_requested;
    int load_requested;
    int activate_requested;
    int background_required;
    int should_load;
    int load_result;
    int activation_completed = 0;
    int action;
    if (!dluma_custom_main_menu_selected_ready() ||
        g_dluma_cmm_runtime.custom_presentation_disabled ||
        dluma_atomic_compare_exchange(
            &g_dluma_cmm_runtime.restoration_pending, 0, 0) == 0 ||
        !dluma_cmm_frontend_restore_allowed() ||
        dluma_atomic_compare_exchange(
            &g_dluma_cmm_runtime.stopping, 0, 0) != 0)
        return;
    state = dluma_atomic_compare_exchange(
        &g_dluma_cmm_runtime.publication_state, 0, 0);
    action = dluma_custom_main_menu_runtime_restoration_action(
        1, dluma_atomic_compare_exchange(
            &g_dluma_cmm_runtime.frontend_return_ready, 0, 0) != 0,
        state, g_dluma_cmm_runtime.custom_load_complete);
    if (action == DLUMA_CMM_RESTORATION_WAIT ||
        (action == DLUMA_CMM_RESTORATION_PRESENTATION &&
         dluma_atomic_compare_exchange(
             &g_dluma_cmm_runtime.restoration_ui_requested, 0, 0) == 0))
        return;
    dluma_cmm_lock(&g_dluma_cmm_runtime.generation_lock);
    module = g_dluma_cmm_runtime.restoration_module;
    ui_level = g_dluma_cmm_runtime.restoration_ui_level;
    background_required = 1;
    dluma_cmm_unlock(&g_dluma_cmm_runtime.generation_lock);
    if (!dluma_cmm_main_menu_module_valid(module))
    {
        if (!g_dluma_cmm_runtime.restoration_wait_logged)
        {
            g_dluma_cmm_runtime.restoration_wait_logged = 1U;
            dluma_log_pair(
                "INFO", "custom_main_menu_frontend_restoration",
                "waiting_for_valid_main_menu_module");
        }
        return;
    }
    if (dluma_atomic_compare_exchange(
            &g_dluma_cmm_runtime.publication_processing, 1, 0) != 0)
        return;
    dluma_atomic_exchange(
        &g_dluma_cmm_runtime.restoration_processing, 1);

    if (action == DLUMA_CMM_RESTORATION_PUBLISH)
        dluma_log_pair(
            "INFO", "custom_main_menu_frontend_restoration",
            "gameplay_to_frontend_generation_started");
    if (action == DLUMA_CMM_RESTORATION_PUBLISH)
        failure = dluma_cmm_publish_frontend_generation(
            module, "gameplay_to_frontend_serialized_tick");
    else if (module != g_dluma_cmm_runtime.publication_module)
        failure = "restored_generation_module_changed_after_publication";
    ui_requested = dluma_atomic_exchange(
        &g_dluma_cmm_runtime.restoration_ui_requested, 0) != 0;
    load_requested = dluma_atomic_exchange(
        &g_dluma_cmm_runtime.restoration_load_requested, 0) != 0;
    activate_requested = dluma_atomic_exchange(
        &g_dluma_cmm_runtime.restoration_activate_requested, 0) != 0;
    if (!ui_level)
        ui_level = level;
    if (failure)
    {
        dluma_cmm_disable_custom_presentation(failure);
        dluma_atomic_exchange(
            &g_dluma_cmm_runtime.restoration_processing, 0);
        dluma_atomic_exchange(
            &g_dluma_cmm_runtime.publication_processing, 0);
        dluma_cmm_replay_deferred_stock_frontend(
            module, ui_level,
            ui_requested, load_requested, activate_requested);
        return;
    }

    manifest = dluma_custom_main_menu_selected_manifest();
    /* Original UI registration may have been requested synchronously while
     * publication was running.  Fold that nested request into this owner
     * before replaying the UI exactly once. */
    if (dluma_atomic_exchange(
            &g_dluma_cmm_runtime.restoration_ui_requested, 0) != 0)
    {
        ui_requested = 1;
        dluma_cmm_lock(&g_dluma_cmm_runtime.generation_lock);
        if (g_dluma_cmm_runtime.restoration_ui_level)
            ui_level = g_dluma_cmm_runtime.restoration_ui_level;
        dluma_cmm_unlock(&g_dluma_cmm_runtime.generation_lock);
    }
    if (manifest && manifest->use_custom_textures &&
        !ui_level)
    {
        /* The UI owner can arrive after the cleanup notification.  Keep
         * requests pending instead of turning that ordering into fallback. */
        if (ui_requested)
            dluma_atomic_exchange(&g_dluma_cmm_runtime.restoration_ui_requested, 1);
        if (load_requested)
            dluma_atomic_exchange(&g_dluma_cmm_runtime.restoration_load_requested, 1);
        if (activate_requested)
            dluma_atomic_exchange(&g_dluma_cmm_runtime.restoration_activate_requested, 1);
        if (!g_dluma_cmm_runtime.restoration_wait_logged)
        {
            g_dluma_cmm_runtime.restoration_wait_logged = 1U;
            dluma_log_pair(
                "INFO", "custom_main_menu_frontend_restoration",
                "waiting_for_ui_texture_owner");
        }
        dluma_atomic_exchange(&g_dluma_cmm_runtime.restoration_processing, 0);
        dluma_atomic_exchange(&g_dluma_cmm_runtime.publication_processing, 0);
        return;
    }
    g_dluma_cmm_runtime.restoration_wait_logged = 0U;
    if (manifest && manifest->use_custom_textures &&
        !dluma_cmm_texture_definitions_load(ui_level))
        dluma_log_pair(
            "WARN", "custom_main_menu_frontend_restoration",
            "texture_definitions_restore_failed_presentation_retained");
    if (ui_requested &&
        g_dluma_cmm_runtime.main_register_ui_elements_original)
    {
        __try
        {
            g_dluma_cmm_runtime.main_register_ui_elements_original(ui_level);
        }
        __except (dluma_cmm_content_exception_filter(GetExceptionInformation()))
        {
            failure = "deferred_custom_ui_registration_failed";
        }
    }
    /* A replayed UI registration can synchronously request background load
     * or activation.  Merge those requests before deciding whether this
     * generation needs its single custom load. */
    load_requested = load_requested ||
        dluma_atomic_exchange(
            &g_dluma_cmm_runtime.restoration_load_requested, 0) != 0;
    activate_requested = activate_requested ||
        dluma_atomic_exchange(
            &g_dluma_cmm_runtime.restoration_activate_requested, 0) != 0;
    activate_requested = activate_requested || background_required;
    state = dluma_atomic_compare_exchange(
        &g_dluma_cmm_runtime.publication_state, 0, 0);
    should_load = !g_dluma_cmm_runtime.custom_load_complete &&
        (background_required || load_requested || activate_requested) &&
        state == DLUMA_CMM_PUBLICATION_READY_BEFORE_UI;
    if (!failure && should_load && !dluma_cmm_publication_transition(
            DLUMA_CMM_PUBLICATION_EVENT_LOAD_STARTED))
        failure = "restored_generation_load_transition_failed";
    if (!failure && should_load)
    {
        load_result = dluma_cmm_call_custom_load(module);
        if (load_result != DLUMA_CMM_CUSTOM_LOAD_COMPLETED)
            failure = load_result == DLUMA_CMM_CUSTOM_LOAD_ENTERED_FAILED
                ? "restored_custom_load_entered_then_failed_no_replay"
                : "restored_custom_load_not_entered_stock_replay";
        else
        {
            g_dluma_cmm_runtime.custom_load_complete = 1U;
            dluma_log_pair(
                "INFO", "custom_main_menu_frontend_restoration",
                load_requested
                    ? "deferred_background_load_replayed_once"
                    : "background_load_recreated_once");
        }
    }
    /* Custom load can synchronously request activation through the hooked
     * vtable.  Coalesce it into this owner; a duplicate load request is
     * already satisfied by the single call above. */
    (void)dluma_atomic_exchange(
        &g_dluma_cmm_runtime.restoration_load_requested, 0);
    activate_requested = activate_requested ||
        dluma_atomic_exchange(
            &g_dluma_cmm_runtime.restoration_activate_requested, 0) != 0;
    state = dluma_atomic_compare_exchange(
        &g_dluma_cmm_runtime.publication_state, 0, 0);
    if (!failure && activate_requested &&
        state == DLUMA_CMM_PUBLICATION_LOADING_BACKGROUND &&
        g_dluma_cmm_runtime.custom_load_complete &&
        g_dluma_cmm_runtime.activate_original)
    {
        __try
        {
            g_dluma_cmm_runtime.activate_original(module);
            activation_completed = 1;
        }
        __except (dluma_cmm_content_exception_filter(GetExceptionInformation()))
        {
            failure = "restored_custom_background_activation_failed";
        }
        if (activation_completed)
        {
            dluma_cmm_apply_camera(module);
            if (!dluma_cmm_publication_transition(
                    DLUMA_CMM_PUBLICATION_EVENT_ACTIVATE_COMPLETE))
                failure = "restored_generation_activation_transition_failed";
        }
    }
    (void)dluma_atomic_exchange(
        &g_dluma_cmm_runtime.restoration_ui_requested, 0);
    (void)dluma_atomic_exchange(
        &g_dluma_cmm_runtime.restoration_activate_requested, 0);
    dluma_atomic_exchange(
        &g_dluma_cmm_runtime.restoration_processing, 0);
    dluma_atomic_exchange(
        &g_dluma_cmm_runtime.publication_processing, 0);
    if (failure)
    {
        int custom_load_entered = g_dluma_cmm_runtime.custom_load_complete ||
            g_dluma_cmm_runtime.custom_background_substituted;
        dluma_cmm_disable_custom_presentation(failure);
        if (!custom_load_entered)
            dluma_cmm_replay_deferred_stock_frontend(
                module, ui_level, 0, 1, activate_requested);
        else
            dluma_log_pair(
                "WARN", "custom_main_menu_frontend_restoration",
                "engine_custom_load_entered_failure_not_replayed");
        return;
    }
    dluma_cmm_lock(&g_dluma_cmm_runtime.generation_lock);
    g_dluma_cmm_runtime.restoration_ui_level = ui_level;
    g_dluma_cmm_runtime.restoration_background_required = 0U;
    dluma_cmm_unlock(&g_dluma_cmm_runtime.generation_lock);
    dluma_log_pair(
        "INFO", "custom_main_menu_frontend_restoration",
        activation_completed
            ? "generation_active"
            : should_load
                ? "generation_loaded_awaiting_original_activation"
                : "generation_ready_awaiting_original_load");
}

static int dluma_cmm_install_vtable_hooks(void)
{
    DWORD old_protect;
    DWORD restored;
    PVOID previous;
    void **first = g_dluma_cmm_runtime.load_slot;
    if (!first || !g_dluma_cmm_runtime.unload_slot ||
        g_dluma_cmm_runtime.unload_slot != first + 1 ||
        !dluma_is_readable(first, 2U * sizeof(void *)) ||
        *first != (void *)g_dluma_cmm_runtime.load_original ||
        *(first + 1) != (void *)g_dluma_cmm_runtime.unload_original ||
        !VirtualProtect(first, 2U * sizeof(void *), PAGE_READWRITE,
                        &old_protect))
        return 0;
    previous = dluma_atomic_pointer_compare_exchange(
        (PVOID volatile *)first, (PVOID)dluma_cmm_load_hook,
        (PVOID)g_dluma_cmm_runtime.load_original);
    if (previous != (PVOID)g_dluma_cmm_runtime.load_original)
        goto rollback;
    g_dluma_cmm_runtime.load_installed = 1U;
    previous = dluma_atomic_pointer_compare_exchange(
        (PVOID volatile *)(first + 1), (PVOID)dluma_cmm_unload_hook,
        (PVOID)g_dluma_cmm_runtime.unload_original);
    if (previous != (PVOID)g_dluma_cmm_runtime.unload_original)
        goto rollback;
    g_dluma_cmm_runtime.unload_installed = 1U;
    if (!VirtualProtect(first, 2U * sizeof(void *), old_protect, &restored))
    {
        g_dluma_cmm_runtime.teardown_failed = 1U;
        goto rollback_unprotected;
    }
    return dluma_cmm_finish_hook_mutation(
        1, 1, first, 2U * sizeof(void *));

rollback:
    if (g_dluma_cmm_runtime.load_installed)
    {
        previous = dluma_atomic_pointer_compare_exchange(
            (PVOID volatile *)first,
            (PVOID)g_dluma_cmm_runtime.load_original,
            (PVOID)dluma_cmm_load_hook);
        if (previous == (PVOID)dluma_cmm_load_hook)
            g_dluma_cmm_runtime.load_installed = 0;
        else
            g_dluma_cmm_runtime.teardown_failed = 1U;
    }
    (void)dluma_cmm_finish_hook_mutation(
        !g_dluma_cmm_runtime.load_installed,
        VirtualProtect(
            first, 2U * sizeof(void *), old_protect, &restored) != 0,
        first, 2U * sizeof(void *));
    return 0;

rollback_unprotected:
    previous = dluma_atomic_pointer_compare_exchange(
        (PVOID volatile *)(first + 1),
        (PVOID)g_dluma_cmm_runtime.unload_original,
        (PVOID)dluma_cmm_unload_hook);
    if (previous == (PVOID)dluma_cmm_unload_hook)
        g_dluma_cmm_runtime.unload_installed = 0;
    previous = dluma_atomic_pointer_compare_exchange(
        (PVOID volatile *)first,
        (PVOID)g_dluma_cmm_runtime.load_original,
        (PVOID)dluma_cmm_load_hook);
    if (previous == (PVOID)dluma_cmm_load_hook)
        g_dluma_cmm_runtime.load_installed = 0;
    (void)dluma_cmm_finish_hook_mutation(
        !g_dluma_cmm_runtime.load_installed &&
            !g_dluma_cmm_runtime.unload_installed,
        VirtualProtect(
            first, 2U * sizeof(void *), old_protect, &restored) != 0,
        first, 2U * sizeof(void *));
    return 0;
}

static int dluma_cmm_install_screen_hooks(void)
{
    DWORD old_protect;
    DWORD restored;
    PVOID previous;
    void **first = g_dluma_cmm_runtime.screen_shown_slot;
    int success = 1;
    if (!first || !g_dluma_cmm_runtime.screen_hidden_slot ||
        g_dluma_cmm_runtime.screen_hidden_slot != first + 1 ||
        !dluma_is_readable(first, 2U * sizeof(void *)) ||
        *first != (void *)g_dluma_cmm_runtime.screen_shown_original ||
        *(first + 1) != (void *)g_dluma_cmm_runtime.screen_hidden_original ||
        !VirtualProtect(first, 2U * sizeof(void *), PAGE_READWRITE,
                        &old_protect))
        return 0;
    previous = dluma_atomic_pointer_compare_exchange(
        (PVOID volatile *)first, (PVOID)dluma_cmm_screen_shown_hook,
        (PVOID)g_dluma_cmm_runtime.screen_shown_original);
    if (previous != (PVOID)g_dluma_cmm_runtime.screen_shown_original)
        success = 0;
    else
        g_dluma_cmm_runtime.screen_shown_installed = 1U;
    if (success)
    {
        previous = dluma_atomic_pointer_compare_exchange(
            (PVOID volatile *)(first + 1),
            (PVOID)dluma_cmm_screen_hidden_hook,
            (PVOID)g_dluma_cmm_runtime.screen_hidden_original);
        if (previous != (PVOID)g_dluma_cmm_runtime.screen_hidden_original)
            success = 0;
        else
            g_dluma_cmm_runtime.screen_hidden_installed = 1U;
    }
    if (!success && g_dluma_cmm_runtime.screen_shown_installed)
    {
        previous = dluma_atomic_pointer_compare_exchange(
            (PVOID volatile *)first,
            (PVOID)g_dluma_cmm_runtime.screen_shown_original,
            (PVOID)dluma_cmm_screen_shown_hook);
        if (previous == (PVOID)dluma_cmm_screen_shown_hook)
            g_dluma_cmm_runtime.screen_shown_installed = 0;
        else
            g_dluma_cmm_runtime.teardown_failed = 1U;
    }
    if (!VirtualProtect(
            first, 2U * sizeof(void *), old_protect, &restored))
    {
        success = 0;
        g_dluma_cmm_runtime.teardown_failed = 1U;
    }
    return dluma_cmm_finish_hook_mutation(
        success && g_dluma_cmm_runtime.screen_shown_installed &&
            g_dluma_cmm_runtime.screen_hidden_installed,
        !g_dluma_cmm_runtime.teardown_failed,
        first, 2U * sizeof(void *));
}

static int dluma_cmm_install_activate_hook(void)
{
    BYTE *trampoline;
    DWORD old_protect;
    DWORD trampoline_old;
    DWORD restored;
    ULONG_PTR hook_address = 0;
    DlumaCmmActivateFn hook = dluma_cmm_activate_hook;
    unsigned int index;
    if (!g_dluma_cmm_runtime.activate_target ||
        !dluma_cmm_bytes(
            g_dluma_cmm_runtime.activate_target,
            g_dluma_cmm_activate_prefix,
            DLUMA_CMM_ACTIVATE_STOLEN_SIZE))
        return 0;
    trampoline = (BYTE *)VirtualAlloc(
        0, DLUMA_CMM_ACTIVATE_STOLEN_SIZE + DLUMA_CMM_ABSOLUTE_JUMP_SIZE,
        MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!trampoline)
        return 0;
    memcpy(trampoline, g_dluma_cmm_activate_prefix,
           DLUMA_CMM_ACTIVATE_STOLEN_SIZE);
    dluma_cmm_absolute_jump(
        trampoline + DLUMA_CMM_ACTIVATE_STOLEN_SIZE,
        (ULONG_PTR)(g_dluma_cmm_runtime.activate_target +
                    DLUMA_CMM_ACTIVATE_STOLEN_SIZE));
    if (!VirtualProtect(
            trampoline,
            DLUMA_CMM_ACTIVATE_STOLEN_SIZE + DLUMA_CMM_ABSOLUTE_JUMP_SIZE,
            PAGE_EXECUTE_READ, &trampoline_old))
    {
        VirtualFree(trampoline, 0, MEM_RELEASE);
        return 0;
    }
    if (!FlushInstructionCache(
            GetCurrentProcess(), trampoline,
            DLUMA_CMM_ACTIVATE_STOLEN_SIZE +
                DLUMA_CMM_ABSOLUTE_JUMP_SIZE))
    {
        VirtualFree(trampoline, 0, MEM_RELEASE);
        return 0;
    }
    memcpy(g_dluma_cmm_runtime.activate_original_bytes,
           g_dluma_cmm_activate_prefix, DLUMA_CMM_ACTIVATE_STOLEN_SIZE);
    memset(g_dluma_cmm_runtime.activate_patch, 0x90,
           DLUMA_CMM_ACTIVATE_STOLEN_SIZE);
    memcpy(&hook_address, &hook, sizeof(hook_address));
    dluma_cmm_absolute_jump(
        g_dluma_cmm_runtime.activate_patch, hook_address);
    if (!VirtualProtect(
            g_dluma_cmm_runtime.activate_target,
            DLUMA_CMM_ACTIVATE_STOLEN_SIZE,
            PAGE_EXECUTE_READWRITE, &old_protect))
    {
        VirtualFree(trampoline, 0, MEM_RELEASE);
        return 0;
    }
    if (memcmp(g_dluma_cmm_runtime.activate_target,
               g_dluma_cmm_activate_prefix,
               DLUMA_CMM_ACTIVATE_STOLEN_SIZE) != 0)
        goto rollback;
    for (index = 0; index < DLUMA_CMM_ACTIVATE_STOLEN_SIZE; ++index)
        g_dluma_cmm_runtime.activate_target[index] =
            g_dluma_cmm_runtime.activate_patch[index];
    if (!VirtualProtect(
            g_dluma_cmm_runtime.activate_target,
            DLUMA_CMM_ACTIVATE_STOLEN_SIZE, old_protect, &restored))
    {
        g_dluma_cmm_runtime.teardown_failed = 1U;
        goto rollback_unprotected;
    }
    g_dluma_cmm_runtime.activate_trampoline = trampoline;
    g_dluma_cmm_runtime.activate_original =
        (DlumaCmmActivateFn)trampoline;
    g_dluma_cmm_runtime.activate_installed = 1U;
    return dluma_cmm_finish_hook_mutation(
        1, 1, g_dluma_cmm_runtime.activate_target,
        DLUMA_CMM_ACTIVATE_STOLEN_SIZE);

rollback_unprotected:
    memcpy(g_dluma_cmm_runtime.activate_target,
           g_dluma_cmm_activate_prefix, DLUMA_CMM_ACTIVATE_STOLEN_SIZE);
rollback:
    if (!VirtualProtect(
            g_dluma_cmm_runtime.activate_target,
            DLUMA_CMM_ACTIVATE_STOLEN_SIZE, old_protect, &restored))
        g_dluma_cmm_runtime.teardown_failed = 1U;
    (void)dluma_cmm_finish_hook_mutation(
        1,
        !g_dluma_cmm_runtime.teardown_failed,
        g_dluma_cmm_runtime.activate_target,
        DLUMA_CMM_ACTIVATE_STOLEN_SIZE);
    VirtualFree(trampoline, 0, MEM_RELEASE);
    return 0;
}


static int dluma_cmm_install_main_ui_initializer_hook(void)
{
    const DlumaCustomMainMenuManifest *manifest =
        dluma_custom_main_menu_selected_manifest();
    DWORD old_protect;
    DWORD restored;
    PVOID previous;
    void **first = g_dluma_cmm_runtime.main_initialize_ui_slot;
    const SIZE_T span = 2U * sizeof(void *);
    int install_texture_hook = manifest && manifest->use_custom_textures;
    if (!first || !g_dluma_cmm_runtime.main_register_ui_elements_slot ||
        g_dluma_cmm_runtime.main_register_ui_elements_slot != first + 1 ||
        !dluma_is_readable(first, span) ||
        *first != (void *)g_dluma_cmm_runtime.main_initialize_ui_original ||
        *(first + 1) !=
            (void *)g_dluma_cmm_runtime.main_register_ui_elements_original ||
        !VirtualProtect(first, span, PAGE_READWRITE, &old_protect))
        return 0;
    previous = dluma_atomic_pointer_compare_exchange(
        (PVOID volatile *)first,
        (PVOID)dluma_cmm_main_initialize_ui_hook,
        (PVOID)g_dluma_cmm_runtime.main_initialize_ui_original);
    if (previous != (PVOID)g_dluma_cmm_runtime.main_initialize_ui_original)
        goto rollback;
    g_dluma_cmm_runtime.main_initialize_ui_installed = 1U;
    if (install_texture_hook)
    {
        previous = dluma_atomic_pointer_compare_exchange(
            (PVOID volatile *)(first + 1),
            (PVOID)dluma_cmm_main_register_ui_elements_hook,
            (PVOID)g_dluma_cmm_runtime.main_register_ui_elements_original);
        if (previous !=
                (PVOID)g_dluma_cmm_runtime.main_register_ui_elements_original)
            goto rollback;
        g_dluma_cmm_runtime.main_texture_hook_installed = 1U;
    }
    if (!VirtualProtect(first, span, old_protect, &restored))
    {
        g_dluma_cmm_runtime.teardown_failed = 1U;
        goto rollback_unprotected;
    }
    return dluma_cmm_finish_hook_mutation(1, 1, first, span);

rollback_unprotected:
rollback:
    if (g_dluma_cmm_runtime.main_texture_hook_installed)
    {
        previous = dluma_atomic_pointer_compare_exchange(
            (PVOID volatile *)(first + 1),
            (PVOID)g_dluma_cmm_runtime.main_register_ui_elements_original,
            (PVOID)dluma_cmm_main_register_ui_elements_hook);
        if (previous == (PVOID)dluma_cmm_main_register_ui_elements_hook)
            g_dluma_cmm_runtime.main_texture_hook_installed = 0U;
        else
            g_dluma_cmm_runtime.teardown_failed = 1U;
    }
    if (g_dluma_cmm_runtime.main_initialize_ui_installed)
    {
        previous = dluma_atomic_pointer_compare_exchange(
            (PVOID volatile *)first,
            (PVOID)g_dluma_cmm_runtime.main_initialize_ui_original,
            (PVOID)dluma_cmm_main_initialize_ui_hook);
        if (previous == (PVOID)dluma_cmm_main_initialize_ui_hook)
            g_dluma_cmm_runtime.main_initialize_ui_installed = 0U;
        else
            g_dluma_cmm_runtime.teardown_failed = 1U;
    }
    (void)dluma_cmm_finish_hook_mutation(
        !g_dluma_cmm_runtime.main_initialize_ui_installed &&
            !g_dluma_cmm_runtime.main_texture_hook_installed,
        VirtualProtect(first, span, old_protect, &restored) != 0,
        first, span);
    return 0;
}

static int dluma_cmm_restore_vtable_hooks(void)
{
    DWORD old_protect;
    DWORD restored;
    PVOID previous;
    int success = 1;
    if (!g_dluma_cmm_runtime.load_installed &&
        !g_dluma_cmm_runtime.unload_installed)
        return 1;
    if (!g_dluma_cmm_runtime.load_slot ||
        !g_dluma_cmm_runtime.unload_slot ||
        !VirtualProtect(
            g_dluma_cmm_runtime.load_slot, 2U * sizeof(void *),
            PAGE_READWRITE, &old_protect))
    {
        g_dluma_cmm_runtime.teardown_failed = 1U;
        return 0;
    }
    if (g_dluma_cmm_runtime.unload_installed)
    {
        previous = dluma_atomic_pointer_compare_exchange(
            (PVOID volatile *)g_dluma_cmm_runtime.unload_slot,
            (PVOID)g_dluma_cmm_runtime.unload_original,
            (PVOID)dluma_cmm_unload_hook);
        if (previous == (PVOID)dluma_cmm_unload_hook)
            g_dluma_cmm_runtime.unload_installed = 0;
        else
        {
            success = 0;
            g_dluma_cmm_runtime.teardown_failed = 1U;
        }
    }
    if (g_dluma_cmm_runtime.load_installed)
    {
        previous = dluma_atomic_pointer_compare_exchange(
            (PVOID volatile *)g_dluma_cmm_runtime.load_slot,
            (PVOID)g_dluma_cmm_runtime.load_original,
            (PVOID)dluma_cmm_load_hook);
        if (previous == (PVOID)dluma_cmm_load_hook)
            g_dluma_cmm_runtime.load_installed = 0;
        else
        {
            success = 0;
            g_dluma_cmm_runtime.teardown_failed = 1U;
        }
    }
    return dluma_cmm_finish_hook_mutation(
        success,
        VirtualProtect(
            g_dluma_cmm_runtime.load_slot, 2U * sizeof(void *),
            old_protect, &restored) != 0,
        g_dluma_cmm_runtime.load_slot,
        2U * sizeof(void *));
}

static int dluma_cmm_restore_screen_hooks(void)
{
    DWORD old_protect;
    DWORD restored;
    PVOID previous;
    int success = 1;
    void **first = g_dluma_cmm_runtime.screen_shown_slot;
    if (!g_dluma_cmm_runtime.screen_shown_installed &&
        !g_dluma_cmm_runtime.screen_hidden_installed)
        return 1;
    if (!first || !g_dluma_cmm_runtime.screen_hidden_slot ||
        !VirtualProtect(first, 2U * sizeof(void *), PAGE_READWRITE,
                        &old_protect))
    {
        g_dluma_cmm_runtime.teardown_failed = 1U;
        return 0;
    }
    if (g_dluma_cmm_runtime.screen_hidden_installed)
    {
        previous = dluma_atomic_pointer_compare_exchange(
            (PVOID volatile *)g_dluma_cmm_runtime.screen_hidden_slot,
            (PVOID)g_dluma_cmm_runtime.screen_hidden_original,
            (PVOID)dluma_cmm_screen_hidden_hook);
        if (previous == (PVOID)dluma_cmm_screen_hidden_hook)
            g_dluma_cmm_runtime.screen_hidden_installed = 0;
        else
        {
            success = 0;
            g_dluma_cmm_runtime.teardown_failed = 1U;
        }
    }
    if (g_dluma_cmm_runtime.screen_shown_installed)
    {
        previous = dluma_atomic_pointer_compare_exchange(
            (PVOID volatile *)g_dluma_cmm_runtime.screen_shown_slot,
            (PVOID)g_dluma_cmm_runtime.screen_shown_original,
            (PVOID)dluma_cmm_screen_shown_hook);
        if (previous == (PVOID)dluma_cmm_screen_shown_hook)
            g_dluma_cmm_runtime.screen_shown_installed = 0;
        else
        {
            success = 0;
            g_dluma_cmm_runtime.teardown_failed = 1U;
        }
    }
    return dluma_cmm_finish_hook_mutation(
        success,
        VirtualProtect(
            first, 2U * sizeof(void *), old_protect, &restored) != 0,
        first, 2U * sizeof(void *));
}

static int dluma_cmm_restore_activate_hook(void)
{
    DWORD old_protect;
    DWORD restored;
    if (!g_dluma_cmm_runtime.activate_installed)
        return 1;
    if (!g_dluma_cmm_runtime.activate_target ||
        !VirtualProtect(
            g_dluma_cmm_runtime.activate_target,
            DLUMA_CMM_ACTIVATE_STOLEN_SIZE,
            PAGE_EXECUTE_READWRITE, &old_protect))
    {
        g_dluma_cmm_runtime.teardown_failed = 1U;
        return 0;
    }
    if (memcmp(g_dluma_cmm_runtime.activate_target,
               g_dluma_cmm_runtime.activate_patch,
               DLUMA_CMM_ACTIVATE_STOLEN_SIZE) != 0)
    {
        if (!VirtualProtect(
                g_dluma_cmm_runtime.activate_target,
                DLUMA_CMM_ACTIVATE_STOLEN_SIZE, old_protect, &restored))
            g_dluma_cmm_runtime.teardown_failed = 1U;
        g_dluma_cmm_runtime.teardown_failed = 1U;
        return 0;
    }
    memcpy(g_dluma_cmm_runtime.activate_target,
           g_dluma_cmm_runtime.activate_original_bytes,
           DLUMA_CMM_ACTIVATE_STOLEN_SIZE);
    if (!dluma_cmm_finish_hook_mutation(
            1,
            VirtualProtect(
                g_dluma_cmm_runtime.activate_target,
                DLUMA_CMM_ACTIVATE_STOLEN_SIZE,
                old_protect, &restored) != 0,
            g_dluma_cmm_runtime.activate_target,
            DLUMA_CMM_ACTIVATE_STOLEN_SIZE))
        return 0;
    g_dluma_cmm_runtime.activate_installed = 0;
    return 1;
}


static int dluma_cmm_restore_main_ui_initializer_hook(void)
{
    DWORD old_protect;
    DWORD restored;
    PVOID previous;
    int success = 1;
    void **first = g_dluma_cmm_runtime.main_initialize_ui_slot;
    const SIZE_T span = 2U * sizeof(void *);
    if (!g_dluma_cmm_runtime.main_initialize_ui_installed &&
        !g_dluma_cmm_runtime.main_texture_hook_installed)
        return 1;
    if (!first || !VirtualProtect(
            first, span, PAGE_READWRITE, &old_protect))
    {
        g_dluma_cmm_runtime.teardown_failed = 1U;
        return 0;
    }
    if (g_dluma_cmm_runtime.main_texture_hook_installed)
    {
        previous = dluma_atomic_pointer_compare_exchange(
            (PVOID volatile *)(first + 1),
            (PVOID)g_dluma_cmm_runtime.main_register_ui_elements_original,
            (PVOID)dluma_cmm_main_register_ui_elements_hook);
        if (previous == (PVOID)dluma_cmm_main_register_ui_elements_hook)
            g_dluma_cmm_runtime.main_texture_hook_installed = 0U;
        else
            success = 0;
    }
    if (g_dluma_cmm_runtime.main_initialize_ui_installed)
    {
        previous = dluma_atomic_pointer_compare_exchange(
            (PVOID volatile *)first,
            (PVOID)g_dluma_cmm_runtime.main_initialize_ui_original,
            (PVOID)dluma_cmm_main_initialize_ui_hook);
        if (previous == (PVOID)dluma_cmm_main_initialize_ui_hook)
            g_dluma_cmm_runtime.main_initialize_ui_installed = 0U;
        else
            success = 0;
    }
    if (!success)
        g_dluma_cmm_runtime.teardown_failed = 1U;
    return dluma_cmm_finish_hook_mutation(
        success,
        VirtualProtect(first, span, old_protect, &restored) != 0,
        first, span);
}

static int dluma_cmm_validate_profile(HMODULE gamedll)
{
    const DlumaCustomMainMenuManifest *manifest =
        dluma_custom_main_menu_selected_manifest();
    WCHAR player_path[DLUMA_MAX_WPATH];
    WCHAR gamedll_path[DLUMA_MAX_WPATH];
    char player_sha256[DLUMA_SHA256_HEX_CAPACITY];
    char gamedll_sha256[DLUMA_SHA256_HEX_CAPACITY];
    DWORD engine_timestamp;
    DWORD engine_image_size;
    DWORD gamedll_timestamp;
    DWORD gamedll_image_size;
    void *procedure;
    HMODULE player = GetModuleHandleW(0);
    g_dluma_cmm_runtime.engine = GetModuleHandleW(L"engine_x64_rwdi.dll");
    g_dluma_cmm_runtime.gamedll = gamedll;
    if (!player || !g_dluma_cmm_runtime.engine || !gamedll ||
        !dluma_get_module_path_checked(
            player, player_path, DLUMA_ARRAY_COUNT(player_path)) ||
        !dluma_get_module_path_checked(
            gamedll, gamedll_path, DLUMA_ARRAY_COUNT(gamedll_path)) ||
        !dluma_sha256_file_hex(
            player_path, 1024ULL * 1024ULL * 1024ULL, player_sha256) ||
        !dluma_sha256_file_hex(
            gamedll_path, 1024ULL * 1024ULL * 1024ULL,
            gamedll_sha256) ||
        !dluma_cmm_get_pe_identity(
            g_dluma_cmm_runtime.engine, &engine_timestamp,
            &engine_image_size) ||
        !dluma_cmm_get_pe_identity(
            gamedll, &gamedll_timestamp, &gamedll_image_size) ||
        !dluma_custom_main_menu_runtime_profile_identity_valid(
            player_sha256, gamedll_sha256,
            engine_timestamp, engine_image_size,
            gamedll_timestamp, gamedll_image_size))
        return 0;
    g_dluma_cmm_runtime.engine_base =
        (BYTE *)g_dluma_cmm_runtime.engine;
    g_dluma_cmm_runtime.gamedll_base = (BYTE *)gamedll;
    g_dluma_cmm_runtime.engine_image_size = engine_image_size;
    g_dluma_cmm_runtime.gamedll_image_size = gamedll_image_size;
    if (!dluma_cmm_bytes(
            g_dluma_cmm_runtime.gamedll_base + DLUMA_CMM_LOAD_RVA,
            g_dluma_cmm_load_prefix, DLUMA_ARRAY_COUNT(g_dluma_cmm_load_prefix)) ||
        !dluma_cmm_bytes(
            g_dluma_cmm_runtime.gamedll_base + DLUMA_CMM_UNLOAD_RVA,
            g_dluma_cmm_unload_prefix,
            DLUMA_ARRAY_COUNT(g_dluma_cmm_unload_prefix)) ||
        !dluma_cmm_bytes(
            g_dluma_cmm_runtime.gamedll_base + DLUMA_CMM_SCREEN_SHOWN_RVA,
            g_dluma_cmm_screen_shown_prefix,
            DLUMA_ARRAY_COUNT(g_dluma_cmm_screen_shown_prefix)) ||
        !dluma_cmm_bytes(
            g_dluma_cmm_runtime.gamedll_base + DLUMA_CMM_SCREEN_HIDDEN_RVA,
            g_dluma_cmm_screen_hidden_prefix,
            DLUMA_ARRAY_COUNT(g_dluma_cmm_screen_hidden_prefix)) ||
        !dluma_cmm_bytes(
            g_dluma_cmm_runtime.gamedll_base + DLUMA_CMM_ACTIVATE_RVA,
            g_dluma_cmm_activate_prefix,
            DLUMA_ARRAY_COUNT(g_dluma_cmm_activate_prefix)) ||
        !dluma_cmm_bytes(
            g_dluma_cmm_runtime.gamedll_base +
                DLUMA_CMM_PLAY_BACKGROUND_MUSIC_RVA,
            g_dluma_cmm_play_background_music_prefix,
            DLUMA_ARRAY_COUNT(g_dluma_cmm_play_background_music_prefix)) ||
        !dluma_cmm_bytes(
            g_dluma_cmm_runtime.gamedll_base +
                DLUMA_CMM_STOP_BACKGROUND_MUSIC_RVA,
            g_dluma_cmm_stop_background_music_prefix,
            DLUMA_ARRAY_COUNT(g_dluma_cmm_stop_background_music_prefix)) ||
        !dluma_cmm_bytes(
            g_dluma_cmm_runtime.gamedll_base + DLUMA_CMM_DYNAMIC_CAST_RVA,
            g_dluma_cmm_dynamic_cast_prefix,
            DLUMA_ARRAY_COUNT(g_dluma_cmm_dynamic_cast_prefix)) ||
        !dluma_cmm_type_descriptor(
            g_dluma_cmm_runtime.gamedll_base, gamedll_image_size,
            DLUMA_CMM_LEVEL_TYPE_RVA, ".?AVLevel@@") ||
        !dluma_cmm_type_descriptor(
            g_dluma_cmm_runtime.gamedll_base, gamedll_image_size,
            DLUMA_CMM_MENU_BACKGROUND_TYPE_RVA,
            ".?AVMenu3DBackgroundDI@@") ||
        !dluma_cmm_type_descriptor(
            g_dluma_cmm_runtime.gamedll_base, gamedll_image_size,
            DLUMA_CMM_IGS_OBJECT_TYPE_RVA, ".?AVIGSObject@@") ||
        !dluma_cmm_type_descriptor(
            g_dluma_cmm_runtime.gamedll_base, gamedll_image_size,
            DLUMA_CMM_CONTROL_OBJECT_TYPE_RVA,
            ".?AVIControlObject@@") ||
        !dluma_cmm_type_descriptor(
            g_dluma_cmm_runtime.gamedll_base, gamedll_image_size,
            DLUMA_CMM_CAMERA_POSITION_HELPER_TYPE_RVA,
            ".?AVCameraPostionHelper@@") ||
        !dluma_cmm_type_descriptor(
            g_dluma_cmm_runtime.gamedll_base, gamedll_image_size,
            DLUMA_CMM_MENU_CAMERA_HELPER_TYPE_RVA,
            ".?AVCameraHelperMenuScene3dDI@@"))
        return 0;
    if (!dluma_cmm_bytes(
            g_dluma_cmm_runtime.gamedll_base +
                DLUMA_CMM_MAIN_INITIALIZE_UI_RVA,
            g_dluma_cmm_main_initialize_ui_prefix,
            DLUMA_ARRAY_COUNT(g_dluma_cmm_main_initialize_ui_prefix)) ||
        !dluma_cmm_bytes(
            g_dluma_cmm_runtime.gamedll_base +
                DLUMA_CMM_MAIN_REGISTER_UI_ELEMENTS_RVA,
            g_dluma_cmm_main_register_ui_elements_prefix,
            DLUMA_ARRAY_COUNT(g_dluma_cmm_main_register_ui_elements_prefix)))
        return 0;
    if (!dluma_cmm_export(
            g_dluma_cmm_runtime.engine, engine_image_size,
            "?GetObjectByID@ILevel@@QEBAPEAVIGSObject@@H@Z",
            DLUMA_CMM_GET_OBJECT_BY_ID_RVA, g_dluma_cmm_get_object_prefix,
            DLUMA_ARRAY_COUNT(g_dluma_cmm_get_object_prefix), &procedure))
        return 0;
    g_dluma_cmm_runtime.get_object_by_id =
        (DlumaCmmGetObjectByIdFn)procedure;
    if (!dluma_cmm_export(
            g_dluma_cmm_runtime.engine, engine_image_size,
            "?GetPrev@IGSObject@@QEBAPEAV1@XZ",
            DLUMA_CMM_GET_PREV_RVA, g_dluma_cmm_get_prev_prefix,
            DLUMA_ARRAY_COUNT(g_dluma_cmm_get_prev_prefix), &procedure))
        return 0;
    g_dluma_cmm_runtime.get_prev = (DlumaCmmObjectLinkFn)procedure;
    if (!dluma_cmm_export(
            g_dluma_cmm_runtime.engine, engine_image_size,
            "?GetNext@IGSObject@@QEBAPEAV1@XZ",
            DLUMA_CMM_GET_NEXT_RVA, g_dluma_cmm_get_next_prefix,
            DLUMA_ARRAY_COUNT(g_dluma_cmm_get_next_prefix), &procedure))
        return 0;
    g_dluma_cmm_runtime.get_next = (DlumaCmmObjectLinkFn)procedure;
    if (!dluma_cmm_export(
            g_dluma_cmm_runtime.engine, engine_image_size,
            "?GetLevel@IGSObject@@QEBAPEAVILevel@@XZ",
            DLUMA_CMM_GET_LEVEL_RVA, g_dluma_cmm_get_level_prefix,
            DLUMA_ARRAY_COUNT(g_dluma_cmm_get_level_prefix), &procedure))
        return 0;
    g_dluma_cmm_runtime.get_level = (DlumaCmmGetLevelFn)procedure;
    if (!dluma_cmm_export(
            g_dluma_cmm_runtime.engine, engine_image_size,
            "?GetID@IGSObject@@QEBAHXZ", DLUMA_CMM_GET_ID_RVA,
            g_dluma_cmm_get_id_prefix,
            DLUMA_ARRAY_COUNT(g_dluma_cmm_get_id_prefix), &procedure))
        return 0;
    g_dluma_cmm_runtime.get_id = (DlumaCmmGetIdFn)procedure;
    if (!dluma_cmm_export(
            g_dluma_cmm_runtime.engine, engine_image_size,
            "?GetScreenId@IUIScreen@@QEBAHXZ",
            DLUMA_CMM_GET_SCREEN_ID_RVA,
            g_dluma_cmm_get_screen_id_prefix,
            DLUMA_ARRAY_COUNT(g_dluma_cmm_get_screen_id_prefix),
            &procedure))
        return 0;
    g_dluma_cmm_runtime.get_screen_id =
        (DlumaCmmGetScreenIdFn)procedure;
    if (!dluma_cmm_export(
            g_dluma_cmm_runtime.engine, engine_image_size,
            "?GetScriptName@IUIScreen@@QEAAAEBV?$string_base@D@ttl@@XZ",
            DLUMA_CMM_GET_SCRIPT_NAME_RVA,
            g_dluma_cmm_get_script_name_prefix,
            DLUMA_ARRAY_COUNT(g_dluma_cmm_get_script_name_prefix),
            &procedure))
        return 0;
    g_dluma_cmm_runtime.get_script_name =
        (DlumaCmmGetScriptNameFn)procedure;
    if (!dluma_cmm_export(
            g_dluma_cmm_runtime.engine, engine_image_size,
            "?GetWorldXform@IControlObject@@QEBAAEBVmtx34@@XZ",
            DLUMA_CMM_GET_WORLD_XFORM_RVA,
            g_dluma_cmm_get_world_xform_prefix,
            DLUMA_ARRAY_COUNT(g_dluma_cmm_get_world_xform_prefix),
            &procedure))
        return 0;
    g_dluma_cmm_runtime.get_world_xform =
        (DlumaCmmGetWorldXformFn)procedure;
    if (!dluma_cmm_export(
            g_dluma_cmm_runtime.engine, engine_image_size,
            "?ActivateView@ILevel@@QEAAXPEAVCLevelView@@@Z",
            DLUMA_CMM_ACTIVATE_VIEW_RVA,
            g_dluma_cmm_activate_view_prefix,
            DLUMA_ARRAY_COUNT(g_dluma_cmm_activate_view_prefix),
            &procedure))
        return 0;
    g_dluma_cmm_runtime.activate_view =
        (DlumaCmmActivateViewFn)procedure;
    if (!dluma_cmm_export(
            g_dluma_cmm_runtime.engine, engine_image_size,
            "?GetViewCamera@ILevel@@QEAAPEAVIBaseCamera@@PEAVCLevelView@@@Z",
            DLUMA_CMM_GET_VIEW_CAMERA_RVA,
            g_dluma_cmm_get_view_camera_prefix,
            DLUMA_ARRAY_COUNT(g_dluma_cmm_get_view_camera_prefix),
            &procedure))
        return 0;
    g_dluma_cmm_runtime.get_view_camera =
        (DlumaCmmGetViewCameraFn)procedure;
    if (!dluma_cmm_export(
            g_dluma_cmm_runtime.engine, engine_image_size,
            "?GetWorldPosition@IControlObject@@QEBA?AVvec3@@XZ",
            DLUMA_CMM_GET_WORLD_POSITION_RVA,
            g_dluma_cmm_get_world_position_prefix,
            DLUMA_ARRAY_COUNT(g_dluma_cmm_get_world_position_prefix),
            &procedure))
        return 0;
    g_dluma_cmm_runtime.get_world_position =
        (DlumaCmmGetControlVectorFn)procedure;
    if (!dluma_cmm_export(
            g_dluma_cmm_runtime.engine, engine_image_size,
            "?GetUpVector@IControlObject@@QEBA?AVvec3@@XZ",
            DLUMA_CMM_GET_UP_VECTOR_RVA,
            g_dluma_cmm_get_up_vector_prefix,
            DLUMA_ARRAY_COUNT(g_dluma_cmm_get_up_vector_prefix),
            &procedure))
        return 0;
    g_dluma_cmm_runtime.get_up_vector =
        (DlumaCmmGetControlVectorFn)procedure;
    if (!dluma_cmm_export(
            g_dluma_cmm_runtime.engine, engine_image_size,
            "?GetForwardVector@IControlObject@@QEBA?AVvec3@@XZ",
            DLUMA_CMM_GET_FORWARD_VECTOR_RVA,
            g_dluma_cmm_get_forward_vector_prefix,
            DLUMA_ARRAY_COUNT(g_dluma_cmm_get_forward_vector_prefix),
            &procedure))
        return 0;
    g_dluma_cmm_runtime.get_forward_vector =
        (DlumaCmmGetControlVectorFn)procedure;
    if (!dluma_cmm_export(
            g_dluma_cmm_runtime.engine, engine_image_size,
            "?GetFOV@IBaseCamera@@QEAAMXZ", DLUMA_CMM_GET_FOV_RVA,
            g_dluma_cmm_get_fov_prefix,
            DLUMA_ARRAY_COUNT(g_dluma_cmm_get_fov_prefix), &procedure))
        return 0;
    g_dluma_cmm_runtime.get_fov = (DlumaCmmGetFovFn)procedure;
    if (!dluma_cmm_export(
            g_dluma_cmm_runtime.engine, engine_image_size,
            "?FromForwardUpPos@IBaseCamera@@UEAAXAEBVvec3@@00@Z",
            DLUMA_CMM_FROM_FORWARD_UP_POS_RVA,
            g_dluma_cmm_from_forward_up_pos_prefix,
            DLUMA_ARRAY_COUNT(g_dluma_cmm_from_forward_up_pos_prefix),
            &procedure))
        return 0;
    g_dluma_cmm_runtime.from_forward_up_pos =
        (DlumaCmmFromForwardUpPosFn)procedure;
    if (!dluma_cmm_export(
            g_dluma_cmm_runtime.engine, engine_image_size,
            "?SetFOV@IBaseCamera@@QEAAXM@Z", DLUMA_CMM_SET_FOV_RVA,
            g_dluma_cmm_set_fov_prefix,
            DLUMA_ARRAY_COUNT(g_dluma_cmm_set_fov_prefix), &procedure))
        return 0;
    g_dluma_cmm_runtime.set_fov = (DlumaCmmSetFovFn)procedure;
    if (!dluma_cmm_export(
            g_dluma_cmm_runtime.engine, engine_image_size,
            "?ForceFrame@IBaseCamera@@QEAAXXZ",
            DLUMA_CMM_FORCE_FRAME_RVA,
            g_dluma_cmm_force_frame_prefix,
            DLUMA_ARRAY_COUNT(g_dluma_cmm_force_frame_prefix),
            &procedure))
        return 0;
    g_dluma_cmm_runtime.force_frame = (DlumaCmmForceFrameFn)procedure;
    if (manifest && manifest->use_custom_textures)
    {
        if (!dluma_cmm_export(
                g_dluma_cmm_runtime.engine, engine_image_size,
                "?LoadUITextures@ILevel@@QEAAXAEBV?$string_base@D@ttl@@@Z",
                DLUMA_CMM_LOAD_UI_TEXTURES_RVA,
                g_dluma_cmm_load_ui_textures_prefix,
                DLUMA_ARRAY_COUNT(g_dluma_cmm_load_ui_textures_prefix),
                &procedure))
            return 0;
        g_dluma_cmm_runtime.load_ui_textures =
            (DlumaCmmLoadUiTexturesFn)procedure;
        if (!dluma_cmm_export(
                g_dluma_cmm_runtime.engine, engine_image_size,
                "?UnloadUITextures@ILevel@@QEAAXAEBV?$string_base@D@ttl@@@Z",
                DLUMA_CMM_UNLOAD_UI_TEXTURES_RVA,
                g_dluma_cmm_unload_ui_textures_prefix,
                DLUMA_ARRAY_COUNT(g_dluma_cmm_unload_ui_textures_prefix),
                &procedure))
            return 0;
        g_dluma_cmm_runtime.unload_ui_textures =
            (DlumaCmmUnloadUiTexturesFn)procedure;
    }
    if (!dluma_cmm_bind_fx_enable(engine_image_size))
        dluma_log_pair(
            "WARN", "custom_main_menu_fx_profile",
            "exact_export_or_backing_state_unavailable_visual_bridge_retained");
    if (!dluma_cmm_bind_fx_lifecycle(
            engine_image_size, gamedll_image_size))
        dluma_log_pair(
            "WARN", "custom_main_menu_fx_profile",
            "exact_emitter_lifecycle_unavailable_custom_visual_retained");
    /* The content-update entry points below are bound only for profile
     * validation and the content-object ABI check.  UI-texture exports above
     * are invoked by the pre-registration hook when the manifest requests
     * them. */
    if (!dluma_cmm_bind_content_publication(
            engine_image_size, gamedll_image_size))
        return 0;
    g_dluma_cmm_runtime.dynamic_cast =
        (DlumaCmmDynamicCastFn)(g_dluma_cmm_runtime.gamedll_base +
                              DLUMA_CMM_DYNAMIC_CAST_RVA);
    g_dluma_cmm_runtime.load_original =
        (DlumaCmmLoadFn)(g_dluma_cmm_runtime.gamedll_base +
                       DLUMA_CMM_LOAD_RVA);
    g_dluma_cmm_runtime.unload_original =
        (DlumaCmmUnloadFn)(g_dluma_cmm_runtime.gamedll_base +
                         DLUMA_CMM_UNLOAD_RVA);
    g_dluma_cmm_runtime.screen_shown_original =
        (DlumaCmmScreenEventFn)(g_dluma_cmm_runtime.gamedll_base +
                              DLUMA_CMM_SCREEN_SHOWN_RVA);
    g_dluma_cmm_runtime.screen_hidden_original =
        (DlumaCmmScreenEventFn)(g_dluma_cmm_runtime.gamedll_base +
                              DLUMA_CMM_SCREEN_HIDDEN_RVA);
    g_dluma_cmm_runtime.activate_target =
        g_dluma_cmm_runtime.gamedll_base + DLUMA_CMM_ACTIVATE_RVA;
    g_dluma_cmm_runtime.main_initialize_ui_original =
        (DlumaCmmInitializeUiFn)(g_dluma_cmm_runtime.gamedll_base +
                               DLUMA_CMM_MAIN_INITIALIZE_UI_RVA);
    g_dluma_cmm_runtime.main_register_ui_elements_original =
        (DlumaCmmUiPhaseFn)(g_dluma_cmm_runtime.gamedll_base +
                          DLUMA_CMM_MAIN_REGISTER_UI_ELEMENTS_RVA);
    g_dluma_cmm_runtime.play_background_music =
        (DlumaCmmMainMenuMusicFn)(g_dluma_cmm_runtime.gamedll_base +
                                DLUMA_CMM_PLAY_BACKGROUND_MUSIC_RVA);
    g_dluma_cmm_runtime.stop_background_music =
        (DlumaCmmMainMenuMusicFn)(g_dluma_cmm_runtime.gamedll_base +
                                DLUMA_CMM_STOP_BACKGROUND_MUSIC_RVA);
    g_dluma_cmm_runtime.main_initialize_ui_slot =
        (void **)(g_dluma_cmm_runtime.gamedll_base +
                  DLUMA_CMM_MAIN_INITIALIZE_UI_SLOT_RVA);
    g_dluma_cmm_runtime.main_register_ui_elements_slot =
        (void **)(g_dluma_cmm_runtime.gamedll_base +
                  DLUMA_CMM_MAIN_REGISTER_UI_ELEMENTS_SLOT_RVA);
    g_dluma_cmm_runtime.load_slot =
        (void **)(g_dluma_cmm_runtime.gamedll_base +
                  DLUMA_CMM_LOAD_SLOT_RVA);
    g_dluma_cmm_runtime.unload_slot =
        (void **)(g_dluma_cmm_runtime.gamedll_base +
                  DLUMA_CMM_UNLOAD_SLOT_RVA);
    g_dluma_cmm_runtime.screen_shown_slot =
        (void **)(g_dluma_cmm_runtime.gamedll_base +
                  DLUMA_CMM_SCREEN_SHOWN_SLOT_RVA);
    g_dluma_cmm_runtime.screen_hidden_slot =
        (void **)(g_dluma_cmm_runtime.gamedll_base +
                  DLUMA_CMM_SCREEN_HIDDEN_SLOT_RVA);
    return dluma_is_readable(
               g_dluma_cmm_runtime.load_slot, 2U * sizeof(void *)) &&
           dluma_is_readable(
               g_dluma_cmm_runtime.screen_shown_slot,
               2U * sizeof(void *)) &&
           dluma_is_readable(
               g_dluma_cmm_runtime.main_initialize_ui_slot,
               2U * sizeof(void *)) &&
           *g_dluma_cmm_runtime.load_slot ==
               (void *)g_dluma_cmm_runtime.load_original &&
           *g_dluma_cmm_runtime.unload_slot ==
               (void *)g_dluma_cmm_runtime.unload_original &&
           *g_dluma_cmm_runtime.screen_shown_slot ==
               (void *)g_dluma_cmm_runtime.screen_shown_original &&
           *g_dluma_cmm_runtime.screen_hidden_slot ==
               (void *)g_dluma_cmm_runtime.screen_hidden_original &&
           *g_dluma_cmm_runtime.main_initialize_ui_slot ==
               (void *)g_dluma_cmm_runtime.main_initialize_ui_original &&
           *g_dluma_cmm_runtime.main_register_ui_elements_slot ==
               (void *)g_dluma_cmm_runtime.main_register_ui_elements_original &&
           g_dluma_cmm_runtime.main_register_ui_elements_slot ==
               g_dluma_cmm_runtime.main_initialize_ui_slot + 1;
}

int dluma_custom_main_menu_runtime_start(HMODULE gamedll)
{
    HMODULE pinned = 0;
    if (!dluma_custom_main_menu_selected_ready())
        return 1;
    if (g_dluma_cmm_runtime.started)
        return g_dluma_cmm_runtime.load_installed &&
               g_dluma_cmm_runtime.unload_installed &&
               g_dluma_cmm_runtime.screen_shown_installed &&
               g_dluma_cmm_runtime.screen_hidden_installed &&
               g_dluma_cmm_runtime.activate_installed &&
               g_dluma_cmm_runtime.main_initialize_ui_installed;
    memset(&g_dluma_cmm_runtime, 0, sizeof(g_dluma_cmm_runtime));
    dluma_custom_main_menu_music_reset();
    if (!dluma_cmm_prepare_content_descriptor())
    {
        dluma_log_pair(
            "WARN", "custom_main_menu_content_descriptor",
            "selected_project_root_invalid_stock_frontend_retained");
        return 0;
    }
    if (!GetModuleHandleExW(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                GET_MODULE_HANDLE_EX_FLAG_PIN,
            (LPCWSTR)(ULONG_PTR)dluma_custom_main_menu_runtime_start,
            &pinned) ||
        !dluma_cmm_validate_profile(gamedll))
    {
        dluma_log_pair("WARN", "custom_main_menu_runtime_profile",
                     "unsupported_or_validation_failed");
        return 0;
    }
    dluma_cmm_lock(&g_dluma_cmm_runtime.lifecycle_lock);
    if (!dluma_cmm_install_vtable_hooks() ||
        !dluma_cmm_install_screen_hooks() ||
        !dluma_cmm_install_activate_hook() ||
        !dluma_cmm_install_main_ui_initializer_hook())
    {
        int initializer_restored =
            dluma_cmm_restore_main_ui_initializer_hook();
        int activate_restored = dluma_cmm_restore_activate_hook();
        int screen_restored = dluma_cmm_restore_screen_hooks();
        int vtables_restored = dluma_cmm_restore_vtable_hooks();
        if (!initializer_restored || !activate_restored ||
            !screen_restored || !vtables_restored ||
            g_dluma_cmm_runtime.load_installed ||
            g_dluma_cmm_runtime.unload_installed ||
            g_dluma_cmm_runtime.screen_shown_installed ||
            g_dluma_cmm_runtime.screen_hidden_installed ||
            g_dluma_cmm_runtime.activate_installed ||
            g_dluma_cmm_runtime.main_initialize_ui_installed ||
            g_dluma_cmm_runtime.main_texture_hook_installed ||
            g_dluma_cmm_runtime.teardown_failed)
        {
            g_dluma_cmm_runtime.started = 1U;
            dluma_atomic_exchange(&g_dluma_cmm_runtime.stopping, 1);
        }
        dluma_cmm_unlock(&g_dluma_cmm_runtime.lifecycle_lock);
        dluma_log_pair("WARN", "custom_main_menu_runtime_hooks",
                     "installation_failed_stock_retained");
        return 0;
    }
    g_dluma_cmm_runtime.started = 1U;
    dluma_atomic_exchange(
        &g_dluma_cmm_runtime.publication_state,
        DLUMA_CMM_PUBLICATION_AWAITING_UI_INITIALIZER);
    dluma_log_pair(
        "INFO", "custom_main_menu_publication_state_to",
        "awaiting_ui_initializer");
    if (dluma_cmm_install_music_assign_hook())
        dluma_log_pair(
            "INFO", "custom_main_menu_music_hook", "installed");
    else
        dluma_log_pair(
            "WARN", "custom_main_menu_music_hook",
            g_dluma_cmm_runtime.music_assign_installed ?
                "unsafe_install_state_relay_retained" :
                "unavailable_stock_dw_menu_retained");
    dluma_cmm_unlock(&g_dluma_cmm_runtime.lifecycle_lock);
    dluma_log_pair("INFO", "custom_main_menu_runtime_profile",
                 "exact_player_profile_validated");
    dluma_log_pair("INFO", "custom_main_menu_runtime_hooks", "installed");
    return 1;
}

static int dluma_cmm_content_tools_live(void **tools_out)
{
    void *tools = 0;
    void **tools_vtable = 0;
    void *prepare_target = 0;
    void *packages_target = 0;
    void *locale_target = 0;
    void *speech_target = 0;
    void *publish_target = 0;
    if (tools_out) *tools_out = 0;
    if (!tools_out ||
        !g_dluma_cmm_runtime.content_route_available ||
        !g_dluma_cmm_runtime.tools_global ||
        !g_dluma_cmm_runtime.premount_prepare ||
        !g_dluma_cmm_runtime.premount_packages ||
        !g_dluma_cmm_runtime.get_locale_id ||
        !g_dluma_cmm_runtime.get_speech_id ||
        !g_dluma_cmm_runtime.publish_content ||
        !g_dluma_cmm_runtime.engine_free)
    {
        dluma_log_pair(
            "WARN", "custom_main_menu_stock_premount_precondition",
            "route_binding_unavailable");
        return -1;
    }
    __try
    {
        tools = *g_dluma_cmm_runtime.tools_global;
        if (!tools)
        {
            dluma_log_pair(
                "WARN", "custom_main_menu_stock_premount_precondition",
                "tools_not_constructed");
            __leave;
        }
        if (!dluma_is_readable(tools, sizeof(void *)))
        {
            dluma_log_pair(
                "WARN", "custom_main_menu_stock_premount_precondition",
                "tools_unreadable");
            return -1;
        }
        tools_vtable = *(void ***)tools;
        if (!tools_vtable ||
            tools_vtable !=
                (void **)(g_dluma_cmm_runtime.engine_base +
                          DLUMA_CMM_TOOLS_VTABLE_RVA) ||
            !dluma_is_readable(
                (BYTE *)tools_vtable +
                    DLUMA_CMM_TOOLS_PREMOUNT_PREPARE_SLOT,
                sizeof(void *)) ||
            !dluma_is_readable(
                (BYTE *)tools_vtable +
                    DLUMA_CMM_TOOLS_PREMOUNT_PACKAGES_SLOT,
                sizeof(void *)) ||
            !dluma_is_readable(
                (BYTE *)tools_vtable + DLUMA_CMM_TOOLS_GET_LOCALE_SLOT,
                sizeof(void *)) ||
            !dluma_is_readable(
                (BYTE *)tools_vtable + DLUMA_CMM_TOOLS_GET_SPEECH_SLOT,
                sizeof(void *)) ||
            !dluma_is_readable(
                (BYTE *)tools_vtable + DLUMA_CMM_TOOLS_PUBLISH_CONTENT_SLOT,
                sizeof(void *)))
        {
            dluma_log_pair(
                "WARN", "custom_main_menu_stock_premount_precondition",
                "tools_abi_mismatch");
            return -1;
        }
        prepare_target = *(void **)(
            (BYTE *)tools_vtable +
                DLUMA_CMM_TOOLS_PREMOUNT_PREPARE_SLOT);
        packages_target = *(void **)(
            (BYTE *)tools_vtable +
                DLUMA_CMM_TOOLS_PREMOUNT_PACKAGES_SLOT);
        locale_target = *(void **)(
            (BYTE *)tools_vtable + DLUMA_CMM_TOOLS_GET_LOCALE_SLOT);
        speech_target = *(void **)(
            (BYTE *)tools_vtable + DLUMA_CMM_TOOLS_GET_SPEECH_SLOT);
        publish_target = *(void **)(
            (BYTE *)tools_vtable + DLUMA_CMM_TOOLS_PUBLISH_CONTENT_SLOT);
        if (prepare_target !=
                (void *)g_dluma_cmm_runtime.premount_prepare ||
            packages_target !=
                (void *)g_dluma_cmm_runtime.premount_packages ||
            locale_target != (void *)g_dluma_cmm_runtime.get_locale_id ||
            speech_target != (void *)g_dluma_cmm_runtime.get_speech_id ||
            publish_target != (void *)g_dluma_cmm_runtime.publish_content ||
            !dluma_cmm_executable(prepare_target, 1U) ||
            !dluma_cmm_executable(packages_target, 1U) ||
            !dluma_cmm_executable(locale_target, 1U) ||
            !dluma_cmm_executable(speech_target, 1U) ||
            !dluma_cmm_executable(publish_target, 1U))
        {
            dluma_log_pair(
                "WARN", "custom_main_menu_stock_premount_precondition",
                "premount_target_mismatch");
            return -1;
        }
        *tools_out = tools;
        return 1;
    }
    __except (DLUMA_CMM_EXCEPTION_EXECUTE_HANDLER)
    {
        dluma_log_pair(
            "WARN", "custom_main_menu_stock_premount_precondition",
            "read_exception");
        return -1;
    }
    return 0;
}

static int dluma_cmm_reset_package_descriptor(void)
{
    SIZE_T length = strlen(g_dluma_cmm_runtime.content_name_storage);
    if (!length || length + 1U >
            DLUMA_ARRAY_COUNT(g_dluma_cmm_runtime.package_name_storage))
        return 0;
    memcpy(
        g_dluma_cmm_runtime.package_name_storage,
        g_dluma_cmm_runtime.content_name_storage, length + 1U);
    g_dluma_cmm_runtime.package_name.data =
        g_dluma_cmm_runtime.package_name_storage;
    g_dluma_cmm_runtime.package_name.size = (unsigned int)length;
    g_dluma_cmm_runtime.package_name.capacity =
        (unsigned int)(
            DLUMA_ARRAY_COUNT(g_dluma_cmm_runtime.package_name_storage) - 1U);
    return dluma_cmm_ttl_valid(&g_dluma_cmm_runtime.package_name);
}

/* Execute the resource-publication portion of the supported Player's stock
 * custom-content transaction.  Exact Player evidence proves the order
 * +0x1AC8(project, identity) -> +0x1AE8(project) -> GetSpeechID ->
 * GetLocaleID -> +0x1408(locale, speech).  The last call dispatches the
 * storage publication that creates the project/Data/Assets_PC sources and
 * updates their resource catalogs. */
static int dluma_cmm_run_stock_premount(void)
{
    void *tools = 0;
    DlumaCmmTtlString locale;
    DlumaCmmTtlString speech;
    DlumaCmmTtlString *locale_result = 0;
    DlumaCmmTtlString *speech_result = 0;
    int live;
    int published = 0;
    memset(&locale, 0, sizeof(locale));
    memset(&speech, 0, sizeof(speech));
    if (g_dluma_cmm_runtime.content_publish_called)
        return 0;
    live = dluma_cmm_content_tools_live(&tools);
    if (live != 1)
    {
        dluma_log_pair(
            "WARN", "custom_main_menu_stock_premount_failure",
            live == 0 ? "engine_objects_not_ready" :
                        "engine_object_validation_failed");
        return 0;
    }
    if (!dluma_cmm_reset_package_descriptor())
    {
        dluma_log_pair(
            "WARN", "custom_main_menu_stock_premount_failure",
            "package_descriptor_invalid");
        return 0;
    }
    if (!dluma_custom_main_menu_publication_scope_enter())
    {
        dluma_log_pair(
            "WARN", "custom_main_menu_stock_premount_failure",
            "publication_scope_unavailable");
        return 0;
    }
    ++g_dluma_cmm_runtime.content_prepare_attempt_count;
    g_dluma_cmm_runtime.content_exception_code = 0;
    g_dluma_cmm_runtime.content_exception_address = 0;
    __try
    {
        __try
        {
            if (!dluma_cmm_ttl_valid(&g_dluma_cmm_runtime.content_name) ||
                g_dluma_cmm_runtime.content_name.data !=
                    g_dluma_cmm_runtime.content_name_storage)
                __leave;
            if (!dluma_cmm_ttl_valid(&g_dluma_cmm_runtime.package_name))
                __leave;
            g_dluma_cmm_runtime.content_failure_phase =
                "stock_premount_prepare";
            g_dluma_cmm_runtime.premount_prepare(
                tools, &g_dluma_cmm_runtime.package_name,
                g_dluma_cmm_runtime.content_identity);
            g_dluma_cmm_runtime.content_failure_phase =
                "stock_premount_packages";
            g_dluma_cmm_runtime.premount_packages(
                tools, &g_dluma_cmm_runtime.package_name);
            g_dluma_cmm_runtime.content_failure_phase =
                "stock_get_speech_id";
            speech_result = g_dluma_cmm_runtime.get_speech_id(
                tools, &speech);
            if (speech_result != &speech || !dluma_cmm_ttl_valid(&speech))
                __leave;
            g_dluma_cmm_runtime.content_failure_phase =
                "stock_get_locale_id";
            locale_result = g_dluma_cmm_runtime.get_locale_id(
                tools, &locale);
            if (locale_result != &locale || !dluma_cmm_ttl_valid(&locale))
                __leave;
            g_dluma_cmm_runtime.content_failure_phase =
                "stock_publish_locale_speech_resources";
            g_dluma_cmm_runtime.publish_content(
                tools, &locale, &speech);
            g_dluma_cmm_runtime.content_failure_phase = 0;
            published = 1;
        }
        __finally
        {
            if (locale.data)
            {
                g_dluma_cmm_runtime.engine_free(locale.data);
                memset(&locale, 0, sizeof(locale));
            }
            if (speech.data)
            {
                g_dluma_cmm_runtime.engine_free(speech.data);
                memset(&speech, 0, sizeof(speech));
            }
            dluma_custom_main_menu_publication_scope_leave();
        }
    }
    __except (dluma_cmm_content_exception_filter(GetExceptionInformation()))
    {
        published = 0;
    }
    if (!published)
    {
        dluma_log_pair(
            "WARN", "custom_main_menu_stock_premount_exception_phase",
            g_dluma_cmm_runtime.content_failure_phase ?
                g_dluma_cmm_runtime.content_failure_phase : "unknown");
        dluma_cmm_log_content_exception();
        return 0;
    }
    g_dluma_cmm_runtime.content_publish_called = 1U;
    dluma_log_pair(
        "INFO", "custom_main_menu_stock_premount",
        "prepare_packages_locale_speech_resources_published");
    return 1;
}

static void dluma_cmm_log_published_source(
    const char *owner_id, const char *label)
{
    void *source =
        dluma_custom_main_menu_source_object_for_owner(owner_id);
    unsigned int flags =
        dluma_custom_main_menu_source_flags_for_owner(owner_id);
    dluma_log_u64(
        "INFO", label,
        (ULONGLONG)(ULONG_PTR)source);
    dluma_log_u64(
        "INFO", "custom_main_menu_native_source_validated_flags",
        flags);
}

static int dluma_cmm_validate_stock_publication(void)
{
    int captured;
    int inventory_bound;
    int sources_refreshed;
    /* The isolation fix owns stronger in-call source observations when it is
     * enabled, but custom-menu publication must not depend on that optional
     * policy.  Immediately after the exact synchronous stock pre-mount
     * transaction, adopt the exact project/data/assets objects from the
     * filesystem registry when the observers were not installed.  The stock
     * transaction and exact-build profile establish the per-kind flags; the
     * adopter still revalidates canonical directory identity, exact registry
     * membership, distinct objects, and process-lifetime ownership. */
    captured = dluma_custom_main_menu_native_sources_ready() ||
        dluma_custom_main_menu_capture_published_sources();
    inventory_bound = g_dluma_cmm_runtime.frontend_generation &&
        dluma_custom_main_menu_bind_live_source_inventory();
    sources_refreshed = inventory_bound &&
        dluma_custom_main_menu_refresh_live_sources(
            g_dluma_cmm_runtime.frontend_generation);
    g_dluma_cmm_runtime.content_engine_sources_observed =
        (unsigned char)(captured != 0);
    if (!captured)
        dluma_log_pair(
            "WARN", "custom_main_menu_native_source_validation",
            "engine_source_capture_incomplete_custom_presentation_retained");
    else if (!inventory_bound || !sources_refreshed)
        dluma_log_pair(
            "WARN", "custom_main_menu_native_source_validation",
            "ownership_join_pending_custom_presentation_retained");
    dluma_cmm_log_published_source(
        DLUMA_SOURCE_OWNER_ID_CUSTOM_MAIN_MENU_PROJECT,
        "custom_main_menu_project_source_object");
    dluma_cmm_log_published_source(
        DLUMA_SOURCE_OWNER_ID_CUSTOM_MAIN_MENU_DATA,
        "custom_main_menu_data_source_object");
    if (dluma_custom_main_menu_source_object_for_owner(
            DLUMA_SOURCE_OWNER_ID_CUSTOM_MAIN_MENU_ASSETS_PC))
        dluma_cmm_log_published_source(
            DLUMA_SOURCE_OWNER_ID_CUSTOM_MAIN_MENU_ASSETS_PC,
            "custom_main_menu_assets_pc_source_object");
    if (captured)
        dluma_log_pair(
            "INFO", "custom_main_menu_native_source_validation",
            "stock_project_data_assets_sources_observed_after_publication");
    return 1;
}

static int dluma_cmm_mount_overlay_after_catalogs(void)
{
    int inventory_bound;
    int sources_refreshed;
    if (!dluma_custom_main_menu_mount_selected_sources())
        return 0;
    inventory_bound = dluma_custom_main_menu_bind_live_source_inventory();
    sources_refreshed = inventory_bound &&
        dluma_custom_main_menu_refresh_live_sources(
            g_dluma_cmm_runtime.frontend_generation);
    if (!inventory_bound || !sources_refreshed ||
        !dluma_custom_main_menu_sources_ready())
        dluma_log_pair(
            "WARN", "custom_main_menu_source_ownership",
            "live_join_incomplete_overlay_and_presentation_retained");
    dluma_cmm_log_published_source(
        DLUMA_SOURCE_OWNER_ID_CUSTOM_MAIN_MENU_OVERLAY,
        "custom_main_menu_overlay_source_object");
    g_dluma_cmm_runtime.content_activated = 1U;
    g_dluma_cmm_runtime.content_sources_mounted = 1U;
    dluma_cmm_lock(&g_dluma_cmm_runtime.generation_lock);
    g_dluma_cmm_runtime.generation_active = 1U;
    dluma_cmm_unlock(&g_dluma_cmm_runtime.generation_lock);
    dluma_log_pair(
        "INFO", "custom_main_menu_content_publication",
        g_dluma_cmm_runtime.content_engine_sources_observed
            ? "synchronous_stock_completion_observed_then_overlay_mounted"
            : "stock_publication_degraded_overlay_mounted_presentation_retained");
    return 1;
}


static void dluma_cmm_log_screen_observation(
    unsigned int count, const char *top_script)
{
    LONG shown = dluma_atomic_compare_exchange(
        &g_dluma_cmm_runtime.screen_shown_events, 0, 0);
    LONG hidden = dluma_atomic_compare_exchange(
        &g_dluma_cmm_runtime.screen_hidden_events, 0, 0);
    int changed = !g_dluma_cmm_runtime.screen_observation_logged ||
                  count != g_dluma_cmm_runtime.logged_visible_screen_count ||
                  shown != g_dluma_cmm_runtime.logged_screen_shown_events ||
                  hidden != g_dluma_cmm_runtime.logged_screen_hidden_events ||
                  dluma_stricmp(
                      top_script ? top_script : "",
                      g_dluma_cmm_runtime.logged_top_script) != 0;
    if (!changed)
        return;
    g_dluma_cmm_runtime.screen_observation_logged = 1U;
    g_dluma_cmm_runtime.logged_visible_screen_count = count;
    g_dluma_cmm_runtime.logged_screen_shown_events = shown;
    g_dluma_cmm_runtime.logged_screen_hidden_events = hidden;
    dluma_str_copy(
        g_dluma_cmm_runtime.logged_top_script,
        DLUMA_ARRAY_COUNT(g_dluma_cmm_runtime.logged_top_script),
        top_script ? top_script : "");
    dluma_log_u64("INFO", "custom_main_menu_screen_shown_events",
                (ULONGLONG)(unsigned long)shown);
    dluma_log_u64("INFO", "custom_main_menu_screen_hidden_events",
                (ULONGLONG)(unsigned long)hidden);
    dluma_log_u64("INFO", "custom_main_menu_visible_screen_count", count);
    dluma_log_pair(
        "INFO", "custom_main_menu_top_screen",
        top_script && top_script[0] ? top_script : "none");
}

static void dluma_cmm_reconcile_screen_audio(ULONGLONG now_ms)
{
    const DlumaCustomMainMenuManifest *manifest =
        dluma_custom_main_menu_selected_manifest();
    char top_script[DLUMA_CUSTOM_MENU_XUI_NAME_CAPACITY];
    char desired_cue[DLUMA_CUSTOM_MENU_CUE_CAPACITY];
    ULONGLONG empty_since;
    unsigned int count;
    unsigned int index;
    top_script[0] = 0;
    desired_cue[0] = 0;
    if (!manifest || !g_dluma_cmm_runtime.active_module ||
        !g_dluma_cmm_runtime.main_menu_module_published)
    {
        (void)dluma_custom_main_menu_music_reconcile(0);
        return;
    }
    (void)dluma_custom_main_menu_music_prepare(
        g_dluma_cmm_runtime.active_module);
    dluma_cmm_lock(&g_dluma_cmm_runtime.visible_screen_lock);
    count = g_dluma_cmm_runtime.visible_screen_count;
    empty_since = g_dluma_cmm_runtime.empty_stack_since_ms;
    if (count)
        dluma_str_copy(
            top_script, DLUMA_ARRAY_COUNT(top_script),
            g_dluma_cmm_runtime.visible_screens[count - 1U].script);
    dluma_cmm_unlock(&g_dluma_cmm_runtime.visible_screen_lock);
    dluma_cmm_log_screen_observation(count, top_script);
    if (!count)
    {
        if (dluma_custom_main_menu_runtime_empty_stack_grace_active(
                empty_since, now_ms, DLUMA_CMM_EMPTY_STACK_GRACE_MS))
            return;
        (void)dluma_custom_main_menu_music_reconcile(0);
        return;
    }
    for (index = 0; index < manifest->override_count; ++index)
    {
        if (dluma_custom_main_menu_runtime_screen_script_matches(
                top_script, manifest->overrides[index].destination))
        {
            dluma_str_copy(
                desired_cue, DLUMA_ARRAY_COUNT(desired_cue),
                manifest->overrides[index].music_override);
            break;
        }
    }
    (void)dluma_custom_main_menu_music_reconcile(
        desired_cue[0] ? desired_cue : 0);
}


/* The direct active-level query can temporarily report an empty or stale
 * frontend name while a gameplay session already owns the frame.  Consult the
 * media adapter's validated session-level receiver before treating that frame
 * as a frontend return.  This is intentionally private to the custom-menu
 * lifecycle so restoration does not depend on Discord being enabled. */
static int dluma_cmm_session_gameplay_active(void)
{
    DlumaEngineState *state = &g_dluma.engine;
    void *level = 0;
    const char *level_name = 0;
    ULONG_PTR level_vtable = 0;
    char raw[DLUMA_MAX_MAP_NAME * 2];
    char normalized[DLUMA_MAX_MAP_NAME * 2];
    unsigned int i;
    int terminated = 0;
    int delivered = 1;
    if (!state->ready || !state->get_level_name ||
        !dluma_player_media_adapter_get_session_level(&level) ||
        !level || !dluma_is_readable(level, sizeof(level_vtable)))
        return 0;
    memcpy(&level_vtable, level, sizeof(level_vtable));
    if (!level_vtable ||
        !dluma_is_readable((void *)level_vtable, sizeof(void *)))
        return 0;
    __try
    {
        level_name = state->get_level_name(level);
    }
    __except (1)
    {
        delivered = 0;
    }
    if (!delivered || !level_name || !dluma_is_readable(level_name, 1))
        return 0;
    for (i = 0; i < DLUMA_ARRAY_COUNT(raw); ++i)
    {
        if (!dluma_is_readable(level_name + i, 1))
            return 0;
        raw[i] = level_name[i];
        if (!raw[i])
        {
            terminated = 1;
            break;
        }
    }
    if (!terminated || !raw[0])
        return 0;
    dluma_normalize_map_name(
        raw, normalized, DLUMA_ARRAY_COUNT(normalized));
    return normalized[0] &&
        !dluma_frontend_level_name_is_frontend(normalized);
}


void dluma_custom_main_menu_runtime_tick(
    const char *map_name, void *level, ULONGLONG now_ms)
{
    const DlumaCustomMainMenuManifest *manifest =
        dluma_custom_main_menu_selected_manifest();
    int background_still_owned;
    LONG retirement_reason;
    int frontend_observed;
    int gameplay_started;
    int return_ready;
    int frontend_committed;
    int main_music_required = 0;
    unsigned int music_index;
    if (!g_dluma_cmm_runtime.started ||
        dluma_atomic_compare_exchange(
            &g_dluma_cmm_runtime.stopping, 0, 0) != 0)
        return;
    return_ready = dluma_atomic_compare_exchange(
        &g_dluma_cmm_runtime.frontend_return_ready, 0, 0) != 0;
    frontend_committed = dluma_atomic_compare_exchange(
        &g_dluma_cmm_runtime.frontend_committed, 0, 0) != 0;
    if (frontend_committed && dluma_cmm_session_gameplay_active())
    {
        dluma_atomic_exchange(&g_dluma_cmm_runtime.frontend_committed, 0);
        frontend_committed = 0;
    }
    frontend_observed = map_name && map_name[0] &&
        dluma_frontend_level_name_is_frontend(map_name);
    if ((return_ready || frontend_observed) &&
        !dluma_cmm_frontend_restore_allowed())
        return_ready = 0;
    else if (frontend_observed && dluma_atomic_compare_exchange(
                 &g_dluma_cmm_runtime.restoration_pending, 0, 0) != 0)
    {
        dluma_custom_main_menu_runtime_frontend_ready();
        return_ready = dluma_atomic_compare_exchange(
            &g_dluma_cmm_runtime.frontend_return_ready, 0, 0) != 0;
    }
    gameplay_started = !return_ready && !frontend_committed &&
        map_name && map_name[0] &&
        !frontend_observed;
    /* A concrete frontend map name is authoritative.  The media adapter can
     * retain the outgoing gameplay receiver briefly while the frontend is
     * already committed; never let that stale session suppress restoration. */
    if (!return_ready && !frontend_committed &&
        !gameplay_started && !frontend_observed &&
        dluma_cmm_session_gameplay_active())
    {
        gameplay_started = 1;
        frontend_observed = 0;
    }
    retirement_reason =
        dluma_cmm_consume_source_retirement(gameplay_started);
    if (gameplay_started)
    {
        if (!retirement_reason)
            dluma_cmm_retire_frontend_generation(
                "gameplay_started", 1);
        return;
    }
    /* The native menu request owns ordinary returns. When enabled, isolation
     * must also finish cleanup. Neither route needs a fresh active-level name. */
    if (dluma_atomic_compare_exchange(
            &g_dluma_cmm_runtime.restoration_pending, 0, 0) != 0 && return_ready)
        dluma_cmm_restore_frontend_generation(frontend_observed ? level : 0);
    if (dluma_atomic_compare_exchange(
            &g_dluma_cmm_runtime.restoration_pending, 0, 0) != 0 &&
        dluma_atomic_compare_exchange(
            &g_dluma_cmm_runtime.publication_state, 0, 0) !=
                DLUMA_CMM_PUBLICATION_ACTIVE)
        return;
    if (manifest && manifest->use_custom_textures &&
        !g_dluma_cmm_runtime.texture_definitions_loaded &&
        dluma_atomic_compare_exchange(
            &g_dluma_cmm_runtime.restoration_pending, 0, 0) != 0)
    {
        void *ui_owner;
        dluma_cmm_lock(&g_dluma_cmm_runtime.generation_lock);
        ui_owner = g_dluma_cmm_runtime.restoration_ui_level;
        dluma_cmm_unlock(&g_dluma_cmm_runtime.generation_lock);
        if (ui_owner == g_dluma_cmm_runtime.restoration_module &&
            dluma_cmm_main_menu_module_valid(ui_owner))
            (void)dluma_cmm_texture_definitions_load(ui_owner);
    }
    if (g_dluma_cmm_runtime.started &&
        g_dluma_cmm_runtime.custom_background_substituted &&
        g_dluma_cmm_runtime.content_activated &&
        g_dluma_cmm_runtime.content_sources_mounted &&
        (!manifest ||
         dluma_custom_main_menu_runtime_static_camera_bridge_enabled(
             manifest->does_use_3d_camera_movement)) &&
        !g_dluma_cmm_runtime.camera_applied &&
        g_dluma_cmm_runtime.loaded_module &&
        dluma_custom_main_menu_selected_ready() &&
        dluma_atomic_compare_exchange(
            &g_dluma_cmm_runtime.stopping, 0, 0) == 0 &&
        now_ms >= g_dluma_cmm_runtime.next_camera_retry_ms &&
        dluma_is_readable(
            (BYTE *)g_dluma_cmm_runtime.loaded_module +
                DLUMA_CMM_MODULE_BACKGROUND_OFFSET,
            sizeof(void *)) &&
        *(void **)((BYTE *)g_dluma_cmm_runtime.loaded_module +
                   DLUMA_CMM_MODULE_BACKGROUND_OFFSET))
    {
        g_dluma_cmm_runtime.next_camera_retry_ms =
            now_ms + DLUMA_CMM_CAMERA_RETRY_MS;
        dluma_custom_main_menu_mark_activation_pending();
        dluma_cmm_apply_camera(g_dluma_cmm_runtime.loaded_module);
    }
    background_still_owned =
        g_dluma_cmm_runtime.active_module &&
        g_dluma_cmm_runtime.active_background &&
        dluma_is_readable(
            (BYTE *)g_dluma_cmm_runtime.active_module +
                DLUMA_CMM_MODULE_BACKGROUND_OFFSET,
            sizeof(void *)) &&
        *(void **)((BYTE *)g_dluma_cmm_runtime.active_module +
                   DLUMA_CMM_MODULE_BACKGROUND_OFFSET) ==
            g_dluma_cmm_runtime.active_background;
    if (!g_dluma_cmm_runtime.active_background ||
        (!dluma_frontend_level_name_is_frontend(map_name) &&
         !background_still_owned))
    {
        (void)dluma_custom_main_menu_music_reconcile(0);
        return;
    }
    if (dluma_atomic_compare_exchange(
            &g_dluma_cmm_runtime.restoration_pending, 0, 0) != 0)
    {
        if (dluma_atomic_compare_exchange(
                &g_dluma_cmm_runtime.active_calls, 0, 0) != 0)
            return;
        dluma_atomic_exchange(&g_dluma_cmm_runtime.return_audio_ready, 1);
    }
    if ((!g_dluma_cmm_runtime.music_manager_published ||
         !g_dluma_cmm_runtime.main_menu_module_published) &&
        now_ms >= g_dluma_cmm_runtime.next_music_manager_retry_ms)
    {
        g_dluma_cmm_runtime.next_music_manager_retry_ms =
            now_ms + DLUMA_CMM_MUSIC_MANAGER_RETRY_MS;
        if (dluma_cmm_publish_music_manager(
                g_dluma_cmm_runtime.active_module,
                g_dluma_cmm_runtime.active_background))
            (void)dluma_cmm_refresh_main_music_once(
                g_dluma_cmm_runtime.active_module);
    }
    if (g_dluma_cmm_runtime.music_manager_published &&
        g_dluma_cmm_runtime.main_menu_module_published)
        (void)dluma_cmm_refresh_main_music_once(
            g_dluma_cmm_runtime.active_module);
    dluma_cmm_reconcile_screen_audio(now_ms);
    for (music_index = 0; manifest &&
         music_index < manifest->music_override_count; ++music_index)
        if (manifest->music_overrides[music_index].target ==
                DLUMA_CUSTOM_MENU_MUSIC_MAIN)
            main_music_required = 1;
    if (background_still_owned && g_dluma_cmm_runtime.camera_applied &&
        g_dluma_cmm_runtime.music_manager_published &&
        g_dluma_cmm_runtime.main_menu_module_published &&
        dluma_atomic_compare_exchange(
            &g_dluma_cmm_runtime.restoration_ui_requested, 0, 0) == 0 &&
        (!manifest || !manifest->use_custom_textures ||
         g_dluma_cmm_runtime.texture_definitions_loaded) &&
        (!main_music_required ||
         dluma_atomic_compare_exchange(
             &g_dluma_cmm_runtime.main_music_refresh_state, 0, 0) == 2) &&
        dluma_atomic_compare_exchange(
            &g_dluma_cmm_runtime.restoration_pending, 0, 1) == 1)
    {
        g_dluma_cmm_runtime.gameplay_generation_retired = 0U;
        dluma_atomic_exchange(&g_dluma_cmm_runtime.frontend_committed, 1);
        dluma_atomic_exchange(&g_dluma_cmm_runtime.frontend_return_ready, 0);
        dluma_cmm_lock(&g_dluma_cmm_runtime.generation_lock);
        g_dluma_cmm_runtime.restoration_ui_level = 0;
        dluma_cmm_unlock(&g_dluma_cmm_runtime.generation_lock);
        dluma_log_pair(
            "INFO", "custom_main_menu_frontend_restoration",
            "scene_camera_audio_ready");
    }
    if (!g_dluma_cmm_runtime.camera_applied)
        return;
    if (g_dluma_cmm_runtime.receipt_good)
        return;
    if (!dluma_custom_main_menu_runtime_stability_ready(
            g_dluma_cmm_runtime.stable_since_ms, now_ms,
            DLUMA_CMM_STABLE_MS, background_still_owned))
        return;
    dluma_custom_main_menu_mark_activation_good();
    g_dluma_cmm_runtime.receipt_good = 1U;
    dluma_log_pair("INFO", "custom_main_menu_stability", "two_seconds_good");
}

void dluma_custom_main_menu_runtime_quiesce(void)
{
    int initializer_restored;
    int music_assign_restored;
    int activate_restored;
    int screen_restored;
    int vtables_restored;
    if (!g_dluma_cmm_runtime.started)
        return;
    dluma_atomic_exchange(&g_dluma_cmm_runtime.stopping, 1);
    dluma_cmm_lock(&g_dluma_cmm_runtime.lifecycle_lock);
    initializer_restored =
        dluma_cmm_restore_main_ui_initializer_hook();
    music_assign_restored =
        dluma_cmm_restore_music_assign_hook();
    activate_restored = dluma_cmm_restore_activate_hook();
    screen_restored = dluma_cmm_restore_screen_hooks();
    vtables_restored = dluma_cmm_restore_vtable_hooks();
    dluma_cmm_unlock(&g_dluma_cmm_runtime.lifecycle_lock);
    if (!initializer_restored || !music_assign_restored || !screen_restored ||
        !activate_restored || !vtables_restored)
    {
        g_dluma_cmm_runtime.teardown_failed = 1U;
        dluma_log_pair("WARN", "custom_main_menu_runtime_hooks",
                     "ownership_or_protection_restore_failed");
    }
}

int dluma_custom_main_menu_runtime_stop(void)
{
    ULONGLONG started;
    if (!g_dluma_cmm_runtime.started)
        return 1;
    dluma_custom_main_menu_runtime_quiesce();
    started = GetTickCount64();
    while (dluma_atomic_compare_exchange(
               &g_dluma_cmm_runtime.active_calls, 0, 0) != 0 &&
           GetTickCount64() - started < DLUMA_CMM_DRAIN_TIMEOUT_MS)
        Sleep(1);
    if (dluma_atomic_compare_exchange(
            &g_dluma_cmm_runtime.active_calls, 0, 0) != 0 ||
        g_dluma_cmm_runtime.load_installed ||
        g_dluma_cmm_runtime.unload_installed ||
        g_dluma_cmm_runtime.screen_shown_installed ||
        g_dluma_cmm_runtime.screen_hidden_installed ||
        g_dluma_cmm_runtime.activate_installed ||
        g_dluma_cmm_runtime.main_initialize_ui_installed ||
        g_dluma_cmm_runtime.main_texture_hook_installed ||
        g_dluma_cmm_runtime.music_assign_installed ||
        g_dluma_cmm_runtime.teardown_failed)
    {
        dluma_log_pair("WARN", "custom_main_menu_runtime_stop",
                     "callbacks_or_hooks_retained");
        return 0;
    }
    if (!dluma_custom_main_menu_music_shutdown())
    {
        dluma_log_pair(
            "WARN", "custom_main_menu_runtime_stop",
            "music_override_not_released_dependencies_retained");
        return 0;
    }
    if (!dluma_cmm_clear_music_manager())
    {
        dluma_log_pair(
            "WARN", "custom_main_menu_runtime_stop",
            "frontend_music_manager_not_cleared_dependencies_retained");
        return 0;
    }
    if (!dluma_cmm_texture_definitions_unload())
    {
        dluma_log_pair(
            "WARN", "custom_main_menu_runtime_stop",
            "texture_definitions_not_unloaded_dependencies_retained");
        return 0;
    }
    dluma_cmm_stop_started_fx_emitters(
        g_dluma_cmm_runtime.active_background);
    dluma_cmm_restore_fx(g_dluma_cmm_runtime.active_background);
    if (g_dluma_cmm_runtime.frontend_generation)
        dluma_custom_main_menu_invalidate_frontend_generation(
            g_dluma_cmm_runtime.frontend_generation);
    dluma_custom_main_menu_retire_generation();
    if (g_dluma_cmm_runtime.activate_trampoline)
        VirtualFree(g_dluma_cmm_runtime.activate_trampoline, 0, MEM_RELEASE);
    if (g_dluma_cmm_runtime.music_assign_relay)
        VirtualFree(
            g_dluma_cmm_runtime.music_assign_relay,
            0, MEM_RELEASE);
    memset(&g_dluma_cmm_runtime, 0, sizeof(g_dluma_cmm_runtime));
    dluma_log_pair("INFO", "custom_main_menu_runtime_stop", "drained");
    return 1;
}
