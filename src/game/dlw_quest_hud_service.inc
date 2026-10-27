/* Internal host-context implementation. Uses only copied quest-HUD records
 * and the established presentation adapter; it introduces no engine binding. */

typedef struct DlumaQuestHudJsonWriter {
    char* data;
    size_t size;
    size_t capacity;
    int invalid;
} DlumaQuestHudJsonWriter;

static void dluma_game_hud_json_append(
    DlumaQuestHudJsonWriter* writer, const char* text)
{
    while (*text) {
        if (writer->size + 1U < writer->capacity)
            writer->data[writer->size] = *text;
        ++writer->size;
        ++text;
    }
}

static void dluma_game_hud_json_string(
    DlumaQuestHudJsonWriter* writer, const char* text, size_t capacity)
{
    size_t index;
    static const char hex[] = "0123456789abcdef";
    dluma_game_hud_json_append(writer, "\"");
    for (index = 0; index < capacity && text[index]; ++index) {
        unsigned char value = (unsigned char)text[index];
        char escaped[7] = {0};
        if (value == '"' || value == '\\') {
            escaped[0] = '\\';
            escaped[1] = (char)value;
        } else if (value < 0x20U) {
            memcpy(escaped, "\\u00", 4U);
            escaped[4] = hex[value >> 4U];
            escaped[5] = hex[value & 15U];
        } else {
            escaped[0] = (char)value;
        }
        dluma_game_hud_json_append(writer, escaped);
    }
    if (index == capacity) writer->invalid = 1;
    dluma_game_hud_json_append(writer, "\"");
}

static void dluma_game_hud_json_number(
    DlumaQuestHudJsonWriter* writer, uint64_t value)
{
    char digits[21];
    unsigned int index = 20U;
    digits[index] = 0;
    do {
        digits[--index] = (char)('0' + value % 10U);
        value /= 10U;
    } while (value);
    dluma_game_hud_json_append(writer, digits + index);
}

static void dluma_game_hud_json_target(
    DlumaQuestHudJsonWriter* writer, const DlumaGameQuestHudSnapshot* snapshot,
    const DlumaLua54QuestHudTarget* target, int objective)
{
    (void)snapshot;
    dluma_game_hud_json_append(writer, "{\"id\":");
    dluma_game_hud_json_string(writer, target->id, sizeof(target->id));
    dluma_game_hud_json_append(writer, ",\"text\":");
    dluma_game_hud_json_string(writer, target->text, sizeof(target->text));
    dluma_game_hud_json_append(writer, ",\"display_text\":");
    dluma_game_hud_json_string(
        writer, target->display_text, sizeof(target->display_text));
    dluma_game_hud_json_append(writer, target->overridden
        ? ",\"overridden\":true" : ",\"overridden\":false");
    if (objective) {
        dluma_game_hud_json_append(writer, ",\"slot\":");
        dluma_game_hud_json_number(writer, target->slot);
    }
    dluma_game_hud_json_append(writer, "}");
}

static DlumaStatus dluma_game_hud_status(const DlumaGameError* error)
{
    if (dluma_game_host_string_equal(error->code, "quest_hud_target_expired"))
        return DLUMA_STATUS_STALE_HANDLE;
    if (dluma_game_host_string_equal(error->code, "quest_hud_override_limit"))
        return DLUMA_STATUS_LIMIT_EXCEEDED;
    if (dluma_game_host_string_equal(error->code, "invalid_quest_hud_override") ||
        dluma_game_host_string_equal(error->code, "invalid_quest_hud_snapshot"))
        return DLUMA_STATUS_INVALID_ARGUMENT;
    if (dluma_game_host_string_equal(error->code, "unsupported_capability"))
        return DLUMA_STATUS_UNAVAILABLE;
    return DLUMA_STATUS_INTERNAL_ERROR;
}

static DlumaStatus dluma_game_host_quest_hud_snapshot(DlumaQuestHudResult* result)
{
    DlumaGameQuestHudSnapshot snapshot;
    DlumaGameError error;
    char json[8192];
    DlumaQuestHudJsonWriter writer = {json, 0U, sizeof(json), 0};
    uint32_t index;
    if (!result) return DLUMA_STATUS_INVALID_ARGUMENT;
    result->revision = 0U;
    result->json.required = 0U;
    if (result->json.data && result->json.capacity)
        result->json.data[0] = 0;
    memset(&snapshot, 0, sizeof(snapshot));
    snapshot.struct_size = (uint32_t)sizeof(snapshot);
    memset(&error, 0, sizeof(error));
    error.struct_size = (uint32_t)sizeof(error);
    if (!dluma_quest_hud_snapshot(&snapshot, &error))
        return dluma_game_hud_status(&error);
    if (snapshot.struct_size < sizeof(snapshot) ||
        snapshot.objective_count > DLUMA_LUA54_MAX_QUEST_HUD_OBJECTIVES)
        return DLUMA_STATUS_INTERNAL_ERROR;
    dluma_game_hud_json_append(&writer, "{\"revision\":");
    dluma_game_hud_json_number(&writer, snapshot.revision);
    if (snapshot.has_header) {
        dluma_game_hud_json_append(&writer, ",\"header\":");
        dluma_game_hud_json_target(&writer, &snapshot, &snapshot.header, 0);
    }
    dluma_game_hud_json_append(&writer, ",\"objectives\":[");
    for (index = 0U; index < snapshot.objective_count; ++index) {
        if (index) dluma_game_hud_json_append(&writer, ",");
        dluma_game_hud_json_target(
            &writer, &snapshot, &snapshot.objectives[index], 1);
    }
    dluma_game_hud_json_append(&writer, "]");
    if (snapshot.has_timer) {
        dluma_game_hud_json_append(&writer, ",\"timer\":");
        dluma_game_hud_json_target(&writer, &snapshot, &snapshot.timer, 0);
    }
    dluma_game_hud_json_append(&writer, "}");
    if (writer.invalid) return DLUMA_STATUS_INTERNAL_ERROR;
    result->json.required = (uint64_t)writer.size + 1U;
    if (writer.size >= sizeof(json)) return DLUMA_STATUS_LIMIT_EXCEEDED;
    result->revision = snapshot.revision;
    if (!result->json.data || result->json.capacity < result->json.required)
        return DLUMA_STATUS_INSUFFICIENT_BUFFER;
    json[writer.size] = 0;
    memcpy(result->json.data, json, writer.size + 1U);
    return DLUMA_STATUS_OK;
}

static DlumaStatus dluma_game_host_quest_hud_override(
    DlumaGameHostState* state, const DlumaQuestHudOverrideRequest* request,
    DlumaTokenResult* result)
{
    DlumaGameHostQuestHudOverride* slot = 0;
    DlumaGameQuestHudOverrideRequest native_request;
    DlumaGameQuestHudOverrideResult native_result;
    DlumaGameError error;
    uint32_t index;
    if (!state || !state->owner || !request || !result)
        return DLUMA_STATUS_INVALID_ARGUMENT;
    result->token = 0U;
    memset(&native_request, 0, sizeof(native_request));
    native_request.struct_size = (uint32_t)sizeof(native_request);
    native_request.owner_id = state->owner->owner_id;
    native_request.map_generation = state->owner->map_generation;
    if (!dluma_game_host_copy_view(native_request.target_id,
            (unsigned int)sizeof(native_request.target_id), request->target_id) ||
        !dluma_game_host_copy_view(native_request.text,
            (unsigned int)sizeof(native_request.text), request->text))
        return DLUMA_STATUS_INVALID_ARGUMENT;
    for (index = 0U; index < DLUMA_GAME_HOST_QUEST_HUD_LIMIT; ++index) {
        DlumaGameHostQuestHudOverride* candidate = &state->quest_hud[index];
        if (request->token ? candidate->token == request->token
                           : candidate->token == 0U) {
            slot = candidate;
            break;
        }
    }
    if (!slot) return request->token
        ? DLUMA_STATUS_STALE_HANDLE : DLUMA_STATUS_LIMIT_EXCEEDED;
    if (request->token &&
        (slot->map_generation != state->owner->map_generation ||
         slot->owner_generation != state->owner->generation))
        return DLUMA_STATUS_STALE_HANDLE;
    native_request.override_id = request->token ? slot->native_id : 0U;
    memset(&native_result, 0, sizeof(native_result));
    native_result.struct_size = (uint32_t)sizeof(native_result);
    memset(&error, 0, sizeof(error));
    error.struct_size = (uint32_t)sizeof(error);
    if (!dluma_quest_hud_override_set(&native_request, &native_result, &error))
        return dluma_game_hud_status(&error);
    if (!native_result.active || !native_result.override_id)
        return DLUMA_STATUS_INTERNAL_ERROR;
    if (!request->token)
        slot->token = dluma_game_host_token(DLUMA_GAME_TOKEN_QUEST_HUD,
            dluma_game_host_next_generation(state), index);
    slot->native_id = native_result.override_id;
    slot->map_generation = state->owner->map_generation;
    slot->owner_generation = state->owner->generation;
    result->token = slot->token;
    return DLUMA_STATUS_OK;
}

static DlumaStatus dluma_game_host_quest_hud_cancel(
    DlumaGameHostState* state, uint64_t token, DlumaBoolResult* result)
{
    uint32_t index;
    result->value = 0U;
    for (index = 0U; index < DLUMA_GAME_HOST_QUEST_HUD_LIMIT; ++index) {
        DlumaGameHostQuestHudOverride* slot = &state->quest_hud[index];
        DlumaGameQuestHudOverrideResult native_result;
        DlumaGameError error;
        if (!slot->token || slot->token != token) continue;
        memset(&native_result, 0, sizeof(native_result));
        native_result.struct_size = (uint32_t)sizeof(native_result);
        memset(&error, 0, sizeof(error));
        error.struct_size = (uint32_t)sizeof(error);
        if (!dluma_quest_hud_override_clear(state->owner->owner_id,
                slot->map_generation, slot->native_id, &native_result, &error))
            return dluma_game_hud_status(&error);
        memset(slot, 0, sizeof(*slot));
        result->value = 1U;
        break;
    }
    return DLUMA_STATUS_OK;
}

static void dluma_game_host_quest_hud_clear(DlumaGameHostState* state)
{
    if (state->owner) dluma_quest_hud_clear_owner(state->owner->owner_id);
    memset(state->quest_hud, 0, sizeof(state->quest_hud));
}
