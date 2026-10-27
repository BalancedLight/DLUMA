#include "../src/dlw_common.h"
#include "../src/dlw_native_gameplay.h"

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FAKE_INLINE_OBJECTS 16u
#define FAKE_MAX_EVENTS 512u
#define FAKE_SCAN_TICK_LIMIT 40u
#define FAKE_INTERFACE_MAGIC 0x44574C46UL
#define FAKE_INTERFACE_LOCAL_ENABLE 1u
#define FAKE_INTERFACE_SENSOR_AREA 2u
#define FAKE_INTERFACE_CONTROL_OBJECT 3u

#define FAKE_BASE_IGSOBJECT (1u << 0)
#define FAKE_BASE_SENSOR_AREA (1u << 1)
#define FAKE_BASE_INTERACTIVE_ITEM (1u << 2)
#define FAKE_BASE_ILOCAL_ENABLE (1u << 3)
#define FAKE_BASE_CONTROL_OBJECT (1u << 4)

#define FAKE_VTABLE_MODEL_OBJECT 0x10000001ULL
#define FAKE_VTABLE_SENSOR_AREA 0x0188EEC8ULL
#define FAKE_VTABLE_SENSOR_GOTO 0x017A1798ULL
#define FAKE_VTABLE_INTERACTIVE_ITEM 0x017CD048ULL
#define FAKE_VTABLE_ACTION_CHAIN 0x0195B938ULL
#define FAKE_GAMEPLAY_RTTI_SENTINEL 0x00276FF0ULL
#define FAKE_ENGINE_RTTI_SENTINEL 0x00277000ULL
#define FAKE_CHECKINSIDE_TARGET_SENTINEL 0x00ECB880ULL
#define FAKE_CHECKINSIDE_SLOT 0x0310u
#define FAKE_SENSOR_AREA_OFFSET 0x0000u
#define FAKE_IGSOBJECT_OFFSET 0x0010u
#define FAKE_ILOCAL_ENABLE_OFFSET 0x00F8u

#define FAKE_SENSOR_INTERLEAVE_NONE 0
#define FAKE_SENSOR_INTERLEAVE_REMOVE_READD 1
#define FAKE_SENSOR_INTERLEAVE_GENERATION_RESET 2

/* This harness tests the pre-existing native gameplay adapters. The quest HUD
 * bridge has its own copied-ABI contract tests and deliberately remains
 * unavailable without the exact Player PE. */
static int g_fake_hud_available;

int dluma_quest_hud_is_available(void)
{
    return g_fake_hud_available;
}

int dluma_quest_hud_start(void)
{
    return 0;
}

void dluma_quest_hud_quiesce(void)
{
}

int dluma_quest_hud_stop(void)
{
    return 1;
}

void dluma_quest_hud_on_map_generation_changed(uint32_t map_generation)
{
    (void)map_generation;
}

/* The shared native gameplay dispatcher now fans validated events out to
 * native, managed, and Lua hosts. This focused harness observes the Lua-side
 * queue and supplies bounded no-op peers for the other two language hosts. */
int dluma_native_mods_enqueue_game_event(
    unsigned int kind,
    unsigned int map_generation,
    ULONGLONG native_index_epoch,
    ULONGLONG native_subscription_key_revision,
    const char* exact_name,
    int state,
    double number)
{
    (void)kind;
    (void)map_generation;
    (void)native_index_epoch;
    (void)native_subscription_key_revision;
    (void)exact_name;
    (void)state;
    (void)number;
    return 1;
}

int dluma_managed_host_enqueue_game_event(
    unsigned int kind,
    unsigned int map_generation,
    ULONGLONG native_index_epoch,
    ULONGLONG native_subscription_key_revision,
    const char* exact_name,
    int state,
    double number)
{
    (void)kind;
    (void)map_generation;
    (void)native_index_epoch;
    (void)native_subscription_key_revision;
    (void)exact_name;
    (void)state;
    (void)number;
    return 1;
}

typedef struct FakeObject FakeObject;

typedef struct FakeInterface {
    uint32_t magic;
    uint32_t kind;
    FakeObject* owner;
} FakeInterface;

struct FakeObject {
    int32_t id;
    const char* name;
    int name_valid;
    const char* dynamic_class;
    int rtti_valid;
    uint64_t gameplay_rtti_sentinel;
    uint64_t engine_rtti_sentinel;
    uint32_t base_class_flags;
    void* complete_object;
    int complete_object_valid;
    uint32_t igsobject_subobject_offset;
    uint32_t sensor_area_subobject_offset;
    uint64_t primary_vtable_identity;
    int sensor_area_base_valid;
    uint32_t checkinside_slot_offset;
    uint64_t checkinside_target_identity;
    int checkinside_target_valid;
    uint32_t local_enable_subobject_offset;
    int local_get_fail_after_set;
    int local_set_fail;
    uint32_t local_get_call_count;
    uint32_t local_get_fail_on_call;
    int enabled;
    int inside;
    int local_set_seen;
    float position[3];
    float forward[3];
    int world_position_fail;
    int world_forward_fail;
    FakeObject* previous;
    FakeObject* next;
    void* level;
    uint32_t generation;
    FakeInterface local_interface;
    FakeInterface sensor_interface;
    FakeInterface control_interface;
};

typedef struct FakeEvent {
    uint32_t kind;
    uint32_t generation;
    uint64_t index_epoch;
    uint64_t subscription_revision;
    int state;
    char name[DLUMA_LUA54_MAX_OBJECT_NAME];
} FakeEvent;

typedef struct FakeWorld {
    char map[DLUMA_MAX_MAP_NAME];
    uint64_t clock_ms;
    void* level;
    int active_level_available;
    void* same_map_level;
    void* foreign_level;
    int same_map_level_token;
    int foreign_level_token;
    int owner_scoped_lookup;
    int reject_distinct_owner_lookup;
    uint32_t object_count;
    uint32_t local_get_calls;
    uint32_t local_set_calls;
    uint32_t sensor_inside_calls;
    uint32_t object_by_id_calls;
    uint32_t subtitle_show_calls;
    uint32_t subtitle_hide_calls;
    uint32_t radio_dialog_calls;
    uint32_t action_chain_run_calls;
    int32_t last_subtitle_id;
    int last_subtitle_force;
    char last_subtitle_text[DLUMA_LUA54_MAX_SUBTITLE_TEXT];
    char last_radio_group[DLUMA_LUA54_MAX_DIALOG_NAME];
    FakeObject* last_action_chain;
    int action_chain_fail;
    uint32_t active_generation;
    int32_t alias_id;
    FakeObject* alias_object;
    int32_t promote_owner_on_lookup_id;
    FakeObject* promote_owner_object;
    int sensor_interleave_action;
    int sensor_interleave_ok;
    uint64_t sensor_interleave_owner;
    uint32_t sensor_interleave_token;
    uint32_t sensor_interleave_kind;
    uint32_t sensor_interleave_generation;
    const char* sensor_interleave_name;
    int world_find_epoch_change_on_is_a;
    uint32_t world_find_epoch_generation;
    void* local_last_interface;
    void* sensor_last_interface;
    FakeObject* objects;
    FakeObject inline_objects[FAKE_INLINE_OBJECTS];
} FakeWorld;

typedef int (*FakeCaseFn)(void);

typedef struct FakeCase {
    const char* name;
    FakeCaseFn function;
} FakeCase;

DlumaRuntime g_dluma;

static FakeEvent g_events[FAKE_MAX_EVENTS];
static uint32_t g_event_count;
static uint32_t g_overflow_logs;
static uint32_t g_interaction_original_calls;

static void fake_change_generation(FakeWorld* world, uint32_t generation);

static int fake_streq(const char* a, const char* b)
{
    if (!a || !b) return 0;
    while (*a && *b && *a == *b) { ++a; ++b; }
    return *a == 0 && *b == 0;
}

int dluma_stricmp(const char* a, const char* b)
{
    unsigned char ca;
    unsigned char cb;
    if (!a) a = "";
    if (!b) b = "";
    for (;;) {
        ca = (unsigned char)*a++;
        cb = (unsigned char)*b++;
        if (ca >= 'A' && ca <= 'Z') ca = (unsigned char)(ca + ('a' - 'A'));
        if (cb >= 'A' && cb <= 'Z') cb = (unsigned char)(cb + ('a' - 'A'));
        if (ca != cb) return ca < cb ? -1 : 1;
        if (!ca) return 0;
    }
}

void dluma_str_copy(char* dst, unsigned int capacity, const char* src)
{
    unsigned int i = 0;
    if (!dst || !capacity) return;
    if (!src) src = "";
    while (i + 1 < capacity && src[i]) {
        dst[i] = src[i];
        ++i;
    }
    dst[i] = 0;
}

void dluma_str_append(char* dst, unsigned int capacity, const char* src)
{
    unsigned int length = 0;
    if (!dst || !capacity) return;
    while (length < capacity && dst[length]) ++length;
    if (length >= capacity) {
        dst[capacity - 1] = 0;
        return;
    }
    dluma_str_copy(dst + length, capacity - length, src);
}

void dluma_normalize_map_name(const char* input, char* out, unsigned int capacity)
{
    dluma_str_copy(out, capacity, input);
}

unsigned int dluma_u64_to_text(
    ULONGLONG value, char* out, unsigned int capacity)
{
    char reversed[32];
    unsigned int count = 0;
    unsigned int i;
    if (!out || !capacity) return 0;
    do {
        reversed[count++] = (char)('0' + (value % 10u));
        value /= 10u;
    } while (value && count < DLUMA_ARRAY_COUNT(reversed));
    if (count + 1u > capacity) {
        out[0] = 0;
        return 0;
    }
    for (i = 0; i < count; ++i) out[i] = reversed[count - i - 1u];
    out[count] = 0;
    return count;
}

int dluma_is_readable(const void* address, SIZE_T bytes)
{
    return address != 0 && bytes != 0;
}

void dluma_log_line(const char* level, const char* message)
{
    (void)level;
    if (message && strstr(message, "overflow")) ++g_overflow_logs;
}

void dluma_log_pair(const char* level, const char* label, const char* value)
{
    (void)level;
    (void)label;
    (void)value;
}

void dluma_log_u64(const char* level, const char* label, ULONGLONG value)
{
    (void)level;
    (void)label;
    (void)value;
}

int dluma_engine_query_level(DlumaEngineState* state, char* map_out, unsigned int map_cap)
{
    (void)state;
    if (map_out && map_cap) map_out[0] = 0;
    return 0;
}

int dluma_lua_runtime_enqueue_event(
    uint32_t kind,
    uint32_t map_generation,
    ULONGLONG native_index_epoch,
    ULONGLONG native_subscription_revision,
    const char* exact_name,
    int state,
    double number)
{
    FakeEvent* event;
    (void)number;
    if (g_event_count >= FAKE_MAX_EVENTS) return 0;
    event = &g_events[g_event_count++];
    memset(event, 0, sizeof(*event));
    event->kind = kind;
    event->generation = map_generation;
    event->index_epoch = native_index_epoch;
    event->subscription_revision = native_subscription_revision;
    event->state = state;
    dluma_str_copy(event->name, DLUMA_ARRAY_COUNT(event->name), exact_name);
    return 1;
}

static char __fastcall fake_interaction_original(
    void* item, unsigned int action, __int64* context)
{
    (void)item;
    (void)action;
    ++g_interaction_original_calls;
    if (context) ++*context;
    return 37;
}

static char __fastcall fake_interaction_failed_original(
    void* item, unsigned int action, __int64* context)
{
    (void)item;
    (void)action;
    (void)context;
    ++g_interaction_original_calls;
    return 0;
}

static uint64_t fake_clock(void* user)
{
    return ((FakeWorld*)user)->clock_ms;
}

static int fake_object_is_current(FakeWorld* world, FakeObject* object)
{
    return world && object && object->generation == world->active_generation;
}

static void* fake_active_level(void* user, char* map_out, uint32_t map_capacity)
{
    FakeWorld* world = (FakeWorld*)user;
    if (!world->active_level_available) {
        if (map_out && map_capacity) map_out[0] = 0;
        return 0;
    }
    dluma_str_copy(map_out, map_capacity, world->map);
    return world->level;
}

static void* fake_object_by_id(void* user, void* level, int32_t object_id)
{
    FakeWorld* world = (FakeWorld*)user;
    uint32_t i;
    ++world->object_by_id_calls;
    if (!world->owner_scoped_lookup && level != world->level) return 0;
    if (world->promote_owner_object &&
        object_id == world->promote_owner_on_lookup_id)
        world->promote_owner_object->level = world->level;
    if (world->reject_distinct_owner_lookup &&
        level == world->same_map_level) return 0;
    if (world->alias_object && world->alias_id == object_id &&
        (!world->owner_scoped_lookup || world->alias_object->level == level) &&
        fake_object_is_current(world, world->alias_object))
        return world->alias_object;
    for (i = 0; i < world->object_count; ++i)
        if (world->objects[i].id == object_id &&
            (!world->owner_scoped_lookup || world->objects[i].level == level) &&
            fake_object_is_current(world, &world->objects[i]))
            return &world->objects[i];
    return 0;
}

static void* fake_object_prev(void* user, void* object)
{
    (void)user;
    return object ? ((FakeObject*)object)->previous : 0;
}

static void* fake_object_next(void* user, void* object)
{
    (void)user;
    return object ? ((FakeObject*)object)->next : 0;
}

static void* fake_object_level(void* user, void* object)
{
    FakeWorld* world = (FakeWorld*)user;
    FakeObject* value = (FakeObject*)object;
    return fake_object_is_current(world, value) ? value->level : 0;
}

static int fake_level_name(
    void* user, void* level, char* out, uint32_t capacity)
{
    FakeWorld* world = (FakeWorld*)user;
    if (!world || !level || !out || !capacity) return 0;
    if (level == world->level || level == world->same_map_level) {
        dluma_str_copy(out, capacity, world->map);
        return 1;
    }
    if (level == world->foreign_level) {
        dluma_str_copy(out, capacity, "other_map");
        return 1;
    }
    return 0;
}

static int32_t fake_object_id(void* user, void* object)
{
    FakeWorld* world = (FakeWorld*)user;
    FakeObject* value = (FakeObject*)object;
    return fake_object_is_current(world, value) ? value->id : -1;
}

static int fake_object_name(void* user, void* object, char* out, uint32_t capacity)
{
    FakeObject* value = (FakeObject*)object;
    FakeWorld* world = (FakeWorld*)user;
    if (!fake_object_is_current(world, value) ||
        !value->name_valid || !value->name) return 0;
    dluma_str_copy(out, capacity, value->name);
    return 1;
}

static int fake_object_class(void* user, void* object, char* out, uint32_t capacity)
{
    FakeObject* value = (FakeObject*)object;
    FakeWorld* world = (FakeWorld*)user;
    if (!fake_object_is_current(world, value) || !value->rtti_valid ||
        value->gameplay_rtti_sentinel != FAKE_GAMEPLAY_RTTI_SENTINEL ||
        value->engine_rtti_sentinel != FAKE_ENGINE_RTTI_SENTINEL ||
        value->gameplay_rtti_sentinel == value->engine_rtti_sentinel ||
        !value->dynamic_class) return 0;
    dluma_str_copy(out, capacity, value->dynamic_class);
    return 1;
}

static int fake_object_is_a(void* user, void* object, const char* class_name)
{
    FakeObject* value = (FakeObject*)object;
    FakeWorld* world = (FakeWorld*)user;
    int result = 0;
    if (!fake_object_is_current(world, value) || !value->rtti_valid ||
        value->gameplay_rtti_sentinel != FAKE_GAMEPLAY_RTTI_SENTINEL ||
        !value->dynamic_class || !class_name) return 0;
    if (fake_streq(value->dynamic_class, class_name)) result = 1;
    else if ((value->base_class_flags & FAKE_BASE_IGSOBJECT) &&
             fake_streq(class_name, "IGSObject")) result = 1;
    else if ((value->base_class_flags & FAKE_BASE_SENSOR_AREA) &&
             fake_streq(class_name, "SensorArea")) result = 1;
    else if ((value->base_class_flags & FAKE_BASE_INTERACTIVE_ITEM) &&
             fake_streq(class_name, "InteractiveItem")) result = 1;
    else if ((value->base_class_flags & FAKE_BASE_ILOCAL_ENABLE) &&
             fake_streq(class_name, "ILocalEnable")) result = 1;
    if (world->world_find_epoch_change_on_is_a) {
        world->world_find_epoch_change_on_is_a = 0;
        fake_change_generation(world, world->world_find_epoch_generation);
    }
    return result;
}

static void* fake_complete_object(void* user, void* object)
{
    FakeObject* value = (FakeObject*)object;
    FakeWorld* world = (FakeWorld*)user;
    if (!fake_object_is_current(world, value) ||
        !value->complete_object_valid || !value->complete_object ||
        value->igsobject_subobject_offset > 0x0200u) return 0;
    return value->complete_object;
}

static void* fake_local_enable_interface(void* user, void* object)
{
    FakeObject* value = (FakeObject*)object;
    FakeWorld* world = (FakeWorld*)user;
    if (!fake_object_is_current(world, value) ||
        !(value->base_class_flags & FAKE_BASE_ILOCAL_ENABLE) ||
        !value->complete_object_valid ||
        value->local_enable_subobject_offset !=
            FAKE_ILOCAL_ENABLE_OFFSET) return 0;
    return &value->local_interface;
}

static void* fake_control_object_interface(void* user, void* object)
{
    FakeObject* value = (FakeObject*)object;
    FakeWorld* world = (FakeWorld*)user;
    if (!fake_object_is_current(world, value) ||
        !(value->base_class_flags & FAKE_BASE_CONTROL_OBJECT) ||
        !value->complete_object_valid) return 0;
    return &value->control_interface;
}

static int fake_world_get_position(
    void* user, void* interface_pointer, DlumaLua54Vec3* position)
{
    FakeWorld* world = (FakeWorld*)user;
    FakeInterface* interface_value = (FakeInterface*)interface_pointer;
    FakeObject* value;
    if (!interface_value || interface_value->magic != FAKE_INTERFACE_MAGIC ||
        interface_value->kind != FAKE_INTERFACE_CONTROL_OBJECT ||
        !interface_value->owner || !position) return 0;
    value = interface_value->owner;
    if (!fake_object_is_current(world, value) ||
        !(value->base_class_flags & FAKE_BASE_CONTROL_OBJECT) ||
        value->world_position_fail) return 0;
    position->struct_size = sizeof(*position);
    position->x = value->position[0];
    position->y = value->position[1];
    position->z = value->position[2];
    return 1;
}

static int fake_world_get_forward(
    void* user, void* interface_pointer, DlumaLua54Vec3* forward)
{
    FakeWorld* world = (FakeWorld*)user;
    FakeInterface* interface_value = (FakeInterface*)interface_pointer;
    FakeObject* value;
    if (!interface_value || interface_value->magic != FAKE_INTERFACE_MAGIC ||
        interface_value->kind != FAKE_INTERFACE_CONTROL_OBJECT ||
        !interface_value->owner || !forward) return 0;
    value = interface_value->owner;
    if (!fake_object_is_current(world, value) ||
        !(value->base_class_flags & FAKE_BASE_CONTROL_OBJECT) ||
        value->world_forward_fail) return 0;
    forward->struct_size = sizeof(*forward);
    forward->x = value->forward[0];
    forward->y = value->forward[1];
    forward->z = value->forward[2];
    return 1;
}

static int fake_local_enable_get(void* user, void* interface_pointer, int* enabled)
{
    FakeWorld* world = (FakeWorld*)user;
    FakeInterface* interface_value = (FakeInterface*)interface_pointer;
    FakeObject* value;
    ++world->local_get_calls;
    world->local_last_interface = interface_pointer;
    if (!interface_value || interface_value->magic != FAKE_INTERFACE_MAGIC ||
        interface_value->kind != FAKE_INTERFACE_LOCAL_ENABLE ||
        !interface_value->owner || !enabled) return 0;
    value = interface_value->owner;
    ++value->local_get_call_count;
    if (!fake_object_is_current(world, value) ||
        !(value->base_class_flags & FAKE_BASE_ILOCAL_ENABLE) ||
        (value->local_get_fail_on_call &&
         value->local_get_call_count == value->local_get_fail_on_call) ||
        (value->local_get_fail_after_set && value->local_set_seen)) return 0;
    *enabled = value->enabled;
    return 1;
}

static int fake_local_enable_set(void* user, void* interface_pointer, int enabled)
{
    FakeWorld* world = (FakeWorld*)user;
    FakeInterface* interface_value = (FakeInterface*)interface_pointer;
    FakeObject* value;
    ++world->local_set_calls;
    world->local_last_interface = interface_pointer;
    if (!interface_value || interface_value->magic != FAKE_INTERFACE_MAGIC ||
        interface_value->kind != FAKE_INTERFACE_LOCAL_ENABLE ||
        !interface_value->owner) return 0;
    value = interface_value->owner;
    if (!fake_object_is_current(world, value) ||
        !(value->base_class_flags & FAKE_BASE_ILOCAL_ENABLE) ||
        value->local_set_fail) return 0;
    value->enabled = enabled ? 1 : 0;
    value->local_set_seen = 1;
    return 1;
}

static void* fake_sensor_area_interface(void* user, void* object)
{
    FakeWorld* world = (FakeWorld*)user;
    FakeObject* value = (FakeObject*)object;
    if (!fake_object_is_current(world, value) ||
        !(value->base_class_flags & FAKE_BASE_SENSOR_AREA) ||
        !value->sensor_area_base_valid || !value->complete_object_valid ||
        value->sensor_area_subobject_offset !=
            FAKE_SENSOR_AREA_OFFSET) return 0;
    return &value->sensor_interface;
}

static int fake_sensor_inside(void* user, void* sensor_area_interface, int* inside)
{
    FakeWorld* world = (FakeWorld*)user;
    FakeInterface* interface_value = (FakeInterface*)sensor_area_interface;
    FakeObject* value;
    ++world->sensor_inside_calls;
    world->sensor_last_interface = sensor_area_interface;
    if (!interface_value || interface_value->magic != FAKE_INTERFACE_MAGIC ||
        interface_value->kind != FAKE_INTERFACE_SENSOR_AREA ||
        !interface_value->owner || !inside) return 0;
    value = interface_value->owner;
    if (!fake_object_is_current(world, value) ||
        !(value->base_class_flags & FAKE_BASE_SENSOR_AREA) ||
        !value->primary_vtable_identity ||
        value->checkinside_slot_offset != FAKE_CHECKINSIDE_SLOT ||
        value->checkinside_target_identity !=
            FAKE_CHECKINSIDE_TARGET_SENTINEL ||
        !value->checkinside_target_valid)
        return 0;
    *inside = value->inside;
    if (world->sensor_interleave_action ==
            FAKE_SENSOR_INTERLEAVE_REMOVE_READD) {
        DlumaLua54Error error;
        world->sensor_interleave_action = FAKE_SENSOR_INTERLEAVE_NONE;
        memset(&error, 0, sizeof(error));
        error.struct_size = sizeof(error);
        world->sensor_interleave_ok =
            dluma_native_gameplay_subscription_remove(
                world->sensor_interleave_owner,
                world->sensor_interleave_token,
                world->sensor_interleave_kind,
                world->sensor_interleave_name,
                &error);
        memset(&error, 0, sizeof(error));
        error.struct_size = sizeof(error);
        world->sensor_interleave_ok = world->sensor_interleave_ok &&
            dluma_native_gameplay_subscription_add(
                world->sensor_interleave_owner,
                world->sensor_interleave_generation,
                world->sensor_interleave_token,
                world->sensor_interleave_kind,
                world->sensor_interleave_name,
                &error);
    } else if (world->sensor_interleave_action ==
                   FAKE_SENSOR_INTERLEAVE_GENERATION_RESET) {
        uint32_t generation = world->sensor_interleave_generation;
        world->sensor_interleave_action = FAKE_SENSOR_INTERLEAVE_NONE;
        world->sensor_interleave_ok = 1;
        fake_change_generation(world, generation);
    }
    return 1;
}

static int fake_subtitle_show(
    void* user, void* level, int32_t subtitle_id,
    const char* text, int force)
{
    FakeWorld* world = (FakeWorld*)user;
    if (!world || level != world->level || !text || !text[0]) return 0;
    ++world->subtitle_show_calls;
    world->last_subtitle_id = subtitle_id;
    world->last_subtitle_force = force ? 1 : 0;
    dluma_str_copy(world->last_subtitle_text,
        DLUMA_ARRAY_COUNT(world->last_subtitle_text), text);
    return 1;
}

static int fake_subtitle_hide(
    void* user, void* level, int32_t subtitle_id)
{
    FakeWorld* world = (FakeWorld*)user;
    if (!world || level != world->level || subtitle_id != world->last_subtitle_id)
        return 0;
    ++world->subtitle_hide_calls;
    return 1;
}

static int fake_radio_dialog(
    void* user, const char* speaker, const char* group)
{
    FakeWorld* world = (FakeWorld*)user;
    if (!world || !speaker || !speaker[0] || !group || !group[0]) return 0;
    ++world->radio_dialog_calls;
    dluma_str_copy(world->last_radio_group,
        DLUMA_ARRAY_COUNT(world->last_radio_group), group);
    return 1;
}

static int fake_action_run_chain(void* user, void* complete_action_chain)
{
    FakeWorld* world = (FakeWorld*)user;
    FakeObject* value = (FakeObject*)complete_action_chain;
    if (!world || !fake_object_is_current(world, value) ||
        value->complete_object != complete_action_chain ||
        !fake_streq(value->dynamic_class, "ActionChain") ||
        value->primary_vtable_identity != FAKE_VTABLE_ACTION_CHAIN)
        return 0;
    ++world->action_chain_run_calls;
    world->last_action_chain = value;
    return world->action_chain_fail ? 0 : 1;
}

static void fake_reset_capture(void)
{
    memset(g_events, 0, sizeof(g_events));
    g_event_count = 0;
    g_overflow_logs = 0;
    g_interaction_original_calls = 0;
}

static void fake_init_object(
    FakeWorld* world,
    uint32_t index,
    int32_t id,
    const char* name,
    const char* class_name)
{
    FakeObject* object = &world->objects[index];
    memset(object, 0, sizeof(*object));
    object->id = id;
    object->name = name;
    object->name_valid = name ? 1 : 0;
    object->dynamic_class = class_name;
    object->rtti_valid = class_name ? 1 : 0;
    object->gameplay_rtti_sentinel = FAKE_GAMEPLAY_RTTI_SENTINEL;
    object->engine_rtti_sentinel = FAKE_ENGINE_RTTI_SENTINEL;
    object->base_class_flags = FAKE_BASE_IGSOBJECT | FAKE_BASE_CONTROL_OBJECT;
    object->complete_object = object;
    object->complete_object_valid = 1;
    object->igsobject_subobject_offset = FAKE_IGSOBJECT_OFFSET;
    object->primary_vtable_identity = FAKE_VTABLE_MODEL_OBJECT;
    object->level = world->level;
    object->generation = world->active_generation;
    object->local_interface.magic = FAKE_INTERFACE_MAGIC;
    object->local_interface.kind = FAKE_INTERFACE_LOCAL_ENABLE;
    object->local_interface.owner = object;
    object->sensor_interface.magic = FAKE_INTERFACE_MAGIC;
    object->sensor_interface.kind = FAKE_INTERFACE_SENSOR_AREA;
    object->sensor_interface.owner = object;
    object->control_interface.magic = FAKE_INTERFACE_MAGIC;
    object->control_interface.kind = FAKE_INTERFACE_CONTROL_OBJECT;
    object->control_interface.owner = object;
    object->position[0] = (float)id;
    object->position[1] = (float)id + 0.25f;
    object->position[2] = (float)id + 0.5f;
    object->forward[0] = 0.0f;
    object->forward[1] = 0.0f;
    object->forward[2] = 1.0f;
    if (class_name && fake_streq(class_name, "SensorArea")) {
        object->base_class_flags |= FAKE_BASE_SENSOR_AREA;
        object->sensor_area_base_valid = 1;
        object->sensor_area_subobject_offset = FAKE_SENSOR_AREA_OFFSET;
        object->primary_vtable_identity = FAKE_VTABLE_SENSOR_AREA;
        object->checkinside_slot_offset = FAKE_CHECKINSIDE_SLOT;
        object->checkinside_target_identity =
            FAKE_CHECKINSIDE_TARGET_SENTINEL;
        object->checkinside_target_valid = 1;
    } else if (class_name && fake_streq(class_name, "SensorGoTo")) {
        object->base_class_flags |= FAKE_BASE_SENSOR_AREA;
        object->sensor_area_base_valid = 1;
        object->sensor_area_subobject_offset = FAKE_SENSOR_AREA_OFFSET;
        object->primary_vtable_identity = FAKE_VTABLE_SENSOR_GOTO;
        object->checkinside_slot_offset = FAKE_CHECKINSIDE_SLOT;
        object->checkinside_target_identity =
            FAKE_CHECKINSIDE_TARGET_SENTINEL;
        object->checkinside_target_valid = 1;
    } else if (class_name && fake_streq(class_name, "InteractiveItem")) {
        object->base_class_flags |= FAKE_BASE_INTERACTIVE_ITEM;
        object->primary_vtable_identity = FAKE_VTABLE_INTERACTIVE_ITEM;
    } else if (class_name && fake_streq(class_name, "ActionChain")) {
        object->primary_vtable_identity = FAKE_VTABLE_ACTION_CHAIN;
    }
}

static void fake_add_local_enable_base(FakeObject* object)
{
    if (!object) return;
    object->base_class_flags |= FAKE_BASE_ILOCAL_ENABLE;
    object->local_enable_subobject_offset = FAKE_ILOCAL_ENABLE_OFFSET;
}

static void fake_init_world(FakeWorld* world)
{
    memset(world, 0, sizeof(*world));
    dluma_str_copy(world->map, DLUMA_ARRAY_COUNT(world->map), "nowhere");
    world->clock_ms = 1000;
    world->level = world;
    world->active_level_available = 1;
    world->same_map_level = &world->same_map_level_token;
    world->foreign_level = &world->foreign_level_token;
    world->objects = world->inline_objects;
    world->object_count = 5;
    fake_init_object(world, 0, 10, "DLUMA_TestSensor", "SensorGoTo");
    fake_init_object(world, 1, 11, "DLUMA_TestInteract", "InteractiveItem");
    fake_init_object(world, 2, 12, "DLUMA_LocalA", "ModelObject");
    fake_add_local_enable_base(&world->objects[2]);
    fake_init_object(world, 3, 13, "DLUMA_NoLocalEnable", "ModelObject");
    fake_init_object(world, 4, 14, "DLUMA_LocalB", "ModelObject");
    fake_add_local_enable_base(&world->objects[4]);
}

static void fake_link_linear(FakeWorld* world)
{
    uint32_t i;
    for (i = 0; i < world->object_count; ++i) {
        world->objects[i].previous = i ? &world->objects[i - 1] : 0;
        world->objects[i].next = i + 1 < world->object_count
            ? &world->objects[i + 1] : 0;
    }
}

static DlumaNativeGameplayBackend fake_backend(FakeWorld* world, uint32_t readiness)
{
    DlumaNativeGameplayBackend backend;
    memset(&backend, 0, sizeof(backend));
    backend.struct_size = sizeof(backend);
    backend.readiness = readiness;
    backend.user = world;
    backend.clock_ms = fake_clock;
    backend.active_level = fake_active_level;
    backend.object_by_id = fake_object_by_id;
    backend.object_prev = fake_object_prev;
    backend.object_next = fake_object_next;
    backend.object_level = fake_object_level;
    backend.level_name = fake_level_name;
    backend.object_id = fake_object_id;
    backend.object_name = fake_object_name;
    backend.object_class = fake_object_class;
    backend.object_is_a = fake_object_is_a;
    backend.complete_object = fake_complete_object;
    backend.control_object_interface = fake_control_object_interface;
    backend.world_get_position = fake_world_get_position;
    backend.world_get_forward = fake_world_get_forward;
    backend.local_enable_interface = fake_local_enable_interface;
    backend.local_enable_get = fake_local_enable_get;
    backend.local_enable_set = fake_local_enable_set;
    backend.sensor_area_interface = fake_sensor_area_interface;
    backend.sensor_inside = fake_sensor_inside;
    backend.subtitle_show = fake_subtitle_show;
    backend.subtitle_hide = fake_subtitle_hide;
    backend.radio_dialog = fake_radio_dialog;
    backend.action_run_chain = fake_action_run_chain;
    return backend;
}

static void fake_change_generation(FakeWorld* world, uint32_t generation)
{
    uint32_t i;
    world->active_generation = generation;
    for (i = 0; i < world->object_count; ++i)
        world->objects[i].generation = generation;
    dluma_native_gameplay_on_map_generation_changed(
        generation, world->map, world->level);
}

static int fake_begin(FakeWorld* world, uint32_t readiness, uint32_t generation)
{
    DlumaNativeGameplayBackend backend = fake_backend(world, readiness);
    fake_reset_capture();
    if (!dluma_native_gameplay_start_with_backend(&backend)) return 0;
    fake_change_generation(world, generation);
    return 1;
}

static int fake_scan_to_terminal(void)
{
    uint32_t i;
    for (i = 0; i < FAKE_SCAN_TICK_LIMIT; ++i) {
        DlumaNativeIndexPhase phase = dluma_native_gameplay_index_phase();
        if (phase == DLUMA_NATIVE_INDEX_COMPLETE ||
            phase == DLUMA_NATIVE_INDEX_DEGRADED) return 1;
        dluma_native_gameplay_game_thread_tick();
    }
    return 0;
}

static int fake_start_ready(
    FakeWorld* world, uint32_t readiness, uint32_t generation)
{
    if (!fake_begin(world, readiness, generation) || !fake_scan_to_terminal())
        return 0;
    return dluma_native_gameplay_index_phase() == DLUMA_NATIVE_INDEX_COMPLETE;
}

#define CHECK(expression) do { \
    if (!(expression)) { \
        fprintf(stderr, "CHECK failed at line %d: %s\n", __LINE__, #expression); \
        dluma_native_gameplay_stop(); \
        return 0; \
    } \
} while (0)

static void fake_error(DlumaLua54Error* error)
{
    memset(error, 0, sizeof(*error));
    error->struct_size = sizeof(*error);
}

static int fake_find(
    uint64_t owner,
    uint32_t generation,
    const char* name,
    const char* expected,
    DlumaLua54Object* object,
    DlumaLua54Error* error)
{
    memset(object, 0, sizeof(*object));
    fake_error(error);
    return dluma_native_gameplay_world_find(
        owner, generation, name, expected, object, error);
}

static void fake_enabled_request(
    DlumaLua54SetEnabledRequest* request,
    uint64_t owner,
    uint32_t generation,
    int enabled)
{
    memset(request, 0, sizeof(*request));
    request->struct_size = sizeof(*request);
    request->owner_id = owner;
    request->map_generation = generation;
    request->enabled = enabled ? 1u : 0u;
    request->atomic = 1;
}

static int case_readiness_fail_closed(void)
{
    FakeWorld world;
    fake_init_world(&world);
    CHECK(fake_begin(&world, 0, 1));
    CHECK(dluma_native_gameplay_supported_capabilities() == 0);
    CHECK(dluma_native_gameplay_capabilities() == 0);
    CHECK(dluma_native_gameplay_index_phase() == DLUMA_NATIVE_INDEX_UNINITIALIZED);
    dluma_native_gameplay_stop();
    return 1;
}

static int case_hud_readiness_survives_index_resets(void)
{
    FakeWorld world;
    fake_init_world(&world);
    fake_link_linear(&world);
    g_fake_hud_available = 1;
    CHECK(fake_begin(&world,
        DLUMA_NATIVE_READY_WORLD_FIND | DLUMA_NATIVE_READY_QUEST_HUD, 1));
    CHECK(dluma_native_gameplay_readiness() & DLUMA_NATIVE_READY_QUEST_HUD);
    CHECK(!(dluma_native_gameplay_readiness() & DLUMA_NATIVE_READY_WORLD_FIND));
    CHECK(fake_scan_to_terminal());
    CHECK(dluma_native_gameplay_readiness() & DLUMA_NATIVE_READY_QUEST_HUD);
    fake_change_generation(&world, 2);
    CHECK(dluma_native_gameplay_readiness() & DLUMA_NATIVE_READY_QUEST_HUD);
    g_fake_hud_available = 0;
    fake_change_generation(&world, 3);
    CHECK(!(dluma_native_gameplay_readiness() & DLUMA_NATIVE_READY_QUEST_HUD));
    dluma_native_gameplay_stop();
    g_fake_hud_available = 1;
    CHECK(fake_begin(&world, DLUMA_NATIVE_READY_WORLD_FIND, 4));
    CHECK(!(dluma_native_gameplay_readiness() & DLUMA_NATIVE_READY_QUEST_HUD));
    g_fake_hud_available = 0;
    dluma_native_gameplay_stop();
    return 1;
}

static int case_index_incomplete_not_ready(void)
{
    FakeWorld world;
    DlumaLua54Object object;
    DlumaLua54Error error;
    fake_init_world(&world);
    fake_link_linear(&world);
    CHECK(fake_begin(&world, DLUMA_NATIVE_READY_WORLD_FIND, 2));
    CHECK(dluma_native_gameplay_index_phase() == DLUMA_NATIVE_INDEX_SCANNING_IDS);
    CHECK(!fake_find(1, 2, "DLUMA_LocalA", "", &object, &error));
    CHECK(fake_streq(error.code, "world_index_not_ready") && error.retryable);
    CHECK(dluma_native_gameplay_capabilities() == 0);
    dluma_native_gameplay_stop();
    return 1;
}

static int case_generation_change_during_scan_restarts_builder(void)
{
    FakeWorld world;
    DlumaLua54Object object;
    DlumaLua54Error error;
    DlumaNativeIndexDiagnostics diagnostics;
    fake_init_world(&world);
    fake_link_linear(&world);
    CHECK(fake_begin(&world, DLUMA_NATIVE_READY_WORLD_FIND, 40));
    dluma_native_gameplay_game_thread_tick();
    memset(&diagnostics, 0, sizeof(diagnostics));
    diagnostics.struct_size = sizeof(diagnostics);
    CHECK(dluma_native_gameplay_index_diagnostics(&diagnostics));
    CHECK(diagnostics.ids_scanned == 2048u &&
          diagnostics.phase == DLUMA_NATIVE_INDEX_SCANNING_IDS);

    fake_change_generation(&world, 41);
    memset(&diagnostics, 0, sizeof(diagnostics));
    diagnostics.struct_size = sizeof(diagnostics);
    CHECK(dluma_native_gameplay_index_diagnostics(&diagnostics));
    CHECK(diagnostics.ids_scanned == 0u && diagnostics.published_entries == 0u);
    CHECK(fake_scan_to_terminal());
    CHECK(fake_find(1, 41, "DLUMA_LocalA", "ModelObject", &object, &error));
    CHECK(object.map_generation == 41u);
    dluma_native_gameplay_stop();
    return 1;
}

static int case_bad_seed_then_sparse_7935(void)
{
    FakeWorld world;
    DlumaLua54Object object;
    DlumaLua54Error error;
    DlumaNativeIndexDiagnostics diagnostics;
    fake_init_world(&world);
    world.object_count = 2;
    fake_init_object(&world, 0, 2, "BadSeed", "BadSeedClass");
    world.objects[0].gameplay_rtti_sentinel =
        world.objects[0].engine_rtti_sentinel; /* models the old +0x18 defect */
    fake_init_object(&world, 1, 7935, "DLUMA_TestSensor", "SensorGoTo");
    CHECK(fake_start_ready(&world, DLUMA_NATIVE_READY_WORLD_FIND, 3));
    memset(&diagnostics, 0, sizeof(diagnostics));
    diagnostics.struct_size = sizeof(diagnostics);
    CHECK(dluma_native_gameplay_index_diagnostics(&diagnostics));
    CHECK(diagnostics.ids_scanned == 65536u);
    CHECK(diagnostics.nonnull_id_results == 2u);
    CHECK(diagnostics.accepted_entries == 1u);
    CHECK(diagnostics.rejected_rtti >= 1u);
    CHECK(diagnostics.published_entries == 1u);
    CHECK(fake_find(1, 3, "DLUMA_TestSensor", "SensorArea", &object, &error));
    CHECK(object.object_id == 7935 && fake_streq(object.class_name, "SensorGoTo"));
    dluma_native_gameplay_stop();
    return 1;
}

static int case_rejected_head_walks_to_valid_tail(void)
{
    FakeWorld world;
    DlumaLua54Object object;
    DlumaLua54Error error;
    fake_init_world(&world);
    world.object_count = 2;
    fake_init_object(&world, 0, 2, "RejectedHead", 0);
    fake_init_object(&world, 1, 7935, "SparseTail", "ModelObject");
    world.objects[0].next = &world.objects[1];
    world.objects[1].previous = &world.objects[0];
    CHECK(fake_start_ready(&world, DLUMA_NATIVE_READY_WORLD_FIND, 4));
    CHECK(fake_find(1, 4, "SparseTail", "ModelObject", &object, &error));
    dluma_native_gameplay_stop();
    return 1;
}

static int case_disconnected_components(void)
{
    FakeWorld world;
    DlumaLua54Object object;
    DlumaLua54Error error;
    DlumaNativeIndexDiagnostics diagnostics;
    fake_init_world(&world);
    world.object_count = 2;
    fake_init_object(&world, 0, 2, "ComponentA", "ModelObject");
    fake_init_object(&world, 1, 7935, "ComponentB", "ModelObject");
    CHECK(fake_start_ready(&world, DLUMA_NATIVE_READY_WORLD_FIND, 5));
    CHECK(fake_find(1, 5, "ComponentA", "", &object, &error));
    CHECK(fake_find(1, 5, "ComponentB", "", &object, &error));
    memset(&diagnostics, 0, sizeof(diagnostics));
    diagnostics.struct_size = sizeof(diagnostics);
    CHECK(dluma_native_gameplay_index_diagnostics(&diagnostics));
    CHECK(diagnostics.component_walks == 2u && diagnostics.published_entries == 2u);
    dluma_native_gameplay_stop();
    return 1;
}

static int case_same_pointer_returned_by_two_ids(void)
{
    FakeWorld world;
    DlumaLua54Object object;
    DlumaLua54Error error;
    DlumaNativeIndexDiagnostics diagnostics;
    fake_init_world(&world);
    world.object_count = 1;
    fake_init_object(&world, 0, 2, "AliasedSeed", "ModelObject");
    world.alias_id = 7935;
    world.alias_object = &world.objects[0];
    CHECK(fake_start_ready(&world, DLUMA_NATIVE_READY_WORLD_FIND, 42));
    CHECK(fake_find(1, 42, "AliasedSeed", "", &object, &error));
    memset(&diagnostics, 0, sizeof(diagnostics));
    diagnostics.struct_size = sizeof(diagnostics);
    CHECK(dluma_native_gameplay_index_diagnostics(&diagnostics));
    CHECK(diagnostics.nonnull_id_results == 2u);
    CHECK(diagnostics.candidate_pointers == 1u &&
          diagnostics.accepted_entries == 1u &&
          diagnostics.rejected_duplicate_pointer >= 1u);
    dluma_native_gameplay_stop();
    return 1;
}

static int case_rejected_cycle_does_not_end_scan(void)
{
    FakeWorld world;
    DlumaLua54Object object;
    DlumaLua54Error error;
    DlumaNativeIndexDiagnostics diagnostics;
    fake_init_world(&world);
    world.object_count = 3;
    fake_init_object(&world, 0, 2, "BadCycleA", 0);
    fake_init_object(&world, 1, 3, "BadCycleName", "ModelObject");
    world.objects[1].name_valid = 0;
    fake_init_object(&world, 2, 7935, "AfterCycle", "ModelObject");
    world.objects[0].next = &world.objects[1];
    world.objects[1].next = &world.objects[0];
    CHECK(fake_start_ready(&world, DLUMA_NATIVE_READY_WORLD_FIND, 6));
    CHECK(fake_find(1, 6, "AfterCycle", "", &object, &error));
    memset(&diagnostics, 0, sizeof(diagnostics));
    diagnostics.struct_size = sizeof(diagnostics);
    CHECK(dluma_native_gameplay_index_diagnostics(&diagnostics));
    CHECK(diagnostics.next_cycle_or_cap >= 1u);
    CHECK(diagnostics.rejected_name >= 1u && diagnostics.rejected_rtti >= 1u);
    dluma_native_gameplay_stop();
    return 1;
}

static int case_wrong_level_rejected_but_scan_continues(void)
{
    FakeWorld world;
    DlumaLua54Object object;
    DlumaLua54Error error;
    DlumaNativeIndexDiagnostics diagnostics;
    fake_init_world(&world);
    world.object_count = 2;
    fake_init_object(&world, 0, 2, "ForeignLevel", "ModelObject");
    world.objects[0].level = &world.objects[0];
    fake_init_object(&world, 1, 7935, "ActiveLevel", "ModelObject");
    CHECK(fake_start_ready(&world, DLUMA_NATIVE_READY_WORLD_FIND, 7));
    CHECK(fake_find(1, 7, "ActiveLevel", "", &object, &error));
    memset(&diagnostics, 0, sizeof(diagnostics));
    diagnostics.struct_size = sizeof(diagnostics);
    CHECK(dluma_native_gameplay_index_diagnostics(&diagnostics));
    CHECK(diagnostics.rejected_wrong_level >= 1u);
    dluma_native_gameplay_stop();
    return 1;
}

static int case_distinct_same_map_owner_without_id_roundtrip_is_accepted(void)
{
    FakeWorld world;
    DlumaLua54Object object;
    DlumaLua54Error error;
    DlumaNativeIndexDiagnostics diagnostics;
    uint32_t lookups_before_find;
    int found;
    fake_init_world(&world);
    world.owner_scoped_lookup = 1;
    world.reject_distinct_owner_lookup = 1;
    world.object_count = 3;
    fake_init_object(&world, 0, 2, "RootSeed", "LevelDI");
    fake_init_object(&world, 1, 7935, "ChildMapObject", "SensorGoTo");
    world.objects[1].level = world.same_map_level;
    fake_init_object(&world, 2, 7939, "ForeignMapObject", "ModelObject");
    world.objects[2].level = world.foreign_level;
    fake_link_linear(&world);
    CHECK(fake_start_ready(&world, DLUMA_NATIVE_READY_WORLD_FIND, 51));
    memset(&diagnostics, 0, sizeof(diagnostics));
    diagnostics.struct_size = sizeof(diagnostics);
    CHECK(dluma_native_gameplay_index_diagnostics(&diagnostics));
    CHECK(diagnostics.accepted_entries >= 2u);
    CHECK(diagnostics.accepted_distinct_owner_level >= 1u);
    CHECK(diagnostics.observed_owner_id_roundtrip_mismatch >= 1u);
    lookups_before_find = world.object_by_id_calls;
    found = fake_find(1, 51, "ChildMapObject", "", &object, &error);
    if (!found)
        fprintf(stderr, "distinct owner lookup error: %s (%s)\n",
                error.code, error.message);
    CHECK(found);
    CHECK(world.object_by_id_calls == lookups_before_find);
    CHECK(fake_streq(object.class_name, "SensorGoTo"));
    CHECK(!fake_find(2, 51, "ForeignMapObject", "", &object, &error));
    CHECK(fake_streq(error.code, "object_not_found"));
    CHECK(diagnostics.rejected_wrong_level >= 1u);
    dluma_native_gameplay_stop();
    return 1;
}

static int case_degraded_streaming_index_recovers_after_objects_arrive(void)
{
    FakeWorld world;
    DlumaLua54Object object;
    DlumaLua54Error error;
    fake_init_world(&world);
    world.object_count = 0;
    CHECK(fake_begin(&world, DLUMA_NATIVE_READY_WORLD_FIND, 61));
    CHECK(fake_scan_to_terminal());
    CHECK(dluma_native_gameplay_index_phase() == DLUMA_NATIVE_INDEX_DEGRADED);

    fake_init_object(&world, 0, 2, "StreamedSensor", "SensorGoTo");
    world.object_count = 1;
    world.clock_ms += 3000u;
    dluma_native_gameplay_game_thread_tick();
    CHECK(dluma_native_gameplay_index_phase() == DLUMA_NATIVE_INDEX_SCANNING_IDS);
    CHECK(fake_scan_to_terminal());
    CHECK(fake_find(1, 61, "StreamedSensor", "SensorArea", &object, &error));
    dluma_native_gameplay_stop();
    return 1;
}

static int case_direct_seed_reconsiders_early_ownerless_link(void)
{
    FakeWorld world;
    DlumaLua54Object object;
    DlumaLua54Error error;
    DlumaNativeIndexDiagnostics diagnostics;
    fake_init_world(&world);
    world.object_count = 2;
    fake_init_object(&world, 0, 2, "RootSeed", "LevelDI");
    fake_init_object(&world, 1, 7935, "LateOwnedSensor", "SensorGoTo");
    world.objects[0].next = &world.objects[1];
    world.objects[1].previous = &world.objects[0];
    world.objects[1].level = 0;
    world.promote_owner_on_lookup_id = 7935;
    world.promote_owner_object = &world.objects[1];
    CHECK(fake_start_ready(&world, DLUMA_NATIVE_READY_WORLD_FIND, 63));
    CHECK(fake_find(1, 63, "LateOwnedSensor", "SensorArea", &object, &error));
    memset(&diagnostics, 0, sizeof(diagnostics));
    diagnostics.struct_size = sizeof(diagnostics);
    CHECK(dluma_native_gameplay_index_diagnostics(&diagnostics));
    CHECK(diagnostics.reconsidered_direct_seeds >= 1u);
    CHECK(diagnostics.accepted_reconsidered_direct_seeds >= 1u);
    dluma_native_gameplay_stop();
    return 1;
}

static int case_not_found_requests_bounded_streaming_refresh(void)
{
    FakeWorld world;
    DlumaLua54Object object;
    DlumaLua54Error error;
    fake_init_world(&world);
    world.object_count = 1;
    fake_init_object(&world, 0, 2, "InitialObject", "ModelObject");
    CHECK(fake_start_ready(&world, DLUMA_NATIVE_READY_WORLD_FIND, 62));
    CHECK(!fake_find(1, 62, "LateObject", "", &object, &error));
    CHECK(fake_streq(error.code, "object_not_found"));

    fake_init_object(&world, 1, 3, "LateObject", "ModelObject");
    world.object_count = 2;
    world.clock_ms += 3000u;
    dluma_native_gameplay_game_thread_tick();
    CHECK(dluma_native_gameplay_index_phase() == DLUMA_NATIVE_INDEX_SCANNING_IDS);
    CHECK(fake_scan_to_terminal());
    CHECK(fake_find(1, 62, "LateObject", "ModelObject", &object, &error));
    dluma_native_gameplay_stop();
    return 1;
}

static int case_complete_object_rejection_continues(void)
{
    FakeWorld world;
    DlumaLua54Object object;
    DlumaLua54Error error;
    DlumaNativeIndexDiagnostics diagnostics;
    fake_init_world(&world);
    world.object_count = 4;
    fake_init_object(&world, 0, 2, "NoComplete", "ModelObject");
    world.objects[0].complete_object_valid = 0;
    fake_init_object(&world, 1, 3, "NullComplete", "ModelObject");
    world.objects[1].complete_object = 0;
    fake_init_object(&world, 2, 4, "BadIgsOffset", "ModelObject");
    world.objects[2].igsobject_subobject_offset = 0x0400u;
    fake_init_object(&world, 3, 7935, "Complete", "ModelObject");
    CHECK(fake_start_ready(&world, DLUMA_NATIVE_READY_WORLD_FIND, 8));
    CHECK(fake_find(1, 8, "Complete", "", &object, &error));
    memset(&diagnostics, 0, sizeof(diagnostics));
    diagnostics.struct_size = sizeof(diagnostics);
    CHECK(dluma_native_gameplay_index_diagnostics(&diagnostics));
    CHECK(diagnostics.rejected_complete_object >= 3u);
    dluma_native_gameplay_stop();
    return 1;
}

static int case_duplicate_exact_names_rejected(void)
{
    FakeWorld world;
    DlumaLua54Object object;
    DlumaLua54Error error;
    fake_init_world(&world);
    world.objects[3].name = "DLUMA_LocalA";
    fake_link_linear(&world);
    CHECK(fake_start_ready(&world, DLUMA_NATIVE_READY_WORLD_FIND, 9));
    CHECK(!fake_find(1, 9, "DLUMA_LocalA", "", &object, &error));
    CHECK(fake_streq(error.code, "duplicate_object_name"));
    dluma_native_gameplay_stop();
    return 1;
}

static int case_handle_copies_metadata(void)
{
    FakeWorld world;
    DlumaLua54Object object;
    DlumaLua54Object info;
    DlumaLua54Error error;
    fake_init_world(&world);
    fake_link_linear(&world);
    CHECK(fake_start_ready(&world, DLUMA_NATIVE_READY_WORLD_FIND, 10));
    CHECK(fake_find(44, 10, "DLUMA_LocalA", "ModelObject", &object, &error));
    CHECK(object.handle_id != 0 && object.owner_id == 44 && object.object_id == 12);
    memset(&info, 0, sizeof(info));
    fake_error(&error);
    CHECK(dluma_native_gameplay_world_get_info(
        44, object.handle_id, 10, &info, &error));
    CHECK(info.handle_id == object.handle_id && info.owner_id == 44 &&
          fake_streq(info.name, "DLUMA_LocalA") &&
          fake_streq(info.class_name, "ModelObject"));
    dluma_native_gameplay_stop();
    return 1;
}

static int case_world_position_revalidates_handle_and_copies_vector(void)
{
    FakeWorld world;
    DlumaLua54Object object;
    DlumaLua54Vec3 position;
    DlumaLua54Error error;
    fake_init_world(&world);
    fake_link_linear(&world);
    CHECK(fake_start_ready(&world,
        DLUMA_NATIVE_READY_WORLD_FIND | DLUMA_NATIVE_READY_WORLD_POSITION, 70));
    CHECK(dluma_native_gameplay_supported_capabilities() &
          DLUMA_LUA54_CAP_WORLD_POSITION);
    CHECK(dluma_native_gameplay_capabilities() & DLUMA_LUA54_CAP_WORLD_POSITION);
    CHECK(fake_find(44, 70, "DLUMA_LocalA", "ModelObject", &object, &error));
    memset(&position, 0, sizeof(position));
    fake_error(&error);
    CHECK(dluma_native_gameplay_world_get_position(
        44, object.handle_id, 70, &position, &error));
    CHECK(position.struct_size == sizeof(position));
    CHECK(position.x == 12.0f && position.y == 12.25f && position.z == 12.5f);
    memset(&position, 0, sizeof(position));
    CHECK(dluma_native_gameplay_world_get_forward(
        44, object.handle_id, 70, &position, &error));
    CHECK(position.struct_size == sizeof(position));
    CHECK(position.x == 0.0f && position.y == 0.0f && position.z == 1.0f);
    world.objects[2].world_forward_fail = 1;
    fake_error(&error);
    CHECK(!dluma_native_gameplay_world_get_forward(
        44, object.handle_id, 70, &position, &error));
    CHECK(fake_streq(error.code, "world_forward_unavailable"));
    world.objects[2].world_forward_fail = 0;
    world.objects[2].world_position_fail = 1;
    fake_error(&error);
    CHECK(!dluma_native_gameplay_world_get_position(
        44, object.handle_id, 70, &position, &error));
    CHECK(fake_streq(error.code, "world_position_unavailable"));
    world.objects[2].world_position_fail = 0;
    fake_change_generation(&world, 71);
    fake_error(&error);
    CHECK(!dluma_native_gameplay_world_get_position(
        44, object.handle_id, 70, &position, &error));
    CHECK(fake_streq(error.code, "stale_object_handle"));
    dluma_native_gameplay_stop();
    return 1;
}

static int case_wrong_expected_class(void)
{
    FakeWorld world;
    DlumaLua54Object object;
    DlumaLua54Error error;
    fake_init_world(&world);
    fake_link_linear(&world);
    CHECK(fake_start_ready(&world, DLUMA_NATIVE_READY_WORLD_FIND, 11));
    CHECK(!fake_find(1, 11, "DLUMA_LocalA", "SensorArea", &object, &error));
    CHECK(fake_streq(error.code, "wrong_object_class"));
    dluma_native_gameplay_stop();
    return 1;
}

static int case_foreign_handle_rejected(void)
{
    FakeWorld world;
    DlumaLua54Object object;
    DlumaLua54Object info;
    DlumaLua54Error error;
    fake_init_world(&world);
    fake_link_linear(&world);
    CHECK(fake_start_ready(&world, DLUMA_NATIVE_READY_WORLD_FIND, 12));
    CHECK(fake_find(1, 12, "DLUMA_LocalA", "", &object, &error));
    fake_error(&error);
    CHECK(!dluma_native_gameplay_world_get_info(
        2, object.handle_id, 12, &info, &error));
    CHECK(fake_streq(error.code, "foreign_object_handle"));
    dluma_native_gameplay_stop();
    return 1;
}

static int case_stale_generation_handle(void)
{
    FakeWorld world;
    DlumaLua54Object object;
    DlumaLua54Object info;
    DlumaLua54Error error;
    fake_init_world(&world);
    fake_link_linear(&world);
    CHECK(fake_start_ready(&world, DLUMA_NATIVE_READY_WORLD_FIND, 13));
    CHECK(fake_find(1, 13, "DLUMA_LocalA", "", &object, &error));
    fake_change_generation(&world, 14);
    fake_error(&error);
    CHECK(!dluma_native_gameplay_world_get_info(
        1, object.handle_id, 13, &info, &error));
    CHECK(fake_streq(error.code, "stale_object_handle"));
    dluma_native_gameplay_stop();
    return 1;
}

static int case_handle_revalidation_failure(void)
{
    FakeWorld world;
    DlumaLua54Object object;
    DlumaLua54Object info;
    DlumaLua54Error error;
    fake_init_world(&world);
    fake_link_linear(&world);
    CHECK(fake_start_ready(&world, DLUMA_NATIVE_READY_WORLD_FIND, 15));
    CHECK(fake_find(1, 15, "DLUMA_LocalA", "", &object, &error));
    world.objects[2].name = "DLUMA_Renamed";
    fake_error(&error);
    CHECK(!dluma_native_gameplay_world_get_info(
        1, object.handle_id, 15, &info, &error));
    CHECK(fake_streq(error.code, "stale_object_handle"));
    dluma_native_gameplay_stop();
    return 1;
}

static int case_handle_capacity_fails_closed(void)
{
    FakeWorld world;
    DlumaLua54Object object;
    DlumaLua54Error error;
    uint32_t i;
    fake_init_world(&world);
    world.object_count = 1;
    fake_init_object(&world, 0, 7935, "HandleTarget", "ModelObject");
    CHECK(fake_start_ready(&world, DLUMA_NATIVE_READY_WORLD_FIND, 35));
    for (i = 0; i < 512u; ++i)
        CHECK(fake_find((uint64_t)i + 1u, 35, "HandleTarget", "",
                        &object, &error));
    CHECK(!fake_find(513, 35, "HandleTarget", "", &object, &error));
    CHECK(fake_streq(error.code, "object_handle_limit"));
    dluma_native_gameplay_stop();
    return 1;
}

static int case_world_find_epoch_change_rejects_handle_commit(void)
{
    FakeWorld world;
    DlumaLua54Object object;
    DlumaLua54Error error;
    fake_init_world(&world);
    fake_link_linear(&world);
    CHECK(fake_start_ready(&world, DLUMA_NATIVE_READY_WORLD_FIND, 54));
    world.world_find_epoch_generation = 55;
    world.world_find_epoch_change_on_is_a = 1;
    CHECK(!fake_find(12, 54, "DLUMA_LocalA", "ModelObject", &object, &error));
    CHECK(fake_streq(error.code, "stale_map_generation"));
    CHECK(object.handle_id == 0u);
    CHECK(dluma_native_gameplay_index_phase() == DLUMA_NATIVE_INDEX_SCANNING_IDS);
    CHECK(fake_scan_to_terminal());
    CHECK(fake_find(12, 55, "DLUMA_LocalA", "ModelObject", &object, &error));
    CHECK(object.handle_id == 1u); /* failed commit consumed no handle ID */
    dluma_native_gameplay_stop();
    return 1;
}

static int case_derived_sensor_and_base_interface(void)
{
    FakeWorld world;
    DlumaLua54Object object;
    DlumaLua54Error error;
    int inside = 0;
    fake_init_world(&world);
    fake_link_linear(&world);
    CHECK(fake_start_ready(&world,
        DLUMA_NATIVE_READY_WORLD_FIND | DLUMA_NATIVE_READY_SENSOR, 16));
    CHECK(fake_find(1, 16, "DLUMA_TestSensor", "SensorArea", &object, &error));
    CHECK(world.objects[0].primary_vtable_identity == FAKE_VTABLE_SENSOR_GOTO);
    CHECK(world.objects[0].primary_vtable_identity != FAKE_VTABLE_SENSOR_AREA);
    CHECK(world.objects[0].checkinside_slot_offset == FAKE_CHECKINSIDE_SLOT &&
          world.objects[0].checkinside_target_identity ==
              FAKE_CHECKINSIDE_TARGET_SENTINEL);
    fake_error(&error);
    CHECK(dluma_native_gameplay_sensor_inside(
        1, 16, "DLUMA_TestSensor", &inside, &error));
    CHECK(world.sensor_last_interface == &world.objects[0].sensor_interface);
    CHECK(world.sensor_last_interface != &world.objects[0]);
    dluma_native_gameplay_stop();
    return 1;
}

static int case_wrong_sensor_class(void)
{
    FakeWorld world;
    DlumaLua54Error error;
    int inside = 0;
    fake_init_world(&world);
    fake_link_linear(&world);
    CHECK(fake_start_ready(&world,
        DLUMA_NATIVE_READY_WORLD_FIND | DLUMA_NATIVE_READY_SENSOR, 17));
    fake_error(&error);
    CHECK(!dluma_native_gameplay_sensor_inside(
        1, 17, "DLUMA_TestInteract", &inside, &error));
    CHECK(fake_streq(error.code, "wrong_object_class"));
    dluma_native_gameplay_stop();
    return 1;
}

static int case_sensor_base_unavailable(void)
{
    FakeWorld world;
    DlumaLua54Error error;
    int inside = 0;
    fake_init_world(&world);
    fake_link_linear(&world);
    CHECK(fake_start_ready(&world,
        DLUMA_NATIVE_READY_WORLD_FIND | DLUMA_NATIVE_READY_SENSOR, 18));
    world.objects[0].sensor_area_base_valid = 0;
    fake_error(&error);
    CHECK(!dluma_native_gameplay_sensor_inside(
        1, 18, "DLUMA_TestSensor", &inside, &error));
    CHECK(fake_streq(error.code, "sensor_base_unavailable"));
    world.objects[0].sensor_area_base_valid = 1;
    world.objects[0].sensor_area_subobject_offset = 0x0040u;
    fake_error(&error);
    CHECK(!dluma_native_gameplay_sensor_inside(
        1, 18, "DLUMA_TestSensor", &inside, &error));
    CHECK(fake_streq(error.code, "sensor_base_unavailable"));
    dluma_native_gameplay_stop();
    return 1;
}

static int case_sensor_checkinside_unvalidated(void)
{
    FakeWorld world;
    DlumaLua54Error error;
    int inside = 0;
    fake_init_world(&world);
    fake_link_linear(&world);
    CHECK(fake_start_ready(&world,
        DLUMA_NATIVE_READY_WORLD_FIND | DLUMA_NATIVE_READY_SENSOR, 19));
    world.objects[0].checkinside_target_valid = 0;
    fake_error(&error);
    CHECK(!dluma_native_gameplay_sensor_inside(
        1, 19, "DLUMA_TestSensor", &inside, &error));
    CHECK(fake_streq(error.code, "sensor_checkinside_unvalidated"));
    world.objects[0].checkinside_target_valid = 1;
    world.objects[0].checkinside_target_identity = 0x00ECB881ULL;
    fake_error(&error);
    CHECK(!dluma_native_gameplay_sensor_inside(
        1, 19, "DLUMA_TestSensor", &inside, &error));
    CHECK(fake_streq(error.code, "sensor_checkinside_unvalidated"));
    dluma_native_gameplay_stop();
    return 1;
}

static int case_sensor_baseline_enter_leave_once(void)
{
    FakeWorld world;
    DlumaLua54Error error;
    fake_init_world(&world);
    fake_link_linear(&world);
    CHECK(fake_start_ready(&world,
        DLUMA_NATIVE_READY_WORLD_FIND | DLUMA_NATIVE_READY_SENSOR, 20));
    fake_error(&error);
    CHECK(dluma_native_gameplay_subscription_add(
        2, 20, 10, DLUMA_LUA54_EVENT_SENSOR_ENTER,
        "DLUMA_TestSensor", &error));
    fake_error(&error);
    CHECK(dluma_native_gameplay_subscription_add(
        2, 20, 11, DLUMA_LUA54_EVENT_SENSOR_LEAVE,
        "DLUMA_TestSensor", &error));
    world.sensor_inside_calls = 0;
    world.clock_ms += 75;
    dluma_native_gameplay_game_thread_tick();
    CHECK(g_event_count == 0 && world.sensor_inside_calls == 1);
    world.objects[0].inside = 1;
    world.clock_ms += 75;
    dluma_native_gameplay_game_thread_tick();
    CHECK(g_event_count == 1 && world.sensor_inside_calls == 2 &&
          g_events[0].kind == DLUMA_LUA54_EVENT_SENSOR_ENTER);
    world.clock_ms += 75;
    dluma_native_gameplay_game_thread_tick();
    CHECK(g_event_count == 1 && world.sensor_inside_calls == 3);
    world.objects[0].inside = 0;
    world.clock_ms += 75;
    dluma_native_gameplay_game_thread_tick();
    CHECK(g_event_count == 2 && world.sensor_inside_calls == 4 &&
          g_events[1].kind == DLUMA_LUA54_EVENT_SENSOR_LEAVE);
    dluma_native_gameplay_stop();
    return 1;
}

static int case_retained_level_pumps_events_during_active_level_gap(void)
{
    FakeWorld world;
    DlumaLua54Error error;
    fake_init_world(&world);
    fake_link_linear(&world);
    CHECK(fake_start_ready(&world,
        DLUMA_NATIVE_READY_WORLD_FIND | DLUMA_NATIVE_READY_SENSOR |
        DLUMA_NATIVE_READY_INTERACT, 54));
    fake_error(&error);
    CHECK(dluma_native_gameplay_subscription_add(
        2, 54, 70, DLUMA_LUA54_EVENT_SENSOR_ENTER,
        "DLUMA_TestSensor", &error));
    fake_error(&error);
    CHECK(dluma_native_gameplay_subscription_add(
        2, 54, 71, DLUMA_LUA54_EVENT_SENSOR_LEAVE,
        "DLUMA_TestSensor", &error));
    fake_error(&error);
    CHECK(dluma_native_gameplay_subscription_add(
        2, 54, 72, DLUMA_LUA54_EVENT_INTERACT_BEGIN,
        "DLUMA_TestInteract", &error));
    world.clock_ms += 75;
    dluma_native_gameplay_game_thread_tick();
    CHECK(g_event_count == 0); /* establishes the outside sensor baseline */

    world.active_level_available = 0;
    world.objects[0].inside = 1;
    CHECK(dluma_native_gameplay_enqueue_interactive_item_event(
        &world.objects[1], 1, 1));
    world.clock_ms += 75;
    dluma_native_gameplay_game_thread_tick();
    CHECK(g_event_count == 2);
    CHECK(g_events[0].kind == DLUMA_LUA54_EVENT_INTERACT_BEGIN);
    CHECK(g_events[1].kind == DLUMA_LUA54_EVENT_SENSOR_ENTER);

    world.objects[0].inside = 0;
    world.clock_ms += 75;
    dluma_native_gameplay_game_thread_tick();
    CHECK(g_event_count == 3);
    CHECK(g_events[2].kind == DLUMA_LUA54_EVENT_SENSOR_LEAVE);
    dluma_native_gameplay_stop();
    return 1;
}

static int case_pending_sensor_subscription_survives_scan(void)
{
    FakeWorld world;
    DlumaLua54Error error;
    fake_init_world(&world);
    fake_link_linear(&world);
    CHECK(fake_begin(&world,
        DLUMA_NATIVE_READY_WORLD_FIND | DLUMA_NATIVE_READY_SENSOR, 21));
    fake_error(&error);
    CHECK(dluma_native_gameplay_subscription_add(
        3, 21, 20, DLUMA_LUA54_EVENT_SENSOR_ENTER,
        "DLUMA_TestSensor", &error));
    CHECK(fake_scan_to_terminal());
    CHECK(dluma_native_gameplay_index_phase() == DLUMA_NATIVE_INDEX_COMPLETE);
    world.clock_ms += 75;
    dluma_native_gameplay_game_thread_tick();
    CHECK(g_event_count == 0); /* first post-resolution sample is baseline */
    world.objects[0].inside = 1;
    world.clock_ms += 75;
    dluma_native_gameplay_game_thread_tick();
    CHECK(g_event_count == 1 &&
          g_events[0].kind == DLUMA_LUA54_EVENT_SENSOR_ENTER);
    dluma_native_gameplay_stop();
    return 1;
}

static int case_removed_sensor_subscription_is_silent(void)
{
    FakeWorld world;
    DlumaLua54Error error;
    fake_init_world(&world);
    fake_link_linear(&world);
    CHECK(fake_start_ready(&world,
        DLUMA_NATIVE_READY_WORLD_FIND | DLUMA_NATIVE_READY_SENSOR, 22));
    fake_error(&error);
    CHECK(dluma_native_gameplay_subscription_add(
        9, 22, 21, DLUMA_LUA54_EVENT_SENSOR_ENTER,
        "DLUMA_TestSensor", &error));
    world.clock_ms += 75;
    dluma_native_gameplay_game_thread_tick();
    fake_error(&error);
    CHECK(dluma_native_gameplay_subscription_remove(
        9, 21, DLUMA_LUA54_EVENT_SENSOR_ENTER,
        "DLUMA_TestSensor", &error));
    world.objects[0].inside = 1;
    world.clock_ms += 75;
    dluma_native_gameplay_game_thread_tick();
    CHECK(g_event_count == 0);
    dluma_native_gameplay_stop();
    return 1;
}

static int case_sensor_generation_change_clears_baseline(void)
{
    FakeWorld world;
    DlumaLua54Error error;
    fake_init_world(&world);
    fake_link_linear(&world);
    CHECK(fake_start_ready(&world,
        DLUMA_NATIVE_READY_WORLD_FIND | DLUMA_NATIVE_READY_SENSOR, 43));
    fake_error(&error);
    CHECK(dluma_native_gameplay_subscription_add(
        8, 43, 51, DLUMA_LUA54_EVENT_SENSOR_ENTER,
        "DLUMA_TestSensor", &error));
    world.clock_ms += 75;
    dluma_native_gameplay_game_thread_tick(); /* outside baseline */
    world.objects[0].inside = 1;
    world.clock_ms += 75;
    dluma_native_gameplay_game_thread_tick();
    CHECK(g_event_count == 1u);

    fake_change_generation(&world, 44);
    CHECK(fake_scan_to_terminal());
    fake_error(&error);
    CHECK(dluma_native_gameplay_subscription_add(
        8, 44, 52, DLUMA_LUA54_EVENT_SENSOR_ENTER,
        "DLUMA_TestSensor", &error));
    world.clock_ms += 75;
    dluma_native_gameplay_game_thread_tick();
    CHECK(g_event_count == 1u); /* inside is a fresh baseline, not an edge */
    world.objects[0].inside = 0;
    world.clock_ms += 75;
    dluma_native_gameplay_game_thread_tick();
    CHECK(g_event_count == 1u); /* no leave subscription */
    world.objects[0].inside = 1;
    world.clock_ms += 75;
    dluma_native_gameplay_game_thread_tick();
    CHECK(g_event_count == 2u &&
          g_events[1].kind == DLUMA_LUA54_EVENT_SENSOR_ENTER &&
          g_events[1].generation == 44u);
    dluma_native_gameplay_stop();
    return 1;
}

static int case_sensor_remove_readd_during_sample_rejects_commit(void)
{
    FakeWorld world;
    DlumaLua54Error error;
    fake_init_world(&world);
    fake_link_linear(&world);
    CHECK(fake_start_ready(&world,
        DLUMA_NATIVE_READY_WORLD_FIND | DLUMA_NATIVE_READY_SENSOR, 51));
    fake_error(&error);
    CHECK(dluma_native_gameplay_subscription_add(
        9, 51, 80, DLUMA_LUA54_EVENT_SENSOR_ENTER,
        "DLUMA_TestSensor", &error));
    world.clock_ms += 75;
    dluma_native_gameplay_game_thread_tick();
    CHECK(g_event_count == 0u);

    world.objects[0].inside = 1;
    world.sensor_interleave_owner = 9;
    world.sensor_interleave_token = 80;
    world.sensor_interleave_kind = DLUMA_LUA54_EVENT_SENSOR_ENTER;
    world.sensor_interleave_generation = 51;
    world.sensor_interleave_name = "DLUMA_TestSensor";
    world.sensor_interleave_action = FAKE_SENSOR_INTERLEAVE_REMOVE_READD;
    world.clock_ms += 75;
    dluma_native_gameplay_game_thread_tick();
    CHECK(world.sensor_interleave_ok && g_event_count == 0u);

    world.clock_ms += 75;
    dluma_native_gameplay_game_thread_tick();
    CHECK(g_event_count == 0u); /* replacement registration baselines inside */
    world.objects[0].inside = 0;
    world.clock_ms += 75;
    dluma_native_gameplay_game_thread_tick();
    CHECK(g_event_count == 0u);
    world.objects[0].inside = 1;
    world.clock_ms += 75;
    dluma_native_gameplay_game_thread_tick();
    CHECK(g_event_count == 1u &&
          g_events[0].kind == DLUMA_LUA54_EVENT_SENSOR_ENTER);
    dluma_native_gameplay_stop();
    return 1;
}

static int case_sensor_generation_reset_during_sample_rejects_commit(void)
{
    FakeWorld world;
    DlumaLua54Error error;
    fake_init_world(&world);
    fake_link_linear(&world);
    CHECK(fake_start_ready(&world,
        DLUMA_NATIVE_READY_WORLD_FIND | DLUMA_NATIVE_READY_SENSOR, 52));
    fake_error(&error);
    CHECK(dluma_native_gameplay_subscription_add(
        10, 52, 81, DLUMA_LUA54_EVENT_SENSOR_ENTER,
        "DLUMA_TestSensor", &error));
    world.clock_ms += 75;
    dluma_native_gameplay_game_thread_tick();
    CHECK(g_event_count == 0u);

    world.objects[0].inside = 1;
    world.sensor_interleave_generation = 53;
    world.sensor_interleave_action = FAKE_SENSOR_INTERLEAVE_GENERATION_RESET;
    world.clock_ms += 75;
    dluma_native_gameplay_game_thread_tick();
    CHECK(world.sensor_interleave_ok && g_event_count == 0u);
    CHECK(dluma_native_gameplay_index_phase() == DLUMA_NATIVE_INDEX_SCANNING_IDS);
    CHECK(fake_scan_to_terminal());
    world.clock_ms += 75;
    dluma_native_gameplay_game_thread_tick();
    CHECK(g_event_count == 0u); /* new generation baselines inside */
    world.objects[0].inside = 0;
    world.clock_ms += 75;
    dluma_native_gameplay_game_thread_tick();
    world.objects[0].inside = 1;
    world.clock_ms += 75;
    dluma_native_gameplay_game_thread_tick();
    CHECK(g_event_count == 1u && g_events[0].generation == 53u);
    dluma_native_gameplay_stop();
    return 1;
}

static int case_sensor_failed_poll_still_attempted_once_per_name(void)
{
    FakeWorld world;
    DlumaLua54Error error;
    fake_init_world(&world);
    fake_link_linear(&world);
    CHECK(fake_start_ready(&world,
        DLUMA_NATIVE_READY_WORLD_FIND | DLUMA_NATIVE_READY_SENSOR, 56));
    fake_error(&error);
    CHECK(dluma_native_gameplay_subscription_add(
        11, 56, 82, DLUMA_LUA54_EVENT_SENSOR_ENTER,
        "DLUMA_TestSensor", &error));
    fake_error(&error);
    CHECK(dluma_native_gameplay_subscription_add(
        11, 56, 83, DLUMA_LUA54_EVENT_SENSOR_LEAVE,
        "DLUMA_TestSensor", &error));
    world.sensor_inside_calls = 0;
    world.objects[0].checkinside_target_valid = 0;
    world.clock_ms += 75;
    dluma_native_gameplay_game_thread_tick();
    CHECK(world.sensor_inside_calls == 1u && g_event_count == 0u);
    dluma_native_gameplay_stop();
    return 1;
}

static int case_nonzero_local_enable_interface(void)
{
    FakeWorld world;
    DlumaLua54Object object;
    DlumaLua54Error error;
    int enabled = -1;
    fake_init_world(&world);
    fake_link_linear(&world);
    CHECK(fake_start_ready(&world,
        DLUMA_NATIVE_READY_WORLD_FIND | DLUMA_NATIVE_READY_LOCAL_ENABLE, 23));
    CHECK(fake_find(7, 23, "DLUMA_LocalA", "", &object, &error));
    world.local_get_calls = 0;
    world.objects[2].local_enable_subobject_offset = 0x0100u;
    fake_error(&error);
    CHECK(!dluma_native_gameplay_world_get_enabled(
        7, object.handle_id, 23, &enabled, &error));
    CHECK(fake_streq(error.code, "local_enable_unavailable"));
    world.objects[2].local_enable_subobject_offset =
        FAKE_ILOCAL_ENABLE_OFFSET;
    fake_error(&error);
    CHECK(dluma_native_gameplay_world_get_enabled(
        7, object.handle_id, 23, &enabled, &error));
    CHECK(enabled == 0 && world.local_get_calls == 1);
    CHECK(world.objects[2].local_enable_subobject_offset ==
          FAKE_ILOCAL_ENABLE_OFFSET);
    CHECK(world.local_last_interface == &world.objects[2].local_interface);
    CHECK(world.local_last_interface != &world.objects[2]);
    dluma_native_gameplay_stop();
    return 1;
}

static int case_atomic_preflight_changes_nothing(void)
{
    FakeWorld world;
    DlumaLua54SetEnabledRequest request;
    DlumaLua54SetEnabledResult result;
    DlumaLua54Error error;
    fake_init_world(&world);
    fake_link_linear(&world);
    CHECK(fake_start_ready(&world,
        DLUMA_NATIVE_READY_WORLD_FIND | DLUMA_NATIVE_READY_LOCAL_ENABLE, 24));
    world.local_get_calls = 0;
    world.local_set_calls = 0;
    fake_enabled_request(&request, 77, 24, 1);
    request.requested_count = 2;
    dluma_str_copy(request.names[0], DLUMA_ARRAY_COUNT(request.names[0]), "DLUMA_LocalA");
    dluma_str_copy(request.names[1], DLUMA_ARRAY_COUNT(request.names[1]), "DLUMA_NoLocalEnable");
    fake_error(&error);
    CHECK(!dluma_native_gameplay_world_set_enabled(77, &request, &result, &error));
    CHECK(fake_streq(error.code, "local_enable_unavailable"));
    CHECK(world.objects[2].enabled == 0 && world.local_set_calls == 0);
    dluma_native_gameplay_stop();
    return 1;
}

static int case_duplicate_atomic_request_rejected(void)
{
    FakeWorld world;
    DlumaLua54SetEnabledRequest request;
    DlumaLua54SetEnabledResult result;
    DlumaLua54Error error;
    fake_init_world(&world);
    fake_link_linear(&world);
    CHECK(fake_start_ready(&world,
        DLUMA_NATIVE_READY_WORLD_FIND | DLUMA_NATIVE_READY_LOCAL_ENABLE, 25));
    fake_enabled_request(&request, 1, 25, 1);
    request.requested_count = 2;
    dluma_str_copy(request.names[0], DLUMA_ARRAY_COUNT(request.names[0]), "DLUMA_LocalA");
    dluma_str_copy(request.names[1], DLUMA_ARRAY_COUNT(request.names[1]), "DLUMA_LocalA");
    fake_error(&error);
    CHECK(!dluma_native_gameplay_world_set_enabled(1, &request, &result, &error));
    CHECK(fake_streq(error.code, "duplicate_request_name"));
    CHECK(world.objects[2].enabled == 0 && world.local_set_calls == 0);
    dluma_native_gameplay_stop();
    return 1;
}

static int case_partial_commit_readback_failure(void)
{
    FakeWorld world;
    DlumaLua54SetEnabledRequest request;
    DlumaLua54SetEnabledResult result;
    DlumaLua54Error error;
    fake_init_world(&world);
    fake_link_linear(&world);
    CHECK(fake_start_ready(&world,
        DLUMA_NATIVE_READY_WORLD_FIND | DLUMA_NATIVE_READY_LOCAL_ENABLE, 26));
    world.local_get_calls = 0;
    world.local_set_calls = 0;
    world.objects[4].local_get_fail_after_set = 1;
    fake_enabled_request(&request, 2, 26, 1);
    request.requested_count = 2;
    dluma_str_copy(request.names[0], DLUMA_ARRAY_COUNT(request.names[0]), "DLUMA_LocalA");
    dluma_str_copy(request.names[1], DLUMA_ARRAY_COUNT(request.names[1]), "DLUMA_LocalB");
    fake_error(&error);
    CHECK(!dluma_native_gameplay_world_set_enabled(2, &request, &result, &error));
    CHECK(fake_streq(error.code, "local_enable_commit_failed"));
    CHECK(result.partial == 1u && result.changed_count == 1u &&
          result.name_count == 1u && fake_streq(result.names[0], "DLUMA_LocalA"));
    CHECK(world.objects[2].enabled == 1 && world.objects[4].enabled == 1);
    dluma_native_gameplay_stop();
    return 1;
}

static int case_unchanged_then_precommit_failure_is_not_partial(void)
{
    FakeWorld world;
    DlumaLua54SetEnabledRequest request;
    DlumaLua54SetEnabledResult result;
    DlumaLua54Error error;
    fake_init_world(&world);
    world.objects[2].enabled = 1;
    fake_link_linear(&world);
    CHECK(fake_start_ready(&world,
        DLUMA_NATIVE_READY_WORLD_FIND | DLUMA_NATIVE_READY_LOCAL_ENABLE, 49));
    world.local_get_calls = 0;
    world.local_set_calls = 0;
    world.objects[2].local_get_call_count = 0;
    world.objects[4].local_get_call_count = 0;
    world.objects[4].local_get_fail_on_call = 2;
    fake_enabled_request(&request, 6, 49, 1);
    request.requested_count = 2;
    dluma_str_copy(request.names[0], DLUMA_ARRAY_COUNT(request.names[0]),
                 "DLUMA_LocalA");
    dluma_str_copy(request.names[1], DLUMA_ARRAY_COUNT(request.names[1]),
                 "DLUMA_LocalB");
    fake_error(&error);
    CHECK(!dluma_native_gameplay_world_set_enabled(
        6, &request, &result, &error));
    CHECK(fake_streq(error.code, "local_enable_commit_failed"));
    CHECK(result.partial == 0u && result.changed_count == 0u &&
          result.unchanged_count == 1u && result.name_count == 1u &&
          fake_streq(result.names[0], "DLUMA_LocalA"));
    CHECK(world.local_set_calls == 0u &&
          world.objects[2].enabled == 1 && world.objects[4].enabled == 0);
    dluma_native_gameplay_stop();
    return 1;
}

static int case_local_enable_unchanged_result(void)
{
    FakeWorld world;
    DlumaLua54SetEnabledRequest request;
    DlumaLua54SetEnabledResult result;
    DlumaLua54Error error;
    fake_init_world(&world);
    world.objects[2].enabled = 1;
    fake_link_linear(&world);
    CHECK(fake_start_ready(&world,
        DLUMA_NATIVE_READY_WORLD_FIND | DLUMA_NATIVE_READY_LOCAL_ENABLE, 27));
    world.local_set_calls = 0;
    fake_enabled_request(&request, 2, 27, 1);
    request.requested_count = 1;
    dluma_str_copy(request.names[0], DLUMA_ARRAY_COUNT(request.names[0]), "DLUMA_LocalA");
    fake_error(&error);
    CHECK(dluma_native_gameplay_world_set_enabled(2, &request, &result, &error));
    CHECK(result.changed_count == 0u && result.unchanged_count == 1u &&
          result.partial == 0u && result.name_count == 1u);
    CHECK(world.local_set_calls == 0);
    dluma_native_gameplay_stop();
    return 1;
}

static int case_interaction_queue_dispatch(void)
{
    FakeWorld world;
    DlumaLua54Error error;
    fake_init_world(&world);
    fake_link_linear(&world);
    CHECK(fake_start_ready(&world,
        DLUMA_NATIVE_READY_WORLD_FIND | DLUMA_NATIVE_READY_INTERACT, 28));
    fake_error(&error);
    CHECK(dluma_native_gameplay_subscription_add(
        3, 28, 41, DLUMA_LUA54_EVENT_INTERACT_BEGIN,
        "DLUMA_TestInteract", &error));
    CHECK(dluma_native_gameplay_enqueue_interactive_item_event(
        &world.objects[1], 1, 1));
    dluma_native_gameplay_game_thread_tick();
    CHECK(g_event_count == 1 &&
          g_events[0].kind == DLUMA_LUA54_EVENT_INTERACT_BEGIN &&
          g_events[0].generation == 28);
    dluma_native_gameplay_stop();
    return 1;
}

static int case_interaction_original_once_result_preserved(void)
{
    FakeWorld world;
    DlumaLua54Error error;
    __int64 context = 99;
    int result;
    fake_init_world(&world);
    fake_link_linear(&world);
    CHECK(fake_start_ready(&world,
        DLUMA_NATIVE_READY_WORLD_FIND | DLUMA_NATIVE_READY_INTERACT, 34));
    fake_error(&error);
    CHECK(dluma_native_gameplay_subscription_add(
        3, 34, 45, DLUMA_LUA54_EVENT_INTERACT_BEGIN,
        "DLUMA_TestInteract", &error));
    result = dluma_native_gameplay_fake_invoke_interaction_observer(
        fake_interaction_original, &world.objects[1], 1, &context);
    CHECK(result == 37 && g_interaction_original_calls == 1u && context == 100);
    dluma_native_gameplay_game_thread_tick();
    CHECK(g_event_count == 1u &&
          g_events[0].kind == DLUMA_LUA54_EVENT_INTERACT_BEGIN);
    dluma_native_gameplay_stop();
    return 1;
}

static int case_interaction_nonexecute_action_observed_only(void)
{
    FakeWorld world;
    DlumaLua54Error error;
    __int64 context = 0;
    int result;
    fake_init_world(&world);
    fake_link_linear(&world);
    CHECK(fake_start_ready(&world,
        DLUMA_NATIVE_READY_WORLD_FIND | DLUMA_NATIVE_READY_INTERACT, 45));
    fake_error(&error);
    CHECK(dluma_native_gameplay_subscription_add(
        3, 45, 53, DLUMA_LUA54_EVENT_INTERACT_BEGIN,
        "DLUMA_TestInteract", &error));
    result = dluma_native_gameplay_fake_invoke_interaction_observer(
        fake_interaction_original, &world.objects[1], 2, &context);
    CHECK(result == 37 && g_interaction_original_calls == 1u);
    dluma_native_gameplay_game_thread_tick();
    CHECK(g_event_count == 0u);
    dluma_native_gameplay_stop();
    return 1;
}

static int case_interaction_failed_original_does_not_queue(void)
{
    FakeWorld world;
    DlumaLua54Error error;
    __int64 context = 0;
    int result;
    fake_init_world(&world);
    fake_link_linear(&world);
    CHECK(fake_start_ready(&world,
        DLUMA_NATIVE_READY_WORLD_FIND | DLUMA_NATIVE_READY_INTERACT, 46));
    fake_error(&error);
    CHECK(dluma_native_gameplay_subscription_add(
        3, 46, 54, DLUMA_LUA54_EVENT_INTERACT_BEGIN,
        "DLUMA_TestInteract", &error));
    result = dluma_native_gameplay_fake_invoke_interaction_observer(
        fake_interaction_failed_original, &world.objects[1], 1, &context);
    CHECK(result == 0 && g_interaction_original_calls == 1u);
    dluma_native_gameplay_game_thread_tick();
    CHECK(g_event_count == 0u);
    dluma_native_gameplay_stop();
    return 1;
}

static int case_interaction_without_subscription_does_not_queue(void)
{
    FakeWorld world;
    __int64 context = 0;
    int result;
    fake_init_world(&world);
    fake_link_linear(&world);
    CHECK(fake_start_ready(&world,
        DLUMA_NATIVE_READY_WORLD_FIND | DLUMA_NATIVE_READY_INTERACT, 47));
    result = dluma_native_gameplay_fake_invoke_interaction_observer(
        fake_interaction_original, &world.objects[1], 1, &context);
    CHECK(result == 37 && g_interaction_original_calls == 1u);
    dluma_native_gameplay_game_thread_tick();
    CHECK(g_event_count == 0u);
    dluma_native_gameplay_stop();
    return 1;
}

static int case_interaction_remove_observer_interleavings(void)
{
    FakeWorld world;
    DlumaLua54Error error;
    __int64 context = 0;
    fake_init_world(&world);
    fake_link_linear(&world);
    CHECK(fake_start_ready(&world,
        DLUMA_NATIVE_READY_WORLD_FIND | DLUMA_NATIVE_READY_INTERACT, 48));

    fake_error(&error);
    CHECK(dluma_native_gameplay_subscription_add(
        4, 48, 55, DLUMA_LUA54_EVENT_INTERACT_BEGIN,
        "DLUMA_TestInteract", &error));
    fake_error(&error);
    CHECK(dluma_native_gameplay_subscription_remove(
        4, 55, DLUMA_LUA54_EVENT_INTERACT_BEGIN,
        "DLUMA_TestInteract", &error));
    CHECK(dluma_native_gameplay_fake_invoke_interaction_observer(
        fake_interaction_original, &world.objects[1], 1, &context) == 37);
    dluma_native_gameplay_game_thread_tick();
    CHECK(g_event_count == 0u);

    fake_error(&error);
    CHECK(dluma_native_gameplay_subscription_add(
        4, 48, 56, DLUMA_LUA54_EVENT_INTERACT_BEGIN,
        "DLUMA_TestInteract", &error));
    CHECK(dluma_native_gameplay_fake_invoke_interaction_observer(
        fake_interaction_original, &world.objects[1], 1, &context) == 37);
    fake_error(&error);
    CHECK(dluma_native_gameplay_subscription_remove(
        4, 56, DLUMA_LUA54_EVENT_INTERACT_BEGIN,
        "DLUMA_TestInteract", &error));
    dluma_native_gameplay_game_thread_tick();
    CHECK(g_event_count == 0u && g_interaction_original_calls == 2u);
    dluma_native_gameplay_stop();
    return 1;
}

static int case_interaction_remove_readd_aba_discards_old_event(void)
{
    FakeWorld world;
    DlumaLua54Error error;
    fake_init_world(&world);
    fake_link_linear(&world);
    CHECK(fake_start_ready(&world,
        DLUMA_NATIVE_READY_WORLD_FIND | DLUMA_NATIVE_READY_INTERACT, 57));

    fake_error(&error);
    CHECK(dluma_native_gameplay_subscription_add(
        12, 57, 84, DLUMA_LUA54_EVENT_INTERACT_BEGIN,
        "DLUMA_TestInteract", &error));
    CHECK(dluma_native_gameplay_enqueue_interactive_item_event(
        &world.objects[1], 1, 1));

    fake_error(&error);
    CHECK(dluma_native_gameplay_subscription_remove(
        12, 84, DLUMA_LUA54_EVENT_INTERACT_BEGIN,
        "DLUMA_TestInteract", &error));
    fake_error(&error);
    CHECK(dluma_native_gameplay_subscription_add(
        13, 57, 85, DLUMA_LUA54_EVENT_INTERACT_BEGIN,
        "DLUMA_TestInteract", &error));
    dluma_native_gameplay_game_thread_tick();
    CHECK(g_event_count == 0u); /* A's queued event must not bind to B. */

    CHECK(dluma_native_gameplay_enqueue_interactive_item_event(
        &world.objects[1], 1, 1));
    dluma_native_gameplay_game_thread_tick();
    CHECK(g_event_count == 1u &&
          g_events[0].kind == DLUMA_LUA54_EVENT_INTERACT_BEGIN &&
          g_events[0].generation == 57u &&
          g_events[0].subscription_revision != 0u);
    dluma_native_gameplay_stop();
    return 1;
}

static int case_interaction_old_generation_discarded(void)
{
    FakeWorld world;
    DlumaLua54Error error;
    fake_init_world(&world);
    fake_link_linear(&world);
    CHECK(fake_start_ready(&world,
        DLUMA_NATIVE_READY_WORLD_FIND | DLUMA_NATIVE_READY_INTERACT, 29));
    fake_error(&error);
    CHECK(dluma_native_gameplay_subscription_add(
        3, 29, 42, DLUMA_LUA54_EVENT_INTERACT_BEGIN,
        "DLUMA_TestInteract", &error));
    CHECK(dluma_native_gameplay_enqueue_interactive_item_event(
        &world.objects[1], 1, 1));
    fake_change_generation(&world, 30);
    dluma_native_gameplay_game_thread_tick();
    CHECK(g_event_count == 0);
    dluma_native_gameplay_stop();
    return 1;
}

static int case_generation_invalidates_handle_subscription_and_event(void)
{
    FakeWorld world;
    DlumaLua54Object object;
    DlumaLua54Object info;
    DlumaLua54Error error;
    fake_init_world(&world);
    fake_link_linear(&world);
    CHECK(fake_start_ready(&world,
        DLUMA_NATIVE_READY_WORLD_FIND | DLUMA_NATIVE_READY_INTERACT, 36));
    CHECK(fake_find(55, 36, "DLUMA_TestInteract", "InteractiveItem",
                    &object, &error));
    fake_error(&error);
    CHECK(dluma_native_gameplay_subscription_add(
        55, 36, 46, DLUMA_LUA54_EVENT_INTERACT_BEGIN,
        "DLUMA_TestInteract", &error));
    CHECK(dluma_native_gameplay_enqueue_interactive_item_event(
        &world.objects[1], 1, 1));

    fake_change_generation(&world, 37);
    fake_error(&error);
    CHECK(!dluma_native_gameplay_world_get_info(
        55, object.handle_id, 36, &info, &error));
    CHECK(fake_streq(error.code, "stale_object_handle"));
    CHECK(!dluma_native_gameplay_enqueue_interactive_item_event(
        &world.objects[1], 1, 1)); /* old resolved binding is invalid */
    CHECK(fake_scan_to_terminal());
    CHECK(dluma_native_gameplay_index_phase() == DLUMA_NATIVE_INDEX_COMPLETE);
    CHECK(dluma_native_gameplay_enqueue_interactive_item_event(
        &world.objects[1], 1, 1));
    dluma_native_gameplay_game_thread_tick();
    CHECK(g_event_count == 1u && g_events[0].generation == 37u);
    dluma_native_gameplay_stop();
    return 1;
}

static int case_interaction_removed_subscription_no_queue(void)
{
    FakeWorld world;
    DlumaLua54Error error;
    fake_init_world(&world);
    fake_link_linear(&world);
    CHECK(fake_start_ready(&world,
        DLUMA_NATIVE_READY_WORLD_FIND | DLUMA_NATIVE_READY_INTERACT, 31));
    fake_error(&error);
    CHECK(dluma_native_gameplay_subscription_add(
        3, 31, 43, DLUMA_LUA54_EVENT_INTERACT_BEGIN,
        "DLUMA_TestInteract", &error));
    fake_error(&error);
    CHECK(dluma_native_gameplay_subscription_remove(
        3, 43, DLUMA_LUA54_EVENT_INTERACT_BEGIN,
        "DLUMA_TestInteract", &error));
    CHECK(!dluma_native_gameplay_enqueue_interactive_item_event(
        &world.objects[1], 1, 1));
    dluma_native_gameplay_game_thread_tick();
    CHECK(g_event_count == 0);
    dluma_native_gameplay_stop();
    return 1;
}

static int case_interaction_queue_bounded(void)
{
    FakeWorld world;
    DlumaLua54Error error;
    uint32_t i;
    fake_init_world(&world);
    fake_link_linear(&world);
    CHECK(fake_start_ready(&world,
        DLUMA_NATIVE_READY_WORLD_FIND | DLUMA_NATIVE_READY_INTERACT, 32));
    fake_error(&error);
    CHECK(dluma_native_gameplay_subscription_add(
        3, 32, 44, DLUMA_LUA54_EVENT_INTERACT_BEGIN,
        "DLUMA_TestInteract", &error));
    for (i = 0; i < 64u; ++i)
        CHECK(dluma_native_gameplay_enqueue_interactive_item_event(
            &world.objects[1], 1, 1));
    CHECK(!dluma_native_gameplay_enqueue_interactive_item_event(
        &world.objects[1], 1, 1));
    CHECK(g_overflow_logs == 0u); /* hook path never logs */
    dluma_native_gameplay_game_thread_tick();
    CHECK(g_event_count == 64u && g_overflow_logs == 1u);
    dluma_native_gameplay_stop();
    return 1;
}

static int case_index_capacity_degrades(void)
{
    FakeWorld world;
    FakeObject* objects;
    DlumaNativeIndexDiagnostics diagnostics;
    uint32_t i;
    fake_init_world(&world);
    objects = (FakeObject*)calloc(8193u, sizeof(*objects));
    CHECK(objects != 0);
    world.objects = objects;
    world.object_count = 8193u;
    for (i = 0; i < world.object_count; ++i) {
        fake_init_object(&world, i, (int32_t)i, "CapacityObject", "ModelObject");
        objects[i].previous = i ? &objects[i - 1] : 0;
        objects[i].next = i + 1u < world.object_count ? &objects[i + 1] : 0;
    }
    CHECK(fake_begin(&world, DLUMA_NATIVE_READY_WORLD_FIND, 33));
    CHECK(fake_scan_to_terminal());
    CHECK(dluma_native_gameplay_index_phase() == DLUMA_NATIVE_INDEX_DEGRADED);
    CHECK(dluma_native_gameplay_capabilities() == 0);
    memset(&diagnostics, 0, sizeof(diagnostics));
    diagnostics.struct_size = sizeof(diagnostics);
    CHECK(dluma_native_gameplay_index_diagnostics(&diagnostics));
    CHECK(diagnostics.rejected_capacity >= 1u &&
          diagnostics.published_entries == 0u);
    dluma_native_gameplay_stop();
    free(objects);
    return 1;
}

static int case_shutdown_restores_observer_slot(void)
{
    FakeWorld world;
    void* slot = (void*)fake_interaction_original;
    void* original = slot;
    fake_init_world(&world);
    CHECK(fake_begin(&world,
        DLUMA_NATIVE_READY_WORLD_FIND | DLUMA_NATIVE_READY_INTERACT, 38));
    CHECK(dluma_native_gameplay_fake_install_interaction_slot(
        &slot, fake_interaction_original));
    CHECK(slot != 0 && slot != original);
    dluma_native_gameplay_quiesce();
    CHECK(slot == original);
    dluma_native_gameplay_stop();
    return 1;
}

static int case_quiesce_never_waits_for_native_locks(void)
{
    FakeWorld world;
    void* slot = (void*)fake_interaction_original;
    void* original = slot;
    void* observer;
    fake_init_world(&world);
    CHECK(fake_begin(&world,
        DLUMA_NATIVE_READY_WORLD_FIND | DLUMA_NATIVE_READY_INTERACT, 59));
    CHECK(dluma_native_gameplay_fake_install_interaction_slot(
        &slot, fake_interaction_original));
    observer = slot;
    CHECK(observer != 0 && observer != original);

    /* The menu-quit hook must return to stock ExitGame even if interrupted
     * native work owns either adapter lock.  Observation closes immediately;
     * hook restoration is retried after the contention clears. */
    dluma_native_gameplay_fake_set_quiesce_lock_contention(1, 1);
    dluma_native_gameplay_quiesce();
    CHECK(dluma_native_gameplay_readiness() == 0u);
    CHECK(slot == observer);
    dluma_native_gameplay_fake_set_quiesce_lock_contention(0, 0);
    dluma_native_gameplay_quiesce();
    CHECK(slot == original);
    dluma_native_gameplay_stop();
    return 1;
}

static int case_interaction_lost_hook_withdraws_readiness(void)
{
    FakeWorld world;
    void* slot = (void*)fake_interaction_original;
    void* original = slot;
    void* observer;
    fake_init_world(&world);
    fake_link_linear(&world);
    CHECK(fake_begin(&world,
        DLUMA_NATIVE_READY_WORLD_FIND | DLUMA_NATIVE_READY_INTERACT, 58));
    CHECK(dluma_native_gameplay_fake_install_interaction_slot(
        &slot, fake_interaction_original));
    observer = slot;
    CHECK(observer != 0 && observer != original);
    CHECK(fake_scan_to_terminal());
    CHECK((dluma_native_gameplay_readiness() &
           (DLUMA_NATIVE_READY_WORLD_FIND | DLUMA_NATIVE_READY_INTERACT)) ==
          (DLUMA_NATIVE_READY_WORLD_FIND | DLUMA_NATIVE_READY_INTERACT));
    CHECK(dluma_native_gameplay_capability_state(DLUMA_NATIVE_READY_INTERACT) ==
          DLUMA_NATIVE_CAP_READY);

    slot = (void*)fake_interaction_failed_original; /* observer ownership lost */
    dluma_native_gameplay_game_thread_tick();
    CHECK(dluma_native_gameplay_readiness() & DLUMA_NATIVE_READY_WORLD_FIND);
    CHECK(!(dluma_native_gameplay_readiness() & DLUMA_NATIVE_READY_INTERACT));
    CHECK(dluma_native_gameplay_capability_state(DLUMA_NATIVE_READY_INTERACT) ==
          DLUMA_NATIVE_CAP_DEGRADED);

    slot = observer;
    dluma_native_gameplay_game_thread_tick();
    CHECK(dluma_native_gameplay_readiness() & DLUMA_NATIVE_READY_INTERACT);
    CHECK(dluma_native_gameplay_capability_state(DLUMA_NATIVE_READY_INTERACT) ==
          DLUMA_NATIVE_CAP_READY);
    dluma_native_gameplay_quiesce();
    CHECK(slot == original);
    dluma_native_gameplay_stop();
    return 1;
}

static int case_restart_refuses_to_erase_retained_hook_state(void)
{
    FakeWorld original_world;
    FakeWorld replacement_world;
    DlumaNativeGameplayBackend replacement_backend;
    void* slot = (void*)fake_interaction_original;
    void* original = slot;
    void* observer;
    fake_init_world(&original_world);
    fake_init_world(&replacement_world);
    CHECK(fake_begin(&original_world,
        DLUMA_NATIVE_READY_WORLD_FIND | DLUMA_NATIVE_READY_INTERACT, 50));
    CHECK(dluma_native_gameplay_fake_install_interaction_slot(
        &slot, fake_interaction_original));
    observer = slot;
    CHECK(observer != 0 && observer != original);
    slot = (void*)fake_interaction_failed_original; /* simulate foreign owner */
    replacement_backend = fake_backend(&replacement_world, 0);
    CHECK(!dluma_native_gameplay_start_with_backend(&replacement_backend));
    CHECK(dluma_native_gameplay_supported_capabilities() &
          DLUMA_LUA54_CAP_WORLD_FIND);
    CHECK(dluma_native_gameplay_readiness() == 0u);

    slot = observer;
    dluma_native_gameplay_quiesce();
    CHECK(slot == original);
    dluma_native_gameplay_stop();
    return 1;
}

static void fake_write_get_rtti_stub(
    unsigned char* image,
    uint32_t function_offset,
    uint32_t rtti_offset,
    unsigned char rip_opcode)
{
    int32_t displacement = (int32_t)rtti_offset -
        (int32_t)(function_offset + 7u);
    image[function_offset + 0u] = 0x48;
    image[function_offset + 1u] = rip_opcode;
    image[function_offset + 2u] = 0x05;
    memcpy(image + function_offset + 3u,
           &displacement, sizeof(displacement));
    image[function_offset + 7u] = 0xC3;
}

static int case_action_chain_runs_exact_named_chain(void)
{
    FakeWorld world;
    DlumaLua54Error error;
    fake_init_world(&world);
    world.object_count = 6;
    fake_init_object(&world, 5, 15, "DLUMA_TestActionChain", "ActionChain");
    fake_link_linear(&world);
    CHECK(fake_start_ready(&world,
        DLUMA_NATIVE_READY_WORLD_FIND | DLUMA_NATIVE_READY_ACTION_CHAIN, 66));
    CHECK(dluma_native_gameplay_supported_capabilities() &
          DLUMA_LUA54_CAP_ACTION_CHAIN);
    CHECK(dluma_native_gameplay_capabilities() & DLUMA_LUA54_CAP_ACTION_CHAIN);
    fake_error(&error);
    CHECK(dluma_native_gameplay_action_run_chain(
        77, 66, "DLUMA_TestActionChain", &error));
    CHECK(world.action_chain_run_calls == 1u);
    CHECK(world.last_action_chain == &world.objects[5]);

    fake_error(&error);
    CHECK(!dluma_native_gameplay_action_run_chain(
        77, 66, "DLUMA_LocalA", &error));
    CHECK(fake_streq(error.code, "wrong_action_chain_class"));
    CHECK(world.action_chain_run_calls == 1u);
    dluma_native_gameplay_stop();
    return 1;
}

static int case_action_chain_backend_failure_is_structured(void)
{
    FakeWorld world;
    DlumaLua54Error error;
    fake_init_world(&world);
    world.object_count = 6;
    fake_init_object(&world, 5, 15, "DLUMA_FailingActionChain", "ActionChain");
    fake_link_linear(&world);
    CHECK(fake_start_ready(&world,
        DLUMA_NATIVE_READY_WORLD_FIND | DLUMA_NATIVE_READY_ACTION_CHAIN, 67));
    world.action_chain_fail = 1;
    fake_error(&error);
    CHECK(!dluma_native_gameplay_action_run_chain(
        78, 67, "DLUMA_FailingActionChain", &error));
    CHECK(fake_streq(error.code, "action_chain_run_failed"));
    CHECK(fake_streq(error.capability, "action.chain"));
    CHECK(fake_streq(error.permission, "world.write"));
    CHECK(world.action_chain_run_calls == 1u);
    dluma_native_gameplay_stop();
    return 1;
}

static int case_quest_mutation_is_typed_and_fail_closed(void)
{
    FakeWorld world;
    DlumaLua54QuestRequest request;
    DlumaLua54QuestResult result;
    DlumaLua54Error error;
    fake_init_world(&world);
    fake_link_linear(&world);
    CHECK(fake_start_ready(&world, DLUMA_NATIVE_READY_WORLD_FIND, 68));
    memset(&request, 0, sizeof(request));
    request.struct_size = sizeof(request);
    request.map_generation = 68;
    request.owner_id = 79;
    request.operation = DLUMA_LUA54_QUEST_JUMP_TO_PHASE;
    dluma_str_copy(request.native_quest_id,
                 DLUMA_ARRAY_COUNT(request.native_quest_id), "DLUMA_Rescue");
    dluma_str_copy(request.native_phase_id,
                 DLUMA_ARRAY_COUNT(request.native_phase_id), "DLUMA_ReachLab");
    memset(&result, 0, sizeof(result));
    fake_error(&error);
    CHECK(!dluma_native_gameplay_quest_mutate(
        79, &request, &result, &error));
    CHECK(fake_streq(error.code, "unsupported_capability"));
    CHECK(fake_streq(error.capability, "quests.native"));
    CHECK(fake_streq(error.permission, "quest.write"));
    CHECK(result.struct_size == sizeof(result));
    CHECK(result.changed == 0u);

    request.map_generation = 69;
    fake_error(&error);
    CHECK(!dluma_native_gameplay_quest_mutate(
        79, &request, &result, &error));
    CHECK(fake_streq(error.code, "stale_map_generation"));
    CHECK(error.retryable == 1u);
    request.map_generation = 68;

    dluma_str_copy(request.native_phase_id,
                 DLUMA_ARRAY_COUNT(request.native_phase_id), "bad phase");
    fake_error(&error);
    CHECK(!dluma_native_gameplay_quest_mutate(
        79, &request, &result, &error));
    CHECK(fake_streq(error.code, "invalid_quest_request"));
    dluma_native_gameplay_stop();
    return 1;
}

static int case_subtitle_send_update_fade_and_expiry(void)
{
    FakeWorld world;
    DlumaLua54SubtitleRequest send;
    DlumaLua54SubtitleUpdateRequest update;
    DlumaLua54Error error;
    fake_init_world(&world);
    fake_link_linear(&world);
    CHECK(fake_start_ready(&world,
        DLUMA_NATIVE_READY_WORLD_FIND | DLUMA_NATIVE_READY_SUBTITLES, 64));
    memset(&send, 0, sizeof(send));
    send.struct_size = sizeof(send);
    send.force = 1;
    send.duration_seconds = 2.0;
    dluma_str_copy(send.text, DLUMA_ARRAY_COUNT(send.text), "&DLUMA_TEST_LINE&");
    fake_error(&error);
    CHECK(dluma_native_gameplay_subtitle_show(&send, &error));
    CHECK(world.subtitle_show_calls == 1u && world.subtitle_hide_calls == 0u);
    CHECK(world.last_subtitle_force == 1);
    CHECK(fake_streq(world.last_subtitle_text, "&DLUMA_TEST_LINE&"));

    memset(&update, 0, sizeof(update));
    update.struct_size = sizeof(update);
    dluma_str_copy(update.text, DLUMA_ARRAY_COUNT(update.text), "updated");
    fake_error(&error);
    CHECK(dluma_native_gameplay_subtitle_update(&update, &error));
    CHECK(world.subtitle_show_calls == 2u && world.subtitle_hide_calls == 0u);

    update.fade = 1;
    dluma_str_copy(update.text, DLUMA_ARRAY_COUNT(update.text), "fresh transition");
    fake_error(&error);
    CHECK(dluma_native_gameplay_subtitle_update(&update, &error));
    CHECK(world.subtitle_show_calls == 3u && world.subtitle_hide_calls == 1u);
    CHECK(world.last_subtitle_force == 1);

    world.clock_ms = 2999;
    dluma_native_gameplay_game_thread_tick();
    CHECK(world.subtitle_hide_calls == 1u);
    world.clock_ms = 3000;
    dluma_native_gameplay_game_thread_tick();
    CHECK(world.subtitle_hide_calls == 2u);
    dluma_native_gameplay_game_thread_tick();
    CHECK(world.subtitle_hide_calls == 2u);
    dluma_native_gameplay_stop();
    return 1;
}

static int case_radio_dialog_resolves_exact_speaker_and_group(void)
{
    FakeWorld world;
    DlumaLua54RadioDialogRequest request;
    DlumaLua54Error error;
    fake_init_world(&world);
    fake_link_linear(&world);
    CHECK(fake_start_ready(&world,
        DLUMA_NATIVE_READY_WORLD_FIND | DLUMA_NATIVE_READY_RADIO_DIALOG, 65));
    memset(&request, 0, sizeof(request));
    request.struct_size = sizeof(request);
    dluma_str_copy(request.speaker, DLUMA_ARRAY_COUNT(request.speaker), "voice2d");
    dluma_str_copy(request.group, DLUMA_ARRAY_COUNT(request.group), "dluma_intro_radio");
    fake_error(&error);
    CHECK(dluma_native_gameplay_radio_dialog(&request, &error));
    CHECK(world.radio_dialog_calls == 1u);
    CHECK(fake_streq(world.last_radio_group, "dluma_intro_radio"));
    dluma_native_gameplay_stop();
    return 1;
}

static int case_derived_get_rtti_lea_stub_is_accepted(void)
{
    unsigned char image[512];
    memset(image, 0, sizeof(image));
    /* A derived class has its own target and displacement.  It is valid even
     * though its eight bytes differ from the IGSObject base getter. */
    fake_write_get_rtti_stub(image, 32u, 256u, 0x8D);
    CHECK(dluma_native_gameplay_fake_validate_get_rtti_stub(
        image, sizeof(image), 32u, 256u));
    return 1;
}

static int case_get_engine_rtti_mov_stub_is_rejected(void)
{
    unsigned char image[512];
    memset(image, 0, sizeof(image));
    /* +0x18 GetEngineRTTI uses MOV (8B), never gameplay LEA (8D). */
    fake_write_get_rtti_stub(image, 32u, 256u, 0x8B);
    CHECK(!dluma_native_gameplay_fake_validate_get_rtti_stub(
        image, sizeof(image), 32u, 256u));
    return 1;
}

static int case_get_rtti_target_outside_readable_image_is_rejected(void)
{
    unsigned char image[512];
    memset(image, 0, sizeof(image));
    /* The target begins in-image but cannot contain the bounded CRTTI view. */
    fake_write_get_rtti_stub(image, 32u, 480u, 0x8D);
    CHECK(!dluma_native_gameplay_fake_validate_get_rtti_stub(
        image, sizeof(image), 32u, 480u));
    return 1;
}

int main(void)
{
    static const FakeCase cases[] = {
        {"readiness_fail_closed", case_readiness_fail_closed},
        {"hud_readiness_survives_index_resets", case_hud_readiness_survives_index_resets},
        {"index_incomplete_not_ready", case_index_incomplete_not_ready},
        {"generation_change_during_scan_restarts_builder", case_generation_change_during_scan_restarts_builder},
        {"bad_seed_then_sparse_7935", case_bad_seed_then_sparse_7935},
        {"rejected_head_walks_to_valid_tail", case_rejected_head_walks_to_valid_tail},
        {"disconnected_components", case_disconnected_components},
        {"same_pointer_returned_by_two_ids", case_same_pointer_returned_by_two_ids},
        {"rejected_cycle_does_not_end_scan", case_rejected_cycle_does_not_end_scan},
        {"wrong_level_rejected_but_scan_continues", case_wrong_level_rejected_but_scan_continues},
        {"distinct_same_map_owner_without_id_roundtrip_is_accepted", case_distinct_same_map_owner_without_id_roundtrip_is_accepted},
        {"degraded_streaming_index_recovers_after_objects_arrive", case_degraded_streaming_index_recovers_after_objects_arrive},
        {"direct_seed_reconsiders_early_ownerless_link", case_direct_seed_reconsiders_early_ownerless_link},
        {"not_found_requests_bounded_streaming_refresh", case_not_found_requests_bounded_streaming_refresh},
        {"complete_object_rejection_continues", case_complete_object_rejection_continues},
        {"duplicate_exact_names_rejected", case_duplicate_exact_names_rejected},
        {"handle_copies_metadata", case_handle_copies_metadata},
        {"world_position_revalidates_handle_and_copies_vector", case_world_position_revalidates_handle_and_copies_vector},
        {"wrong_expected_class", case_wrong_expected_class},
        {"foreign_handle_rejected", case_foreign_handle_rejected},
        {"stale_generation_handle", case_stale_generation_handle},
        {"handle_revalidation_failure", case_handle_revalidation_failure},
        {"handle_capacity_fails_closed", case_handle_capacity_fails_closed},
        {"world_find_epoch_change_rejects_handle_commit", case_world_find_epoch_change_rejects_handle_commit},
        {"derived_sensor_and_base_interface", case_derived_sensor_and_base_interface},
        {"wrong_sensor_class", case_wrong_sensor_class},
        {"sensor_base_unavailable", case_sensor_base_unavailable},
        {"sensor_checkinside_unvalidated", case_sensor_checkinside_unvalidated},
        {"sensor_baseline_enter_leave_once", case_sensor_baseline_enter_leave_once},
        {"retained_level_pumps_events_during_active_level_gap", case_retained_level_pumps_events_during_active_level_gap},
        {"pending_sensor_subscription_survives_scan", case_pending_sensor_subscription_survives_scan},
        {"removed_sensor_subscription_is_silent", case_removed_sensor_subscription_is_silent},
        {"sensor_generation_change_clears_baseline", case_sensor_generation_change_clears_baseline},
        {"sensor_remove_readd_during_sample_rejects_commit", case_sensor_remove_readd_during_sample_rejects_commit},
        {"sensor_generation_reset_during_sample_rejects_commit", case_sensor_generation_reset_during_sample_rejects_commit},
        {"sensor_failed_poll_still_attempted_once_per_name", case_sensor_failed_poll_still_attempted_once_per_name},
        {"nonzero_local_enable_interface", case_nonzero_local_enable_interface},
        {"atomic_preflight_changes_nothing", case_atomic_preflight_changes_nothing},
        {"duplicate_atomic_request_rejected", case_duplicate_atomic_request_rejected},
        {"partial_commit_readback_failure", case_partial_commit_readback_failure},
        {"unchanged_then_precommit_failure_is_not_partial", case_unchanged_then_precommit_failure_is_not_partial},
        {"local_enable_unchanged_result", case_local_enable_unchanged_result},
        {"interaction_original_once_result_preserved", case_interaction_original_once_result_preserved},
        {"interaction_nonexecute_action_observed_only", case_interaction_nonexecute_action_observed_only},
        {"interaction_failed_original_does_not_queue", case_interaction_failed_original_does_not_queue},
        {"interaction_without_subscription_does_not_queue", case_interaction_without_subscription_does_not_queue},
        {"interaction_remove_observer_interleavings", case_interaction_remove_observer_interleavings},
        {"interaction_remove_readd_aba_discards_old_event", case_interaction_remove_readd_aba_discards_old_event},
        {"interaction_queue_dispatch", case_interaction_queue_dispatch},
        {"interaction_old_generation_discarded", case_interaction_old_generation_discarded},
        {"generation_invalidates_handle_subscription_and_event", case_generation_invalidates_handle_subscription_and_event},
        {"interaction_removed_subscription_no_queue", case_interaction_removed_subscription_no_queue},
        {"interaction_queue_bounded", case_interaction_queue_bounded},
        {"index_capacity_degrades", case_index_capacity_degrades},
        {"shutdown_restores_observer_slot", case_shutdown_restores_observer_slot},
        {"quiesce_never_waits_for_native_locks", case_quiesce_never_waits_for_native_locks},
        {"interaction_lost_hook_withdraws_readiness", case_interaction_lost_hook_withdraws_readiness},
        {"restart_refuses_to_erase_retained_hook_state", case_restart_refuses_to_erase_retained_hook_state},
        {"action_chain_runs_exact_named_chain", case_action_chain_runs_exact_named_chain},
        {"action_chain_backend_failure_is_structured", case_action_chain_backend_failure_is_structured},
        {"quest_mutation_is_typed_and_fail_closed", case_quest_mutation_is_typed_and_fail_closed},
        {"subtitle_send_update_fade_and_expiry", case_subtitle_send_update_fade_and_expiry},
        {"radio_dialog_resolves_exact_speaker_and_group", case_radio_dialog_resolves_exact_speaker_and_group},
        {"derived_get_rtti_lea_stub_is_accepted", case_derived_get_rtti_lea_stub_is_accepted},
        {"get_engine_rtti_mov_stub_is_rejected", case_get_engine_rtti_mov_stub_is_rejected},
        {"get_rtti_target_outside_readable_image_is_rejected", case_get_rtti_target_outside_readable_image_is_rejected}
    };
    uint32_t i;
    for (i = 0; i < DLUMA_ARRAY_COUNT(cases); ++i) {
        if (!cases[i].function()) {
            fprintf(stderr, "native gameplay fake-backend case failed: %s\n",
                    cases[i].name);
            return 1;
        }
        printf("[PASS] %s\n", cases[i].name);
    }
    printf("native gameplay fake-backend cases passed: %u\n",
           (unsigned int)DLUMA_ARRAY_COUNT(cases));
    return 0;
}
