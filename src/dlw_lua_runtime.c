#include "dlw_common.h"
#include "core/dlw_api_runtime.h"
#include "core/dlw_mod_package_runtime.h"
#include "game/dlw_game_backend.h"
#include "game/dlw_quest_phase_service.h"
#include "hosts/dlw_managed_host.h"
#include "dlw_animation_refresh_runtime.h"
#include "dlw_path_security.h"
#include "dlw_lua_config.h"
#include "dlw_lua54_api.h"
#include "dlw_native_gameplay.h"
#include "dlw_native_mods.h"
#include "dlw_quest_phase_player.h"
#include "dlw_quest_hud.h"
#include "dlw_lua_project_exit.h"
#include "dlw_project_map_exit.h"

#define DLUMA_LUA_RUNTIME_MAX_STATES 64u
#define DLUMA_LUA_RUNTIME_MAX_EVENTS 256u
#define DLUMA_LUA_RUNTIME_MAX_DEBUG_VALUES 8u
#define DLUMA_LUA_RUNTIME_MAX_DEBUG_LINES 8u
#define DLUMA_LUA_RUNTIME_DEBUG_KEY 64u
#define DLUMA_LUA_RUNTIME_DEBUG_TEXT 256u
#define DLUMA_LUA_RUNTIME_MAX_UNHANDLED_EVENT_LOGS 8u
#define DLUMA_LUA_RUNTIME_MAX_CONFIG_EVENTS 128u
#define DLUMA_LUA_RUNTIME_MAX_QUEST_SNAPSHOTS DLUMA_LUA_QUEST_UI_MAX_SNAPSHOTS
#define DLUMA_LUA_STORAGE_VALUE_LIMIT (16u * 1024u)
#define DLUMA_LUA_STORAGE_TOTAL_LIMIT (64u * 1024u)
#define DLUMA_LUA_CRASH_CONTEXT_CAPACITY (32u * 1024u)
#define DLUMA_LUA_CRASH_CONTEXT_REFRESH_MS 1000ULL

typedef struct DlumaLuaRuntimeEvent {
    uint64_t native_index_epoch;
    uint64_t native_subscription_key_revision;
    uint32_t kind;
    uint32_t map_generation;
    int state;
    double number;
    uint32_t owner_count;
    uint64_t owner_ids[DLUMA_LUA_RUNTIME_MAX_STATES];
    char name[96];
} DlumaLuaRuntimeEvent;

typedef struct DlumaLuaRuntimeDebugValue {
    char key[DLUMA_LUA_RUNTIME_DEBUG_KEY];
    char value[DLUMA_LUA_RUNTIME_DEBUG_TEXT];
} DlumaLuaRuntimeDebugValue;

typedef struct DlumaLuaRuntimeConfigEvent {
    char registry_key[DLUMA_SHA256_HEX_CAPACITY];
    char metadata_fingerprint[DLUMA_SHA256_HEX_CAPACITY];
    DlumaLua54ConfigChangedEvent change;
} DlumaLuaRuntimeConfigEvent;

typedef struct DlumaLuaRuntimeSlot {
    DlumaLuaMetadata metadata;
    void* helper_script;
    DlumaModOwner api_owner;
    DlumaCapabilityRegistry api_capabilities;
    DlumaApiBackendV1 api_backend;
    DlumaApiRuntime api_runtime;
    DlumaGameHostContext api_host;
    DlumaModPackageSnapshot api_package;
    uint32_t approved_permissions;
    uint32_t capabilities;
    uint64_t owner_id;
    uint64_t desired_epoch;
    unsigned char occupied;
    unsigned char teardown_pending;
    unsigned char map_unload_sent;
    unsigned char unload_sent;
    unsigned char legacy_storage_logged;
    unsigned char disabled_logged;
    unsigned char debug_value_overflow_logged;
    unsigned char reserved[1];
    uint32_t debug_value_count;
    uint32_t debug_line_head;
    uint32_t debug_line_count;
    DlumaLuaRuntimeDebugValue debug_values[DLUMA_LUA_RUNTIME_MAX_DEBUG_VALUES];
    char debug_lines[DLUMA_LUA_RUNTIME_MAX_DEBUG_LINES][DLUMA_LUA_RUNTIME_DEBUG_TEXT];
    DlumaLua54Status status;
} DlumaLuaRuntimeSlot;

typedef struct DlumaLuaRuntimeState {
    HMODULE helper_module;
    DlumaLua54Api api;
    volatile LONG pending_lock;
    volatile LONG event_lock;
    volatile LONG config_event_lock;
    volatile LONG quest_lock;
    volatile LONG pump_active;
    volatile LONG quiescing;
    unsigned char available;
    unsigned char is_editor;
    unsigned char is_player;
    unsigned char overflow_reported;
    uint64_t global_revision;
    uint64_t pending_global_revision;
    uint64_t context_sequence;
    uint64_t pending_context_sequence;
    uint64_t global_epoch;
    uint64_t next_owner_id;
    uint32_t map_generation;
    uint32_t pending_global_count;
    uint32_t pending_local_count;
    uint32_t event_head;
    uint32_t event_count;
    uint32_t config_event_count;
    uint32_t unhandled_event_log_count;
    uint32_t quest_snapshot_count;
    uint32_t archived_quest_snapshot_count;
    uint64_t quest_snapshot_revision;
    uint64_t archived_quest_snapshot_revision;
    uint64_t last_pump_ms;
    char current_map[DLUMA_MAX_MAP_NAME];
    char current_project_name[128];
    char current_workshop_id[64];
    WCHAR current_project_root[DLUMA_MAX_WPATH];
    char pending_map[DLUMA_MAX_MAP_NAME];
    char pending_project_name[128];
    char pending_workshop_id[64];
    WCHAR pending_project_root[DLUMA_MAX_WPATH];
    char next_map[DLUMA_MAX_MAP_NAME];
    char next_project_name[128];
    char next_workshop_id[64];
    WCHAR next_project_root[DLUMA_MAX_WPATH];
    DlumaLuaMetadata pending_globals[DLUMA_MAX_LUA_FILES];
    DlumaLuaMetadata pending_locals[DLUMA_MAX_LUA_FILES];
    DlumaLuaMetadata committed_globals[DLUMA_MAX_LUA_FILES];
    DlumaLuaMetadata committed_locals[DLUMA_MAX_LUA_FILES];
    uint32_t committed_global_count;
    uint32_t committed_local_count;
    DlumaLuaRuntimeSlot slots[DLUMA_LUA_RUNTIME_MAX_STATES];
    DlumaLuaRuntimeEvent events[DLUMA_LUA_RUNTIME_MAX_EVENTS];
    DlumaLuaRuntimeConfigEvent
        config_events[DLUMA_LUA_RUNTIME_MAX_CONFIG_EVENTS];
    DlumaLuaQuestUiSnapshot quests[DLUMA_LUA_RUNTIME_MAX_QUEST_SNAPSHOTS];
    /* The frontend can render this read-only final copy after a local map has
     * unloaded.  It is deliberately separate from the live generation-bound
     * cache and is never consulted by Lua, native gameplay, or event routing. */
    DlumaLuaQuestUiSnapshot archived_quests[DLUMA_LUA_RUNTIME_MAX_QUEST_SNAPSHOTS];
} DlumaLuaRuntimeState;

static DlumaLuaRuntimeState g_dluma_lua_runtime;
static volatile LONG g_dluma_lua_runtime_stop_ok;
static volatile LONG g_dluma_lua_runtime_recovery_suppression_logged;
static volatile LONG g_dluma_lua_crash_context_sequence;
static ULONGLONG g_dluma_lua_crash_context_last_ms;
static char g_dluma_lua_crash_context[DLUMA_LUA_CRASH_CONTEXT_CAPACITY];
/* This gate stays closed until the regular owner commits an empty frontend
 * context. A stale local-file reconciliation cannot resurrect retired Lua. */
static unsigned int g_dluma_lua_project_exit_gate;
static volatile LONG g_dluma_map_exit_active;
static volatile LONG g_dluma_map_exit_lock;
static DlumaProjectMapExitReceipt g_dluma_map_exit_receipt;
static DlumaLuaRuntimeSlot* g_dluma_exit_notification_slot;
static uint32_t g_dluma_exit_notification_kind;

static void dluma_lua_runtime_publish_crash_context(
    ULONGLONG now, int force)
{
    unsigned int index;
    unsigned int loaded = 0;
    char number[32];
    if (!force && g_dluma_lua_crash_context_last_ms &&
        now - g_dluma_lua_crash_context_last_ms <
            DLUMA_LUA_CRASH_CONTEXT_REFRESH_MS)
        return;
    dluma_atomic_increment(&g_dluma_lua_crash_context_sequence);
    g_dluma_lua_crash_context[0] = 0;
    for (index = 0; index < DLUMA_LUA_RUNTIME_MAX_STATES; ++index)
        if (g_dluma_lua_runtime.slots[index].occupied) ++loaded;
    dluma_str_append(
        g_dluma_lua_crash_context,
        DLUMA_ARRAY_COUNT(g_dluma_lua_crash_context),
        "[CRASH] [INFO] lua_runtime available=");
    dluma_str_append(
        g_dluma_lua_crash_context,
        DLUMA_ARRAY_COUNT(g_dluma_lua_crash_context),
        g_dluma_lua_runtime.available ? "1" : "0");
    dluma_str_append(
        g_dluma_lua_crash_context,
        DLUMA_ARRAY_COUNT(g_dluma_lua_crash_context), " quiescing=");
    dluma_str_append(
        g_dluma_lua_crash_context,
        DLUMA_ARRAY_COUNT(g_dluma_lua_crash_context),
        g_dluma_lua_runtime.quiescing ? "1" : "0");
    dluma_str_append(
        g_dluma_lua_crash_context,
        DLUMA_ARRAY_COUNT(g_dluma_lua_crash_context), " map_generation=");
    dluma_u64_to_text(
        g_dluma_lua_runtime.map_generation,
        number, DLUMA_ARRAY_COUNT(number));
    dluma_str_append(
        g_dluma_lua_crash_context,
        DLUMA_ARRAY_COUNT(g_dluma_lua_crash_context), number);
    dluma_str_append(
        g_dluma_lua_crash_context,
        DLUMA_ARRAY_COUNT(g_dluma_lua_crash_context), " loaded=");
    dluma_u64_to_text(loaded, number, DLUMA_ARRAY_COUNT(number));
    dluma_str_append(
        g_dluma_lua_crash_context,
        DLUMA_ARRAY_COUNT(g_dluma_lua_crash_context), number);
    dluma_str_append(
        g_dluma_lua_crash_context,
        DLUMA_ARRAY_COUNT(g_dluma_lua_crash_context), " map=");
    dluma_str_append(
        g_dluma_lua_crash_context,
        DLUMA_ARRAY_COUNT(g_dluma_lua_crash_context),
        g_dluma_lua_runtime.current_map[0]
            ? g_dluma_lua_runtime.current_map : "<none>");
    dluma_str_append(
        g_dluma_lua_crash_context,
        DLUMA_ARRAY_COUNT(g_dluma_lua_crash_context), " project=");
    dluma_str_append(
        g_dluma_lua_crash_context,
        DLUMA_ARRAY_COUNT(g_dluma_lua_crash_context),
        g_dluma_lua_runtime.current_project_name[0]
            ? g_dluma_lua_runtime.current_project_name : "<none>");
    dluma_str_append(
        g_dluma_lua_crash_context,
        DLUMA_ARRAY_COUNT(g_dluma_lua_crash_context), "\r\n");
    for (index = 0; index < DLUMA_LUA_RUNTIME_MAX_STATES; ++index) {
        const DlumaLuaRuntimeSlot* slot =
            &g_dluma_lua_runtime.slots[index];
        if (!slot->occupied) continue;
        dluma_str_append(
            g_dluma_lua_crash_context,
            DLUMA_ARRAY_COUNT(g_dluma_lua_crash_context),
            "[CRASH] [INFO] lua_mod id=");
        dluma_str_append(
            g_dluma_lua_crash_context,
            DLUMA_ARRAY_COUNT(g_dluma_lua_crash_context),
            slot->metadata.id);
        dluma_str_append(
            g_dluma_lua_crash_context,
            DLUMA_ARRAY_COUNT(g_dluma_lua_crash_context), " version=");
        dluma_str_append(
            g_dluma_lua_crash_context,
            DLUMA_ARRAY_COUNT(g_dluma_lua_crash_context),
            slot->metadata.version);
        dluma_str_append(
            g_dluma_lua_crash_context,
            DLUMA_ARRAY_COUNT(g_dluma_lua_crash_context), " scope=");
        dluma_u64_to_text(
            slot->metadata.scope, number, DLUMA_ARRAY_COUNT(number));
        dluma_str_append(
            g_dluma_lua_crash_context,
            DLUMA_ARRAY_COUNT(g_dluma_lua_crash_context), number);
        dluma_str_append(
            g_dluma_lua_crash_context,
            DLUMA_ARRAY_COUNT(g_dluma_lua_crash_context), " alive=");
        dluma_str_append(
            g_dluma_lua_crash_context,
            DLUMA_ARRAY_COUNT(g_dluma_lua_crash_context),
            slot->status.alive ? "1" : "0");
        dluma_str_append(
            g_dluma_lua_crash_context,
            DLUMA_ARRAY_COUNT(g_dluma_lua_crash_context), " disabled=");
        dluma_str_append(
            g_dluma_lua_crash_context,
            DLUMA_ARRAY_COUNT(g_dluma_lua_crash_context),
            slot->status.disabled ? "1" : "0");
        dluma_str_append(
            g_dluma_lua_crash_context,
            DLUMA_ARRAY_COUNT(g_dluma_lua_crash_context), " memory_bytes=");
        dluma_u64_to_text(
            slot->status.memory_used_bytes,
            number, DLUMA_ARRAY_COUNT(number));
        dluma_str_append(
            g_dluma_lua_crash_context,
            DLUMA_ARRAY_COUNT(g_dluma_lua_crash_context), number);
        dluma_str_append(
            g_dluma_lua_crash_context,
            DLUMA_ARRAY_COUNT(g_dluma_lua_crash_context), " sha256=");
        dluma_str_append(
            g_dluma_lua_crash_context,
            DLUMA_ARRAY_COUNT(g_dluma_lua_crash_context),
            slot->metadata.content_sha256);
        dluma_str_append(
            g_dluma_lua_crash_context,
            DLUMA_ARRAY_COUNT(g_dluma_lua_crash_context), " source=");
        dluma_str_append(
            g_dluma_lua_crash_context,
            DLUMA_ARRAY_COUNT(g_dluma_lua_crash_context),
            slot->metadata.source_identity);
        if (slot->status.last_error[0]) {
            dluma_str_append(
                g_dluma_lua_crash_context,
                DLUMA_ARRAY_COUNT(g_dluma_lua_crash_context), " error=");
            dluma_str_append(
                g_dluma_lua_crash_context,
                DLUMA_ARRAY_COUNT(g_dluma_lua_crash_context),
                slot->status.last_error);
        }
        dluma_str_append(
            g_dluma_lua_crash_context,
            DLUMA_ARRAY_COUNT(g_dluma_lua_crash_context), "\r\n");
    }
    g_dluma_lua_crash_context_last_ms = now;
    dluma_atomic_increment(&g_dluma_lua_crash_context_sequence);
}

static void dluma_lua_runtime_lock(volatile LONG* lock)
{
    while (dluma_atomic_compare_exchange(lock, 1, 0) != 0) Sleep(0);
}

static void dluma_lua_runtime_unlock(volatile LONG* lock)
{
    dluma_atomic_exchange(lock, 0);
}

static int dluma_lua_runtime_token_equals(const char* start, unsigned int length, const char* expected)
{
    unsigned int i = 0;
    while (i < length && expected[i]) {
        if (dluma_ascii_lower(start[i]) != dluma_ascii_lower(expected[i])) return 0;
        ++i;
    }
    return i == length && !expected[i];
}

static uint32_t dluma_lua_runtime_permission_bit(const char* start, unsigned int length)
{
#define DLUMA_PERMISSION_TOKEN(text, bit) \
    if (dluma_lua_runtime_token_equals(start, length, text)) return bit
    DLUMA_PERMISSION_TOKEN("game.read", DLUMA_LUA54_PERMISSION_GAME_READ);
    DLUMA_PERMISSION_TOKEN("player.read", DLUMA_LUA54_PERMISSION_PLAYER_READ);
    DLUMA_PERMISSION_TOKEN("world.query", DLUMA_LUA54_PERMISSION_WORLD_QUERY);
    DLUMA_PERMISSION_TOKEN("events.sensor", DLUMA_LUA54_PERMISSION_EVENTS_SENSOR);
    DLUMA_PERMISSION_TOKEN("events.interact", DLUMA_LUA54_PERMISSION_EVENTS_INTERACT);
    DLUMA_PERMISSION_TOKEN("events.enemy_killed", DLUMA_LUA54_PERMISSION_EVENTS_ENEMY_KILLED);
    DLUMA_PERMISSION_TOKEN("storage.mod", DLUMA_LUA54_PERMISSION_STORAGE_MOD);
    DLUMA_PERMISSION_TOKEN("ui.debug", DLUMA_LUA54_PERMISSION_UI_DEBUG);
    DLUMA_PERMISSION_TOKEN("ui.overlay", DLUMA_LUA54_PERMISSION_UI_OVERLAY);
    DLUMA_PERMISSION_TOKEN("audio.play", DLUMA_LUA54_PERMISSION_AUDIO_PLAY);
    DLUMA_PERMISSION_TOKEN("world.write", DLUMA_LUA54_PERMISSION_WORLD_WRITE);
    DLUMA_PERMISSION_TOKEN("world.transform", DLUMA_LUA54_PERMISSION_WORLD_TRANSFORM);
    DLUMA_PERMISSION_TOKEN("player.write", DLUMA_LUA54_PERMISSION_PLAYER_WRITE);
    DLUMA_PERMISSION_TOKEN("model.write", DLUMA_LUA54_PERMISSION_MODEL_WRITE);
    DLUMA_PERMISSION_TOKEN("model.animate", DLUMA_LUA54_PERMISSION_MODEL_ANIMATE);
    DLUMA_PERMISSION_TOKEN("map.write", DLUMA_LUA54_PERMISSION_MAP_WRITE);
    DLUMA_PERMISSION_TOKEN("camera.effect", DLUMA_LUA54_PERMISSION_CAMERA_EFFECT);
    DLUMA_PERMISSION_TOKEN("ai.spawn", DLUMA_LUA54_PERMISSION_AI_SPAWN);
    DLUMA_PERMISSION_TOKEN("cinematic.control", DLUMA_LUA54_PERMISSION_CINEMATIC_CONTROL);
    DLUMA_PERMISSION_TOKEN("inventory.write", DLUMA_LUA54_PERMISSION_INVENTORY_WRITE);
    DLUMA_PERMISSION_TOKEN("quest.write", DLUMA_LUA54_PERMISSION_QUEST_WRITE);
    DLUMA_PERMISSION_TOKEN("world.global_state", DLUMA_LUA54_PERMISSION_WORLD_GLOBAL_STATE);
    DLUMA_PERMISSION_TOKEN("window.control", DLUMA_LUA54_PERMISSION_WINDOW_CONTROL);
    DLUMA_PERMISSION_TOKEN("filesystem.workshop_read", DLUMA_LUA54_PERMISSION_FILESYSTEM_WORKSHOP_READ);
    DLUMA_PERMISSION_TOKEN("filesystem.arbitrary", DLUMA_LUA54_PERMISSION_FILESYSTEM_ARBITRARY);
    DLUMA_PERMISSION_TOKEN("process.launch", DLUMA_LUA54_PERMISSION_PROCESS_LAUNCH);
    DLUMA_PERMISSION_TOKEN("native.ffi", DLUMA_LUA54_PERMISSION_NATIVE_FFI);
    DLUMA_PERMISSION_TOKEN("quest.phase.handle", DLUMA_LUA54_PERMISSION_QUEST_PHASE_HANDLE);
    DLUMA_PERMISSION_TOKEN("quest.phase.observe", DLUMA_LUA54_PERMISSION_QUEST_PHASE_OBSERVE);
    DLUMA_PERMISSION_TOKEN("editor.quest.extend", DLUMA_LUA54_PERMISSION_EDITOR_QUEST_EXTEND);
#undef DLUMA_PERMISSION_TOKEN
    return 0;
}

static uint32_t dluma_lua_runtime_requested_permissions(const char* text)
{
    uint32_t result = 0;
    const char* cursor = text;
    while (cursor && *cursor) {
        const char* start;
        const char* end;
        while (*cursor == ',' || dluma_is_space(*cursor)) ++cursor;
        start = cursor;
        while (*cursor && *cursor != ',') ++cursor;
        end = cursor;
        while (end > start && dluma_is_space(end[-1])) --end;
        if (end > start)
            result |= dluma_lua_runtime_permission_bit(start, (unsigned int)(end - start));
        if (*cursor == ',') ++cursor;
    }
    return result;
}

static uint32_t dluma_lua_runtime_local_policy_permissions(void)
{
    uint32_t permissions = 0;
    DWORD policy = g_dluma.config.lua_local_permission_policy;
#define DLUMA_APPLY_LOCAL_POLICY(policy_bit, permission_bit) \
    do { \
        if (policy & (policy_bit)) permissions |= (permission_bit); \
    } while (0)
    DLUMA_APPLY_LOCAL_POLICY(DLUMA_LOCAL_LUA_POLICY_GAME_READ,
                           DLUMA_LUA54_PERMISSION_GAME_READ);
    DLUMA_APPLY_LOCAL_POLICY(DLUMA_LOCAL_LUA_POLICY_PLAYER_READ,
                           DLUMA_LUA54_PERMISSION_PLAYER_READ);
    DLUMA_APPLY_LOCAL_POLICY(DLUMA_LOCAL_LUA_POLICY_WORLD_QUERY,
                           DLUMA_LUA54_PERMISSION_WORLD_QUERY);
    DLUMA_APPLY_LOCAL_POLICY(DLUMA_LOCAL_LUA_POLICY_EVENTS_SENSOR,
                           DLUMA_LUA54_PERMISSION_EVENTS_SENSOR);
    DLUMA_APPLY_LOCAL_POLICY(DLUMA_LOCAL_LUA_POLICY_EVENTS_INTERACT,
                           DLUMA_LUA54_PERMISSION_EVENTS_INTERACT);
    DLUMA_APPLY_LOCAL_POLICY(DLUMA_LOCAL_LUA_POLICY_STORAGE_MOD,
                           DLUMA_LUA54_PERMISSION_STORAGE_MOD);
    DLUMA_APPLY_LOCAL_POLICY(DLUMA_LOCAL_LUA_POLICY_UI_DEBUG,
                           DLUMA_LUA54_PERMISSION_UI_DEBUG);
    DLUMA_APPLY_LOCAL_POLICY(DLUMA_LOCAL_LUA_POLICY_UI_OVERLAY,
                           DLUMA_LUA54_PERMISSION_UI_OVERLAY);
    DLUMA_APPLY_LOCAL_POLICY(DLUMA_LOCAL_LUA_POLICY_AUDIO_PLAY,
                           DLUMA_LUA54_PERMISSION_AUDIO_PLAY);
    DLUMA_APPLY_LOCAL_POLICY(DLUMA_LOCAL_LUA_POLICY_WORLD_WRITE,
                           DLUMA_LUA54_PERMISSION_WORLD_WRITE);
    DLUMA_APPLY_LOCAL_POLICY(DLUMA_LOCAL_LUA_POLICY_QUEST_WRITE,
                           DLUMA_LUA54_PERMISSION_QUEST_WRITE);
    DLUMA_APPLY_LOCAL_POLICY(DLUMA_LOCAL_LUA_POLICY_EVENTS_ENEMY_KILLED,
                           DLUMA_LUA54_PERMISSION_EVENTS_ENEMY_KILLED);
    DLUMA_APPLY_LOCAL_POLICY(DLUMA_LOCAL_LUA_POLICY_QUEST_PHASE_HANDLE,
                           DLUMA_LUA54_PERMISSION_QUEST_PHASE_HANDLE);
    DLUMA_APPLY_LOCAL_POLICY(DLUMA_LOCAL_LUA_POLICY_QUEST_PHASE_OBSERVE,
                           DLUMA_LUA54_PERMISSION_QUEST_PHASE_OBSERVE);
#undef DLUMA_APPLY_LOCAL_POLICY
    return permissions;
}

static uint32_t dluma_lua_runtime_approved_permissions(const DlumaLuaMetadata* metadata)
{
    const uint32_t standard =
        DLUMA_LUA54_PERMISSION_GAME_READ |
        DLUMA_LUA54_PERMISSION_PLAYER_READ |
        DLUMA_LUA54_PERMISSION_WORLD_QUERY |
        DLUMA_LUA54_PERMISSION_EVENTS_SENSOR |
        DLUMA_LUA54_PERMISSION_EVENTS_INTERACT |
        DLUMA_LUA54_PERMISSION_EVENTS_ENEMY_KILLED |
        DLUMA_LUA54_PERMISSION_STORAGE_MOD |
        DLUMA_LUA54_PERMISSION_UI_DEBUG |
        DLUMA_LUA54_PERMISSION_UI_OVERLAY |
        DLUMA_LUA54_PERMISSION_AUDIO_PLAY |
        DLUMA_LUA54_PERMISSION_QUEST_PHASE_OBSERVE;
    /* Global scripts keep their per-identity consent record. Project/map
     * scripts must both declare a permission and pass the player's bounded
     * local-script policy. OS-facing permissions are never granted here. */
    uint32_t requested = dluma_lua_runtime_requested_permissions(metadata->permissions);
    if (metadata->scope == DLUMA_LUA_SCOPE_GLOBAL && metadata->approval_valid)
        return requested & (standard | DLUMA_LUA54_PERMISSION_WORLD_WRITE |
                            DLUMA_LUA54_PERMISSION_QUEST_WRITE |
                            DLUMA_LUA54_PERMISSION_QUEST_PHASE_HANDLE);
    if (metadata->scope != DLUMA_LUA_SCOPE_GLOBAL)
        return requested & dluma_lua_runtime_local_policy_permissions();
    return requested & standard;
}

static int dluma_lua_runtime_key_valid(const char* key)
{
    unsigned int i;
    if (!key || !key[0]) return 0;
    for (i = 0; key[i] && i <= 64; ++i) {
        char c = key[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.'))
            return 0;
    }
    return i > 0 && i <= 64 && !key[i];
}

static int dluma_lua_runtime_sha256_key_valid(const char* key)
{
    unsigned int i;
    if (!key) return 0;
    for (i = 0; i < 64U; ++i) {
        char value = key[i];
        if (!((value >= '0' && value <= '9') ||
              (value >= 'a' && value <= 'f') ||
              (value >= 'A' && value <= 'F')))
            return 0;
    }
    return key[64] == 0;
}

static int dluma_lua_runtime_storage_path(
    DlumaLuaRuntimeSlot* slot,
    const char* key,
    WCHAR* directory,
    WCHAR* path,
    unsigned int capacity)
{
    WCHAR runtime_dir[DLUMA_MAX_WPATH];
    WCHAR mod_data[DLUMA_MAX_WPATH];
    WCHAR storage_key[DLUMA_SHA256_HEX_CAPACITY];
    WCHAR legacy_id[160];
    WCHAR legacy_directory[DLUMA_MAX_WPATH];
    WCHAR key_wide[96];
    if (!slot || !dluma_lua_runtime_key_valid(key) ||
        !dluma_lua_runtime_sha256_key_valid(slot->metadata.storage_key))
        return 0;
    dluma_path_join(runtime_dir, DLUMA_ARRAY_COUNT(runtime_dir), g_dluma.module_dir, L"dluma_runtime");
    dluma_path_join(mod_data, DLUMA_ARRAY_COUNT(mod_data), runtime_dir, L"mod_data");
    if (!dluma_utf8_to_wide(slot->metadata.storage_key, storage_key,
                          DLUMA_ARRAY_COUNT(storage_key)) ||
        !dluma_utf8_to_wide(key, key_wide, DLUMA_ARRAY_COUNT(key_wide)))
        return 0;
    dluma_path_join(directory, capacity, mod_data, storage_key);
    /* Legacy ID directories have no trustworthy owner marker.  Never merge
     * them into a new logical namespace: two historic source identities may
     * have shared the same lossy directory.  The deterministic migration is
     * therefore fail-closed; data stays preserved in place for an explicit
     * user-reviewed migration. */
    legacy_id[0] = 0;
    legacy_directory[0] = 0;
    if (!slot->legacy_storage_logged &&
        dluma_utf8_to_wide(slot->metadata.id, legacy_id,
                         DLUMA_ARRAY_COUNT(legacy_id))) {
        dluma_path_join(legacy_directory, DLUMA_ARRAY_COUNT(legacy_directory),
                      mod_data, legacy_id);
        if (dluma_dir_exists(legacy_directory) &&
            dluma_wstricmp(legacy_directory, directory) != 0) {
            slot->legacy_storage_logged = 1;
            dluma_log_pair("WARN", "lua_legacy_storage_left_unclaimed",
                         slot->metadata.id);
        }
    }
    if (!dluma_mkdirs(directory)) return 0;
    dluma_wstr_append(key_wide, DLUMA_ARRAY_COUNT(key_wide), L".dlumas");
    dluma_path_join(path, capacity, directory, key_wide);
    return 1;
}

static uint64_t dluma_lua_runtime_storage_usage(const WCHAR* directory)
{
    WCHAR pattern[DLUMA_MAX_WPATH];
    WIN32_FIND_DATAW data;
    HANDLE find;
    uint64_t total = 0;
    dluma_path_join(pattern, DLUMA_ARRAY_COUNT(pattern), directory, L"*.dlumas");
    find = FindFirstFileW(pattern, &data);
    if (find == INVALID_HANDLE_VALUE) return 0;
    do {
        if (!(data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
            total += ((uint64_t)data.nFileSizeHigh << 32) | data.nFileSizeLow;
    } while (FindNextFileW(find, &data));
    FindClose(find);
    return total;
}

static int dluma_lua_runtime_storage_get(
    void* host_user,
    const char* key,
    uint32_t* value_type,
    char* value,
    uint32_t value_capacity)
{
    DlumaLuaRuntimeSlot* slot = (DlumaLuaRuntimeSlot*)host_user;
    WCHAR directory[DLUMA_MAX_WPATH];
    WCHAR path[DLUMA_MAX_WPATH];
    char* data = 0;
    DWORD size = 0;
    DWORD attributes;
    char* payload;
    if (!value_type || !value || !value_capacity ||
        !dluma_lua_runtime_storage_path(slot, key, directory, path,
                                      DLUMA_ARRAY_COUNT(path)))
        return 0;
    /* A missing scalar is a successful lookup with VALUE_NONE.  Do not
     * conflate it with an unreadable, malformed, oversized, or racing file:
     * callers that persist recovery state (notably Lua quests) must never
     * overwrite a valid record merely because its read failed this frame. */
    attributes = GetFileAttributesW(path);
    if (attributes == INVALID_FILE_ATTRIBUTES) {
        DWORD path_error = GetLastError();
        if (path_error == ERROR_FILE_NOT_FOUND ||
            path_error == ERROR_PATH_NOT_FOUND) {
            *value_type = DLUMA_LUA54_VALUE_NONE;
            value[0] = 0;
            return 1;
        }
        return 0;
    }
    if (!dluma_read_file_utf8(path, &data, &size)) return 0;
    if (size < 2 || data[1] != '\n' || size - 2 >= value_capacity) {
        dluma_free(data);
        return 0;
    }
    if (data[0] == 's') *value_type = DLUMA_LUA54_VALUE_STRING;
    else if (data[0] == 'n') *value_type = DLUMA_LUA54_VALUE_NUMBER;
    else if (data[0] == 'b') *value_type = DLUMA_LUA54_VALUE_BOOLEAN;
    else {
        dluma_free(data);
        return 0;
    }
    payload = data + 2;
    dluma_str_copy(value, value_capacity, payload);
    dluma_free(data);
    return 1;
}

static int dluma_lua_runtime_write_atomic(const WCHAR* path, const char* data, DWORD size)
{
    WCHAR temporary[DLUMA_MAX_WPATH];
    HANDLE file;
    DWORD written = 0;
    int ok = 0;
    dluma_wstr_copy(temporary, DLUMA_ARRAY_COUNT(temporary), path);
    dluma_wstr_append(temporary, DLUMA_ARRAY_COUNT(temporary), L".tmp");
    file = CreateFileW(temporary, GENERIC_WRITE, 0, 0, CREATE_ALWAYS,
                       FILE_ATTRIBUTE_NORMAL, 0);
    if (file == INVALID_HANDLE_VALUE) return 0;
    if (WriteFile(file, data, size, &written, 0) && written == size &&
        FlushFileBuffers(file)) ok = 1;
    CloseHandle(file);
    if (!ok || !MoveFileExW(temporary, path,
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(temporary);
        return 0;
    }
    return 1;
}

static int dluma_lua_runtime_storage_set(
    void* host_user,
    const char* key,
    uint32_t value_type,
    const char* value)
{
    DlumaLuaRuntimeSlot* slot = (DlumaLuaRuntimeSlot*)host_user;
    WCHAR directory[DLUMA_MAX_WPATH];
    WCHAR path[DLUMA_MAX_WPATH];
    char* encoded;
    SIZE_T value_length;
    SIZE_T encoded_size;
    char prefix;
    uint64_t existing_size = 0;
    LARGE_INTEGER old_size;
    HANDLE old_file;
    int result;
    if (!value || !dluma_lua_runtime_storage_path(slot, key, directory, path,
                                                 DLUMA_ARRAY_COUNT(path)))
        return 0;
    value_length = strlen(value);
    if (value_length > DLUMA_LUA_STORAGE_VALUE_LIMIT) return 0;
    if (value_type == DLUMA_LUA54_VALUE_STRING) prefix = 's';
    else if (value_type == DLUMA_LUA54_VALUE_NUMBER) prefix = 'n';
    else if (value_type == DLUMA_LUA54_VALUE_BOOLEAN) prefix = 'b';
    else return 0;
    old_file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, 0, OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL, 0);
    if (old_file != INVALID_HANDLE_VALUE) {
        if (GetFileSizeEx(old_file, &old_size) && old_size.QuadPart > 0)
            existing_size = (uint64_t)old_size.QuadPart;
        CloseHandle(old_file);
    }
    encoded_size = value_length + 2;
    if (dluma_lua_runtime_storage_usage(directory) - existing_size + encoded_size >
        DLUMA_LUA_STORAGE_TOTAL_LIMIT)
        return 0;
    encoded = (char*)dluma_alloc(encoded_size);
    if (!encoded) return 0;
    encoded[0] = prefix;
    encoded[1] = '\n';
    memcpy(encoded + 2, value, value_length);
    result = dluma_lua_runtime_write_atomic(path, encoded, (DWORD)encoded_size);
    dluma_free(encoded);
    return result;
}

static int dluma_lua_runtime_storage_delete(void* host_user, const char* key)
{
    DlumaLuaRuntimeSlot* slot = (DlumaLuaRuntimeSlot*)host_user;
    WCHAR directory[DLUMA_MAX_WPATH];
    WCHAR path[DLUMA_MAX_WPATH];
    DWORD attributes;
    if (!dluma_lua_runtime_storage_path(slot, key, directory, path, DLUMA_ARRAY_COUNT(path)))
        return 0;
    attributes = GetFileAttributesW(path);
    if (attributes == INVALID_FILE_ATTRIBUTES) return 1;
    return DeleteFileW(path) != 0;
}

static uint64_t dluma_lua_runtime_clock(void* host_user)
{
    (void)host_user;
    return GetTickCount64();
}

static void dluma_lua_runtime_log(void* host_user, const char* level, const char* text)
{
    DlumaLuaRuntimeSlot* slot = (DlumaLuaRuntimeSlot*)host_user;
    char line[DLUMA_MAX_TEXT * 4];
    const char* normalized = "INFO";
    line[0] = 0;
    if (slot && slot->metadata.id[0]) {
        dluma_str_append(line, DLUMA_ARRAY_COUNT(line), slot->metadata.id);
        dluma_str_append(line, DLUMA_ARRAY_COUNT(line), ": ");
    }
    dluma_str_append(line, DLUMA_ARRAY_COUNT(line), text ? text : "");
    if (level && dluma_stricmp(level, "error") == 0) normalized = "ERROR";
    else if (level && dluma_stricmp(level, "warn") == 0) normalized = "WARN";
    else if (level && dluma_stricmp(level, "debug") == 0) normalized = "DEBUG";
    dluma_log_line(normalized, line);
}

static void dluma_lua_runtime_copy_debug_text(
    char* out, unsigned int capacity, const char* text)
{
    unsigned int i = 0;
    if (!out || !capacity) return;
    while (text && text[i] && i + 1 < capacity) {
        unsigned char c = (unsigned char)text[i];
        out[i] = (c < 32U || c == 127U) ? ' ' : (char)c;
        ++i;
    }
    out[i] = 0;
}

static int dluma_lua_runtime_debug_text_equals(const char* left, const char* right)
{
    unsigned int i = 0;
    if (!left) left = "";
    if (!right) right = "";
    while (left[i] && right[i]) {
        if (left[i] != right[i]) return 0;
        ++i;
    }
    return left[i] == right[i];
}

static int dluma_lua_runtime_store_debug_value(
    DlumaLuaRuntimeSlot* slot, const char* key, const char* value)
{
    unsigned int i;
    char normalized_key[DLUMA_LUA_RUNTIME_DEBUG_KEY];
    char normalized_value[DLUMA_LUA_RUNTIME_DEBUG_TEXT];
    if (!slot || !key || !key[0]) return 0;
    dluma_lua_runtime_copy_debug_text(
        normalized_key, DLUMA_ARRAY_COUNT(normalized_key), key);
    dluma_lua_runtime_copy_debug_text(
        normalized_value, DLUMA_ARRAY_COUNT(normalized_value), value);
    if (!normalized_key[0]) return 0;
    for (i = 0; i < slot->debug_value_count; ++i) {
        if (dluma_stricmp(slot->debug_values[i].key, normalized_key) == 0) break;
    }
    if (i < slot->debug_value_count &&
        dluma_lua_runtime_debug_text_equals(
            slot->debug_values[i].value, normalized_value))
        return 0;
    if (i == slot->debug_value_count) {
        if (slot->debug_value_count >= DLUMA_LUA_RUNTIME_MAX_DEBUG_VALUES) {
            if (!slot->debug_value_overflow_logged) {
                slot->debug_value_overflow_logged = 1;
                dluma_log_pair("WARN", "lua_debug_value_limit_reached",
                             slot->metadata.id);
            }
            return 0;
        }
        ++slot->debug_value_count;
    }
    dluma_lua_runtime_copy_debug_text(
        slot->debug_values[i].key,
        DLUMA_ARRAY_COUNT(slot->debug_values[i].key), normalized_key);
    dluma_lua_runtime_copy_debug_text(
        slot->debug_values[i].value,
        DLUMA_ARRAY_COUNT(slot->debug_values[i].value), normalized_value);
    return 1;
}

static int dluma_lua_runtime_remove_debug_value(
    DlumaLuaRuntimeSlot* slot, const char* key)
{
    unsigned int i;
    if (!slot || !key || !key[0]) return 0;
    for (i = 0; i < slot->debug_value_count; ++i) {
        if (dluma_stricmp(slot->debug_values[i].key, key) == 0) {
            for (; i + 1 < slot->debug_value_count; ++i)
                slot->debug_values[i] = slot->debug_values[i + 1];
            --slot->debug_value_count;
            memset(&slot->debug_values[slot->debug_value_count], 0,
                   sizeof(slot->debug_values[slot->debug_value_count]));
            return 1;
        }
    }
    return 0;
}

static void dluma_lua_runtime_append_debug_line(
    DlumaLuaRuntimeSlot* slot, const char* value)
{
    unsigned int index;
    if (!slot) return;
    if (slot->debug_line_count < DLUMA_LUA_RUNTIME_MAX_DEBUG_LINES) {
        index = (slot->debug_line_head + slot->debug_line_count) %
            DLUMA_LUA_RUNTIME_MAX_DEBUG_LINES;
        ++slot->debug_line_count;
    } else {
        index = slot->debug_line_head;
        slot->debug_line_head = (slot->debug_line_head + 1) %
            DLUMA_LUA_RUNTIME_MAX_DEBUG_LINES;
    }
    dluma_lua_runtime_copy_debug_text(
        slot->debug_lines[index],
        DLUMA_ARRAY_COUNT(slot->debug_lines[index]), value);
}

static void dluma_lua_runtime_debug_set(
    void* host_user,
    const char* key,
    const char* value,
    int remove_value)
{
    DlumaLuaRuntimeSlot* slot = (DlumaLuaRuntimeSlot*)host_user;
    char line[1024];
    line[0] = 0;
    if (slot) dluma_str_append(line, DLUMA_ARRAY_COUNT(line), slot->metadata.id);
    dluma_str_append(line, DLUMA_ARRAY_COUNT(line), ".");
    dluma_str_append(line, DLUMA_ARRAY_COUNT(line), key ? key : "");
    if (remove_value == 2) {
        dluma_str_append(line, DLUMA_ARRAY_COUNT(line), ": ");
        dluma_str_append(line, DLUMA_ARRAY_COUNT(line), value ? value : "");
        dluma_lua_runtime_append_debug_line(slot, value ? value : "");
        dluma_log_pair("DEBUG", "lua_debug_append", line);
        return;
    }
    if (!remove_value) {
        if (!dluma_lua_runtime_store_debug_value(
                slot, key, value ? value : "")) return;
        dluma_str_append(line, DLUMA_ARRAY_COUNT(line), "=");
        dluma_str_append(line, DLUMA_ARRAY_COUNT(line), value ? value : "");
    } else {
        if (!dluma_lua_runtime_remove_debug_value(slot, key)) return;
    }
    dluma_log_pair("DEBUG", remove_value ? "lua_debug_removed" : "lua_debug", line);
}

static void dluma_lua_runtime_host_error(
    DlumaLua54Error* error,
    const char* code,
    const char* message,
    const char* capability,
    const char* permission,
    const char* object_name)
{
    if (!error || error->struct_size < sizeof(*error)) return;
    error->retryable = 0;
    dluma_str_copy(error->code, DLUMA_ARRAY_COUNT(error->code), code ? code : "host_error");
    dluma_str_copy(error->message, DLUMA_ARRAY_COUNT(error->message), message ? message : "host adapter failed");
    dluma_str_copy(error->capability, DLUMA_ARRAY_COUNT(error->capability), capability ? capability : "");
    dluma_str_copy(error->permission, DLUMA_ARRAY_COUNT(error->permission), permission ? permission : "");
    dluma_str_copy(error->object_name, DLUMA_ARRAY_COUNT(error->object_name), object_name ? object_name : "");
}

static int dluma_lua_runtime_api_status(
    DlumaStatus status,
    DlumaLua54Error* error,
    const char* capability,
    const char* permission,
    const char* object_name)
{
    const char* code;
    const char* message;
    if (status == DLUMA_STATUS_OK) return 1;
    switch (status) {
    case DLUMA_STATUS_INVALID_ARGUMENT:
        code = "invalid_argument";
        message = "the DLUMA API rejected an invalid request";
        break;
    case DLUMA_STATUS_UNAVAILABLE:
        code = "unsupported_capability";
        message = "the requested DLUMA capability is not ready";
        break;
    case DLUMA_STATUS_ACCESS_DENIED:
        code = "permission_denied";
        message = "the mod manifest did not declare the required access";
        break;
    case DLUMA_STATUS_SCOPE_DENIED:
        code = "scope_denied";
        message = "the operation is not valid in this mod scope";
        break;
    case DLUMA_STATUS_WRONG_THREAD:
        code = "wrong_thread";
        message = "the operation must run on the game thread";
        break;
    case DLUMA_STATUS_STALE_HANDLE:
        code = "stale_handle";
        message = "the object handle belongs to an earlier map generation";
        break;
    case DLUMA_STATUS_FOREIGN_HANDLE:
        code = "foreign_handle";
        message = "the object handle belongs to another mod owner";
        break;
    case DLUMA_STATUS_NOT_FOUND:
        code = "not_found";
        message = "the requested game object was not found";
        break;
    case DLUMA_STATUS_LIMIT_EXCEEDED:
        code = "limit_exceeded";
        message = "the mod exceeded its shared DLUMA resource quota";
        break;
    case DLUMA_STATUS_INSUFFICIENT_BUFFER:
        code = "insufficient_buffer";
        message = "the DLUMA result did not fit the bounded output buffer";
        break;
    case DLUMA_STATUS_OWNER_REVOKED:
        code = "script_owner_inactive";
        message = "the script owner is no longer active";
        break;
    case DLUMA_STATUS_REMOVED:
        code = "removed_operation";
        message = "the requested operation was removed from DLUMA API 1.0";
        break;
    default:
        code = "host_error";
        message = "the shared DLUMA game service failed";
        break;
    }
    dluma_lua_runtime_host_error(
        error, code, message, capability, permission, object_name);
    if (error)
        error->retryable =
            status == DLUMA_STATUS_UNAVAILABLE ||
            status == DLUMA_STATUS_WRONG_THREAD;
    return 0;
}

static DlumaStringView dluma_lua_runtime_string_view(
    const char* value)
{
    DlumaStringView view;
    view.data = value ? value : "";
    view.size = value ? (uint64_t)strlen(value) : 0U;
    return view;
}

static int dluma_lua_runtime_host_slot(
    void* host_user,
    DlumaLuaRuntimeSlot** out_slot,
    DlumaLua54Error* error)
{
    DlumaLuaRuntimeSlot* slot = (DlumaLuaRuntimeSlot*)host_user;
    if (!slot || !slot->occupied || !slot->owner_id ||
        slot->api_owner.state != DLUMA_MOD_OWNER_ACTIVE) {
        dluma_lua_runtime_host_error(error, "script_owner_inactive",
            "the script owner is no longer active", "lua.runtime", "", "");
        return 0;
    }
    if (out_slot) *out_slot = slot;
    return 1;
}

static int dluma_lua_runtime_module_path(
    const char* module_name,
    char* relative_path,
    unsigned int capacity)
{
    unsigned int source = 0;
    unsigned int destination = 0;
    int segment_has_character = 0;
    if (!module_name || !module_name[0] ||
        !relative_path || capacity < 6U)
        return 0;
    while (module_name[source]) {
        char value = module_name[source++];
        if (source > 128U || destination + 5U >= capacity)
            return 0;
        if (value == '.') {
            if (!segment_has_character)
                return 0;
            relative_path[destination++] = '/';
            segment_has_character = 0;
            continue;
        }
        if (!((value >= 'a' && value <= 'z') ||
              (value >= 'A' && value <= 'Z') ||
              (value >= '0' && value <= '9') ||
              value == '_'))
            return 0;
        relative_path[destination++] = value;
        segment_has_character = 1;
    }
    if (!segment_has_character)
        return 0;
    relative_path[destination] = 0;
    dluma_str_append(relative_path, capacity, ".lua");
    return relative_path[destination] != 0;
}

static int dluma_lua_runtime_module_load(
    void* host_user,
    const char* module_name,
    const char** source_out,
    size_t* source_size_out,
    void** artifact_token_out,
    DlumaLua54Error* error)
{
    DlumaLuaRuntimeSlot* slot;
    DlumaModPackageSnapshot approved;
    DlumaModArtifactSnapshot* artifact;
    char relative_path[DLUMA_MOD_RELATIVE_PATH_CAPACITY];
    DlumaStatus status;
    if (source_out) *source_out = 0;
    if (source_size_out) *source_size_out = 0;
    if (artifact_token_out) *artifact_token_out = 0;
    if (!source_out || !source_size_out ||
        !artifact_token_out ||
        !dluma_lua_runtime_host_slot(
            host_user, &slot, error) ||
        !slot->metadata.manifest_present ||
        !dluma_lua_runtime_module_path(
            module_name, relative_path,
            DLUMA_ARRAY_COUNT(relative_path))) {
        dluma_lua_runtime_host_error(
            error, "invalid_module_name",
            "the requested Lua module name is invalid",
            "lua.runtime", "", module_name);
        return 0;
    }
    artifact = (DlumaModArtifactSnapshot*)dluma_alloc(
        sizeof(*artifact));
    if (!artifact) {
        dluma_lua_runtime_host_error(
            error, "module_snapshot_allocation_failed",
            "the immutable module snapshot could not be allocated",
            "lua.runtime", "", module_name);
        return 0;
    }
    memset(artifact, 0, sizeof(*artifact));
    artifact->struct_size = (unsigned int)sizeof(*artifact);
    memset(&approved, 0, sizeof(approved));
    approved.struct_size = (unsigned int)sizeof(approved);
    approved.valid = 1U;
    dluma_wstr_copy(
        approved.package_root,
        DLUMA_ARRAY_COUNT(approved.package_root),
        slot->metadata.package_root);
    dluma_str_copy(
        approved.manifest.id,
        DLUMA_ARRAY_COUNT(approved.manifest.id),
        slot->metadata.id);
    dluma_str_copy(
        approved.manifest_sha256,
        DLUMA_ARRAY_COUNT(approved.manifest_sha256),
        slot->metadata.manifest_sha256);
    dluma_str_copy(
        approved.lock_sha256,
        DLUMA_ARRAY_COUNT(approved.lock_sha256),
        slot->metadata.lock_sha256);
    status = dluma_mod_package_read_artifact(
        &approved, relative_path,
        DLUMA_MOD_ARTIFACT_SCRIPT_MODULE,
        DLUMA_MAX_SCRIPT_FILE_BYTES, artifact);
    if (status != DLUMA_STATUS_OK ||
        !artifact->valid || !artifact->bytes ||
        !artifact->size) {
        dluma_lua_runtime_host_error(
            error,
            status == DLUMA_STATUS_NOT_FOUND
                ? "module_not_found"
                : "module_changed_since_approval",
            artifact->diagnostic[0]
                ? artifact->diagnostic
                : "the package-local module is not an approved script module",
            "lua.runtime", "", module_name);
        dluma_mod_artifact_release(artifact);
        dluma_free(artifact);
        return 0;
    }
    *source_out = (const char*)artifact->bytes;
    *source_size_out = artifact->size;
    *artifact_token_out = artifact;
    return 1;
}

static void dluma_lua_runtime_module_release(
    void* host_user,
    void* artifact_token)
{
    DlumaModArtifactSnapshot* artifact =
        (DlumaModArtifactSnapshot*)artifact_token;
    (void)host_user;
    if (!artifact) return;
    dluma_mod_artifact_release(artifact);
    dluma_free(artifact);
}

static int dluma_lua_runtime_config_get(
    void* host_user,
    const char* key,
    uint32_t* value_type,
    char* value,
    uint32_t value_capacity,
    DlumaLua54Error* error)
{
    DlumaLuaRuntimeSlot* slot;
    DlumaLuaConfigSchema* schema;
    DlumaLuaConfigValue resolved;
    unsigned int setting_index;
    char encoded[DLUMA_LUA_CONFIG_VALUE_CAPACITY];
    char read_error[96];
    int has_override = 0;
    int result = 0;
    if (value_type) *value_type = DLUMA_LUA54_VALUE_NONE;
    if (value && value_capacity) value[0] = 0;
    if (!value_type || !value || !value_capacity ||
        !dluma_lua_runtime_host_slot(
            host_user, &slot, error))
        return 0;
    if (!slot->metadata.config_available ||
        !dluma_lua_config_identifier_valid(key)) {
        dluma_lua_runtime_host_error(
            error, "config_key_unknown",
            "the script did not declare this player preference",
            "config.read", "", key);
        return 0;
    }
    schema = (DlumaLuaConfigSchema*)dluma_alloc(
        sizeof(*schema));
    if (!schema) {
        dluma_lua_runtime_host_error(
            error, "config_snapshot_unavailable",
            "the immutable config schema could not be copied",
            "config.read", "", key);
        return 0;
    }
    memset(schema, 0, sizeof(*schema));
    if (!dluma_lua_config_snapshot(
            slot->metadata.registry_key,
            slot->metadata.metadata_fingerprint,
            slot->metadata.config_schema_fingerprint,
            schema) ||
        !dluma_lua_config_find_setting(
            schema, key, &setting_index)) {
        dluma_lua_runtime_host_error(
            error, "config_key_unknown",
            "the script did not declare this player preference",
            "config.read", "", key);
        goto cleanup;
    }
    read_error[0] = 0;
    if (!dluma_lua_config_read(
            &slot->metadata, schema, setting_index,
            &resolved, &has_override, read_error,
            DLUMA_ARRAY_COUNT(read_error)) ||
        !dluma_lua_config_value_to_text(
            &resolved, encoded,
            DLUMA_ARRAY_COUNT(encoded))) {
        dluma_lua_runtime_host_error(
            error,
            read_error[0] ? read_error
                          : "config_read_failed",
            "the declared player preference record is invalid or unreadable",
            "config.read", "", key);
        goto cleanup;
    }
    if (strlen(encoded) + 1U > value_capacity) {
        dluma_lua_runtime_host_error(
            error, "config_value_too_long",
            "the declared player preference exceeds the helper buffer",
            "config.read", "", key);
        goto cleanup;
    }
    dluma_str_copy(value, value_capacity, encoded);
    if (resolved.kind == DLUMA_LUA_CONFIG_BOOLEAN)
        *value_type = DLUMA_LUA54_VALUE_BOOLEAN;
    else if (resolved.kind == DLUMA_LUA_CONFIG_INTEGER)
        *value_type = DLUMA_LUA54_VALUE_INTEGER;
    else
        *value_type = DLUMA_LUA54_VALUE_STRING;
    result = 1;

cleanup:
    dluma_free(schema);
    return result;
}

static int dluma_lua_runtime_subscription_added(
    void* host_user,
    uint32_t token,
    uint32_t kind,
    const char* exact_name,
    DlumaLua54Error* error)
{
    DlumaLuaRuntimeSlot* slot;
    DlumaEventSubscribeRequest request;
    DlumaQuestPhaseRegistrationRequest phase_request;
    DlumaTokenResult result;
    DlumaStatus status;
    uint32_t operation_id;
    const char* capability;
    const char* permission;
    if (!dluma_lua_runtime_host_slot(host_user, &slot, error)) return 0;
    if (kind == DLUMA_LUA54_EVENT_SENSOR_ENTER)
        operation_id = 4003U;
    else if (kind == DLUMA_LUA54_EVENT_SENSOR_LEAVE)
        operation_id = 4004U;
    else if (kind == DLUMA_LUA54_EVENT_INTERACT_BEGIN)
        operation_id = 4005U;
    else if (kind == DLUMA_LUA54_EVENT_QUEST_PHASE_PROVIDER)
        operation_id = 12001U;
    else if (kind == DLUMA_LUA54_EVENT_QUEST_PHASE_OBSERVER)
        operation_id = 12002U;
    else {
        dluma_lua_runtime_host_error(
            error, "unsupported_event",
            "the event does not have a shared native subscription",
            "events", "", exact_name);
        return 0;
    }
    memset(&request, 0, sizeof(request));
    memset(&phase_request, 0, sizeof(phase_request));
    memset(&result, 0, sizeof(result));
    if (kind == DLUMA_LUA54_EVENT_QUEST_PHASE_PROVIDER ||
        kind == DLUMA_LUA54_EVENT_QUEST_PHASE_OBSERVER) {
        phase_request.struct_size = (uint32_t)sizeof(phase_request);
        phase_request.struct_version = 1U;
        phase_request.key = dluma_lua_runtime_string_view(exact_name);
        phase_request.callback_id = token;
    } else {
        request.struct_size = (uint32_t)sizeof(request);
        request.struct_version = 1U;
        request.name = dluma_lua_runtime_string_view(exact_name);
        request.callback_id = token;
    }
    result.struct_size = (uint32_t)sizeof(result);
    result.struct_version = 1U;
    status = dluma_api_runtime_invoke(
        &slot->api_runtime, operation_id,
        kind == DLUMA_LUA54_EVENT_QUEST_PHASE_PROVIDER ||
                kind == DLUMA_LUA54_EVENT_QUEST_PHASE_OBSERVER
            ? (const void*)&phase_request : (const void*)&request,
        kind == DLUMA_LUA54_EVENT_QUEST_PHASE_PROVIDER ||
                kind == DLUMA_LUA54_EVENT_QUEST_PHASE_OBSERVER
            ? (uint32_t)sizeof(phase_request) : (uint32_t)sizeof(request),
        &result, (uint32_t)sizeof(result));
    capability = kind == DLUMA_LUA54_EVENT_QUEST_PHASE_PROVIDER ||
            kind == DLUMA_LUA54_EVENT_QUEST_PHASE_OBSERVER
        ? "quests.phase_handlers" :
        kind == DLUMA_LUA54_EVENT_INTERACT_BEGIN
            ? "events.interact" : "events.sensor";
    permission = kind == DLUMA_LUA54_EVENT_QUEST_PHASE_PROVIDER
        ? "quest.phase.handle" :
        kind == DLUMA_LUA54_EVENT_QUEST_PHASE_OBSERVER
            ? "quest.phase.observe" :
        kind == DLUMA_LUA54_EVENT_INTERACT_BEGIN
            ? "events.interact" : "events.sensor";
    return dluma_lua_runtime_api_status(
        status, error, capability, permission, exact_name);
}

static int dluma_lua_runtime_subscription_removed(
    void* host_user,
    uint32_t token,
    uint32_t kind,
    const char* exact_name,
    DlumaLua54Error* error)
{
    DlumaLuaRuntimeSlot* slot;
    int removed;
    if (!dluma_lua_runtime_host_slot(host_user, &slot, error)) return 0;
    if (kind == DLUMA_LUA54_EVENT_QUEST_PHASE_PROVIDER ||
        kind == DLUMA_LUA54_EVENT_QUEST_PHASE_OBSERVER)
        return dluma_quest_phase_service_unregister_callback(
            slot->owner_id,
            kind == DLUMA_LUA54_EVENT_QUEST_PHASE_PROVIDER
                ? DLUMA_MOD_EVENT_QUEST_PHASE_PROVIDER
                : DLUMA_MOD_EVENT_QUEST_PHASE_OBSERVER,
            token, exact_name);
    removed = dluma_native_gameplay_subscription_remove(
        slot->owner_id, token, kind, exact_name, error);
    if (removed && slot->api_owner.active_subscriptions)
        --slot->api_owner.active_subscriptions;
    return removed;
}

static int dluma_lua_runtime_world_find(
    void* host_user,
    const char* exact_name,
    const char* expected_class,
    DlumaLua54Object* object,
    DlumaLua54Error* error)
{
    DlumaLuaRuntimeSlot* slot;
    DlumaWorldFindRequest request;
    DlumaObjectResult result;
    DlumaStatus status;
    if (!dluma_lua_runtime_host_slot(host_user, &slot, error)) return 0;
    if (!object || object->struct_size < sizeof(*object)) {
        dluma_lua_runtime_host_error(
            error, "invalid_argument",
            "world.find requires a complete output record",
            "world.find", "world.query", exact_name);
        return 0;
    }
    memset(&request, 0, sizeof(request));
    memset(&result, 0, sizeof(result));
    memset(object, 0, sizeof(*object));
    object->struct_size = sizeof(*object);
    request.struct_size = (uint32_t)sizeof(request);
    request.struct_version = 1U;
    request.name = dluma_lua_runtime_string_view(exact_name);
    request.expected_class =
        dluma_lua_runtime_string_view(expected_class);
    result.struct_size = (uint32_t)sizeof(result);
    result.struct_version = 1U;
    result.name.data = object->name;
    result.name.capacity = DLUMA_ARRAY_COUNT(object->name);
    result.class_name.data = object->class_name;
    result.class_name.capacity =
        DLUMA_ARRAY_COUNT(object->class_name);
    status = dluma_api_runtime_invoke(
        &slot->api_runtime, 2001U,
        &request, (uint32_t)sizeof(request),
        &result, (uint32_t)sizeof(result));
    if (!dluma_lua_runtime_api_status(
            status, error, "world.find",
            "world.query", exact_name))
        return 0;
    object->owner_id = result.handle.owner_id;
    object->handle_id = result.handle.handle_id;
    object->map_generation = result.handle.generation;
    return 1;
}

static int dluma_lua_runtime_world_get_info(
    void* host_user,
    uint64_t handle_id,
    uint64_t owner_id,
    uint32_t map_generation,
    DlumaLua54Object* object,
    DlumaLua54Error* error)
{
    DlumaLuaRuntimeSlot* slot;
    if (!dluma_lua_runtime_host_slot(host_user, &slot, error)) return 0;
    if (owner_id != slot->owner_id) {
        dluma_lua_runtime_host_error(error, "object_owner_mismatch",
            "object handle belongs to another script state", "world.find",
            "world.query", "");
        return 0;
    }
    return dluma_native_gameplay_world_get_info(
        slot->owner_id, handle_id, map_generation, object, error);
}

static int dluma_lua_runtime_world_get_local_enabled(
    void* host_user,
    uint64_t handle_id,
    uint64_t owner_id,
    uint32_t map_generation,
    int* enabled,
    DlumaLua54Error* error)
{
    DlumaLuaRuntimeSlot* slot;
    DlumaObjectRequest request;
    DlumaBoolResult result;
    DlumaStatus status;
    if (!dluma_lua_runtime_host_slot(host_user, &slot, error)) return 0;
    if (!enabled) return 0;
    memset(&request, 0, sizeof(request));
    memset(&result, 0, sizeof(result));
    request.struct_size = (uint32_t)sizeof(request);
    request.struct_version = 1U;
    request.handle.owner_id = owner_id;
    request.handle.handle_id = handle_id;
    request.handle.generation = map_generation;
    request.handle.kind = 1U;
    result.struct_size = (uint32_t)sizeof(result);
    result.struct_version = 1U;
    status = dluma_api_runtime_invoke(
        &slot->api_runtime, 2004U,
        &request, (uint32_t)sizeof(request),
        &result, (uint32_t)sizeof(result));
    if (!dluma_lua_runtime_api_status(
            status, error, "world.local_enable",
            "world.query", ""))
        return 0;
    *enabled = result.value != 0;
    return 1;
}

static int dluma_lua_runtime_world_get_position(
    void* host_user,
    uint64_t handle_id,
    uint64_t owner_id,
    uint32_t map_generation,
    DlumaLua54Vec3* position,
    DlumaLua54Error* error)
{
    DlumaLuaRuntimeSlot* slot;
    DlumaObjectRequest request;
    DlumaVec3Result result;
    DlumaStatus status;
    if (!dluma_lua_runtime_host_slot(host_user, &slot, error)) return 0;
    if (!position || position->struct_size < sizeof(*position))
        return 0;
    memset(&request, 0, sizeof(request));
    memset(&result, 0, sizeof(result));
    request.struct_size = (uint32_t)sizeof(request);
    request.struct_version = 1U;
    request.handle.owner_id = owner_id;
    request.handle.handle_id = handle_id;
    request.handle.generation = map_generation;
    request.handle.kind = 1U;
    result.struct_size = (uint32_t)sizeof(result);
    result.struct_version = 1U;
    status = dluma_api_runtime_invoke(
        &slot->api_runtime, 2002U,
        &request, (uint32_t)sizeof(request),
        &result, (uint32_t)sizeof(result));
    if (!dluma_lua_runtime_api_status(
            status, error, "world.position",
            "world.query", ""))
        return 0;
    position->x = result.value.x;
    position->y = result.value.y;
    position->z = result.value.z;
    return 1;
}

static int dluma_lua_runtime_world_get_forward(
    void* host_user,
    uint64_t handle_id,
    uint64_t owner_id,
    uint32_t map_generation,
    DlumaLua54Vec3* forward,
    DlumaLua54Error* error)
{
    DlumaLuaRuntimeSlot* slot;
    DlumaObjectRequest request;
    DlumaVec3Result result;
    DlumaStatus status;
    if (!dluma_lua_runtime_host_slot(host_user, &slot, error)) return 0;
    if (!forward || forward->struct_size < sizeof(*forward))
        return 0;
    memset(&request, 0, sizeof(request));
    memset(&result, 0, sizeof(result));
    request.struct_size = (uint32_t)sizeof(request);
    request.struct_version = 1U;
    request.handle.owner_id = owner_id;
    request.handle.handle_id = handle_id;
    request.handle.generation = map_generation;
    request.handle.kind = 1U;
    result.struct_size = (uint32_t)sizeof(result);
    result.struct_version = 1U;
    status = dluma_api_runtime_invoke(
        &slot->api_runtime, 2003U,
        &request, (uint32_t)sizeof(request),
        &result, (uint32_t)sizeof(result));
    if (!dluma_lua_runtime_api_status(
            status, error, "world.position",
            "world.query", ""))
        return 0;
    forward->x = result.value.x;
    forward->y = result.value.y;
    forward->z = result.value.z;
    return 1;
}

static int dluma_lua_runtime_world_set_local_enabled(
    void* host_user,
    const DlumaLua54SetEnabledRequest* request,
    DlumaLua54SetEnabledResult* result,
    DlumaLua54Error* error)
{
    DlumaLuaRuntimeSlot* slot;
    DlumaSetEnabledManyRequest api_request;
    DlumaSetEnabledManyResult api_result;
    DlumaStringView names[DLUMA_LUA54_MAX_BULK_OBJECTS];
    DlumaStatus status;
    uint32_t index;
    if (!dluma_lua_runtime_host_slot(host_user, &slot, error)) return 0;
    if (!request || request->owner_id != slot->owner_id) {
        dluma_lua_runtime_host_error(error, "object_owner_mismatch",
            "atomic request belongs to another script state",
            "world.local_enable", "world.write", "");
        return 0;
    }
    if (!result || result->struct_size < sizeof(*result) ||
        request->requested_count > DLUMA_LUA54_MAX_BULK_OBJECTS) {
        dluma_lua_runtime_host_error(
            error, "invalid_argument",
            "world.set_enabled_many received an invalid record",
            "world.local_enable", "world.write", "");
        return 0;
    }
    memset(&api_request, 0, sizeof(api_request));
    memset(&api_result, 0, sizeof(api_result));
    api_request.struct_size =
        (uint32_t)sizeof(api_request);
    api_request.struct_version = 1U;
    api_request.names = names;
    api_request.name_count = request->requested_count;
    api_request.enabled = request->enabled != 0;
    api_request.atomic =
        request->atomic && !request->best_effort;
    for (index = 0; index < request->requested_count; ++index)
        names[index] = dluma_lua_runtime_string_view(
            request->names[index]);
    api_result.struct_size = (uint32_t)sizeof(api_result);
    api_result.struct_version = 1U;
    status = dluma_api_runtime_invoke(
        &slot->api_runtime, 2005U,
        &api_request, (uint32_t)sizeof(api_request),
        &api_result, (uint32_t)sizeof(api_result));
    result->requested_count = api_result.requested_count;
    result->changed_count = api_result.changed_count;
    result->unchanged_count = api_result.unchanged_count;
    result->partial = api_result.partial;
    result->name_count = request->requested_count;
    for (index = 0; index < request->requested_count; ++index)
        dluma_str_copy(
            result->names[index],
            DLUMA_ARRAY_COUNT(result->names[index]),
            request->names[index]);
    return dluma_lua_runtime_api_status(
        status, error, "world.local_enable",
        "world.write", "");
}

static int dluma_lua_runtime_sensor_is_inside(
    void* host_user,
    const char* exact_name,
    int* inside,
    DlumaLua54Error* error)
{
    DlumaLuaRuntimeSlot* slot;
    DlumaNamedRequest request;
    DlumaBoolResult result;
    DlumaStatus status;
    if (!dluma_lua_runtime_host_slot(host_user, &slot, error)) return 0;
    if (!inside) return 0;
    memset(&request, 0, sizeof(request));
    memset(&result, 0, sizeof(result));
    request.struct_size = (uint32_t)sizeof(request);
    request.struct_version = 1U;
    request.name = dluma_lua_runtime_string_view(exact_name);
    result.struct_size = (uint32_t)sizeof(result);
    result.struct_version = 1U;
    status = dluma_api_runtime_invoke(
        &slot->api_runtime, 4002U,
        &request, (uint32_t)sizeof(request),
        &result, (uint32_t)sizeof(result));
    if (!dluma_lua_runtime_api_status(
            status, error, "events.sensor",
            "events.sensor", exact_name))
        return 0;
    *inside = result.value != 0;
    return 1;
}

static int dluma_lua_runtime_subtitle_show(
    void* host_user,
    const DlumaLua54SubtitleRequest* request,
    DlumaLua54Error* error)
{
    DlumaLuaRuntimeSlot* slot;
    DlumaSubtitleRequest api_request;
    DlumaBoolResult result;
    DlumaStatus status;
    if (!dluma_lua_runtime_host_slot(host_user, &slot, error) ||
        !request)
        return 0;
    memset(&api_request, 0, sizeof(api_request));
    memset(&result, 0, sizeof(result));
    api_request.struct_size = (uint32_t)sizeof(api_request);
    api_request.struct_version = 1U;
    api_request.text =
        dluma_lua_runtime_string_view(request->text);
    api_request.duration_seconds = request->duration_seconds;
    api_request.force = request->force != 0;
    result.struct_size = (uint32_t)sizeof(result);
    result.struct_version = 1U;
    status = dluma_api_runtime_invoke(
        &slot->api_runtime, 5001U,
        &api_request, (uint32_t)sizeof(api_request),
        &result, (uint32_t)sizeof(result));
    return dluma_lua_runtime_api_status(
        status, error, "ui.subtitles",
        "ui.overlay", "");
}

static int dluma_lua_runtime_subtitle_update(
    void* host_user,
    const DlumaLua54SubtitleUpdateRequest* request,
    DlumaLua54Error* error)
{
    DlumaLuaRuntimeSlot* slot;
    DlumaSubtitleUpdateRequest api_request;
    DlumaBoolResult result;
    DlumaStatus status;
    if (!dluma_lua_runtime_host_slot(host_user, &slot, error) ||
        !request)
        return 0;
    memset(&api_request, 0, sizeof(api_request));
    memset(&result, 0, sizeof(result));
    api_request.struct_size = (uint32_t)sizeof(api_request);
    api_request.struct_version = 1U;
    api_request.text =
        dluma_lua_runtime_string_view(request->text);
    api_request.fade = request->fade != 0;
    result.struct_size = (uint32_t)sizeof(result);
    result.struct_version = 1U;
    status = dluma_api_runtime_invoke(
        &slot->api_runtime, 5002U,
        &api_request, (uint32_t)sizeof(api_request),
        &result, (uint32_t)sizeof(result));
    return dluma_lua_runtime_api_status(
        status, error, "ui.subtitles",
        "ui.overlay", "");
}

static int dluma_lua_runtime_radio_dialog(
    void* host_user,
    const DlumaLua54RadioDialogRequest* request,
    DlumaLua54Error* error)
{
    DlumaLuaRuntimeSlot* slot;
    DlumaRadioDialogRequest api_request;
    DlumaBoolResult result;
    DlumaStatus status;
    if (!dluma_lua_runtime_host_slot(host_user, &slot, error) ||
        !request)
        return 0;
    memset(&api_request, 0, sizeof(api_request));
    memset(&result, 0, sizeof(result));
    api_request.struct_size = (uint32_t)sizeof(api_request);
    api_request.struct_version = 1U;
    api_request.speaker =
        dluma_lua_runtime_string_view(request->speaker);
    api_request.group =
        dluma_lua_runtime_string_view(request->group);
    result.struct_size = (uint32_t)sizeof(result);
    result.struct_version = 1U;
    status = dluma_api_runtime_invoke(
        &slot->api_runtime, 6001U,
        &api_request, (uint32_t)sizeof(api_request),
        &result, (uint32_t)sizeof(result));
    return dluma_lua_runtime_api_status(
        status, error, "audio.radio_dialog",
        "audio.play", "");
}

static int dluma_lua_runtime_action_run_chain(
    void* host_user,
    const char* exact_name,
    DlumaLua54Error* error)
{
    DlumaLuaRuntimeSlot* slot;
    DlumaNamedRequest request;
    DlumaBoolResult result;
    DlumaStatus status;
    if (!dluma_lua_runtime_host_slot(host_user, &slot, error)) return 0;
    memset(&request, 0, sizeof(request));
    memset(&result, 0, sizeof(result));
    request.struct_size = (uint32_t)sizeof(request);
    request.struct_version = 1U;
    request.name = dluma_lua_runtime_string_view(exact_name);
    result.struct_size = (uint32_t)sizeof(result);
    result.struct_version = 1U;
    status = dluma_api_runtime_invoke(
        &slot->api_runtime, 8001U,
        &request, (uint32_t)sizeof(request),
        &result, (uint32_t)sizeof(result));
    return dluma_lua_runtime_api_status(
        status, error, "actions.chain",
        "world.write", exact_name);
}

static int dluma_lua_runtime_quest_hud_snapshot(
    void* host_user,
    DlumaLua54QuestHudSnapshot* snapshot,
    DlumaLua54Error* error)
{
    DlumaLuaRuntimeSlot* slot;
    if (!dluma_lua_runtime_host_slot(host_user, &slot, error)) return 0;
    if (!(slot->approved_permissions & DLUMA_LUA54_PERMISSION_GAME_READ)) {
        dluma_lua_runtime_host_error(error, "permission_denied",
            "quest HUD snapshots require game.read",
            "ui.quest_hud", "game.read", "");
        return 0;
    }
    return dluma_quest_hud_snapshot(snapshot, error);
}

static int dluma_lua_runtime_quest_hud_override_set(
    void* host_user,
    const DlumaLua54QuestHudOverrideRequest* request,
    DlumaLua54QuestHudOverrideResult* result,
    DlumaLua54Error* error)
{
    DlumaLuaRuntimeSlot* slot;
    if (!dluma_lua_runtime_host_slot(host_user, &slot, error)) return 0;
    if (!(slot->approved_permissions & DLUMA_LUA54_PERMISSION_UI_OVERLAY)) {
        dluma_lua_runtime_host_error(error, "permission_denied",
            "quest HUD overrides require ui.overlay",
            "ui.quest_hud", "ui.overlay",
            request ? request->target_id : "");
        return 0;
    }
    if (slot->metadata.scope == DLUMA_LUA_SCOPE_GLOBAL) {
        dluma_lua_runtime_host_error(error, "quest_hud_scope_not_supported",
            "global scripts may read but may not override the local quest HUD",
            "ui.quest_hud", "ui.overlay",
            request ? request->target_id : "");
        return 0;
    }
    if (!request || request->owner_id != slot->owner_id ||
        request->map_generation != g_dluma_lua_runtime.map_generation) {
        dluma_lua_runtime_host_error(error, "quest_hud_owner_mismatch",
            "quest HUD override request belongs to another owner or map",
            "ui.quest_hud", "ui.overlay",
            request ? request->target_id : "");
        return 0;
    }
    return dluma_quest_hud_override_set(request, result, error);
}

static int dluma_lua_runtime_quest_hud_override_clear(
    void* host_user,
    uint64_t owner_id,
    uint32_t map_generation,
    uint64_t override_id,
    DlumaLua54QuestHudOverrideResult* result,
    DlumaLua54Error* error)
{
    DlumaLuaRuntimeSlot* slot;
    if (!dluma_lua_runtime_host_slot(host_user, &slot, error)) return 0;
    if (slot->metadata.scope == DLUMA_LUA_SCOPE_GLOBAL ||
        !(slot->approved_permissions & DLUMA_LUA54_PERMISSION_UI_OVERLAY)) {
        dluma_lua_runtime_host_error(error, "permission_denied",
            "this script may not clear quest HUD overrides",
            "ui.quest_hud", "ui.overlay", "");
        return 0;
    }
    if (owner_id != slot->owner_id) {
        dluma_lua_runtime_host_error(error, "quest_hud_owner_mismatch",
            "quest HUD override belongs to another script owner",
            "ui.quest_hud", "ui.overlay", "");
        return 0;
    }
    return dluma_quest_hud_override_clear(
        owner_id, map_generation, override_id, result, error);
}

static void dluma_lua_runtime_quest_hud_clear_owner(
    void* host_user, uint64_t owner_id)
{
    DlumaLuaRuntimeSlot* slot = (DlumaLuaRuntimeSlot*)host_user;
    if (!slot || !slot->occupied || slot->owner_id != owner_id)
        return;
    dluma_quest_hud_clear_owner(owner_id);
}

static int dluma_lua_runtime_utf8_valid(const char* text, unsigned int length)
{
    unsigned int i = 0;
    if (!text) return 0;
    while (i < length) {
        unsigned char lead = (unsigned char)text[i++];
        if (lead <= 0x7Fu) continue;
        if (lead >= 0xC2u && lead <= 0xDFu) {
            if (i >= length || ((unsigned char)text[i] & 0xC0u) != 0x80u)
                return 0;
            ++i;
            continue;
        }
        if (lead == 0xE0u) {
            if (i + 1u >= length || (unsigned char)text[i] < 0xA0u ||
                (unsigned char)text[i] > 0xBFu ||
                ((unsigned char)text[i + 1u] & 0xC0u) != 0x80u)
                return 0;
            i += 2u;
            continue;
        }
        if ((lead >= 0xE1u && lead <= 0xECu) ||
            (lead >= 0xEEu && lead <= 0xEFu)) {
            if (i + 1u >= length ||
                ((unsigned char)text[i] & 0xC0u) != 0x80u ||
                ((unsigned char)text[i + 1u] & 0xC0u) != 0x80u)
                return 0;
            i += 2u;
            continue;
        }
        if (lead == 0xEDu) {
            if (i + 1u >= length || (unsigned char)text[i] < 0x80u ||
                (unsigned char)text[i] > 0x9Fu ||
                ((unsigned char)text[i + 1u] & 0xC0u) != 0x80u)
                return 0;
            i += 2u;
            continue;
        }
        if (lead == 0xF0u) {
            if (i + 2u >= length || (unsigned char)text[i] < 0x90u ||
                (unsigned char)text[i] > 0xBFu ||
                ((unsigned char)text[i + 1u] & 0xC0u) != 0x80u ||
                ((unsigned char)text[i + 2u] & 0xC0u) != 0x80u)
                return 0;
            i += 3u;
            continue;
        }
        if (lead >= 0xF1u && lead <= 0xF3u) {
            if (i + 2u >= length ||
                ((unsigned char)text[i] & 0xC0u) != 0x80u ||
                ((unsigned char)text[i + 1u] & 0xC0u) != 0x80u ||
                ((unsigned char)text[i + 2u] & 0xC0u) != 0x80u)
                return 0;
            i += 3u;
            continue;
        }
        if (lead == 0xF4u) {
            if (i + 2u >= length || (unsigned char)text[i] < 0x80u ||
                (unsigned char)text[i] > 0x8Fu ||
                ((unsigned char)text[i + 1u] & 0xC0u) != 0x80u ||
                ((unsigned char)text[i + 2u] & 0xC0u) != 0x80u)
                return 0;
            i += 3u;
            continue;
        }
        return 0;
    }
    return 1;
}

static int dluma_lua_runtime_quest_text_valid(
    const char* text,
    unsigned int capacity,
    int required)
{
    unsigned int length;
    if (!text || !capacity) return 0;
    for (length = 0; length < capacity; ++length) {
        if (!text[length]) {
            if (required && !length) return 0;
            return dluma_lua_runtime_utf8_valid(text, length);
        }
    }
    return 0;
}

static int dluma_lua_runtime_quest_id_valid(const char* text, unsigned int capacity)
{
    unsigned int i;
    if (!dluma_lua_runtime_quest_text_valid(text, capacity, 1)) return 0;
    for (i = 0; text[i]; ++i) {
        char c = text[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '_' || c == '-' ||
              c == '.')) return 0;
    }
    return 1;
}

static int dluma_lua_runtime_quest_state_valid(uint32_t state)
{
    return state == DLUMA_LUA54_QUEST_STATE_DECLARED ||
           state == DLUMA_LUA54_QUEST_STATE_ACTIVE ||
           state == DLUMA_LUA54_QUEST_STATE_COMPLETED ||
           state == DLUMA_LUA54_QUEST_STATE_FAILED;
}

static int dluma_lua_runtime_quest_native_state_valid(uint32_t state)
{
    return state == DLUMA_LUA54_QUEST_NATIVE_NONE ||
           state == DLUMA_LUA54_QUEST_NATIVE_UNAVAILABLE ||
           state == DLUMA_LUA54_QUEST_NATIVE_MIRRORED;
}

static void dluma_lua_runtime_clear_quest_snapshots_for_owner(uint64_t owner_id)
{
    uint32_t source;
    uint32_t destination = 0;
    int changed = 0;
    if (!owner_id) return;
    dluma_lua_runtime_lock(&g_dluma_lua_runtime.quest_lock);
    for (source = 0; source < g_dluma_lua_runtime.quest_snapshot_count;
         ++source) {
        DlumaLuaQuestUiSnapshot* snapshot =
            &g_dluma_lua_runtime.quests[source];
        if (snapshot->owner_id == owner_id) {
            changed = 1;
            continue;
        }
        if (destination != source)
            g_dluma_lua_runtime.quests[destination] = *snapshot;
        ++destination;
    }
    if (changed) {
        g_dluma_lua_runtime.quest_snapshot_count = destination;
        ++g_dluma_lua_runtime.quest_snapshot_revision;
        if (!g_dluma_lua_runtime.quest_snapshot_revision)
            ++g_dluma_lua_runtime.quest_snapshot_revision;
        for (; destination < DLUMA_LUA_RUNTIME_MAX_QUEST_SNAPSHOTS;
             ++destination)
            memset(&g_dluma_lua_runtime.quests[destination], 0,
                   sizeof(g_dluma_lua_runtime.quests[destination]));
    }
    dluma_lua_runtime_unlock(&g_dluma_lua_runtime.quest_lock);
}

static void dluma_lua_runtime_clear_quest_snapshots(void)
{
    dluma_lua_runtime_lock(&g_dluma_lua_runtime.quest_lock);
    if (g_dluma_lua_runtime.quest_snapshot_count) {
        memset(g_dluma_lua_runtime.quests, 0,
               sizeof(g_dluma_lua_runtime.quests));
        g_dluma_lua_runtime.quest_snapshot_count = 0;
        ++g_dluma_lua_runtime.quest_snapshot_revision;
        if (!g_dluma_lua_runtime.quest_snapshot_revision)
            ++g_dluma_lua_runtime.quest_snapshot_revision;
    }
    dluma_lua_runtime_unlock(&g_dluma_lua_runtime.quest_lock);
}

/* The archive is presentation-only.  Unlike the live cache, it may survive a
 * map unload so the frontend Quest Log has something useful to show after the
 * project/map Lua states have been torn down.  No gameplay or Lua path reads
 * it, and it is cleared before the next map becomes active. */
static void dluma_lua_runtime_clear_archived_quest_snapshots(void)
{
    dluma_lua_runtime_lock(&g_dluma_lua_runtime.quest_lock);
    if (g_dluma_lua_runtime.archived_quest_snapshot_count) {
        memset(g_dluma_lua_runtime.archived_quests, 0,
               sizeof(g_dluma_lua_runtime.archived_quests));
        g_dluma_lua_runtime.archived_quest_snapshot_count = 0;
        ++g_dluma_lua_runtime.archived_quest_snapshot_revision;
        if (!g_dluma_lua_runtime.archived_quest_snapshot_revision)
            ++g_dluma_lua_runtime.archived_quest_snapshot_revision;
    }
    dluma_lua_runtime_unlock(&g_dluma_lua_runtime.quest_lock);
}

static void dluma_lua_runtime_archive_quest_snapshots_for_frontend(void)
{
    uint32_t source;
    uint32_t destination = 0;
    uint32_t generation;
    dluma_lua_runtime_lock(&g_dluma_lua_runtime.quest_lock);
    generation = g_dluma_lua_runtime.map_generation;
    memset(g_dluma_lua_runtime.archived_quests, 0,
           sizeof(g_dluma_lua_runtime.archived_quests));
    for (source = 0; source < g_dluma_lua_runtime.quest_snapshot_count;
         ++source) {
        const DlumaLuaQuestUiSnapshot* snapshot =
            &g_dluma_lua_runtime.quests[source];
        if (snapshot->map_generation != generation) continue;
        if (destination >= DLUMA_LUA_RUNTIME_MAX_QUEST_SNAPSHOTS) break;
        g_dluma_lua_runtime.archived_quests[destination++] = *snapshot;
    }
    g_dluma_lua_runtime.archived_quest_snapshot_count = destination;
    ++g_dluma_lua_runtime.archived_quest_snapshot_revision;
    if (!g_dluma_lua_runtime.archived_quest_snapshot_revision)
        ++g_dluma_lua_runtime.archived_quest_snapshot_revision;
    dluma_lua_runtime_unlock(&g_dluma_lua_runtime.quest_lock);
}

static int dluma_lua_runtime_quest_state_changed(
    void* host_user,
    const DlumaLua54QuestStateReport* report,
    DlumaLua54Error* error)
{
    DlumaLuaRuntimeSlot* slot;
    DlumaLuaQuestUiSnapshot* snapshot = 0;
    uint32_t i;
    if (!dluma_lua_runtime_host_slot(host_user, &slot, error)) return 0;
    if (!report || report->struct_size < sizeof(*report) ||
        report->owner_id != slot->owner_id ||
        report->active > 1u ||
        !dluma_lua_runtime_quest_state_valid(report->state) ||
        !dluma_lua_runtime_quest_native_state_valid(report->native_state) ||
        !dluma_lua_runtime_quest_id_valid(
            report->script_id, DLUMA_ARRAY_COUNT(report->script_id)) ||
        dluma_stricmp(report->script_id, slot->metadata.id) != 0 ||
        !dluma_lua_runtime_quest_id_valid(
            report->quest_id, DLUMA_ARRAY_COUNT(report->quest_id)) ||
        !dluma_lua_runtime_quest_text_valid(
            report->title, DLUMA_ARRAY_COUNT(report->title), 1) ||
        !dluma_lua_runtime_quest_text_valid(
            report->version, DLUMA_ARRAY_COUNT(report->version), 1) ||
        !dluma_lua_runtime_quest_text_valid(
            report->phase_id, DLUMA_ARRAY_COUNT(report->phase_id),
            report->state != DLUMA_LUA54_QUEST_STATE_DECLARED) ||
        !dluma_lua_runtime_quest_text_valid(
            report->phase_title, DLUMA_ARRAY_COUNT(report->phase_title), 0) ||
        !dluma_lua_runtime_quest_text_valid(
            report->objective_summary,
            DLUMA_ARRAY_COUNT(report->objective_summary), 0)) {
        dluma_lua_runtime_host_error(error, "invalid_quest_state_report",
            "quest state report is not a bounded snapshot for this script",
            "quest.local", "", "");
        return 0;
    }
    if (report->map_generation != g_dluma_lua_runtime.map_generation) {
        dluma_lua_runtime_host_error(error, "stale_map_generation",
            "quest state report belongs to a previous map generation",
            "quest.local", "", report->quest_id);
        return 0;
    }
    dluma_lua_runtime_lock(&g_dluma_lua_runtime.quest_lock);
    for (i = 0; i < g_dluma_lua_runtime.quest_snapshot_count; ++i) {
        DlumaLuaQuestUiSnapshot* candidate = &g_dluma_lua_runtime.quests[i];
        if (candidate->owner_id == report->owner_id &&
            dluma_stricmp(candidate->id, report->quest_id) == 0) {
            snapshot = candidate;
            break;
        }
    }
    if (!snapshot) {
        if (g_dluma_lua_runtime.quest_snapshot_count >=
            DLUMA_LUA_RUNTIME_MAX_QUEST_SNAPSHOTS) {
            dluma_lua_runtime_unlock(&g_dluma_lua_runtime.quest_lock);
            dluma_lua_runtime_host_error(error, "quest_snapshot_limit",
                "the loader quest presentation cache is full",
                "quest.local", "", report->quest_id);
            return 0;
        }
        snapshot = &g_dluma_lua_runtime.quests[
            g_dluma_lua_runtime.quest_snapshot_count++];
        memset(snapshot, 0, sizeof(*snapshot));
    } else if (report->revision < snapshot->revision) {
        dluma_lua_runtime_unlock(&g_dluma_lua_runtime.quest_lock);
        dluma_lua_runtime_host_error(error, "stale_quest_state_report",
            "quest state report revision moved backwards",
            "quest.local", "", report->quest_id);
        return 0;
    }
    snapshot->map_generation = report->map_generation;
    snapshot->active = report->active;
    snapshot->state = report->state;
    snapshot->native_state = report->native_state;
    snapshot->owner_id = report->owner_id;
    snapshot->revision = report->revision;
    dluma_str_copy(snapshot->script_id, DLUMA_ARRAY_COUNT(snapshot->script_id),
                 report->script_id);
    dluma_str_copy(snapshot->id, DLUMA_ARRAY_COUNT(snapshot->id), report->quest_id);
    dluma_str_copy(snapshot->title, DLUMA_ARRAY_COUNT(snapshot->title), report->title);
    dluma_str_copy(snapshot->version, DLUMA_ARRAY_COUNT(snapshot->version),
                 report->version);
    dluma_str_copy(snapshot->phase_id, DLUMA_ARRAY_COUNT(snapshot->phase_id),
                 report->phase_id);
    dluma_str_copy(snapshot->phase_title,
                 DLUMA_ARRAY_COUNT(snapshot->phase_title), report->phase_title);
    dluma_str_copy(snapshot->objective_summary,
                 DLUMA_ARRAY_COUNT(snapshot->objective_summary),
                 report->objective_summary);
    ++g_dluma_lua_runtime.quest_snapshot_revision;
    if (!g_dluma_lua_runtime.quest_snapshot_revision)
        ++g_dluma_lua_runtime.quest_snapshot_revision;
    dluma_lua_runtime_unlock(&g_dluma_lua_runtime.quest_lock);
    return 1;
}

static int dluma_lua_runtime_quest_mutate(
    void* host_user,
    const DlumaLua54QuestRequest* request,
    DlumaLua54QuestResult* result,
    DlumaLua54Error* error)
{
    DlumaLuaRuntimeSlot* slot;
    DlumaQuestTransitionRequest api_request;
    DlumaBoolResult api_result;
    DlumaStatus status;
    if (!dluma_lua_runtime_host_slot(host_user, &slot, error)) return 0;
    if (!request || request->owner_id != slot->owner_id) {
        dluma_lua_runtime_host_error(error, "quest_owner_mismatch",
            "quest request belongs to another script state",
            "quests.native", "quest.write", "");
        return 0;
    }
    if (!result || result->struct_size < sizeof(*result) ||
        request->operation != DLUMA_LUA54_QUEST_JUMP_TO_PHASE) {
        dluma_lua_runtime_host_error(
            error, "removed_operation",
            "DLUMA API 1.0 supports quests.advance_to_phase only",
            "quests.native", "quest.write",
            request ? request->native_quest_id : "");
        return 0;
    }
    memset(&api_request, 0, sizeof(api_request));
    memset(&api_result, 0, sizeof(api_result));
    api_request.struct_size =
        (uint32_t)sizeof(api_request);
    api_request.struct_version = 1U;
    api_request.quest_id =
        dluma_lua_runtime_string_view(
            request->native_quest_id);
    api_request.phase_id =
        dluma_lua_runtime_string_view(
            request->native_phase_id);
    api_result.struct_size =
        (uint32_t)sizeof(api_result);
    api_result.struct_version = 1U;
    status = dluma_api_runtime_invoke(
        &slot->api_runtime, 7001U,
        &api_request, (uint32_t)sizeof(api_request),
        &api_result, (uint32_t)sizeof(api_result));
    if (!dluma_lua_runtime_api_status(
            status, error, "quests.native",
            "quest.write", request->native_quest_id))
        return 0;
    result->operation = request->operation;
    result->changed = api_result.value != 0;
    result->native_state =
        DLUMA_LUA54_QUEST_NATIVE_MIRRORED;
    dluma_str_copy(
        result->native_quest_id,
        DLUMA_ARRAY_COUNT(result->native_quest_id),
        request->native_quest_id);
    dluma_str_copy(
        result->native_phase_id,
        DLUMA_ARRAY_COUNT(result->native_phase_id),
        request->native_phase_id);
    return 1;
}

static DlumaLuaRuntimeSlot* dluma_lua_runtime_find_slot(uint32_t scope, const char* id)
{
    unsigned int i;
    for (i = 0; i < DLUMA_LUA_RUNTIME_MAX_STATES; ++i) {
        DlumaLuaRuntimeSlot* slot = &g_dluma_lua_runtime.slots[i];
        if (slot->occupied && slot->metadata.scope == scope &&
            dluma_stricmp(slot->metadata.id, id) == 0)
            return slot;
    }
    return 0;
}

static DlumaLuaRuntimeSlot* dluma_lua_runtime_free_slot(void)
{
    unsigned int i;
    for (i = 0; i < DLUMA_LUA_RUNTIME_MAX_STATES; ++i)
        if (!g_dluma_lua_runtime.slots[i].occupied)
            return &g_dluma_lua_runtime.slots[i];
    return 0;
}

static int dluma_lua_runtime_dispatch_helper_event(
    DlumaLuaRuntimeSlot* slot,
    DlumaLua54Event* event)
{
    int callback_count;
    int exit_notification=slot && event && slot==g_dluma_exit_notification_slot &&
        event->kind==g_dluma_exit_notification_kind &&
        slot->api_owner.game_thread_id==GetCurrentThreadId();
    if (!slot || !event || !slot->occupied ||
        slot->teardown_pending ||
        ((g_dluma_map_exit_active || (g_dluma_lua_project_exit_gate &&
          slot->metadata.scope != DLUMA_LUA_SCOPE_GLOBAL)) && !exit_notification) ||
        !slot->helper_script ||
        !g_dluma_lua_runtime.api.dispatch)
        return 0;
    event->struct_size = sizeof(*event);
    event->map_generation =
        g_dluma_lua_runtime.map_generation;
    event->project =
        g_dluma_lua_runtime.current_project_name;
    event->map = g_dluma_lua_runtime.current_map;
    callback_count = g_dluma_lua_runtime.api.dispatch(
        slot->helper_script, event);
    memset(&slot->status, 0, sizeof(slot->status));
    slot->status.struct_size = sizeof(slot->status);
    g_dluma_lua_runtime.api.get_status(slot->helper_script, &slot->status);
    if (slot->status.disabled && !slot->disabled_logged) {
        slot->disabled_logged = 1;
        dluma_log_pair("ERROR", "lua_state_disabled_for_session", slot->metadata.id);
        if (slot->status.last_error[0])
            dluma_log_pair("ERROR", "lua_state_last_error", slot->status.last_error);
    }
    return callback_count > 0 ? callback_count : 0;
}

static int dluma_lua_runtime_dispatch_slot(
    DlumaLuaRuntimeSlot* slot,
    uint32_t kind,
    const char* name,
    double number,
    int state)
{
    DlumaLua54Event event;
    memset(&event, 0, sizeof(event));
    event.kind = kind;
    event.state = state;
    event.number = number;
    event.name = name;
    return dluma_lua_runtime_dispatch_helper_event(
        slot, &event);
}

static DlumaLua54ByteView dluma_lua_runtime_phase_view(
    const char* value,
    size_t size)
{
    DlumaLua54ByteView view;
    view.data = value;
    view.size = size;
    return view;
}

static int dluma_lua_runtime_quest_phase_event_sink(
    void* user,
    uint32_t event_kind,
    uint64_t callback_id,
    uint64_t registration_token,
    const DlumaQuestPhaseEvent* event_value)
{
    DlumaLuaRuntimeSlot* slot = (DlumaLuaRuntimeSlot*)user;
    DlumaModPackageSnapshot current;
    DlumaLua54Event event;
    unsigned int required_access;
    uint32_t required_permission;
    if (!slot || !event_value || !callback_id ||
        callback_id > UINT32_MAX || !registration_token ||
        (event_kind != DLUMA_MOD_EVENT_QUEST_PHASE_PROVIDER &&
         event_kind != DLUMA_MOD_EVENT_QUEST_PHASE_OBSERVER))
        return 0;
    required_access = event_kind == DLUMA_MOD_EVENT_QUEST_PHASE_PROVIDER
        ? DLUMA_ACCESS_QUEST_PHASE_HANDLE
        : DLUMA_ACCESS_QUEST_PHASE_OBSERVE;
    required_permission = event_kind == DLUMA_MOD_EVENT_QUEST_PHASE_PROVIDER
        ? DLUMA_LUA54_PERMISSION_QUEST_PHASE_HANDLE
        : DLUMA_LUA54_PERMISSION_QUEST_PHASE_OBSERVE;
    memset(&current, 0, sizeof(current));
    current.struct_size = (unsigned int)sizeof(current);
    if (!slot->occupied || slot->teardown_pending || !slot->helper_script ||
        slot->api_owner.state != DLUMA_MOD_OWNER_ACTIVE ||
        slot->api_owner.map_generation != event_value->map_generation ||
        !(slot->approved_permissions & required_permission) ||
        dluma_mod_package_revalidate(&slot->api_package, &current) !=
            DLUMA_STATUS_OK ||
        !dluma_mod_manifest_has_access(&current.manifest, required_access) ||
        (event_kind == DLUMA_MOD_EVENT_QUEST_PHASE_PROVIDER &&
         !dluma_mod_manifest_declares_quest_phase_handler(
             &current.manifest, event_value->handler_id)))
        return 0;
    memset(&event, 0, sizeof(event));
    event.kind = event_kind == DLUMA_MOD_EVENT_QUEST_PHASE_PROVIDER
        ? DLUMA_LUA54_EVENT_QUEST_PHASE_PROVIDER
        : DLUMA_LUA54_EVENT_QUEST_PHASE_OBSERVER;
    event.name = event_kind == DLUMA_MOD_EVENT_QUEST_PHASE_PROVIDER
        ? event_value->handler_id : event_value->contract_id;
    event.quest_phase.struct_size =
        (uint32_t)sizeof(event.quest_phase);
    event.quest_phase.map_generation = event_value->map_generation;
    event.quest_phase.runtime_kind = event_value->runtime_kind;
    event.quest_phase.completion = event_value->completion;
    event.quest_phase.sequence = event_value->sequence;
    event.quest_phase.wait_seconds = event_value->wait_seconds;
    event.quest_phase.local_peer = event_value->local_peer;
    event.quest_phase.quest_id = dluma_lua_runtime_phase_view(
        event_value->quest_id, strlen(event_value->quest_id));
    event.quest_phase.phase_id = dluma_lua_runtime_phase_view(
        event_value->phase_id, strlen(event_value->phase_id));
    event.quest_phase.contract_id = dluma_lua_runtime_phase_view(
        event_value->contract_id, strlen(event_value->contract_id));
    event.quest_phase.provider = dluma_lua_runtime_phase_view(
        event_value->provider_mod, strlen(event_value->provider_mod));
    event.quest_phase.handler = dluma_lua_runtime_phase_view(
        event_value->handler_id, strlen(event_value->handler_id));
    event.quest_phase.binding_hash = dluma_lua_runtime_phase_view(
        event_value->binding_hash, strlen(event_value->binding_hash));
    event.quest_phase.arguments_json = dluma_lua_runtime_phase_view(
        event_value->arguments_json, event_value->arguments_size);
    return dluma_lua_runtime_dispatch_helper_event(slot, &event) > 0;
}

static void dluma_lua_runtime_destroy_slot(
    DlumaLuaRuntimeSlot* slot,
    int send_map_unload,
    int send_unload)
{
    if (!slot || !slot->occupied) return;
    if (!slot->teardown_pending && send_map_unload &&
        !slot->map_unload_sent && g_dluma_lua_runtime.current_map[0]) {
        slot->map_unload_sent = 1;
        dluma_lua_runtime_dispatch_slot(slot, DLUMA_LUA54_EVENT_MAP_UNLOAD,
                                      g_dluma_lua_runtime.current_map, 0.0, 0);
    }
    if (!slot->teardown_pending && send_unload && !slot->unload_sent) {
        slot->unload_sent = 1;
        dluma_lua_runtime_dispatch_slot(slot, DLUMA_LUA54_EVENT_UNLOAD, "on_unload", 0.0, 0);
    }
    slot->teardown_pending = 1;
    dluma_mod_owner_begin_revoke(&slot->api_owner);
    if (slot->helper_script && g_dluma_lua_runtime.api.destroy_script) {
        if (!g_dluma_lua_runtime.api.destroy_script(slot->helper_script)) {
            dluma_log_pair("WARN", "lua_state_destroy_deferred_active_callback", slot->metadata.id);
            return;
        }
        slot->helper_script = 0;
    }
    dluma_game_host_context_revoke(
        &slot->api_host);
    dluma_mod_owner_finish_revoke(&slot->api_owner);
    dluma_quest_hud_clear_owner(slot->owner_id);
    if (slot->metadata.scope == DLUMA_LUA_SCOPE_GLOBAL)
        dluma_lua_update_global_runtime_status(
            slot->metadata.registry_key,
            slot->metadata.metadata_fingerprint,
            0, "");
    dluma_log_pair("INFO", "lua_state_destroyed", slot->metadata.id);
    dluma_lua_runtime_clear_quest_snapshots_for_owner(slot->owner_id);
    memset(slot, 0, sizeof(*slot));
}

unsigned int dluma_lua_runtime_current_map_generation(void)
{
    return g_dluma_lua_runtime.map_generation;
}

int dluma_lua_runtime_revoke_local_scopes_for_exit(
    unsigned int expected_generation, DlumaLuaProjectExitDrain* out)
{
    return dluma_lua_runtime_revoke_local_scopes_for_exit_ex(expected_generation,0,out);
}

int dluma_lua_runtime_revoke_local_scopes_for_exit_ex(
    unsigned int expected_generation, const DlumaProjectExitDrainAuthority* authority,
    DlumaLuaProjectExitDrain* out)
{
    DlumaLuaProjectExitDrain result;
    uint64_t owners[DLUMA_LUA_RUNTIME_MAX_STATES];
    unsigned int owner_count = 0;
    unsigned int i;
    unsigned int current_thread = GetCurrentThreadId();
    memset(&result, 0, sizeof(result));
    result.expected_generation = expected_generation;
    result.observed_generation = g_dluma_lua_runtime.map_generation;
    if (!out) return 0;
    if (!dluma_project_exit_validate_authority(expected_generation,authority)) {
        dluma_str_copy(result.blocker, DLUMA_ARRAY_COUNT(result.blocker), "script_owner_not_authoritative");
        *out = result;
        return 0;
    }
    if (expected_generation != result.observed_generation) {
        dluma_str_copy(result.blocker, DLUMA_ARRAY_COUNT(result.blocker), "script_generation_mismatch");
        *out = result;
        return 0;
    }
    if (dluma_atomic_compare_exchange(&g_dluma_lua_runtime.pump_active, 1, 0) != 0) {
        dluma_str_copy(result.blocker, DLUMA_ARRAY_COUNT(result.blocker), "script_pump_active");
        *out = result;
        return 0;
    }
    for (i = 0; i < DLUMA_LUA_RUNTIME_MAX_STATES; ++i) {
        DlumaLuaRuntimeSlot* slot = &g_dluma_lua_runtime.slots[i];
        if (!slot->occupied || slot->metadata.scope == DLUMA_LUA_SCOPE_GLOBAL) continue;
        ++result.local_slots;
        if (slot->api_owner.map_generation != expected_generation) {
            dluma_str_copy(result.blocker, DLUMA_ARRAY_COUNT(result.blocker), "local_script_owner_generation_mismatch");
            goto done;
        }
        owners[owner_count++] = slot->owner_id;
    }
    result.owner_thread_confirmed = 1;
    result.accepted = 1;
    g_dluma_lua_project_exit_gate = 1;
    /* Check every live helper before destroying any. This path never executes
     * user handlers: provider retirement may already follow level teardown. */
    for (i = 0; i < DLUMA_LUA_RUNTIME_MAX_STATES; ++i) {
        DlumaLuaRuntimeSlot* slot = &g_dluma_lua_runtime.slots[i];
        DlumaLua54Status status;
        if (!slot->occupied || slot->metadata.scope == DLUMA_LUA_SCOPE_GLOBAL ||
            !slot->helper_script) continue;
        memset(&status, 0, sizeof(status));
        status.struct_size = sizeof(status);
        if (!g_dluma_lua_runtime.api.get_status ||
            !g_dluma_lua_runtime.api.get_status(slot->helper_script, &status)) {
            dluma_str_copy(result.blocker, DLUMA_ARRAY_COUNT(result.blocker), "local_script_status_unavailable");
            goto done;
        }
        result.active_callbacks += status.active_callbacks;
    }
    if (result.active_callbacks) {
        dluma_str_copy(result.blocker, DLUMA_ARRAY_COUNT(result.blocker), "local_script_callbacks_active");
        goto done;
    }
    /* Supported serialized workers may migrate between the last script pump
     * and natural teardown. Transfer only after exact authority and the
     * absence of active pumps/helper callbacks have both been proved. */
    for (i=0;i<DLUMA_LUA_RUNTIME_MAX_STATES;++i)
        if (g_dluma_lua_runtime.slots[i].occupied &&
            g_dluma_lua_runtime.slots[i].metadata.scope != DLUMA_LUA_SCOPE_GLOBAL)
            g_dluma_lua_runtime.slots[i].api_owner.game_thread_id=current_thread;
    /* Events retain explicit immutable owner IDs. Remove only departing Lua
     * recipients; unrelated global subscriptions keep their queued events. */
    if (dluma_atomic_compare_exchange(&g_dluma_lua_runtime.event_lock,1,0)) {
        dluma_str_copy(result.blocker,DLUMA_ARRAY_COUNT(result.blocker),"local_script_event_queue_busy");
        goto done;
    }
    for (i = 0; i < g_dluma_lua_runtime.event_count; ++i) {
        DlumaLuaRuntimeEvent* event = &g_dluma_lua_runtime.events[
            (g_dluma_lua_runtime.event_head+i) % DLUMA_LUA_RUNTIME_MAX_EVENTS];
        unsigned int read;
        unsigned int write = 0;
        for (read = 0; read < event->owner_count; ++read) {
            unsigned int owner;
            int retiring = 0;
            for (owner = 0; owner < owner_count; ++owner)
                if (event->owner_ids[read] == owners[owner]) retiring = 1;
            if (!retiring) event->owner_ids[write++] = event->owner_ids[read];
        }
        event->owner_count = write;
    }
    dluma_lua_runtime_unlock(&g_dluma_lua_runtime.event_lock);
    for (i = 0; i < DLUMA_LUA_RUNTIME_MAX_STATES; ++i) {
        DlumaLuaRuntimeSlot* slot = &g_dluma_lua_runtime.slots[i];
        if (slot->occupied && slot->metadata.scope != DLUMA_LUA_SCOPE_GLOBAL)
            dluma_lua_runtime_destroy_slot(slot, 0, 0);
    }
done:
    for (i = 0; i < DLUMA_LUA_RUNTIME_MAX_STATES; ++i)
        if (g_dluma_lua_runtime.slots[i].occupied &&
            g_dluma_lua_runtime.slots[i].metadata.scope != DLUMA_LUA_SCOPE_GLOBAL)
            ++result.remaining_slots;
    result.complete = result.accepted && !result.blocker[0] && !result.remaining_slots;
    if (result.accepted && !result.complete && !result.blocker[0])
        dluma_str_copy(result.blocker, DLUMA_ARRAY_COUNT(result.blocker), "local_script_destruction_deferred");
    dluma_atomic_exchange(&g_dluma_lua_runtime.pump_active, 0);
    *out = result;
    return result.complete != 0;
}

static int dluma_map_exit_key_equal(const DlumaProjectMapExitKey* a,const DlumaProjectMapExitKey* b)
{
    return a && b && a->map_generation==b->map_generation && a->project_epoch==b->project_epoch &&
        a->owner_thread_id==b->owner_thread_id && a->serialized_route_token==b->serialized_route_token &&
        a->outgoing_session==b->outgoing_session && a->outgoing_level==b->outgoing_level;
}

int dluma_project_runtime_snapshot_outgoing_map(DlumaProjectMapExitReceipt* out)
{
    if(!out || dluma_atomic_compare_exchange(&g_dluma_map_exit_lock,1,0))return 0;
    *out=g_dluma_map_exit_receipt;
    dluma_atomic_exchange(&g_dluma_map_exit_lock,0);return out->key.project_epoch!=0;
}

int dluma_project_runtime_begin_outgoing_map(const DlumaProjectMapExitKey* key,DlumaProjectMapExitReceipt* out)
{
    DlumaProjectExitDrainAuthority authority;
    DlumaLuaProjectExitDrain lua;
    DlumaProjectHostExitDrain managed,native;
    unsigned int i,scope;
    int lua_locked=0,ready=1,notifications_ok=1;
    int managed_frozen,native_frozen,managed_done,native_done,lua_done;
    if(!key || !out || !key->project_epoch || !key->outgoing_session ||
       key->owner_thread_id!=GetCurrentThreadId() || !key->serialized_route_token)return 0;
    memset(&authority,0,sizeof(authority));authority.map_generation=key->map_generation;
    if(!dluma_engine_current_thread_can_finalize_content_ex(&g_dluma.engine,0,0,&authority.owner) ||
       authority.owner.selected_proof!=DLUMA_CONTENT_FINALIZATION_PROOF_SERIALIZED_FRAME_ROUTE ||
       authority.owner.serialized_route_token!=key->serialized_route_token)return 0;
    if(dluma_atomic_compare_exchange(&g_dluma_map_exit_lock,1,0))return 0;
    if(g_dluma_map_exit_active){
        if(dluma_map_exit_key_equal(key,&g_dluma_map_exit_receipt.key))*out=g_dluma_map_exit_receipt;
        else {memset(out,0,sizeof(*out));out->key=*key;dluma_str_copy(out->blocker,sizeof(out->blocker),"outgoing_map_key_conflict");}
        dluma_atomic_exchange(&g_dluma_map_exit_lock,0);return out->begin_complete!=0;
    }
    memset(&g_dluma_map_exit_receipt,0,sizeof(g_dluma_map_exit_receipt));g_dluma_map_exit_receipt.key=*key;
    dluma_atomic_exchange(&g_dluma_map_exit_active,1);g_dluma_lua_project_exit_gate=1;
    managed_frozen=dluma_managed_host_freeze_map_exit(key->map_generation,&authority);
    native_frozen=dluma_native_mods_freeze_map_exit(key->map_generation,&authority);
    if(!managed_frozen || !native_frozen || key->map_generation!=g_dluma_lua_runtime.map_generation ||
       (g_dluma_lua_runtime.current_map[0] && (!key->outgoing_level ||
        (g_dluma.active_project_owner_epoch && g_dluma.active_project_owner_epoch!=key->project_epoch))) ||
       dluma_atomic_compare_exchange(&g_dluma_lua_runtime.pump_active,1,0)){
        dluma_str_copy(g_dluma_map_exit_receipt.blocker,sizeof(g_dluma_map_exit_receipt.blocker),"outgoing_map_owner_generation_or_callback_not_ready");goto done;
    }
    lua_locked=1;
    for(i=0;i<DLUMA_LUA_RUNTIME_MAX_STATES;++i){
        DlumaLuaRuntimeSlot* s=&g_dluma_lua_runtime.slots[i];
        if(!s->occupied)continue;
        if(s->teardown_pending || (s->api_owner.map_generation!=key->map_generation &&
           g_dluma_lua_runtime.current_map[0]) || (!g_dluma_lua_runtime.current_map[0] &&
           s->metadata.scope!=DLUMA_LUA_SCOPE_GLOBAL)){ready=0;break;}
        memset(&s->status,0,sizeof(s->status));s->status.struct_size=sizeof(s->status);
        if(s->helper_script && (!g_dluma_lua_runtime.api.get_status ||
           !g_dluma_lua_runtime.api.get_status(s->helper_script,&s->status) || s->status.active_callbacks)){ready=0;break;}
    }
    if(!ready){dluma_str_copy(g_dluma_map_exit_receipt.blocker,sizeof(g_dluma_map_exit_receipt.blocker),"outgoing_lua_callback_or_context_not_ready");goto done;}
    for(i=0;i<DLUMA_LUA_RUNTIME_MAX_STATES;++i){
        DlumaLuaRuntimeSlot* s=&g_dluma_lua_runtime.slots[i];
        if(!s->occupied || !s->helper_script)continue;
        if(!g_dluma_lua_runtime.api.begin_pump || !g_dluma_lua_runtime.api.begin_pump(s->helper_script)){
            dluma_str_copy(g_dluma_map_exit_receipt.blocker,sizeof(g_dluma_map_exit_receipt.blocker),"outgoing_lua_budget_epoch_unavailable");goto done;
        }
    }
    /* Lua's supported map notification runs only while the exact outgoing
     * level is still alive. A generation-zero/data-only exit sends none. */
    if(g_dluma_lua_runtime.current_map[0]){
        const uint32_t scopes[3]={DLUMA_LUA_SCOPE_GLOBAL,DLUMA_LUA_SCOPE_PROJECT,DLUMA_LUA_SCOPE_MAP};
        for(scope=0;scope<3;++scope)for(i=0;i<DLUMA_LUA_RUNTIME_MAX_STATES;++i){
            DlumaLuaRuntimeSlot* s=&g_dluma_lua_runtime.slots[i];
            if(!s->occupied || s->metadata.scope!=scopes[scope] || s->status.disabled)continue;
            s->api_owner.game_thread_id=GetCurrentThreadId();s->map_unload_sent=1;
            g_dluma_exit_notification_slot=s;g_dluma_exit_notification_kind=DLUMA_LUA54_EVENT_MAP_UNLOAD;
            g_dluma_map_exit_receipt.lua_notifications+=(unsigned int)dluma_lua_runtime_dispatch_slot(
                s,DLUMA_LUA54_EVENT_MAP_UNLOAD,g_dluma_lua_runtime.current_map,0.0,0);
            if(s->status.disabled)notifications_ok=0;
        }
        for(scope=0;scope<2;++scope)for(i=0;i<DLUMA_LUA_RUNTIME_MAX_STATES;++i){
            DlumaLuaRuntimeSlot* s=&g_dluma_lua_runtime.slots[i];
            if(!s->occupied || s->metadata.scope!=(scope ? DLUMA_LUA_SCOPE_PROJECT:DLUMA_LUA_SCOPE_MAP) || s->status.disabled)continue;
            s->unload_sent=1;g_dluma_exit_notification_slot=s;g_dluma_exit_notification_kind=DLUMA_LUA54_EVENT_UNLOAD;
            (void)dluma_lua_runtime_dispatch_slot(s,DLUMA_LUA54_EVENT_UNLOAD,"on_unload",0.0,0);
            if(s->status.disabled)notifications_ok=0;
        }
    }
    g_dluma_exit_notification_slot=0;g_dluma_exit_notification_kind=0;
    dluma_atomic_exchange(&g_dluma_lua_runtime.pump_active,0);lua_locked=0;
    lua_done=dluma_lua_runtime_revoke_local_scopes_for_exit_ex(key->map_generation,&authority,&lua);
    managed_done=dluma_managed_host_revoke_project_for_exit_ex(key->map_generation,&authority,&managed);
    native_done=dluma_native_mods_revoke_project_for_exit_ex(key->map_generation,&authority,&native);
    ready=notifications_ok && lua_done && managed_done && native_done;
    for(i=0;i<DLUMA_LUA_RUNTIME_MAX_STATES;++i){
        DlumaLuaRuntimeSlot* s=&g_dluma_lua_runtime.slots[i];
        if(!s->occupied || s->metadata.scope!=DLUMA_LUA_SCOPE_GLOBAL)continue;
        s->api_owner.game_thread_id=GetCurrentThreadId();
        if(!dluma_game_host_context_suspend_map_for_exit(&s->api_host))ready=0;
        ++g_dluma_map_exit_receipt.global_lua_owners;
    }
    if(!dluma_managed_host_suspend_global_map(key->map_generation,&authority,&g_dluma_map_exit_receipt.global_managed_owners))ready=0;
    if(!dluma_native_mods_suspend_global_map(key->map_generation,&authority,&g_dluma_map_exit_receipt.global_native_owners))ready=0;
    g_dluma_map_exit_receipt.begin_complete=ready;
    if(!ready)dluma_str_copy(g_dluma_map_exit_receipt.blocker,sizeof(g_dluma_map_exit_receipt.blocker),
        !notifications_ok ? "outgoing_map_notification_failed" :
        !lua_done ? lua.blocker : !managed_done ? managed.blocker : !native_done ? native.blocker : "global_map_resource_release_incomplete");
done:
    g_dluma_exit_notification_slot=0;g_dluma_exit_notification_kind=0;
    if(lua_locked)dluma_atomic_exchange(&g_dluma_lua_runtime.pump_active,0);
    *out=g_dluma_map_exit_receipt;dluma_atomic_exchange(&g_dluma_map_exit_lock,0);
    return out->begin_complete!=0;
}

int dluma_project_runtime_finish_outgoing_map(const DlumaProjectMapExitKey* key,
    const DlumaProjectExitDrainAuthority* current,DlumaProjectMapExitReceipt* out)
{
    unsigned int i,next,expected_generation;
    int success=0;
    if(!key || !current || !out || dluma_atomic_compare_exchange(&g_dluma_map_exit_lock,1,0))return 0;
    if(!g_dluma_map_exit_active || !dluma_map_exit_key_equal(key,&g_dluma_map_exit_receipt.key) ||
       !g_dluma_map_exit_receipt.begin_complete || g_dluma_map_exit_receipt.blocker[0])goto done;
    /* The immutable key above retains the Begin route identity. The current
     * proof is separate: C10 may run on a different serialized worker. Even a
     * repeated Finish must prove that worker and the currently bound scope. */
    expected_generation=g_dluma_map_exit_receipt.context_rebound ?
        g_dluma_map_exit_receipt.new_map_generation : key->map_generation;
    if(g_dluma_lua_runtime.map_generation!=expected_generation ||
       !dluma_project_exit_validate_authority(expected_generation,current))goto done;
    if(g_dluma_map_exit_receipt.context_rebound){success=1;goto done;}
    if(dluma_atomic_compare_exchange(&g_dluma_lua_runtime.pump_active,0,0) ||
       dluma_atomic_compare_exchange(&g_dluma_lua_runtime.pending_lock,1,0))goto done;
    next=key->map_generation+1U;if(!next)next=1U;
    if(!key->map_generation && !g_dluma_map_exit_receipt.global_lua_owners &&
       !g_dluma_map_exit_receipt.global_native_owners && !g_dluma_map_exit_receipt.global_managed_owners)next=0;
    if(!dluma_native_mods_bind_frontend_after_exit(key->map_generation,next,current) ||
       !dluma_managed_host_bind_frontend_after_exit(key->map_generation,next,current)){
        dluma_lua_runtime_unlock(&g_dluma_lua_runtime.pending_lock);goto done;
    }
    for(i=0;i<DLUMA_LUA_RUNTIME_MAX_STATES;++i){
        DlumaLuaRuntimeSlot* s=&g_dluma_lua_runtime.slots[i];
        if(!s->occupied || s->metadata.scope!=DLUMA_LUA_SCOPE_GLOBAL)continue;
        s->api_owner.game_thread_id=GetCurrentThreadId();s->api_owner.state=DLUMA_MOD_OWNER_ACTIVE;
        if(s->api_owner.map_generation!=next)dluma_mod_owner_advance_scope(&s->api_owner,next);
        s->api_owner.state=DLUMA_MOD_OWNER_REVOKING;
        dluma_game_host_context_bind_clean_frontend(&s->api_host);
    }
    g_dluma_lua_runtime.current_map[0]=g_dluma_lua_runtime.current_project_name[0]=g_dluma_lua_runtime.current_workshop_id[0]=0;
    g_dluma_lua_runtime.current_project_root[0]=0;
    g_dluma_lua_runtime.pending_map[0]=g_dluma_lua_runtime.pending_project_name[0]=g_dluma_lua_runtime.pending_workshop_id[0]=0;
    g_dluma_lua_runtime.pending_project_root[0]=0;
    g_dluma_lua_runtime.next_map[0]=g_dluma_lua_runtime.next_project_name[0]=g_dluma_lua_runtime.next_workshop_id[0]=0;
    g_dluma_lua_runtime.next_project_root[0]=0;
    g_dluma_lua_runtime.pending_local_count=g_dluma_lua_runtime.committed_local_count=0;
    g_dluma_lua_runtime.context_sequence=++g_dluma_lua_runtime.pending_context_sequence;
    g_dluma_lua_runtime.map_generation=next;
    dluma_lua_runtime_unlock(&g_dluma_lua_runtime.pending_lock);
    dluma_native_gameplay_on_map_generation_changed(next,"",0);
    if(next && dluma_quest_phase_player_started() &&
       !dluma_quest_phase_player_on_map_generation_changed(L"","",next))goto done;
    if(dluma_atomic_compare_exchange(&g_dluma_lua_runtime.event_lock,1,0))goto done;
    g_dluma_lua_runtime.event_head=g_dluma_lua_runtime.event_count=0;
    dluma_lua_runtime_unlock(&g_dluma_lua_runtime.event_lock);
    if(dluma_atomic_compare_exchange(&g_dluma_lua_runtime.quest_lock,1,0))goto done;
    g_dluma_lua_runtime.quest_snapshot_count=g_dluma_lua_runtime.archived_quest_snapshot_count=0;
    ++g_dluma_lua_runtime.quest_snapshot_revision;++g_dluma_lua_runtime.archived_quest_snapshot_revision;
    dluma_lua_runtime_unlock(&g_dluma_lua_runtime.quest_lock);
    g_dluma_map_exit_receipt.new_map_generation=next;
    g_dluma_map_exit_receipt.context_rebound=1U;success=1;
done:
    if(!success && !g_dluma_map_exit_receipt.blocker[0])
        dluma_str_copy(g_dluma_map_exit_receipt.blocker,sizeof(g_dluma_map_exit_receipt.blocker),"outgoing_map_finish_proof_or_rebind_incomplete");
    *out=g_dluma_map_exit_receipt;dluma_atomic_exchange(&g_dluma_map_exit_lock,0);return success;
}

int dluma_project_runtime_resume_frontend(const DlumaProjectMapExitKey* key)
{
    unsigned int i;
    if(!key || dluma_atomic_compare_exchange(&g_dluma_map_exit_lock,1,0))return 0;
    if(!dluma_map_exit_key_equal(key,&g_dluma_map_exit_receipt.key) ||
       !g_dluma_map_exit_receipt.context_rebound || g_dluma_map_exit_receipt.blocker[0]){
        dluma_atomic_exchange(&g_dluma_map_exit_lock,0);return 0;
    }
    if(!g_dluma_map_exit_receipt.callbacks_resumed){
        for(i=0;i<DLUMA_LUA_RUNTIME_MAX_STATES;++i){
            DlumaLuaRuntimeSlot* s=&g_dluma_lua_runtime.slots[i];
            if(s->occupied && s->metadata.scope==DLUMA_LUA_SCOPE_GLOBAL && s->api_owner.state==DLUMA_MOD_OWNER_REVOKING)
                s->api_owner.state=DLUMA_MOD_OWNER_ACTIVE;
        }
        dluma_managed_host_resume_frontend_after_exit();dluma_native_mods_resume_frontend_after_exit();
        g_dluma_lua_project_exit_gate=0;g_dluma_map_exit_receipt.callbacks_resumed=1U;
        dluma_atomic_exchange(&g_dluma_map_exit_active,0);
    }
    dluma_atomic_exchange(&g_dluma_map_exit_lock,0);return 1;
}

static uint32_t dluma_lua_runtime_capabilities_for_script(uint32_t permissions)
{
    uint32_t capabilities = DLUMA_LUA54_CAP_LUA_RUNTIME;
    uint32_t native = dluma_native_gameplay_supported_capabilities();
    /* The helper-owned PlayerVariables bridge performs its own exact PE,
     * vtable, function-prefix, and live-state checks at every public call.
     * Advertising player.read here only exposes that independently gated
     * compatibility surface; it is not evidence for vitals or transforms. */
    if (permissions & DLUMA_LUA54_PERMISSION_PLAYER_READ)
        capabilities |= DLUMA_LUA54_CAP_PLAYER_READ;
    if (permissions & DLUMA_LUA54_PERMISSION_STORAGE_MOD)
        capabilities |= DLUMA_LUA54_CAP_STORAGE_MOD;
    if ((permissions & DLUMA_LUA54_PERMISSION_WORLD_QUERY) &&
        (native & DLUMA_LUA54_CAP_WORLD_FIND))
        capabilities |= DLUMA_LUA54_CAP_WORLD_FIND;
    if ((permissions & DLUMA_LUA54_PERMISSION_WORLD_QUERY) &&
        (native & DLUMA_LUA54_CAP_WORLD_POSITION))
        capabilities |= DLUMA_LUA54_CAP_WORLD_POSITION;
    if ((permissions & (DLUMA_LUA54_PERMISSION_WORLD_QUERY |
                        DLUMA_LUA54_PERMISSION_WORLD_WRITE)) &&
        (native & DLUMA_LUA54_CAP_WORLD_LOCAL_ENABLE))
        capabilities |= DLUMA_LUA54_CAP_WORLD_LOCAL_ENABLE;
    if ((permissions & DLUMA_LUA54_PERMISSION_EVENTS_SENSOR) &&
        (native & DLUMA_LUA54_CAP_EVENTS_SENSOR))
        capabilities |= DLUMA_LUA54_CAP_EVENTS_SENSOR;
    if ((permissions & DLUMA_LUA54_PERMISSION_EVENTS_INTERACT) &&
        (native & DLUMA_LUA54_CAP_EVENTS_INTERACT))
        capabilities |= DLUMA_LUA54_CAP_EVENTS_INTERACT;
    if ((permissions & DLUMA_LUA54_PERMISSION_UI_OVERLAY) &&
        (native & DLUMA_LUA54_CAP_UI_SUBTITLES))
        capabilities |= DLUMA_LUA54_CAP_UI_SUBTITLES;
    if ((permissions & DLUMA_LUA54_PERMISSION_AUDIO_PLAY) &&
        (native & DLUMA_LUA54_CAP_AUDIO_RADIO_DIALOG))
        capabilities |= DLUMA_LUA54_CAP_AUDIO_RADIO_DIALOG;
    if ((permissions & DLUMA_LUA54_PERMISSION_WORLD_WRITE) &&
        (native & DLUMA_LUA54_CAP_ACTION_CHAIN))
        capabilities |= DLUMA_LUA54_CAP_ACTION_CHAIN;
    if ((permissions & DLUMA_LUA54_PERMISSION_QUEST_WRITE) &&
        (native & DLUMA_LUA54_CAP_QUEST_NATIVE_CONTROL))
        capabilities |= DLUMA_LUA54_CAP_QUEST_NATIVE_CONTROL;
    if ((permissions & DLUMA_LUA54_PERMISSION_EVENTS_ENEMY_KILLED) &&
        (native & DLUMA_LUA54_CAP_EVENTS_ENEMY_KILLED))
        capabilities |= DLUMA_LUA54_CAP_EVENTS_ENEMY_KILLED;
    if ((permissions & (DLUMA_LUA54_PERMISSION_GAME_READ |
                        DLUMA_LUA54_PERMISSION_UI_OVERLAY)) &&
        (native & DLUMA_LUA54_CAP_UI_QUEST_HUD))
        capabilities |= DLUMA_LUA54_CAP_UI_QUEST_HUD;
    if ((permissions & (DLUMA_LUA54_PERMISSION_QUEST_PHASE_HANDLE |
                        DLUMA_LUA54_PERMISSION_QUEST_PHASE_OBSERVE)) &&
        dluma_quest_phase_player_started())
        capabilities |= DLUMA_LUA54_CAP_QUEST_PHASE_HANDLERS;
    return capabilities;
}

static uint32_t dluma_lua_runtime_current_capabilities(void* host_user)
{
    DlumaLuaRuntimeSlot* slot = (DlumaLuaRuntimeSlot*)host_user;
    uint32_t permissions;
    uint32_t native;
    uint32_t capabilities = DLUMA_LUA54_CAP_LUA_RUNTIME;
    if (!slot || !slot->occupied) return capabilities;
    permissions = slot->approved_permissions;
    native = dluma_native_gameplay_capabilities();
    if (permissions & DLUMA_LUA54_PERMISSION_PLAYER_READ)
        capabilities |= DLUMA_LUA54_CAP_PLAYER_READ;
    if (permissions & DLUMA_LUA54_PERMISSION_STORAGE_MOD)
        capabilities |= DLUMA_LUA54_CAP_STORAGE_MOD;
    if ((permissions & DLUMA_LUA54_PERMISSION_WORLD_QUERY) &&
        (native & DLUMA_LUA54_CAP_WORLD_FIND))
        capabilities |= DLUMA_LUA54_CAP_WORLD_FIND;
    if ((permissions & DLUMA_LUA54_PERMISSION_WORLD_QUERY) &&
        (native & DLUMA_LUA54_CAP_WORLD_POSITION))
        capabilities |= DLUMA_LUA54_CAP_WORLD_POSITION;
    if ((permissions & (DLUMA_LUA54_PERMISSION_WORLD_QUERY |
                        DLUMA_LUA54_PERMISSION_WORLD_WRITE)) &&
        (native & DLUMA_LUA54_CAP_WORLD_LOCAL_ENABLE))
        capabilities |= DLUMA_LUA54_CAP_WORLD_LOCAL_ENABLE;
    if ((permissions & DLUMA_LUA54_PERMISSION_EVENTS_SENSOR) &&
        (native & DLUMA_LUA54_CAP_EVENTS_SENSOR))
        capabilities |= DLUMA_LUA54_CAP_EVENTS_SENSOR;
    if ((permissions & DLUMA_LUA54_PERMISSION_EVENTS_INTERACT) &&
        (native & DLUMA_LUA54_CAP_EVENTS_INTERACT))
        capabilities |= DLUMA_LUA54_CAP_EVENTS_INTERACT;
    if ((permissions & DLUMA_LUA54_PERMISSION_UI_OVERLAY) &&
        (native & DLUMA_LUA54_CAP_UI_SUBTITLES))
        capabilities |= DLUMA_LUA54_CAP_UI_SUBTITLES;
    if ((permissions & DLUMA_LUA54_PERMISSION_AUDIO_PLAY) &&
        (native & DLUMA_LUA54_CAP_AUDIO_RADIO_DIALOG))
        capabilities |= DLUMA_LUA54_CAP_AUDIO_RADIO_DIALOG;
    if ((permissions & DLUMA_LUA54_PERMISSION_WORLD_WRITE) &&
        (native & DLUMA_LUA54_CAP_ACTION_CHAIN))
        capabilities |= DLUMA_LUA54_CAP_ACTION_CHAIN;
    if ((permissions & DLUMA_LUA54_PERMISSION_QUEST_WRITE) &&
        (native & DLUMA_LUA54_CAP_QUEST_NATIVE_CONTROL))
        capabilities |= DLUMA_LUA54_CAP_QUEST_NATIVE_CONTROL;
    if ((permissions & DLUMA_LUA54_PERMISSION_EVENTS_ENEMY_KILLED) &&
        (native & DLUMA_LUA54_CAP_EVENTS_ENEMY_KILLED))
        capabilities |= DLUMA_LUA54_CAP_EVENTS_ENEMY_KILLED;
    if ((permissions & (DLUMA_LUA54_PERMISSION_GAME_READ |
                        DLUMA_LUA54_PERMISSION_UI_OVERLAY)) &&
        (native & DLUMA_LUA54_CAP_UI_QUEST_HUD))
        capabilities |= DLUMA_LUA54_CAP_UI_QUEST_HUD;
    if ((permissions & (DLUMA_LUA54_PERMISSION_QUEST_PHASE_HANDLE |
                        DLUMA_LUA54_PERMISSION_QUEST_PHASE_OBSERVE)) &&
        dluma_quest_phase_player_started())
        capabilities |= DLUMA_LUA54_CAP_QUEST_PHASE_HANDLERS;
    return capabilities;
}

static int dluma_lua_runtime_capability_query(
    void* host_user,
    uint32_t capability_id,
    uint32_t* configured,
    uint32_t* ready,
    char* status_text,
    uint32_t status_capacity,
    DlumaLua54Error* error)
{
    DlumaLuaRuntimeSlot* slot;
    DlumaCapabilityQueryResult result;
    DlumaStatus status;
    if (!configured || !ready || !status_text || !status_capacity ||
        !dluma_lua_runtime_host_slot(host_user, &slot, error))
        return 0;
    memset(&result, 0, sizeof(result));
    result.struct_size = (uint32_t)sizeof(result);
    result.struct_version = 1U;
    result.status.data = status_text;
    result.status.capacity = status_capacity;
    status = dluma_capability_registry_query(
        &slot->api_capabilities, capability_id, &result);
    if (!dluma_lua_runtime_api_status(
            status, error, "core.runtime", "", ""))
        return 0;
    *configured = result.configured;
    *ready = result.ready;
    return 1;
}

static int dluma_lua_runtime_api_invoke(
    void* host_user,
    uint32_t operation_id,
    const void* request,
    uint32_t request_size,
    void* result,
    uint32_t result_size,
    DlumaLua54Error* error)
{
    DlumaLuaRuntimeSlot* slot;
    DlumaStatus status;
    const char* capability = "";
    const char* permission = "";
    if (!dluma_lua_runtime_host_slot(host_user, &slot, error)) return 0;
    status = dluma_api_runtime_invoke(
        &slot->api_runtime,
        operation_id,
        request,
        request_size,
        result,
        result_size);
    (void)dluma_api_runtime_operation_context(
        operation_id, &capability, &permission);
    return dluma_lua_runtime_api_status(
        status, error, capability, permission, "");
}

static int dluma_lua_runtime_initialize_api(
    DlumaLuaRuntimeSlot* slot,
    const DlumaLuaMetadata* metadata)
{
    DlumaModOwnerDefinition definition;
    DlumaGameHostContextDefinition host_definition;
    DlumaModConfigIdentity config_identity;
    if (!slot || !metadata) return 0;
    memset(&definition, 0, sizeof(definition));
    definition.struct_size = (uint32_t)sizeof(definition);
    definition.struct_version = DLUMA_MOD_OWNER_STRUCT_VERSION;
    definition.owner_id = slot->owner_id;
    definition.language = DLUMA_MOD_LANGUAGE_LUA;
    definition.scope = metadata->scope;
    definition.map_generation =
        g_dluma_lua_runtime.map_generation;
    definition.game_thread_id = GetCurrentThreadId();
    definition.stable_id.data = metadata->id;
    definition.stable_id.size =
        (uint64_t)strlen(metadata->id);
    definition.access_ids = metadata->access_ids;
    definition.access_count = metadata->access_count;
    definition.max_handles = 256U;
    definition.max_subscriptions =
        g_dluma.config.lua_max_callbacks;
    definition.max_timers = g_dluma.config.lua_max_timers;
    definition.max_companion_artifacts = 32U;
    if (dluma_mod_owner_initialize(
            &slot->api_owner,
            &definition) != DLUMA_STATUS_OK)
        return 0;

    memset(&config_identity, 0, sizeof(config_identity));
    dluma_mod_config_identity_from_lua(
        metadata, &config_identity);
    memset(&host_definition, 0, sizeof(host_definition));
    host_definition.struct_size =
        (uint32_t)sizeof(host_definition);
    host_definition.struct_version = 1U;
    host_definition.owner = &slot->api_owner;
    host_definition.package = &slot->api_package;
    host_definition.config_identity =
        &config_identity;
    host_definition.storage_key =
        metadata->storage_key;
    slot->api_host.struct_size =
        (uint32_t)sizeof(slot->api_host);
    if (dluma_game_host_context_initialize(
            &slot->api_host,
            &host_definition) != DLUMA_STATUS_OK) {
        dluma_mod_owner_begin_revoke(
            &slot->api_owner);
        dluma_mod_owner_finish_revoke(
            &slot->api_owner);
        return 0;
    }
    dluma_game_backend_prepare_capabilities(
        &slot->api_capabilities);
    dluma_game_host_context_prepare_capabilities(
        &slot->api_host,
        &slot->api_capabilities);
    dluma_game_backend_initialize_for_host(
        &slot->api_backend,
        &slot->api_host);
    if (dluma_api_runtime_initialize(
            &slot->api_runtime,
            &slot->api_owner,
            &slot->api_capabilities,
            &slot->api_backend) != DLUMA_STATUS_OK) {
        dluma_game_host_context_revoke(
            &slot->api_host);
        dluma_mod_owner_begin_revoke(&slot->api_owner);
        dluma_mod_owner_finish_revoke(&slot->api_owner);
        return 0;
    }
    if ((dluma_mod_manifest_has_access(
             &slot->api_package.manifest,
             DLUMA_ACCESS_QUEST_PHASE_HANDLE) ||
         dluma_mod_manifest_has_access(
             &slot->api_package.manifest,
             DLUMA_ACCESS_QUEST_PHASE_OBSERVE)) &&
        !dluma_quest_phase_service_attach(
            &slot->api_owner, &slot->api_package,
            slot->api_package.manifest.load_priority,
            dluma_lua_runtime_quest_phase_event_sink, slot)) {
        dluma_game_host_context_revoke(&slot->api_host);
        dluma_mod_owner_begin_revoke(&slot->api_owner);
        dluma_mod_owner_finish_revoke(&slot->api_owner);
        return 0;
    }
    return 1;
}

static int dluma_lua_runtime_create_slot(const DlumaLuaMetadata* metadata)
{
    if (metadata && g_dluma_lua_project_exit_gate &&
        metadata->scope != DLUMA_LUA_SCOPE_GLOBAL)
        return 0;
    DlumaLuaRuntimeSlot* slot;
    DlumaLua54ScriptConfig config;
    DlumaModPackageSnapshot approved_package;
    DlumaModArtifactSnapshot source_artifact;
    const char* source = 0;
    DWORD source_size = 0;
    char file_path[DLUMA_MAX_WPATH * 2];
    uint32_t permissions;
    if (!g_dluma_lua_runtime.available || !metadata || !metadata->valid ||
        metadata->duplicate_id || !metadata->enabled ||
        !metadata->manifest_present)
        return 0;
    memset(&approved_package, 0, sizeof(approved_package));
    memset(&source_artifact, 0, sizeof(source_artifact));
    approved_package.struct_size =
        (unsigned int)sizeof(approved_package);
    source_artifact.struct_size =
        (unsigned int)sizeof(source_artifact);
    if (metadata->scope == DLUMA_LUA_SCOPE_GLOBAL &&
        (!metadata->has_enabled_override || !metadata->approval_valid)) {
        dluma_log_pair("INFO", "global_lua_waiting_for_explicit_player_enable", metadata->id);
        return 0;
    }
    slot = dluma_lua_runtime_free_slot();
    if (!slot) {
        dluma_log_pair("ERROR", "lua_state_limit_reached", metadata->id);
        return 0;
    }
    memset(slot, 0, sizeof(*slot));
    slot->metadata = *metadata;
    slot->occupied = 1;
    slot->owner_id = ++g_dluma_lua_runtime.next_owner_id;
    if (!slot->owner_id) slot->owner_id = ++g_dluma_lua_runtime.next_owner_id;
    permissions = dluma_lua_runtime_approved_permissions(metadata);
    slot->approved_permissions = permissions;
    slot->capabilities = dluma_lua_runtime_capabilities_for_script(permissions);
    slot->api_package.struct_size =
        (unsigned int)sizeof(slot->api_package);
    if (dluma_mod_package_discover(
            metadata->package_root,
            &slot->api_package) != DLUMA_STATUS_OK ||
        dluma_stricmp(
            slot->api_package.manifest.id,
            metadata->id) != 0 ||
        dluma_stricmp(
            slot->api_package.manifest_sha256,
            metadata->manifest_sha256) != 0 ||
        dluma_stricmp(
            slot->api_package.lock_sha256,
            metadata->lock_sha256) != 0 ||
        dluma_stricmp(
            slot->api_package.entry_sha256,
            metadata->content_sha256) != 0) {
        dluma_log_pair(
            "ERROR",
            "lua_approved_package_revalidation_failed",
            metadata->id);
        memset(slot, 0, sizeof(*slot));
        return 0;
    }
    approved_package = slot->api_package;
    if (dluma_mod_package_read_artifact(
            &approved_package,
            metadata->package_entry,
            DLUMA_MOD_ARTIFACT_ENTRY,
            DLUMA_MAX_SCRIPT_FILE_BYTES,
            &source_artifact) != DLUMA_STATUS_OK) {
        dluma_log_pair(
            "ERROR", "lua_approved_package_revalidation_failed",
            metadata->id);
        dluma_mod_artifact_release(&source_artifact);
        memset(slot, 0, sizeof(*slot));
        return 0;
    }
    source = (const char*)source_artifact.bytes;
    source_size = source_artifact.size;
    if (!source || source_size != metadata->source_size ||
        dluma_stricmp(
            source_artifact.sha256,
            metadata->content_sha256) != 0 ||
        source_artifact.volume_serial !=
            metadata->source_volume_serial ||
        source_artifact.file_index_high !=
            metadata->source_file_index_high ||
        source_artifact.file_index_low !=
            metadata->source_file_index_low ||
        dluma_wstricmp(
            source_artifact.final_path,
            metadata->file_path) != 0) {
        dluma_log_pair("ERROR", "lua_approved_source_snapshot_mismatch",
                     metadata->id);
        if (metadata->scope == DLUMA_LUA_SCOPE_GLOBAL)
            dluma_lua_update_global_runtime_status(
                metadata->registry_key, metadata->metadata_fingerprint,
                0, "Approved Lua source bytes changed before execution");
        dluma_mod_artifact_release(&source_artifact);
        memset(slot, 0, sizeof(*slot));
        return 0;
    }
    if (!dluma_lua_runtime_initialize_api(slot, metadata)) {
        dluma_log_pair(
            "ERROR", "lua_api_runtime_initialization_failed",
            metadata->id);
        dluma_mod_artifact_release(&source_artifact);
        memset(slot, 0, sizeof(*slot));
        return 0;
    }
    file_path[0] = 0;
    dluma_wide_to_utf8(metadata->file_path, file_path, DLUMA_ARRAY_COUNT(file_path));
    memset(&config, 0, sizeof(config));
    config.struct_size = sizeof(config);
    config.scope = metadata->scope;
    config.map_generation = g_dluma_lua_runtime.map_generation;
    config.approved_permissions = permissions;
    config.capabilities = slot->capabilities;
    config.owner_id = slot->owner_id;
    config.source_fingerprint = metadata->fingerprint;
    config.memory_limit_bytes = g_dluma.config.lua_memory_limit_bytes;
    config.instruction_limit = g_dluma.config.lua_instruction_limit;
    config.wall_clock_limit_ms = g_dluma.config.lua_wall_clock_limit_ms;
    config.max_callbacks = g_dluma.config.lua_max_callbacks;
    config.max_timers = g_dluma.config.lua_max_timers;
    config.max_rules = g_dluma.config.lua_max_rules;
    config.max_log_messages_per_second = g_dluma.config.lua_log_messages_per_second;
    config.id = metadata->id;
    config.name = metadata->name;
    config.version = metadata->version;
    config.permissions = metadata->permissions;
    config.file_path = file_path;
    config.project = g_dluma_lua_runtime.current_project_name;
    config.map = g_dluma_lua_runtime.current_map;
    config.workshop_id = metadata->workshop_id;
    config.loader_version = DLUMA_VERSION;
    config.is_editor = g_dluma_lua_runtime.is_editor;
    config.is_player = g_dluma_lua_runtime.is_player;
    config.host_user = slot;
    config.host.struct_size = sizeof(config.host);
    config.host.clock_ms = dluma_lua_runtime_clock;
    config.host.log = dluma_lua_runtime_log;
    config.host.storage_get = dluma_lua_runtime_storage_get;
    config.host.storage_set = dluma_lua_runtime_storage_set;
    config.host.storage_delete = dluma_lua_runtime_storage_delete;
    config.host.debug_set = dluma_lua_runtime_debug_set;
    config.host.subscription_added = dluma_lua_runtime_subscription_added;
    config.host.subscription_removed = dluma_lua_runtime_subscription_removed;
    config.host.world_find = dluma_lua_runtime_world_find;
    config.host.world_get_info = dluma_lua_runtime_world_get_info;
    config.host.world_get_local_enabled = dluma_lua_runtime_world_get_local_enabled;
    config.host.world_get_position = dluma_lua_runtime_world_get_position;
    config.host.world_get_forward = dluma_lua_runtime_world_get_forward;
    config.host.world_set_local_enabled = dluma_lua_runtime_world_set_local_enabled;
    config.host.sensor_is_inside = dluma_lua_runtime_sensor_is_inside;
    config.host.subtitle_show = dluma_lua_runtime_subtitle_show;
    config.host.subtitle_update = dluma_lua_runtime_subtitle_update;
    config.host.radio_dialog = dluma_lua_runtime_radio_dialog;
    config.host.action_run_chain = dluma_lua_runtime_action_run_chain;
    config.host.quest_mutate = dluma_lua_runtime_quest_mutate;
    config.host.quest_state_changed = dluma_lua_runtime_quest_state_changed;
    config.host.quest_hud_snapshot = dluma_lua_runtime_quest_hud_snapshot;
    config.host.quest_hud_override_set =
        dluma_lua_runtime_quest_hud_override_set;
    config.host.quest_hud_override_clear =
        dluma_lua_runtime_quest_hud_override_clear;
    config.host.quest_hud_clear_owner =
        dluma_lua_runtime_quest_hud_clear_owner;
    config.host.capabilities = dluma_lua_runtime_current_capabilities;
    config.host.config_get = dluma_lua_runtime_config_get;
    config.host.module_load = dluma_lua_runtime_module_load;
    config.host.module_release =
        dluma_lua_runtime_module_release;
    config.host.capability_query =
        dluma_lua_runtime_capability_query;
    config.host.api_invoke =
        dluma_lua_runtime_api_invoke;
    slot->helper_script = g_dluma_lua_runtime.api.create_script(&config);
    if (!slot->helper_script ||
        !g_dluma_lua_runtime.api.load_script(slot->helper_script, source, source_size)) {
        dluma_log_pair("ERROR", "lua_state_load_failed", metadata->id);
        if (metadata->scope == DLUMA_LUA_SCOPE_GLOBAL)
            dluma_lua_update_global_runtime_status(
                metadata->registry_key, metadata->metadata_fingerprint,
                0, "Lua state creation or chunk initialization failed");
        if (slot->helper_script &&
            !g_dluma_lua_runtime.api.destroy_script(slot->helper_script)) {
            slot->teardown_pending = 1;
            dluma_mod_owner_begin_revoke(&slot->api_owner);
            dluma_mod_artifact_release(&source_artifact);
            dluma_log_pair("WARN", "lua_failed_load_teardown_pending",
                         metadata->id);
            return 0;
        }
        dluma_mod_owner_begin_revoke(&slot->api_owner);
        dluma_game_host_context_revoke(
            &slot->api_host);
        dluma_mod_owner_finish_revoke(&slot->api_owner);
        dluma_mod_artifact_release(&source_artifact);
        memset(slot, 0, sizeof(*slot));
        return 0;
    }
    dluma_mod_artifact_release(&source_artifact);
    dluma_log_pair("INFO", "lua_state_created", metadata->id);
    if (metadata->scope == DLUMA_LUA_SCOPE_GLOBAL)
        dluma_lua_update_global_runtime_status(
            metadata->registry_key, metadata->metadata_fingerprint, 1, "");
    dluma_lua_runtime_dispatch_slot(slot, DLUMA_LUA54_EVENT_LOAD, "on_load", 0.0, 0);
    return 1;
}

static int dluma_lua_runtime_metadata_matches(
    const DlumaLuaRuntimeSlot* slot,
    const DlumaLuaMetadata* metadata)
{
    return slot && metadata && slot->occupied &&
        slot->metadata.scope == metadata->scope &&
        dluma_stricmp(slot->metadata.id, metadata->id) == 0 &&
        slot->metadata.fingerprint == metadata->fingerprint &&
        dluma_stricmp(slot->metadata.content_sha256,
                    metadata->content_sha256) == 0 &&
        dluma_wstricmp(slot->metadata.file_path, metadata->file_path) == 0;
}

static void dluma_lua_runtime_reconcile_globals(void)
{
    uint64_t epoch = ++g_dluma_lua_runtime.global_epoch;
    uint32_t i;
    for (i = 0; i < g_dluma_lua_runtime.committed_global_count; ++i) {
        DlumaLuaMetadata* metadata = &g_dluma_lua_runtime.committed_globals[i];
        DlumaLuaRuntimeSlot* slot;
        if (!metadata->valid || metadata->duplicate_id ||
            !metadata->source_selected || !metadata->enabled ||
            !metadata->has_enabled_override || !metadata->approval_valid)
            continue;
        slot = dluma_lua_runtime_find_slot(DLUMA_LUA_SCOPE_GLOBAL, metadata->id);
        if (slot && slot->teardown_pending) {
            dluma_lua_runtime_destroy_slot(slot, 0, 0);
            if (!slot->occupied)
                slot = 0;
            else {
                slot->desired_epoch = epoch;
                continue;
            }
        }
        if (slot && !dluma_lua_runtime_metadata_matches(slot, metadata)) {
            dluma_lua_runtime_destroy_slot(slot, 1, 1);
            if (!slot->occupied)
                slot = 0;
            else {
                slot->desired_epoch = epoch;
                continue;
            }
        }
        if (!slot) {
            dluma_lua_runtime_create_slot(metadata);
            slot = dluma_lua_runtime_find_slot(DLUMA_LUA_SCOPE_GLOBAL, metadata->id);
            if (slot && g_dluma_lua_runtime.current_map[0])
                dluma_lua_runtime_dispatch_slot(slot, DLUMA_LUA54_EVENT_MAP_LOAD,
                                              g_dluma_lua_runtime.current_map, 0.0, 0);
        }
        if (slot) slot->desired_epoch = epoch;
    }
    for (i = 0; i < DLUMA_LUA_RUNTIME_MAX_STATES; ++i) {
        DlumaLuaRuntimeSlot* slot = &g_dluma_lua_runtime.slots[i];
        if (slot->occupied && slot->metadata.scope == DLUMA_LUA_SCOPE_GLOBAL &&
            slot->desired_epoch != epoch)
            dluma_lua_runtime_destroy_slot(slot, 1, 1);
    }
    for (i = 0; i < g_dluma_lua_runtime.committed_global_count; ++i) {
        DlumaLuaMetadata* metadata = &g_dluma_lua_runtime.committed_globals[i];
        if (metadata->runtime_transition == DLUMA_GLOBAL_TRANSITION_DISABLING &&
            !dluma_lua_runtime_find_slot(DLUMA_LUA_SCOPE_GLOBAL, metadata->id))
            dluma_lua_update_global_runtime_status(
                metadata->registry_key,
                metadata->metadata_fingerprint,
                0,
                "");
    }
}

static void dluma_lua_runtime_dispatch_scope(uint32_t scope, uint32_t kind, const char* name)
{
    unsigned int i;
    for (i = 0; i < DLUMA_LUA_RUNTIME_MAX_STATES; ++i) {
        DlumaLuaRuntimeSlot* slot = &g_dluma_lua_runtime.slots[i];
        if (slot->occupied && slot->metadata.scope == scope)
            dluma_lua_runtime_dispatch_slot(slot, kind, name, 0.0, 0);
    }
}

static void dluma_lua_runtime_destroy_scope(uint32_t scope, int send_unload)
{
    unsigned int i;
    for (i = 0; i < DLUMA_LUA_RUNTIME_MAX_STATES; ++i) {
        DlumaLuaRuntimeSlot* slot = &g_dluma_lua_runtime.slots[i];
        if (slot->occupied && slot->metadata.scope == scope)
            dluma_lua_runtime_destroy_slot(slot, 0, send_unload);
    }
}

static void dluma_lua_runtime_reconcile_local_scope(uint32_t scope)
{
    uint64_t epoch = ++g_dluma_lua_runtime.global_epoch;
    uint32_t i;
    for (i = 0; i < g_dluma_lua_runtime.committed_local_count; ++i) {
        DlumaLuaMetadata* metadata = &g_dluma_lua_runtime.committed_locals[i];
        DlumaLuaRuntimeSlot* slot;
        if (metadata->scope != scope || !metadata->valid || metadata->duplicate_id ||
            !metadata->enabled)
            continue;
        slot = dluma_lua_runtime_find_slot(scope, metadata->id);
        if (slot && slot->teardown_pending) {
            dluma_lua_runtime_destroy_slot(slot, 0, 0);
            if (!slot->occupied)
                slot = 0;
            else {
                slot->desired_epoch = epoch;
                continue;
            }
        }
        if (slot && !dluma_lua_runtime_metadata_matches(slot, metadata)) {
            dluma_lua_runtime_destroy_slot(slot, 0, 1);
            if (!slot->occupied)
                slot = 0;
            else {
                slot->desired_epoch = epoch;
                continue;
            }
        }
        if (!slot) {
            dluma_lua_runtime_create_slot(metadata);
            slot = dluma_lua_runtime_find_slot(scope, metadata->id);
        }
        if (slot) slot->desired_epoch = epoch;
    }
    for (i = 0; i < DLUMA_LUA_RUNTIME_MAX_STATES; ++i) {
        DlumaLuaRuntimeSlot* slot = &g_dluma_lua_runtime.slots[i];
        if (slot->occupied && slot->metadata.scope == scope &&
            slot->desired_epoch != epoch)
            dluma_lua_runtime_destroy_slot(slot, 0, 1);
    }
}

static void dluma_lua_runtime_apply_context(void)
{
    int had_map = g_dluma_lua_runtime.current_map[0] != 0;
    int same_project = had_map && g_dluma_lua_runtime.next_project_root[0] &&
        dluma_wstricmp(g_dluma_lua_runtime.current_project_root,
                     g_dluma_lua_runtime.next_project_root) == 0;
    unsigned int slot_index;
    if (had_map) {
        dluma_lua_runtime_dispatch_scope(DLUMA_LUA_SCOPE_GLOBAL,
                                       DLUMA_LUA54_EVENT_MAP_UNLOAD,
                                       g_dluma_lua_runtime.current_map);
        dluma_lua_runtime_dispatch_scope(DLUMA_LUA_SCOPE_PROJECT,
                                       DLUMA_LUA54_EVENT_MAP_UNLOAD,
                                       g_dluma_lua_runtime.current_map);
        dluma_lua_runtime_dispatch_scope(DLUMA_LUA_SCOPE_MAP,
                                       DLUMA_LUA54_EVENT_MAP_UNLOAD,
                                       g_dluma_lua_runtime.current_map);
        /* Keep only a read-only final presentation copy when returning to
         * the frontend.  This must happen after unload callbacks have had a
         * chance to report final state, but before project/map slots erase
         * their live owner snapshots. */
        if (!g_dluma_lua_runtime.next_map[0])
            dluma_lua_runtime_archive_quest_snapshots_for_frontend();
        dluma_lua_runtime_destroy_scope(DLUMA_LUA_SCOPE_MAP, 1);
        if (!same_project) dluma_lua_runtime_destroy_scope(DLUMA_LUA_SCOPE_PROJECT, 1);
    }
    if (g_dluma_lua_runtime.next_map[0])
        dluma_lua_runtime_clear_archived_quest_snapshots();
    /*
     * Cross-language scope exit is reverse activation order. Lua callbacks
     * and states above have already quiesced; managed follows, then native.
     */
    dluma_managed_host_deactivate_scope(
        g_dluma_lua_runtime.map_generation + 1U);
    dluma_native_mods_deactivate_scope(
        g_dluma_lua_runtime.map_generation + 1U);
    /* A report belongs to its originating map generation.  Clear every
     * cached presentation record before current_map/current generation change
     * so UI can never render a departed map's Lua quest. */
    dluma_lua_runtime_clear_quest_snapshots();
    ++g_dluma_lua_runtime.map_generation;
    for (slot_index = 0;
         slot_index < DLUMA_LUA_RUNTIME_MAX_STATES;
         ++slot_index) {
        DlumaLuaRuntimeSlot* slot =
            &g_dluma_lua_runtime.slots[slot_index];
        if (slot->occupied && !slot->teardown_pending) {
            dluma_mod_owner_advance_scope(
                &slot->api_owner,
                g_dluma_lua_runtime.map_generation);
            if (slot->metadata.scope == DLUMA_LUA_SCOPE_GLOBAL &&
                !dluma_game_host_context_needs_map_rebind(&slot->api_host))
                dluma_game_host_context_rebind_map(
                    &slot->api_host);
            else if(slot->metadata.scope != DLUMA_LUA_SCOPE_GLOBAL)
                dluma_game_host_context_advance_scope(
                    &slot->api_host);
        }
    }
    g_dluma_lua_runtime.unhandled_event_log_count = 0;
    dluma_wstr_copy(g_dluma_lua_runtime.current_project_root, DLUMA_MAX_WPATH,
                  g_dluma_lua_runtime.next_project_root);
    dluma_str_copy(g_dluma_lua_runtime.current_project_name,
                 DLUMA_ARRAY_COUNT(g_dluma_lua_runtime.current_project_name),
                 g_dluma_lua_runtime.next_project_name);
    dluma_str_copy(g_dluma_lua_runtime.current_workshop_id,
                 DLUMA_ARRAY_COUNT(g_dluma_lua_runtime.current_workshop_id),
                 g_dluma_lua_runtime.next_workshop_id);
    dluma_str_copy(g_dluma_lua_runtime.current_map,
                 DLUMA_ARRAY_COUNT(g_dluma_lua_runtime.current_map),
                 g_dluma_lua_runtime.next_map);
    dluma_native_gameplay_on_map_generation_changed(
        g_dluma_lua_runtime.map_generation,
        g_dluma_lua_runtime.current_map,
        g_dluma.engine.last_level_interface);
    for(slot_index=0;slot_index<DLUMA_LUA_RUNTIME_MAX_STATES;++slot_index){
        DlumaLuaRuntimeSlot* slot=&g_dluma_lua_runtime.slots[slot_index];
        if(slot->occupied && slot->metadata.scope==DLUMA_LUA_SCOPE_GLOBAL &&
           dluma_game_host_context_needs_map_rebind(&slot->api_host))
            dluma_game_host_context_rebind_map(&slot->api_host);
        if(slot->occupied && g_dluma_lua_runtime.current_map[0])slot->map_unload_sent=0;
    }
    if (dluma_quest_phase_player_started() &&
        !dluma_quest_phase_player_on_map_generation_changed(
            g_dluma_lua_runtime.current_project_root,
            g_dluma_lua_runtime.current_map,
            g_dluma_lua_runtime.map_generation))
        dluma_log_line(
            "ERROR",
            "Custom quest phase sidecars were rejected for the active map generation; no prior binding generation was retained.");
    /* Incoming activation is native -> managed -> Lua. */
    dluma_native_mods_activate_scope(
        g_dluma_lua_runtime.current_project_root,
        g_dluma_lua_runtime.current_project_name,
        g_dluma_lua_runtime.current_map,
        g_dluma_lua_runtime.map_generation);
    dluma_managed_host_activate_scope(
        g_dluma_lua_runtime.current_project_root,
        g_dluma_lua_runtime.current_project_name,
        g_dluma_lua_runtime.current_map,
        g_dluma_lua_runtime.map_generation);
    if (!g_dluma_lua_runtime.current_map[0]) {
        g_dluma_lua_project_exit_gate = 0;
        return;
    }
    dluma_lua_runtime_reconcile_local_scope(DLUMA_LUA_SCOPE_PROJECT);
    dluma_lua_runtime_reconcile_local_scope(DLUMA_LUA_SCOPE_MAP);
    dluma_lua_runtime_dispatch_scope(DLUMA_LUA_SCOPE_GLOBAL,
                                   DLUMA_LUA54_EVENT_MAP_LOAD,
                                   g_dluma_lua_runtime.current_map);
    dluma_lua_runtime_dispatch_scope(DLUMA_LUA_SCOPE_PROJECT,
                                   DLUMA_LUA54_EVENT_MAP_LOAD,
                                   g_dluma_lua_runtime.current_map);
    dluma_lua_runtime_dispatch_scope(DLUMA_LUA_SCOPE_MAP,
                                   DLUMA_LUA54_EVENT_MAP_LOAD,
                                   g_dluma_lua_runtime.current_map);
}

static void dluma_lua_runtime_take_pending(void)
{
    unsigned int i;
    int globals_changed = 0;
    int context_changed = 0;
    dluma_lua_runtime_lock(&g_dluma_lua_runtime.pending_lock);
    if (g_dluma_lua_runtime.pending_global_revision != g_dluma_lua_runtime.global_revision) {
        g_dluma_lua_runtime.global_revision = g_dluma_lua_runtime.pending_global_revision;
        g_dluma_lua_runtime.committed_global_count = g_dluma_lua_runtime.pending_global_count;
        for (i = 0; i < g_dluma_lua_runtime.committed_global_count; ++i)
            g_dluma_lua_runtime.committed_globals[i] = g_dluma_lua_runtime.pending_globals[i];
        globals_changed = 1;
    }
    if (g_dluma_lua_runtime.pending_context_sequence != g_dluma_lua_runtime.context_sequence) {
        g_dluma_lua_runtime.context_sequence = g_dluma_lua_runtime.pending_context_sequence;
        dluma_wstr_copy(g_dluma_lua_runtime.next_project_root, DLUMA_MAX_WPATH,
                      g_dluma_lua_runtime.pending_project_root);
        dluma_str_copy(g_dluma_lua_runtime.next_project_name,
                     DLUMA_ARRAY_COUNT(g_dluma_lua_runtime.next_project_name),
                     g_dluma_lua_runtime.pending_project_name);
        dluma_str_copy(g_dluma_lua_runtime.next_workshop_id,
                     DLUMA_ARRAY_COUNT(g_dluma_lua_runtime.next_workshop_id),
                     g_dluma_lua_runtime.pending_workshop_id);
        dluma_str_copy(g_dluma_lua_runtime.next_map,
                     DLUMA_ARRAY_COUNT(g_dluma_lua_runtime.next_map),
                     g_dluma_lua_runtime.pending_map);
        g_dluma_lua_runtime.committed_local_count = g_dluma_lua_runtime.pending_local_count;
        for (i = 0; i < g_dluma_lua_runtime.committed_local_count; ++i)
            g_dluma_lua_runtime.committed_locals[i] = g_dluma_lua_runtime.pending_locals[i];
        context_changed = 1;
    }
    dluma_lua_runtime_unlock(&g_dluma_lua_runtime.pending_lock);
    if (globals_changed) dluma_lua_runtime_reconcile_globals();
    if (context_changed) dluma_lua_runtime_apply_context();
}

static const char* dluma_lua_runtime_native_event_kind_name(uint32_t kind)
{
    switch (kind) {
    case DLUMA_LUA54_EVENT_SENSOR_ENTER: return "sensor_enter";
    case DLUMA_LUA54_EVENT_SENSOR_LEAVE: return "sensor_leave";
    case DLUMA_LUA54_EVENT_INTERACT_BEGIN: return "interact_begin";
    default: return "unknown";
    }
}

static void dluma_lua_runtime_append_native_event_identity(
    char* text,
    unsigned int capacity,
    const DlumaLuaRuntimeEvent* event)
{
    char number[32];
    dluma_str_append(text, capacity, "kind=");
    dluma_str_append(text, capacity, dluma_lua_runtime_native_event_kind_name(event->kind));
    dluma_str_append(text, capacity, "(");
    dluma_u64_to_text(event->kind, number, DLUMA_ARRAY_COUNT(number));
    dluma_str_append(text, capacity, number);
    dluma_str_append(text, capacity, ") name=");
    dluma_str_append(text, capacity, event->name);
    dluma_str_append(text, capacity, " generation=");
    dluma_u64_to_text(event->map_generation, number, DLUMA_ARRAY_COUNT(number));
    dluma_str_append(text, capacity, number);
    dluma_str_append(text, capacity, " native_index_epoch=");
    dluma_u64_to_text(event->native_index_epoch, number, DLUMA_ARRAY_COUNT(number));
    dluma_str_append(text, capacity, number);
    dluma_str_append(text, capacity, " native_subscription_key_revision=");
    dluma_u64_to_text(event->native_subscription_key_revision,
                    number, DLUMA_ARRAY_COUNT(number));
    dluma_str_append(text, capacity, number);
}

static void dluma_lua_runtime_log_native_event_slot_delivery(
    const DlumaLuaRuntimeEvent* event,
    const DlumaLuaRuntimeSlot* slot,
    int callback_count)
{
    char text[512];
    char number[32];
    text[0] = 0;
    dluma_lua_runtime_append_native_event_identity(text, DLUMA_ARRAY_COUNT(text), event);
    dluma_str_append(text, DLUMA_ARRAY_COUNT(text), " script_id=");
    dluma_str_append(text, DLUMA_ARRAY_COUNT(text), slot->metadata.id);
    dluma_str_append(text, DLUMA_ARRAY_COUNT(text), " callback_count=");
    dluma_u64_to_text((ULONGLONG)callback_count, number, DLUMA_ARRAY_COUNT(number));
    dluma_str_append(text, DLUMA_ARRAY_COUNT(text), number);
    dluma_log_pair("INFO", "lua_native_event_slot_delivered", text);
}

static void dluma_lua_runtime_log_native_event_result(
    const DlumaLuaRuntimeEvent* event,
    int callback_count)
{
    char text[512];
    char number[32];
    text[0] = 0;
    dluma_lua_runtime_append_native_event_identity(text, DLUMA_ARRAY_COUNT(text), event);
    dluma_str_append(text, DLUMA_ARRAY_COUNT(text), " callback_count=");
    dluma_u64_to_text((ULONGLONG)callback_count, number, DLUMA_ARRAY_COUNT(number));
    dluma_str_append(text, DLUMA_ARRAY_COUNT(text), number);
    dluma_log_pair(callback_count > 0 ? "INFO" : "DEBUG",
        callback_count > 0 ? "lua_native_event_delivered" :
                             "lua_native_event_unhandled",
        text);
}

static void dluma_lua_runtime_dispatch_queued_events(void)
{
    for (;;) {
        DlumaLuaRuntimeEvent event;
        unsigned int i;
        int have_event = 0;
        int delivered = 0;
        memset(&event, 0, sizeof(event));
        dluma_lua_runtime_lock(&g_dluma_lua_runtime.event_lock);
        if (g_dluma_lua_runtime.event_count) {
            event = g_dluma_lua_runtime.events[g_dluma_lua_runtime.event_head];
            g_dluma_lua_runtime.event_head =
                (g_dluma_lua_runtime.event_head + 1) % DLUMA_LUA_RUNTIME_MAX_EVENTS;
            --g_dluma_lua_runtime.event_count;
            have_event = 1;
        }
        dluma_lua_runtime_unlock(&g_dluma_lua_runtime.event_lock);
        if (!have_event) break;
        if (event.map_generation != g_dluma_lua_runtime.map_generation ||
            event.native_index_epoch !=
                dluma_native_gameplay_index_epoch() ||
            !dluma_native_gameplay_event_subscription_current(
                event.kind, event.map_generation,
                event.native_index_epoch,
                event.native_subscription_key_revision,
                event.name)) continue;
        for (i = 0; i < DLUMA_LUA_RUNTIME_MAX_STATES; ++i) {
            DlumaLuaRuntimeSlot* slot = &g_dluma_lua_runtime.slots[i];
            unsigned int owner_index;
            int owner_matches = 0;
            for (owner_index = 0; owner_index < event.owner_count;
                 ++owner_index) {
                if (event.owner_ids[owner_index] == slot->owner_id) {
                    owner_matches = 1;
                    break;
                }
            }
            if (slot->occupied && !slot->teardown_pending && owner_matches) {
                int slot_delivered = dluma_lua_runtime_dispatch_slot(
                    slot, event.kind, event.name, event.number, event.state);
                if (slot_delivered > 0) {
                    delivered += slot_delivered;
                    dluma_lua_runtime_log_native_event_slot_delivery(
                        &event, slot, slot_delivered);
                }
            }
        }
        if (delivered > 0) {
            dluma_lua_runtime_log_native_event_result(&event, delivered);
        } else if (g_dluma_lua_runtime.unhandled_event_log_count <
                   DLUMA_LUA_RUNTIME_MAX_UNHANDLED_EVENT_LOGS) {
            ++g_dluma_lua_runtime.unhandled_event_log_count;
            dluma_lua_runtime_log_native_event_result(&event, 0);
        }
    }
}

int dluma_lua_runtime_start(void)
{
    WCHAR path[DLUMA_MAX_WPATH];
    WCHAR executable[DLUMA_MAX_WPATH];
    char path_utf8[DLUMA_MAX_WPATH * 2];
    DlumaLua54GetApiFn get_api;
    DWORD error;
    memset(&g_dluma_lua_runtime, 0, sizeof(g_dluma_lua_runtime));
    dluma_path_join(path, DLUMA_ARRAY_COUNT(path), g_dluma.module_dir, L"dluma_lua54.dll");
    path_utf8[0] = 0;
    dluma_wide_to_utf8(path, path_utf8, DLUMA_ARRAY_COUNT(path_utf8));
    g_dluma_lua_runtime.helper_module = LoadLibraryW(path);
    if (!g_dluma_lua_runtime.helper_module) {
        error = GetLastError();
        dluma_log_pair("ERROR", "lua_runtime_path", path_utf8);
        dluma_log_hex("ERROR", "lua_runtime_load_win32_error", error);
        dluma_log_line("WARN", "Lua runtime unavailable; discovery and the original game script manager remain active, and no chunks will execute.");
        return 0;
    }
    get_api = (DlumaLua54GetApiFn)GetProcAddress(
        g_dluma_lua_runtime.helper_module, "DLUMA_Lua54_GetApi");
    memset(&g_dluma_lua_runtime.api, 0, sizeof(g_dluma_lua_runtime.api));
    if (!get_api || !get_api(DLUMA_LUA54_ABI_VERSION, &g_dluma_lua_runtime.api,
                             sizeof(g_dluma_lua_runtime.api)) ||
        g_dluma_lua_runtime.api.struct_size <
            sizeof(g_dluma_lua_runtime.api) ||
        g_dluma_lua_runtime.api.abi_version != DLUMA_LUA54_ABI_VERSION ||
        !g_dluma_lua_runtime.api.release ||
        dluma_stricmp(g_dluma_lua_runtime.api.release,
                    DLUMA_LUA54_EXPECTED_RELEASE) != 0 ||
        !g_dluma_lua_runtime.api.create_script ||
        !g_dluma_lua_runtime.api.load_script ||
        !g_dluma_lua_runtime.api.dispatch ||
        !g_dluma_lua_runtime.api.get_status ||
        !g_dluma_lua_runtime.api.destroy_script ||
        !g_dluma_lua_runtime.api.begin_pump) {
        dluma_log_pair("ERROR", "lua_runtime_rejected_path", path_utf8);
        dluma_log_line("ERROR", "Lua helper ABI or exact release did not match Lua 5.4.8.");
        FreeLibrary(g_dluma_lua_runtime.helper_module);
        memset(&g_dluma_lua_runtime, 0, sizeof(g_dluma_lua_runtime));
        return 0;
    }
    executable[0] = 0;
    if (!dluma_get_module_path_checked(
            0, executable, DLUMA_ARRAY_COUNT(executable)))
        executable[0] = 0;
    g_dluma_lua_runtime.is_editor =
        (unsigned char)(dluma_wcontains_icase(executable, L"DyingLightEditor") ||
                        dluma_wcontains_icase(executable, L"ChromeEd"));
    g_dluma_lua_runtime.is_player =
        (unsigned char)(dluma_wcontains_icase(executable, L"DyingLightPlayer") ||
                        (!g_dluma_lua_runtime.is_editor));
    g_dluma_lua_runtime.available = 1;
    g_dluma_lua_runtime.map_generation = 1;
    dluma_log_pair("INFO", "lua_runtime_path", path_utf8);
    dluma_log_pair("INFO", "lua_runtime_release", g_dluma_lua_runtime.api.release);
    dluma_log_line("INFO", "Private Lua helper loaded by full path with one isolated state per eligible script.");
    return 1;
}

int dluma_lua_runtime_is_available(void)
{
    return g_dluma_lua_runtime.available && !g_dluma_lua_runtime.quiescing;
}

unsigned int dluma_lua_runtime_snapshot_quests(
    DlumaLuaQuestUiSnapshot* out,
    unsigned int capacity,
    unsigned int* total,
    ULONGLONG* revision,
    int* archived)
{
    unsigned int i;
    unsigned int copied = 0;
    unsigned int available = 0;
    uint32_t generation;
    uint32_t source_count;
    const DlumaLuaQuestUiSnapshot* source;
    int use_archive;
    dluma_lua_runtime_lock(&g_dluma_lua_runtime.quest_lock);
    generation = g_dluma_lua_runtime.map_generation;
    use_archive = !g_dluma_lua_runtime.current_map[0] &&
        g_dluma_lua_runtime.archived_quest_snapshot_count != 0;
    source = use_archive ? g_dluma_lua_runtime.archived_quests :
                           g_dluma_lua_runtime.quests;
    source_count = use_archive ?
        g_dluma_lua_runtime.archived_quest_snapshot_count :
        g_dluma_lua_runtime.quest_snapshot_count;
    for (i = 0; i < source_count; ++i) {
        const DlumaLuaQuestUiSnapshot* snapshot = &source[i];
        if (!use_archive && snapshot->map_generation != generation) continue;
        ++available;
        if (out && copied < capacity)
            out[copied++] = *snapshot;
    }
    if (total) *total = available;
    if (revision) *revision = use_archive
        ? g_dluma_lua_runtime.archived_quest_snapshot_revision
        : g_dluma_lua_runtime.quest_snapshot_revision;
    if (archived) *archived = use_archive;
    dluma_lua_runtime_unlock(&g_dluma_lua_runtime.quest_lock);
    return copied;
}

static void dluma_lua_runtime_append_missing_bindings(
    char* text, unsigned int capacity, const DlumaLuaRuntimeSlot* slot)
{
    int any = 0;
    if (!slot) return;
#define DLUMA_APPEND_MISSING(permission_bit, capability_bit, label) \
    do { \
        if ((slot->approved_permissions & (permission_bit)) && \
            !(slot->capabilities & (capability_bit))) { \
            dluma_str_append(text, capacity, any ? "," : " [native unavailable: "); \
            dluma_str_append(text, capacity, label); \
            any = 1; \
        } \
    } while (0)
    DLUMA_APPEND_MISSING(DLUMA_LUA54_PERMISSION_EVENTS_SENSOR,
                       DLUMA_LUA54_CAP_EVENTS_SENSOR, "sensor");
    DLUMA_APPEND_MISSING(DLUMA_LUA54_PERMISSION_EVENTS_INTERACT,
                       DLUMA_LUA54_CAP_EVENTS_INTERACT, "interact");
    DLUMA_APPEND_MISSING(DLUMA_LUA54_PERMISSION_PLAYER_READ,
                       DLUMA_LUA54_CAP_PLAYER_READ, "player");
    DLUMA_APPEND_MISSING(DLUMA_LUA54_PERMISSION_WORLD_QUERY,
                       DLUMA_LUA54_CAP_WORLD_FIND, "world-query");
    DLUMA_APPEND_MISSING(DLUMA_LUA54_PERMISSION_WORLD_WRITE,
                       DLUMA_LUA54_CAP_WORLD_LOCAL_ENABLE, "world-write");
    DLUMA_APPEND_MISSING(DLUMA_LUA54_PERMISSION_UI_OVERLAY,
                       DLUMA_LUA54_CAP_UI_SUBTITLES, "subtitles");
    DLUMA_APPEND_MISSING(DLUMA_LUA54_PERMISSION_GAME_READ,
                       DLUMA_LUA54_CAP_UI_QUEST_HUD, "quest-hud-read");
    DLUMA_APPEND_MISSING(DLUMA_LUA54_PERMISSION_UI_OVERLAY,
                       DLUMA_LUA54_CAP_UI_QUEST_HUD, "quest-hud-override");
    DLUMA_APPEND_MISSING(DLUMA_LUA54_PERMISSION_AUDIO_PLAY,
                       DLUMA_LUA54_CAP_AUDIO_RADIO_DIALOG, "radio-dialog");
    DLUMA_APPEND_MISSING(DLUMA_LUA54_PERMISSION_QUEST_WRITE,
                       DLUMA_LUA54_CAP_QUEST_NATIVE_CONTROL, "quest-native");
    DLUMA_APPEND_MISSING(DLUMA_LUA54_PERMISSION_EVENTS_ENEMY_KILLED,
                       DLUMA_LUA54_CAP_EVENTS_ENEMY_KILLED, "enemy-killed");
    DLUMA_APPEND_MISSING(DLUMA_LUA54_PERMISSION_QUEST_PHASE_HANDLE,
                       DLUMA_LUA54_CAP_QUEST_PHASE_HANDLERS,
                       "quest-phase-provider");
    DLUMA_APPEND_MISSING(DLUMA_LUA54_PERMISSION_QUEST_PHASE_OBSERVE,
                       DLUMA_LUA54_CAP_QUEST_PHASE_HANDLERS,
                       "quest-phase-observer");
#undef DLUMA_APPEND_MISSING
    if (any) dluma_str_append(text, capacity, "]");
}

static void dluma_lua_runtime_append_local_policy_denials(
    char* text, unsigned int capacity, const DlumaLuaRuntimeSlot* slot)
{
    uint32_t denied;
    int any = 0;
    if (!slot || slot->metadata.scope == DLUMA_LUA_SCOPE_GLOBAL) return;
    denied = dluma_lua_runtime_requested_permissions(slot->metadata.permissions) &
             ~slot->approved_permissions;
#define DLUMA_APPEND_DENIED(permission_bit, label) \
    do { \
        if (denied & (permission_bit)) { \
            dluma_str_append(text, capacity, any ? "," : " [local policy off: "); \
            dluma_str_append(text, capacity, label); \
            any = 1; \
        } \
    } while (0)
    DLUMA_APPEND_DENIED(DLUMA_LUA54_PERMISSION_GAME_READ, "game.read");
    DLUMA_APPEND_DENIED(DLUMA_LUA54_PERMISSION_PLAYER_READ, "player.read");
    DLUMA_APPEND_DENIED(DLUMA_LUA54_PERMISSION_WORLD_QUERY, "world.query");
    DLUMA_APPEND_DENIED(DLUMA_LUA54_PERMISSION_EVENTS_SENSOR, "events.sensor");
    DLUMA_APPEND_DENIED(DLUMA_LUA54_PERMISSION_EVENTS_INTERACT, "events.interact");
    DLUMA_APPEND_DENIED(DLUMA_LUA54_PERMISSION_STORAGE_MOD, "storage.mod");
    DLUMA_APPEND_DENIED(DLUMA_LUA54_PERMISSION_UI_DEBUG, "ui.debug");
    DLUMA_APPEND_DENIED(DLUMA_LUA54_PERMISSION_UI_OVERLAY, "ui.overlay");
    DLUMA_APPEND_DENIED(DLUMA_LUA54_PERMISSION_AUDIO_PLAY, "audio.play");
    DLUMA_APPEND_DENIED(DLUMA_LUA54_PERMISSION_WORLD_WRITE, "world.write");
    DLUMA_APPEND_DENIED(DLUMA_LUA54_PERMISSION_QUEST_WRITE, "quest.write");
    DLUMA_APPEND_DENIED(DLUMA_LUA54_PERMISSION_EVENTS_ENEMY_KILLED,
                      "events.enemy_killed");
    DLUMA_APPEND_DENIED(DLUMA_LUA54_PERMISSION_QUEST_PHASE_HANDLE,
                      "quest.phase.handle");
    DLUMA_APPEND_DENIED(DLUMA_LUA54_PERMISSION_QUEST_PHASE_OBSERVE,
                      "quest.phase.observe");
#undef DLUMA_APPEND_DENIED
    if (any) dluma_str_append(text, capacity, "]");
}

void dluma_lua_runtime_append_info_text(char* text, unsigned int capacity)
{
    unsigned int i;
    unsigned int loaded = 0;
    unsigned int scripts_shown = 0;
    unsigned int debug_shown = 0;
    char number[32];
    if (!text || !capacity) return;
    for (i = 0; i < DLUMA_LUA_RUNTIME_MAX_STATES; ++i)
        if (g_dluma_lua_runtime.slots[i].occupied) ++loaded;
    dluma_u64_to_text(loaded, number, DLUMA_ARRAY_COUNT(number));
    dluma_str_append(text, capacity, "Lua states loaded: ");
    dluma_str_append(text, capacity, number);
    dluma_str_append(text, capacity, "\n");
    for (i = 0; i < DLUMA_LUA_RUNTIME_MAX_STATES && scripts_shown < 4; ++i) {
        DlumaLuaRuntimeSlot* slot = &g_dluma_lua_runtime.slots[i];
        if (!slot->occupied) continue;
        dluma_str_append(text, capacity,
            slot->metadata.scope == DLUMA_LUA_SCOPE_GLOBAL ? "  [G] " :
            (slot->metadata.scope == DLUMA_LUA_SCOPE_PROJECT ? "  [P] " : "  [M] "));
        dluma_str_append(text, capacity, slot->metadata.id);
        if (slot->status.disabled) dluma_str_append(text, capacity, " [DISABLED]");
        dluma_lua_runtime_append_local_policy_denials(text, capacity, slot);
        dluma_lua_runtime_append_missing_bindings(text, capacity, slot);
        dluma_str_append(text, capacity, "\n");
        ++scripts_shown;
    }
    if (loaded > scripts_shown) {
        dluma_u64_to_text(loaded - scripts_shown, number, DLUMA_ARRAY_COUNT(number));
        dluma_str_append(text, capacity, "  +");
        dluma_str_append(text, capacity, number);
        dluma_str_append(text, capacity, " more\n");
    }
    dluma_str_append(text, capacity, "Script debug:\n");
    for (i = 0; i < DLUMA_LUA_RUNTIME_MAX_STATES && debug_shown < 6; ++i) {
        DlumaLuaRuntimeSlot* slot = &g_dluma_lua_runtime.slots[i];
        unsigned int j;
        if (!slot->occupied) continue;
        for (j = 0; j < slot->debug_value_count && debug_shown < 6; ++j) {
            dluma_str_append(text, capacity, "  ");
            dluma_str_append(text, capacity, slot->metadata.id);
            dluma_str_append(text, capacity, ".");
            dluma_str_append(text, capacity, slot->debug_values[j].key);
            dluma_str_append(text, capacity, " = ");
            dluma_str_append(text, capacity, slot->debug_values[j].value);
            dluma_str_append(text, capacity, "\n");
            ++debug_shown;
        }
        for (j = 0; j < slot->debug_line_count && debug_shown < 6; ++j) {
            unsigned int index = (slot->debug_line_head + j) %
                DLUMA_LUA_RUNTIME_MAX_DEBUG_LINES;
            dluma_str_append(text, capacity, "  > ");
            dluma_str_append(text, capacity, slot->metadata.id);
            dluma_str_append(text, capacity, ": ");
            dluma_str_append(text, capacity, slot->debug_lines[index]);
            dluma_str_append(text, capacity, "\n");
            ++debug_shown;
        }
    }
    if (!debug_shown) dluma_str_append(text, capacity, "  <none>\n");
}

void dluma_lua_runtime_publish_globals(
    const DlumaLuaMetadata* entries,
    unsigned int count,
    uint64_t revision)
{
    unsigned int i;
    if (count > DLUMA_MAX_LUA_FILES) count = DLUMA_MAX_LUA_FILES;
    dluma_lua_runtime_lock(&g_dluma_lua_runtime.pending_lock);
    g_dluma_lua_runtime.pending_global_count = count;
    for (i = 0; i < count; ++i) g_dluma_lua_runtime.pending_globals[i] = entries[i];
    g_dluma_lua_runtime.pending_global_revision = revision ? revision :
        g_dluma_lua_runtime.pending_global_revision + 1;
    dluma_lua_runtime_unlock(&g_dluma_lua_runtime.pending_lock);
}

void dluma_lua_runtime_publish_context(
    const WCHAR* project_root,
    const char* project_name,
    const char* workshop_id,
    const char* map_name,
    const DlumaLuaMetadata* entries,
    unsigned int count)
{
    unsigned int i;
    if(dluma_atomic_compare_exchange(&g_dluma_map_exit_active,0,0))return;
    if (count > DLUMA_MAX_LUA_FILES) count = DLUMA_MAX_LUA_FILES;
    dluma_lua_runtime_lock(&g_dluma_lua_runtime.pending_lock);
    dluma_wstr_copy(g_dluma_lua_runtime.pending_project_root, DLUMA_MAX_WPATH,
                  project_root ? project_root : L"");
    dluma_str_copy(g_dluma_lua_runtime.pending_project_name,
                 DLUMA_ARRAY_COUNT(g_dluma_lua_runtime.pending_project_name),
                 project_name ? project_name : "");
    dluma_str_copy(g_dluma_lua_runtime.pending_workshop_id,
                 DLUMA_ARRAY_COUNT(g_dluma_lua_runtime.pending_workshop_id),
                 workshop_id ? workshop_id : "");
    dluma_str_copy(g_dluma_lua_runtime.pending_map,
                 DLUMA_ARRAY_COUNT(g_dluma_lua_runtime.pending_map),
                 map_name ? map_name : "");
    g_dluma_lua_runtime.pending_local_count = count;
    for (i = 0; i < count; ++i) g_dluma_lua_runtime.pending_locals[i] = entries[i];
    ++g_dluma_lua_runtime.pending_context_sequence;
    dluma_lua_runtime_unlock(&g_dluma_lua_runtime.pending_lock);
}

int dluma_lua_runtime_enqueue_event(
    uint32_t kind,
    uint32_t map_generation,
    uint64_t native_index_epoch,
    uint64_t native_subscription_key_revision,
    const char* exact_name,
    int state,
    double number)
{
    uint32_t index;
    unsigned int i;
    if (!exact_name || !exact_name[0] || strlen(exact_name) >= 96) return 0;
    dluma_lua_runtime_lock(&g_dluma_lua_runtime.event_lock);
    /* Only identical sensor edges coalesce. Separate successful interactions
     * must remain individually observable even when they happen in one frame. */
    if (kind == DLUMA_LUA54_EVENT_SENSOR_ENTER ||
        kind == DLUMA_LUA54_EVENT_SENSOR_LEAVE) {
        for (i = 0; i < g_dluma_lua_runtime.event_count; ++i) {
            DlumaLuaRuntimeEvent* existing = &g_dluma_lua_runtime.events[
                (g_dluma_lua_runtime.event_head + i) % DLUMA_LUA_RUNTIME_MAX_EVENTS];
            if (existing->kind == kind && existing->map_generation == map_generation &&
                existing->native_index_epoch == native_index_epoch &&
                existing->native_subscription_key_revision ==
                    native_subscription_key_revision &&
                existing->state == state &&
                strlen(existing->name) == strlen(exact_name) &&
                memcmp(existing->name, exact_name, strlen(exact_name)) == 0) {
                dluma_lua_runtime_unlock(&g_dluma_lua_runtime.event_lock);
                return 1;
            }
        }
    }
    if (g_dluma_lua_runtime.event_count >= DLUMA_LUA_RUNTIME_MAX_EVENTS) {
        dluma_lua_runtime_unlock(&g_dluma_lua_runtime.event_lock);
        if (!g_dluma_lua_runtime.overflow_reported) {
            g_dluma_lua_runtime.overflow_reported = 1;
            dluma_log_line("ERROR", "Lua native-event queue overflow; newest event was dropped.");
        }
        return 0;
    }
    index = (g_dluma_lua_runtime.event_head + g_dluma_lua_runtime.event_count) %
        DLUMA_LUA_RUNTIME_MAX_EVENTS;
    memset(&g_dluma_lua_runtime.events[index], 0, sizeof(g_dluma_lua_runtime.events[index]));
    g_dluma_lua_runtime.events[index].kind = kind;
    g_dluma_lua_runtime.events[index].map_generation = map_generation;
    g_dluma_lua_runtime.events[index].native_index_epoch = native_index_epoch;
    g_dluma_lua_runtime.events[index].native_subscription_key_revision =
        native_subscription_key_revision;
    g_dluma_lua_runtime.events[index].state = state;
    g_dluma_lua_runtime.events[index].number = number;
    g_dluma_lua_runtime.events[index].owner_count =
        dluma_native_gameplay_snapshot_event_owners(
            kind, map_generation, native_index_epoch,
            native_subscription_key_revision, exact_name,
            g_dluma_lua_runtime.events[index].owner_ids,
            DLUMA_LUA_RUNTIME_MAX_STATES);
    if (!g_dluma_lua_runtime.events[index].owner_count) {
        memset(&g_dluma_lua_runtime.events[index], 0,
               sizeof(g_dluma_lua_runtime.events[index]));
        dluma_lua_runtime_unlock(&g_dluma_lua_runtime.event_lock);
        return 0;
    }
    dluma_str_copy(g_dluma_lua_runtime.events[index].name,
                 DLUMA_ARRAY_COUNT(g_dluma_lua_runtime.events[index].name), exact_name);
    ++g_dluma_lua_runtime.event_count;
    dluma_lua_runtime_unlock(&g_dluma_lua_runtime.event_lock);
    return 1;
}

static int dluma_lua_runtime_config_value_valid(
    unsigned int type, const char* value)
{
    const char* cursor;
    ULONGLONG magnitude = 0;
    ULONGLONG limit;
    if (!value ||
        strlen(value) > DLUMA_LUA54_MAX_CONFIG_VALUE)
        return 0;
    if (type == DLUMA_LUA54_VALUE_STRING)
        return dluma_lua_config_utf8_valid(
            value, DLUMA_LUA54_MAX_CONFIG_VALUE,
            DLUMA_LUA54_MAX_CONFIG_VALUE, 1);
    if (type == DLUMA_LUA54_VALUE_BOOLEAN)
        return dluma_stricmp(value, "true") == 0 ||
               dluma_stricmp(value, "false") == 0;
    if (type != DLUMA_LUA54_VALUE_INTEGER)
        return 0;
    cursor = value;
    if (*cursor == '-') {
        limit = 2147483648ULL;
        ++cursor;
    } else {
        limit = 2147483647ULL;
        if (*cursor == '+') ++cursor;
    }
    if (!*cursor) return 0;
    while (*cursor) {
        unsigned int digit;
        if (*cursor < '0' || *cursor > '9')
            return 0;
        digit = (unsigned int)(*cursor - '0');
        if (magnitude > (limit - digit) / 10ULL)
            return 0;
        magnitude = magnitude * 10ULL + digit;
        ++cursor;
    }
    return 1;
}

static int dluma_lua_runtime_config_value_equal(
    unsigned int left_type, const char* left,
    unsigned int right_type, const char* right)
{
    unsigned int index = 0;
    if (left_type != right_type || !left || !right)
        return 0;
    while (left[index] && right[index]) {
        if (left[index] != right[index]) return 0;
        ++index;
    }
    return left[index] == right[index];
}

int dluma_lua_runtime_enqueue_config_changed(
    const char* registry_key,
    const char* metadata_fingerprint,
    const char* setting_key,
    const char* old_value,
    unsigned int old_value_type,
    const char* new_value,
    unsigned int new_value_type)
{
    unsigned int index;
    if (!dluma_lua_runtime_sha256_key_valid(registry_key) ||
        !dluma_lua_runtime_sha256_key_valid(
            metadata_fingerprint) ||
        !dluma_lua_config_identifier_valid(setting_key) ||
        !dluma_lua_runtime_config_value_valid(
            old_value_type, old_value) ||
        !dluma_lua_runtime_config_value_valid(
            new_value_type, new_value))
        return 0;
    dluma_lua_runtime_lock(
        &g_dluma_lua_runtime.config_event_lock);
    for (index = 0;
         index < g_dluma_lua_runtime.config_event_count;
         ++index) {
        DlumaLuaRuntimeConfigEvent* existing =
            &g_dluma_lua_runtime.config_events[index];
        if (dluma_stricmp(
                existing->registry_key,
                registry_key) != 0 ||
            dluma_stricmp(
                existing->metadata_fingerprint,
                metadata_fingerprint) != 0 ||
            !dluma_lua_runtime_debug_text_equals(
                existing->change.key, setting_key))
            continue;
        existing->change.value_type = new_value_type;
        dluma_str_copy(
            existing->change.value,
            DLUMA_ARRAY_COUNT(existing->change.value),
            new_value);
        if (dluma_lua_runtime_config_value_equal(
                existing->change.old_value_type,
                existing->change.old_value,
                existing->change.value_type,
                existing->change.value)) {
            unsigned int move;
            for (move = index + 1U;
                 move <
                    g_dluma_lua_runtime.config_event_count;
                 ++move)
                g_dluma_lua_runtime.config_events[move - 1U] =
                    g_dluma_lua_runtime.config_events[move];
            --g_dluma_lua_runtime.config_event_count;
            memset(
                &g_dluma_lua_runtime.config_events[
                    g_dluma_lua_runtime.config_event_count],
                0,
                sizeof(g_dluma_lua_runtime.config_events[0]));
        }
        dluma_lua_runtime_unlock(
            &g_dluma_lua_runtime.config_event_lock);
        return 1;
    }
    if (dluma_lua_runtime_config_value_equal(
            old_value_type, old_value,
            new_value_type, new_value)) {
        dluma_lua_runtime_unlock(
            &g_dluma_lua_runtime.config_event_lock);
        return 1;
    }
    if (g_dluma_lua_runtime.config_event_count >=
        DLUMA_LUA_RUNTIME_MAX_CONFIG_EVENTS) {
        dluma_lua_runtime_unlock(
            &g_dluma_lua_runtime.config_event_lock);
        dluma_log_line(
            "ERROR",
            "Lua config-change queue overflow; newest event was dropped.");
        return 0;
    }
    {
        DlumaLuaRuntimeConfigEvent* queued =
            &g_dluma_lua_runtime.config_events[
                g_dluma_lua_runtime.config_event_count++];
        memset(queued, 0, sizeof(*queued));
        dluma_str_copy(
            queued->registry_key,
            DLUMA_ARRAY_COUNT(queued->registry_key),
            registry_key);
        dluma_str_copy(
            queued->metadata_fingerprint,
            DLUMA_ARRAY_COUNT(queued->metadata_fingerprint),
            metadata_fingerprint);
        queued->change.struct_size =
            sizeof(queued->change);
        queued->change.old_value_type = old_value_type;
        queued->change.value_type = new_value_type;
        dluma_str_copy(
            queued->change.key,
            DLUMA_ARRAY_COUNT(queued->change.key),
            setting_key);
        dluma_str_copy(
            queued->change.old_value,
            DLUMA_ARRAY_COUNT(queued->change.old_value),
            old_value);
        dluma_str_copy(
            queued->change.value,
            DLUMA_ARRAY_COUNT(queued->change.value),
            new_value);
    }
    dluma_lua_runtime_unlock(
        &g_dluma_lua_runtime.config_event_lock);
    return 1;
}

static void dluma_lua_runtime_dispatch_config_events(void)
{
    for (;;) {
        DlumaLuaRuntimeConfigEvent queued;
        unsigned int move;
        unsigned int slot_index;
        int have_event = 0;
        memset(&queued, 0, sizeof(queued));
        dluma_lua_runtime_lock(
            &g_dluma_lua_runtime.config_event_lock);
        if (g_dluma_lua_runtime.config_event_count) {
            queued =
                g_dluma_lua_runtime.config_events[0];
            for (move = 1U;
                 move <
                    g_dluma_lua_runtime.config_event_count;
                 ++move)
                g_dluma_lua_runtime.config_events[move - 1U] =
                    g_dluma_lua_runtime.config_events[move];
            --g_dluma_lua_runtime.config_event_count;
            memset(
                &g_dluma_lua_runtime.config_events[
                    g_dluma_lua_runtime.config_event_count],
                0,
                sizeof(g_dluma_lua_runtime.config_events[0]));
            have_event = 1;
        }
        dluma_lua_runtime_unlock(
            &g_dluma_lua_runtime.config_event_lock);
        if (!have_event) break;
        for (slot_index = 0;
             slot_index < DLUMA_LUA_RUNTIME_MAX_STATES;
             ++slot_index) {
            DlumaLuaRuntimeSlot* slot =
                &g_dluma_lua_runtime.slots[slot_index];
            DlumaLua54Event event;
            if (!slot->occupied ||
                slot->teardown_pending ||
                slot->metadata.scope !=
                    DLUMA_LUA_SCOPE_GLOBAL ||
                !slot->metadata.config_available ||
                dluma_stricmp(
                    slot->metadata.registry_key,
                    queued.registry_key) != 0 ||
                dluma_stricmp(
                    slot->metadata.metadata_fingerprint,
                    queued.metadata_fingerprint) != 0)
                continue;
            memset(&event, 0, sizeof(event));
            event.kind =
                DLUMA_LUA54_EVENT_CONFIG_CHANGED;
            event.name = "on_config_changed";
            event.config_changed = queued.change;
            (void)dluma_lua_runtime_dispatch_helper_event(
                slot, &event);
            break;
        }
    }
}

static void dluma_lua_runtime_refresh_api_capabilities(void)
{
    unsigned int i;
    for (i = 0; i < DLUMA_LUA_RUNTIME_MAX_STATES; ++i) {
        DlumaLuaRuntimeSlot* slot = &g_dluma_lua_runtime.slots[i];
        if (!slot->occupied || slot->teardown_pending ||
            slot->api_owner.state != DLUMA_MOD_OWNER_ACTIVE)
            continue;
        /* World streaming and native adapters become ready after state
         * creation. Refresh both layers, preserving host-owned services. */
        dluma_game_backend_prepare_capabilities(&slot->api_capabilities);
        dluma_game_host_context_prepare_capabilities(
            &slot->api_host, &slot->api_capabilities);
    }
}

void dluma_lua_runtime_game_thread_pump(void)
{
    uint64_t now;
    double delta;
    unsigned int i;
    int lua_available;
    if (g_dluma.stop_requested || dluma_atomic_compare_exchange(&g_dluma_map_exit_active,0,0)) return;
    /* Editor playtests may migrate the serialized OnFrame route between
     * workers. Only that currently authoritative route can rebind owners;
     * arbitrary callers must never make themselves the game thread. */
    if (!dluma_engine_game_frame_dispatch_active_on_current_thread(&g_dluma.engine) ||
        dluma_engine_game_frame_authority_status(&g_dluma.engine, GetTickCount64()) !=
            DLUMA_GAME_FRAME_AUTHORITY_STATUS_AUTHORITATIVE)
        return;
    if (dluma_atomic_compare_exchange(&g_dluma_lua_runtime.pump_active, 1, 0) != 0)
        return;
    for (i = 0; i < DLUMA_LUA_RUNTIME_MAX_STATES; ++i) {
        DlumaLuaRuntimeSlot* slot = &g_dluma_lua_runtime.slots[i];
        if (slot->occupied && !slot->teardown_pending &&
            slot->api_owner.state == DLUMA_MOD_OWNER_ACTIVE)
            slot->api_owner.game_thread_id = GetCurrentThreadId();
    }
    /* Intent commands are always consumed on the game thread. If the private
     * helper is unavailable, the consent layer records a readable runtime
     * error instead of leaving an eternal ENABLING state. */
    dluma_lua_process_global_runtime_queue();
    if (dluma_startup_recovery_suppresses_mods()) {
        if (dluma_atomic_compare_exchange(
                &g_dluma_lua_runtime_recovery_suppression_logged, 1, 0) == 0)
            dluma_log_line("WARN", "Lua MOD execution suppressed for startup recovery until process exit.");
        for (i = 0; i < DLUMA_LUA_RUNTIME_MAX_STATES; ++i) {
            DlumaLuaRuntimeSlot* slot = &g_dluma_lua_runtime.slots[i];
            if (slot->occupied)
                dluma_lua_runtime_destroy_slot(slot, 0, 0);
        }
        dluma_lua_runtime_publish_crash_context(
            GetTickCount64(), 1);
        dluma_atomic_exchange(&g_dluma_lua_runtime.pump_active, 0);
        return;
    }
    dluma_animation_refresh_runtime_player_game_thread_pump();
    lua_available =
        g_dluma_lua_runtime.available &&
        !g_dluma_lua_runtime.quiescing;
    now = GetTickCount64();
    if (g_dluma_lua_runtime.last_pump_ms && now == g_dluma_lua_runtime.last_pump_ms) {
        dluma_atomic_exchange(&g_dluma_lua_runtime.pump_active, 0);
        return;
    }
    delta = g_dluma_lua_runtime.last_pump_ms
        ? (double)(now - g_dluma_lua_runtime.last_pump_ms) / 1000.0 : 0.0;
    if (delta > 0.25) delta = 0.25;
    g_dluma_lua_runtime.last_pump_ms = now;
    if (lua_available) {
        for (i = 0; i < DLUMA_LUA_RUNTIME_MAX_STATES; ++i) {
            DlumaLuaRuntimeSlot* slot = &g_dluma_lua_runtime.slots[i];
            if (slot->occupied && slot->helper_script)
                (void)g_dluma_lua_runtime.api.begin_pump(
                    slot->helper_script);
        }
    }
    /* Both calls are on the established game-thread pump. The first permits a
     * late-loaded module pair to bind before state creation; the second sees
     * any map-generation change committed by take_pending. */
    dluma_native_gameplay_game_thread_tick();
    dluma_native_mods_prepare_game_frame();
    dluma_managed_host_prepare_game_frame();
    dluma_lua_runtime_take_pending();
    dluma_native_gameplay_game_thread_tick();
    dluma_lua_runtime_refresh_api_capabilities();
    dluma_native_mods_prepare_game_frame();
    dluma_managed_host_prepare_game_frame();
    dluma_quest_phase_player_game_thread_pump();
    /* Native observations enqueue immutable targets for all hosts. Common
     * dispatch preserves native -> C# -> Lua order within the frame. */
    dluma_native_mods_game_thread_pump(delta);
    dluma_managed_host_game_thread_pump(delta);
    if (!lua_available) {
        dluma_lua_runtime_publish_crash_context(now, 0);
        dluma_atomic_exchange(&g_dluma_lua_runtime.pump_active, 0);
        return;
    }
    dluma_lua_runtime_dispatch_config_events();
    dluma_lua_runtime_dispatch_queued_events();
    for (i = 0; i < DLUMA_LUA_RUNTIME_MAX_STATES; ++i) {
        DlumaLuaRuntimeSlot* slot = &g_dluma_lua_runtime.slots[i];
        if (slot->occupied)
            dluma_lua_runtime_dispatch_slot(slot, DLUMA_LUA54_EVENT_UPDATE,
                                          "on_update", delta, 0);
    }
    dluma_lua_runtime_publish_crash_context(now, 0);
    dluma_atomic_exchange(&g_dluma_lua_runtime.pump_active, 0);
}

void dluma_lua_runtime_stop(void)
{
    unsigned int i;
    unsigned int remaining = 0;
    uint64_t wait_started;
    dluma_atomic_exchange(&g_dluma_lua_runtime_stop_ok, 0);
    if (!g_dluma_lua_runtime.helper_module) {
        dluma_lua_runtime_publish_crash_context(
            GetTickCount64(), 1);
        dluma_atomic_exchange(&g_dluma_lua_runtime_stop_ok, 1);
        return;
    }
    dluma_atomic_exchange(&g_dluma_lua_runtime.quiescing, 1);
    wait_started = GetTickCount64();
    while (dluma_atomic_compare_exchange(
               &g_dluma_lua_runtime.pump_active, 0, 0) != 0 &&
           GetTickCount64() - wait_started < 2000) {
        Sleep(1);
    }
    if (dluma_atomic_compare_exchange(
            &g_dluma_lua_runtime.pump_active, 0, 0) != 0) {
        /* Unloading the helper while a protected callback is still executing
         * is worse than retaining it until process teardown. */
        dluma_log_line("WARN", "Timed out draining the Lua game-thread pump; private helper retained for shutdown safety.");
        dluma_lua_runtime_publish_crash_context(
            GetTickCount64(), 1);
        return;
    }
    for (i = 0; i < DLUMA_LUA_RUNTIME_MAX_STATES; ++i) {
        DlumaLuaRuntimeSlot* slot = &g_dluma_lua_runtime.slots[i];
        if (slot->occupied) {
            if (slot->helper_script &&
                g_dluma_lua_runtime.api.begin_pump)
                (void)g_dluma_lua_runtime.api.begin_pump(
                    slot->helper_script);
            dluma_lua_runtime_destroy_slot(slot,
                g_dluma_lua_runtime.current_map[0] != 0, 1);
        }
        if (slot->occupied) ++remaining;
    }
    if (remaining) {
        dluma_log_u64("WARN", "lua_states_retained_after_destroy_drain", remaining);
        dluma_log_line("WARN", "Private Lua helper retained because an owned state still reports an active callback.");
        dluma_lua_runtime_publish_crash_context(
            GetTickCount64(), 1);
        return;
    }
    dluma_native_gameplay_stop();
    if (!dluma_native_gameplay_stop_succeeded()) {
        dluma_log_line("WARN", "Private Lua helper retained because the native adapter did not fully detach.");
        dluma_lua_runtime_publish_crash_context(
            GetTickCount64(), 1);
        return;
    }
    FreeLibrary(g_dluma_lua_runtime.helper_module);
    memset(&g_dluma_lua_runtime, 0, sizeof(g_dluma_lua_runtime));
    dluma_lua_runtime_publish_crash_context(
        GetTickCount64(), 1);
    dluma_atomic_exchange(&g_dluma_lua_runtime_stop_ok, 1);
    dluma_log_line("INFO", "Private Lua runtime stopped after state teardown.");
}

int dluma_lua_runtime_stop_succeeded(void)
{
    return dluma_atomic_compare_exchange(
        &g_dluma_lua_runtime_stop_ok, 0, 0) != 0;
}

int dluma_lua_runtime_copy_crash_context(
    char* out, unsigned int capacity)
{
    unsigned int attempt;
    if (!out || !capacity) return 0;
    out[0] = 0;
    for (attempt = 0; attempt < 2U; ++attempt) {
        LONG before = g_dluma_lua_crash_context_sequence;
        LONG after;
        if (before & 1) continue;
        dluma_str_copy(out, capacity, g_dluma_lua_crash_context);
        after = g_dluma_lua_crash_context_sequence;
        if (before == after && !(after & 1))
            return out[0] != 0;
        out[0] = 0;
    }
    return 0;
}
