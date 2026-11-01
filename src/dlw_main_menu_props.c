#include "dlw_main_menu_props.h"
#include "dlw_path_security.h"

#define PROP_GAME_HASH "cbc7dd7923778936341cdfb755474a0c714079d910849df16ffc5ec88a103b95"
#define PROP_ENGINE_HASH "a18ca1103614947ae7ff956c6852f45123b440563a967d9e1e496c42fa574a3c"
#define PROP_ACTOR_VTABLE 0x0167B0A8UL
#define PROP_CHILD_OFFSET 0x188U
#define PROP_CHILD_HOLDER_OFFSET 0x148U
#define PROP_PARENT_HOLDER_OFFSET 0x158U
#define PROP_LOOP_OFFSET 0xE0U
#define PROP_ANIM_HANDLE_OFFSET 0xF0U
#define PROP_SET_RVA 0x0075C590UL
#define PROP_RESET_RVA 0x0075C660UL
#define PROP_ATTACH_RVA 0x0075CE80UL
#define PROP_GET_MESH_IAT 0x013D0D60UL
#define PROP_GET_SKIN_IAT 0x013CCC68UL
#define PROP_GET_BIND_IAT 0x013CCC20UL
#define PROP_FREE_IAT 0x013CC198UL
#define PROP_GET_BIND_RVA 0x0015B390UL
#define PROP_COLLECT_SKINS_RVA 0x0015A180UL
#define PROP_MESH_SKIN_EXISTS_RVA 0x0015A020UL
#define PROP_GET_ELEMENT_RVA 0x0014A810UL
#define PROP_MESH_MANAGER_GLOBAL 0x00ACD110UL
#define PROP_MESH_MANAGER_VTABLE 0x00942FB8UL
#define PROP_MESH_EXISTS_RVA 0x007B5E20UL
#define PROP_NAME_CAPACITY 512U
#define PROP_RETRY_LIMIT 20U
#define PROP_RETRY_MS 500ULL

typedef struct PropString {
    const char *data;
    unsigned int size;
    unsigned int capacity;
} PropString;

typedef struct PropVector {
    PropString *data;
    unsigned int size;
    unsigned int capacity;
    unsigned int tag;
} PropVector;

typedef struct PropAnimId {
    int value;
    unsigned int padding;
    void *sequence;
} PropAnimId;

typedef PropString *(__fastcall *PropGetNameFn)(void *, PropString *);
typedef void *(__fastcall *PropGetBindFn)(void *);
typedef void (__cdecl *PropFreeFn)(void *);
typedef unsigned char (__fastcall *PropSetFn)(void *, const PropString *, const PropString *);
typedef void (__fastcall *PropResetFn)(void *, const PropString *, const PropString *);
typedef void (__fastcall *PropAttachFn)(void *, void *);
typedef void (__fastcall *PropCollectFn)(const PropString *, unsigned char, PropVector *);
typedef unsigned char (__fastcall *PropExistsFn)(const PropString *, const PropString *);
typedef unsigned char (__fastcall *PropMeshExistsFn)(void *, const PropString *);
typedef int (__fastcall *PropGetElementFn)(void *, const char *);
typedef PropAnimId *(__fastcall *PropFindAnimFn)(void *, PropAnimId *, const PropString *);

typedef struct PropState {
    BYTE *game;
    BYTE *engine;
    PropGetNameFn get_mesh;
    PropGetNameFn get_skin;
    PropGetBindFn get_bind;
    PropFreeFn game_free;
    PropSetFn set;
    PropResetFn reset;
    PropAttachFn attach;
    PropCollectFn collect;
    PropExistsFn exists;
    PropGetElementFn get_element;
    void *background;
    void *actor;
    void *child;
    const DlumaMainMenuPropSet *set_of_props;
    const DlumaMainMenuPropProfile *applied;
    unsigned int candidate;
    unsigned int attempts;
    unsigned int candidates_tried;
    ULONGLONG next_attempt;
    char saved_mesh[PROP_NAME_CAPACITY];
    char saved_skin[PROP_NAME_CAPACITY];
    char applied_mesh[PROP_NAME_CAPACITY];
    char applied_skin[PROP_NAME_CAPACITY];
    unsigned char active;
    unsigned char recovering;
    unsigned char advance_after_recovery;
    unsigned char abandoned;
    unsigned char ready;
} PropState;

static PropState g_props;

static int prop_hash(HMODULE module, const char *expected)
{
    WCHAR path[DLUMA_MAX_WPATH];
    char digest[DLUMA_SHA256_HEX_CAPACITY];
    return module &&
        dluma_get_module_path_checked(module, path, DLUMA_ARRAY_COUNT(path)) &&
        dluma_sha256_file_hex(path, 1024ULL * 1024ULL * 1024ULL, digest) &&
        dluma_stricmp(digest, expected) == 0;
}

static int prop_callable(void *address)
{
    MEMORY_BASIC_INFORMATION info;
    DWORD protection;
    if (!address || VirtualQuery(address, &info, sizeof(info)) != sizeof(info) ||
        info.State != MEM_COMMIT ||
        (info.Protect & (PAGE_NOACCESS | PAGE_GUARD)))
        return 0;
    protection = info.Protect & 0xffUL;
    return protection == PAGE_EXECUTE || protection == PAGE_EXECUTE_READ ||
        protection == PAGE_EXECUTE_READWRITE ||
        protection == PAGE_EXECUTE_WRITECOPY;
}

static PropString prop_string(const char *text)
{
    PropString value;
    value.data = text;
    value.size = (unsigned int)strlen(text);
    value.capacity = value.size;
    return value;
}

static int prop_field_is(void *actor, unsigned int offset, const char *expected)
{
    const PropString *value = (const PropString *)((BYTE *)actor + offset);
    size_t expected_size = strlen(expected);
    return dluma_is_readable(value, sizeof(*value)) && value->data &&
        value->size == expected_size && value->capacity >= value->size &&
        dluma_is_readable(value->data, expected_size + 1U) &&
        value->data[expected_size] == 0 &&
        dluma_stricmp(value->data, expected) == 0;
}

static int prop_actor_child(PropState *state, void *actor, void **child_out)
{
    void *child;
    if (child_out)
        *child_out = 0;
    if (!state->ready || !actor || !dluma_is_readable(actor, 0x190U) ||
        *(void **)actor != state->game + PROP_ACTOR_VTABLE ||
        !prop_field_is(actor, PROP_PARENT_HOLDER_OFFSET, "r_handholder") ||
        !prop_field_is(actor, PROP_CHILD_HOLDER_OFFSET, "holder"))
        return 0;
    child = *(void **)((BYTE *)actor + PROP_CHILD_OFFSET);
    if (!child || !dluma_is_readable(child, 0x190U) ||
        *(void **)child != state->game + PROP_ACTOR_VTABLE ||
        *(int *)((BYTE *)child + PROP_ANIM_HANDLE_OFFSET) != -1 ||
        state->get_element(actor, "r_handholder") < 0)
        return 0;
    if (child_out)
        *child_out = child;
    return 1;
}

static int prop_copy_name(PropState *state, PropGetNameFn getter, void *object,
                          char output[PROP_NAME_CAPACITY], int allow_empty)
{
    PropString value;
    PropString *result;
    int valid = 0;
    memset(&value, 0, sizeof(value));
    output[0] = 0;
    result = getter(object, &value);
    if (result == &value && !value.size && allow_empty)
        valid = 1;
    else if (result == &value && value.data && value.size &&
        value.size < PROP_NAME_CAPACITY && value.capacity >= value.size &&
        dluma_is_readable(value.data, (SIZE_T)value.size + 1U) &&
        value.data[value.size] == 0 &&
        strlen(value.data) == value.size) {
        memcpy(output, value.data, value.size + 1U);
        valid = 1;
    }
    if (value.data)
        state->game_free((void *)value.data);
    return valid;
}

static int prop_read_names(PropState *state, void *child, char *mesh, char *skin)
{
    return prop_copy_name(state, state->get_mesh, child, mesh, 0) &&
        prop_copy_name(state, state->get_skin, child, skin, 1);
}

static int prop_mesh_exists(PropState *state, const PropString *mesh)
{
    void *manager;
    void **vtable;
    PropMeshExistsFn exists;
    if (!dluma_is_readable(state->engine + PROP_MESH_MANAGER_GLOBAL,
                           sizeof(manager)))
        return 0;
    manager = *(void **)(state->engine + PROP_MESH_MANAGER_GLOBAL);
    if (!manager || !dluma_is_readable(manager, sizeof(void *)))
        return 0;
    vtable = *(void ***)manager;
    if (vtable != (void **)(state->engine + PROP_MESH_MANAGER_VTABLE) ||
        !dluma_is_readable((BYTE *)vtable + 0x30U, sizeof(void *)))
        return 0;
    exists = *(PropMeshExistsFn *)((BYTE *)vtable + 0x30U);
    return (void *)exists == state->engine + PROP_MESH_EXISTS_RVA &&
        prop_callable((void *)exists) && exists(manager, mesh);
}

static int prop_pair_exists(PropState *state, const PropString *mesh,
                            const PropString *skin)
{
    return state->exists(mesh, skin) ||
        (!skin->size && prop_mesh_exists(state, mesh));
}

static int prop_skin_for_mesh(PropState *state, const char *mesh,
                              const char *requested, char out[PROP_NAME_CAPACITY])
{
    PropString name = prop_string(mesh), skin;
    PropVector skins;
    unsigned int i, pick = 0;
    int valid = 0;
    out[0] = 0;
    if (!prop_mesh_exists(state, &name))
        return 0;
    if (requested && requested[0]) {
        if (strlen(requested) >= PROP_NAME_CAPACITY)
            return 0;
        dluma_str_copy(out, PROP_NAME_CAPACITY, requested);
        skin = prop_string(out);
        return state->exists(&name, &skin) != 0;
    }
    memset(&skins, 0, sizeof(skins));
    skins.tag = 77U;
    state->collect(&name, 0, &skins);
    if (skins.size && skins.size <= skins.capacity && skins.size <= 1024U &&
        skins.data && dluma_is_readable(skins.data,
                                       (SIZE_T)skins.size * sizeof(PropString))) {
        for (i = 0; i < skins.size; ++i) {
            const PropString *entry = &skins.data[i];
            if (entry->data && entry->size && entry->size < PROP_NAME_CAPACITY &&
                entry->capacity >= entry->size &&
                dluma_is_readable(entry->data, (SIZE_T)entry->size + 1U) &&
                entry->data[entry->size] == 0 &&
                dluma_stricmp(entry->data, "Default") == 0) {
                pick = i;
                break;
            }
        }
        if (skins.data[pick].data && skins.data[pick].size &&
            skins.data[pick].size < PROP_NAME_CAPACITY &&
            skins.data[pick].capacity >= skins.data[pick].size &&
            dluma_is_readable(skins.data[pick].data,
                              (SIZE_T)skins.data[pick].size + 1U) &&
            skins.data[pick].data[skins.data[pick].size] == 0) {
            memcpy(out, skins.data[pick].data, skins.data[pick].size + 1U);
            skin = prop_string(out);
            valid = state->exists(&name, &skin) != 0;
        }
        for (i = 0; i < skins.size; ++i)
            if (skins.data[i].data)
                state->game_free((void *)skins.data[i].data);
        state->game_free(skins.data);
    } else if (!skins.size) {
        if (skins.data)
            state->game_free(skins.data);
        skin = prop_string("");
        valid = prop_pair_exists(state, &name, &skin);
    } else if (skins.data) {
        /* A malformed vector cannot be safely traversed, but its allocation
         * still belongs to the game's allocator. */
        state->game_free(skins.data);
    }
    return valid;
}

static int prop_idle_ready(PropState *state, void *actor, const char *idle)
{
    PropAnimId id;
    PropString query;
    PropFindAnimFn find;
    void *bind;
    void *model;
    void **model_vtable;
    void **vtable;
    if (!idle || !idle[0] || !prop_field_is(actor, PROP_LOOP_OFFSET, idle) ||
        *(int *)((BYTE *)actor + PROP_ANIM_HANDLE_OFFSET) == -1)
        return 0;
    model = *(void **)((BYTE *)actor + sizeof(void *));
    if (!model || !dluma_is_readable(model, sizeof(void *)))
        return 0;
    model_vtable = *(void ***)model;
    if (!model_vtable ||
        !dluma_is_readable((BYTE *)model_vtable + 0x738U, sizeof(void *)) ||
        !prop_callable(*(void **)((BYTE *)model_vtable + 0x738U)))
        return 0;
    bind = state->get_bind(actor);
    if (!bind || !dluma_is_readable(bind, sizeof(void *)))
        return 0;
    vtable = *(void ***)bind;
    if (!vtable || !dluma_is_readable((BYTE *)vtable + 0x148U, sizeof(void *)))
        return 0;
    find = *(PropFindAnimFn *)((BYTE *)vtable + 0x148U);
    if (!prop_callable((void *)find))
        return 0;
    memset(&id, 0, sizeof(id));
    id.value = -1;
    query = prop_string(idle);
    return find(bind, &id, &query) == &id && id.value != -1;
}

static int prop_apply(PropState *state, void *actor, void *child,
                      const char *mesh, const char *skin)
{
    PropString mesh_value = prop_string(mesh), skin_value = prop_string(skin);
    char actual_mesh[PROP_NAME_CAPACITY], actual_skin[PROP_NAME_CAPACITY];
    int applied;
    if (!prop_pair_exists(state, &mesh_value, &skin_value))
        return 0;
    if (skin_value.size || state->exists(&mesh_value, &skin_value))
        applied = state->set(child, &mesh_value, &skin_value) != 0;
    else {
        state->reset(child, &mesh_value, &skin_value);
        applied = 1;
    }
    if (!applied ||
        !prop_read_names(state, child, actual_mesh, actual_skin) ||
        dluma_stricmp(mesh, actual_mesh) != 0 ||
        dluma_stricmp(skin, actual_skin) != 0 ||
        state->get_element(child, "holder") < 0 ||
        state->get_element(actor, "r_handholder") < 0)
        return 0;
    state->attach(actor, child);
    return 1;
}

static void prop_retire_attachment(PropState *state)
{
    state->active = 0;
    state->recovering = 0;
    state->advance_after_recovery = 0;
    state->applied = 0;
    state->child = 0;
    state->saved_mesh[0] = 0;
    state->saved_skin[0] = 0;
}

static int prop_restore(PropState *state, void *actor)
{
    void *child = 0;
    char mesh[PROP_NAME_CAPACITY], skin[PROP_NAME_CAPACITY];
    if (!state->active && !state->recovering)
        return 1;
    if (state->actor != actor)
        return 0;
    if (!prop_actor_child(state, actor, &child)) {
        prop_retire_attachment(state);
        return 1;
    }
    if (child != state->child) {
        prop_retire_attachment(state);
        return 1;
    }
    if (!prop_read_names(state, child, mesh, skin))
        return 0;
    if (!state->recovering &&
        (dluma_stricmp(mesh, state->applied_mesh) != 0 ||
         dluma_stricmp(skin, state->applied_skin) != 0)) {
        state->active = 0;
        state->abandoned = 1;
        return 1;
    }
    if (!prop_apply(state, actor, child, state->saved_mesh, state->saved_skin)) {
        state->recovering = 1;
        return 0;
    }
    state->active = 0;
    state->recovering = 0;
    state->applied = 0;
    state->saved_mesh[0] = 0;
    state->saved_skin[0] = 0;
    return 1;
}

static void prop_advance(PropState *state, const DlumaMainMenuPropSet *set)
{
    if (!set || !set->count || ++state->candidates_tried >= set->count)
        state->abandoned = 1;
    else
        state->candidate = (state->candidate + 1U) % set->count;
}

int dluma_main_menu_props_start(void)
{
    static const BYTE setter_prefix[] = {
        0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x6c,0x24,0x10
    };
    PropState *state = &g_props;
    HMODULE game = GetModuleHandleW(L"gamedll_x64_rwdi.dll");
    HMODULE engine = GetModuleHandleW(L"engine_x64_rwdi.dll");
    BYTE *g = (BYTE *)game, *e = (BYTE *)engine;
    if (state->ready)
        return 1;
    if (!prop_hash(game, PROP_GAME_HASH) || !prop_hash(engine, PROP_ENGINE_HASH) ||
        !dluma_is_readable(g + PROP_SET_RVA, sizeof(setter_prefix)) ||
        memcmp(g + PROP_SET_RVA, setter_prefix, sizeof(setter_prefix)) != 0)
        return 0;
    memset(state, 0, sizeof(*state));
    state->game = g;
    state->engine = e;
    state->get_mesh = *(PropGetNameFn *)(g + PROP_GET_MESH_IAT);
    state->get_skin = *(PropGetNameFn *)(g + PROP_GET_SKIN_IAT);
    state->get_bind = *(PropGetBindFn *)(g + PROP_GET_BIND_IAT);
    state->game_free = *(PropFreeFn *)(g + PROP_FREE_IAT);
    state->set = (PropSetFn)(g + PROP_SET_RVA);
    state->reset = (PropResetFn)(g + PROP_RESET_RVA);
    state->attach = (PropAttachFn)(g + PROP_ATTACH_RVA);
    state->collect = (PropCollectFn)GetProcAddress(
        engine, "?CollectMeshSkins@IModelObject@@SAXAEBV?$string_base@D@ttl@@_NAEAV?$vector@V?$string_base@D@ttl@@@3@@Z");
    state->exists = (PropExistsFn)GetProcAddress(
        engine, "?MeshAndSkinExists@IModelObject@@SA_NAEBV?$string_base@D@ttl@@0@Z");
    state->get_element = (PropGetElementFn)GetProcAddress(
        engine, "?GetElementID@IModelObject@@QEBAHPEBD@Z");
    if (!state->get_mesh || !state->get_skin || !state->game_free ||
        (void *)state->get_bind != e + PROP_GET_BIND_RVA ||
        (void *)state->collect != e + PROP_COLLECT_SKINS_RVA ||
        (void *)state->exists != e + PROP_MESH_SKIN_EXISTS_RVA ||
        (void *)state->get_element != e + PROP_GET_ELEMENT_RVA ||
        !prop_callable((void *)state->attach) ||
        !prop_callable((void *)state->reset) ||
        !prop_callable((void *)state->collect) ||
        !prop_callable((void *)state->exists) ||
        !prop_callable((void *)state->get_element))
        return 0;
    state->ready = 1;
    return 1;
}

void dluma_main_menu_props_tick(
    void *background, void *actor, const DlumaMainMenuPropSet *set,
    ULONGLONG seed, ULONGLONG now)
{
    PropState *state = &g_props;
    const DlumaMainMenuPropProfile *profile;
    void *child;
    char mesh[PROP_NAME_CAPACITY], skin[PROP_NAME_CAPACITY];
    char target_skin[PROP_NAME_CAPACITY];
    PropString saved_mesh, saved_skin;
    if (!state->ready || !background || !actor)
        return;
    if (state->background && state->background != background)
        return;
    if (state->actor && state->actor != actor) {
        prop_retire_attachment(state);
        state->actor = actor;
        state->attempts = 0;
        state->candidates_tried = 0;
        state->abandoned = 0;
        state->next_attempt = 0;
        state->candidate = set && set->count
            ? (unsigned int)(seed % set->count) : 0U;
    }
    if (!state->background) {
        state->background = background;
        state->actor = actor;
    }
    if (state->next_attempt && now < state->next_attempt)
        return;
    state->next_attempt = now + PROP_RETRY_MS;
    if (state->recovering) {
        if (state->attempts < PROP_RETRY_LIMIT) {
            ++state->attempts;
            if (prop_restore(state, actor)) {
                if (state->advance_after_recovery &&
                    state->set_of_props == set)
                    prop_advance(state, set);
                state->advance_after_recovery = 0;
            }
        }
        return;
    }
    if (state->set_of_props != set) {
        if (state->active && !prop_restore(state, actor)) {
            state->recovering = 1;
            state->advance_after_recovery = 0;
            state->attempts = 1U;
            return;
        }
        state->set_of_props = set;
        state->candidate = set && set->count ? (unsigned int)(seed % set->count) : 0U;
        state->attempts = 0;
        state->candidates_tried = 0;
        state->abandoned = 0;
    }
    if (state->active) {
        if (!prop_actor_child(state, actor, &child)) {
            prop_retire_attachment(state);
            state->attempts = 0;
            return;
        }
        if (child != state->child) {
            state->active = 0;
            state->applied = 0;
            state->child = 0;
            state->saved_mesh[0] = 0;
            state->saved_skin[0] = 0;
            state->attempts = 0;
        } else if (prop_read_names(state, child, mesh, skin) &&
                   (dluma_stricmp(mesh, state->applied_mesh) != 0 ||
                    dluma_stricmp(skin, state->applied_skin) != 0)) {
            state->active = 0;
            state->applied = 0;
            state->abandoned = 1;
        }
        return;
    }
    if (!set || !set->props || !set->count || set->count > 16U ||
        state->active || state->abandoned || state->attempts >= PROP_RETRY_LIMIT)
        return;
    ++state->attempts;
    if (!prop_actor_child(state, actor, &child)) {
        if (g_dluma.config.developer_mode && state->attempts == PROP_RETRY_LIMIT)
            dluma_log_pair("INFO", "main_menu_held_prop_skip", "hand_attachment_unavailable");
        return;
    }
    profile = &set->props[state->candidate];
    if (profile->grip != DLUMA_MAIN_MENU_PROP_GRIP_ONE_HANDED ||
        !profile->mesh || !profile->mesh[0] ||
        !prop_idle_ready(state, actor, profile->idle_loop) ||
        state->get_element(child, "holder") < 0 ||
        !prop_read_names(state, child, mesh, skin)) {
        if (g_dluma.config.developer_mode && state->attempts == PROP_RETRY_LIMIT)
            dluma_log_pair("INFO", "main_menu_held_prop_skip", "idle_or_attachment_unavailable");
        return;
    }
    saved_mesh = prop_string(mesh);
    saved_skin = prop_string(skin);
    if (!prop_pair_exists(state, &saved_mesh, &saved_skin)) {
        if (g_dluma.config.developer_mode && state->attempts == PROP_RETRY_LIMIT)
            dluma_log_pair("INFO", "main_menu_held_prop_skip", "original_mesh_unavailable");
        return;
    }
    if (!prop_skin_for_mesh(state, profile->mesh, profile->skin, target_skin)) {
        prop_advance(state, set);
        return;
    }
    state->background = background;
    state->actor = actor;
    state->child = child;
    dluma_str_copy(state->saved_mesh, PROP_NAME_CAPACITY, mesh);
    dluma_str_copy(state->saved_skin, PROP_NAME_CAPACITY, skin);
    dluma_str_copy(state->applied_mesh, PROP_NAME_CAPACITY, profile->mesh);
    dluma_str_copy(state->applied_skin, PROP_NAME_CAPACITY, target_skin);
    if (prop_apply(state, actor, child, profile->mesh, target_skin)) {
        if (g_dluma.config.developer_mode)
            dluma_log_pair("INFO", "main_menu_held_prop", profile->mesh);
        state->active = 1;
        state->applied = profile;
        state->attempts = 0;
        return;
    }
    state->recovering = 1;
    state->advance_after_recovery = 1;
    state->attempts = 0;
    if (prop_restore(state, actor)) {
        state->advance_after_recovery = 0;
        prop_advance(state, set);
    }
}

int dluma_main_menu_props_before_body_change(void *actor, ULONGLONG now)
{
    PropState *state = &g_props;
    void *child = 0;
    if (!actor || state->actor != actor || (!state->active && !state->recovering))
        return 1;
    if (!prop_actor_child(state, actor, &child) || child != state->child) {
        prop_retire_attachment(state);
        return 1;
    }
    if (state->recovering && state->next_attempt && now < state->next_attempt)
        return 0;
    if (state->attempts >= PROP_RETRY_LIMIT)
        return 0;
    state->next_attempt = now + PROP_RETRY_MS;
    if (!prop_restore(state, actor)) {
        ++state->attempts;
        return 0;
    }
    state->attempts = 0;
    return 1;
}

void dluma_main_menu_props_forget(void *background)
{
    PropState *state = &g_props;
    if (background && state->background == background) {
        state->background = 0;
        state->actor = 0;
        state->child = 0;
        state->set_of_props = 0;
        state->applied = 0;
        state->active = 0;
        state->recovering = 0;
        state->advance_after_recovery = 0;
        state->abandoned = 0;
        state->attempts = 0;
        state->next_attempt = 0;
    }
}

int dluma_main_menu_props_stop(void)
{
    if (g_props.active || g_props.recovering)
        return 0;
    memset(&g_props, 0, sizeof(g_props));
    return 1;
}
