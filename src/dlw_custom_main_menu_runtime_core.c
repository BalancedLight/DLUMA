#include "dlw_custom_main_menu_runtime_core.h"

int dluma_custom_main_menu_runtime_restoration_action(
    int pending, int frontend_ready, int publication_state,
    int custom_load_complete)
{
    if (!pending || !frontend_ready)
        return DLUMA_CMM_RESTORATION_WAIT;
    switch (publication_state)
    {
    case DLUMA_CMM_PUBLICATION_AWAITING_UI_INITIALIZER:
        return DLUMA_CMM_RESTORATION_PUBLISH;
    case DLUMA_CMM_PUBLICATION_READY_BEFORE_UI:
        return DLUMA_CMM_RESTORATION_LOAD;
    case DLUMA_CMM_PUBLICATION_LOADING_BACKGROUND:
        return custom_load_complete ? DLUMA_CMM_RESTORATION_ACTIVATE :
                                      DLUMA_CMM_RESTORATION_WAIT;
    case DLUMA_CMM_PUBLICATION_ACTIVE:
        return DLUMA_CMM_RESTORATION_PRESENTATION;
    default:
        return DLUMA_CMM_RESTORATION_WAIT;
    }
}

static unsigned char dluma_cmm_core_lower(unsigned char value)
{
    return value >= (unsigned char)'A' && value <= (unsigned char)'Z'
        ? (unsigned char)(value + ((unsigned char)'a' - (unsigned char)'A'))
        : value;
}

static int dluma_cmm_core_equal(const char *left, const char *right)
{
    unsigned int index = 0;
    if (!left || !right)
        return 0;
    while (left[index] && right[index])
    {
        if (dluma_cmm_core_lower((unsigned char)left[index]) !=
            dluma_cmm_core_lower((unsigned char)right[index]))
            return 0;
        ++index;
    }
    return left[index] == 0 && right[index] == 0;
}

int dluma_custom_main_menu_runtime_engine_content_name(
    const char *canonical_path,
    char *output,
    unsigned int output_capacity)
{
    const char *source = canonical_path;
    unsigned int length = 0;
    unsigned int source_offset = 0;
    unsigned int prefix_length = 0;
    unsigned int index;

    if (output && output_capacity)
        output[0] = 0;
    if (!canonical_path || !output || output_capacity < 2U)
        return 0;
    while (canonical_path[length])
        ++length;
    if (!length)
        return 0;

    if (length >= 4U && canonical_path[0] == '\\' &&
        canonical_path[1] == '\\' && canonical_path[2] == '?' &&
        canonical_path[3] == '\\')
    {
        source_offset = 4U;
        if (length >= 8U &&
            dluma_cmm_core_lower((unsigned char)canonical_path[4]) == 'u' &&
            dluma_cmm_core_lower((unsigned char)canonical_path[5]) == 'n' &&
            dluma_cmm_core_lower((unsigned char)canonical_path[6]) == 'c' &&
            canonical_path[7] == '\\')
        {
            source_offset = 8U;
            prefix_length = 2U;
        }
    }
    if (output_capacity <= prefix_length ||
        source_offset >= length ||
        length - source_offset >
            output_capacity - prefix_length - 1U)
        return 0;

    source = canonical_path + source_offset;
    if (prefix_length)
    {
        output[0] = '\\';
        output[1] = '\\';
    }
    for (index = 0; source[index]; ++index)
        output[prefix_length + index] = source[index];
    output[prefix_length + index] = 0;
    return 1;
}

int dluma_custom_main_menu_runtime_content_identity(
    const char *fingerprint,
    unsigned long long *identity_out)
{
    unsigned long long value = 0ULL;
    unsigned int index;
    if (identity_out)
        *identity_out = 0ULL;
    if (!fingerprint || !identity_out)
        return 0;
    for (index = 0; index < 64U; ++index)
    {
        unsigned char character = (unsigned char)fingerprint[index];
        unsigned int nibble;
        if (!character)
            return 0;
        if (character >= (unsigned char)'0' &&
            character <= (unsigned char)'9')
            nibble = (unsigned int)(character - (unsigned char)'0');
        else if (dluma_cmm_core_lower(character) >= (unsigned char)'a' &&
                 dluma_cmm_core_lower(character) <= (unsigned char)'f')
            nibble = (unsigned int)(
                dluma_cmm_core_lower(character) - (unsigned char)'a') + 10U;
        else
            return 0;
        if (index < 16U)
            value = (value << 4U) | (unsigned long long)nibble;
    }
    if (fingerprint[64] != 0)
        return 0;
    if (!value)
        value = 1ULL;
    *identity_out = value;
    return 1;
}

int dluma_custom_main_menu_runtime_publication_transition(
    int current_state,
    int event,
    int *next_state_out)
{
    int next_state = current_state;
    if (!next_state_out ||
        current_state < DLUMA_CMM_PUBLICATION_AWAITING_UI_INITIALIZER ||
        current_state > DLUMA_CMM_PUBLICATION_STOCK_FALLBACK)
        return 0;
    if (event == DLUMA_CMM_PUBLICATION_EVENT_FAIL)
    {
        if (current_state == DLUMA_CMM_PUBLICATION_ACTIVE ||
            current_state == DLUMA_CMM_PUBLICATION_STOCK_FALLBACK)
            return 0;
        *next_state_out = DLUMA_CMM_PUBLICATION_STOCK_FALLBACK;
        return 1;
    }
    switch (current_state)
    {
    case DLUMA_CMM_PUBLICATION_AWAITING_UI_INITIALIZER:
        if (event != DLUMA_CMM_PUBLICATION_EVENT_ENTER_UI_INITIALIZER)
            return 0;
        next_state = DLUMA_CMM_PUBLICATION_STOCK_PREMOUNT;
        break;
    case DLUMA_CMM_PUBLICATION_STOCK_PREMOUNT:
        if (event != DLUMA_CMM_PUBLICATION_EVENT_PREMOUNT_COMPLETE)
            return 0;
        next_state = DLUMA_CMM_PUBLICATION_AWAITING_STOCK_COMPLETION;
        break;
    case DLUMA_CMM_PUBLICATION_AWAITING_STOCK_COMPLETION:
        if (event != DLUMA_CMM_PUBLICATION_EVENT_CATALOGS_VALID)
            return 0;
        next_state = DLUMA_CMM_PUBLICATION_CATALOG_VERIFIED;
        break;
    case DLUMA_CMM_PUBLICATION_CATALOG_VERIFIED:
        if (event != DLUMA_CMM_PUBLICATION_EVENT_OVERLAY_MOUNTED)
            return 0;
        next_state = DLUMA_CMM_PUBLICATION_READY_BEFORE_UI;
        break;
    case DLUMA_CMM_PUBLICATION_READY_BEFORE_UI:
        if (event != DLUMA_CMM_PUBLICATION_EVENT_LOAD_STARTED)
            return 0;
        next_state = DLUMA_CMM_PUBLICATION_LOADING_BACKGROUND;
        break;
    case DLUMA_CMM_PUBLICATION_LOADING_BACKGROUND:
        if (event != DLUMA_CMM_PUBLICATION_EVENT_ACTIVATE_COMPLETE)
            return 0;
        next_state = DLUMA_CMM_PUBLICATION_ACTIVE;
        break;
    default:
        return 0;
    }
    *next_state_out = next_state;
    return 1;
}

int dluma_custom_main_menu_runtime_profile_identity_valid(
    const char *player_sha256,
    const char *gamedll_sha256,
    unsigned long engine_timestamp,
    unsigned long engine_image_size,
    unsigned long gamedll_timestamp,
    unsigned long gamedll_image_size)
{
    return dluma_cmm_core_equal(
               player_sha256, DLUMA_CMM_SUPPORTED_PLAYER_SHA256) &&
           dluma_cmm_core_equal(
               gamedll_sha256, DLUMA_CMM_SUPPORTED_GAMEDLL_SHA256) &&
           engine_timestamp == DLUMA_CMM_SUPPORTED_ENGINE_TIMESTAMP &&
           engine_image_size == DLUMA_CMM_SUPPORTED_ENGINE_IMAGE_SIZE &&
           gamedll_timestamp == DLUMA_CMM_SUPPORTED_GAMEDLL_TIMESTAMP &&
           gamedll_image_size == DLUMA_CMM_SUPPORTED_GAMEDLL_IMAGE_SIZE;
}

int dluma_custom_main_menu_runtime_float_finite(float value)
{
    union DlumaCmmFloatBits
    {
        float value;
        unsigned long bits;
    } bits;
    bits.value = value;
    return (bits.bits & 0x7F800000UL) != 0x7F800000UL;
}

int dluma_custom_main_menu_runtime_matrix_valid(
    const float *matrix, unsigned int value_count)
{
    unsigned int index;
    float magnitude = 0.0f;
    if (!matrix || value_count != 12U)
        return 0;
    for (index = 0; index < value_count; ++index)
    {
        float value = matrix[index];
        float absolute = value < 0.0f ? -value : value;
        if (!dluma_custom_main_menu_runtime_float_finite(value) ||
            absolute > 10000000.0f)
            return 0;
        if (index < 9U)
            magnitude += absolute;
    }
    return magnitude > 0.0001f;
}

int dluma_custom_main_menu_runtime_camera_values_valid(
    int rtti_matches,
    int expected_id,
    int actual_id,
    int name_matches,
    int level_matches,
    int camera_present,
    const float *matrix,
    unsigned int matrix_value_count,
    float fov)
{
    return rtti_matches && expected_id >= 0 &&
           actual_id == expected_id && name_matches && level_matches &&
           camera_present &&
           dluma_custom_main_menu_runtime_float_finite(fov) &&
           fov >= 1.0f && fov <= 179.0f &&
           dluma_custom_main_menu_runtime_matrix_valid(
               matrix, matrix_value_count);
}

int dluma_custom_main_menu_runtime_editor_camera_forward(
    const float *helper_forward,
    float *camera_forward)
{
    if (!helper_forward || !camera_forward ||
        !dluma_custom_main_menu_runtime_float_finite(helper_forward[0]) ||
        !dluma_custom_main_menu_runtime_float_finite(helper_forward[1]) ||
        !dluma_custom_main_menu_runtime_float_finite(helper_forward[2]))
        return 0;
    /* CameraHelperMenuScene3dDI::BindEditorCamera and the base game's
     * CameraPostionHelper::BindEditorCamera both build the camera from the
     * negative third column of the helper world transform.  The exact Player
     * IControlObject::GetForwardVector export returns that column unnegated. */
    camera_forward[0] = -helper_forward[0];
    camera_forward[1] = -helper_forward[1];
    camera_forward[2] = -helper_forward[2];
    return 1;
}

int dluma_custom_main_menu_runtime_ttl_unchanged(
    const void *observed_data,
    unsigned int observed_size,
    unsigned int observed_capacity,
    const void *replacement_data,
    unsigned int replacement_size,
    unsigned int replacement_capacity)
{
    return observed_data == replacement_data &&
           observed_size == replacement_size &&
           observed_capacity == replacement_capacity;
}

int dluma_custom_main_menu_runtime_hook_mutation_complete(
    int ownership_preserved,
    int protection_restored,
    int instruction_cache_flushed)
{
    return ownership_preserved && protection_restored &&
           instruction_cache_flushed;
}

int dluma_custom_main_menu_runtime_relative_call_valid(
    const unsigned char *block,
    unsigned int block_size,
    unsigned int call_offset,
    unsigned long block_rva,
    unsigned long expected_target_rva)
{
    unsigned long encoded;
    int displacement;
    long long target;
    if (!block || call_offset > block_size ||
        block_size - call_offset < 5U ||
        block[call_offset] != 0xE8U)
        return 0;
    encoded = (unsigned long)block[call_offset + 1U] |
              ((unsigned long)block[call_offset + 2U] << 8U) |
              ((unsigned long)block[call_offset + 3U] << 16U) |
              ((unsigned long)block[call_offset + 4U] << 24U);
    displacement = (int)encoded;
    target = (long long)block_rva + (long long)call_offset + 5LL +
             (long long)displacement;
    return target >= 0LL &&
           (unsigned long long)target ==
               (unsigned long long)expected_target_rva;
}

int dluma_custom_main_menu_runtime_relative_call_displacement(
    unsigned long long instruction_end,
    unsigned long long relay,
    int *displacement_out)
{
    long long displacement;
    if (displacement_out)
        *displacement_out = 0;
    if (!instruction_end || !relay || !displacement_out)
        return 0;
    if (relay >= instruction_end)
    {
        unsigned long long distance = relay - instruction_end;
        if (distance > 0x7fffffffULL)
            return 0;
        displacement = (long long)distance;
    }
    else
    {
        unsigned long long distance = instruction_end - relay;
        if (distance > 0x80000000ULL)
            return 0;
        displacement = -(long long)distance;
    }
    *displacement_out = (int)displacement;
    return 1;
}

int dluma_custom_main_menu_runtime_music_route_should_substitute(
    int source_is_music_main,
    int destination_matches,
    int primary_vtable_matches,
    int igsobject_vtable_matches,
    int selection_valid,
    int prepared_bank_valid)
{
    return source_is_music_main && destination_matches &&
           primary_vtable_matches && igsobject_vtable_matches &&
           selection_valid && prepared_bank_valid;
}

static int dluma_cmm_core_equal_exact(
    const char *left, const char *right)
{
    unsigned int index = 0;
    if (!left || !right)
        return left == right;
    while (left[index] && right[index] && left[index] == right[index])
        ++index;
    return left[index] == right[index];
}

int dluma_custom_main_menu_runtime_texture_registration_matches(
    const void *loaded_level,
    const void *candidate_level,
    const char *loaded_fingerprint,
    const char *candidate_fingerprint,
    int definitions_loaded)
{
    return definitions_loaded && loaded_level &&
           loaded_level == candidate_level &&
           loaded_fingerprint && loaded_fingerprint[0] &&
           candidate_fingerprint && candidate_fingerprint[0] &&
           dluma_cmm_core_equal_exact(
               loaded_fingerprint, candidate_fingerprint);
}

int dluma_custom_main_menu_runtime_static_camera_bridge_enabled(
    int does_use_3d_camera_movement)
{
    return !does_use_3d_camera_movement;
}

int dluma_custom_main_menu_runtime_music_transition(
    const char *active_cue,
    const char *desired_cue,
    const char *normal_cue)
{
    int active = active_cue && active_cue[0];
    if (!desired_cue || !desired_cue[0] ||
        (normal_cue && normal_cue[0] &&
         dluma_cmm_core_equal_exact(desired_cue, normal_cue)))
        return active ? DLUMA_CMM_MUSIC_TRANSITION_RELEASE
                      : DLUMA_CMM_MUSIC_TRANSITION_NO_OPERATION;
    if (active && dluma_cmm_core_equal_exact(active_cue, desired_cue))
        return DLUMA_CMM_MUSIC_TRANSITION_NO_OPERATION;
    return active ? DLUMA_CMM_MUSIC_TRANSITION_SWITCH
                  : DLUMA_CMM_MUSIC_TRANSITION_START;
}

int dluma_custom_main_menu_runtime_empty_stack_grace_active(
    unsigned long long empty_since_ms,
    unsigned long long now_ms,
    unsigned long long grace_ms)
{
    return empty_since_ms != 0ULL && now_ms >= empty_since_ms &&
           now_ms - empty_since_ms < grace_ms;
}

static const char *dluma_cmm_core_script_leaf(const char *script_name)
{
    const char *leaf = script_name;
    unsigned int index;
    if (!script_name)
        return 0;
    for (index = 0; script_name[index]; ++index)
        if (script_name[index] == '\\' || script_name[index] == '/')
            leaf = script_name + index + 1U;
    return leaf;
}

static unsigned int dluma_cmm_core_script_stem_length(const char *leaf)
{
    unsigned int length = 0;
    if (!leaf)
        return 0;
    while (leaf[length])
        ++length;
    if (length >= 4U && leaf[length - 4U] == '.' &&
        dluma_cmm_core_lower((unsigned char)leaf[length - 3U]) == 'x' &&
        dluma_cmm_core_lower((unsigned char)leaf[length - 2U]) == 'u' &&
        dluma_cmm_core_lower((unsigned char)leaf[length - 1U]) == 'i')
        length -= 4U;
    return length;
}

int dluma_custom_main_menu_runtime_screen_script_matches(
    const char *script_name,
    const char *override_destination)
{
    const char *script_leaf = dluma_cmm_core_script_leaf(script_name);
    const char *destination_leaf =
        dluma_cmm_core_script_leaf(override_destination);
    unsigned int script_length =
        dluma_cmm_core_script_stem_length(script_leaf);
    unsigned int destination_length =
        dluma_cmm_core_script_stem_length(destination_leaf);
    unsigned int index;
    if (!script_leaf || !destination_leaf || !script_length ||
        script_length != destination_length)
        return 0;
    for (index = 0; index < script_length; ++index)
        if (dluma_cmm_core_lower((unsigned char)script_leaf[index]) !=
            dluma_cmm_core_lower((unsigned char)destination_leaf[index]))
            return 0;
    return 1;
}

int dluma_custom_main_menu_runtime_screen_script_is_cursor(
    const char *script_name)
{
    /* MenuModule::OnUIScreenShown intentionally excludes _UI_CURSOR.  Keep
     * the loader's visible-screen reducer aligned with that engine contract
     * so a cursor show after a menu transition cannot mask the real screen. */
    return dluma_custom_main_menu_runtime_screen_script_matches(
        script_name, "UICursor.xui");
}

int dluma_custom_main_menu_runtime_fx_enable_contract_valid(
    const unsigned char *bytes,
    unsigned int byte_count,
    unsigned long export_rva,
    unsigned long image_size,
    unsigned long *backing_flag_rva_out)
{
    static const unsigned char expected[] = {
        0x0F, 0xB6, 0xC2, 0x89, 0x05,
        0xFB, 0x47, 0x90, 0x00, 0xC3
    };
    unsigned long encoded;
    int displacement;
    long long target;
    if (backing_flag_rva_out)
        *backing_flag_rva_out = 0;
    if (!bytes || byte_count < sizeof(expected) ||
        !backing_flag_rva_out)
        return 0;
    {
        unsigned int index;
        for (index = 0; index < sizeof(expected); ++index)
            if (bytes[index] != expected[index])
                return 0;
    }
    encoded = (unsigned long)bytes[5] |
              ((unsigned long)bytes[6] << 8U) |
              ((unsigned long)bytes[7] << 16U) |
              ((unsigned long)bytes[8] << 24U);
    displacement = (int)encoded;
    target = (long long)export_rva + 9LL + (long long)displacement;
    if (target < 0LL || (unsigned long long)target + 4ULL > image_size ||
        ((unsigned long)target & 3UL) != 0UL)
        return 0;
    *backing_flag_rva_out = (unsigned long)target;
    return 1;
}

static int dluma_cmm_core_relative_target(
    const unsigned char *bytes,
    unsigned int displacement_offset,
    unsigned long instruction_end_rva,
    unsigned long image_size,
    unsigned long *target_rva_out)
{
    unsigned long encoded;
    int displacement;
    long long target;
    if (target_rva_out)
        *target_rva_out = 0;
    if (!bytes || !target_rva_out)
        return 0;
    encoded = (unsigned long)bytes[displacement_offset] |
              ((unsigned long)bytes[displacement_offset + 1U] << 8U) |
              ((unsigned long)bytes[displacement_offset + 2U] << 16U) |
              ((unsigned long)bytes[displacement_offset + 3U] << 24U);
    displacement = (int)encoded;
    target = (long long)instruction_end_rva + (long long)displacement;
    if (target < 0LL || (unsigned long long)target >= image_size)
        return 0;
    *target_rva_out = (unsigned long)target;
    return 1;
}

int dluma_custom_main_menu_runtime_stock_premount_contract_valid(
    const unsigned char *premount_bytes,
    unsigned int premount_byte_count,
    unsigned long premount_rva,
    unsigned long image_size,
    unsigned long *tools_global_rva_out)
{
    static const unsigned char expected_premount[] = {
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
    unsigned long tools_global = 0;
    unsigned long second_tools_global = 0;
    unsigned int index;
    if (tools_global_rva_out) *tools_global_rva_out = 0;
    if (!premount_bytes ||
        premount_byte_count < sizeof(expected_premount) ||
        !tools_global_rva_out || premount_rva >= image_size ||
        sizeof(expected_premount) > image_size - premount_rva)
        return 0;
    for (index = 0; index < sizeof(expected_premount); ++index)
        if (premount_bytes[index] != expected_premount[index])
            return 0;
    if (!dluma_cmm_core_relative_target(
            premount_bytes, 28U, premount_rva + 32U,
            image_size, &tools_global) ||
        !dluma_cmm_core_relative_target(
            premount_bytes, 55U, premount_rva + 59U,
            image_size, &second_tools_global) ||
        tools_global != second_tools_global ||
        (tools_global & 7UL) != 0UL)
        return 0;
    *tools_global_rva_out = tools_global;
    return 1;
}

int dluma_custom_main_menu_runtime_fx_restore_should_apply(
    int changed_by_runtime,
    int background_owner_matches,
    int observed_flag,
    int runtime_enabled_flag)
{
    return changed_by_runtime && background_owner_matches &&
           observed_flag == runtime_enabled_flag;
}

int dluma_custom_main_menu_runtime_stability_ready(
    unsigned long long stable_since_ms,
    unsigned long long now_ms,
    unsigned long long required_ms,
    int background_still_owned)
{
    return background_still_owned && stable_since_ms != 0ULL &&
           now_ms >= stable_since_ms &&
           now_ms - stable_since_ms >= required_ms;
}
