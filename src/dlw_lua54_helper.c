#define DLUMA_LUA54_HELPER_BUILD 1

#include "dlw_lua54_api.h"
#include "../sdk/c/dluma_api.h"

#include <math.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <time.h>
#endif

#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"

#include "dlw_lua54_player_bridge.h"
#include "dlw_lua54_stdlib.h"

#define DLUMA_HELPER_MAX_CALLBACKS 96u
#define DLUMA_HELPER_MAX_TIMERS 64u
#define DLUMA_HELPER_MAX_RULES 32u
#define DLUMA_HELPER_TEXT 2048u
#define DLUMA_HELPER_HOOK_GRANULARITY 1000
#define DLUMA_HELPER_DISABLE_AFTER_VIOLATIONS 3u
#define DLUMA_HELPER_PUMP_BUDGET_MULTIPLIER 4u
#define DLUMA_HELPER_OBJECT_METATABLE "DLUMA.Object.v2"
#define DLUMA_HELPER_QUEST_HUD_OVERRIDE_METATABLE \
    "DLUMA.QuestHudOverride.v1"
#define DLUMA_HELPER_EVENT_QUEST_CHANGED 0xD100u

typedef struct DlumaHelperCallback {
    int ref;
    uint32_t token;
    uint32_t kind;
    int active;
    int native_registered;
    /* SensorArea's native edge sampler deliberately establishes a baseline
     * without emitting an enter.  A newly registered enter listener gets one
     * game-thread CheckInside attempt sequence so an already-occupying player
     * is not silently lost to that baseline. */
    int initial_occupancy_pending;
    char key[257];
} DlumaHelperCallback;

typedef struct DlumaHelperObject {
    struct DlumaHelperScript* script;
    DlumaLua54Object object;
} DlumaHelperObject;

typedef struct DlumaHelperQuestHudOverride {
    struct DlumaHelperScript* script;
    uint64_t owner_id;
    uint64_t override_id;
    uint32_t map_generation;
    uint32_t active;
    char target_id[DLUMA_LUA54_MAX_QUEST_HUD_TARGET_ID];
} DlumaHelperQuestHudOverride;

typedef struct DlumaHelperTimer {
    int ref;
    uint32_t id;
    int active;
    int repeating;
    int is_rule;
    double period_seconds;
    uint64_t due_ms;
} DlumaHelperTimer;

#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable:4324)
#endif
typedef struct DlumaHelperScript {
    lua_State* state;
    DlumaLua54HostCallbacks host;
    void* host_user;
    uint32_t scope;
    uint32_t map_generation;
    uint32_t approved_permissions;
    uint32_t capabilities;
    uint64_t owner_id;
    DlumaLua54PlayerBridgeContext player_bridge;
    uint64_t source_fingerprint;
    uint64_t memory_limit;
    uint64_t memory_used;
    uint64_t instruction_limit;
    uint64_t budget_remaining;
    uint64_t budget_instruction_charged;
    uint64_t pump_instruction_limit;
    uint64_t pump_instruction_remaining;
    uint64_t pump_lua_us_limit;
    uint64_t pump_lua_us_consumed;
    uint64_t budget_callback_start_ticks;
    uint64_t budget_host_pause_start_ticks;
    uint64_t budget_host_excluded_ticks;
    uint64_t budget_qpc_frequency;
    uint64_t last_callback_lua_us;
    uint64_t last_callback_host_excluded_us;
    uint64_t last_instruction_count_or_remaining;
    uint32_t wall_clock_limit_ms;
    uint32_t budget_host_pause_depth;
    uint32_t max_callbacks;
    uint32_t max_timers;
    uint32_t max_rules;
    uint32_t max_log_messages_per_second;
    uint32_t next_timer_id;
    uint32_t callback_count;
    uint32_t timer_count;
    uint32_t rule_count;
    uint32_t active_callbacks;
    uint32_t budget_violations;
    uint32_t log_messages_in_window;
    uint32_t gate_warning_capabilities;
    uint32_t gate_warning_permissions;
    uint64_t log_window_started_ms;
    int alive;
    int disabled;
    int declaration_seen;
    int has_update_callback;
    int destroy_pending;
    int budget_active;
    int pump_budget_active;
    int pump_budget_exhausted;
    int panic_guard_active;
    int panic_raised;
    int stdlib_install_mod_ref;
    int module_cache_ref;
    jmp_buf panic_jump;
    char id[128];
    char name[128];
    char version[64];
    char permissions[2048];
    char file_path[1024];
    char project[256];
    char map[128];
    char workshop_id[64];
    char loader_version[64];
    char last_event[DLUMA_LUA54_MAX_EVENT];
    char budget_type[32];
    char last_error[DLUMA_LUA54_MAX_ERROR];
    int is_editor;
    int is_player;
    DlumaHelperCallback callbacks[DLUMA_HELPER_MAX_CALLBACKS];
    DlumaHelperTimer timers[DLUMA_HELPER_MAX_TIMERS];
} DlumaHelperScript;
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

#if defined(_WIN32)
static volatile LONG g_dluma_helper_next_callback_token;
#else
static uint32_t g_dluma_helper_next_callback_token;
#endif

static void dluma_helper_set_function(
    lua_State* state, int table_index, const char* name,
    DlumaHelperScript* script, lua_CFunction function);
static DlumaHelperScript* dluma_helper_from_upvalue(lua_State* state, int index);
static int dluma_helper_cancel_timer(DlumaHelperScript* script, uint32_t id);

/* Retired 0.2.1 Lua-owned quest graph.  It is deliberately not compiled or
 * installed into dluma: custom state/objectives and the loader Quest Log were
 * removed in favour of authored engine quests.  Keep this source only as a
 * migration reference while older scripts receive a structured deprecation
 * error from dluma.quest.define. */
#if 0
/* Quest state deliberately lives in the owned Lua state instead of exposing
 * native Quest/Phase objects.  The compact scalar persistence format is
 * private to this helper and is reset if a script changes its definition
 * version or an interrupted/older record cannot be validated. */
static const char g_quest_source[] =
    "local storage = dluma.storage\n"
    "local notify = __dluma_quest_notify\n"
    "local native_mutate = __dluma_quest_mutate\n"
    "local scope_check = __dluma_quest_scope_check\n"
    "local MAX_QUESTS, MAX_PHASES, MAX_OBJECTIVES, MAX_STATE_BYTES = 16, 64, 32, 8192\n"
    "local MAX_QUEST_NOTIFICATIONS = 64\n"
    "local MAX_REVISION = 9007199254740991\n"
    "local ACTIVE_KEY = 'dlw.quest.active'\n"
    "local registry, active_id, active_loaded, quest_count = {}, nil, false, 0\n"
    "local reporting, report_queue, report_head, report_tail = false, {}, 1, 0\n"
    "local function err(code, message, object_name)\n"
    "  return nil, { code = code, message = message, capability = 'lua.quest', permission = 'storage.mod', object_name = object_name, retryable = false }\n"
    "end\n"
    "local function native_err(code, message, object_name)\n"
    "  return nil, { code = code, message = message, capability = 'quest.native', permission = 'quest.write', object_name = object_name, retryable = false }\n"
    "end\n"
    "local function valid_id(value, maximum)\n"
    "  return type(value) == 'string' and #value > 0 and #value <= maximum and value:match('^[A-Za-z0-9_.%-]+$') ~= nil\n"
    "end\n"
    "local function same_id(left, right)\n"
    "  return type(left) == 'string' and type(right) == 'string' and #left == #right and string.lower(left) == string.lower(right)\n"
    "end\n"
    "local function valid_text(value, maximum)\n"
    "  return type(value) == 'string' and #value > 0 and #value <= maximum and not value:find('\\0', 1, true) and utf8.len(value) ~= nil\n"
    "end\n"
    "local function valid_objective_state(value)\n"
    "  return value == 'active' or value == 'completed' or value == 'hidden' or value == 'failed'\n"
    "end\n"
    "local function normalize_objective_state(value)\n"
    "  if value == 'complete' then return 'completed' end\n"
    "  return value or 'active'\n"
    "end\n"
    "local function normalize_version(value)\n"
    "  if type(value) == 'number' then\n"
    "    if value < 1 or value ~= math.floor(value) or value > MAX_REVISION then return nil end\n"
    "    return tostring(value)\n"
    "  end\n"
    "  if valid_id(value, 63) then return value end\n"
    "  return nil\n"
    "end\n"
    "local function escape(value)\n"
    "  return (value:gsub('([^A-Za-z0-9_.%-])', function(byte) return string.format('%%%02X', string.byte(byte)) end))\n"
    "end\n"
    "local function unescape(value)\n"
    "  local index = 1\n"
    "  while index <= #value do\n"
    "    if value:sub(index, index) == '%' then\n"
    "      local hex = value:sub(index + 1, index + 2)\n"
    "      if #hex ~= 2 or not hex:match('^%x%x$') then return nil end\n"
    "      index = index + 3\n"
    "    else index = index + 1 end\n"
    "  end\n"
    "  return (value:gsub('%%(%x%x)', function(hex) return string.char(tonumber(hex, 16)) end))\n"
    "end\n"
    "local function parse_objectives(raw, quest_id, phase_id)\n"
    "  local result, seen = {}, {}\n"
    "  if raw == nil then return result end\n"
    "  if type(raw) ~= 'table' or #raw > MAX_OBJECTIVES then return err('invalid_objectives', 'phase objectives must be an array of at most 32 entries', quest_id) end\n"
    "  for index = 1, #raw do\n"
    "    local item = raw[index]\n"
    "    if type(item) ~= 'table' or not valid_id(item.id, 64) or seen[item.id] or not valid_text(item.text, 512) then\n"
    "      return err('invalid_objective', 'every objective needs a unique bounded id and UTF-8 text', phase_id)\n"
    "    end\n"
    "    local state = normalize_objective_state(item.state)\n"
    "    if not valid_objective_state(state) then return err('invalid_objective_state', 'objective state is invalid', item.id) end\n"
    "    seen[item.id] = true\n"
    "    result[#result + 1] = { id = item.id, text = item.text, state = state }\n"
    "  end\n"
    "  return result\n"
    "end\n"
    "local function parse_definition(raw)\n"
    "  if type(raw) ~= 'table' or not valid_id(raw.id, 48) then return err('invalid_quest', 'quest id must be 1..48 letters, digits, dot, underscore, or hyphen') end\n"
    "  local version = normalize_version(raw.version)\n"
    "  if not version then return err('invalid_quest_version', 'quest version must be a positive integer or bounded identifier', raw.id) end\n"
    "  local title = raw.title or raw.id\n"
    "  if not valid_text(title, 159) then return err('invalid_quest_title', 'quest title must be 1..159 UTF-8 bytes', raw.id) end\n"
    "  if type(raw.phases) ~= 'table' then return err('invalid_phases', 'quest phases are required', raw.id) end\n"
    "  local quest = { id = raw.id, version = version, title = title, phase_order = {}, phases = {}, native = nil, native_state = 'none' }\n"
    "  for key, phase in pairs(raw.phases) do\n"
    "    local phase_id\n"
    "    if type(key) == 'string' then phase_id = type(phase) == 'table' and (phase.id or key) or nil\n"
    "    elseif type(key) == 'number' then phase_id = type(phase) == 'table' and phase.id or nil\n"
    "    end\n"
    "    if type(phase) ~= 'table' or not valid_id(phase_id, 64) or quest.phases[phase_id] then return err('invalid_phase', 'every phase needs a unique bounded id', raw.id) end\n"
    "    if #quest.phase_order >= MAX_PHASES then return err('phase_limit', 'a quest may define at most 64 phases', raw.id) end\n"
    "    local phase_title = phase.title or phase_id\n"
    "    if not valid_text(phase_title, 159) then return err('invalid_phase_title', 'phase title must be 1..159 UTF-8 bytes', phase_id) end\n"
    "    if phase.next ~= nil and not valid_id(phase.next, 64) then return err('invalid_next_phase', 'next must name a bounded phase id', phase_id) end\n"
    "    if phase.native_phase ~= nil and not valid_id(phase.native_phase, 96) then return err('invalid_native_phase', 'native_phase must be a bounded identifier', phase_id) end\n"
    "    local objectives, objective_error = parse_objectives(phase.objectives, raw.id, phase_id)\n"
    "    if not objectives then return nil, objective_error end\n"
    "    quest.phase_order[#quest.phase_order + 1] = phase_id\n"
    "    quest.phases[phase_id] = { id = phase_id, title = phase_title, next = phase.next, native_phase = phase.native_phase, objectives = objectives }\n"
    "  end\n"
    "  if #quest.phase_order == 0 then return err('invalid_phases', 'quest needs at least one phase', raw.id) end\n"
    "  local incoming = {}\n"
    "  for _, phase_id in ipairs(quest.phase_order) do\n"
    "    local next_id = quest.phases[phase_id].next\n"
    "    if next_id and not quest.phases[next_id] then return err('unknown_next_phase', 'next names a phase that is not defined', next_id) end\n"
    "    if next_id then incoming[next_id] = true end\n"
    "  end\n"
    "  if raw.initial ~= nil then\n"
    "    if not valid_id(raw.initial, 64) or not quest.phases[raw.initial] then return err('invalid_initial_phase', 'initial must name a defined phase', raw.id) end\n"
    "    quest.initial = raw.initial\n"
    "  else\n"
    "    local roots = {}\n"
    "    for _, phase_id in ipairs(quest.phase_order) do if not incoming[phase_id] then roots[#roots + 1] = phase_id end end\n"
    "    if #roots ~= 1 then return err('initial_phase_required', 'set initial when phases do not have one unique root', raw.id) end\n"
    "    quest.initial = roots[1]\n"
    "  end\n"
    "  if raw.native ~= nil then\n"
    "    if type(raw.native) ~= 'table' or not valid_id(raw.native.quest, 96) then return err('invalid_native_binding', 'native.quest must be a bounded identifier', raw.id) end\n"
    "    local native_phase_ids = {}\n"
    "    for _, phase_id in ipairs(quest.phase_order) do\n"
    "      local native_phase = quest.phases[phase_id].native_phase\n"
    "      if not native_phase then return err('native_phase_required', 'every phase of a native-bound quest needs a mapped native_phase', phase_id) end\n"
    "      if native_phase_ids[native_phase] then return err('duplicate_native_phase', 'a native phase may be mapped by only one Lua phase', native_phase) end\n"
    "      native_phase_ids[native_phase] = true\n"
    "    end\n"
    "    quest.native = { quest = raw.native.quest }\n"
    "    quest.native_state = 'unavailable'\n"
    "  end\n"
    "  return quest\n"
    "end\n"
    "local function make_objectives(quest)\n"
    "  local result = {}\n"
    "  for _, phase_id in ipairs(quest.phase_order) do\n"
    "    local items, phase = {}, quest.phases[phase_id]\n"
    "    for index, source in ipairs(phase.objectives) do\n"
    "      items[index] = { id = source.id, text = source.text, state = source.state, base_text = source.text, base_state = source.state, added = false }\n"
    "    end\n"
    "    result[phase_id] = items\n"
    "  end\n"
    "  return result\n"
    "end\n"
    "local function initial_state(quest) return { status = 'declared', phase_id = quest.initial, revision = 0, objectives = make_objectives(quest) } end\n"
    "local function copy_state(source)\n"
    "  local result = { status = source.status, phase_id = source.phase_id, revision = source.revision, objectives = {} }\n"
    "  for phase_id, items in pairs(source.objectives) do\n"
    "    local copied = {}\n"
    "    for index, item in ipairs(items) do copied[index] = { id = item.id, text = item.text, state = item.state, base_text = item.base_text, base_state = item.base_state, added = item.added } end\n"
    "    result.objectives[phase_id] = copied\n"
    "  end\n"
    "  return result\n"
    "end\n"
    "local function find_objective(state, phase_id, objective_id)\n"
    "  local items = state.objectives[phase_id] or {}\n"
    "  for _, item in ipairs(items) do if item.id == objective_id then return item, items end end\n"
    "  return nil, items\n"
    "end\n"
    "local function encode_state(quest, state)\n"
    "  local changes = {}\n"
    "  for _, phase_id in ipairs(quest.phase_order) do\n"
    "    for _, item in ipairs(state.objectives[phase_id]) do\n"
    "      if item.added or item.state ~= item.base_state or item.text ~= item.base_text then\n"
    "        changes[#changes + 1] = table.concat({ escape(phase_id), escape(item.id), escape(item.state), escape(item.text), item.added and '1' or '0' }, ',')\n"
    "      end\n"
    "    end\n"
    "  end\n"
    "  local encoded = table.concat({ 'Q1', escape(quest.version), state.status, escape(state.phase_id), tostring(state.revision), table.concat(changes, ';') }, '|')\n"
    "  if #encoded > MAX_STATE_BYTES then return err('quest_state_too_large', 'dynamic quest state exceeds the 8 KiB persistence limit', quest.id) end\n"
    "  return encoded\n"
    "end\n"
    "local function decode_state(quest, encoded)\n"
    "  if type(encoded) ~= 'string' or #encoded > MAX_STATE_BYTES then return nil end\n"
    "  local marker, raw_version, status, raw_phase_id, raw_revision, changes = encoded:match('^([^|]*)|([^|]*)|([^|]*)|([^|]*)|([^|]*)|(.*)$')\n"
    "  local version, phase_id = unescape(raw_version or ''), unescape(raw_phase_id or '')\n"
    "  if type(raw_revision) ~= 'string' or (raw_revision ~= '0' and not raw_revision:match('^[1-9][0-9]*$')) or #raw_revision > 16 then return nil end\n"
    "  local revision = tonumber(raw_revision)\n"
    "  if marker ~= 'Q1' or not version or not phase_id or escape(version) ~= raw_version or escape(phase_id) ~= raw_phase_id or version ~= quest.version or (status ~= 'declared' and status ~= 'active' and status ~= 'completed') or not quest.phases[phase_id] or not revision or revision > MAX_REVISION then return nil end\n"
    "  local state = initial_state(quest)\n"
    "  state.status, state.phase_id, state.revision = status, phase_id, revision\n"
    "  if changes ~= '' then\n"
    "    local cursor, seen = 1, {}\n"
    "    while cursor <= #changes do\n"
    "      local separator = changes:find(';', cursor, true)\n"
    "      local record = separator and changes:sub(cursor, separator - 1) or changes:sub(cursor)\n"
    "      if record == '' then return nil end\n"
    "      local raw_phase, raw_id, raw_state, raw_text, added = record:match('^([^,]+),([^,]+),([^,]+),([^,]+),([01])$')\n"
    "      local phase, id, objective_state, text = unescape(raw_phase or ''), unescape(raw_id or ''), unescape(raw_state or ''), unescape(raw_text or '')\n"
    "      local change_key\n"
    "      if not phase or not id or not objective_state or not text or escape(phase) ~= raw_phase or escape(id) ~= raw_id or escape(objective_state) ~= raw_state or escape(text) ~= raw_text or not quest.phases[phase] or not valid_id(id, 64) or not valid_objective_state(objective_state) or not valid_text(text, 512) then return nil end\n"
    "      change_key = phase .. '\0' .. id\n"
    "      if seen[change_key] then return nil end\n"
    "      seen[change_key] = true\n"
    "      local existing, items = find_objective(state, phase, id)\n"
    "      if added == '1' then\n"
    "        if existing or #items >= MAX_OBJECTIVES then return nil end\n"
    "        items[#items + 1] = { id = id, text = text, state = objective_state, base_text = '', base_state = 'active', added = true }\n"
    "      else\n"
    "        if not existing or existing.added then return nil end\n"
    "        if existing.text == text and existing.state == objective_state then return nil end\n"
    "        existing.text, existing.state = text, objective_state\n"
    "      end\n"
    "      if not separator then break end\n"
    "      cursor = separator + 1\n"
    "      if cursor > #changes then return nil end\n"
    "    end\n"
    "  end\n"
    "  return state\n"
    "end\n"
    /* Quest IDs are intentionally case-insensitive at the Lua boundary.  Keep
     * the backing key canonical too, so a cosmetic casing change in a script
     * cannot strand otherwise compatible saved state. */
    "local function storage_key(quest) return 'dlw.quest.' .. string.lower(quest.id) end\n"
    "local function persist(quest, state)\n"
    "  local encoded, encode_error = encode_state(quest, state)\n"
    "  if not encoded then return nil, encode_error end\n"
    "  if not storage.set(storage_key(quest), encoded) then return err('quest_storage_failed', 'quest state could not be persisted', quest.id) end\n"
    "  return true\n"
    "end\n"
    "local function load_active()\n"
    "  if active_loaded then return true end\n"
    "  local missing = {}\n"
    "  local value, storage_error = storage.get(ACTIVE_KEY, missing)\n"
    "  if value == nil then return nil, storage_error end\n"
    "  if value ~= missing and type(value) == 'string' and valid_id(value, 48) then active_id = value end\n"
    "  active_loaded = true\n"
    "  return true\n"
    "end\n"
    "local function load_state(quest)\n"
    "  local missing = {}\n"
    "  local value, storage_error = storage.get(storage_key(quest), missing)\n"
    "  if value == nil then return nil, storage_error end\n"
    "  local state = value ~= missing and decode_state(quest, value) or nil\n"
    "  if not state then\n"
    "    state = initial_state(quest)\n"
    "    local saved, save_error = persist(quest, state)\n"
    "    if not saved then return nil, save_error end\n"
    "  end\n"
    "  return state\n"
    "end\n"
    "local function find_quest(id)\n"
    "  if type(id) ~= 'string' then return nil end\n"
    "  for candidate_id, candidate in pairs(registry) do\n"
    "    if same_id(candidate_id, id) then return candidate end\n"
    "  end\n"
    "  return nil\n"
    "end\n"
    "local function snapshot(quest)\n"
    "  local state, phase = quest.state, quest.phases[quest.state.phase_id]\n"
    "  local objectives = {}\n"
    "  for index, item in ipairs(state.objectives[state.phase_id] or {}) do objectives[index] = { id = item.id, text = item.text, state = item.state } end\n"
    "  local result = { id = quest.id, title = quest.title, version = quest.version, status = state.status, active = same_id(active_id, quest.id), revision = state.revision, phase_id = phase.id, phase = { id = phase.id, title = phase.title, native_phase = phase.native_phase }, objectives = objectives }\n"
    "  if quest.native then result.native, result.native_state = { quest = quest.native.quest }, quest.native_state else result.native_state = 'none' end\n"
    "  return result\n"
    "end\n"
    "local function report(quest, action, host_only)\n"
    "  local event = { action = action, quest = snapshot(quest), host_only = host_only == true }\n"
    "  if reporting then\n"
    "    if report_tail - report_head + 1 >= MAX_QUEST_NOTIFICATIONS then error('quest notification queue limit reached') end\n"
    "    report_tail = report_tail + 1\n"
    "    report_queue[report_tail] = event\n"
    "    return\n"
    "  end\n"
    "  reporting = true\n"
    "  while event do\n"
    "    notify(event)\n"
    "    if report_head <= report_tail then\n"
    "      event = report_queue[report_head]\n"
    "      report_queue[report_head] = nil\n"
    "      report_head = report_head + 1\n"
    "    else event = nil end\n"
    "  end\n"
    "  report_head, report_tail, reporting = 1, 0, false\n"
    "end\n"
    "local function sync_native(quest, operation, phase_id)\n"
    "  if not quest.native then return true end\n"
    "  local phase = quest.phases[phase_id]\n"
    "  if not phase or not phase.native_phase then return native_err('native_phase_unmapped', 'native-bound quest operation needs a mapped native_phase', quest.id) end\n"
    "  local changed, result_or_error = native_mutate(operation, quest.native.quest, phase.native_phase)\n"
    "  if not changed then return nil, result_or_error end\n"
    "  if type(result_or_error) ~= 'table' or result_or_error.native_state ~= 'mirrored' then return native_err('native_transition_not_mirrored', 'native quest transition was not confirmed as synchronized', quest.id) end\n"
    "  quest.native_state = 'mirrored'\n"
    "  return true\n"
    "end\n"
    "local function transact(quest, action, update)\n"
    "  local candidate = copy_state(quest.state)\n"
    "  local updated, update_error = update(candidate)\n"
    "  if not updated then return nil, update_error end\n"
    "  if candidate.revision >= MAX_REVISION then return err('quest_revision_limit', 'quest revision reached its safe persistence limit', quest.id) end\n"
    "  candidate.revision = candidate.revision + 1\n"
    "  local saved, save_error = persist(quest, candidate)\n"
    "  if not saved then return nil, save_error end\n"
    "  quest.state = candidate\n"
    "  report(quest, action)\n"
    "  return true\n"
    "end\n"
    "local function start(quest)\n"
    "  if quest.state.status == 'active' then return true end\n"
    "  if quest.state.status == 'completed' then return err('quest_completed', 'a completed quest cannot be started again; use jump to explicitly select a phase', quest.id) end\n"
    "  local native_ok, native_error = sync_native(quest, 1, quest.initial)\n"
    "  if not native_ok then return nil, native_error end\n"
    "  return transact(quest, 'started', function(state)\n"
    "    state.status, state.phase_id = 'active', quest.initial\n"
    "    return true\n"
    "  end)\n"
    "end\n"
    "local function complete_phase(quest)\n"
    "  if quest.state.status ~= 'active' then return err('quest_not_active', 'start the quest before completing a phase', quest.id) end\n"
    "  local native_ok, native_error = sync_native(quest, 2, quest.state.phase_id)\n"
    "  if not native_ok then return nil, native_error end\n"
    "  return transact(quest, 'phase_completed', function(state)\n"
    "    for _, item in ipairs(state.objectives[state.phase_id] or {}) do if item.state == 'active' then item.state = 'completed' end end\n"
    "    local next_id = quest.phases[state.phase_id].next\n"
    "    if next_id then state.phase_id = next_id else state.status = 'completed' end\n"
    "    return true\n"
    "  end)\n"
    "end\n"
    "local function jump(quest, phase_id)\n"
    "  if not valid_id(phase_id, 64) or not quest.phases[phase_id] then return err('unknown_phase', 'phase is not declared by this quest', quest.id) end\n"
    "  local native_ok, native_error = sync_native(quest, 3, phase_id)\n"
    "  if not native_ok then return nil, native_error end\n"
    "  return transact(quest, 'jumped', function(state) state.status, state.phase_id = 'active', phase_id; return true end)\n"
    "end\n"
    "local function complete(quest)\n"
    "  if quest.state.status == 'completed' then return true end\n"
    "  local native_ok, native_error = sync_native(quest, 4, quest.state.phase_id)\n"
    "  if not native_ok then return nil, native_error end\n"
    "  return transact(quest, 'completed', function(state)\n"
    "    for _, item in ipairs(state.objectives[state.phase_id] or {}) do if item.state == 'active' then item.state = 'completed' end end\n"
    "    state.status = 'completed'\n"
    "    return true\n"
    "  end)\n"
    "end\n"
    "local function set_objective(quest, objective_id, change)\n"
    "  if quest.native then return err('native_objectives_read_only', 'native-bound quest objectives are authored and localized outside Lua', quest.id) end\n"
    "  if quest.state.status == 'completed' then return err('quest_completed', 'jump to a declared phase before changing objectives on a completed quest', quest.id) end\n"
    "  if not valid_id(objective_id, 64) or type(change) ~= 'table' then return err('invalid_objective_change', 'set_objective needs an id and a change table', quest.id) end\n"
    "  return transact(quest, 'objective_changed', function(state)\n"
    "    local item = find_objective(state, state.phase_id, objective_id)\n"
    "    if not item then return err('unknown_objective', 'objective is not in the current phase', objective_id) end\n"
    "    if change.text ~= nil and not valid_text(change.text, 512) then return err('invalid_objective_text', 'objective text must be 1..512 UTF-8 bytes', objective_id) end\n"
    "    local next_state = change.state ~= nil and normalize_objective_state(change.state) or item.state\n"
    "    if not valid_objective_state(next_state) then return err('invalid_objective_state', 'objective state is invalid', objective_id) end\n"
    "    item.text, item.state = change.text or item.text, next_state\n"
    "    return true\n"
    "  end)\n"
    "end\n"
    "local function add_objective(quest, definition)\n"
    "  if quest.native then return err('native_objectives_read_only', 'native-bound quest objectives are authored and localized outside Lua', quest.id) end\n"
    "  if quest.state.status == 'completed' then return err('quest_completed', 'jump to a declared phase before adding objectives to a completed quest', quest.id) end\n"
    "  if type(definition) ~= 'table' or not valid_id(definition.id, 64) or not valid_text(definition.text, 512) then return err('invalid_objective', 'add_objective needs a bounded id and UTF-8 text', quest.id) end\n"
    "  local objective_state = normalize_objective_state(definition.state)\n"
    "  if not valid_objective_state(objective_state) then return err('invalid_objective_state', 'objective state is invalid', definition.id) end\n"
    "  return transact(quest, 'objective_added', function(state)\n"
    "    local existing, items = find_objective(state, state.phase_id, definition.id)\n"
    "    if existing then return err('duplicate_objective', 'objective id already exists in the current phase', definition.id) end\n"
    "    if #items >= MAX_OBJECTIVES then return err('objective_limit', 'a phase may have at most 32 objectives', quest.id) end\n"
    "    items[#items + 1] = { id = definition.id, text = definition.text, state = objective_state, base_text = '', base_state = 'active', added = true }\n"
    "    return true\n"
    "  end)\n"
    "end\n"
    "local function handle_for(quest)\n"
    "  local handle = { id = quest.id }\n"
    "  handle.start = function() return start(quest) end\n"
    "  handle.advance = function() return complete_phase(quest) end\n"
    "  handle.complete_phase = function() return complete_phase(quest) end\n"
    "  handle.jump = function(first, second) return jump(quest, second or first) end\n"
    "  handle.complete = function() return complete(quest) end\n"
    "  handle.set_objective = function(first, second, third) if third == nil then return set_objective(quest, first, second) end return set_objective(quest, second, third) end\n"
    "  handle.add_objective = function(first, second) return add_objective(quest, second or first) end\n"
    "  handle.state = function() return snapshot(quest) end\n"
    "  return handle\n"
    "end\n"
    "local api = {}\n"
    "function api.define(raw)\n"
    "  local allowed, scope_error = scope_check()\n"
    "  if not allowed then return nil, scope_error end\n"
    "  local quest, parse_error = parse_definition(raw)\n"
    "  if not quest then return nil, parse_error end\n"
    "  if find_quest(quest.id) then return err('duplicate_quest', 'quest ids are unique case-insensitively within a script', quest.id) end\n"
    "  if quest_count >= MAX_QUESTS then return err('quest_limit', 'a script may define at most 16 quests', quest.id) end\n"
    "  local loaded, active_error = load_active()\n"
    "  if not loaded then return nil, active_error end\n"
    "  local state, state_error = load_state(quest)\n"
    "  if not state then return nil, state_error end\n"
    "  quest.state = state\n"
    "  registry[quest.id], quest_count = quest, quest_count + 1\n"
    "  if same_id(active_id, quest.id) then active_id = quest.id end\n"
    "  report(quest, 'defined')\n"
    "  return handle_for(quest)\n"
    "end\n"
    "function api.set_active(value)\n"
    "  local requested = type(value) == 'string' and value or (type(value) == 'table' and value.id)\n"
    "  local quest = find_quest(requested)\n"
    "  if not quest then return err('unknown_quest', 'set_active needs a quest defined by this script') end\n"
    "  local id = quest.id\n"
    "  if same_id(active_id, id) then active_id = id; return true end\n"
    "  local previous = find_quest(active_id)\n"
    "  if not storage.set(ACTIVE_KEY, id) then return err('quest_storage_failed', 'active quest could not be persisted', id) end\n"
    "  active_id = id\n"
    "  if previous then report(previous, 'active_changed') end\n"
    "  report(quest, 'active_changed')\n"
    "  return true\n"
    "end\n"
    "function api.state(value)\n"
    "  local id = value == nil and active_id or (type(value) == 'string' and value or (type(value) == 'table' and value.id))\n"
    "  local quest = find_quest(id)\n"
    "  if not quest then return nil end\n"
    "  return snapshot(quest)\n"
    "end\n"
    "function api._republish_for_host()\n"
    "  for _, quest in pairs(registry) do report(quest, 'republished', true) end\n"
    "  return true\n"
    "end\n"
    "return api\n";
#endif

static void dluma_helper_copy(char* out, size_t capacity, const char* text)
{
    size_t i = 0;
    if (!out || capacity == 0) return;
    if (!text) text = "";
    while (i + 1 < capacity && text[i]) {
        out[i] = text[i];
        ++i;
    }
    out[i] = 0;
}

static char dluma_helper_ascii_lower(char value)
{
    if (value >= 'A' && value <= 'Z') return (char)(value + ('a' - 'A'));
    return value;
}

static int dluma_helper_permission_declared(
    const char* permissions, const char* permission_name)
{
    const char* cursor = permissions;
    size_t expected_length;
    if (!permission_name || !permission_name[0]) return 0;
    expected_length = strlen(permission_name);
    while (cursor && *cursor) {
        const char* start;
        const char* end;
        size_t length;
        size_t i;
        while (*cursor == ',' || *cursor == ' ' || *cursor == '\t' ||
               *cursor == '\r' || *cursor == '\n')
            ++cursor;
        start = cursor;
        while (*cursor && *cursor != ',') ++cursor;
        end = cursor;
        while (end > start &&
               (end[-1] == ' ' || end[-1] == '\t' ||
                end[-1] == '\r' || end[-1] == '\n'))
            --end;
        length = (size_t)(end - start);
        if (length == expected_length) {
            for (i = 0; i < length; ++i)
                if (dluma_helper_ascii_lower(start[i]) !=
                    dluma_helper_ascii_lower(permission_name[i]))
                    break;
            if (i == length) return 1;
        }
        if (*cursor == ',') ++cursor;
    }
    return 0;
}

static uint64_t dluma_helper_query_frequency(void)
{
#if defined(_WIN32)
    LARGE_INTEGER frequency;
    if (!QueryPerformanceFrequency(&frequency) || frequency.QuadPart <= 0)
        return 0;
    return (uint64_t)frequency.QuadPart;
#else
    return (uint64_t)CLOCKS_PER_SEC;
#endif
}

static uint64_t dluma_helper_query_ticks(void)
{
#if defined(_WIN32)
    LARGE_INTEGER ticks;
    if (!QueryPerformanceCounter(&ticks) || ticks.QuadPart < 0) return 0;
    return (uint64_t)ticks.QuadPart;
#else
    clock_t ticks = clock();
    return ticks == (clock_t)-1 ? 0 : (uint64_t)ticks;
#endif
}

static uint32_t dluma_helper_allocate_callback_token(void)
{
    uint32_t token;
    do {
#if defined(_WIN32)
        token = (uint32_t)InterlockedIncrement(
            &g_dluma_helper_next_callback_token);
#else
        token = ++g_dluma_helper_next_callback_token;
#endif
    } while (!token);
    return token;
}

static uint64_t dluma_helper_ticks_to_us(uint64_t ticks, uint64_t frequency)
{
    uint64_t whole;
    uint64_t remainder;
    if (!frequency) return 0;
    whole = ticks / frequency;
    remainder = ticks % frequency;
    if (whole > UINT64_MAX / 1000000u) return UINT64_MAX;
    return whole * 1000000u + (remainder * 1000000u) / frequency;
}

/* Host exclusion is depth-counted because a host adapter may synchronously
 * enter another loader adapter.  Only the outermost interval is accumulated. */
static void dluma_helper_pause_for_host(DlumaHelperScript* script)
{
    if (!script || !script->budget_active) return;
    if (script->budget_host_pause_depth == UINT32_MAX) return;
    if (script->budget_host_pause_depth++ == 0)
        script->budget_host_pause_start_ticks = dluma_helper_query_ticks();
}

static void dluma_helper_resume_after_host(DlumaHelperScript* script)
{
    uint64_t now;
    if (!script || !script->budget_active || !script->budget_host_pause_depth)
        return;
    if (--script->budget_host_pause_depth) return;
    now = dluma_helper_query_ticks();
    if (now >= script->budget_host_pause_start_ticks)
        script->budget_host_excluded_ticks +=
            now - script->budget_host_pause_start_ticks;
    script->budget_host_pause_start_ticks = 0;
}

static uint64_t dluma_helper_budget_elapsed_lua_ticks(
    const DlumaHelperScript* script,
    uint64_t now,
    uint64_t* excluded_out)
{
    uint64_t excluded;
    uint64_t total;
    if (excluded_out) *excluded_out = 0;
    if (!script || !script->budget_active ||
        now < script->budget_callback_start_ticks)
        return 0;
    excluded = script->budget_host_excluded_ticks;
    if (script->budget_host_pause_depth &&
        now >= script->budget_host_pause_start_ticks)
        excluded += now - script->budget_host_pause_start_ticks;
    total = now - script->budget_callback_start_ticks;
    if (excluded > total) excluded = total;
    if (excluded_out) *excluded_out = excluded;
    return total - excluded;
}

static int dluma_helper_budget_wall_clock_exceeded(DlumaHelperScript* script)
{
    uint64_t now;
    uint64_t lua_ticks;
    uint64_t lua_us;
    if (!script || !script->budget_active || !script->wall_clock_limit_ms)
        return 0;
    now = dluma_helper_query_ticks();
    lua_ticks = dluma_helper_budget_elapsed_lua_ticks(script, now, NULL);
    lua_us = dluma_helper_ticks_to_us(lua_ticks, script->budget_qpc_frequency);
    return lua_us > (uint64_t)script->wall_clock_limit_ms * 1000u;
}

static int dluma_helper_budget_pump_wall_clock_exceeded(
    DlumaHelperScript* script)
{
    uint64_t now;
    uint64_t lua_ticks;
    uint64_t lua_us;
    if (!script || !script->budget_active ||
        !script->pump_budget_active ||
        !script->pump_lua_us_limit)
        return 0;
    now = dluma_helper_query_ticks();
    lua_ticks = dluma_helper_budget_elapsed_lua_ticks(
        script, now, NULL);
    lua_us = dluma_helper_ticks_to_us(
        lua_ticks, script->budget_qpc_frequency);
    if (script->pump_lua_us_consumed >=
        script->pump_lua_us_limit)
        return 1;
    return lua_us >
        script->pump_lua_us_limit -
            script->pump_lua_us_consumed;
}

static uint64_t dluma_helper_host_clock_ms(DlumaHelperScript* script)
{
    uint64_t result;
    if (!script || !script->host.clock_ms) return 0;
    dluma_helper_pause_for_host(script);
    result = script->host.clock_ms(script->host_user);
    dluma_helper_resume_after_host(script);
    return result;
}

static void dluma_helper_host_log(
    DlumaHelperScript* script, const char* level, const char* text)
{
    if (!script || !script->host.log) return;
    dluma_helper_pause_for_host(script);
    script->host.log(script->host_user, level, text);
    dluma_helper_resume_after_host(script);
}

static int dluma_helper_host_storage_get(
    DlumaHelperScript* script, const char* key, uint32_t* value_type,
    char* value, uint32_t value_capacity)
{
    int result;
    if (!script || !script->host.storage_get) return 0;
    dluma_helper_pause_for_host(script);
    result = script->host.storage_get(
        script->host_user, key, value_type, value, value_capacity);
    dluma_helper_resume_after_host(script);
    return result;
}

static int dluma_helper_host_config_get(
    DlumaHelperScript* script, const char* key, uint32_t* value_type,
    char* value, uint32_t value_capacity, DlumaLua54Error* error)
{
    int result;
    if (!script || !script->host.config_get) return 0;
    dluma_helper_pause_for_host(script);
    result = script->host.config_get(
        script->host_user, key, value_type, value,
        value_capacity, error);
    dluma_helper_resume_after_host(script);
    return result;
}

static int dluma_helper_host_module_load(
    DlumaHelperScript* script,
    const char* module_name,
    const char** source_out,
    size_t* source_size_out,
    void** artifact_token_out,
    DlumaLua54Error* error)
{
    int result;
    if (!script || !script->host.module_load)
        return 0;
    dluma_helper_pause_for_host(script);
    result = script->host.module_load(
        script->host_user, module_name, source_out,
        source_size_out, artifact_token_out, error);
    dluma_helper_resume_after_host(script);
    return result;
}

static void dluma_helper_host_module_release(
    DlumaHelperScript* script,
    void* artifact_token)
{
    if (!script || !artifact_token ||
        !script->host.module_release)
        return;
    dluma_helper_pause_for_host(script);
    script->host.module_release(
        script->host_user, artifact_token);
    dluma_helper_resume_after_host(script);
}

static int dluma_helper_host_storage_set(
    DlumaHelperScript* script, const char* key, uint32_t value_type,
    const char* value)
{
    int result;
    if (!script || !script->host.storage_set) return 0;
    dluma_helper_pause_for_host(script);
    result = script->host.storage_set(
        script->host_user, key, value_type, value);
    dluma_helper_resume_after_host(script);
    return result;
}

static int dluma_helper_host_storage_delete(
    DlumaHelperScript* script, const char* key)
{
    int result;
    if (!script || !script->host.storage_delete) return 0;
    dluma_helper_pause_for_host(script);
    result = script->host.storage_delete(script->host_user, key);
    dluma_helper_resume_after_host(script);
    return result;
}

static int dluma_helper_host_subscription_added(
    DlumaHelperScript* script, uint32_t token, uint32_t kind,
    const char* exact_name, DlumaLua54Error* error)
{
    int result;
    if (!script || !script->host.subscription_added) return 0;
    dluma_helper_pause_for_host(script);
    result = script->host.subscription_added(
        script->host_user, token, kind, exact_name, error);
    dluma_helper_resume_after_host(script);
    return result;
}

static int dluma_helper_host_subscription_removed(
    DlumaHelperScript* script, uint32_t token, uint32_t kind,
    const char* exact_name, DlumaLua54Error* error)
{
    int result;
    if (!script || !script->host.subscription_removed) return 0;
    dluma_helper_pause_for_host(script);
    result = script->host.subscription_removed(
        script->host_user, token, kind, exact_name, error);
    dluma_helper_resume_after_host(script);
    return result;
}

static int dluma_helper_host_world_find(
    DlumaHelperScript* script, const char* exact_name, const char* expected_class,
    DlumaLua54Object* object, DlumaLua54Error* error)
{
    int result;
    if (!script || !script->host.world_find) return 0;
    dluma_helper_pause_for_host(script);
    result = script->host.world_find(
        script->host_user, exact_name, expected_class, object, error);
    dluma_helper_resume_after_host(script);
    return result;
}

static int dluma_helper_host_world_get_info(
    DlumaHelperScript* script, uint64_t handle_id, uint64_t owner_id,
    uint32_t map_generation, DlumaLua54Object* object, DlumaLua54Error* error)
{
    int result;
    if (!script || !script->host.world_get_info) return 0;
    dluma_helper_pause_for_host(script);
    result = script->host.world_get_info(
        script->host_user, handle_id, owner_id, map_generation, object, error);
    dluma_helper_resume_after_host(script);
    return result;
}

static int dluma_helper_host_world_get_local_enabled(
    DlumaHelperScript* script, uint64_t handle_id, uint64_t owner_id,
    uint32_t map_generation, int* enabled, DlumaLua54Error* error)
{
    int result;
    if (!script || !script->host.world_get_local_enabled) return 0;
    dluma_helper_pause_for_host(script);
    result = script->host.world_get_local_enabled(
        script->host_user, handle_id, owner_id, map_generation, enabled, error);
    dluma_helper_resume_after_host(script);
    return result;
}

static int dluma_helper_host_world_get_position(
    DlumaHelperScript* script, uint64_t handle_id, uint64_t owner_id,
    uint32_t map_generation, DlumaLua54Vec3* position, DlumaLua54Error* error)
{
    int result;
    if (!script || !script->host.world_get_position) return 0;
    dluma_helper_pause_for_host(script);
    result = script->host.world_get_position(
        script->host_user, handle_id, owner_id, map_generation, position, error);
    dluma_helper_resume_after_host(script);
    return result;
}

static int dluma_helper_host_world_get_forward(
    DlumaHelperScript* script, uint64_t handle_id, uint64_t owner_id,
    uint32_t map_generation, DlumaLua54Vec3* forward, DlumaLua54Error* error)
{
    int result;
    if (!script || !script->host.world_get_forward) return 0;
    dluma_helper_pause_for_host(script);
    result = script->host.world_get_forward(
        script->host_user, handle_id, owner_id, map_generation, forward, error);
    dluma_helper_resume_after_host(script);
    return result;
}

static int dluma_helper_host_world_set_local_enabled(
    DlumaHelperScript* script, const DlumaLua54SetEnabledRequest* request,
    DlumaLua54SetEnabledResult* result, DlumaLua54Error* error)
{
    int ok;
    if (!script || !script->host.world_set_local_enabled) return 0;
    dluma_helper_pause_for_host(script);
    ok = script->host.world_set_local_enabled(
        script->host_user, request, result, error);
    dluma_helper_resume_after_host(script);
    return ok;
}

static int dluma_helper_host_sensor_is_inside(
    DlumaHelperScript* script, const char* exact_name, int* inside,
    DlumaLua54Error* error)
{
    int result;
    if (!script || !script->host.sensor_is_inside) return 0;
    dluma_helper_pause_for_host(script);
    result = script->host.sensor_is_inside(
        script->host_user, exact_name, inside, error);
    dluma_helper_resume_after_host(script);
    return result;
}

static int dluma_helper_host_subtitle_show(
    DlumaHelperScript* script, const DlumaLua54SubtitleRequest* request,
    DlumaLua54Error* error)
{
    int result;
    if (!script || !script->host.subtitle_show) return 0;
    dluma_helper_pause_for_host(script);
    result = script->host.subtitle_show(script->host_user, request, error);
    dluma_helper_resume_after_host(script);
    return result;
}

static int dluma_helper_host_subtitle_update(
    DlumaHelperScript* script, const DlumaLua54SubtitleUpdateRequest* request,
    DlumaLua54Error* error)
{
    int result;
    if (!script || !script->host.subtitle_update) return 0;
    dluma_helper_pause_for_host(script);
    result = script->host.subtitle_update(script->host_user, request, error);
    dluma_helper_resume_after_host(script);
    return result;
}

static int dluma_helper_host_radio_dialog(
    DlumaHelperScript* script, const DlumaLua54RadioDialogRequest* request,
    DlumaLua54Error* error)
{
    int result;
    if (!script || !script->host.radio_dialog) return 0;
    dluma_helper_pause_for_host(script);
    result = script->host.radio_dialog(script->host_user, request, error);
    dluma_helper_resume_after_host(script);
    return result;
}

/* Retired with the Lua-owned quest graph.  Native quest transitions return
 * their copied result directly to Lua; there is no Lua quest snapshot cache. */
#if 0
static int dluma_helper_host_quest_state_changed(
    DlumaHelperScript* script, const DlumaLua54QuestStateReport* report,
    DlumaLua54Error* error)
{
    int result;
    if (!script || !script->host.quest_state_changed) return 0;
    dluma_helper_pause_for_host(script);
    result = script->host.quest_state_changed(
        script->host_user, report, error);
    dluma_helper_resume_after_host(script);
    return result;
}
#endif

static int dluma_helper_host_quest_mutate(
    DlumaHelperScript* script, const DlumaLua54QuestRequest* request,
    DlumaLua54QuestResult* result, DlumaLua54Error* error)
{
    int ok;
    if (!script || !script->host.quest_mutate) return 0;
    dluma_helper_pause_for_host(script);
    ok = script->host.quest_mutate(
        script->host_user, request, result, error);
    dluma_helper_resume_after_host(script);
    return ok;
}

static int dluma_helper_host_action_run_chain(
    DlumaHelperScript* script, const char* exact_name,
    DlumaLua54Error* error)
{
    int result;
    if (!script || !script->host.action_run_chain) return 0;
    dluma_helper_pause_for_host(script);
    result = script->host.action_run_chain(
        script->host_user, exact_name, error);
    dluma_helper_resume_after_host(script);
    return result;
}

static int dluma_helper_host_quest_hud_snapshot(
    DlumaHelperScript* script, DlumaLua54QuestHudSnapshot* snapshot,
    DlumaLua54Error* error)
{
    int result;
    if (!script || !script->host.quest_hud_snapshot) return 0;
    dluma_helper_pause_for_host(script);
    result = script->host.quest_hud_snapshot(
        script->host_user, snapshot, error);
    dluma_helper_resume_after_host(script);
    return result;
}

static int dluma_helper_host_quest_hud_override_set(
    DlumaHelperScript* script,
    const DlumaLua54QuestHudOverrideRequest* request,
    DlumaLua54QuestHudOverrideResult* result,
    DlumaLua54Error* error)
{
    int ok;
    if (!script || !script->host.quest_hud_override_set) return 0;
    dluma_helper_pause_for_host(script);
    ok = script->host.quest_hud_override_set(
        script->host_user, request, result, error);
    dluma_helper_resume_after_host(script);
    return ok;
}

static int dluma_helper_host_quest_hud_override_clear(
    DlumaHelperScript* script, uint64_t owner_id,
    uint32_t map_generation, uint64_t override_id,
    DlumaLua54QuestHudOverrideResult* result,
    DlumaLua54Error* error)
{
    int ok;
    if (!script || !script->host.quest_hud_override_clear) return 0;
    dluma_helper_pause_for_host(script);
    ok = script->host.quest_hud_override_clear(
        script->host_user, owner_id, map_generation, override_id,
        result, error);
    dluma_helper_resume_after_host(script);
    return ok;
}

static void dluma_helper_host_quest_hud_clear_owner(
    DlumaHelperScript* script)
{
    if (!script || !script->host.quest_hud_clear_owner)
        return;
    dluma_helper_pause_for_host(script);
    script->host.quest_hud_clear_owner(
        script->host_user, script->owner_id);
    dluma_helper_resume_after_host(script);
}

static uint64_t dluma_helper_now(DlumaHelperScript* script)
{
    return dluma_helper_host_clock_ms(script);
}

/* The standard library needs elapsed-time predicates, but it must not get a
 * direct operating-system clock or a new host callback.  This exposes only
 * the helper's existing monotonic, game-thread clock. */
static int dluma_helper_clock_ms(lua_State* state)
{
    DlumaHelperScript* script = dluma_helper_from_upvalue(state, 1);
    lua_pushinteger(state, (lua_Integer)dluma_helper_now(script));
    return 1;
}

static int dluma_helper_source_fingerprint(lua_State* state)
{
    DlumaHelperScript* script = dluma_helper_from_upvalue(state, 1);
    if (!script) return luaL_error(state, "Lua script context is unavailable");
    /* Lua numbers cannot preserve every uint64 value.  The standard library
     * needs only a deterministic bounded seed, never an identity token. */
    lua_pushinteger(state, (lua_Integer)(script->source_fingerprint & 0x7fffffffu));
    return 1;
}

static void dluma_helper_log(DlumaHelperScript* script, const char* level, const char* text)
{
    dluma_helper_host_log(
        script, level ? level : "info", text ? text : "");
}

static void* dluma_helper_allocator(void* user, void* pointer, size_t old_size, size_t new_size)
{
    DlumaHelperScript* script = (DlumaHelperScript*)user;
    uint64_t effective_old_size;
    uint64_t next_used;
    void* next;
    if (!script) return NULL;
    effective_old_size = pointer ? (uint64_t)old_size : 0;
    if (new_size == 0) {
        free(pointer);
        if (script->memory_used >= effective_old_size)
            script->memory_used -= effective_old_size;
        else {
            script->memory_used = 0;
            dluma_helper_copy(script->last_error, sizeof(script->last_error),
                            "allocator accounting mismatch during free");
        }
        return NULL;
    }
    next_used = script->memory_used;
    if (next_used >= effective_old_size)
        next_used -= effective_old_size;
    else {
        next_used = 0;
        dluma_helper_copy(script->last_error, sizeof(script->last_error),
                        "allocator accounting mismatch during realloc");
    }
    if ((uint64_t)new_size > script->memory_limit ||
        next_used > script->memory_limit - (uint64_t)new_size)
        return NULL;
    next = realloc(pointer, new_size);
    if (!next) return NULL;
    script->memory_used = next_used + (uint64_t)new_size;
    return next;
}

static int dluma_helper_panic(lua_State* state)
{
    DlumaHelperScript* script =
        state ? *(DlumaHelperScript**)lua_getextraspace(state) : NULL;
    const char* message = NULL;
    if (!script) return 0;
    if (lua_type(state, -1) == LUA_TSTRING)
        message = lua_tolstring(state, -1, NULL);
    dluma_helper_copy(script->last_error, sizeof(script->last_error),
                    message ? message : "unprotected Lua panic");
    dluma_helper_copy(script->budget_type, sizeof(script->budget_type), "panic");
    script->panic_raised = 1;
    script->disabled = 1;
    script->alive = 0;
    script->budget_active = 0;
    script->active_callbacks = 0;
    if (script->panic_guard_active)
        longjmp(script->panic_jump, 1);
    return 0;
}

static DlumaHelperScript* dluma_helper_from_upvalue(lua_State* state, int index)
{
    return (DlumaHelperScript*)lua_touserdata(state, lua_upvalueindex(index));
}

static void dluma_helper_set_field_string(lua_State* state, const char* key, const char* value)
{
    if (value && value[0]) lua_pushstring(state, value);
    else lua_pushnil(state);
    lua_setfield(state, -2, key);
}

static int dluma_helper_valid_utf8(const char* text, size_t length)
{
    size_t index = 0;
    while (index < length) {
        unsigned int first = (unsigned char)text[index++];
        unsigned int codepoint;
        unsigned int minimum;
        unsigned int remaining;
        if (first < 0x80U) continue;
        if (first >= 0xC2U && first <= 0xDFU) {
            codepoint = first & 0x1FU;
            minimum = 0x80U;
            remaining = 1U;
        } else if (first >= 0xE0U && first <= 0xEFU) {
            codepoint = first & 0x0FU;
            minimum = 0x800U;
            remaining = 2U;
        } else if (first >= 0xF0U && first <= 0xF4U) {
            codepoint = first & 0x07U;
            minimum = 0x10000U;
            remaining = 3U;
        } else {
            return 0;
        }
        if (remaining > length - index) return 0;
        while (remaining--) {
            unsigned int next = (unsigned char)text[index++];
            if ((next & 0xC0U) != 0x80U) return 0;
            codepoint = (codepoint << 6) | (next & 0x3FU);
        }
        if (codepoint < minimum || codepoint > 0x10FFFFU ||
            (codepoint >= 0xD800U && codepoint <= 0xDFFFU))
            return 0;
    }
    return 1;
}

static int dluma_helper_readonly_write(lua_State* state)
{
    (void)state;
    return luaL_error(state, "attempt to modify an immutable DLUMA snapshot");
}

static int dluma_helper_readonly_pairs_next(lua_State* state)
{
    lua_pushvalue(state, lua_upvalueindex(1));
    if (lua_isnoneornil(state, 2)) lua_pushnil(state);
    else lua_pushvalue(state, 2);
    if (lua_next(state, -2)) {
        lua_remove(state, -3);
        return 2;
    }
    lua_pop(state, 1);
    return 0;
}

static int dluma_helper_readonly_pairs(lua_State* state)
{
    lua_pushvalue(state, lua_upvalueindex(1));
    lua_pushcclosure(state, dluma_helper_readonly_pairs_next, 1);
    lua_pushnil(state);
    lua_pushnil(state);
    return 3;
}

/* Replace the backing table on top of the stack with an immutable proxy. */
static void dluma_helper_make_readonly(lua_State* state)
{
    int backing = lua_absindex(state, -1);
    lua_newtable(state);
    lua_newtable(state);
    lua_pushvalue(state, backing);
    lua_setfield(state, -2, "__index");
    lua_pushcfunction(state, dluma_helper_readonly_write);
    lua_setfield(state, -2, "__newindex");
    lua_pushboolean(state, 0);
    lua_setfield(state, -2, "__metatable");
    lua_pushvalue(state, backing);
    lua_pushcclosure(state, dluma_helper_readonly_pairs, 1);
    lua_setfield(state, -2, "__pairs");
    lua_setmetatable(state, -2);
    lua_remove(state, backing);
}

/* Quest-change callbacks must receive an immutable *copy*, not a shallow
 * proxy over the mutable Lua-owned graph.  The quest snapshot has a bounded
 * table depth, but keeping this helper generic makes the host/event boundary
 * robust if a future copied field becomes another table. */
static void dluma_helper_push_deep_readonly_copy(
    lua_State* state, int source_index, unsigned int depth)
{
    int copy_index;
    source_index = lua_absindex(state, source_index);
    if (!lua_istable(state, source_index)) {
        lua_pushvalue(state, source_index);
        return;
    }
    if (depth >= 16u) {
        lua_newtable(state);
        dluma_helper_make_readonly(state);
        return;
    }
    lua_newtable(state);
    copy_index = lua_absindex(state, -1);
    lua_pushnil(state);
    while (lua_next(state, source_index) != 0) {
        /* Stack: ... copy original_key original_value.  Copy the key and
         * recursively replace only the value before rawset consumes both. */
        lua_pushvalue(state, -2);
        if (lua_istable(state, -2))
            dluma_helper_push_deep_readonly_copy(state, -2, depth + 1u);
        else
            lua_pushvalue(state, -2);
        lua_rawset(state, copy_index);
        lua_pop(state, 1); /* original value; retain key for lua_next */
    }
    dluma_helper_make_readonly(state);
}

typedef struct DlumaHelperJsonReader {
    lua_State* state;
    const unsigned char* cursor;
    const unsigned char* end;
    unsigned int depth;
    unsigned int nodes;
} DlumaHelperJsonReader;

static void dluma_helper_json_space(DlumaHelperJsonReader* reader)
{
    while (reader && reader->cursor < reader->end &&
           (*reader->cursor == ' ' || *reader->cursor == '\t' ||
            *reader->cursor == '\r' || *reader->cursor == '\n'))
        ++reader->cursor;
}

static int dluma_helper_json_hex(unsigned char value)
{
    if (value >= '0' && value <= '9') return (int)(value - '0');
    if (value >= 'a' && value <= 'f') return (int)(value - 'a') + 10;
    if (value >= 'A' && value <= 'F') return (int)(value - 'A') + 10;
    return -1;
}

static int dluma_helper_json_u16(
    DlumaHelperJsonReader* reader,
    unsigned int* output)
{
    unsigned int index;
    unsigned int value = 0;
    if (!reader || !output ||
        (size_t)(reader->end - reader->cursor) < 4u)
        return 0;
    for (index = 0; index < 4u; ++index) {
        int digit = dluma_helper_json_hex(*reader->cursor++);
        if (digit < 0) return 0;
        value = (value << 4u) | (unsigned int)digit;
    }
    *output = value;
    return 1;
}

static int dluma_helper_json_add_codepoint(
    luaL_Buffer* buffer,
    unsigned int codepoint,
    size_t* decoded_size)
{
    char encoded[4];
    size_t count;
    if (!buffer || !decoded_size || codepoint > 0x10FFFFu ||
        (codepoint >= 0xD800u && codepoint <= 0xDFFFu))
        return 0;
    if (codepoint <= 0x7Fu) {
        encoded[0] = (char)codepoint;
        count = 1u;
    } else if (codepoint <= 0x7FFu) {
        encoded[0] = (char)(0xC0u | (codepoint >> 6u));
        encoded[1] = (char)(0x80u | (codepoint & 0x3Fu));
        count = 2u;
    } else if (codepoint <= 0xFFFFu) {
        encoded[0] = (char)(0xE0u | (codepoint >> 12u));
        encoded[1] = (char)(0x80u | ((codepoint >> 6u) & 0x3Fu));
        encoded[2] = (char)(0x80u | (codepoint & 0x3Fu));
        count = 3u;
    } else {
        encoded[0] = (char)(0xF0u | (codepoint >> 18u));
        encoded[1] = (char)(0x80u | ((codepoint >> 12u) & 0x3Fu));
        encoded[2] = (char)(0x80u | ((codepoint >> 6u) & 0x3Fu));
        encoded[3] = (char)(0x80u | (codepoint & 0x3Fu));
        count = 4u;
    }
    if (*decoded_size > 1024u - count) return 0;
    luaL_addlstring(buffer, encoded, count);
    *decoded_size += count;
    return 1;
}

static int dluma_helper_json_string(DlumaHelperJsonReader* reader)
{
    luaL_Buffer buffer;
    size_t decoded_size = 0;
    if (!reader || reader->cursor >= reader->end ||
        *reader->cursor++ != '"')
        return 0;
    luaL_buffinit(reader->state, &buffer);
    while (reader->cursor < reader->end) {
        unsigned char value = *reader->cursor++;
        if (value == '"') {
            luaL_pushresult(&buffer);
            return 1;
        }
        if (value == '\\') {
            unsigned char escape;
            unsigned int codepoint;
            if (reader->cursor >= reader->end) return 0;
            escape = *reader->cursor++;
            if (escape == 'u') {
                unsigned int first;
                if (!dluma_helper_json_u16(reader, &first)) return 0;
                codepoint = first;
                if (first >= 0xD800u && first <= 0xDBFFu) {
                    unsigned int second;
                    if ((size_t)(reader->end - reader->cursor) < 6u ||
                        reader->cursor[0] != '\\' ||
                        reader->cursor[1] != 'u')
                        return 0;
                    reader->cursor += 2;
                    if (!dluma_helper_json_u16(reader, &second) ||
                        second < 0xDC00u || second > 0xDFFFu)
                        return 0;
                    codepoint = 0x10000u +
                        ((first - 0xD800u) << 10u) +
                        (second - 0xDC00u);
                } else if (first >= 0xDC00u && first <= 0xDFFFu) {
                    return 0;
                }
                if (!dluma_helper_json_add_codepoint(
                        &buffer, codepoint, &decoded_size))
                    return 0;
            } else {
                unsigned char decoded;
                if (escape == '"' || escape == '\\' || escape == '/')
                    decoded = escape;
                else if (escape == 'b') decoded = '\b';
                else if (escape == 'f') decoded = '\f';
                else if (escape == 'n') decoded = '\n';
                else if (escape == 'r') decoded = '\r';
                else if (escape == 't') decoded = '\t';
                else return 0;
                if (decoded_size >= 1024u) return 0;
                luaL_addchar(&buffer, (char)decoded);
                ++decoded_size;
            }
        } else {
            if (value < 0x20u || decoded_size >= 1024u) return 0;
            luaL_addchar(&buffer, (char)value);
            ++decoded_size;
        }
    }
    return 0;
}

static int dluma_helper_json_value(DlumaHelperJsonReader* reader);

static int dluma_helper_json_object(DlumaHelperJsonReader* reader)
{
    int table_index;
    if (!reader || reader->cursor >= reader->end ||
        *reader->cursor++ != '{' || reader->depth >= 8u)
        return 0;
    ++reader->depth;
    lua_newtable(reader->state);
    table_index = lua_absindex(reader->state, -1);
    dluma_helper_json_space(reader);
    if (reader->cursor < reader->end && *reader->cursor == '}') {
        ++reader->cursor;
        --reader->depth;
        dluma_helper_make_readonly(reader->state);
        return 1;
    }
    for (;;) {
        int duplicate;
        if (!dluma_helper_json_string(reader)) return 0;
        lua_pushvalue(reader->state, -1);
        lua_rawget(reader->state, table_index);
        duplicate = !lua_isnil(reader->state, -1);
        lua_pop(reader->state, 1);
        if (duplicate) return 0;
        dluma_helper_json_space(reader);
        if (reader->cursor >= reader->end ||
            *reader->cursor++ != ':')
            return 0;
        if (!dluma_helper_json_value(reader)) return 0;
        lua_rawset(reader->state, table_index);
        dluma_helper_json_space(reader);
        if (reader->cursor >= reader->end) return 0;
        if (*reader->cursor == '}') {
            ++reader->cursor;
            break;
        }
        if (*reader->cursor++ != ',') return 0;
        dluma_helper_json_space(reader);
    }
    --reader->depth;
    dluma_helper_make_readonly(reader->state);
    return 1;
}

static int dluma_helper_json_array(DlumaHelperJsonReader* reader)
{
    int table_index;
    lua_Integer item_index = 1;
    if (!reader || reader->cursor >= reader->end ||
        *reader->cursor++ != '[' || reader->depth >= 8u)
        return 0;
    ++reader->depth;
    lua_newtable(reader->state);
    table_index = lua_absindex(reader->state, -1);
    dluma_helper_json_space(reader);
    if (reader->cursor < reader->end && *reader->cursor == ']') {
        ++reader->cursor;
        --reader->depth;
        dluma_helper_make_readonly(reader->state);
        return 1;
    }
    for (;;) {
        if (!dluma_helper_json_value(reader)) return 0;
        lua_rawseti(reader->state, table_index, item_index++);
        dluma_helper_json_space(reader);
        if (reader->cursor >= reader->end) return 0;
        if (*reader->cursor == ']') {
            ++reader->cursor;
            break;
        }
        if (*reader->cursor++ != ',') return 0;
        dluma_helper_json_space(reader);
    }
    --reader->depth;
    dluma_helper_make_readonly(reader->state);
    return 1;
}

static int dluma_helper_json_literal(
    DlumaHelperJsonReader* reader,
    const char* literal)
{
    const char* cursor = literal;
    if (!reader || !literal) return 0;
    while (*cursor) {
        if (reader->cursor >= reader->end ||
            *reader->cursor++ != (unsigned char)*cursor++)
            return 0;
    }
    return 1;
}

static int dluma_helper_json_number(DlumaHelperJsonReader* reader)
{
    const unsigned char* start;
    const unsigned char* cursor;
    if (!reader) return 0;
    start = reader->cursor;
    cursor = start;
    if (cursor < reader->end && *cursor == '-') ++cursor;
    if (cursor >= reader->end) return 0;
    if (*cursor == '0') {
        ++cursor;
        if (cursor < reader->end && *cursor >= '0' && *cursor <= '9')
            return 0;
    } else if (*cursor >= '1' && *cursor <= '9') {
        do { ++cursor; } while (
            cursor < reader->end && *cursor >= '0' && *cursor <= '9');
    } else return 0;
    if (cursor < reader->end && *cursor == '.') {
        ++cursor;
        if (cursor >= reader->end || *cursor < '0' || *cursor > '9')
            return 0;
        do { ++cursor; } while (
            cursor < reader->end && *cursor >= '0' && *cursor <= '9');
    }
    if (cursor < reader->end && (*cursor == 'e' || *cursor == 'E')) {
        ++cursor;
        if (cursor < reader->end && (*cursor == '+' || *cursor == '-'))
            ++cursor;
        if (cursor >= reader->end || *cursor < '0' || *cursor > '9')
            return 0;
        do { ++cursor; } while (
            cursor < reader->end && *cursor >= '0' && *cursor <= '9');
    }
    lua_pushlstring(
        reader->state, (const char*)start, (size_t)(cursor - start));
    if (!lua_stringtonumber(
            reader->state, lua_tostring(reader->state, -1))) {
        lua_pop(reader->state, 1);
        return 0;
    }
    lua_remove(reader->state, -2);
    reader->cursor = cursor;
    return 1;
}

static int dluma_helper_json_value(DlumaHelperJsonReader* reader)
{
    if (!reader) return 0;
    dluma_helper_json_space(reader);
    if (reader->cursor >= reader->end || reader->nodes >= 256u)
        return 0;
    ++reader->nodes;
    if (*reader->cursor == '{') return dluma_helper_json_object(reader);
    if (*reader->cursor == '[') return dluma_helper_json_array(reader);
    if (*reader->cursor == '"') return dluma_helper_json_string(reader);
    if (*reader->cursor == 't') {
        if (!dluma_helper_json_literal(reader, "true")) return 0;
        lua_pushboolean(reader->state, 1);
        return 1;
    }
    if (*reader->cursor == 'f') {
        if (!dluma_helper_json_literal(reader, "false")) return 0;
        lua_pushboolean(reader->state, 0);
        return 1;
    }
    if (*reader->cursor == 'n') {
        if (!dluma_helper_json_literal(reader, "null")) return 0;
        lua_newtable(reader->state);
        lua_pushboolean(reader->state, 1);
        lua_setfield(reader->state, -2, "is_json_null");
        dluma_helper_make_readonly(reader->state);
        return 1;
    }
    if (*reader->cursor == '-' ||
        (*reader->cursor >= '0' && *reader->cursor <= '9'))
        return dluma_helper_json_number(reader);
    return 0;
}

static int dluma_helper_push_json_arguments(
    lua_State* state,
    DlumaLua54ByteView json)
{
    DlumaHelperJsonReader reader;
    int stack_top = lua_gettop(state);
    if (!json.data || json.size < 2u || json.size > 8192u ||
        !dluma_helper_valid_utf8(json.data, json.size) || json.data[0] != '{')
        return 0;
    memset(&reader, 0, sizeof(reader));
    reader.state = state;
    reader.cursor = (const unsigned char*)json.data;
    reader.end = reader.cursor + json.size;
    if (!dluma_helper_json_value(&reader)) {
        lua_settop(state, stack_top);
        return 0;
    }
    dluma_helper_json_space(&reader);
    if (reader.cursor != reader.end || reader.depth != 0u) {
        lua_settop(state, stack_top);
        return 0;
    }
    return 1;
}

static int dluma_helper_set_field_view(
    lua_State* state,
    const char* field,
    DlumaLua54ByteView value,
    size_t maximum)
{
    if (!field || !value.data || !value.size || value.size > maximum ||
        !dluma_helper_valid_utf8(value.data, value.size))
        return 0;
    lua_pushlstring(state, value.data, value.size);
    lua_setfield(state, -2, field);
    return 1;
}

static int dluma_helper_push_quest_phase(
    lua_State* state,
    const DlumaLua54QuestPhaseEvent* phase)
{
    int table_index;
    if (!phase || phase->struct_size < sizeof(*phase) || phase->reserved ||
        !phase->map_generation || !phase->sequence ||
        (phase->runtime_kind != 1u && phase->runtime_kind != 2u) ||
        (phase->completion != 1u && phase->completion != 2u) ||
        !isfinite(phase->wait_seconds) || phase->wait_seconds < 0.0 ||
        phase->wait_seconds > 86400.0 || !phase->local_peer)
        return 0;
    lua_newtable(state);
    table_index = lua_absindex(state, -1);
    if (!dluma_helper_set_field_view(
            state, "quest_id", phase->quest_id, 127u) ||
        !dluma_helper_set_field_view(
            state, "phase_id", phase->phase_id, 127u) ||
        !dluma_helper_set_field_view(
            state, "contract_id", phase->contract_id, 256u) ||
        !dluma_helper_set_field_view(
            state, "provider", phase->provider, 127u) ||
        !dluma_helper_set_field_view(
            state, "handler", phase->handler, 127u) ||
        !dluma_helper_set_field_view(
            state, "binding_hash", phase->binding_hash, 64u)) {
        lua_pop(state, 1);
        return 0;
    }
    lua_pushnumber(state, phase->wait_seconds);
    lua_setfield(state, table_index, "wait");
    lua_pushstring(
        state, phase->completion == 1u
            ? "fire_and_forget" : "stock_timed_wait");
    lua_setfield(state, table_index, "completion");
    lua_pushinteger(state, (lua_Integer)phase->sequence);
    lua_setfield(state, table_index, "sequence");
    lua_pushboolean(state, 1);
    lua_setfield(state, table_index, "local_peer");
    if (!dluma_helper_push_json_arguments(state, phase->arguments_json)) {
        lua_pop(state, 1);
        return 0;
    }
    lua_setfield(state, table_index, "arguments");
    dluma_helper_make_readonly(state);
    return 1;
}

static void dluma_helper_push_context(lua_State* state, DlumaHelperScript* script)
{
    lua_newtable(state);
    dluma_helper_set_field_string(state, "api_version", "1.0");
    dluma_helper_set_field_string(state, "loader_version", script->loader_version);
    dluma_helper_set_field_string(state, "scope",
        script->scope == DLUMA_LUA54_SCOPE_GLOBAL ? "global" :
        script->scope == DLUMA_LUA54_SCOPE_PROJECT ? "project" : "map");
    dluma_helper_set_field_string(state, "map", script->map);
    dluma_helper_set_field_string(state, "project", script->project);
    dluma_helper_set_field_string(state, "workshop_id", script->workshop_id);
    lua_pushinteger(state, (lua_Integer)script->map_generation);
    lua_setfield(state, -2, "map_generation");
    lua_pushboolean(state, script->is_editor);
    lua_setfield(state, -2, "is_editor");
    lua_pushboolean(state, script->is_player);
    lua_setfield(state, -2, "is_player");
    dluma_helper_make_readonly(state);
}

static int dluma_helper_push_error(
    lua_State* state,
    const char* code,
    const char* message,
    const char* capability,
    const char* permission,
    const char* object_name,
    int retryable)
{
    lua_pushnil(state);
    lua_newtable(state);
    dluma_helper_set_field_string(state, "code", code);
    dluma_helper_set_field_string(state, "message", message);
    dluma_helper_set_field_string(state, "capability", capability);
    dluma_helper_set_field_string(state, "permission", permission);
    dluma_helper_set_field_string(state, "object_name", object_name);
    lua_pushboolean(state, retryable);
    lua_setfield(state, -2, "retryable");
    return 2;
}

static int dluma_helper_push_host_error(
    lua_State* state,
    const DlumaLua54Error* error,
    const char* fallback_code,
    const char* fallback_message,
    const char* capability,
    const char* permission,
    const char* object_name)
{
    if (!error || error->struct_size < sizeof(*error))
        return dluma_helper_push_error(state, fallback_code, fallback_message,
                                     capability, permission, object_name, 0);
    return dluma_helper_push_error(state,
        error->code[0] ? error->code : fallback_code,
        error->message[0] ? error->message : fallback_message,
        error->capability[0] ? error->capability : capability,
        error->permission[0] ? error->permission : permission,
        error->object_name[0] ? error->object_name : object_name,
        error->retryable != 0);
}

static int dluma_helper_check_gate(
    lua_State* state,
    DlumaHelperScript* script,
    uint32_t capability,
    uint32_t permission,
    const char* capability_name,
    const char* permission_name)
{
    if (permission && !(script->approved_permissions & permission)) {
        int declared = dluma_helper_permission_declared(
            script->permissions, permission_name);
        const char* reason = !declared
            ? "was not declared in mod.dluma.json access"
            : (script->scope == DLUMA_LUA54_SCOPE_GLOBAL
                ? "was not approved for this global Lua identity"
                : "is disabled by the local Lua permission policy");
        const char* message = !declared
            ? "the shared manifest does not declare the required access"
            : (script->scope == DLUMA_LUA54_SCOPE_GLOBAL
                ? "the required permission was not approved for this global script"
                : "the required permission is disabled for project and map scripts");
        if (!(script->gate_warning_permissions & permission)) {
            char warning[DLUMA_HELPER_TEXT];
            script->gate_warning_permissions |= permission;
            snprintf(warning, sizeof(warning),
                "API unavailable: permission '%s' %s",
                permission_name ? permission_name : "unknown", reason);
            dluma_helper_log(script, "warn", warning);
        }
        dluma_helper_push_error(state,
            "permission_denied",
            message,
            capability_name,
            permission_name,
            NULL,
            0);
        return 0;
    }
    if (capability && !(script->capabilities & capability)) {
        if (!(script->gate_warning_capabilities & capability)) {
            char warning[DLUMA_HELPER_TEXT];
            script->gate_warning_capabilities |= capability;
            snprintf(warning, sizeof(warning),
                "API unavailable: capability '%s' has no verified binding for this executable",
                capability_name ? capability_name : "unknown");
            dluma_helper_log(script, "warn", warning);
        }
        dluma_helper_push_error(state,
            "unsupported_capability",
            "the current executable fingerprint has no verified binding for this capability",
            capability_name,
            permission_name,
            NULL,
            0);
        return 0;
    }
    return 1;
}

static void dluma_helper_record_error(
    DlumaHelperScript* script,
    const char* callback_name,
    const char* error_text)
{
    char combined[DLUMA_LUA54_MAX_ERROR];
    size_t used;
    combined[0] = 0;
    dluma_helper_copy(combined, sizeof(combined), callback_name ? callback_name : "callback");
    used = strlen(combined);
    if (used + 2 < sizeof(combined)) {
        combined[used++] = ':';
        combined[used++] = ' ';
        combined[used] = 0;
    }
    if (error_text) dluma_helper_copy(combined + used, sizeof(combined) - used, error_text);
    dluma_helper_copy(script->last_error, sizeof(script->last_error), combined);
    dluma_helper_log(script, "error", combined);
}

static void dluma_helper_budget_hook(lua_State* state, lua_Debug* debug)
{
    DlumaHelperScript* script = *(DlumaHelperScript**)lua_getextraspace(state);
    (void)debug;
    if (!script || !script->budget_active) return;
    if (script->pump_budget_active &&
        script->pump_instruction_remaining <=
            DLUMA_HELPER_HOOK_GRANULARITY) {
        script->budget_remaining = 0;
        script->pump_instruction_remaining = 0;
        script->pump_budget_exhausted = 1;
        dluma_helper_copy(
            script->budget_type, sizeof(script->budget_type),
            "aggregate_instructions");
        luaL_error(
            state, "DLUMA aggregate pump instruction budget exceeded");
        return;
    }
    if (script->budget_remaining <= DLUMA_HELPER_HOOK_GRANULARITY) {
        script->budget_remaining = 0;
        dluma_helper_copy(script->budget_type, sizeof(script->budget_type), "instructions");
        luaL_error(state, "DLUMA instruction budget exceeded");
        return;
    }
    script->budget_remaining -= DLUMA_HELPER_HOOK_GRANULARITY;
    script->budget_instruction_charged +=
        DLUMA_HELPER_HOOK_GRANULARITY;
    if (script->pump_budget_active)
        script->pump_instruction_remaining -=
            DLUMA_HELPER_HOOK_GRANULARITY;
    if (dluma_helper_budget_wall_clock_exceeded(script)) {
        dluma_helper_copy(
            script->budget_type, sizeof(script->budget_type),
            "wall_clock_lua_only");
        luaL_error(state, "DLUMA Lua-only wall-clock budget exceeded");
    } else if (
        dluma_helper_budget_pump_wall_clock_exceeded(script)) {
        script->pump_budget_exhausted = 1;
        dluma_helper_copy(
            script->budget_type, sizeof(script->budget_type),
            "aggregate_wall_clock_lua_only");
        luaL_error(
            state,
            "DLUMA aggregate pump Lua-only wall-clock budget exceeded");
    }
}

static void dluma_helper_begin_budget(DlumaHelperScript* script)
{
    script->budget_remaining = script->instruction_limit;
    if (script->pump_budget_active &&
        script->pump_instruction_remaining <
            script->budget_remaining)
        script->budget_remaining =
            script->pump_instruction_remaining;
    script->budget_instruction_charged = 0;
    script->budget_callback_start_ticks = dluma_helper_query_ticks();
    script->budget_host_pause_start_ticks = 0;
    script->budget_host_excluded_ticks = 0;
    script->budget_host_pause_depth = 0;
    script->budget_type[0] = 0;
    script->budget_active = 1;
    lua_sethook(script->state, dluma_helper_budget_hook, LUA_MASKCOUNT,
                DLUMA_HELPER_HOOK_GRANULARITY);
}

static void dluma_helper_end_budget(DlumaHelperScript* script)
{
    uint64_t now = dluma_helper_query_ticks();
    uint64_t excluded_ticks = 0;
    uint64_t lua_ticks = dluma_helper_budget_elapsed_lua_ticks(
        script, now, &excluded_ticks);
    script->last_callback_lua_us = dluma_helper_ticks_to_us(
        lua_ticks, script->budget_qpc_frequency);
    script->last_callback_host_excluded_us = dluma_helper_ticks_to_us(
        excluded_ticks, script->budget_qpc_frequency);
    script->last_instruction_count_or_remaining = script->budget_remaining;
    lua_sethook(script->state, NULL, 0, 0);
    if (!script->budget_type[0] && script->wall_clock_limit_ms &&
        script->last_callback_lua_us >
            (uint64_t)script->wall_clock_limit_ms * 1000u)
        dluma_helper_copy(
            script->budget_type, sizeof(script->budget_type),
            "wall_clock_lua_only");
    if (script->pump_budget_active)
    {
        if (UINT64_MAX - script->pump_lua_us_consumed <
            script->last_callback_lua_us)
            script->pump_lua_us_consumed = UINT64_MAX;
        else
            script->pump_lua_us_consumed +=
                script->last_callback_lua_us;
        if (!script->budget_instruction_charged)
        {
            if (script->pump_instruction_remaining <=
                DLUMA_HELPER_HOOK_GRANULARITY)
            {
                script->pump_instruction_remaining = 0;
                script->pump_budget_exhausted = 1;
                if (!script->budget_type[0])
                    dluma_helper_copy(
                        script->budget_type,
                        sizeof(script->budget_type),
                        "aggregate_instructions");
            }
            else
                script->pump_instruction_remaining -=
                    DLUMA_HELPER_HOOK_GRANULARITY;
        }
        if (script->pump_lua_us_consumed >
            script->pump_lua_us_limit)
        {
            script->pump_budget_exhausted = 1;
            if (!script->budget_type[0])
                dluma_helper_copy(
                    script->budget_type,
                    sizeof(script->budget_type),
                    "aggregate_wall_clock_lua_only");
        }
    }
    script->budget_active = 0;
    script->budget_host_pause_depth = 0;
    script->budget_host_pause_start_ticks = 0;
    if (script->budget_type[0]) {
        if (strncmp(
                script->budget_type, "aggregate_", 10) == 0)
            script->pump_budget_exhausted = 1;
        ++script->budget_violations;
        if (script->budget_violations >= DLUMA_HELPER_DISABLE_AFTER_VIOLATIONS)
            script->disabled = 1;
    }
}

static int dluma_helper_message_handler(lua_State* state)
{
    const char* message = lua_type(state, 1) == LUA_TSTRING
        ? lua_tolstring(state, 1, NULL) : NULL;
    /* Lua keeps its canonical OOM string preallocated.  Returning that
     * existing object avoids allocating a traceback while memory is exhausted. */
    if (message && strcmp(message, "not enough memory") == 0) {
        lua_pushvalue(state, 1);
        return 1;
    }
    luaL_traceback(state, state, message ? message : "Lua error", 1);
    return 1;
}

static int dluma_helper_pcall(
    lua_State* state, int nargs, int nresults)
{
    int function_index = lua_gettop(state) - nargs;
    int result;
    lua_pushcfunction(state, dluma_helper_message_handler);
    lua_insert(state, function_index);
    result = lua_pcall(state, nargs, nresults, function_index);
    lua_remove(state, function_index);
    return result;
}

static int dluma_helper_finish_protected(
    DlumaHelperScript* script,
    int result,
    const char* callback_name)
{
    lua_State* state = script->state;
    dluma_helper_end_budget(script);
    if (result != LUA_OK) {
        const char* message = lua_tostring(state, -1);
        dluma_helper_record_error(
            script, callback_name, message ? message : "Lua error");
        lua_pop(state, 1);
        if (result == LUA_ERRMEM && !script->budget_type[0]) {
            dluma_helper_copy(script->budget_type, sizeof(script->budget_type), "memory");
            ++script->budget_violations;
            if (script->budget_violations >= DLUMA_HELPER_DISABLE_AFTER_VIOLATIONS)
                script->disabled = 1;
        }
        return 0;
    }
    if (script->budget_type[0]) {
        dluma_helper_record_error(script, callback_name, "DLUMA callback budget exceeded");
        return 0;
    }
    return 1;
}

static int dluma_helper_push_config_scalar(
    lua_State* state, uint32_t type, const char* value);

static int dluma_helper_pump_budget_can_dispatch(
    DlumaHelperScript* script,
    const char* callback_name)
{
    if (!script || !script->pump_budget_active)
        return 1;
    if (script->pump_budget_exhausted)
        return 0;
    if (script->pump_instruction_remaining <=
        DLUMA_HELPER_HOOK_GRANULARITY)
    {
        script->pump_instruction_remaining = 0;
        script->pump_budget_exhausted = 1;
        script->last_instruction_count_or_remaining = 0;
        dluma_helper_copy(
            script->budget_type, sizeof(script->budget_type),
            "aggregate_instructions");
    }
    else if (script->pump_lua_us_consumed >=
             script->pump_lua_us_limit)
    {
        script->pump_budget_exhausted = 1;
        dluma_helper_copy(
            script->budget_type, sizeof(script->budget_type),
            "aggregate_wall_clock_lua_only");
    }
    else
        return 1;
    ++script->budget_violations;
    if (script->budget_violations >=
        DLUMA_HELPER_DISABLE_AFTER_VIOLATIONS)
        script->disabled = 1;
    dluma_helper_record_error(
        script, callback_name,
        "DLUMA aggregate game-thread pump budget exceeded");
    return 0;
}

static int dluma_helper_call_ref(
    DlumaHelperScript* script,
    int ref,
    const char* callback_name,
    int extra_kind,
    const char* name,
    double number,
    int state_value,
    const DlumaLua54EnemyKilledEvent* enemy,
    const DlumaLua54ConfigChangedEvent* config_change)
{
    lua_State* state = script->state;
    int nargs = 0;
    int result;
    if (!script->alive || script->disabled || ref == LUA_NOREF) return 0;
    if (!dluma_helper_pump_budget_can_dispatch(
            script, callback_name))
        return 0;
    lua_rawgeti(state, LUA_REGISTRYINDEX, ref);
    if (!lua_isfunction(state, -1)) {
        lua_pop(state, 1);
        return 0;
    }
    if (extra_kind != DLUMA_LUA54_EVENT_UPDATE || callback_name) {
        dluma_helper_push_context(state, script);
        ++nargs;
    }
    if (extra_kind == DLUMA_LUA54_EVENT_UPDATE) {
        lua_pushnumber(state, number);
        ++nargs;
    } else if (extra_kind == DLUMA_LUA54_EVENT_KEY) {
        lua_pushboolean(state, state_value);
        ++nargs;
    } else if (extra_kind == DLUMA_LUA54_EVENT_SENSOR_ENTER ||
               extra_kind == DLUMA_LUA54_EVENT_SENSOR_LEAVE ||
               extra_kind == DLUMA_LUA54_EVENT_INTERACT_BEGIN ||
               extra_kind == DLUMA_LUA54_EVENT_INTERACT_END) {
        lua_newtable(state);
        dluma_helper_set_field_string(state, "name", name);
        lua_pushinteger(state, (lua_Integer)script->map_generation);
        lua_setfield(state, -2, "map_generation");
        dluma_helper_make_readonly(state);
        ++nargs;
    } else if (extra_kind == DLUMA_LUA54_EVENT_ENEMY_KILLED) {
        lua_newtable(state);
        if (enemy && enemy->struct_size >= sizeof(*enemy)) {
            dluma_helper_set_field_string(state, "name", enemy->target_name);
            dluma_helper_set_field_string(state, "target_name", enemy->target_name);
            dluma_helper_set_field_string(state, "target_class", enemy->target_class);
            dluma_helper_set_field_string(state, "killer_name", enemy->killer_name);
            lua_pushnumber(state, (lua_Number)enemy->event_sequence);
            lua_setfield(state, -2, "event_sequence");
            lua_pushboolean(state, enemy->killed_by_local_player != 0);
            lua_setfield(state, -2, "killed_by_local_player");
            lua_pushinteger(state, (lua_Integer)enemy->map_generation);
            lua_setfield(state, -2, "map_generation");
        } else {
            lua_pushinteger(state, (lua_Integer)script->map_generation);
            lua_setfield(state, -2, "map_generation");
        }
        dluma_helper_make_readonly(state);
        ++nargs;
    } else if (extra_kind ==
               DLUMA_LUA54_EVENT_CONFIG_CHANGED) {
        lua_newtable(state);
        if (config_change &&
            config_change->struct_size >=
                sizeof(*config_change)) {
            dluma_helper_set_field_string(
                state, "key", config_change->key);
            if (!dluma_helper_push_config_scalar(
                    state,
                    config_change->old_value_type,
                    config_change->old_value))
                lua_pushnil(state);
            lua_setfield(state, -2, "old_value");
            if (!dluma_helper_push_config_scalar(
                    state, config_change->value_type,
                    config_change->value))
                lua_pushnil(state);
            lua_setfield(state, -2, "value");
        }
        dluma_helper_make_readonly(state);
        ++nargs;
    }
    ++script->active_callbacks;
    dluma_helper_begin_budget(script);
    result = dluma_helper_pcall(state, nargs, 0);
    --script->active_callbacks;
    return dluma_helper_finish_protected(script, result, callback_name);
}

static int dluma_helper_call_quest_phase_ref(
    DlumaHelperScript* script,
    int ref,
    const char* callback_name,
    const DlumaLua54QuestPhaseEvent* phase)
{
    lua_State* state;
    int stack_top;
    int result;
    if (!script || !script->alive || script->disabled ||
        ref == LUA_NOREF || !phase)
        return 0;
    if (!dluma_helper_pump_budget_can_dispatch(script, callback_name))
        return 0;
    state = script->state;
    stack_top = lua_gettop(state);
    lua_rawgeti(state, LUA_REGISTRYINDEX, ref);
    if (!lua_isfunction(state, -1)) {
        lua_settop(state, stack_top);
        return 0;
    }
    dluma_helper_push_context(state, script);
    if (!dluma_helper_push_quest_phase(state, phase)) {
        lua_settop(state, stack_top);
        dluma_helper_record_error(
            script, callback_name,
            "custom quest phase payload failed private ABI validation");
        return 0;
    }
    ++script->active_callbacks;
    dluma_helper_begin_budget(script);
    result = dluma_helper_pcall(state, 2, 0);
    --script->active_callbacks;
    return dluma_helper_finish_protected(script, result, callback_name);
}

static int dluma_helper_api_version(lua_State* state)
{
    lua_pushliteral(state, "1.0");
    return 1;
}

static int dluma_helper_loader_version(lua_State* state)
{
    DlumaHelperScript* script = dluma_helper_from_upvalue(state, 1);
    lua_pushstring(state, script->loader_version);
    return 1;
}

static int dluma_helper_capability(lua_State* state)
{
    DlumaHelperScript* script = dluma_helper_from_upvalue(state, 1);
    lua_Integer requested = luaL_checkinteger(state, 1);
    uint32_t configured = 0;
    uint32_t ready = 0;
    char status[128];
    DlumaLua54Error error;
    int ok;
    if (requested <= 0 || requested > 255)
        return dluma_helper_push_error(
            state, "invalid_argument",
            "capability ID must be a generated value in 1..255",
            "core.runtime", "", "", 0);
    if (!script || !script->host.capability_query)
        return dluma_helper_push_error(
            state, "host_adapter_unavailable",
            "stable capability query is unavailable",
            "core.runtime", "", "", 0);
    memset(status, 0, sizeof(status));
    memset(&error, 0, sizeof(error));
    error.struct_size = sizeof(error);
    dluma_helper_pause_for_host(script);
    ok = script->host.capability_query(
        script->host_user, (uint32_t)requested,
        &configured, &ready, status,
        (uint32_t)sizeof(status), &error);
    dluma_helper_resume_after_host(script);
    if (!ok)
        return dluma_helper_push_host_error(
            state, &error, "capability_query_failed",
            "stable capability query failed",
            "core.runtime", "", "");
    lua_newtable(state);
    lua_pushinteger(state, requested);
    lua_setfield(state, -2, "id");
    lua_pushboolean(state, configured != 0);
    lua_setfield(state, -2, "configured");
    lua_pushboolean(state, ready != 0);
    lua_setfield(state, -2, "ready");
    dluma_helper_set_field_string(state, "status", status);
    dluma_helper_make_readonly(state);
    return 1;
}

static int dluma_helper_current_map(lua_State* state)
{
    DlumaHelperScript* script = dluma_helper_from_upvalue(state, 1);
    if (script->map[0]) lua_pushstring(state, script->map);
    else lua_pushnil(state);
    return 1;
}

static int dluma_helper_current_project(lua_State* state)
{
    DlumaHelperScript* script = dluma_helper_from_upvalue(state, 1);
    if (script->project[0]) lua_pushstring(state, script->project);
    else lua_pushnil(state);
    return 1;
}

static int dluma_helper_script_scope(lua_State* state)
{
    DlumaHelperScript* script = dluma_helper_from_upvalue(state, 1);
    lua_pushstring(state,
        script->scope == DLUMA_LUA54_SCOPE_GLOBAL ? "global" :
        script->scope == DLUMA_LUA54_SCOPE_PROJECT ? "project" : "map");
    return 1;
}

static int dluma_helper_log_api(lua_State* state)
{
    DlumaHelperScript* script = dluma_helper_from_upvalue(state, 1);
    int count = lua_gettop(state);
    const char* level = "info";
    const char* text;
    uint64_t now;
    char bounded[DLUMA_HELPER_TEXT];
    if (count == 1) text = luaL_checkstring(state, 1);
    else {
        level = luaL_checkstring(state, 1);
        text = luaL_checkstring(state, 2);
    }
    /* Treat the complete logging adapter as one excluded host interval.  The
     * clock and log wrappers below deliberately nest, exercising the same
     * depth-safe path used when one loader adapter calls another. */
    dluma_helper_pause_for_host(script);
    now = dluma_helper_now(script);
    if (!script->log_window_started_ms || now - script->log_window_started_ms >= 1000) {
        script->log_window_started_ms = now;
        script->log_messages_in_window = 0;
    }
    if (script->log_messages_in_window >= script->max_log_messages_per_second) {
        dluma_helper_resume_after_host(script);
        lua_pushboolean(state, 0);
        return 1;
    }
    ++script->log_messages_in_window;
    dluma_helper_copy(bounded, sizeof(bounded), text);
    dluma_helper_log(script, level, bounded);
    dluma_helper_resume_after_host(script);
    lua_pushboolean(state, 1);
    return 1;
}

static int dluma_helper_stub(lua_State* state)
{
    DlumaHelperScript* script = dluma_helper_from_upvalue(state, 1);
    uint32_t capability = (uint32_t)lua_tointeger(state, lua_upvalueindex(2));
    uint32_t permission = (uint32_t)lua_tointeger(state, lua_upvalueindex(3));
    const char* capability_name = lua_tostring(state, lua_upvalueindex(4));
    const char* permission_name = lua_tostring(state, lua_upvalueindex(5));
    if (!dluma_helper_check_gate(state, script, capability, permission,
                               capability_name, permission_name))
        return 2;
    return dluma_helper_push_error(state,
        "binding_not_implemented",
        "the capability was advertised without a matching high-level adapter",
        capability_name,
        permission_name,
        NULL,
        0);
}

static DlumaHelperObject* dluma_helper_check_object(lua_State* state, int index)
{
    return (DlumaHelperObject*)luaL_checkudata(
        state, index, DLUMA_HELPER_OBJECT_METATABLE);
}

static int dluma_helper_object_refresh(
    lua_State* state,
    DlumaHelperObject* value,
    int push_error)
{
    DlumaLua54Object refreshed;
    DlumaLua54Error error;
    DlumaHelperScript* script;
    if (!value || !(script = value->script) || !script->alive ||
        value->object.owner_id != script->owner_id ||
        !script->host.world_get_info) {
        if (push_error)
            dluma_helper_push_error(state, "stale_object_handle",
                "object handle owner is no longer alive", "world.find",
                "world.query", value ? value->object.name : NULL, 0);
        return 0;
    }
    memset(&refreshed, 0, sizeof(refreshed));
    refreshed.struct_size = sizeof(refreshed);
    memset(&error, 0, sizeof(error));
    error.struct_size = sizeof(error);
    if (!dluma_helper_host_world_get_info(
            script,
            value->object.handle_id,
            value->object.owner_id,
            value->object.map_generation,
            &refreshed,
            &error)) {
        if (push_error)
            dluma_helper_push_host_error(state, &error,
                "stale_object_handle", "object handle is no longer valid",
                "world.find", "world.query", value->object.name);
        return 0;
    }
    value->object = refreshed;
    return 1;
}

static int dluma_helper_object_is_valid(lua_State* state)
{
    DlumaHelperObject* value = dluma_helper_check_object(state, 1);
    lua_pushboolean(state, dluma_helper_object_refresh(state, value, 0));
    return 1;
}

static int dluma_helper_object_is_enabled(lua_State* state)
{
    DlumaHelperObject* value = dluma_helper_check_object(state, 1);
    DlumaHelperScript* script = value->script;
    DlumaLua54Error error;
    int enabled = 0;
    if (!script || !dluma_helper_check_gate(
            state, script, DLUMA_LUA54_CAP_WORLD_LOCAL_ENABLE,
            DLUMA_LUA54_PERMISSION_WORLD_QUERY,
            "world.local_enable", "world.query")) return 2;
    if (!dluma_helper_object_refresh(state, value, 1)) return 2;
    if (!script->host.world_get_local_enabled)
        return dluma_helper_push_error(state, "host_adapter_unavailable",
            "world local-enable host callback is unavailable",
            "world.local_enable", "world.query", value->object.name, 0);
    memset(&error, 0, sizeof(error));
    error.struct_size = sizeof(error);
    if (!dluma_helper_host_world_get_local_enabled(
            script,
            value->object.handle_id,
            value->object.owner_id,
            value->object.map_generation,
            &enabled,
            &error))
        return dluma_helper_push_host_error(state, &error,
            "local_enable_unavailable", "object has no validated ILocalEnable interface",
            "world.local_enable", "world.query", value->object.name);
    lua_pushboolean(state, enabled != 0);
    return 1;
}

static int dluma_helper_object_position(lua_State* state)
{
    DlumaHelperObject* value = dluma_helper_check_object(state, 1);
    DlumaHelperScript* script = value->script;
    DlumaLua54Vec3 position;
    DlumaLua54Error error;
    if (!script || !dluma_helper_check_gate(
            state, script, DLUMA_LUA54_CAP_WORLD_POSITION,
            DLUMA_LUA54_PERMISSION_WORLD_QUERY,
            "world.position", "world.query")) return 2;
    if (!dluma_helper_object_refresh(state, value, 1)) return 2;
    if (!script->host.world_get_position)
        return dluma_helper_push_error(state, "host_adapter_unavailable",
            "world position host callback is unavailable",
            "world.position", "world.query", value->object.name, 0);
    memset(&position, 0, sizeof(position));
    position.struct_size = sizeof(position);
    memset(&error, 0, sizeof(error));
    error.struct_size = sizeof(error);
    if (!dluma_helper_host_world_get_position(
            script, value->object.handle_id, value->object.owner_id,
            value->object.map_generation, &position, &error))
        return dluma_helper_push_host_error(state, &error,
            "world_position_unavailable",
            "object has no validated readable world position",
            "world.position", "world.query", value->object.name);
    if (position.struct_size < sizeof(position) ||
        !isfinite(position.x) || !isfinite(position.y) || !isfinite(position.z))
        return dluma_helper_push_error(state, "invalid_world_position",
            "world position adapter returned a non-finite copied vector",
            "world.position", "world.query", value->object.name, 1);
    lua_createtable(state, 0, 3);
    lua_pushnumber(state, position.x);
    lua_setfield(state, -2, "x");
    lua_pushnumber(state, position.y);
    lua_setfield(state, -2, "y");
    lua_pushnumber(state, position.z);
    lua_setfield(state, -2, "z");
    return 1;
}

static int dluma_helper_object_forward_direction(lua_State* state)
{
    DlumaHelperObject* value = dluma_helper_check_object(state, 1);
    DlumaHelperScript* script = value->script;
    DlumaLua54Vec3 forward;
    DlumaLua54Error error;
    float length;
    if (!script || !dluma_helper_check_gate(
            state, script, DLUMA_LUA54_CAP_WORLD_POSITION,
            DLUMA_LUA54_PERMISSION_WORLD_QUERY,
            "world.position", "world.query")) return 2;
    if (!dluma_helper_object_refresh(state, value, 1)) return 2;
    if (!script->host.world_get_forward)
        return dluma_helper_push_error(state, "host_adapter_unavailable",
            "world forward host callback is unavailable",
            "world.position", "world.query", value->object.name, 0);
    memset(&forward, 0, sizeof(forward));
    forward.struct_size = sizeof(forward);
    memset(&error, 0, sizeof(error));
    error.struct_size = sizeof(error);
    if (!dluma_helper_host_world_get_forward(
            script, value->object.handle_id, value->object.owner_id,
            value->object.map_generation, &forward, &error))
        return dluma_helper_push_host_error(state, &error,
            "world_forward_unavailable",
            "object has no validated readable world forward vector",
            "world.position", "world.query", value->object.name);
    length = sqrtf(forward.x * forward.x + forward.y * forward.y +
                   forward.z * forward.z);
    if (forward.struct_size < sizeof(forward) ||
        !isfinite(forward.x) || !isfinite(forward.y) ||
        !isfinite(forward.z) || !isfinite(length) || length < 0.0001f)
        return dluma_helper_push_error(state, "invalid_world_forward",
            "world forward adapter returned an invalid copied vector",
            "world.position", "world.query", value->object.name, 1);
    lua_createtable(state, 0, 3);
    lua_pushnumber(state, forward.x / length);
    lua_setfield(state, -2, "x");
    lua_pushnumber(state, forward.y / length);
    lua_setfield(state, -2, "y");
    lua_pushnumber(state, forward.z / length);
    lua_setfield(state, -2, "z");
    return 1;
}

static int dluma_helper_object_index(lua_State* state)
{
    DlumaHelperObject* value = dluma_helper_check_object(state, 1);
    const char* key = luaL_checkstring(state, 2);
    if (strcmp(key, "name") == 0) lua_pushstring(state, value->object.name);
    else if (strcmp(key, "class_name") == 0)
        lua_pushstring(state, value->object.class_name);
    else if (strcmp(key, "id") == 0)
        lua_pushinteger(state, (lua_Integer)value->object.object_id);
    else if (strcmp(key, "map_generation") == 0)
        lua_pushinteger(state, (lua_Integer)value->object.map_generation);
    else {
        luaL_getmetatable(state, DLUMA_HELPER_OBJECT_METATABLE);
        lua_getfield(state, -1, "__methods");
        lua_getfield(state, -1, key);
        lua_remove(state, -2);
        lua_remove(state, -2);
    }
    return 1;
}

static int dluma_helper_object_newindex(lua_State* state)
{
    (void)state;
    return luaL_error(state, "DLUMA object handles are immutable");
}

static int dluma_helper_object_tostring(lua_State* state)
{
    DlumaHelperObject* value = dluma_helper_check_object(state, 1);
    char text[256];
    snprintf(text, sizeof(text), "DLUMA object <%s #%d>",
             value->object.name, (int)value->object.object_id);
    lua_pushstring(state, text);
    return 1;
}

static void dluma_helper_create_object_metatable(lua_State* state)
{
    if (!luaL_newmetatable(state, DLUMA_HELPER_OBJECT_METATABLE)) {
        lua_pop(state, 1);
        return;
    }
    lua_pushcfunction(state, dluma_helper_object_index);
    lua_setfield(state, -2, "__index");
    lua_pushcfunction(state, dluma_helper_object_newindex);
    lua_setfield(state, -2, "__newindex");
    lua_pushcfunction(state, dluma_helper_object_tostring);
    lua_setfield(state, -2, "__tostring");
    lua_pushboolean(state, 0);
    lua_setfield(state, -2, "__metatable");
    lua_newtable(state);
    lua_pushcfunction(state, dluma_helper_object_is_valid);
    lua_setfield(state, -2, "is_valid");
    lua_pushcfunction(state, dluma_helper_object_is_enabled);
    lua_setfield(state, -2, "is_enabled");
    lua_pushcfunction(state, dluma_helper_object_position);
    lua_setfield(state, -2, "position");
    lua_pushcfunction(state, dluma_helper_object_forward_direction);
    lua_setfield(state, -2, "forward_direction");
    dluma_helper_make_readonly(state);
    lua_setfield(state, -2, "__methods");
    lua_pop(state, 1);
}

static int dluma_helper_world_find(lua_State* state)
{
    DlumaHelperScript* script = dluma_helper_from_upvalue(state, 1);
    const char* exact_name;
    const char* expected_class = "";
    DlumaLua54Object object;
    DlumaLua54Error error;
    DlumaHelperObject* userdata;
    if (!dluma_helper_check_gate(state, script, DLUMA_LUA54_CAP_WORLD_FIND,
            DLUMA_LUA54_PERMISSION_WORLD_QUERY, "world.find", "world.query"))
        return 2;
    exact_name = luaL_checkstring(state, 1);
    if (!lua_isnoneornil(state, 2)) expected_class = luaL_checkstring(state, 2);
    if (strlen(exact_name) >= DLUMA_LUA54_MAX_OBJECT_NAME ||
        strlen(expected_class) >= DLUMA_LUA54_MAX_CLASS_NAME)
        return luaL_error(state, "object or expected-class name is too long");
    if (!script->host.world_find)
        return dluma_helper_push_error(state, "host_adapter_unavailable",
            "world.find host callback is unavailable", "world.find",
            "world.query", exact_name, 0);
    memset(&object, 0, sizeof(object));
    object.struct_size = sizeof(object);
    memset(&error, 0, sizeof(error));
    error.struct_size = sizeof(error);
    if (!dluma_helper_host_world_find(
            script, exact_name, expected_class, &object, &error))
        return dluma_helper_push_host_error(state, &error,
            "object_not_found", "object lookup failed", "world.find",
            "world.query", exact_name);
    if (!object.handle_id || object.owner_id != script->owner_id ||
        object.struct_size < sizeof(object))
        return dluma_helper_push_error(state, "invalid_host_object",
            "host returned an invalid opaque object record", "world.find",
            "world.query", exact_name, 0);
    userdata = (DlumaHelperObject*)lua_newuserdatauv(state, sizeof(*userdata), 0);
    memset(userdata, 0, sizeof(*userdata));
    userdata->script = script;
    userdata->object = object;
    luaL_getmetatable(state, DLUMA_HELPER_OBJECT_METATABLE);
    lua_setmetatable(state, -2);
    return 1;
}

static void dluma_helper_push_set_enabled_result(
    lua_State* state,
    const DlumaLua54SetEnabledResult* result)
{
    uint32_t i;
    lua_newtable(state);
    lua_pushinteger(state, result->requested_count);
    lua_setfield(state, -2, "requested_count");
    lua_pushinteger(state, result->changed_count);
    lua_setfield(state, -2, "changed_count");
    lua_pushinteger(state, result->unchanged_count);
    lua_setfield(state, -2, "unchanged_count");
    lua_pushboolean(state, result->partial != 0);
    lua_setfield(state, -2, "partial");
    lua_newtable(state);
    for (i = 0; i < result->name_count && i < DLUMA_LUA54_MAX_BULK_OBJECTS; ++i) {
        lua_pushstring(state, result->names[i]);
        lua_rawseti(state, -2, (lua_Integer)i + 1);
    }
    dluma_helper_make_readonly(state);
    lua_setfield(state, -2, "names");
    dluma_helper_make_readonly(state);
}

static int dluma_helper_world_set_enabled_many(lua_State* state)
{
    DlumaHelperScript* script = dluma_helper_from_upvalue(state, 1);
    DlumaLua54SetEnabledRequest request;
    DlumaLua54SetEnabledResult result;
    DlumaLua54Error error;
    lua_Integer count;
    uint32_t i;
    if (!dluma_helper_check_gate(state, script,
            DLUMA_LUA54_CAP_WORLD_LOCAL_ENABLE,
            DLUMA_LUA54_PERMISSION_WORLD_WRITE,
            "world.local_enable", "world.write")) return 2;
    luaL_checktype(state, 1, LUA_TTABLE);
    luaL_checktype(state, 2, LUA_TBOOLEAN);
    count = (lua_Integer)lua_rawlen(state, 1);
    if (count < 1 || count > DLUMA_LUA54_MAX_BULK_OBJECTS)
        return luaL_error(state, "set_enabled_many expects 1..32 exact names");
    memset(&request, 0, sizeof(request));
    request.struct_size = sizeof(request);
    request.map_generation = script->map_generation;
    request.owner_id = script->owner_id;
    request.requested_count = (uint32_t)count;
    request.enabled = lua_toboolean(state, 2) ? 1u : 0u;
    request.atomic = 1;
    request.best_effort = 0;
    if (!lua_isnoneornil(state, 3)) {
        luaL_checktype(state, 3, LUA_TTABLE);
        lua_getfield(state, 3, "atomic");
        if (!lua_isnil(state, -1)) request.atomic = lua_toboolean(state, -1) ? 1u : 0u;
        lua_pop(state, 1);
        lua_getfield(state, 3, "best_effort");
        if (!lua_isnil(state, -1)) request.best_effort = lua_toboolean(state, -1) ? 1u : 0u;
        lua_pop(state, 1);
    }
    for (i = 0; i < request.requested_count; ++i) {
        const char* name;
        lua_rawgeti(state, 1, (lua_Integer)i + 1);
        name = luaL_checkstring(state, -1);
        if (!name[0] || strlen(name) >= DLUMA_LUA54_MAX_OBJECT_NAME) {
            lua_pop(state, 1);
            return luaL_error(state, "set_enabled_many name is empty or too long");
        }
        dluma_helper_copy(request.names[i], sizeof(request.names[i]), name);
        lua_pop(state, 1);
    }
    if (!script->host.world_set_local_enabled)
        return dluma_helper_push_error(state, "host_adapter_unavailable",
            "world local-enable host callback is unavailable",
            "world.local_enable", "world.write", NULL, 0);
    memset(&result, 0, sizeof(result));
    result.struct_size = sizeof(result);
    memset(&error, 0, sizeof(error));
    error.struct_size = sizeof(error);
    if (!dluma_helper_host_world_set_local_enabled(
            script, &request, &result, &error)) {
        if (!result.partial)
            return dluma_helper_push_host_error(state, &error,
                "local_enable_failed", "atomic local-enable request failed",
                "world.local_enable", "world.write", error.object_name);
        dluma_helper_push_set_enabled_result(state, &result);
        (void)dluma_helper_push_host_error(state, &error,
            "local_enable_commit_failed",
            "atomic local-enable commit stopped after a partial result",
            "world.local_enable", "world.write", error.object_name);
        lua_remove(state, -2); /* remove the nil; return result, error */
        return 2;
    }
    dluma_helper_push_set_enabled_result(state, &result);
    return 1;
}

static int dluma_helper_sensor_is_inside(lua_State* state)
{
    DlumaHelperScript* script = dluma_helper_from_upvalue(state, 1);
    const char* exact_name;
    DlumaLua54Error error;
    int inside = 0;
    if (!dluma_helper_check_gate(state, script, DLUMA_LUA54_CAP_EVENTS_SENSOR,
            DLUMA_LUA54_PERMISSION_EVENTS_SENSOR,
            "events.sensor", "events.sensor")) return 2;
    exact_name = luaL_checkstring(state, 1);
    if (!exact_name[0] || strlen(exact_name) >= DLUMA_LUA54_MAX_OBJECT_NAME)
        return luaL_error(state, "sensor name is empty or too long");
    if (!script->host.sensor_is_inside)
        return dluma_helper_push_error(state, "host_adapter_unavailable",
            "sensor host callback is unavailable", "events.sensor",
            "events.sensor", exact_name, 0);
    memset(&error, 0, sizeof(error));
    error.struct_size = sizeof(error);
    if (!dluma_helper_host_sensor_is_inside(
            script, exact_name, &inside, &error))
        return dluma_helper_push_host_error(state, &error,
            "sensor_query_failed", "SensorArea::CheckInside query failed",
            "events.sensor", "events.sensor", exact_name);
    lua_pushboolean(state, inside != 0);
    return 1;
}

static void dluma_helper_push_gate_closure(
    lua_State* state,
    DlumaHelperScript* script,
    lua_CFunction function,
    uint32_t capability,
    uint32_t permission,
    const char* capability_name,
    const char* permission_name)
{
    lua_pushlightuserdata(state, script);
    lua_pushinteger(state, (lua_Integer)capability);
    lua_pushinteger(state, (lua_Integer)permission);
    lua_pushstring(state, capability_name ? capability_name : "");
    lua_pushstring(state, permission_name ? permission_name : "");
    lua_pushcclosure(state, function, 5);
}

static int dluma_helper_register_callback(lua_State* state)
{
    DlumaHelperScript* script = dluma_helper_from_upvalue(state, 1);
    uint32_t kind = (uint32_t)lua_tointeger(state, lua_upvalueindex(2));
    uint32_t capability = (uint32_t)lua_tointeger(state, lua_upvalueindex(3));
    uint32_t permission = (uint32_t)lua_tointeger(state, lua_upvalueindex(4));
    int keyed = kind == DLUMA_LUA54_EVENT_KEY ||
                kind == DLUMA_LUA54_EVENT_SENSOR_ENTER ||
                kind == DLUMA_LUA54_EVENT_SENSOR_LEAVE ||
                kind == DLUMA_LUA54_EVENT_INTERACT_BEGIN ||
                kind == DLUMA_LUA54_EVENT_QUEST_PHASE_PROVIDER ||
                kind == DLUMA_LUA54_EVENT_QUEST_PHASE_OBSERVER;
    int native_backed = kind == DLUMA_LUA54_EVENT_SENSOR_ENTER ||
                        kind == DLUMA_LUA54_EVENT_SENSOR_LEAVE ||
                        kind == DLUMA_LUA54_EVENT_INTERACT_BEGIN ||
                        kind == DLUMA_LUA54_EVENT_QUEST_PHASE_PROVIDER ||
                        kind == DLUMA_LUA54_EVENT_QUEST_PHASE_OBSERVER;
    const char* key = "";
    int callback_index = keyed ? 3 : 2;
    uint32_t i;
    const char* capability_name =
        (kind == DLUMA_LUA54_EVENT_SENSOR_ENTER || kind == DLUMA_LUA54_EVENT_SENSOR_LEAVE)
            ? "events.sensor" :
        kind == DLUMA_LUA54_EVENT_INTERACT_BEGIN
            ? "events.interact" :
        kind == DLUMA_LUA54_EVENT_ENEMY_KILLED
            ? "events.enemy_killed" :
        (kind == DLUMA_LUA54_EVENT_QUEST_PHASE_PROVIDER ||
         kind == DLUMA_LUA54_EVENT_QUEST_PHASE_OBSERVER)
            ? "quests.phase_handlers" : "lua.runtime";
    const char* permission_name =
        permission == DLUMA_LUA54_PERMISSION_EVENTS_SENSOR ? "events.sensor" :
        permission == DLUMA_LUA54_PERMISSION_EVENTS_INTERACT ? "events.interact" :
        permission == DLUMA_LUA54_PERMISSION_EVENTS_ENEMY_KILLED
            ? "events.enemy_killed" :
        permission == DLUMA_LUA54_PERMISSION_QUEST_PHASE_HANDLE
            ? "quest.phase.handle" :
        permission == DLUMA_LUA54_PERMISSION_QUEST_PHASE_OBSERVE
            ? "quest.phase.observe" : "";
    if (!dluma_helper_check_gate(state, script, capability, permission,
                               capability_name, permission_name))
        return 2;
    if (keyed) key = luaL_checkstring(state, 2);
    luaL_checktype(state, callback_index, LUA_TFUNCTION);
    if (strlen(key) >= sizeof(script->callbacks[0].key))
        return luaL_error(state, "subscription name is too long");
    for (i = 0; i < DLUMA_HELPER_MAX_CALLBACKS; ++i) {
        DlumaHelperCallback* item = &script->callbacks[i];
        if (!item->active) continue;
        if (item->kind != kind || strcmp(item->key, key) != 0) continue;
        lua_rawgeti(state, LUA_REGISTRYINDEX, item->ref);
        if (lua_rawequal(state, -1, callback_index)) {
            lua_pop(state, 1);
            lua_pushinteger(state, (lua_Integer)item->token);
            return 1;
        }
        lua_pop(state, 1);
    }
    if (script->callback_count >= script->max_callbacks ||
        script->callback_count >= DLUMA_HELPER_MAX_CALLBACKS)
        return dluma_helper_push_error(state, "callback_limit", "callback limit reached",
                                     "lua.runtime", NULL, key, 0);
    for (i = 0; i < DLUMA_HELPER_MAX_CALLBACKS; ++i)
        if (!script->callbacks[i].active) break;
    if (i >= DLUMA_HELPER_MAX_CALLBACKS)
        return dluma_helper_push_error(state, "callback_limit", "callback limit reached",
                                     "lua.runtime", NULL, key, 0);
    memset(&script->callbacks[i], 0, sizeof(script->callbacks[i]));
    script->callbacks[i].kind = kind;
    script->callbacks[i].token = dluma_helper_allocate_callback_token();
    dluma_helper_copy(script->callbacks[i].key, sizeof(script->callbacks[i].key), key);
    lua_pushvalue(state, callback_index);
    script->callbacks[i].ref = luaL_ref(state, LUA_REGISTRYINDEX);
    if (native_backed) {
        DlumaLua54Error error;
        memset(&error, 0, sizeof(error));
        error.struct_size = sizeof(error);
        if (!script->host.subscription_added ||
            !dluma_helper_host_subscription_added(
                script, script->callbacks[i].token, kind, key, &error)) {
            luaL_unref(state, LUA_REGISTRYINDEX, script->callbacks[i].ref);
            memset(&script->callbacks[i], 0, sizeof(script->callbacks[i]));
            return dluma_helper_push_host_error(state, &error,
                "native_subscription_failed",
                "native gameplay subscription could not be registered",
                capability_name, permission_name, key);
        }
        script->callbacks[i].native_registered = 1;
    }
    script->callbacks[i].active = 1;
    if (kind == DLUMA_LUA54_EVENT_SENSOR_ENTER)
        script->callbacks[i].initial_occupancy_pending = 1;
    ++script->callback_count;
    if (kind == DLUMA_LUA54_EVENT_UPDATE) script->has_update_callback = 1;
    lua_pushinteger(state, (lua_Integer)script->callbacks[i].token);
    return 1;
}

static int dluma_helper_mod_off(lua_State* state)
{
    DlumaHelperScript* script = dluma_helper_from_upvalue(state, 1);
    lua_Integer raw_token = luaL_checkinteger(state, 2);
    uint32_t token;
    uint32_t i;
    if (raw_token <= 0 || (uint64_t)raw_token > UINT32_MAX)
        return dluma_helper_push_error(state, "unknown_subscription",
            "subscription token is not active", "lua.runtime", NULL, NULL, 0);
    token = (uint32_t)raw_token;
    for (i = 0; i < DLUMA_HELPER_MAX_CALLBACKS; ++i) {
        DlumaHelperCallback* callback = &script->callbacks[i];
        if (!callback->active || callback->token != token) continue;
        if (callback->native_registered) {
            DlumaLua54Error error;
            memset(&error, 0, sizeof(error));
            error.struct_size = sizeof(error);
            if (!script->host.subscription_removed ||
                !dluma_helper_host_subscription_removed(
                    script, callback->token, callback->kind,
                    callback->key, &error))
                return dluma_helper_push_host_error(state, &error,
                    "native_unsubscribe_failed",
                    "native gameplay subscription could not be removed",
                    callback->kind == DLUMA_LUA54_EVENT_INTERACT_BEGIN
                        ? "events.interact" :
                    (callback->kind ==
                         DLUMA_LUA54_EVENT_QUEST_PHASE_PROVIDER ||
                     callback->kind ==
                         DLUMA_LUA54_EVENT_QUEST_PHASE_OBSERVER)
                        ? "quests.phase_handlers" : "events.sensor",
                    callback->kind == DLUMA_LUA54_EVENT_INTERACT_BEGIN
                        ? "events.interact" :
                    callback->kind ==
                        DLUMA_LUA54_EVENT_QUEST_PHASE_PROVIDER
                        ? "quest.phase.handle" :
                    callback->kind ==
                        DLUMA_LUA54_EVENT_QUEST_PHASE_OBSERVER
                        ? "quest.phase.observe" : "events.sensor",
                    callback->key);
            callback->native_registered = 0;
        }
        luaL_unref(state, LUA_REGISTRYINDEX, callback->ref);
        memset(callback, 0, sizeof(*callback));
        if (script->callback_count) --script->callback_count;
        script->has_update_callback = 0;
        for (i = 0; i < DLUMA_HELPER_MAX_CALLBACKS; ++i) {
            if (script->callbacks[i].active &&
                script->callbacks[i].kind == DLUMA_LUA54_EVENT_UPDATE) {
                script->has_update_callback = 1;
                break;
            }
        }
        lua_pushboolean(state, 1);
        return 1;
    }
    if (dluma_helper_cancel_timer(script, token)) {
        lua_pushboolean(state, 1);
        return 1;
    }
    return dluma_helper_push_error(state, "unknown_subscription",
        "subscription token is not active", "lua.runtime", NULL, NULL, 0);
}

static void dluma_helper_set_mod_method(
    lua_State* state,
    int table_index,
    const char* name,
    DlumaHelperScript* script,
    uint32_t kind,
    uint32_t capability,
    uint32_t permission)
{
    table_index = lua_absindex(state, table_index);
    lua_pushlightuserdata(state, script);
    lua_pushinteger(state, (lua_Integer)kind);
    lua_pushinteger(state, (lua_Integer)capability);
    lua_pushinteger(state, (lua_Integer)permission);
    lua_pushcclosure(state, dluma_helper_register_callback, 4);
    lua_setfield(state, table_index, name);
}

static int dluma_helper_install_mod_authoring(lua_State* state, int mod_index)
{
    DlumaHelperScript* script = *(DlumaHelperScript**)lua_getextraspace(state);
    int result;
    mod_index = lua_absindex(state, mod_index);
    if (!script || script->stdlib_install_mod_ref == LUA_NOREF) {
        lua_settop(state, mod_index);
        return luaL_error(state, "dluma standard library is unavailable");
    }
    lua_rawgeti(state, LUA_REGISTRYINDEX, script->stdlib_install_mod_ref);
    lua_pushvalue(state, mod_index);
    result = lua_pcall(state, 1, 0, 0);
    if (result != LUA_OK)
        return luaL_error(state, "dluma standard library setup failed: %s",
            lua_tostring(state, -1));
    lua_settop(state, mod_index);
    return 0;
}

static int dluma_helper_mod(lua_State* state)
{
    DlumaHelperScript* script = dluma_helper_from_upvalue(state, 1);
    if (lua_gettop(state) != 0)
        return luaL_error(
            state,
            "dluma.mod() takes no arguments; metadata belongs in "
            "mod.dluma.json");
    if (script->declaration_seen)
        return luaL_error(state, "dluma.mod may be declared only once");
    script->declaration_seen = 1;
    lua_newtable(state);
    dluma_helper_set_mod_method(state, -1, "on_load", script, DLUMA_LUA54_EVENT_LOAD, 0, 0);
    dluma_helper_set_mod_method(state, -1, "on_unload", script, DLUMA_LUA54_EVENT_UNLOAD, 0, 0);
    dluma_helper_set_mod_method(state, -1, "on_map_load", script, DLUMA_LUA54_EVENT_MAP_LOAD, 0, 0);
    dluma_helper_set_mod_method(state, -1, "on_map_unload", script, DLUMA_LUA54_EVENT_MAP_UNLOAD, 0, 0);
    dluma_helper_set_mod_method(state, -1, "on_update", script, DLUMA_LUA54_EVENT_UPDATE, 0, 0);
    dluma_helper_set_mod_method(state, -1, "on_key", script, DLUMA_LUA54_EVENT_KEY, 0, 0);
    dluma_helper_set_mod_method(
        state, -1, "on_config_changed", script,
        DLUMA_LUA54_EVENT_CONFIG_CHANGED, 0, 0);
    dluma_helper_set_mod_method(state, -1, "on_sensor_enter", script,
        DLUMA_LUA54_EVENT_SENSOR_ENTER, DLUMA_LUA54_CAP_EVENTS_SENSOR,
        DLUMA_LUA54_PERMISSION_EVENTS_SENSOR);
    dluma_helper_set_mod_method(state, -1, "on_sensor_leave", script,
        DLUMA_LUA54_EVENT_SENSOR_LEAVE, DLUMA_LUA54_CAP_EVENTS_SENSOR,
        DLUMA_LUA54_PERMISSION_EVENTS_SENSOR);
    dluma_helper_set_mod_method(state, -1, "on_interact_begin", script,
        DLUMA_LUA54_EVENT_INTERACT_BEGIN, DLUMA_LUA54_CAP_EVENTS_INTERACT,
        DLUMA_LUA54_PERMISSION_EVENTS_INTERACT);
    dluma_helper_set_mod_method(state, -1, "on_enemy_killed", script,
        DLUMA_LUA54_EVENT_ENEMY_KILLED,
        DLUMA_LUA54_CAP_EVENTS_ENEMY_KILLED,
        DLUMA_LUA54_PERMISSION_EVENTS_ENEMY_KILLED);
    dluma_helper_set_mod_method(
        state, -1, "handle_quest_phase", script,
        DLUMA_LUA54_EVENT_QUEST_PHASE_PROVIDER,
        DLUMA_LUA54_CAP_QUEST_PHASE_HANDLERS,
        DLUMA_LUA54_PERMISSION_QUEST_PHASE_HANDLE);
    dluma_helper_set_mod_method(
        state, -1, "observe_quest_phase", script,
        DLUMA_LUA54_EVENT_QUEST_PHASE_OBSERVER,
        DLUMA_LUA54_CAP_QUEST_PHASE_HANDLERS,
        DLUMA_LUA54_PERMISSION_QUEST_PHASE_OBSERVE);
    dluma_helper_set_function(state, -1, "off", script, dluma_helper_mod_off);
    if (dluma_helper_install_mod_authoring(state, -1) != 0) return 0;
    dluma_helper_make_readonly(state);
    return 1;
}

static int dluma_helper_timer_create(lua_State* state, int repeating, int is_rule)
{
    DlumaHelperScript* script = dluma_helper_from_upvalue(state, 1);
    double seconds = luaL_checknumber(state, 1);
    uint32_t i;
    DlumaHelperTimer* timer = NULL;
    if (!isfinite(seconds) || seconds < 0.01 || seconds > 86400.0)
        return luaL_error(state, "timer seconds must be finite and in 0.01..86400");
    luaL_checktype(state, 2, LUA_TFUNCTION);
    if (is_rule && script->rule_count >= script->max_rules)
        return dluma_helper_push_error(state, "rule_limit", "active rule limit reached",
                                     "lua.runtime", NULL, NULL, 0);
    if (script->timer_count >= script->max_timers ||
        script->timer_count >= DLUMA_HELPER_MAX_TIMERS)
        return dluma_helper_push_error(state, "timer_limit", "active timer limit reached",
                                     "lua.runtime", NULL, NULL, 0);
    for (i = 0; i < DLUMA_HELPER_MAX_TIMERS; ++i) {
        if (!script->timers[i].active) {
            timer = &script->timers[i];
            break;
        }
    }
    if (!timer)
        return dluma_helper_push_error(state, "timer_limit", "active timer limit reached",
                                     "lua.runtime", NULL, NULL, 0);
    memset(timer, 0, sizeof(*timer));
    lua_pushvalue(state, 2);
    timer->ref = luaL_ref(state, LUA_REGISTRYINDEX);
    /* Timer/rule tokens occupy a disjoint range from native callback tokens,
     * allowing mod:off(token) to own both surfaces without ambiguity. */
    timer->id = (++script->next_timer_id) | 0x80000000u;
    if (timer->id == 0x80000000u)
        timer->id = (++script->next_timer_id) | 0x80000000u;
    timer->active = 1;
    timer->repeating = repeating;
    timer->is_rule = is_rule;
    timer->period_seconds = seconds;
    timer->due_ms = dluma_helper_now(script) + (uint64_t)(seconds * 1000.0);
    ++script->timer_count;
    if (is_rule) ++script->rule_count;
    lua_pushinteger(state, (lua_Integer)timer->id);
    return 1;
}

static int dluma_helper_timer_after(lua_State* state)
{
    return dluma_helper_timer_create(state, 0, 0);
}

static int dluma_helper_timer_every(lua_State* state)
{
    return dluma_helper_timer_create(state, 1, 0);
}

static int dluma_helper_rule_every(lua_State* state)
{
    return dluma_helper_timer_create(state, 1, 1);
}

static int dluma_helper_timer_cancel(lua_State* state)
{
    DlumaHelperScript* script = dluma_helper_from_upvalue(state, 1);
    uint32_t id = (uint32_t)luaL_checkinteger(state, 1);

    lua_pushboolean(state, dluma_helper_cancel_timer(script, id));
    return 1;
}

static int dluma_helper_cancel_timer(DlumaHelperScript* script, uint32_t id)
{
    uint32_t i;
    for (i = 0; i < DLUMA_HELPER_MAX_TIMERS; ++i) {
        DlumaHelperTimer* timer = &script->timers[i];
        if (!timer->active || timer->id != id) continue;
        luaL_unref(script->state, LUA_REGISTRYINDEX, timer->ref);
        if (timer->is_rule && script->rule_count) --script->rule_count;
        memset(timer, 0, sizeof(*timer));
        if (script->timer_count) --script->timer_count;
        return 1;
    }
    return 0;
}

static int dluma_helper_config_key_valid(
    const char* key, size_t length)
{
    size_t index;
    if (!key || !length ||
        length > DLUMA_LUA54_MAX_CONFIG_KEY ||
        key[length - 1u] == '.' ||
        (length == 1u && key[0] == '.') ||
        (length == 2u && key[0] == '.' &&
         key[1] == '.'))
        return 0;
    for (index = 0; index < length; ++index) {
        unsigned char character =
            (unsigned char)key[index];
        if (!((character >= 'a' && character <= 'z') ||
              (character >= 'A' && character <= 'Z') ||
              (character >= '0' && character <= '9') ||
              character == '_' || character == '-' ||
              character == '.'))
            return 0;
    }
    return 1;
}

static int dluma_helper_module_name_valid(
    const char* name,
    size_t length)
{
    size_t index;
    int segment_has_character = 0;
    if (!name || !length || length > 127U)
        return 0;
    for (index = 0; index < length; ++index) {
        char value = name[index];
        if (value == '.') {
            if (!segment_has_character)
                return 0;
            segment_has_character = 0;
            continue;
        }
        if (!((value >= 'a' && value <= 'z') ||
              (value >= 'A' && value <= 'Z') ||
              (value >= '0' && value <= '9') ||
              value == '_'))
            return 0;
        segment_has_character = 1;
    }
    return segment_has_character;
}

static int dluma_helper_require(lua_State* state)
{
    DlumaHelperScript* script =
        dluma_helper_from_upvalue(state, 1);
    DlumaLua54Error error;
    const char* module_name;
    const char* source = NULL;
    size_t module_name_size = 0;
    size_t source_size = 0;
    void* artifact_token = NULL;
    char chunk_name[192];
    int cache;
    int load_result;
    module_name = luaL_checklstring(
        state, 1, &module_name_size);
    if (!script ||
        script->module_cache_ref == LUA_NOREF ||
        !dluma_helper_module_name_valid(
            module_name, module_name_size))
        return luaL_error(
            state,
            "require expects a package-local module name "
            "such as helpers.math");
    lua_rawgeti(
        state, LUA_REGISTRYINDEX,
        script->module_cache_ref);
    cache = lua_absindex(state, -1);
    lua_getfield(state, cache, module_name);
    if (!lua_isnil(state, -1)) {
        lua_remove(state, cache);
        return 1;
    }
    lua_pop(state, 1);
    memset(&error, 0, sizeof(error));
    error.struct_size = sizeof(error);
    if (!dluma_helper_host_module_load(
            script, module_name, &source, &source_size,
            &artifact_token, &error) ||
        !source || !source_size || !artifact_token) {
        if (artifact_token)
            dluma_helper_host_module_release(
                script, artifact_token);
        lua_remove(state, cache);
        return luaL_error(
            state, "module '%s' unavailable: %s",
            module_name,
            error.message[0]
                ? error.message
                : (error.code[0]
                    ? error.code
                    : "not locked in mod.dluma.lock.json"));
    }
    lua_pushboolean(state, 1);
    lua_setfield(state, cache, module_name);
    snprintf(
        chunk_name, sizeof(chunk_name),
        "@dluma-module:%s", module_name);
    load_result = luaL_loadbufferx(
        state, source, source_size, chunk_name, "t");
    dluma_helper_host_module_release(
        script, artifact_token);
    if (load_result != LUA_OK) {
        lua_pushnil(state);
        lua_setfield(state, cache, module_name);
        lua_remove(state, cache);
        return lua_error(state);
    }
    load_result = dluma_helper_pcall(state, 0, 1);
    if (load_result != LUA_OK) {
        lua_pushnil(state);
        lua_setfield(state, cache, module_name);
        lua_remove(state, cache);
        return lua_error(state);
    }
    if (lua_isnil(state, -1)) {
        lua_pop(state, 1);
        lua_pushboolean(state, 1);
    }
    lua_pushvalue(state, -1);
    lua_setfield(state, cache, module_name);
    lua_remove(state, cache);
    return 1;
}

static int dluma_helper_parse_config_integer(
    const char* value, int32_t* out)
{
    const unsigned char* cursor =
        (const unsigned char*)value;
    uint32_t magnitude = 0;
    uint32_t limit = (uint32_t)INT32_MAX;
    int negative = 0;
    if (!cursor || !*cursor) return 0;
    if (*cursor == '-') {
        negative = 1;
        limit = (uint32_t)INT32_MAX + 1u;
        ++cursor;
    }
    if (!*cursor) return 0;
    while (*cursor) {
        uint32_t digit;
        if (*cursor < '0' || *cursor > '9')
            return 0;
        digit = (uint32_t)(*cursor - '0');
        if (magnitude > (limit - digit) / 10u)
            return 0;
        magnitude = magnitude * 10u + digit;
        ++cursor;
    }
    if (out) {
        if (negative && magnitude ==
                (uint32_t)INT32_MAX + 1u)
            *out = INT32_MIN;
        else
            *out = negative
                ? -(int32_t)magnitude
                : (int32_t)magnitude;
    }
    return 1;
}

static int dluma_helper_config_scalar_valid(
    uint32_t type, const char* value)
{
    size_t length;
    if (!value) value = "";
    if (type == DLUMA_LUA54_VALUE_BOOLEAN) {
        return strcmp(value, "true") == 0 ||
               strcmp(value, "false") == 0 ||
               strcmp(value, "1") == 0 ||
               strcmp(value, "0") == 0;
    }
    if (type == DLUMA_LUA54_VALUE_INTEGER)
        return dluma_helper_parse_config_integer(
            value, NULL);
    if (type != DLUMA_LUA54_VALUE_STRING)
        return 0;
    length = strlen(value);
    return length <= DLUMA_LUA54_MAX_CONFIG_VALUE &&
           dluma_helper_valid_utf8(value, length);
}

static int dluma_helper_push_config_scalar(
    lua_State* state, uint32_t type, const char* value)
{
    if (!dluma_helper_config_scalar_valid(type, value))
        return 0;
    if (type == DLUMA_LUA54_VALUE_BOOLEAN) {
        lua_pushboolean(
            state,
            strcmp(value, "true") == 0 ||
            strcmp(value, "1") == 0);
    } else if (type == DLUMA_LUA54_VALUE_INTEGER) {
        int32_t parsed = 0;
        if (!dluma_helper_parse_config_integer(
                value, &parsed))
            return 0;
        lua_pushinteger(state, (lua_Integer)parsed);
    } else {
        lua_pushstring(state, value);
    }
    return 1;
}

static int dluma_helper_config_get(lua_State* state)
{
    DlumaHelperScript* script = dluma_helper_from_upvalue(state, 1);
    size_t key_length = 0;
    const char* key =
        luaL_checklstring(state, 1, &key_length);
    uint32_t type = DLUMA_LUA54_VALUE_NONE;
    char value[DLUMA_LUA54_MAX_CONFIG_VALUE + 1u];
    DlumaLua54Error error;
    if (!dluma_helper_config_key_valid(
            key, key_length))
        return dluma_helper_push_error(
            state, "config_key_invalid",
            "config keys must be bounded ASCII identifiers",
            "config.read", NULL, NULL, 0);
    memset(value, 0, sizeof(value));
    memset(&error, 0, sizeof(error));
    error.struct_size = sizeof(error);
    if (!script->host.config_get ||
        !dluma_helper_host_config_get(
            script, key, &type, value,
            (uint32_t)sizeof(value), &error))
        return dluma_helper_push_host_error(
            state, &error, "config_read_failed",
            "the declared player preference could not be read",
            "config.read", "", key);
    value[sizeof(value) - 1u] = 0;
    if (!dluma_helper_push_config_scalar(state, type, value))
        return dluma_helper_push_error(
            state, "config_value_invalid",
            "the loader returned an invalid typed config value",
            "config.read", NULL, key, 0);
    return 1;
}

static int dluma_helper_storage_get(lua_State* state)
{
    DlumaHelperScript* script = dluma_helper_from_upvalue(state, 1);
    const char* key = luaL_checkstring(state, 1);
    uint32_t type = DLUMA_LUA54_VALUE_NONE;
    char value[16384];
    if (!dluma_helper_check_gate(state, script, DLUMA_LUA54_CAP_STORAGE_MOD,
                               DLUMA_LUA54_PERMISSION_STORAGE_MOD,
                               "storage.mod", "storage.mod"))
        return 2;
    if (!script->host.storage_get ||
        !dluma_helper_host_storage_get(script, key, &type, value, sizeof(value))) {
        return dluma_helper_push_error(state, "storage_read_failed",
            "storage could not read the requested scalar", "storage.mod",
            "storage.mod", key, 1);
    }
    if (type == DLUMA_LUA54_VALUE_NONE) {
        if (lua_gettop(state) >= 2) lua_pushvalue(state, 2);
        else lua_pushnil(state);
    } else if (type == DLUMA_LUA54_VALUE_BOOLEAN)
        lua_pushboolean(state, strcmp(value, "true") == 0 || strcmp(value, "1") == 0);
    else if (type == DLUMA_LUA54_VALUE_NUMBER) {
        char* end = NULL;
        double number = strtod(value, &end);
        if (!end || *end) lua_pushnil(state);
        else lua_pushnumber(state, number);
    } else if (type == DLUMA_LUA54_VALUE_STRING) {
        lua_pushstring(state, value);
    } else {
        return dluma_helper_push_error(state, "storage_read_failed",
            "storage returned an unknown scalar type", "storage.mod",
            "storage.mod", key, 1);
    }
    return 1;
}

static int dluma_helper_storage_set(lua_State* state)
{
    DlumaHelperScript* script = dluma_helper_from_upvalue(state, 1);
    const char* key = luaL_checkstring(state, 1);
    uint32_t type;
    char value[16384];
    int ok;
    if (!dluma_helper_check_gate(state, script, DLUMA_LUA54_CAP_STORAGE_MOD,
                               DLUMA_LUA54_PERMISSION_STORAGE_MOD,
                               "storage.mod", "storage.mod"))
        return 2;
    if (lua_type(state, 2) == LUA_TSTRING) {
        type = DLUMA_LUA54_VALUE_STRING;
        dluma_helper_copy(value, sizeof(value), lua_tostring(state, 2));
    } else if (lua_type(state, 2) == LUA_TNUMBER) {
        type = DLUMA_LUA54_VALUE_NUMBER;
        snprintf(value, sizeof(value), "%.17g", (double)lua_tonumber(state, 2));
    } else if (lua_type(state, 2) == LUA_TBOOLEAN) {
        type = DLUMA_LUA54_VALUE_BOOLEAN;
        dluma_helper_copy(value, sizeof(value), lua_toboolean(state, 2) ? "true" : "false");
    } else {
        return luaL_error(state, "storage values must be string, number, or boolean");
    }
    ok = script->host.storage_set &&
        dluma_helper_host_storage_set(script, key, type, value);
    lua_pushboolean(state, ok);
    return 1;
}

static int dluma_helper_storage_delete(lua_State* state)
{
    DlumaHelperScript* script = dluma_helper_from_upvalue(state, 1);
    const char* key = luaL_checkstring(state, 1);
    int ok;
    if (!dluma_helper_check_gate(state, script, DLUMA_LUA54_CAP_STORAGE_MOD,
                               DLUMA_LUA54_PERMISSION_STORAGE_MOD,
                               "storage.mod", "storage.mod"))
        return 2;
    ok = script->host.storage_delete &&
        dluma_helper_host_storage_delete(script, key);
    lua_pushboolean(state, ok);
    return 1;
}

static int dluma_helper_audio_stub(lua_State* state)
{
    const char* cue = luaL_checkstring(state, 1);
    if (strchr(cue, '/') || strchr(cue, '\\') || strchr(cue, ':') || strstr(cue, ".csb") ||
        strstr(cue, ".wav") || strstr(cue, ".ogg"))
        return dluma_helper_push_error(state, "invalid_registered_cue",
            "audio APIs accept a registered cue name, never a file path",
            "audio.registered_cues", "audio.play", cue, 0);
    return dluma_helper_stub(state);
}

static void dluma_helper_push_quest_hud_target(
    lua_State* state, const DlumaLua54QuestHudTarget* target)
{
    lua_newtable(state);
    dluma_helper_set_field_string(state, "id", target->id);
    dluma_helper_set_field_string(state, "text", target->text);
    dluma_helper_set_field_string(
        state, "display_text", target->display_text);
    lua_pushboolean(state, target->overridden != 0);
    lua_setfield(state, -2, "overridden");
    if (target->kind == DLUMA_LUA54_QUEST_HUD_OBJECTIVE) {
        lua_pushinteger(state, (lua_Integer)target->slot);
        lua_setfield(state, -2, "slot");
    }
}

static int dluma_helper_ui_quest_hud(lua_State* state)
{
    DlumaHelperScript* script = dluma_helper_from_upvalue(state, 1);
    DlumaLua54QuestHudSnapshot snapshot;
    DlumaLua54Error error;
    uint32_t index;
    if (!dluma_helper_check_gate(
            state, script, DLUMA_LUA54_CAP_UI_QUEST_HUD,
            DLUMA_LUA54_PERMISSION_GAME_READ,
            "ui.quest_hud", "game.read"))
        return 2;
    if (!script->host.quest_hud_snapshot)
        return dluma_helper_push_error(state, "host_adapter_unavailable",
            "quest HUD snapshot host callback is unavailable",
            "ui.quest_hud", "game.read", NULL, 0);
    memset(&snapshot, 0, sizeof(snapshot));
    snapshot.struct_size = sizeof(snapshot);
    memset(&error, 0, sizeof(error));
    error.struct_size = sizeof(error);
    if (!dluma_helper_host_quest_hud_snapshot(
            script, &snapshot, &error))
        return dluma_helper_push_host_error(state, &error,
            "quest_hud_snapshot_failed",
            "the current quest HUD could not be copied",
            "ui.quest_hud", "game.read", NULL);
    if (snapshot.struct_size < sizeof(snapshot) ||
        snapshot.objective_count >
            DLUMA_LUA54_MAX_QUEST_HUD_OBJECTIVES)
        return dluma_helper_push_error(state, "invalid_host_snapshot",
            "quest HUD host returned an invalid copied snapshot",
            "ui.quest_hud", "game.read", NULL, 0);
    lua_newtable(state);
    lua_pushinteger(state, (lua_Integer)snapshot.revision);
    lua_setfield(state, -2, "revision");
    if (snapshot.has_header) {
        dluma_helper_push_quest_hud_target(state, &snapshot.header);
        lua_setfield(state, -2, "header");
    }
    lua_newtable(state);
    for (index = 0; index < snapshot.objective_count; ++index) {
        dluma_helper_push_quest_hud_target(
            state, &snapshot.objectives[index]);
        lua_rawseti(state, -2, (lua_Integer)index + 1);
    }
    lua_setfield(state, -2, "objectives");
    if (snapshot.has_timer) {
        dluma_helper_push_quest_hud_target(state, &snapshot.timer);
        lua_setfield(state, -2, "timer");
    }
    return 1;
}

static int dluma_helper_companion_open(lua_State* state)
{
    DlumaHelperScript* script = dluma_helper_from_upvalue(state, 1);
    DlumaCompanionOpenRequest request;
    DlumaCompanionOpenResult result;
    DlumaLua54Error error;
    const char* relative_path;
    size_t length;
    char sha256[65];
    relative_path = luaL_checklstring(state, 1, &length);
    if (!length || length > 512U ||
        memchr(relative_path, 0, length) ||
        !dluma_helper_valid_utf8(relative_path, length))
        return dluma_helper_push_error(
            state, "invalid_argument",
            "companion path must be 1..512 UTF-8 bytes without NUL",
            "companion.content", "companion.read",
            NULL, 0);
    if (!script || !script->host.api_invoke)
        return dluma_helper_push_error(
            state, "host_adapter_unavailable",
            "the shared companion-content service is unavailable",
            "companion.content", "companion.read",
            relative_path, 1);
    memset(&request, 0, sizeof(request));
    request.struct_size = (uint32_t)sizeof(request);
    request.struct_version = 1U;
    request.relative_path.data = relative_path;
    request.relative_path.size = (uint64_t)length;
    memset(&result, 0, sizeof(result));
    result.struct_size = (uint32_t)sizeof(result);
    result.struct_version = 1U;
    memset(sha256, 0, sizeof(sha256));
    result.sha256.data = sha256;
    result.sha256.capacity = (uint64_t)sizeof(sha256);
    memset(&error, 0, sizeof(error));
    error.struct_size = (uint32_t)sizeof(error);
    if (!script->host.api_invoke(
            script->host_user,
            11001U,
            &request, (uint32_t)sizeof(request),
            &result, (uint32_t)sizeof(result),
            &error))
        return dluma_helper_push_host_error(
            state, &error,
            "companion_open_failed",
            "the approval-bound companion artifact could not be opened",
            "companion.content", "companion.read",
            relative_path);
    lua_newtable(state);
    lua_pushinteger(state, (lua_Integer)result.token);
    lua_setfield(state, -2, "token");
    lua_pushinteger(state, (lua_Integer)result.size);
    lua_setfield(state, -2, "size");
    lua_pushstring(state, sha256);
    lua_setfield(state, -2, "sha256");
    dluma_helper_make_readonly(state);
    return 1;
}

static int dluma_helper_public_api_unavailable(
    lua_State* state, const char* capability, const char* permission)
{
    return dluma_helper_push_error(
        state, "host_adapter_unavailable",
        "the shared DLUMA API dispatcher is unavailable",
        capability, permission, NULL, 1);
}

static int dluma_helper_player_vehicle_state(lua_State* state)
{
    DlumaHelperScript* script = dluma_helper_from_upvalue(state, 1);
    DlumaNoRequest request;
    DlumaVehicleStateResult result;
    DlumaLua54Error error;
    char vehicle_type[33];
    char class_name[97];
    if (!script || !script->host.api_invoke)
        return dluma_helper_public_api_unavailable(
            state, "player.vehicle", "game.read");
    memset(&request, 0, sizeof(request));
    request.struct_size = (uint32_t)sizeof(request);
    request.struct_version = 1U;
    memset(&result, 0, sizeof(result));
    result.struct_size = (uint32_t)sizeof(result);
    result.struct_version = 1U;
    memset(vehicle_type, 0, sizeof(vehicle_type));
    memset(class_name, 0, sizeof(class_name));
    result.vehicle_type.data = vehicle_type;
    result.vehicle_type.capacity = (uint64_t)sizeof(vehicle_type);
    result.class_name.data = class_name;
    result.class_name.capacity = (uint64_t)sizeof(class_name);
    memset(&error, 0, sizeof(error));
    error.struct_size = (uint32_t)sizeof(error);
    if (!script->host.api_invoke(
            script->host_user, 3012U,
            &request, (uint32_t)sizeof(request),
            &result, (uint32_t)sizeof(result), &error))
        return dluma_helper_push_host_error(
            state, &error, "vehicle_state_failed",
            "the local player vehicle state is unavailable",
            "player.vehicle", "game.read", NULL);
    lua_newtable(state);
    lua_pushboolean(state, result.player_present != 0U);
    lua_setfield(state, -2, "player_present");
    lua_pushboolean(state, result.vehicle_present != 0U);
    lua_setfield(state, -2, "vehicle_present");
    lua_pushboolean(state, result.is_local_driver != 0U);
    lua_setfield(state, -2, "is_local_driver");
    dluma_helper_set_field_string(state, "vehicle_type", vehicle_type);
    dluma_helper_set_field_string(state, "class_name", class_name);
    lua_newtable(state);
    lua_pushnumber(state, (lua_Number)result.linear_velocity.x);
    lua_setfield(state, -2, "x");
    lua_pushnumber(state, (lua_Number)result.linear_velocity.y);
    lua_setfield(state, -2, "y");
    lua_pushnumber(state, (lua_Number)result.linear_velocity.z);
    lua_setfield(state, -2, "z");
    dluma_helper_make_readonly(state);
    lua_setfield(state, -2, "linear_velocity");
    lua_pushnumber(state, (lua_Number)result.speed_mps);
    lua_setfield(state, -2, "speed_mps");
    lua_pushnumber(state, (lua_Number)result.speed_mph);
    lua_setfield(state, -2, "speed_mph");
    lua_pushinteger(state, (lua_Integer)result.level_epoch);
    lua_setfield(state, -2, "level_epoch");
    dluma_helper_make_readonly(state);
    return 1;
}

static int dluma_helper_companion_read_entry(lua_State* state)
{
    DlumaHelperScript* script = dluma_helper_from_upvalue(state, 1);
    DlumaCompanionEntryReadRequest request;
    DlumaCompanionEntryReadResult result;
    DlumaLua54Error error;
    const char* entry_path;
    unsigned char* bytes;
    size_t entry_length;
    char sha256[65];
    uint64_t token = (uint64_t)luaL_checkinteger(state, 1);
    entry_path = luaL_checklstring(state, 2, &entry_length);
    if (!token || !entry_length || entry_length > 260U ||
        memchr(entry_path, 0, entry_length) ||
        !dluma_helper_valid_utf8(entry_path, entry_length))
        return luaL_error(state,
            "companion token/path is invalid");
    if (!script || !script->host.api_invoke)
        return dluma_helper_public_api_unavailable(
            state, "companion.content", "companion.read");
    bytes = (unsigned char*)malloc(1024U * 1024U);
    if (!bytes)
        return dluma_helper_push_error(
            state, "allocation_failed",
            "the bounded companion entry buffer could not be allocated",
            "companion.content", "companion.read", entry_path, 1);
    memset(&request, 0, sizeof(request));
    request.struct_size = (uint32_t)sizeof(request);
    request.struct_version = 1U;
    request.token = token;
    request.entry_path.data = entry_path;
    request.entry_path.size = (uint64_t)entry_length;
    memset(&result, 0, sizeof(result));
    result.struct_size = (uint32_t)sizeof(result);
    result.struct_version = 1U;
    result.bytes.data = bytes;
    result.bytes.capacity = 1024U * 1024U;
    memset(sha256, 0, sizeof(sha256));
    result.sha256.data = sha256;
    result.sha256.capacity = (uint64_t)sizeof(sha256);
    memset(&error, 0, sizeof(error));
    error.struct_size = (uint32_t)sizeof(error);
    if (!script->host.api_invoke(
            script->host_user, 11002U,
            &request, (uint32_t)sizeof(request),
            &result, (uint32_t)sizeof(result), &error)) {
        free(bytes);
        return dluma_helper_push_host_error(
            state, &error, "companion_entry_read_failed",
            "the approved companion entry could not be read",
            "companion.content", "companion.read", entry_path);
    }
    if (result.bytes.required > 1024U * 1024U) {
        free(bytes);
        return dluma_helper_push_error(
            state, "entry_too_large",
            "the companion entry exceeds the 1 MiB read limit",
            "companion.content", "companion.read", entry_path, 0);
    }
    lua_newtable(state);
    lua_pushlstring(
        state, (const char*)bytes, (size_t)result.bytes.required);
    lua_setfield(state, -2, "bytes");
    lua_pushstring(state, sha256);
    lua_setfield(state, -2, "sha256");
    free(bytes);
    dluma_helper_make_readonly(state);
    return 1;
}

static int dluma_helper_audio_token_result(
    lua_State* state, DlumaHelperScript* script, uint32_t operation_id,
    const void* request, uint32_t request_size, const char* capability)
{
    DlumaTokenResult result;
    DlumaLua54Error error;
    if (!script || !script->host.api_invoke)
        return dluma_helper_public_api_unavailable(
            state, capability, "audio.play");
    memset(&result, 0, sizeof(result));
    result.struct_size = (uint32_t)sizeof(result);
    result.struct_version = 1U;
    memset(&error, 0, sizeof(error));
    error.struct_size = (uint32_t)sizeof(error);
    if (!script->host.api_invoke(
            script->host_user, operation_id,
            request, request_size,
            &result, (uint32_t)sizeof(result), &error))
        return dluma_helper_push_host_error(
            state, &error, "audio_operation_failed",
            "the shared audio operation failed",
            capability, "audio.play", NULL);
    lua_pushinteger(state, (lua_Integer)result.token);
    return 1;
}

static int dluma_helper_audio_bool_result(
    lua_State* state, DlumaHelperScript* script, uint32_t operation_id,
    const void* request, uint32_t request_size, const char* capability)
{
    DlumaBoolResult result;
    DlumaLua54Error error;
    if (!script || !script->host.api_invoke)
        return dluma_helper_public_api_unavailable(
            state, capability, "audio.play");
    memset(&result, 0, sizeof(result));
    result.struct_size = (uint32_t)sizeof(result);
    result.struct_version = 1U;
    memset(&error, 0, sizeof(error));
    error.struct_size = (uint32_t)sizeof(error);
    if (!script->host.api_invoke(
            script->host_user, operation_id,
            request, request_size,
            &result, (uint32_t)sizeof(result), &error))
        return dluma_helper_push_host_error(
            state, &error, "audio_operation_failed",
            "the shared audio operation failed",
            capability, "audio.play", NULL);
    lua_pushboolean(state, result.value != 0U);
    return 1;
}

static void dluma_helper_init_audio_string(
    DlumaStringView* view, const char* value, size_t length)
{
    view->data = value;
    view->size = (uint64_t)length;
}

static int dluma_helper_audio_bank_open(lua_State* state)
{
    DlumaAudioBankOpenRequest request;
    const char* bank_id;
    size_t length;
    memset(&request, 0, sizeof(request));
    request.struct_size = (uint32_t)sizeof(request);
    request.struct_version = 1U;
    request.companion_token = (uint64_t)luaL_checkinteger(state, 1);
    bank_id = luaL_checklstring(state, 2, &length);
    dluma_helper_init_audio_string(&request.bank_id, bank_id, length);
    return dluma_helper_audio_token_result(
        state, dluma_helper_from_upvalue(state, 1), 6004U,
        &request, (uint32_t)sizeof(request), "audio.stem");
}

static int dluma_helper_audio_cue_duration(lua_State* state)
{
    DlumaHelperScript* script = dluma_helper_from_upvalue(state, 1);
    DlumaAudioCueRequest request;
    DlumaU64Result result;
    DlumaLua54Error error;
    const char* cue;
    size_t length;
    memset(&request, 0, sizeof(request));
    request.struct_size = (uint32_t)sizeof(request);
    request.struct_version = 1U;
    request.bank_token = (uint64_t)luaL_checkinteger(state, 1);
    cue = luaL_checklstring(state, 2, &length);
    dluma_helper_init_audio_string(&request.cue, cue, length);
    if (!script || !script->host.api_invoke)
        return dluma_helper_public_api_unavailable(
            state, "audio.stem", "audio.play");
    memset(&result, 0, sizeof(result));
    result.struct_size = (uint32_t)sizeof(result);
    result.struct_version = 1U;
    memset(&error, 0, sizeof(error));
    error.struct_size = (uint32_t)sizeof(error);
    if (!script->host.api_invoke(
            script->host_user, 6005U,
            &request, (uint32_t)sizeof(request),
            &result, (uint32_t)sizeof(result), &error))
        return dluma_helper_push_host_error(
            state, &error, "audio_cue_duration_failed",
            "the cue duration is unavailable",
            "audio.stem", "audio.play", cue);
    lua_pushinteger(state, (lua_Integer)result.value);
    return 1;
}

static int dluma_helper_audio_cue_play(lua_State* state)
{
    DlumaAudioCuePlayRequest request;
    const char* cue;
    size_t length;
    memset(&request, 0, sizeof(request));
    request.struct_size = (uint32_t)sizeof(request);
    request.struct_version = 1U;
    request.bank_token = (uint64_t)luaL_checkinteger(state, 1);
    cue = luaL_checklstring(state, 2, &length);
    dluma_helper_init_audio_string(&request.cue, cue, length);
    request.start_ms = (uint64_t)luaL_optinteger(state, 3, 0);
    request.initial_gain = (float)luaL_optnumber(state, 4, 1.0);
    request.loop = lua_gettop(state) >= 5 && lua_toboolean(state, 5)
        ? 1U : 0U;
    request.fade_in_ms = (uint32_t)luaL_optinteger(state, 6, 0);
    request.fade_out_ms = (uint32_t)luaL_optinteger(state, 7, 0);
    return dluma_helper_audio_token_result(
        state, dluma_helper_from_upvalue(state, 1), 6002U,
        &request, (uint32_t)sizeof(request), "audio.stem");
}

static int dluma_helper_audio_cue_state(lua_State* state)
{
    DlumaHelperScript* script = dluma_helper_from_upvalue(state, 1);
    DlumaTokenRequest request;
    DlumaAudioCueStateResult result;
    DlumaLua54Error error;
    memset(&request, 0, sizeof(request));
    request.struct_size = (uint32_t)sizeof(request);
    request.struct_version = 1U;
    request.token = (uint64_t)luaL_checkinteger(state, 1);
    if (!script || !script->host.api_invoke)
        return dluma_helper_public_api_unavailable(
            state, "audio.stem", "audio.play");
    memset(&result, 0, sizeof(result));
    result.struct_size = (uint32_t)sizeof(result);
    result.struct_version = 1U;
    memset(&error, 0, sizeof(error));
    error.struct_size = (uint32_t)sizeof(error);
    if (!script->host.api_invoke(
            script->host_user, 6006U,
            &request, (uint32_t)sizeof(request),
            &result, (uint32_t)sizeof(result), &error))
        return dluma_helper_push_host_error(
            state, &error, "audio_cue_state_failed",
            "the cue state is unavailable",
            "audio.stem", "audio.play", NULL);
    lua_newtable(state);
    lua_pushboolean(state, result.playing != 0U);
    lua_setfield(state, -2, "playing");
    lua_pushinteger(state, (lua_Integer)result.position_ms);
    lua_setfield(state, -2, "position_ms");
    lua_pushinteger(state, (lua_Integer)result.duration_ms);
    lua_setfield(state, -2, "duration_ms");
    dluma_helper_make_readonly(state);
    return 1;
}

static int dluma_helper_audio_cue_seek(lua_State* state)
{
    DlumaAudioCueSeekRequest request;
    memset(&request, 0, sizeof(request));
    request.struct_size = (uint32_t)sizeof(request);
    request.struct_version = 1U;
    request.cue_token = (uint64_t)luaL_checkinteger(state, 1);
    request.position_ms = (uint64_t)luaL_checkinteger(state, 2);
    return dluma_helper_audio_bool_result(
        state, dluma_helper_from_upvalue(state, 1), 6007U,
        &request, (uint32_t)sizeof(request), "audio.stem");
}

static int dluma_helper_audio_cue_gain(lua_State* state)
{
    DlumaAudioCueGainRequest request;
    memset(&request, 0, sizeof(request));
    request.struct_size = (uint32_t)sizeof(request);
    request.struct_version = 1U;
    request.cue_token = (uint64_t)luaL_checkinteger(state, 1);
    request.gain = (float)luaL_checknumber(state, 2);
    return dluma_helper_audio_bool_result(
        state, dluma_helper_from_upvalue(state, 1), 6008U,
        &request, (uint32_t)sizeof(request), "audio.stem");
}

static int dluma_helper_audio_filter(
    lua_State* state, uint32_t operation_id, const char* capability)
{
    DlumaAudioFilterRequest request;
    memset(&request, 0, sizeof(request));
    request.struct_size = (uint32_t)sizeof(request);
    request.struct_version = 1U;
    request.token = (uint64_t)luaL_checkinteger(state, 1);
    request.wet_mix = (float)luaL_checknumber(state, 2);
    request.cutoff_hz = (float)luaL_checknumber(state, 3);
    return dluma_helper_audio_bool_result(
        state, dluma_helper_from_upvalue(state, 1), operation_id,
        &request, (uint32_t)sizeof(request), capability);
}

static int dluma_helper_audio_cue_filter(lua_State* state)
{
    return dluma_helper_audio_filter(
        state, 6009U, "audio.wave_filter");
}

static int dluma_helper_audio_cue_stop(lua_State* state)
{
    DlumaAudioCueStopRequest request;
    memset(&request, 0, sizeof(request));
    request.struct_size = (uint32_t)sizeof(request);
    request.struct_version = 1U;
    request.cue_token = (uint64_t)luaL_checkinteger(state, 1);
    request.fade_out_ms = (uint32_t)luaL_optinteger(state, 2, 0);
    return dluma_helper_audio_bool_result(
        state, dluma_helper_from_upvalue(state, 1), 6003U,
        &request, (uint32_t)sizeof(request), "audio.stem");
}

static int dluma_helper_audio_music_acquire(lua_State* state)
{
    DlumaAudioMusicAcquireRequest request;
    memset(&request, 0, sizeof(request));
    request.struct_size = (uint32_t)sizeof(request);
    request.struct_version = 1U;
    request.fade_out_ms = (uint32_t)luaL_optinteger(state, 1, 0);
    return dluma_helper_audio_token_result(
        state, dluma_helper_from_upvalue(state, 1), 6010U,
        &request, (uint32_t)sizeof(request), "audio.music_exclusive");
}

static int dluma_helper_audio_music_state(lua_State* state)
{
    DlumaHelperScript* script = dluma_helper_from_upvalue(state, 1);
    DlumaTokenRequest request;
    DlumaAudioMusicStateResult result;
    DlumaLua54Error error;
    memset(&request, 0, sizeof(request));
    request.struct_size = (uint32_t)sizeof(request);
    request.struct_version = 1U;
    request.token = (uint64_t)luaL_checkinteger(state, 1);
    if (!script || !script->host.api_invoke)
        return dluma_helper_public_api_unavailable(
            state, "audio.music_exclusive", "audio.play");
    memset(&result, 0, sizeof(result));
    result.struct_size = (uint32_t)sizeof(result);
    result.struct_version = 1U;
    memset(&error, 0, sizeof(error));
    error.struct_size = (uint32_t)sizeof(error);
    if (!script->host.api_invoke(
            script->host_user, 6011U,
            &request, (uint32_t)sizeof(request),
            &result, (uint32_t)sizeof(result), &error))
        return dluma_helper_push_host_error(
            state, &error, "audio_music_state_failed",
            "the exclusive music lease state is unavailable",
            "audio.music_exclusive", "audio.play", NULL);
    lua_pushinteger(state, (lua_Integer)result.phase);
    return 1;
}

static int dluma_helper_audio_music_filter(lua_State* state)
{
    return dluma_helper_audio_filter(
        state, 6012U, "audio.music_filter");
}

static int dluma_helper_audio_music_release(lua_State* state)
{
    DlumaAudioMusicReleaseRequest request;
    memset(&request, 0, sizeof(request));
    request.struct_size = (uint32_t)sizeof(request);
    request.struct_version = 1U;
    request.token = (uint64_t)luaL_checkinteger(state, 1);
    request.resume_delay_ms = (uint32_t)luaL_optinteger(state, 2, 0);
    return dluma_helper_audio_bool_result(
        state, dluma_helper_from_upvalue(state, 1), 6013U,
        &request, (uint32_t)sizeof(request), "audio.music_exclusive");
}

static int dluma_helper_quest_hud_override_clear_impl(
    lua_State* state, DlumaHelperQuestHudOverride* handle,
    int push_result)
{
    DlumaLua54QuestHudOverrideResult result;
    DlumaLua54Error error;
    int was_active;
    if (!handle || !handle->script) {
        if (push_result) lua_pushboolean(state, 0);
        return push_result ? 1 : 0;
    }
    was_active = handle->active != 0;
    if (!was_active) {
        if (push_result) lua_pushboolean(state, 0);
        return push_result ? 1 : 0;
    }
    memset(&result, 0, sizeof(result));
    result.struct_size = sizeof(result);
    memset(&error, 0, sizeof(error));
    error.struct_size = sizeof(error);
    if (!handle->script->host.quest_hud_override_clear ||
        !dluma_helper_host_quest_hud_override_clear(
            handle->script, handle->owner_id,
            handle->map_generation, handle->override_id,
            &result, &error)) {
        if (!push_result)
            return 0;
        return dluma_helper_push_host_error(state, &error,
            "quest_hud_override_clear_failed",
            "quest HUD override could not be cleared",
            "ui.quest_hud", "ui.overlay", handle->target_id);
    }
    handle->active = 0;
    if (push_result) lua_pushboolean(state, 1);
    return push_result ? 1 : 0;
}

static int dluma_helper_quest_hud_override_update(lua_State* state)
{
    DlumaHelperQuestHudOverride* handle =
        (DlumaHelperQuestHudOverride*)luaL_checkudata(
            state, 1, DLUMA_HELPER_QUEST_HUD_OVERRIDE_METATABLE);
    DlumaLua54QuestHudOverrideRequest request;
    DlumaLua54QuestHudOverrideResult result;
    DlumaLua54Error error;
    const char* text;
    size_t length;
    if (!handle || !handle->script || !handle->active)
        return dluma_helper_push_error(state, "quest_hud_target_expired",
            "the quest HUD override handle is no longer active",
            "ui.quest_hud", "ui.overlay",
            handle ? handle->target_id : NULL, 1);
    text = luaL_checklstring(state, 2, &length);
    if (!length ||
        length > DLUMA_LUA54_MAX_QUEST_HUD_OVERRIDE_TEXT ||
        memchr(text, 0, length) ||
        !dluma_helper_valid_utf8(text, length))
        return luaL_error(
            state,
            "quest HUD override text must be 1..512 UTF-8 bytes without NUL");
    memset(&request, 0, sizeof(request));
    request.struct_size = sizeof(request);
    request.map_generation = handle->map_generation;
    request.owner_id = handle->owner_id;
    request.override_id = handle->override_id;
    memcpy(request.target_id, handle->target_id,
           strlen(handle->target_id) + 1U);
    memcpy(request.text, text, length);
    request.text[length] = 0;
    memset(&result, 0, sizeof(result));
    result.struct_size = sizeof(result);
    memset(&error, 0, sizeof(error));
    error.struct_size = sizeof(error);
    if (!dluma_helper_host_quest_hud_override_set(
            handle->script, &request, &result, &error)) {
        if (strcmp(error.code, "quest_hud_target_expired") == 0)
            handle->active = 0;
        return dluma_helper_push_host_error(state, &error,
            "quest_hud_override_update_failed",
            "quest HUD override could not be updated",
            "ui.quest_hud", "ui.overlay", handle->target_id);
    }
    lua_pushboolean(state, 1);
    return 1;
}

static int dluma_helper_quest_hud_override_clear(lua_State* state)
{
    DlumaHelperQuestHudOverride* handle =
        (DlumaHelperQuestHudOverride*)luaL_checkudata(
            state, 1, DLUMA_HELPER_QUEST_HUD_OVERRIDE_METATABLE);
    return dluma_helper_quest_hud_override_clear_impl(
        state, handle, 1);
}

static int dluma_helper_quest_hud_override_gc(lua_State* state)
{
    DlumaHelperQuestHudOverride* handle =
        (DlumaHelperQuestHudOverride*)luaL_checkudata(
            state, 1, DLUMA_HELPER_QUEST_HUD_OVERRIDE_METATABLE);
    (void)dluma_helper_quest_hud_override_clear_impl(
        state, handle, 0);
    return 0;
}

static void dluma_helper_create_quest_hud_override_metatable(
    lua_State* state)
{
    if (luaL_newmetatable(
            state, DLUMA_HELPER_QUEST_HUD_OVERRIDE_METATABLE)) {
        lua_pushcfunction(state, dluma_helper_quest_hud_override_gc);
        lua_setfield(state, -2, "__gc");
        lua_newtable(state);
        lua_pushcfunction(
            state, dluma_helper_quest_hud_override_update);
        lua_setfield(state, -2, "update");
        lua_pushcfunction(
            state, dluma_helper_quest_hud_override_clear);
        lua_setfield(state, -2, "clear");
        lua_setfield(state, -2, "__index");
    }
    lua_pop(state, 1);
}

static int dluma_helper_ui_override_quest_hud(lua_State* state)
{
    DlumaHelperScript* script = dluma_helper_from_upvalue(state, 1);
    DlumaLua54QuestHudOverrideRequest request;
    DlumaLua54QuestHudOverrideResult result;
    DlumaLua54Error error;
    DlumaHelperQuestHudOverride* handle;
    const char* target_id;
    const char* text;
    size_t target_length;
    size_t text_length;
    if (!dluma_helper_check_gate(
            state, script, DLUMA_LUA54_CAP_UI_QUEST_HUD,
            DLUMA_LUA54_PERMISSION_UI_OVERLAY,
            "ui.quest_hud", "ui.overlay"))
        return 2;
    if (script->scope == DLUMA_LUA54_SCOPE_GLOBAL)
        return dluma_helper_push_error(state,
            "quest_hud_scope_not_supported",
            "global scripts may read but may not override the local quest HUD",
            "ui.quest_hud", "ui.overlay", NULL, 0);
    target_id = luaL_checklstring(state, 1, &target_length);
    text = luaL_checklstring(state, 2, &text_length);
    if (!target_length ||
        target_length >= DLUMA_LUA54_MAX_QUEST_HUD_TARGET_ID ||
        memchr(target_id, 0, target_length))
        return luaL_error(state, "quest HUD target id is invalid");
    if (!text_length ||
        text_length > DLUMA_LUA54_MAX_QUEST_HUD_OVERRIDE_TEXT ||
        memchr(text, 0, text_length) ||
        !dluma_helper_valid_utf8(text, text_length))
        return luaL_error(
            state,
            "quest HUD override text must be 1..512 UTF-8 bytes without NUL");
    if (!script->host.quest_hud_override_set)
        return dluma_helper_push_error(state, "host_adapter_unavailable",
            "quest HUD override host callback is unavailable",
            "ui.quest_hud", "ui.overlay", target_id, 0);
    memset(&request, 0, sizeof(request));
    request.struct_size = sizeof(request);
    request.map_generation = script->map_generation;
    request.owner_id = script->owner_id;
    memcpy(request.target_id, target_id, target_length);
    request.target_id[target_length] = 0;
    memcpy(request.text, text, text_length);
    request.text[text_length] = 0;
    memset(&result, 0, sizeof(result));
    result.struct_size = sizeof(result);
    memset(&error, 0, sizeof(error));
    error.struct_size = sizeof(error);
    if (!dluma_helper_host_quest_hud_override_set(
            script, &request, &result, &error))
        return dluma_helper_push_host_error(state, &error,
            "quest_hud_override_failed",
            "quest HUD override could not be created",
            "ui.quest_hud", "ui.overlay", target_id);
    if (!result.override_id || !result.active)
        return dluma_helper_push_error(state, "invalid_host_override",
            "quest HUD host returned an invalid override handle",
            "ui.quest_hud", "ui.overlay", target_id, 0);
    handle = (DlumaHelperQuestHudOverride*)lua_newuserdatauv(
        state, sizeof(*handle), 0);
    memset(handle, 0, sizeof(*handle));
    handle->script = script;
    handle->owner_id = script->owner_id;
    handle->override_id = result.override_id;
    handle->map_generation = script->map_generation;
    handle->active = 1;
    memcpy(handle->target_id, target_id, target_length);
    handle->target_id[target_length] = 0;
    luaL_getmetatable(
        state, DLUMA_HELPER_QUEST_HUD_OVERRIDE_METATABLE);
    lua_setmetatable(state, -2);
    return 1;
}

static int dluma_helper_ui_send_subtitle(lua_State* state)
{
    DlumaHelperScript* script = dluma_helper_from_upvalue(state, 1);
    DlumaLua54SubtitleRequest request;
    DlumaLua54Error error;
    size_t length;
    const char* value;
    double duration;
    if (!dluma_helper_check_gate(state, script, DLUMA_LUA54_CAP_UI_SUBTITLES,
            DLUMA_LUA54_PERMISSION_UI_OVERLAY,
            "ui.subtitles", "ui.overlay")) return 2;
    value = luaL_checklstring(state, 1, &length);
    duration = luaL_checknumber(state, 2);
    if (!lua_isnoneornil(state, 3) && !lua_isboolean(state, 3))
        return luaL_error(state, "subtitle force must be a boolean");
    if (!length || length >= DLUMA_LUA54_MAX_SUBTITLE_TEXT ||
        memchr(value, 0, length))
        return luaL_error(state, "subtitle text must be 1..1023 UTF-8 bytes without NUL");
    if (!isfinite(duration) || duration < 0.05 || duration > 600.0)
        return luaL_error(state, "subtitle duration must be 0.05..600 seconds");
    if (!script->host.subtitle_show)
        return dluma_helper_push_error(state, "host_adapter_unavailable",
            "subtitle host callback is unavailable",
            "ui.subtitles", "ui.overlay", NULL, 0);
    memset(&request, 0, sizeof(request));
    request.struct_size = sizeof(request);
    request.duration_seconds = duration;
    request.force = lua_toboolean(state, 3) ? 1u : 0u;
    memcpy(request.text, value, length);
    request.text[length] = 0;
    memset(&error, 0, sizeof(error));
    error.struct_size = sizeof(error);
    if (!dluma_helper_host_subtitle_show(script, &request, &error))
        return dluma_helper_push_host_error(state, &error,
            "subtitle_show_failed", "subtitle could not be shown",
            "ui.subtitles", "ui.overlay", NULL);
    lua_pushboolean(state, 1);
    return 1;
}

static int dluma_helper_ui_update_subtitle(lua_State* state)
{
    DlumaHelperScript* script = dluma_helper_from_upvalue(state, 1);
    DlumaLua54SubtitleUpdateRequest request;
    DlumaLua54Error error;
    size_t length;
    const char* value;
    if (!dluma_helper_check_gate(state, script, DLUMA_LUA54_CAP_UI_SUBTITLES,
            DLUMA_LUA54_PERMISSION_UI_OVERLAY,
            "ui.subtitles", "ui.overlay")) return 2;
    value = luaL_checklstring(state, 1, &length);
    if (!lua_isnoneornil(state, 2) && !lua_isboolean(state, 2))
        return luaL_error(state, "subtitle fade must be a boolean");
    if (!length || length >= DLUMA_LUA54_MAX_SUBTITLE_TEXT ||
        memchr(value, 0, length))
        return luaL_error(state, "subtitle text must be 1..1023 UTF-8 bytes without NUL");
    if (!script->host.subtitle_update)
        return dluma_helper_push_error(state, "host_adapter_unavailable",
            "subtitle-update host callback is unavailable",
            "ui.subtitles", "ui.overlay", NULL, 0);
    memset(&request, 0, sizeof(request));
    request.struct_size = sizeof(request);
    request.fade = lua_toboolean(state, 2) ? 1u : 0u;
    memcpy(request.text, value, length);
    request.text[length] = 0;
    memset(&error, 0, sizeof(error));
    error.struct_size = sizeof(error);
    if (!dluma_helper_host_subtitle_update(script, &request, &error))
        return dluma_helper_push_host_error(state, &error,
            "subtitle_update_failed", "active subtitle could not be updated",
            "ui.subtitles", "ui.overlay", NULL);
    lua_pushboolean(state, 1);
    return 1;
}

static int dluma_helper_audio_play_radio_dialog(lua_State* state)
{
    DlumaHelperScript* script = dluma_helper_from_upvalue(state, 1);
    DlumaLua54RadioDialogRequest request;
    DlumaLua54Error error;
    size_t speaker_length;
    size_t group_length;
    const char* speaker;
    const char* group;
    if (!dluma_helper_check_gate(state, script,
            DLUMA_LUA54_CAP_AUDIO_RADIO_DIALOG,
            DLUMA_LUA54_PERMISSION_AUDIO_PLAY,
            "audio.radio_dialog", "audio.play")) return 2;
    speaker = luaL_checklstring(state, 1, &speaker_length);
    group = luaL_checklstring(state, 2, &group_length);
    if (!speaker_length || speaker_length >= DLUMA_LUA54_MAX_DIALOG_NAME ||
        !group_length || group_length >= DLUMA_LUA54_MAX_DIALOG_NAME ||
        memchr(speaker, 0, speaker_length) || memchr(group, 0, group_length))
        return luaL_error(state, "radio-dialog names must be 1..95 bytes without NUL");
    if (!script->host.radio_dialog)
        return dluma_helper_push_error(state, "host_adapter_unavailable",
            "radio-dialog host callback is unavailable",
            "audio.radio_dialog", "audio.play", speaker, 0);
    memset(&request, 0, sizeof(request));
    request.struct_size = sizeof(request);
    memcpy(request.speaker, speaker, speaker_length);
    request.speaker[speaker_length] = 0;
    memcpy(request.group, group, group_length);
    request.group[group_length] = 0;
    memset(&error, 0, sizeof(error));
    error.struct_size = sizeof(error);
    if (!dluma_helper_host_radio_dialog(script, &request, &error))
        return dluma_helper_push_host_error(state, &error,
            "radio_dialog_failed", "radio dialog could not be played",
            "audio.radio_dialog", "audio.play", speaker);
    lua_pushboolean(state, 1);
    return 1;
}

static int dluma_helper_action_run_chain(lua_State* state)
{
    DlumaHelperScript* script = dluma_helper_from_upvalue(state, 1);
    const char* exact_name;
    DlumaLua54Error error;
    if (!dluma_helper_check_gate(state, script,
            DLUMA_LUA54_CAP_ACTION_CHAIN,
            DLUMA_LUA54_PERMISSION_WORLD_WRITE,
            "actions.chain", "world.write")) return 2;
    exact_name = luaL_checkstring(state, 1);
    if (!exact_name[0] || strlen(exact_name) >= DLUMA_LUA54_MAX_OBJECT_NAME)
        return luaL_error(state, "ActionChain name is empty or too long");
    if (!script->host.action_run_chain)
        return dluma_helper_push_error(state, "host_adapter_unavailable",
            "ActionChain host callback is unavailable",
            "actions.chain", "world.write", exact_name, 0);
    memset(&error, 0, sizeof(error));
    error.struct_size = sizeof(error);
    if (!dluma_helper_host_action_run_chain(script, exact_name, &error))
        return dluma_helper_push_host_error(state, &error,
            "action_chain_run_failed", "ActionChain could not be run",
            "actions.chain", "world.write", exact_name);
    lua_pushboolean(state, 1);
    return 1;
}

static int dluma_helper_quest_identifier_valid(
    const char* value, size_t length, int required)
{
    size_t i;
    if (!length) return required ? 0 : 1;
    if (!value || length >= DLUMA_LUA54_MAX_QUEST_ID ||
        memchr(value, 0, length)) return 0;
    for (i = 0; i < length; ++i) {
        char c = value[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.'))
            return 0;
    }
    return 1;
}

static int dluma_helper_quest_result_identifier_matches(
    const char* value, size_t capacity, const char* expected)
{
    size_t length = 0;
    size_t expected_length;
    if (!value || !expected || !capacity) return 0;
    while (length < capacity && value[length]) ++length;
    if (length == capacity) return 0;
    expected_length = strlen(expected);
    return length == expected_length &&
           memcmp(value, expected, length) == 0;
}

static int dluma_helper_quest_mutate(lua_State* state)
{
    DlumaHelperScript* script = dluma_helper_from_upvalue(state, 1);
    lua_Integer raw_operation = luaL_checkinteger(state, 1);
    size_t quest_length;
    size_t phase_length = 0;
    const char* quest = luaL_checklstring(state, 2, &quest_length);
    const char* phase = "";
    DlumaLua54QuestRequest request;
    DlumaLua54QuestResult result;
    DlumaLua54Error error;
    if (!dluma_helper_check_gate(state, script,
            DLUMA_LUA54_CAP_QUEST_NATIVE_CONTROL,
            DLUMA_LUA54_PERMISSION_QUEST_WRITE,
            "quests.native", "quest.write")) return 2;
    if (!script || script->scope == DLUMA_LUA54_SCOPE_GLOBAL)
        return dluma_helper_push_error(state, "quest_scope_not_supported",
            "native quest transitions are available only to a custom-map project or map script",
            "quests.native", "quest.write", quest, 0);
    if (!lua_isnoneornil(state, 3))
        phase = luaL_checklstring(state, 3, &phase_length);
    if (raw_operation < DLUMA_LUA54_QUEST_START ||
        raw_operation > DLUMA_LUA54_QUEST_COMPLETE ||
        !dluma_helper_quest_identifier_valid(quest, quest_length, 1) ||
        !dluma_helper_quest_identifier_valid(phase, phase_length,
            raw_operation == DLUMA_LUA54_QUEST_JUMP_TO_PHASE))
        return dluma_helper_push_error(state, "invalid_quest_request",
            "quest control accepts only bounded exact quest and phase identifiers",
            "quests.native", "quest.write", quest, 0);
    if (!script->host.quest_mutate)
        return dluma_helper_push_error(state, "host_adapter_unavailable",
            "native quest host callback is unavailable",
            "quests.native", "quest.write", quest, 0);
    memset(&request, 0, sizeof(request));
    request.struct_size = sizeof(request);
    request.map_generation = script->map_generation;
    request.owner_id = script->owner_id;
    request.operation = (uint32_t)raw_operation;
    memcpy(request.native_quest_id, quest, quest_length);
    request.native_quest_id[quest_length] = 0;
    if (phase_length) {
        memcpy(request.native_phase_id, phase, phase_length);
        request.native_phase_id[phase_length] = 0;
    }
    memset(&result, 0, sizeof(result));
    result.struct_size = sizeof(result);
    memset(&error, 0, sizeof(error));
    error.struct_size = sizeof(error);
    if (!dluma_helper_host_quest_mutate(script, &request, &result, &error))
        return dluma_helper_push_host_error(state, &error,
            "quest_transition_failed",
            "native quest transition did not complete",
            "quests.native", "quest.write", quest);
    /* The native backend must accept the exact synchronized transition.  A
     * Lua call never falls back to a local phase/objective state machine. */
    if (result.struct_size < sizeof(result) ||
        result.operation != request.operation || !result.changed ||
        result.native_state != DLUMA_LUA54_QUEST_NATIVE_MIRRORED ||
        !dluma_helper_quest_result_identifier_matches(
            result.native_quest_id, sizeof(result.native_quest_id), quest) ||
        (phase_length && !dluma_helper_quest_result_identifier_matches(
            result.native_phase_id, sizeof(result.native_phase_id), phase)))
        return dluma_helper_push_error(state, "native_transition_not_mirrored",
            "native quest transition was not accepted by the exact synchronized route",
            "quests.native", "quest.write", quest, 1);
    lua_pushboolean(state, 1);
    lua_newtable(state);
    dluma_helper_set_field_string(state, "native_state",
        result.native_state == DLUMA_LUA54_QUEST_NATIVE_MIRRORED
            ? "mirrored" :
        result.native_state == DLUMA_LUA54_QUEST_NATIVE_UNAVAILABLE
            ? "unavailable" : "none");
    dluma_helper_set_field_string(state, "native_quest_id",
        result.native_quest_id[0] ? result.native_quest_id : quest);
    dluma_helper_set_field_string(state, "native_phase_id",
        result.native_phase_id[0] ? result.native_phase_id : phase);
    dluma_helper_make_readonly(state);
    return 2;
}

static int dluma_helper_quest_advance_to_phase(lua_State* state)
{
    lua_pushinteger(state, DLUMA_LUA54_QUEST_JUMP_TO_PHASE);
    lua_insert(state, 1);
    return dluma_helper_quest_mutate(state);
}

static int dluma_helper_quest_start_removed(lua_State* state)
{
    return dluma_helper_push_error(state, "removed_operation",
        "quests.start is removed in DLUMA API 1.0 until a separately validated native start route exists",
        "quests.native", "quest.write", "", 0);
}

/* The following report helpers belonged solely to the retired Lua quest
 * presentation cache.  Native quest UI/save state is owned by the game. */
#if 0
static void dluma_helper_quest_copy_field(
    lua_State* state, int table_index, const char* field,
    char* out, size_t capacity)
{
    const char* value = NULL;
    table_index = lua_absindex(state, table_index);
    lua_getfield(state, table_index, field);
    if (lua_type(state, -1) == LUA_TSTRING)
        value = lua_tostring(state, -1);
    dluma_helper_copy(out, capacity, value);
    lua_pop(state, 1);
}

static uint64_t dluma_helper_quest_integer_field(
    lua_State* state, int table_index, const char* field)
{
    lua_Integer value = 0;
    int is_integer = 0;
    table_index = lua_absindex(state, table_index);
    lua_getfield(state, table_index, field);
    value = lua_tointegerx(state, -1, &is_integer);
    lua_pop(state, 1);
    return is_integer && value >= 0 ? (uint64_t)value : 0;
}

static void dluma_helper_quest_append(
    char* out, size_t capacity, const char* value)
{
    size_t used;
    size_t available;
    size_t copied = 0;
    if (!out || !capacity || !value) return;
    used = strlen(out);
    if (used + 1 >= capacity) return;
    available = capacity - used - 1;
    /* Objective text is validated as UTF-8 by the Lua graph.  Preserve that
     * property when a bounded presentation summary ends in the middle of a
     * multi-byte code point: the native UI must never receive a malformed
     * string merely because the summary is full. */
    while (copied < available && value[copied]) {
        unsigned char lead = (unsigned char)value[copied];
        size_t width = 1;
        size_t index;
        if (lead >= 0xC2u && lead <= 0xDFu) width = 2;
        else if (lead >= 0xE0u && lead <= 0xEFu) width = 3;
        else if (lead >= 0xF0u && lead <= 0xF4u) width = 4;
        else if (lead >= 0x80u) break;
        if (copied + width > available) break;
        for (index = 1; index < width; ++index) {
            unsigned char continuation = (unsigned char)value[copied + index];
            if (!continuation || (continuation & 0xC0u) != 0x80u) break;
        }
        if (index != width) break;
        copied += width;
    }
    if (copied) memcpy(out + used, value, copied);
    out[used + copied] = 0;
}

static uint32_t dluma_helper_quest_state_from_text(const char* value)
{
    if (value && strcmp(value, "active") == 0)
        return DLUMA_LUA54_QUEST_STATE_ACTIVE;
    if (value && strcmp(value, "completed") == 0)
        return DLUMA_LUA54_QUEST_STATE_COMPLETED;
    if (value && strcmp(value, "failed") == 0)
        return DLUMA_LUA54_QUEST_STATE_FAILED;
    return DLUMA_LUA54_QUEST_STATE_DECLARED;
}

static void dluma_helper_quest_objective_summary(
    lua_State* state, int quest_index, char* out, size_t capacity)
{
    int objectives;
    size_t count;
    size_t i;
    int wrote = 0;
    if (!out || !capacity) return;
    out[0] = 0;
    quest_index = lua_absindex(state, quest_index);
    lua_getfield(state, quest_index, "objectives");
    if (!lua_istable(state, -1)) {
        lua_pop(state, 1);
        return;
    }
    objectives = lua_absindex(state, -1);
    count = lua_rawlen(state, objectives);
    if (count > 32u) count = 32u;
    for (i = 1; i <= count; ++i) {
        char objective_state[24];
        char objective_text[513];
        lua_rawgeti(state, objectives, (lua_Integer)i);
        if (!lua_istable(state, -1)) {
            lua_pop(state, 1);
            continue;
        }
        dluma_helper_quest_copy_field(state, -1, "state",
                                    objective_state, sizeof(objective_state));
        dluma_helper_quest_copy_field(state, -1, "text",
                                    objective_text, sizeof(objective_text));
        lua_pop(state, 1);
        if (!objective_text[0] || strcmp(objective_state, "hidden") == 0)
            continue;
        if (wrote) dluma_helper_quest_append(out, capacity, "; ");
        if (objective_state[0]) {
            dluma_helper_quest_append(out, capacity, objective_state);
            dluma_helper_quest_append(out, capacity, ": ");
        }
        dluma_helper_quest_append(out, capacity, objective_text);
        wrote = 1;
        if (strlen(out) + 1 >= capacity) break;
    }
    lua_pop(state, 1);
}

static int dluma_helper_quest_notify(lua_State* state)
{
    DlumaHelperScript* script = dluma_helper_from_upvalue(state, 1);
    int event_ref;
    int quest_index;
    int host_only = 0;
    uint32_t i;
    if (!script || !lua_istable(state, 1)) {
        lua_pushboolean(state, 1);
        return 1;
    }
    lua_getfield(state, 1, "quest");
    if (!lua_istable(state, -1)) {
        lua_pop(state, 1);
        lua_pushboolean(state, 1);
        return 1;
    }
    quest_index = lua_absindex(state, -1);
    lua_getfield(state, 1, "host_only");
    host_only = lua_toboolean(state, -1) ? 1 : 0;
    lua_pop(state, 1);
    if (script->host.quest_state_changed) {
        DlumaLua54QuestStateReport report;
        DlumaLua54Error error;
        char status[24];
        char native_state[24];
        memset(&report, 0, sizeof(report));
        report.struct_size = sizeof(report);
        report.map_generation = script->map_generation;
        report.owner_id = script->owner_id;
        report.revision = dluma_helper_quest_integer_field(
            state, quest_index, "revision");
        lua_getfield(state, quest_index, "active");
        report.active = lua_toboolean(state, -1) ? 1u : 0u;
        lua_pop(state, 1);
        dluma_helper_quest_copy_field(state, quest_index, "status",
                                    status, sizeof(status));
        report.state = dluma_helper_quest_state_from_text(status);
        dluma_helper_copy(report.script_id, sizeof(report.script_id), script->id);
        dluma_helper_quest_copy_field(state, quest_index, "id",
                                    report.quest_id, sizeof(report.quest_id));
        dluma_helper_quest_copy_field(state, quest_index, "title",
                                    report.title, sizeof(report.title));
        dluma_helper_quest_copy_field(state, quest_index, "version",
                                    report.version, sizeof(report.version));
        dluma_helper_quest_copy_field(state, quest_index, "phase_id",
                                    report.phase_id, sizeof(report.phase_id));
        lua_getfield(state, quest_index, "phase");
        if (lua_istable(state, -1))
            dluma_helper_quest_copy_field(state, -1, "title",
                                        report.phase_title,
                                        sizeof(report.phase_title));
        lua_pop(state, 1);
        dluma_helper_quest_copy_field(state, quest_index, "native_state",
                                    native_state, sizeof(native_state));
        report.native_state = strcmp(native_state, "mirrored") == 0
            ? DLUMA_LUA54_QUEST_NATIVE_MIRRORED
            : strcmp(native_state, "unavailable") == 0
                ? DLUMA_LUA54_QUEST_NATIVE_UNAVAILABLE
                : DLUMA_LUA54_QUEST_NATIVE_NONE;
        dluma_helper_quest_objective_summary(state, quest_index,
                                           report.objective_summary,
                                           sizeof(report.objective_summary));
        memset(&error, 0, sizeof(error));
        error.struct_size = sizeof(error);
        if (!dluma_helper_host_quest_state_changed(script, &report, &error)) {
            char warning[DLUMA_HELPER_TEXT];
            snprintf(warning, sizeof(warning),
                "quest state report rejected for '%s': %s",
                report.quest_id[0] ? report.quest_id : "unknown",
                error.code[0] ? error.code : "host_adapter_failed");
            dluma_helper_log(script, "warn", warning);
        }
    }
    lua_pop(state, 1); /* quest */
    /* The event table is owned by the quest implementation, but its nested
     * snapshot tables are still mutable.  Copy before invoking user
     * callbacks so an earlier listener cannot alter a later listener's view
     * or the graph's state through a retained table. */
    if (host_only) {
        lua_pushboolean(state, 1);
        return 1;
    }
    dluma_helper_push_deep_readonly_copy(state, 1, 0);
    event_ref = luaL_ref(state, LUA_REGISTRYINDEX);
    for (i = 0; i < DLUMA_HELPER_MAX_CALLBACKS; ++i) {
        DlumaHelperCallback* callback = &script->callbacks[i];
        int result;
        if (!callback->active || callback->kind != DLUMA_HELPER_EVENT_QUEST_CHANGED)
            continue;
        lua_rawgeti(state, LUA_REGISTRYINDEX, callback->ref);
        if (!lua_isfunction(state, -1)) {
            lua_pop(state, 1);
            continue;
        }
        dluma_helper_push_context(state, script);
        lua_rawgeti(state, LUA_REGISTRYINDEX, event_ref);
        ++script->active_callbacks;
        dluma_helper_begin_budget(script);
        result = dluma_helper_pcall(state, 2, 0);
        --script->active_callbacks;
        (void)dluma_helper_finish_protected(script, result, "on_quest_changed");
        if (script->disabled) break;
    }
    luaL_unref(state, LUA_REGISTRYINDEX, event_ref);
    lua_pushboolean(state, 1);
    return 1;
}
#endif

static void dluma_helper_set_function(
    lua_State* state, int table_index, const char* name,
    DlumaHelperScript* script, lua_CFunction function)
{
    table_index = lua_absindex(state, table_index);
    lua_pushlightuserdata(state, script);
    lua_pushcclosure(state, function, 1);
    lua_setfield(state, table_index, name);
}

static void dluma_helper_set_stub(
    lua_State* state, int table_index, const char* name,
    DlumaHelperScript* script, uint32_t capability, uint32_t permission,
    const char* capability_name, const char* permission_name,
    lua_CFunction function)
{
    table_index = lua_absindex(state, table_index);
    dluma_helper_push_gate_closure(state, script,
        function ? function : dluma_helper_stub,
        capability, permission, capability_name, permission_name);
    lua_setfield(state, table_index, name);
}

static void dluma_helper_set_namespace(lua_State* state, int root_index, const char* name)
{
    root_index = lua_absindex(state, root_index);
    dluma_helper_make_readonly(state);
    lua_setfield(state, root_index, name);
}

static int dluma_helper_build_api(DlumaHelperScript* script)
{
    lua_State* state = script->state;
    int root;
    int result;
    luaL_requiref(state, "_G", luaopen_base, 1); lua_pop(state, 1);
    luaL_requiref(state, LUA_TABLIBNAME, luaopen_table, 1); lua_pop(state, 1);
    luaL_requiref(state, LUA_STRLIBNAME, luaopen_string, 1); lua_pop(state, 1);
    luaL_requiref(state, LUA_MATHLIBNAME, luaopen_math, 1); lua_pop(state, 1);
    luaL_requiref(state, LUA_UTF8LIBNAME, luaopen_utf8, 1); lua_pop(state, 1);
    luaL_requiref(state, LUA_COLIBNAME, luaopen_coroutine, 1); lua_pop(state, 1);
    dluma_helper_create_object_metatable(state);
    dluma_helper_create_quest_hud_override_metatable(state);

    /* No file/package/debug/OS surface and no dynamic code-loading helpers. */
    {
        static const char* removed[] = {
            "dofile", "loadfile", "load", "collectgarbage", "print", "warn",
            "rawget", "rawset", "getmetatable", "setmetatable"
        };
        size_t i;
        for (i = 0; i < sizeof(removed) / sizeof(removed[0]); ++i) {
            lua_pushnil(state);
            lua_setglobal(state, removed[i]);
        }
        lua_pushnil(state); lua_setglobal(state, "package");
        lua_pushnil(state); lua_setglobal(state, "io");
        lua_pushnil(state); lua_setglobal(state, "os");
        lua_pushnil(state); lua_setglobal(state, "debug");
    }
    lua_newtable(state);
    script->module_cache_ref =
        luaL_ref(state, LUA_REGISTRYINDEX);
    lua_pushlightuserdata(state, script);
    lua_pushcclosure(state, dluma_helper_require, 1);
    lua_setglobal(state, "require");

    lua_newtable(state);
    root = lua_absindex(state, -1);
    dluma_helper_set_function(state, root, "api_version", script, dluma_helper_api_version);
    dluma_helper_set_function(state, root, "loader_version", script, dluma_helper_loader_version);
    dluma_helper_set_function(state, root, "capability", script, dluma_helper_capability);
    dluma_helper_set_function(state, root, "current_map", script, dluma_helper_current_map);
    dluma_helper_set_function(state, root, "current_project", script, dluma_helper_current_project);
    dluma_helper_set_function(state, root, "script_scope", script, dluma_helper_script_scope);
    dluma_helper_set_function(state, root, "log", script, dluma_helper_log_api);
    dluma_helper_set_function(state, root, "mod", script, dluma_helper_mod);
    dluma_helper_set_function(state, root, "_rule_every", script, dluma_helper_rule_every);
    dluma_helper_set_function(state, root, "_source_fingerprint", script,
        dluma_helper_source_fingerprint);

    lua_newtable(state);
    dluma_helper_set_function(
        state, -1, "monotonic_ms", script, dluma_helper_clock_ms);
    dluma_helper_set_namespace(state, root, "clock");

    lua_newtable(state);
    dluma_helper_set_function(state, -1, "after", script, dluma_helper_timer_after);
    dluma_helper_set_function(state, -1, "every", script, dluma_helper_timer_every);
    dluma_helper_set_function(state, -1, "cancel", script, dluma_helper_timer_cancel);
    dluma_helper_set_namespace(state, root, "timer");

    lua_newtable(state);
    dluma_helper_set_function(state, -1, "get", script, dluma_helper_storage_get);
    dluma_helper_set_function(state, -1, "set", script, dluma_helper_storage_set);
    dluma_helper_set_function(state, -1, "delete", script, dluma_helper_storage_delete);
    dluma_helper_set_namespace(state, root, "storage");

    lua_newtable(state);
    dluma_helper_set_function(
        state, -1, "open", script, dluma_helper_companion_open);
    dluma_helper_set_function(
        state, -1, "read_entry", script,
        dluma_helper_companion_read_entry);
    dluma_helper_make_readonly(state);
    /* Keep the API 1.0 companion alias while publishing the fully named
     * language-neutral domain used by generated clients. */
    lua_pushvalue(state, -1);
    lua_setfield(state, root, "companion");
    lua_setfield(state, root, "companion_content");

    lua_newtable(state);
    dluma_helper_set_function(
        state, -1, "get", script, dluma_helper_config_get);
    dluma_helper_set_namespace(state, root, "configuration");

    lua_newtable(state);
    dluma_helper_set_stub(state, -1, "find", script,
        DLUMA_LUA54_CAP_WORLD_FIND, DLUMA_LUA54_PERMISSION_WORLD_QUERY,
        "world.find", "world.query", dluma_helper_world_find);
    dluma_helper_set_stub(state, -1, "set_enabled_many", script,
        DLUMA_LUA54_CAP_WORLD_LOCAL_ENABLE, DLUMA_LUA54_PERMISSION_WORLD_WRITE,
        "world.local_enable", "world.write", dluma_helper_world_set_enabled_many);
    dluma_helper_set_namespace(state, root, "world");

    lua_newtable(state);
    {
        int events = lua_absindex(state, -1);
        lua_newtable(state);
        dluma_helper_set_stub(state, -1, "is_inside", script,
            DLUMA_LUA54_CAP_EVENTS_SENSOR,
            DLUMA_LUA54_PERMISSION_EVENTS_SENSOR,
            "events.sensor", "events.sensor",
            dluma_helper_sensor_is_inside);
        dluma_helper_set_namespace(state, events, "sensor");
    }
    dluma_helper_set_namespace(state, root, "events");

    lua_newtable(state);
    lua_pushvalue(state, -1);
    lua_setfield(state, root, "player");
    memset(&script->player_bridge, 0, sizeof(script->player_bridge));
    script->player_bridge.state = state;
    script->player_bridge.host = &script->host;
    script->player_bridge.host_user = script->host_user;
    script->player_bridge.scope = script->scope;
    script->player_bridge.map_generation = script->map_generation;
    script->player_bridge.owner_id = script->owner_id;
    script->player_bridge.active = 1U;
    dluma_lua54_player_bridge_register(
        state, &script->player_bridge, root);
    dluma_helper_set_function(
        state, -1, "vehicle_state", script,
        dluma_helper_player_vehicle_state);
    dluma_helper_set_namespace(state, root, "player");

    lua_newtable(state);
    dluma_helper_set_stub(state, -1, "toast", script,
        DLUMA_LUA54_CAP_UI_OVERLAY, DLUMA_LUA54_PERMISSION_UI_OVERLAY,
        "ui.overlay", "ui.overlay", NULL);
    dluma_helper_set_stub(state, -1, "send_subtitle", script,
        DLUMA_LUA54_CAP_UI_SUBTITLES, DLUMA_LUA54_PERMISSION_UI_OVERLAY,
        "ui.subtitles", "ui.overlay", dluma_helper_ui_send_subtitle);
    dluma_helper_set_stub(state, -1, "update_subtitle", script,
        DLUMA_LUA54_CAP_UI_SUBTITLES, DLUMA_LUA54_PERMISSION_UI_OVERLAY,
        "ui.subtitles", "ui.overlay", dluma_helper_ui_update_subtitle);
    dluma_helper_set_stub(state, -1, "quest_hud", script,
        DLUMA_LUA54_CAP_UI_QUEST_HUD, DLUMA_LUA54_PERMISSION_GAME_READ,
        "ui.quest_hud", "game.read", dluma_helper_ui_quest_hud);
    dluma_helper_set_stub(state, -1, "override_quest_hud", script,
        DLUMA_LUA54_CAP_UI_QUEST_HUD, DLUMA_LUA54_PERMISSION_UI_OVERLAY,
        "ui.quest_hud", "ui.overlay",
        dluma_helper_ui_override_quest_hud);
    dluma_helper_set_namespace(state, root, "ui");

    lua_newtable(state);
    dluma_helper_set_stub(state, -1, "play", script,
        DLUMA_LUA54_CAP_AUDIO_REGISTERED_CUES, DLUMA_LUA54_PERMISSION_AUDIO_PLAY,
        "audio.registered_cues", "audio.play", dluma_helper_audio_stub);
    dluma_helper_set_stub(state, -1, "stop", script,
        DLUMA_LUA54_CAP_AUDIO_REGISTERED_CUES, DLUMA_LUA54_PERMISSION_AUDIO_PLAY,
        "audio.registered_cues", "audio.play", NULL);
    dluma_helper_set_stub(state, -1, "play_radio_dialog", script,
        DLUMA_LUA54_CAP_AUDIO_RADIO_DIALOG, DLUMA_LUA54_PERMISSION_AUDIO_PLAY,
        "audio.radio_dialog", "audio.play",
        dluma_helper_audio_play_radio_dialog);
    {
        int audio = lua_absindex(state, -1);
        lua_newtable(state);
        dluma_helper_set_function(
            state, -1, "open", script, dluma_helper_audio_bank_open);
        dluma_helper_set_namespace(state, audio, "bank");

        lua_newtable(state);
        dluma_helper_set_function(
            state, -1, "duration", script,
            dluma_helper_audio_cue_duration);
        dluma_helper_set_function(
            state, -1, "play", script, dluma_helper_audio_cue_play);
        dluma_helper_set_function(
            state, -1, "state", script, dluma_helper_audio_cue_state);
        dluma_helper_set_function(
            state, -1, "seek", script, dluma_helper_audio_cue_seek);
        dluma_helper_set_function(
            state, -1, "set_gain", script,
            dluma_helper_audio_cue_gain);
        dluma_helper_set_function(
            state, -1, "set_filter", script,
            dluma_helper_audio_cue_filter);
        dluma_helper_set_function(
            state, -1, "stop", script, dluma_helper_audio_cue_stop);
        dluma_helper_set_namespace(state, audio, "cue");

        lua_newtable(state);
        dluma_helper_set_function(
            state, -1, "acquire", script,
            dluma_helper_audio_music_acquire);
        dluma_helper_set_function(
            state, -1, "state", script,
            dluma_helper_audio_music_state);
        dluma_helper_set_function(
            state, -1, "set_filter", script,
            dluma_helper_audio_music_filter);
        dluma_helper_set_function(
            state, -1, "release", script,
            dluma_helper_audio_music_release);
        dluma_helper_set_namespace(state, audio, "music");
    }
    dluma_helper_set_namespace(state, root, "audio");

    lua_newtable(state);
    dluma_helper_set_stub(state, -1, "run_chain", script,
        DLUMA_LUA54_CAP_ACTION_CHAIN, DLUMA_LUA54_PERMISSION_WORLD_WRITE,
        "actions.chain", "world.write", dluma_helper_action_run_chain);
    dluma_helper_set_namespace(state, root, "actions");

    /* Native-only quest control. IDs must refer to a pre-authored quest in
     * the active custom-map project. The production backend binds only the
     * exact DevTools QuestManager synchronized transition route. */
    lua_newtable(state);
    dluma_helper_set_function(
        state, -1, "start", script, dluma_helper_quest_start_removed);
    dluma_helper_set_stub(state, -1, "advance_to_phase", script,
        DLUMA_LUA54_CAP_QUEST_NATIVE_CONTROL,
        DLUMA_LUA54_PERMISSION_QUEST_WRITE,
        "quests.native", "quest.write", dluma_helper_quest_advance_to_phase);
    dluma_helper_set_namespace(state, root, "quests");

    /* The embedded standard library uses the same public dluma surface as an
     * authored script.  Publish the still-mutable root before executing it;
     * it is frozen only after std, cond, and the private per-mod installer
     * have been attached below. */
    lua_pushvalue(state, root);
    lua_setglobal(state, "dluma");
    result = luaL_loadbufferx(state, g_dluma_lua54_stdlib_source,
                              g_dluma_lua54_stdlib_source_size,
                              "=dluma.stdlib", "t");
    if (result == LUA_OK) {
        dluma_helper_begin_budget(script);
        result = dluma_helper_pcall(state, 0, 1);
        if (!dluma_helper_finish_protected(script, result, "dluma.stdlib")) return 0;
        if (!lua_istable(state, -1)) {
            dluma_helper_record_error(script, "dluma.stdlib",
                "standard library did not return its export table");
            lua_pop(state, 1);
            return 0;
        }
        lua_getfield(state, -1, "std");
        if (!lua_istable(state, -1)) {
            dluma_helper_record_error(script, "dluma.stdlib", "missing std export");
            lua_pop(state, 2);
            return 0;
        }
        dluma_helper_make_readonly(state);
        lua_setfield(state, root, "std");
        lua_getfield(state, -1, "cond");
        if (!lua_istable(state, -1)) {
            dluma_helper_record_error(script, "dluma.stdlib", "missing cond export");
            lua_pop(state, 2);
            return 0;
        }
        dluma_helper_make_readonly(state);
        lua_setfield(state, root, "cond");
        lua_getfield(state, -1, "flow");
        if (!lua_istable(state, -1)) {
            dluma_helper_record_error(script, "dluma.stdlib", "missing flow export");
            lua_pop(state, 2);
            return 0;
        }
        dluma_helper_make_readonly(state);
        lua_setfield(state, root, "flow");
        lua_getfield(state, -1, "install_mod");
        if (!lua_isfunction(state, -1)) {
            dluma_helper_record_error(script, "dluma.stdlib", "missing mod installer");
            lua_pop(state, 2);
            return 0;
        }
        script->stdlib_install_mod_ref = luaL_ref(state, LUA_REGISTRYINDEX);
        lua_pop(state, 1);
    } else {
        dluma_helper_record_error(script, "dluma.stdlib", lua_tostring(state, -1));
        lua_pop(state, 1);
        return 0;
    }

    /* The embedded library consumes these private bootstrap helpers while it
     * is initialized.  Keeping them out of the frozen authored root avoids
     * accidentally advertising an internal source fingerprint or installer
     * as a supported Lua capability. */
    lua_pushnil(state);
    lua_setfield(state, root, "_source_fingerprint");
    lua_pushnil(state);
    lua_setfield(state, root, "_rule_every");
    lua_pushvalue(state, root);
    dluma_helper_make_readonly(state);
    lua_setglobal(state, "dluma");
    /* DLUMA API 1.0 is a clean namespace break. Never publish the old root. */
    lua_pushnil(state);
    lua_setglobal(state, "dlua");
    lua_pop(state, 1);
    return 1;
}

static void dluma_helper_service_timers(DlumaHelperScript* script, uint64_t now)
{
    uint32_t i;
    for (i = 0; i < DLUMA_HELPER_MAX_TIMERS; ++i) {
        DlumaHelperTimer* timer = &script->timers[i];
        int ref;
        int repeating;
        uint32_t id;
        if (!timer->active || now < timer->due_ms) continue;
        ref = timer->ref;
        repeating = timer->repeating;
        id = timer->id;
        if (repeating)
            timer->due_ms = now + (uint64_t)(timer->period_seconds * 1000.0);
        dluma_helper_call_ref(
            script, ref, "timer", 0, NULL, 0.0, 0,
            NULL, NULL);
        if (!repeating) {
            uint32_t current;
            /*
             * Retire only the same stable timer ID. The callback may cancel
             * itself and create a replacement in the same array slot.
             */
            for (current = 0;
                 current < DLUMA_HELPER_MAX_TIMERS;
                 ++current) {
                DlumaHelperTimer* candidate =
                    &script->timers[current];
                if (!candidate->active ||
                    candidate->id != id)
                    continue;
                luaL_unref(
                    script->state, LUA_REGISTRYINDEX,
                    candidate->ref);
                if (candidate->is_rule &&
                    script->rule_count)
                    --script->rule_count;
                if (script->timer_count)
                    --script->timer_count;
                memset(candidate, 0, sizeof(*candidate));
                break;
            }
        }
        if (script->disabled) return;
    }
}

/* Native sensor polling keeps its first sample as a baseline so the general
 * edge stream never invents an enter at map start.  Quest/gameplay listeners
 * nevertheless need to know when the player loaded inside their area.  This
 * separate helper-side service is deliberately game-thread-only: it retries
 * the bounded CheckInside query until it succeeds, then dispatches one copied
 * synthetic enter to each still-pending listener.  A real queued enter clears
 * the same pending bit before this service can duplicate it. */
static void dluma_helper_service_sensor_initial_occupancy(DlumaHelperScript* script)
{
    uint32_t i;
    if (!script || !script->host.sensor_is_inside || script->disabled) return;
    for (i = 0; i < DLUMA_HELPER_MAX_CALLBACKS; ++i) {
        DlumaHelperCallback* source = &script->callbacks[i];
        char key[sizeof(source->key)];
        uint32_t tokens[DLUMA_HELPER_MAX_CALLBACKS];
        uint32_t token_count = 0;
        uint32_t j;
        int already_queried = 0;
        int inside = 0;
        DlumaLua54Error error;
        if (!source->active ||
            source->kind != DLUMA_LUA54_EVENT_SENSOR_ENTER ||
            !source->initial_occupancy_pending || !source->key[0])
            continue;
        dluma_helper_copy(key, sizeof(key), source->key);
        for (j = 0; j < i; ++j) {
            const DlumaHelperCallback* earlier = &script->callbacks[j];
            if (earlier->active && earlier->initial_occupancy_pending &&
                earlier->kind == DLUMA_LUA54_EVENT_SENSOR_ENTER &&
                strcmp(earlier->key, key) == 0) {
                already_queried = 1;
                break;
            }
        }
        if (already_queried) continue;
        memset(&error, 0, sizeof(error));
        error.struct_size = sizeof(error);
        if (!dluma_helper_host_sensor_is_inside(script, key, &inside, &error))
            continue; /* Index/lifecycle misses remain retryable; do not log-spam. */
        for (j = 0; j < DLUMA_HELPER_MAX_CALLBACKS; ++j) {
            DlumaHelperCallback* listener = &script->callbacks[j];
            if (!listener->active || !listener->initial_occupancy_pending ||
                listener->kind != DLUMA_LUA54_EVENT_SENSOR_ENTER ||
                strcmp(listener->key, key) != 0)
                continue;
            listener->initial_occupancy_pending = 0;
            tokens[token_count++] = listener->token;
        }
        if (!inside) continue;
        for (j = 0; j < token_count; ++j) {
            uint32_t k;
            for (k = 0; k < DLUMA_HELPER_MAX_CALLBACKS; ++k) {
                DlumaHelperCallback* listener = &script->callbacks[k];
                if (!listener->active || listener->token != tokens[j] ||
                    listener->kind != DLUMA_LUA54_EVENT_SENSOR_ENTER)
                    continue;
                (void)dluma_helper_call_ref(
                    script, listener->ref, key,
                    (int)DLUMA_LUA54_EVENT_SENSOR_ENTER,
                    key, 0.0, 1, NULL, NULL);
                break;
            }
            if (script->disabled) return;
        }
    }
}

static void dluma_helper_reset_sensor_initial_occupancy(DlumaHelperScript* script)
{
    uint32_t i;
    if (!script) return;
    for (i = 0; i < DLUMA_HELPER_MAX_CALLBACKS; ++i) {
        DlumaHelperCallback* callback = &script->callbacks[i];
        if (callback->active &&
            callback->kind == DLUMA_LUA54_EVENT_SENSOR_ENTER)
            callback->initial_occupancy_pending = 1;
    }
}

static int dluma_helper_dispatch_callbacks(DlumaHelperScript* script, const DlumaLua54Event* event)
{
    uint32_t tokens[DLUMA_HELPER_MAX_CALLBACKS];
    uint32_t token_count = 0;
    uint32_t i;
    int successes = 0;
    if (event && event->kind == DLUMA_LUA54_EVENT_SENSOR_ENTER &&
        event->name && event->name[0]) {
        for (i = 0; i < DLUMA_HELPER_MAX_CALLBACKS; ++i) {
            DlumaHelperCallback* callback = &script->callbacks[i];
            if (callback->active &&
                callback->kind == DLUMA_LUA54_EVENT_SENSOR_ENTER &&
                strcmp(callback->key, event->name) == 0)
                callback->initial_occupancy_pending = 0;
        }
    }
    /*
     * Snapshot stable tokens before entering Lua. A callback may remove
     * itself or another callback and may register a replacement into the
     * vacated slot. The replacement has a new token and must not observe the
     * event currently being dispatched.
     */
    for (i = 0; i < DLUMA_HELPER_MAX_CALLBACKS; ++i) {
        const DlumaHelperCallback* callback =
            &script->callbacks[i];
        if (!callback->active ||
            callback->kind != event->kind)
            continue;
        if (callback->key[0] &&
            (!event->name ||
             strcmp(callback->key, event->name) != 0))
            continue;
        tokens[token_count++] = callback->token;
    }
    for (i = 0; i < token_count; ++i) {
        uint32_t slot_index;
        for (slot_index = 0;
             slot_index < DLUMA_HELPER_MAX_CALLBACKS;
             ++slot_index) {
            DlumaHelperCallback* callback =
                &script->callbacks[slot_index];
            if (!callback->active ||
                callback->token != tokens[i] ||
                callback->kind != event->kind)
                continue;
            if (event->kind ==
                    DLUMA_LUA54_EVENT_QUEST_PHASE_PROVIDER ||
                event->kind ==
                    DLUMA_LUA54_EVENT_QUEST_PHASE_OBSERVER) {
                successes += dluma_helper_call_quest_phase_ref(
                    script, callback->ref,
                    callback->key[0]
                        ? callback->key
                        : script->last_event,
                    &event->quest_phase);
            } else {
                successes += dluma_helper_call_ref(
                    script, callback->ref,
                    callback->key[0]
                        ? callback->key
                        : script->last_event,
                    (int)event->kind, event->name,
                    event->number, event->state,
                    event->kind ==
                            DLUMA_LUA54_EVENT_ENEMY_KILLED
                        ? &event->enemy_killed
                        : NULL,
                    event->kind ==
                            DLUMA_LUA54_EVENT_CONFIG_CHANGED
                        ? &event->config_changed
                        : NULL);
            }
            break;
        }
        if (script->disabled) break;
    }
    return successes;
}

static void* dluma_helper_create_script(const DlumaLua54ScriptConfig* config)
{
    DlumaHelperScript* script;
    if (!config || config->struct_size < sizeof(*config) || !config->id ||
        !config->name || !config->version || !config->file_path ||
        config->host.struct_size < sizeof(config->host) ||
        !config->host.clock_ms || !config->host.log ||
        !config->host.capabilities ||
        !config->host.capability_query ||
        !config->host.api_invoke ||
        !config->host.config_get ||
        !config->host.module_load ||
        !config->host.module_release)
        return NULL;
    script = (DlumaHelperScript*)calloc(1, sizeof(*script));
    if (!script) return NULL;
    script->stdlib_install_mod_ref = LUA_NOREF;
    script->module_cache_ref = LUA_NOREF;
    script->budget_qpc_frequency = dluma_helper_query_frequency();
    if (!script->budget_qpc_frequency) {
        free(script);
        return NULL;
    }
    script->scope = config->scope;
    script->map_generation = config->map_generation;
    script->owner_id = config->owner_id;
    script->approved_permissions = config->approved_permissions;
    script->capabilities = config->capabilities | DLUMA_LUA54_CAP_LUA_RUNTIME;
    script->source_fingerprint = config->source_fingerprint;
    script->memory_limit = config->memory_limit_bytes;
    if (script->memory_limit < 256u * 1024u) script->memory_limit = 256u * 1024u;
    if (script->memory_limit > 64u * 1024u * 1024u) script->memory_limit = 64u * 1024u * 1024u;
    script->instruction_limit = config->instruction_limit;
    if (script->instruction_limit < 10000u) script->instruction_limit = 10000u;
    if (script->instruction_limit > 10000000u) script->instruction_limit = 10000000u;
    script->wall_clock_limit_ms = config->wall_clock_limit_ms;
    if (script->wall_clock_limit_ms < 1) script->wall_clock_limit_ms = 1;
    if (script->wall_clock_limit_ms > 100) script->wall_clock_limit_ms = 100;
    script->pump_instruction_limit =
        script->instruction_limit *
        DLUMA_HELPER_PUMP_BUDGET_MULTIPLIER;
    script->pump_instruction_remaining =
        script->pump_instruction_limit;
    script->pump_lua_us_limit =
        (uint64_t)script->wall_clock_limit_ms * 1000u *
        DLUMA_HELPER_PUMP_BUDGET_MULTIPLIER;
    script->pump_lua_us_consumed = 0;
    script->pump_budget_active = 1;
    script->pump_budget_exhausted = 0;
    script->max_callbacks = config->max_callbacks ? config->max_callbacks : 48;
    if (script->max_callbacks > DLUMA_HELPER_MAX_CALLBACKS)
        script->max_callbacks = DLUMA_HELPER_MAX_CALLBACKS;
    script->max_timers = config->max_timers ? config->max_timers : 32;
    if (script->max_timers > DLUMA_HELPER_MAX_TIMERS)
        script->max_timers = DLUMA_HELPER_MAX_TIMERS;
    script->max_rules = config->max_rules ? config->max_rules : 16;
    if (script->max_rules > DLUMA_HELPER_MAX_RULES)
        script->max_rules = DLUMA_HELPER_MAX_RULES;
    script->max_log_messages_per_second = config->max_log_messages_per_second
        ? config->max_log_messages_per_second : 20;
    if (script->max_log_messages_per_second > 100)
        script->max_log_messages_per_second = 100;
    script->host = config->host;
    script->host_user = config->host_user;
    script->is_editor = config->is_editor;
    script->is_player = config->is_player;
    script->alive = 1;
    dluma_helper_copy(script->id, sizeof(script->id), config->id);
    dluma_helper_copy(script->name, sizeof(script->name), config->name);
    dluma_helper_copy(script->version, sizeof(script->version), config->version);
    dluma_helper_copy(script->permissions, sizeof(script->permissions), config->permissions);
    dluma_helper_copy(script->file_path, sizeof(script->file_path), config->file_path);
    dluma_helper_copy(script->project, sizeof(script->project), config->project);
    dluma_helper_copy(script->map, sizeof(script->map), config->map);
    dluma_helper_copy(script->workshop_id, sizeof(script->workshop_id), config->workshop_id);
    dluma_helper_copy(script->loader_version, sizeof(script->loader_version), config->loader_version);
    script->state = lua_newstate(dluma_helper_allocator, script);
    if (!script->state) {
        free(script);
        return NULL;
    }
    *(DlumaHelperScript**)lua_getextraspace(script->state) = script;
    lua_atpanic(script->state, dluma_helper_panic);
    script->panic_guard_active = 1;
    if (setjmp(script->panic_jump) != 0) {
        script->panic_guard_active = 0;
        return script;
    }
    if (!dluma_helper_build_api(script)) {
        lua_close(script->state);
        script->state = NULL;
        script->panic_guard_active = 0;
        free(script);
        return NULL;
    }
    script->panic_guard_active = 0;
    return script;
}

static int dluma_helper_load_script(void* handle, const char* source, size_t source_size)
{
    DlumaHelperScript* script = (DlumaHelperScript*)handle;
    int result;
    int loaded = 0;
    char chunk_name[1100];
    if (!script || !script->state || !source || script->disabled) return 0;
    script->panic_guard_active = 1;
    if (setjmp(script->panic_jump) != 0) {
        script->panic_guard_active = 0;
        return 0;
    }
    snprintf(chunk_name, sizeof(chunk_name), "@%s", script->file_path);
    result = luaL_loadbufferx(script->state, source, source_size, chunk_name, "t");
    if (result != LUA_OK) {
        dluma_helper_record_error(script, "chunk_load", lua_tostring(script->state, -1));
        lua_pop(script->state, 1);
        script->disabled = 1;
        goto done;
    }
    ++script->active_callbacks;
    dluma_helper_begin_budget(script);
    result = dluma_helper_pcall(script->state, 0, 0);
    --script->active_callbacks;
    if (!dluma_helper_finish_protected(script, result, "chunk_init")) {
        script->disabled = 1;
        goto done;
    }
    if (!script->declaration_seen) {
        dluma_helper_record_error(
            script, "chunk_init",
            "script did not call local mod = dluma.mod()");
        script->disabled = 1;
        goto done;
    }
    loaded = 1;
done:
    script->panic_guard_active = 0;
    return loaded;
}

static int dluma_helper_dispatch_impl(void* handle, const DlumaLua54Event* event)
{
    DlumaHelperScript* script = (DlumaHelperScript*)handle;
    double delta;
    if (!script || !event || event->struct_size < sizeof(*event) || !script->alive)
        return 0;
    if (event->kind == DLUMA_LUA54_EVENT_CONFIG_CHANGED &&
        (event->config_changed.struct_size <
             sizeof(event->config_changed) ||
         event->config_changed.reserved ||
         !event->config_changed.key[0] ||
         event->config_changed
                 .key[DLUMA_LUA54_MAX_CONFIG_KEY] ||
         !dluma_helper_config_key_valid(
             event->config_changed.key,
             strlen(event->config_changed.key)) ||
         event->config_changed
                 .old_value[DLUMA_LUA54_MAX_CONFIG_VALUE] ||
         event->config_changed
                 .value[DLUMA_LUA54_MAX_CONFIG_VALUE] ||
         !dluma_helper_config_scalar_valid(
             event->config_changed.old_value_type,
             event->config_changed.old_value) ||
         !dluma_helper_config_scalar_valid(
             event->config_changed.value_type,
             event->config_changed.value)))
        return 0;
    if ((event->kind == DLUMA_LUA54_EVENT_QUEST_PHASE_PROVIDER ||
         event->kind == DLUMA_LUA54_EVENT_QUEST_PHASE_OBSERVER) &&
        (event->quest_phase.struct_size <
             sizeof(event->quest_phase) ||
         !event->name || !event->name[0]))
        return 0;
    if (event->project) dluma_helper_copy(script->project, sizeof(script->project), event->project);
    if (event->map) dluma_helper_copy(script->map, sizeof(script->map), event->map);
    script->map_generation = event->map_generation;
    script->player_bridge.map_generation = event->map_generation;
    dluma_helper_copy(script->last_event, sizeof(script->last_event),
        event->name && event->name[0] ? event->name :
        event->kind == DLUMA_LUA54_EVENT_LOAD ? "on_load" :
        event->kind == DLUMA_LUA54_EVENT_UNLOAD ? "on_unload" :
        event->kind == DLUMA_LUA54_EVENT_MAP_LOAD ? "on_map_load" :
        event->kind == DLUMA_LUA54_EVENT_MAP_UNLOAD ? "on_map_unload" :
        event->kind == DLUMA_LUA54_EVENT_UPDATE ? "on_update" :
        event->kind == DLUMA_LUA54_EVENT_CONFIG_CHANGED
            ? "on_config_changed" :
        event->kind == DLUMA_LUA54_EVENT_QUEST_PHASE_PROVIDER
            ? "handle_quest_phase" :
        event->kind == DLUMA_LUA54_EVENT_QUEST_PHASE_OBSERVER
            ? "observe_quest_phase" : "event");
    if (script->disabled && event->kind != DLUMA_LUA54_EVENT_UNLOAD) return 0;
    if (event->kind == DLUMA_LUA54_EVENT_MAP_LOAD) {
        dluma_helper_reset_sensor_initial_occupancy(script);
    }
    if (event->kind == DLUMA_LUA54_EVENT_UPDATE) {
        dluma_helper_service_timers(script, dluma_helper_now(script));
        dluma_helper_service_sensor_initial_occupancy(script);
        if (script->disabled || !script->has_update_callback) return 0;
        delta = event->number;
        if (!isfinite(delta) || delta < 0) delta = 0;
        if (delta > 0.25) delta = 0.25;
        {
            DlumaLua54Event clamped = *event;
            clamped.number = delta;
            return dluma_helper_dispatch_callbacks(script, &clamped);
        }
    }
    return dluma_helper_dispatch_callbacks(script, event);
}

static int dluma_helper_dispatch(void* handle, const DlumaLua54Event* event)
{
    DlumaHelperScript* script = (DlumaHelperScript*)handle;
    int result;
    if (!script || !script->state || script->panic_raised) return 0;
    script->panic_guard_active = 1;
    if (setjmp(script->panic_jump) != 0) {
        script->panic_guard_active = 0;
        return 0;
    }
    result = dluma_helper_dispatch_impl(handle, event);
    script->panic_guard_active = 0;
    return result;
}

static int dluma_helper_begin_pump(void* handle)
{
    DlumaHelperScript* script = (DlumaHelperScript*)handle;
    if (!script || !script->state || !script->alive ||
        script->panic_raised || script->active_callbacks)
        return 0;
    script->pump_instruction_remaining =
        script->pump_instruction_limit;
    script->pump_lua_us_consumed = 0;
    script->pump_budget_active = 1;
    script->pump_budget_exhausted = 0;
    return 1;
}

static int dluma_helper_get_status(void* handle, DlumaLua54Status* status)
{
    DlumaHelperScript* script = (DlumaHelperScript*)handle;
    if (!script || !status || status->struct_size < sizeof(*status)) return 0;
    status->alive = (uint32_t)script->alive;
    status->disabled = (uint32_t)script->disabled;
    status->declaration_seen = (uint32_t)script->declaration_seen;
    status->has_update_callback = (uint32_t)script->has_update_callback;
    status->active_callbacks = script->active_callbacks;
    status->budget_violations = script->budget_violations;
    status->memory_used_bytes = script->memory_used;
    status->source_fingerprint = script->source_fingerprint;
    status->last_callback_lua_us = script->last_callback_lua_us;
    status->last_callback_host_excluded_us =
        script->last_callback_host_excluded_us;
    status->last_instruction_count_or_remaining =
        script->last_instruction_count_or_remaining;
    dluma_helper_copy(status->last_event, sizeof(status->last_event), script->last_event);
    dluma_helper_copy(status->budget_type, sizeof(status->budget_type), script->budget_type);
    dluma_helper_copy(status->last_error, sizeof(status->last_error), script->last_error);
    return 1;
}

static int dluma_helper_destroy_script_impl(void* handle)
{
    DlumaHelperScript* script = (DlumaHelperScript*)handle;
    if (!script) return 1;
    if (script->panic_raised) return 0;
    script->destroy_pending = 1;
    script->alive = 0;
    if (script->active_callbacks) {
        return 0;
    }
    if (script->state) {
        uint32_t i;
        int removal_failed = 0;
        /* Native registrations are removed in a first phase while every Lua
         * reference and callback record is still intact.  Successful removals
         * are remembered so a failed teardown can be retried safely without
         * asking the native registry to remove the same token twice. */
        for (i = 0; i < DLUMA_HELPER_MAX_CALLBACKS; ++i) {
            DlumaHelperCallback* callback = &script->callbacks[i];
            if (!callback->active || !callback->native_registered) continue;
            if (script->host.subscription_removed) {
                DlumaLua54Error ignored;
                memset(&ignored, 0, sizeof(ignored));
                ignored.struct_size = sizeof(ignored);
                if (dluma_helper_host_subscription_removed(
                        script, callback->token, callback->kind,
                        callback->key, &ignored)) {
                    callback->native_registered = 0;
                    continue;
                }
            }
            removal_failed = 1;
        }
        if (removal_failed) {
            dluma_helper_copy(
                script->last_error, sizeof(script->last_error),
                "state_teardown: native subscription removal failed");
            return 0;
        }
        for (i = 0; i < DLUMA_HELPER_MAX_CALLBACKS; ++i) {
            DlumaHelperCallback* callback = &script->callbacks[i];
            if (!callback->active) continue;
            luaL_unref(script->state, LUA_REGISTRYINDEX, callback->ref);
            memset(callback, 0, sizeof(*callback));
        }
    }
    if (script->state) {
        dluma_helper_host_quest_hud_clear_owner(script);
        dluma_lua54_player_bridge_teardown(
            script->state, &script->player_bridge);
        if (script->stdlib_install_mod_ref != LUA_NOREF)
            luaL_unref(script->state, LUA_REGISTRYINDEX,
                script->stdlib_install_mod_ref);
        if (script->module_cache_ref != LUA_NOREF)
            luaL_unref(
                script->state, LUA_REGISTRYINDEX,
                script->module_cache_ref);
        lua_close(script->state);
    }
    memset(script, 0, sizeof(*script));
    free(script);
    return 1;
}

static int dluma_helper_destroy_script(void* handle)
{
    DlumaHelperScript* script = (DlumaHelperScript*)handle;
    int result;
    if (!script) return 1;
    if (script->panic_raised) return 0;
    script->panic_guard_active = 1;
    if (setjmp(script->panic_jump) != 0) {
        script->panic_guard_active = 0;
        return 0;
    }
    result = dluma_helper_destroy_script_impl(handle);
    if (!result) script->panic_guard_active = 0;
    return result;
}

#if defined(_WIN32)
#define DLUMA_HELPER_EXPORT __declspec(dllexport)
#else
#define DLUMA_HELPER_EXPORT
#endif

DLUMA_HELPER_EXPORT int DLUMA_Lua54_GetApi(
    uint32_t requested_abi,
    DlumaLua54Api* out_api,
    uint32_t out_size)
{
    if (requested_abi != DLUMA_LUA54_ABI_VERSION || !out_api ||
        out_size < sizeof(*out_api))
        return 0;
    memset(out_api, 0, sizeof(*out_api));
    out_api->struct_size = sizeof(*out_api);
    out_api->abi_version = DLUMA_LUA54_ABI_VERSION;
    out_api->release = LUA_RELEASE;
    out_api->create_script = dluma_helper_create_script;
    out_api->load_script = dluma_helper_load_script;
    out_api->dispatch = dluma_helper_dispatch;
    out_api->get_status = dluma_helper_get_status;
    out_api->destroy_script = dluma_helper_destroy_script;
    out_api->begin_pump = dluma_helper_begin_pump;
    return 1;
}
