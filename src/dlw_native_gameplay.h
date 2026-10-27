#ifndef DLUMA_NATIVE_GAMEPLAY_H
#define DLUMA_NATIVE_GAMEPLAY_H

#include "game/dlw_game_types.h"

#ifdef __cplusplus
extern "C" {
#endif

enum DlumaNativeGameplayReadiness {
    DLUMA_NATIVE_READY_WORLD_FIND = 1u << 0,
    DLUMA_NATIVE_READY_LOCAL_ENABLE = 1u << 1,
    DLUMA_NATIVE_READY_SENSOR = 1u << 2,
    DLUMA_NATIVE_READY_INTERACT = 1u << 3,
    DLUMA_NATIVE_READY_SUBTITLES = 1u << 4,
    DLUMA_NATIVE_READY_RADIO_DIALOG = 1u << 5,
    DLUMA_NATIVE_READY_ACTION_CHAIN = 1u << 6,
    /* Exact DevTools QuestManager transition binding.  It is available only
     * after the fingerprinted adapter and active-map lifecycle are ready. */
    DLUMA_NATIVE_READY_QUEST_MUTATION = 1u << 7,
    /* IControlObject position is independently fingerprinted and remains
     * fail-closed until a live map/index makes its copied adapter usable. */
    DLUMA_NATIVE_READY_WORLD_POSITION = 1u << 8,
    /* Stock HudQuestObjective observation/override is presentation-only and
     * is independently gated from QuestManager mutation. */
    DLUMA_NATIVE_READY_QUEST_HUD = 1u << 9
};

typedef enum DlumaNativeCapabilityState {
    DLUMA_NATIVE_CAP_STATIC_UNSUPPORTED = 0,
    DLUMA_NATIVE_CAP_STATIC_VALIDATED = 1,
    DLUMA_NATIVE_CAP_WAITING_FOR_MAP = 2,
    DLUMA_NATIVE_CAP_LIVE_SELF_TESTING = 3,
    DLUMA_NATIVE_CAP_READY = 4,
    DLUMA_NATIVE_CAP_DEGRADED = 5
} DlumaNativeCapabilityState;

typedef enum DlumaNativeIndexPhase {
    DLUMA_NATIVE_INDEX_UNINITIALIZED = 0,
    DLUMA_NATIVE_INDEX_SCANNING_IDS = 1,
    DLUMA_NATIVE_INDEX_COMPLETE = 2,
    DLUMA_NATIVE_INDEX_DEGRADED = 3
} DlumaNativeIndexPhase;

typedef struct DlumaNativeIndexDiagnostics {
    uint32_t struct_size;
    uint32_t phase;
    uint32_t scan_cursor;
    uint32_t published_entries;
    uint64_t ids_scanned;
    uint64_t nonnull_id_results;
    uint64_t component_walks;
    uint64_t candidate_pointers;
    uint64_t accepted_entries;
    uint64_t rejected_wrong_level;
    uint64_t rejected_complete_object;
    uint64_t rejected_name;
    uint64_t rejected_rtti;
    uint64_t rejected_duplicate_pointer;
    uint64_t rejected_duplicate_id;
    uint64_t rejected_capacity;
    uint64_t prev_cycle_or_cap;
    uint64_t next_cycle_or_cap;
    uint64_t accepted_distinct_owner_level;
    uint64_t rejected_owner_level_missing;
    uint64_t rejected_owner_id_invalid;
    uint64_t rejected_owner_name_unavailable;
    uint64_t rejected_owner_name_mismatch;
    uint64_t observed_owner_id_roundtrip_mismatch;
    uint64_t reconsidered_direct_seeds;
    uint64_t accepted_reconsidered_direct_seeds;
} DlumaNativeIndexDiagnostics;

/* The core adapter is driven through this small backend so its lifecycle,
 * indexing, edge detection, atomic preflight, and hook-observer semantics can
 * be tested with deterministic fake objects.  Production fills it only after
 * validating the exact supported PE pair and every target listed in the
 * 0.2.1 evidence bundle. */
typedef struct DlumaNativeGameplayBackend {
    uint32_t struct_size;
    uint32_t readiness;
    void* user;
    uint64_t (*clock_ms)(void* user);
    void* (*active_level)(void* user, char* map_out, uint32_t map_capacity);
    void* (*object_by_id)(void* user, void* level, int32_t object_id);
    void* (*object_prev)(void* user, void* object);
    void* (*object_next)(void* user, void* object);
    void* (*object_level)(void* user, void* object);
    int (*level_name)(void* user, void* level, char* out, uint32_t capacity);
    int32_t (*object_id)(void* user, void* object);
    int (*object_name)(void* user, void* object, char* out, uint32_t capacity);
    int (*object_class)(void* user, void* object, char* out, uint32_t capacity);
    int (*object_is_a)(void* user, void* object, const char* class_name);
    void* (*complete_object)(void* user, void* object);
    void* (*control_object_interface)(void* user, void* object);
    int (*world_get_position)(
        void* user, void* control_object_interface,
        DlumaGameVec3* position);
    int (*world_get_forward)(
        void* user, void* control_object_interface,
        DlumaGameVec3* forward);
    void* (*local_enable_interface)(void* user, void* object);
    int (*local_enable_get)(void* user, void* interface_pointer, int* enabled);
    int (*local_enable_set)(void* user, void* interface_pointer, int enabled);
    void* (*sensor_area_interface)(void* user, void* object);
    int (*sensor_inside)(void* user, void* sensor_area_interface, int* inside);
    int (*subtitle_show)(
        void* user, void* level, int32_t subtitle_id,
        const char* text, int force);
    int (*subtitle_hide)(void* user, void* level, int32_t subtitle_id);
    int (*radio_dialog)(
        void* user, const char* speaker, const char* group);
    int (*action_run_chain)(void* user, void* complete_action_chain);
    /* Production assigns this only for the fingerprinted DevTools build and
     * only to the synchronized QuestManager transition adapter. */
    int (*quest_mutate)(
        void* user,
        const DlumaGameQuestRequest* request,
        DlumaGameQuestResult* result,
        DlumaGameError* error);
} DlumaNativeGameplayBackend;

int dluma_native_gameplay_start(void);
int dluma_native_gameplay_start_with_backend(
    const DlumaNativeGameplayBackend* backend);
void dluma_native_gameplay_quiesce(void);
void dluma_native_gameplay_stop(void);
int dluma_native_gameplay_stop_succeeded(void);
void dluma_native_gameplay_on_map_generation_changed(
    uint32_t map_generation,
    const char* normalized_map,
    void* active_level);
void dluma_native_gameplay_game_thread_tick(void);
uint32_t dluma_native_gameplay_readiness(void);
uint32_t dluma_native_gameplay_capabilities(void);
uint32_t dluma_native_gameplay_supported_capabilities(void);
DlumaNativeCapabilityState dluma_native_gameplay_capability_state(
    uint32_t readiness_bit);
DlumaNativeIndexPhase dluma_native_gameplay_index_phase(void);
uint64_t dluma_native_gameplay_index_epoch(void);
int dluma_native_gameplay_event_subscription_current(
    uint32_t kind,
    uint32_t map_generation,
    uint64_t index_epoch,
    uint64_t key_revision,
    const char* exact_name);
uint32_t dluma_native_gameplay_snapshot_event_owners(
    uint32_t kind,
    uint32_t map_generation,
    uint64_t index_epoch,
    uint64_t key_revision,
    const char* exact_name,
    uint64_t* owner_ids,
    uint32_t owner_capacity);
typedef struct DlumaNativeEventTarget {
    uint64_t owner_id;
    uint64_t callback_id;
    uint64_t registration_token;
} DlumaNativeEventTarget;
uint32_t dluma_native_gameplay_snapshot_event_targets(
    uint32_t kind,
    uint32_t map_generation,
    uint64_t index_epoch,
    uint64_t key_revision,
    const char* exact_name,
    DlumaNativeEventTarget* targets,
    uint32_t target_capacity);
int dluma_native_gameplay_index_diagnostics(
    DlumaNativeIndexDiagnostics* diagnostics);

int dluma_native_gameplay_subscription_add(
    uint64_t owner_id,
    uint32_t map_generation,
    uint32_t token,
    uint32_t kind,
    const char* exact_name,
    DlumaGameError* error);
int dluma_native_gameplay_subscription_add_ex(
    uint64_t owner_id,
    uint32_t map_generation,
    uint64_t registration_token,
    uint64_t callback_id,
    uint32_t kind,
    const char* exact_name,
    DlumaGameError* error);
int dluma_native_gameplay_subscription_remove(
    uint64_t owner_id,
    uint32_t token,
    uint32_t kind,
    const char* exact_name,
    DlumaGameError* error);
int dluma_native_gameplay_subscription_remove_ex(
    uint64_t owner_id,
    uint64_t registration_token,
    DlumaGameError* error);
int dluma_native_gameplay_world_find(
    uint64_t owner_id,
    uint32_t map_generation,
    const char* exact_name,
    const char* expected_class,
    DlumaGameObject* object,
    DlumaGameError* error);
int dluma_native_gameplay_world_get_info(
    uint64_t owner_id,
    uint64_t handle_id,
    uint32_t map_generation,
    DlumaGameObject* object,
    DlumaGameError* error);
int dluma_native_gameplay_world_get_enabled(
    uint64_t owner_id,
    uint64_t handle_id,
    uint32_t map_generation,
    int* enabled,
    DlumaGameError* error);
int dluma_native_gameplay_world_get_position(
    uint64_t owner_id,
    uint64_t handle_id,
    uint32_t map_generation,
    DlumaGameVec3* position,
    DlumaGameError* error);
int dluma_native_gameplay_world_get_forward(
    uint64_t owner_id,
    uint64_t handle_id,
    uint32_t map_generation,
    DlumaGameVec3* forward,
    DlumaGameError* error);
int dluma_native_gameplay_world_set_enabled(
    uint64_t owner_id,
    const DlumaGameSetEnabledRequest* request,
    DlumaGameSetEnabledResult* result,
    DlumaGameError* error);
int dluma_native_gameplay_sensor_inside(
    uint64_t owner_id,
    uint32_t map_generation,
    const char* exact_name,
    int* inside,
    DlumaGameError* error);
int dluma_native_gameplay_subtitle_show(
    const DlumaGameSubtitleRequest* request,
    DlumaGameError* error);
int dluma_native_gameplay_subtitle_update(
    const DlumaGameSubtitleUpdateRequest* request,
    DlumaGameError* error);
int dluma_native_gameplay_radio_dialog(
    const DlumaGameRadioDialogRequest* request,
    DlumaGameError* error);
int dluma_native_gameplay_action_run_chain(
    uint64_t owner_id,
    uint32_t map_generation,
    const char* exact_name,
    DlumaGameError* error);
int dluma_native_gameplay_quest_mutate(
    uint64_t owner_id,
    const DlumaGameQuestRequest* request,
    DlumaGameQuestResult* result,
    DlumaGameError* error);

/* Called only by the validated InteractiveItem observer after the original
 * OnExecuteTrigger returns.  It performs no engine call and never runs Lua. */
int dluma_native_gameplay_enqueue_interactive_item_event(
    void* complete_item,
    uint32_t action,
    int original_result);

#if defined(DLUMA_NATIVE_FAKE_TESTS)
typedef char (__fastcall *DlumaNativeFakeInteractiveOriginalFn)(
    void* item, unsigned int action, __int64* context);
int dluma_native_gameplay_fake_validate_get_rtti_stub(
    const unsigned char* image,
    uint32_t image_size,
    uint32_t function_offset,
    uint32_t expected_rtti_offset);
int dluma_native_gameplay_fake_invoke_interaction_observer(
    DlumaNativeFakeInteractiveOriginalFn original,
    void* item,
    uint32_t action,
    __int64* context);
int dluma_native_gameplay_fake_install_interaction_slot(
    void** writable_slot,
    DlumaNativeFakeInteractiveOriginalFn original);
void dluma_native_gameplay_fake_set_quiesce_lock_contention(
    int match_locked,
    int lifecycle_locked);
#endif

#ifdef __cplusplus
}
#endif

#endif
