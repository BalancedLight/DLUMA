#include "dlw_main_menu_animation.h"
#include "dlw_log.h"
#include "dlw_menu_animation_cache.h"
#include "dlw_path_security.h"

#define ANIM_GAMEDLL_HASH "cbc7dd7923778936341cdfb755474a0c714079d910849df16ffc5ec88a103b95"
#define ANIM_ENGINE_HASH "a18ca1103614947ae7ff956c6852f45123b440563a967d9e1e496c42fa574a3c"
#define ANIM_ACTOR_VTABLE 0x0167B0A8UL
#define ANIM_GAME_POINTER 0x01D03440UL
#define ANIM_MIXER_POINTER 0x00ACCB98UL
#define ANIM_CRITICAL_SECTION 0x00AD6AE8UL
#define ANIM_MODEL_INIT 0x0016FC10UL
#define ANIM_MODEL_SHUTDOWN 0x0016FDB0UL
#define ANIM_BANK_LOAD 0x000D5810UL
#define ANIM_BANK_FREE 0x000D5A60UL
#define ANIM_BANK_FIND 0x000C9590UL
#define ANIM_BIND_GET_IAT 0x013CCC20UL
#define ANIM_BIND_GET_RVA 0x0015B390UL
#define ANIM_BANK_OFFSET 0x410U
#define ANIM_BIND_OFFSET 0x418U
#define ANIM_LOOP_NAME_OFFSET 0xE0U
#define ANIM_HANDLE_OFFSET 0xF0U
#define ANIM_NAME_CAPACITY 260U
#define ANIM_RETRY_MS 1000ULL
#define ANIM_RETRY_LIMIT 20U

typedef struct MenuAnimCriticalSection {
    PVOID debug_info;
    LONG lock_count;
    LONG recursion_count;
    HANDLE owning_thread;
    HANDLE semaphore;
    ULONG_PTR spin_count;
} MenuAnimCriticalSection;

__declspec(dllimport) VOID WINAPI EnterCriticalSection(MenuAnimCriticalSection *);
__declspec(dllimport) VOID WINAPI LeaveCriticalSection(MenuAnimCriticalSection *);

typedef struct MenuAnimString {
    const char *data;
    unsigned int size;
    unsigned int capacity;
} MenuAnimString;

typedef struct MenuAnimId {
    int value;
    unsigned int padding;
    void *sequence;
} MenuAnimId;

typedef unsigned char (__fastcall *MenuAnimLoadPackFn)(
    void *, const MenuAnimString *, unsigned char, unsigned char, void **);
typedef void (__fastcall *MenuAnimFreePackFn)(void *, const MenuAnimString *);
typedef void *(__fastcall *MenuAnimBankLoadFn)(
    void *, const char *, const char *, unsigned char);
typedef char (__fastcall *MenuAnimBankFreeFn)(void *);
typedef MenuAnimId *(__fastcall *MenuAnimBankFindFn)(
    void *, MenuAnimId *, const char *);
typedef unsigned char (__fastcall *MenuAnimModelInitFn)(void *, const char *);
typedef void (__fastcall *MenuAnimModelShutdownFn)(void *);
typedef void *(__fastcall *MenuAnimGetBindFn)(void *);
typedef MenuAnimId *(__fastcall *MenuAnimFindFn)(
    void *, MenuAnimId *, const MenuAnimString *);
typedef MenuAnimId *(__fastcall *MenuAnimPlayFn)(
    void *, MenuAnimId *, MenuAnimId *, MenuAnimId *,
    int, int, float, float);

typedef struct MenuAnimState {
    BYTE *gamedll;
    BYTE *engine;
    void *game;
    void *pack;
    void *actor;
    void *background;
    void *model;
    void *custom_bind;
    MenuAnimLoadPackFn load_pack;
    MenuAnimFreePackFn free_pack;
    MenuAnimBankLoadFn bank_load;
    MenuAnimBankFreeFn bank_free;
    MenuAnimBankFindFn bank_find;
    MenuAnimModelInitFn model_init;
    MenuAnimModelShutdownFn model_shutdown;
    MenuAnimGetBindFn get_bind;
    MenuAnimCriticalSection *critical;
    char pack_path[DLUMA_MAX_WPATH];
    char original_bank[ANIM_NAME_CAPACITY];
    char original_loop[ANIM_NAME_CAPACITY];
    ULONGLONG next_attempt;
    unsigned char attempted;
    unsigned char ready;
    unsigned char awaiting_destroy;
    unsigned char bound;
    unsigned char pack_retained;
    unsigned char pack_registered;
    unsigned char recovery_pending;
    unsigned int attempts;
    unsigned int recovery_attempts;
} MenuAnimState;

static MenuAnimState g_menu_anim;

static int anim_module_hash(HMODULE module, const char *expected)
{
    WCHAR path[DLUMA_MAX_WPATH];
    char digest[DLUMA_SHA256_HEX_CAPACITY];
    return module &&
        dluma_get_module_path_checked(module, path, DLUMA_ARRAY_COUNT(path)) &&
        dluma_sha256_file_hex(path, 1024ULL * 1024ULL * 1024ULL, digest) &&
        dluma_stricmp(digest, expected) == 0;
}

static int anim_callable(void *address)
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

static int anim_copy_name(const char *source, char *out, unsigned int capacity)
{
    unsigned int i;
    if (!source || !out || capacity < 2U)
        return 0;
    for (i = 0; i + 1U < capacity; ++i) {
        if (!dluma_is_readable(source + i, 1))
            return 0;
        out[i] = source[i];
        if (!out[i])
            return i != 0U;
    }
    out[0] = 0;
    return 0;
}

static int anim_actor_model(MenuAnimState *state, void *actor, void **model_out)
{
    void *model;
    if (model_out)
        *model_out = 0;
    if (!state->ready || !actor ||
        !dluma_is_readable(actor, ANIM_HANDLE_OFFSET + sizeof(MenuAnimId)) ||
        *(void **)actor != state->gamedll + ANIM_ACTOR_VTABLE)
        return 0;
    model = *(void **)((BYTE *)actor + sizeof(void *));
    if (!model || !dluma_is_readable(model, ANIM_BIND_OFFSET + sizeof(void *)))
        return 0;
    if (model_out)
        *model_out = model;
    return 1;
}

static int anim_model_bank(void *model, char out[ANIM_NAME_CAPACITY])
{
    const char *bank;
    if (!model || !dluma_is_readable(
            (BYTE *)model + ANIM_BANK_OFFSET, sizeof(bank)))
        return 0;
    bank = *(const char **)((BYTE *)model + ANIM_BANK_OFFSET);
    return anim_copy_name(bank, out, ANIM_NAME_CAPACITY);
}

static int anim_actor_loop(void *actor, char out[ANIM_NAME_CAPACITY])
{
    const MenuAnimString *loop;
    if (!actor || !dluma_is_readable(
            (BYTE *)actor + ANIM_LOOP_NAME_OFFSET, sizeof(*loop)))
        return 0;
    loop = (const MenuAnimString *)((BYTE *)actor + ANIM_LOOP_NAME_OFFSET);
    if (!loop->data || !loop->size || loop->size >= ANIM_NAME_CAPACITY ||
        loop->size > loop->capacity ||
        !dluma_is_readable(loop->data, (SIZE_T)loop->size + 1U) ||
        loop->data[loop->size] != 0)
        return 0;
    memcpy(out, loop->data, loop->size + 1U);
    return strlen(out) == loop->size;
}

static int anim_play(MenuAnimState *state, void *actor, const char *name)
{
    MenuAnimString name_view;
    MenuAnimId animation;
    MenuAnimId slot;
    MenuAnimId played;
    void *bind;
    void **vtable;
    MenuAnimFindFn find;
    MenuAnimPlayFn play;
    if (!name || !name[0] || !state->get_bind || state->awaiting_destroy)
        return 0;
    bind = state->get_bind(actor);
    if (state->awaiting_destroy || !bind ||
        !dluma_is_readable(bind, sizeof(void *)))
        return 0;
    vtable = *(void ***)bind;
    if (!vtable || !dluma_is_readable((BYTE *)vtable + 0x148U, sizeof(void *)) ||
        !dluma_is_readable((BYTE *)vtable + 0xD8U, sizeof(void *)))
        return 0;
    find = *(MenuAnimFindFn *)((BYTE *)vtable + 0x148U);
    play = *(MenuAnimPlayFn *)((BYTE *)vtable + 0xD8U);
    if (!anim_callable((void *)find) || !anim_callable((void *)play))
        return 0;
    name_view.data = name;
    name_view.size = (unsigned int)strlen(name);
    name_view.capacity = name_view.size;
    memset(&animation, 0, sizeof(animation));
    animation.value = -1;
    if (find(bind, &animation, &name_view) != &animation ||
        animation.value == -1 || state->awaiting_destroy)
        return 0;
    memset(&slot, 0, sizeof(slot));
    slot.value = -1;
    memset(&played, 0, sizeof(played));
    played.value = -1;
    if (play(bind, &played, &slot, &animation, -1, 0, 1.0f, 0.0f) != &played ||
        played.value == -1 || state->awaiting_destroy)
        return 0;
    memcpy((BYTE *)actor + ANIM_HANDLE_OFFSET, &played, sizeof(played));
    return 1;
}

static void anim_clear_handle(void *actor)
{
    MenuAnimId invalid;
    memset(&invalid, 0, sizeof(invalid));
    invalid.value = -1;
    memcpy((BYTE *)actor + ANIM_HANDLE_OFFSET, &invalid, sizeof(invalid));
}

static int anim_mount(MenuAnimState *state)
{
    MenuAnimString path;
    WCHAR physical_path[DLUMA_MAX_WPATH];
    if (state->pack_registered)
        return 1;
    if (!dluma_is_readable(state->gamedll + ANIM_GAME_POINTER, sizeof(void *)))
        return 0;
    state->game = *(void **)(state->gamedll + ANIM_GAME_POINTER);
    if (!state->game || !dluma_is_readable(state->game, sizeof(void *)))
        return 0;
    if (!dluma_menu_animation_cache_materialize(
            physical_path, DLUMA_ARRAY_COUNT(physical_path)) ||
        !dluma_wide_to_utf8(physical_path, state->pack_path,
                            DLUMA_ARRAY_COUNT(state->pack_path)))
        return 0;
    path.data = state->pack_path;
    path.size = (unsigned int)strlen(path.data);
    path.capacity = path.size;
    state->pack_registered = state->load_pack(
        state->game, &path, 0, 0, &state->pack) != 0;
    return state->pack_registered;
}

static void anim_unmount(MenuAnimState *state)
{
    MenuAnimString path;
    if (!state->pack_registered || state->pack_retained)
        return;
    path.data = state->pack_path;
    path.size = (unsigned int)strlen(path.data);
    path.capacity = path.size;
    state->free_pack(state->game, &path);
    state->pack = 0;
    state->pack_registered = 0;
    state->pack_path[0] = 0;
    dluma_menu_animation_cache_cleanup();
}

static int anim_preflight_bank(MenuAnimState *state)
{
    void *mixer;
    void *bank;
    MenuAnimId id;
    int available;
    if (!dluma_is_readable(state->engine + ANIM_MIXER_POINTER, sizeof(mixer)))
        return 0;
    mixer = *(void **)(state->engine + ANIM_MIXER_POINTER);
    if (!mixer || !dluma_is_readable(mixer, 32U))
        return 0;
    bank = state->bank_load(mixer, "dluma_menu_animations", "", 0);
    if (!bank)
        return 0;
    memset(&id, 0, sizeof(id));
    id.value = -1;
    available = state->bank_find(bank, &id, "dluma_menu_thriller") == &id &&
        id.value != -1;
    (void)state->bank_free(bank);
    return available;
}

static int anim_restore(MenuAnimState *state, void *actor)
{
    void *model;
    void *current_bank;
    char bank[ANIM_NAME_CAPACITY];
    int ok;
    int has_bank;
    if (!state->bound && !state->recovery_pending)
        return 1;
    if (!anim_actor_model(state, actor, &model) || model != state->model)
        return 0;
    current_bank = *(void **)((BYTE *)model + ANIM_BANK_OFFSET);
    has_bank = anim_model_bank(model, bank);
    if (current_bank && !has_bank)
        return 0;
    if (has_bank && dluma_stricmp(bank, state->original_bank) == 0) {
        state->bound = 0;
        state->custom_bind = 0;
        if (!anim_play(state, actor, state->original_loop)) {
            state->recovery_pending = 1;
            return 0;
        }
        state->recovery_pending = 0;
        return 1;
    }
    if (has_bank &&
        (dluma_stricmp(bank, "dluma_menu_animations") != 0 ||
         *(void **)((BYTE *)model + ANIM_BIND_OFFSET) != state->custom_bind)) {
        state->pack_retained = 1;
        state->bound = 0;
        state->recovery_pending = 0;
        state->custom_bind = 0;
        return 1;
    }
    ok = 0;
    state->recovery_pending = 1;
    EnterCriticalSection(state->critical);
    __try {
        if (current_bank) {
            anim_clear_handle(actor);
            state->model_shutdown(model);
            if (state->awaiting_destroy)
                __leave;
        }
        state->bound = 0;
        state->custom_bind = 0;
        ok = state->model_init(model, state->original_bank) != 0;
    } __finally {
        LeaveCriticalSection(state->critical);
    }
    if (!ok || state->awaiting_destroy)
        return 0;
    if (!anim_play(state, actor, state->original_loop))
        return 0;
    state->recovery_pending = 0;
    return 1;
}

int dluma_main_menu_animation_start(void)
{
    static const BYTE init_prefix[] = {
        0x48,0x89,0x5c,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x57,0x48,0x83,0xec,0x30
    };
    MenuAnimState *state = &g_menu_anim;
    HMODULE gamedll = GetModuleHandleW(L"gamedll_x64_rwdi.dll");
    HMODULE engine = GetModuleHandleW(L"engine_x64_rwdi.dll");
    BYTE *g = (BYTE *)gamedll;
    BYTE *e = (BYTE *)engine;
    if (state->ready)
        return 1;
    if (!anim_module_hash(gamedll, ANIM_GAMEDLL_HASH) ||
        !anim_module_hash(engine, ANIM_ENGINE_HASH) ||
        !dluma_is_readable(e + ANIM_MODEL_INIT, sizeof(init_prefix)) ||
        memcmp(e + ANIM_MODEL_INIT, init_prefix, sizeof(init_prefix)) != 0 ||
        !dluma_is_readable(g + ANIM_BIND_GET_IAT, sizeof(void *)) ||
        *(void **)(g + ANIM_BIND_GET_IAT) != e + ANIM_BIND_GET_RVA ||
        !dluma_is_readable(g + ANIM_GAME_POINTER, sizeof(void *)))
        return 0;
    memset(state, 0, sizeof(*state));
    state->gamedll = g;
    state->engine = e;
    state->game = *(void **)(g + ANIM_GAME_POINTER);
    state->load_pack = (MenuAnimLoadPackFn)GetProcAddress(
        engine, "?LoadPack@IGame@@QEAA_NAEBV?$string_base@D@ttl@@_N1PEAPEAUSRPackHandle@1@@Z");
    state->free_pack = (MenuAnimFreePackFn)GetProcAddress(
        engine, "?FreePack@IGame@@QEAAXAEBV?$string_base@D@ttl@@@Z");
    state->model_init = (MenuAnimModelInitFn)(e + ANIM_MODEL_INIT);
    state->model_shutdown = (MenuAnimModelShutdownFn)(e + ANIM_MODEL_SHUTDOWN);
    state->bank_load = (MenuAnimBankLoadFn)(e + ANIM_BANK_LOAD);
    state->bank_free = (MenuAnimBankFreeFn)(e + ANIM_BANK_FREE);
    state->bank_find = (MenuAnimBankFindFn)(e + ANIM_BANK_FIND);
    state->get_bind = *(MenuAnimGetBindFn *)(g + ANIM_BIND_GET_IAT);
    state->critical = (MenuAnimCriticalSection *)(e + ANIM_CRITICAL_SECTION);
    if (!state->load_pack || !state->free_pack ||
        !dluma_is_readable(state->critical, sizeof(*state->critical)) ||
        !anim_callable((void *)state->load_pack) ||
        !anim_callable((void *)state->free_pack))
        return 0;
    state->ready = 1;
    return 1;
}

void dluma_main_menu_animation_tick(
    void *background, void *actor, int enabled, ULONGLONG now)
{
    MenuAnimState *state = &g_menu_anim;
    void *model;
    int initialized;
    int played;
    if (!state->ready || state->awaiting_destroy || !background || !actor ||
        !anim_actor_model(state, actor, &model))
        return;
    if (state->background && state->background != background)
        return;
    if (state->actor && state->actor != actor) {
        state->pack_retained = 1;
        state->bound = 0;
        state->recovery_pending = 0;
        state->custom_bind = 0;
        state->awaiting_destroy = 1;
        return;
    }
    if (state->bound &&
        (model != state->model ||
         *(void **)((BYTE *)model + ANIM_BIND_OFFSET) != state->custom_bind)) {
        state->pack_retained = 1;
        state->bound = 0;
        state->custom_bind = 0;
        state->awaiting_destroy = 1;
        return;
    }
    if (state->recovery_pending) {
        if (state->recovery_attempts < ANIM_RETRY_LIMIT &&
            (!state->next_attempt || now >= state->next_attempt)) {
            state->next_attempt = now + ANIM_RETRY_MS;
            ++state->recovery_attempts;
            if (anim_restore(state, actor))
                anim_unmount(state);
        }
        return;
    }
    if (!enabled) {
        if (state->bound && state->recovery_attempts < ANIM_RETRY_LIMIT &&
            (!state->next_attempt || now >= state->next_attempt)) {
            state->next_attempt = now + ANIM_RETRY_MS;
            ++state->recovery_attempts;
            if (anim_restore(state, actor))
                anim_unmount(state);
        }
        return;
    }
    if (state->bound || state->attempted ||
        (state->next_attempt && now < state->next_attempt))
        return;
    state->next_attempt = now + ANIM_RETRY_MS;
    if (state->attempts >= ANIM_RETRY_LIMIT)
        return;
    ++state->attempts;
    state->actor = actor;
    state->background = background;
    state->model = model;
    if (!anim_model_bank(model, state->original_bank) ||
        !anim_actor_loop(actor, state->original_loop))
        return;
    state->attempted = 1;
    if (!anim_mount(state)) {
        dluma_log_pair("WARN", "main_menu_animation", "pack_load_failed");
        return;
    }
    initialized = 0;
    EnterCriticalSection(state->critical);
    __try {
        if (anim_preflight_bank(state)) {
            anim_clear_handle(actor);
            state->model_shutdown(model);
            if (state->awaiting_destroy)
                __leave;
            initialized = state->model_init(model, "dluma_menu_animations") != 0;
            if (state->awaiting_destroy)
                __leave;
            state->custom_bind = *(void **)((BYTE *)model + ANIM_BIND_OFFSET);
            state->bound = initialized && state->custom_bind != 0;
            if (!state->bound) {
                anim_clear_handle(actor);
                state->model_shutdown(model);
                if (state->awaiting_destroy)
                    __leave;
                initialized = state->model_init(model, state->original_bank) != 0;
                if (!initialized)
                    state->recovery_pending = 1;
            }
        } else
            dluma_log_pair("WARN", "main_menu_animation", "bank_lookup_failed");
    } __finally {
        LeaveCriticalSection(state->critical);
    }
    if (state->awaiting_destroy)
        return;
    if (!state->bound) {
        if (initialized && !anim_play(state, actor, state->original_loop))
            state->recovery_pending = 1;
        if (!state->recovery_pending)
            anim_unmount(state);
        return;
    }
    played = anim_play(state, actor, "dluma_menu_thriller");
    if (!played)
        dluma_log_pair("WARN", "main_menu_animation", "playback_failed");
    if (!played && anim_restore(state, actor))
        anim_unmount(state);
}

int dluma_main_menu_animation_prepare_model_change(void *actor, ULONGLONG now)
{
    MenuAnimState *state = &g_menu_anim;
    if (!actor || state->actor != actor)
        return 1;
    dluma_main_menu_animation_tick(state->background, actor, 0, now);
    return !state->bound && !state->recovery_pending &&
        !state->awaiting_destroy;
}

void dluma_main_menu_animation_forget(void *background)
{
    MenuAnimState *state = &g_menu_anim;
    if (background && state->background == background)
        state->awaiting_destroy = 1;
}

void dluma_main_menu_animation_before_destroy(void *background)
{
    MenuAnimState *state = &g_menu_anim;
    if (!state->ready || !background || state->background != background ||
        !state->pack_registered)
        return;
    if (!state->awaiting_destroy && anim_restore(state, state->actor))
        anim_unmount(state);
    else
        state->pack_retained = 1;
}

void dluma_main_menu_animation_after_destroy(void *background)
{
    MenuAnimState *state = &g_menu_anim;
    if (!state->ready || !background || state->background != background)
        return;
    state->bound = 0;
    state->recovery_pending = 0;
    state->custom_bind = 0;
    /* The background callback can precede final model destruction. Keep a
     * remaining pack registered when its last bind could not be restored. */
    if (state->pack_registered)
        state->pack_retained = 1;
    state->actor = 0;
    state->background = 0;
    state->model = 0;
    state->attempted = 0;
    state->awaiting_destroy = 0;
    state->original_bank[0] = 0;
    state->original_loop[0] = 0;
    state->next_attempt = 0;
    state->attempts = 0;
    state->recovery_attempts = 0;
}

int dluma_main_menu_animation_stop(void)
{
    MenuAnimState *state = &g_menu_anim;
    if (state->pack_registered || state->bound || state->awaiting_destroy ||
        state->recovery_pending)
        return 0;
    memset(state, 0, sizeof(*state));
    return 1;
}
