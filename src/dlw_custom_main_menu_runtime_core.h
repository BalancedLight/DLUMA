#ifndef DLUMA_CUSTOM_MAIN_MENU_RUNTIME_CORE_H
#define DLUMA_CUSTOM_MAIN_MENU_RUNTIME_CORE_H

#ifdef __cplusplus
extern "C" {
#endif

#define DLUMA_CMM_SUPPORTED_PLAYER_SHA256 \
    "61d9dfb20cfa179c6d097537da46ba9dbe3d58797938762ef4eacff5dfcc4b3a"
#define DLUMA_CMM_SUPPORTED_GAMEDLL_SHA256 \
    "cbc7dd7923778936341cdfb755474a0c714079d910849df16ffc5ec88a103b95"
#define DLUMA_CMM_SUPPORTED_ENGINE_TIMESTAMP 0x623395D0UL
#define DLUMA_CMM_SUPPORTED_ENGINE_IMAGE_SIZE 0x00E8D000UL
#define DLUMA_CMM_SUPPORTED_GAMEDLL_TIMESTAMP 0x623397AAUL
#define DLUMA_CMM_SUPPORTED_GAMEDLL_IMAGE_SIZE 0x02082000UL

#define DLUMA_CMM_MUSIC_TRANSITION_NO_OPERATION 0
#define DLUMA_CMM_MUSIC_TRANSITION_START 1
#define DLUMA_CMM_MUSIC_TRANSITION_SWITCH 2
#define DLUMA_CMM_MUSIC_TRANSITION_RELEASE 3

#define DLUMA_CMM_PUBLICATION_AWAITING_UI_INITIALIZER 0
#define DLUMA_CMM_PUBLICATION_STOCK_PREMOUNT 1
#define DLUMA_CMM_PUBLICATION_AWAITING_STOCK_COMPLETION 2
#define DLUMA_CMM_PUBLICATION_CATALOG_VERIFIED 3
#define DLUMA_CMM_PUBLICATION_READY_BEFORE_UI 4
#define DLUMA_CMM_PUBLICATION_LOADING_BACKGROUND 5
#define DLUMA_CMM_PUBLICATION_ACTIVE 6
#define DLUMA_CMM_PUBLICATION_STOCK_FALLBACK 7

#define DLUMA_CMM_PUBLICATION_EVENT_ENTER_UI_INITIALIZER 1
#define DLUMA_CMM_PUBLICATION_EVENT_PREMOUNT_COMPLETE 2
#define DLUMA_CMM_PUBLICATION_EVENT_CATALOGS_VALID 3
#define DLUMA_CMM_PUBLICATION_EVENT_OVERLAY_MOUNTED 4
#define DLUMA_CMM_PUBLICATION_EVENT_LOAD_STARTED 5
#define DLUMA_CMM_PUBLICATION_EVENT_ACTIVATE_COMPLETE 6
#define DLUMA_CMM_PUBLICATION_EVENT_FAIL 7

#define DLUMA_CMM_RESTORATION_WAIT 0
#define DLUMA_CMM_RESTORATION_PUBLISH 1
#define DLUMA_CMM_RESTORATION_LOAD 2
#define DLUMA_CMM_RESTORATION_ACTIVATE 3
#define DLUMA_CMM_RESTORATION_PRESENTATION 4

int dluma_custom_main_menu_runtime_restoration_action(
    int pending, int frontend_ready, int publication_state,
    int custom_load_complete);

int dluma_custom_main_menu_runtime_profile_identity_valid(
    const char *player_sha256,
    const char *gamedll_sha256,
    unsigned long engine_timestamp,
    unsigned long engine_image_size,
    unsigned long gamedll_timestamp,
    unsigned long gamedll_image_size);

int dluma_custom_main_menu_runtime_float_finite(float value);
int dluma_custom_main_menu_runtime_matrix_valid(
    const float *matrix, unsigned int value_count);
int dluma_custom_main_menu_runtime_camera_values_valid(
    int rtti_matches,
    int expected_id,
    int actual_id,
    int name_matches,
    int level_matches,
    int camera_present,
    const float *matrix,
    unsigned int matrix_value_count,
    float fov);

int dluma_custom_main_menu_runtime_editor_camera_forward(
    const float *helper_forward,
    float *camera_forward);

int dluma_custom_main_menu_runtime_ttl_unchanged(
    const void *observed_data,
    unsigned int observed_size,
    unsigned int observed_capacity,
    const void *replacement_data,
    unsigned int replacement_size,
    unsigned int replacement_capacity);

int dluma_custom_main_menu_runtime_hook_mutation_complete(
    int ownership_preserved,
    int protection_restored,
    int instruction_cache_flushed);

int dluma_custom_main_menu_runtime_relative_call_valid(
    const unsigned char *block,
    unsigned int block_size,
    unsigned int call_offset,
    unsigned long block_rva,
    unsigned long expected_target_rva);

int dluma_custom_main_menu_runtime_relative_call_displacement(
    unsigned long long instruction_end,
    unsigned long long relay,
    int *displacement_out);

int dluma_custom_main_menu_runtime_music_route_should_substitute(
    int source_is_music_main,
    int destination_matches,
    int primary_vtable_matches,
    int igsobject_vtable_matches,
    int selection_valid,
    int prepared_bank_valid);

int dluma_custom_main_menu_runtime_music_transition(
    const char *active_cue,
    const char *desired_cue,
    const char *normal_cue);

int dluma_custom_main_menu_runtime_empty_stack_grace_active(
    unsigned long long empty_since_ms,
    unsigned long long now_ms,
    unsigned long long grace_ms);

int dluma_custom_main_menu_runtime_screen_script_matches(
    const char *script_name,
    const char *override_destination);

int dluma_custom_main_menu_runtime_screen_script_is_cursor(
    const char *script_name);

int dluma_custom_main_menu_runtime_fx_enable_contract_valid(
    const unsigned char *bytes,
    unsigned int byte_count,
    unsigned long export_rva,
    unsigned long image_size,
    unsigned long *backing_flag_rva_out);

int dluma_custom_main_menu_runtime_stock_premount_contract_valid(
    const unsigned char *premount_bytes,
    unsigned int premount_byte_count,
    unsigned long premount_rva,
    unsigned long image_size,
    unsigned long *tools_global_rva_out);

int dluma_custom_main_menu_runtime_engine_content_name(
    const char *canonical_path,
    char *output,
    unsigned int output_capacity);

int dluma_custom_main_menu_runtime_content_identity(
    const char *fingerprint,
    unsigned long long *identity_out);

int dluma_custom_main_menu_runtime_fx_restore_should_apply(
    int changed_by_runtime,
    int background_owner_matches,
    int observed_flag,
    int runtime_enabled_flag);

int dluma_custom_main_menu_runtime_stability_ready(
    unsigned long long stable_since_ms,
    unsigned long long now_ms,
    unsigned long long required_ms,
    int background_still_owned);

int dluma_custom_main_menu_runtime_texture_registration_matches(
    const void *loaded_level,
    const void *candidate_level,
    const char *loaded_fingerprint,
    const char *candidate_fingerprint,
    int definitions_loaded);

int dluma_custom_main_menu_runtime_static_camera_bridge_enabled(
    int does_use_3d_camera_movement);

int dluma_custom_main_menu_runtime_publication_transition(
    int current_state,
    int event,
    int *next_state_out);

#ifdef __cplusplus
}
#endif

#endif
