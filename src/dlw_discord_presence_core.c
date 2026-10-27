#include "dlw_discord_presence_core.h"

int dluma_discord_select_host_route(
    DlumaHostKind host_kind,
    int forwarding_only,
    unsigned long long dying_light_application_id,
    unsigned long long developer_tools_application_id,
    DlumaDiscordHostRoute* output)
{
    if (!output) return 0;
    output->application_id = 0;
    output->uses_editor_setting = 0U;
    if (forwarding_only) return 0;
    if (host_kind == DLUMA_HOST_PLAYER_STANDALONE) {
        if (!dying_light_application_id) return 0;
        output->application_id = dying_light_application_id;
        return 1;
    }
    if (host_kind == DLUMA_HOST_CHROME_EDITOR) {
        if (!developer_tools_application_id) return 0;
        output->application_id = developer_tools_application_id;
        output->uses_editor_setting = 1U;
        return 1;
    }
    if (host_kind == DLUMA_HOST_PLAYER_EDITOR_PLAYTEST) {
        if (!developer_tools_application_id) return 0;
        output->application_id = developer_tools_application_id;
        return 1;
    }
    return 0;
}

static unsigned int dluma_discord_core_strlen_bounded(
    const char* text, unsigned int capacity)
{
    unsigned int length = 0;
    if (!text) return 0;
    while (length < capacity && text[length]) ++length;
    return length;
}

static int dluma_discord_core_is_continuation(unsigned char value)
{
    return (value & 0xC0u) == 0x80u;
}

static unsigned int dluma_discord_core_decode(
    const unsigned char* input, unsigned int remaining,
    unsigned int* scalar_out)
{
    unsigned int scalar;
    if (!remaining) return 0;
    if (input[0] < 0x80u) {
        *scalar_out = input[0];
        return 1;
    }
    if (input[0] >= 0xC2u && input[0] <= 0xDFu &&
        remaining >= 2 && dluma_discord_core_is_continuation(input[1])) {
        *scalar_out = ((unsigned int)(input[0] & 0x1Fu) << 6) |
                      (unsigned int)(input[1] & 0x3Fu);
        return 2;
    }
    if (input[0] >= 0xE0u && input[0] <= 0xEFu && remaining >= 3 &&
        dluma_discord_core_is_continuation(input[1]) &&
        dluma_discord_core_is_continuation(input[2]) &&
        !(input[0] == 0xE0u && input[1] < 0xA0u) &&
        !(input[0] == 0xEDu && input[1] >= 0xA0u)) {
        scalar = ((unsigned int)(input[0] & 0x0Fu) << 12) |
                 ((unsigned int)(input[1] & 0x3Fu) << 6) |
                 (unsigned int)(input[2] & 0x3Fu);
        *scalar_out = scalar;
        return 3;
    }
    if (input[0] >= 0xF0u && input[0] <= 0xF4u && remaining >= 4 &&
        dluma_discord_core_is_continuation(input[1]) &&
        dluma_discord_core_is_continuation(input[2]) &&
        dluma_discord_core_is_continuation(input[3]) &&
        !(input[0] == 0xF0u && input[1] < 0x90u) &&
        !(input[0] == 0xF4u && input[1] >= 0x90u)) {
        scalar = ((unsigned int)(input[0] & 0x07u) << 18) |
                 ((unsigned int)(input[1] & 0x3Fu) << 12) |
                 ((unsigned int)(input[2] & 0x3Fu) << 6) |
                 (unsigned int)(input[3] & 0x3Fu);
        *scalar_out = scalar;
        return 4;
    }
    *scalar_out = 0xFFFDu;
    return 1;
}

unsigned int dluma_discord_utf8_sanitize(
    const char* input, unsigned int input_size,
    char* output, unsigned int output_capacity,
    unsigned int max_codepoints)
{
    unsigned int input_offset = 0;
    unsigned int output_offset = 0;
    unsigned int codepoints = 0;
    if (!output || !output_capacity) return 0;
    output[0] = 0;
    if (!input || !max_codepoints) return 0;
    while (input_offset < input_size && input[input_offset] &&
           codepoints < max_codepoints) {
        unsigned int scalar = 0;
        unsigned int consumed = dluma_discord_core_decode(
            (const unsigned char*)input + input_offset,
            input_size - input_offset, &scalar);
        const unsigned char* source =
            (const unsigned char*)input + input_offset;
        unsigned int required = consumed;
        input_offset += consumed;
        if (scalar < 0x20u ||
            (scalar >= 0x7Fu && scalar <= 0x9Fu) ||
            scalar == 0x2028u || scalar == 0x2029u)
            continue;
        if (scalar == 0xFFFDu) {
            static const unsigned char replacement[3] = {0xEFu, 0xBFu, 0xBDu};
            source = replacement;
            required = 3;
        }
        if (output_offset + required >= output_capacity) break;
        while (required) {
            output[output_offset++] = (char)*source++;
            --required;
        }
        ++codepoints;
    }
    output[output_offset] = 0;
    return output_offset;
}

unsigned int dluma_discord_apply_name_format(
    const char* format, unsigned int format_capacity,
    const char* value, unsigned int value_capacity,
    char* output, unsigned int output_capacity,
    unsigned int max_codepoints)
{
    static const char placeholder[] = "{name}";
    char combined[2048];
    unsigned int format_length;
    unsigned int value_length;
    unsigned int format_offset = 0;
    unsigned int combined_offset = 0;
    int replaced = 0;
    if (!output || !output_capacity) return 0;
    output[0] = 0;
    if (!format || !format_capacity || !value || !value_capacity)
        return 0;
    format_length = dluma_discord_core_strlen_bounded(
        format, format_capacity);
    value_length = dluma_discord_core_strlen_bounded(
        value, value_capacity);
    if (format_length == format_capacity ||
        value_length == value_capacity)
        return 0;
    while (format_offset < format_length &&
           combined_offset + 1U < sizeof(combined)) {
        if (format_offset + sizeof(placeholder) - 1U <=
                format_length &&
            format[format_offset] == placeholder[0] &&
            format[format_offset + 1U] == placeholder[1] &&
            format[format_offset + 2U] == placeholder[2] &&
            format[format_offset + 3U] == placeholder[3] &&
            format[format_offset + 4U] == placeholder[4] &&
            format[format_offset + 5U] == placeholder[5]) {
            unsigned int value_offset = 0;
            while (value_offset < value_length &&
                   combined_offset + 1U < sizeof(combined))
                combined[combined_offset++] = value[value_offset++];
            format_offset += sizeof(placeholder) - 1U;
            replaced = 1;
        } else {
            combined[combined_offset++] = format[format_offset++];
        }
    }
    if (!replaced) return 0;
    combined[combined_offset] = 0;
    return dluma_discord_utf8_sanitize(
        combined, combined_offset, output, output_capacity,
        max_codepoints);
}

int dluma_discord_https_asset_url_valid(const char* value)
{
    unsigned int length;
    unsigned int i;
    if (!value) return 0;
    length = dluma_discord_core_strlen_bounded(
        value, DLUMA_DISCORD_ACTIVITY_IMAGE_CAPACITY);
    if (length < 9 || length > 300 || value[length]) return 0;
    if (!((value[0] == 'h' || value[0] == 'H') &&
          (value[1] == 't' || value[1] == 'T') &&
          (value[2] == 't' || value[2] == 'T') &&
          (value[3] == 'p' || value[3] == 'P') &&
          (value[4] == 's' || value[4] == 'S') &&
          value[5] == ':' && value[6] == '/' && value[7] == '/'))
        return 0;
    for (i = 8; i < length; ++i) {
        unsigned char c = (unsigned char)value[i];
        if (c <= 0x20u || c > 0x7Eu || c == '"' || c == '\\')
            return 0;
    }
    return 1;
}

int dluma_discord_build_workshop_item_url(
    const char* workshop_id, unsigned int workshop_id_capacity,
    char* output, unsigned int output_capacity)
{
    static const char prefix[] =
        "https://steamcommunity.com/sharedfiles/filedetails/?id=";
    unsigned long long parsed = 0;
    unsigned int id_length;
    unsigned int prefix_length = (unsigned int)sizeof(prefix) - 1U;
    unsigned int i;
    if (!output || !output_capacity) return 0;
    output[0] = 0;
    if (!workshop_id || !workshop_id_capacity) return 0;
    id_length = dluma_discord_core_strlen_bounded(
        workshop_id, workshop_id_capacity);
    if (!id_length || id_length == workshop_id_capacity ||
        id_length > 20U ||
        prefix_length + id_length >= output_capacity)
        return 0;
    for (i = 0; i < id_length; ++i) {
        unsigned int digit;
        if (workshop_id[i] < '0' || workshop_id[i] > '9')
            return 0;
        digit = (unsigned int)(workshop_id[i] - '0');
        if (parsed >
            (0xFFFFFFFFFFFFFFFFULL - digit) / 10ULL)
            return 0;
        parsed = parsed * 10ULL + digit;
    }
    if (!parsed) return 0;
    for (i = 0; i < prefix_length; ++i) output[i] = prefix[i];
    for (i = 0; i < id_length; ++i)
        output[prefix_length + i] = workshop_id[i];
    output[prefix_length + id_length] = 0;
    return 1;
}

static int dluma_discord_core_bytes_equal(
    const char* left, const char* right, unsigned int capacity)
{
    unsigned int i;
    for (i = 0; i < capacity; ++i) {
        if (left[i] != right[i]) return 0;
        if (!left[i]) return 1;
    }
    return 0;
}

int dluma_discord_activity_equal(
    const DlumaDiscordActivityV1* left,
    const DlumaDiscordActivityV1* right)
{
    if (!left || !right || left->flags != right->flags ||
        left->start_timestamp_seconds !=
            right->start_timestamp_seconds)
        return 0;
    return dluma_discord_core_bytes_equal(
               left->details, right->details,
               DLUMA_DISCORD_ACTIVITY_TEXT_CAPACITY) &&
           dluma_discord_core_bytes_equal(
               left->state, right->state,
               DLUMA_DISCORD_ACTIVITY_TEXT_CAPACITY) &&
           dluma_discord_core_bytes_equal(
               left->large_image, right->large_image,
               DLUMA_DISCORD_ACTIVITY_IMAGE_CAPACITY) &&
           dluma_discord_core_bytes_equal(
               left->large_text, right->large_text,
               DLUMA_DISCORD_ACTIVITY_TEXT_CAPACITY) &&
           dluma_discord_core_bytes_equal(
               left->small_image, right->small_image,
               DLUMA_DISCORD_ACTIVITY_IMAGE_CAPACITY) &&
           dluma_discord_core_bytes_equal(
               left->small_text, right->small_text,
               DLUMA_DISCORD_ACTIVITY_TEXT_CAPACITY) &&
           dluma_discord_core_bytes_equal(
               left->button_label, right->button_label,
               DLUMA_DISCORD_ACTIVITY_BUTTON_LABEL_CAPACITY) &&
           dluma_discord_core_bytes_equal(
               left->button_url, right->button_url,
               DLUMA_DISCORD_ACTIVITY_BUTTON_URL_CAPACITY);
}

DlumaDiscordMenuActivity dluma_discord_menu_from_visibility(
    unsigned int visibility_mask)
{
    if (visibility_mask & DLUMA_DISCORD_MENU_VISIBLE_SELECTING_MAP)
        return DLUMA_DISCORD_MENU_SELECTING_MAP;
    if (visibility_mask & DLUMA_DISCORD_MENU_VISIBLE_SETTINGS)
        return DLUMA_DISCORD_MENU_CONFIGURING_SETTINGS;
    if (visibility_mask & DLUMA_DISCORD_MENU_VISIBLE_MODS)
        return DLUMA_DISCORD_MENU_ADJUSTING_MODS;
    if (visibility_mask & DLUMA_DISCORD_MENU_VISIBLE_CREDITS)
        return DLUMA_DISCORD_MENU_READING_CREDITS;
    if (visibility_mask & DLUMA_DISCORD_MENU_VISIBLE_STATISTICS)
        return DLUMA_DISCORD_MENU_VIEWING_STATISTICS;
    return DLUMA_DISCORD_MENU_MAIN;
}

DlumaDiscordLevelSource dluma_discord_choose_level_source(
    int active_level_positive,
    int session_level_positive)
{
    /*
     * A session observation is made from the current frame and is accepted
     * only when it either agrees with the current active level or that query
     * is unavailable. It must therefore supersede the retained last-positive
     * active-level snapshot used to bridge ordinary loading gaps.
     */
    if (session_level_positive)
        return DLUMA_DISCORD_LEVEL_SOURCE_SESSION;
    if (active_level_positive)
        return DLUMA_DISCORD_LEVEL_SOURCE_ACTIVE;
    return DLUMA_DISCORD_LEVEL_SOURCE_NONE;
}

unsigned long long dluma_discord_timer_update(
    DlumaDiscordTimerState* state,
    DlumaDiscordTimerKind kind,
    unsigned int map_context,
    unsigned long long level_identity,
    unsigned long long now_seconds)
{
    int changed;
    if (!state) return 0;
    if (kind == DLUMA_DISCORD_TIMER_NONE || !now_seconds) {
        state->start_timestamp_seconds = 0;
        state->level_identity = 0;
        state->map_context = 0;
        state->kind = DLUMA_DISCORD_TIMER_NONE;
        state->valid = 0;
        return 0;
    }

    changed = !state->valid ||
              state->kind != (unsigned char)kind;
    if (!changed &&
        (kind == DLUMA_DISCORD_TIMER_GAMEPLAY ||
         kind == DLUMA_DISCORD_TIMER_EDITOR)) {
        changed = state->level_identity != level_identity;
        if (!changed && kind == DLUMA_DISCORD_TIMER_GAMEPLAY)
            changed = state->map_context != map_context;
    }
    if (changed) {
        state->start_timestamp_seconds = now_seconds;
        state->level_identity =
            kind == DLUMA_DISCORD_TIMER_GAMEPLAY ||
            kind == DLUMA_DISCORD_TIMER_EDITOR
                ? level_identity
                : 0;
        state->map_context =
            kind == DLUMA_DISCORD_TIMER_GAMEPLAY
                ? map_context
                : 0;
        state->kind = (unsigned char)kind;
        state->valid = 1;
    }
    return state->start_timestamp_seconds;
}

unsigned int dluma_discord_debounce_update(
    DlumaDiscordDebounceState* state,
    unsigned int observed_kind,
    int positive_observation,
    unsigned long long now_ms,
    unsigned int debounce_ms,
    unsigned int unmatched_grace_ms,
    int* changed_out)
{
    if (changed_out) *changed_out = 0;
    if (!state) return observed_kind;
    if (!positive_observation) {
        if (state->has_committed &&
            now_ms - state->last_positive_ms <= unmatched_grace_ms)
            return state->committed_kind;
        if (state->has_committed && changed_out) *changed_out = 1;
        state->has_committed = 0;
        state->has_candidate = 0;
        return observed_kind;
    }
    state->last_positive_ms = now_ms;
    if (!state->has_committed) {
        state->committed_kind = observed_kind;
        state->has_committed = 1;
        state->has_candidate = 0;
        if (changed_out) *changed_out = 1;
        return observed_kind;
    }
    if (state->committed_kind == observed_kind) {
        state->has_candidate = 0;
        return observed_kind;
    }
    if (!state->has_candidate || state->candidate_kind != observed_kind) {
        state->candidate_kind = observed_kind;
        state->candidate_since_ms = now_ms;
        state->has_candidate = 1;
        return state->committed_kind;
    }
    if (now_ms - state->candidate_since_ms < debounce_ms)
        return state->committed_kind;
    state->committed_kind = observed_kind;
    state->has_candidate = 0;
    if (changed_out) *changed_out = 1;
    return observed_kind;
}

unsigned long long dluma_discord_next_backoff(
    unsigned long long current_ms,
    unsigned long long initial_ms,
    unsigned long long maximum_ms)
{
    if (!initial_ms || !maximum_ms || initial_ms > maximum_ms)
        return 0;
    if (!current_ms) return initial_ms;
    if (current_ms >= maximum_ms) return maximum_ms;
    if (current_ms > maximum_ms - current_ms) return maximum_ms;
    current_ms += current_ms;
    return current_ms > maximum_ms ? maximum_ms : current_ms;
}
