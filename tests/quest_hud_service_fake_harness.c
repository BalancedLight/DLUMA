#include <stdio.h>
#include <string.h>
#include "../src/core/dlw_mod_owner.h"
#include "../src/dlw_quest_hud.h"
#include "../src/game/dlw_quest_hud_service.h"

typedef struct DlumaGameHostState {
    DlumaModOwner* owner;
    DlumaGameHostQuestHudOverride quest_hud[DLUMA_GAME_HOST_QUEST_HUD_LIMIT];
} DlumaGameHostState;

static uint32_t g_generation;
static uint64_t g_native_id;
static uint64_t g_cleared_owner;
static uint64_t g_updated_id;
static unsigned int g_clear_calls;
static int g_available = 1;
static DlumaGameQuestHudSnapshot g_snapshot;

static uint32_t dluma_game_host_next_generation(DlumaGameHostState* state)
{
    (void)state;
    return ++g_generation;
}

static uint64_t dluma_game_host_token(
    uint32_t kind, uint32_t generation, uint32_t index)
{
    return ((uint64_t)kind << 56U) | ((uint64_t)generation << 32U) |
        ((uint64_t)index + 1U);
}

static int dluma_game_host_copy_view(
    char* output, unsigned int capacity, DlumaStringView value)
{
    if (value.size >= capacity || (!value.data && value.size) ||
        (value.size && memchr(value.data, 0, (size_t)value.size))) return 0;
    if (value.size) memcpy(output, value.data, (size_t)value.size);
    output[value.size] = 0;
    return 1;
}

static int dluma_game_host_string_equal(const char* left, const char* right)
{
    return strcmp(left, right) == 0;
}

int dluma_quest_hud_snapshot(DlumaGameQuestHudSnapshot* snapshot, DlumaGameError* error)
{
    if (!g_available) { strcpy(error->code, "unsupported_capability"); return 0; }
    *snapshot = g_snapshot;
    return 1;
}

int dluma_quest_hud_override_set(const DlumaGameQuestHudOverrideRequest* request,
    DlumaGameQuestHudOverrideResult* result, DlumaGameError* error)
{
    if (!g_available) { strcpy(error->code, "unsupported_capability"); return 0; }
    if (!strcmp(request->target_id, "expired")) {
        strcpy(error->code, "quest_hud_target_expired"); return 0;
    }
    g_updated_id = request->override_id;
    result->override_id = request->override_id ? request->override_id : ++g_native_id;
    result->active = 1U;
    return 1;
}

int dluma_quest_hud_override_clear(uint64_t owner, uint32_t generation, uint64_t id,
    DlumaGameQuestHudOverrideResult* result, DlumaGameError* error)
{
    (void)generation; (void)error;
    g_cleared_owner = owner;
    ++g_clear_calls;
    result->override_id = id;
    result->active = 0U;
    return 1;
}

void dluma_quest_hud_clear_owner(uint64_t owner) { g_cleared_owner = owner; }

#include "../src/game/dlw_quest_hud_service.inc"

#define CHECK(value) do { if (!(value)) { \
    printf("FAIL line %d: %s\n", __LINE__, #value); return 0; } } while (0)

static DlumaStringView view(const char* text)
{
    DlumaStringView value = {text, (uint64_t)strlen(text)};
    return value;
}

static int snapshot_cases(void)
{
    char json[8192];
    DlumaQuestHudResult result = {0};
    memset(&g_snapshot, 0, sizeof(g_snapshot));
    g_snapshot.struct_size = (uint32_t)sizeof(g_snapshot);
    g_snapshot.revision = 42U;
    g_snapshot.has_header = 1U;
    strcpy(g_snapshot.header.id, "header:1");
    strcpy(g_snapshot.header.text, "A \"quoted\" \\ path\nCaf\xc3\xa9");
    strcpy(g_snapshot.header.display_text, "Shown");
    g_snapshot.header.overridden = 1U;
    g_snapshot.objective_count = 1U;
    g_snapshot.objectives[0].slot = 3U;
    strcpy(g_snapshot.objectives[0].id, "objective:1");
    g_snapshot.has_timer = 1U;
    strcpy(g_snapshot.timer.id, "timer:1");
    result.json.data = json;
    result.json.capacity = 1U;
    json[0] = '!';
    CHECK(dluma_game_host_quest_hud_snapshot(&result) == DLUMA_STATUS_INSUFFICIENT_BUFFER);
    CHECK(json[0] == 0 && result.json.required > 1U && result.revision == 42U);
    result.json.capacity = sizeof(json);
    CHECK(dluma_game_host_quest_hud_snapshot(&result) == DLUMA_STATUS_OK);
    CHECK(result.json.required == strlen(json) + 1U);
    printf("JSON:%s\n", json);
    g_available = 0;
    CHECK(dluma_game_host_quest_hud_snapshot(&result) == DLUMA_STATUS_UNAVAILABLE);
    CHECK(json[0] == 0 && result.revision == 0U);
    g_available = 1;
    g_snapshot.objective_count = 5U;
    CHECK(dluma_game_host_quest_hud_snapshot(&result) == DLUMA_STATUS_INTERNAL_ERROR);
    g_snapshot.objective_count = 0U;
    memset(g_snapshot.header.text, 1, sizeof(g_snapshot.header.text));
    CHECK(dluma_game_host_quest_hud_snapshot(&result) == DLUMA_STATUS_INTERNAL_ERROR);
    g_snapshot.header.text[sizeof(g_snapshot.header.text) - 1U] = 0;
    memcpy(g_snapshot.header.display_text, g_snapshot.header.text,
        sizeof(g_snapshot.header.text));
    memcpy(&g_snapshot.timer, &g_snapshot.header, sizeof(g_snapshot.timer));
    CHECK(dluma_game_host_quest_hud_snapshot(&result) == DLUMA_STATUS_LIMIT_EXCEEDED);
    CHECK(json[0] == 0);
    puts("[PASS] snapshot copies, escaping, bounds and unavailable state");
    return 1;
}

static int override_cases(void)
{
    DlumaModOwner owner = {0}, other_owner = {0};
    DlumaGameHostState state = {0}, other = {0};
    DlumaQuestHudOverrideRequest request = {0};
    DlumaTokenResult result = {0};
    DlumaBoolResult cancelled = {0};
    uint64_t original_token;
    unsigned int index;
    owner.owner_id = 10U; owner.generation = 1U; owner.map_generation = 7U;
    other_owner.owner_id = 20U; other_owner.generation = 1U;
    other_owner.map_generation = 7U;
    state.owner = &owner; other.owner = &other_owner;
    request.target_id = view("header:1"); request.text = view("Replacement");
    g_available = 0;
    CHECK(dluma_game_host_quest_hud_override(&state, &request, &result) == DLUMA_STATUS_UNAVAILABLE);
    CHECK(result.token == 0 && state.quest_hud[0].token == 0);
    g_available = 1;
    CHECK(dluma_game_host_quest_hud_override(&state, &request, &result) == DLUMA_STATUS_OK);
    original_token = result.token;
    CHECK(original_token != 0 && (original_token >> 56U) == DLUMA_GAME_TOKEN_QUEST_HUD);
    CHECK(dluma_game_host_quest_hud_override(&other, &request, &result) == DLUMA_STATUS_OK);
    CHECK(result.token != original_token);
    request.token = original_token;
    CHECK(dluma_game_host_quest_hud_override(&other, &request, &result) == DLUMA_STATUS_STALE_HANDLE);
    CHECK(dluma_game_host_quest_hud_override(&state, &request, &result) == DLUMA_STATUS_OK);
    CHECK(result.token == original_token && g_updated_id == state.quest_hud[0].native_id);
    ++owner.generation;
    CHECK(dluma_game_host_quest_hud_override(&state, &request, &result) == DLUMA_STATUS_STALE_HANDLE);
    --owner.generation; ++owner.map_generation;
    CHECK(dluma_game_host_quest_hud_override(&state, &request, &result) == DLUMA_STATUS_STALE_HANDLE);
    --owner.map_generation;
    CHECK(dluma_game_host_quest_hud_cancel(&other, original_token, &cancelled) == DLUMA_STATUS_OK);
    CHECK(cancelled.value == 0U && g_clear_calls == 0U);
    CHECK(dluma_game_host_quest_hud_cancel(&state, original_token, &cancelled) == DLUMA_STATUS_OK);
    CHECK(cancelled.value == 1U && g_cleared_owner == owner.owner_id && g_clear_calls == 1U);
    CHECK(dluma_game_host_quest_hud_cancel(&state, original_token, &cancelled) == DLUMA_STATUS_OK);
    CHECK(cancelled.value == 0U && g_clear_calls == 1U);
    request.token = 0U;
    for (index = 0U; index < DLUMA_GAME_HOST_QUEST_HUD_LIMIT; ++index)
        CHECK(dluma_game_host_quest_hud_override(&state, &request, &result) == DLUMA_STATUS_OK);
    CHECK(dluma_game_host_quest_hud_override(&state, &request, &result) == DLUMA_STATUS_LIMIT_EXCEEDED);
    dluma_game_host_quest_hud_clear(&state);
    CHECK(g_cleared_owner == owner.owner_id && state.quest_hud[0].token == 0U);
    request.token = original_token;
    CHECK(dluma_game_host_quest_hud_override(&state, &request, &result) == DLUMA_STATUS_STALE_HANDLE);
    request.token = 0U; request.target_id = view("expired");
    CHECK(dluma_game_host_quest_hud_override(&state, &request, &result) == DLUMA_STATUS_STALE_HANDLE);
    puts("[PASS] owned create/update/cancel, quotas, stale generation and cleanup");
    return 1;
}

int main(void) { return snapshot_cases() && override_cases() ? 0 : 1; }
