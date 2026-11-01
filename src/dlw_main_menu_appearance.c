#include "dlw_main_menu_appearance.h"
#include "dlw_main_menu_animation.h"
#include "dlw_common.h"
#include "dlw_main_menu_presentation_core.h"
#include "dlw_path_security.h"

#define MENU_GAMEDLL_HASH "cbc7dd7923778936341cdfb755474a0c714079d910849df16ffc5ec88a103b95"
#define MENU_ENGINE_HASH "a18ca1103614947ae7ff956c6852f45123b440563a967d9e1e496c42fa574a3c"
#define MENU_FRAME_SLOT 0x0167AA90UL
#define MENU_DESTROY_SLOT 0x0167AA58UL
#define MENU_FRAME_RVA 0x007587B0UL
#define MENU_DESTROY_RVA 0x00758830UL
#define MENU_SET_APPEARANCE_RVA 0x0075C590UL
#define MENU_MODEL_VTABLE_RVA 0x0167B0A8UL
#define MENU_BACKGROUND_PRIMARY_VTABLE_RVA 0x0167A0F8UL
#define MENU_BACKGROUND_VTABLE_RVA 0x0167A6A8UL
#define MENU_GET_MESH_IAT 0x013D0D60UL
#define MENU_GET_SKIN_IAT 0x013CCC68UL
#define MENU_GET_BIND_IAT 0x013CCC20UL
#define MENU_GET_BIND_ENGINE_RVA 0x0015B390UL
#define MENU_FREE_IAT 0x013CC198UL
#define MENU_ACTOR_OFFSET 0x0AC8U
#define MENU_APPEARANCE_READY_OFFSET 0x0AFCU
#define MENU_NAME_CAPACITY 512U
#define MENU_RETRY_LIMIT 20U
#define MENU_RETRY_MS 500ULL
#define MENU_DATE_INTERVAL_MS 60000ULL

typedef struct MenuString {
    const char *data;
    unsigned int size;
    unsigned int capacity;
} MenuString;

typedef __int64 (__fastcall *MenuFrameFn)(void *, float);
typedef void (__fastcall *MenuDestroyFn)(void *);
typedef unsigned char (__fastcall *MenuSetAppearanceFn)(
    void *, const MenuString *, const MenuString *);
typedef MenuString *(__fastcall *MenuGetNameFn)(void *, MenuString *);
typedef void *(__fastcall *MenuGetBindFn)(void *);
typedef void (__cdecl *MenuFreeFn)(void *);

typedef struct MenuAppearanceState {
    BYTE *gamedll;
    void **frame_slot;
    void **destroy_slot;
    MenuFrameFn frame;
    MenuDestroyFn destroy;
    MenuSetAppearanceFn set_appearance;
    MenuGetNameFn get_mesh;
    MenuGetNameFn get_skin;
    MenuGetBindFn get_bind;
    MenuFreeFn game_free;
    volatile LONG active_calls;
    volatile LONG stopping;
    volatile LONG lock;
    volatile LONG retiring;
    volatile LONG destroying;
    volatile LONG lock_owner;
    void *background;
    void *destroyed_background;
    void *actor;
    const DlumaMainMenuAppearanceSet *season;
    const DlumaMainMenuAppearance *selected;
    const DlumaMainMenuAppearance *applied;
    unsigned int candidate_index;
    unsigned int candidates_tried;
    ULONGLONG selection_seed;
    ULONGLONG next_date_ms;
    ULONGLONG next_attempt_ms;
    unsigned int attempts;
    unsigned char abandoned;
    unsigned char recovering;
    unsigned char frame_hooked;
    unsigned char destroy_hooked;
    char saved_mesh[MENU_NAME_CAPACITY];
    char saved_skin[MENU_NAME_CAPACITY];
    unsigned char installed;
    unsigned char animation_enabled;
    unsigned char animation_choice_logged;
} MenuAppearanceState;

static MenuAppearanceState g_menu_appearance;

static void menu_unlock(MenuAppearanceState *state)
{
    dluma_atomic_exchange(&state->lock, 0);
}

static void menu_clear_generation(MenuAppearanceState *state)
{
    dluma_main_menu_animation_forget(state->background);
    state->background = 0;
    state->actor = 0;
    state->season = 0;
    state->selected = 0;
    state->applied = 0;
    state->candidate_index = 0;
    state->candidates_tried = 0;
    state->next_date_ms = 0;
    state->next_attempt_ms = 0;
    state->attempts = 0;
    state->abandoned = 0;
    state->recovering = 0;
    state->animation_enabled = 0;
    state->saved_mesh[0] = 0;
    state->saved_skin[0] = 0;
}

static int menu_module_hash(HMODULE module, const char *expected)
{
    WCHAR path[DLUMA_MAX_WPATH];
    char digest[DLUMA_SHA256_HEX_CAPACITY];
    return module &&
        dluma_get_module_path_checked(module, path, DLUMA_ARRAY_COUNT(path)) &&
        dluma_sha256_file_hex(path, 1024ULL * 1024ULL * 1024ULL, digest) &&
        dluma_stricmp(digest, expected) == 0;
}

static int menu_bytes_match(const BYTE *address, const BYTE *expected,
                            unsigned int count)
{
    return address && dluma_is_readable(address, count) &&
        memcmp(address, expected, count) == 0;
}

static int menu_patch_slot(void **slot, void *expected, void *replacement)
{
    DWORD previous;
    DWORD restored;
    void *old;
    if (!slot || !expected || !replacement ||
        !dluma_is_readable(slot, sizeof(*slot)) ||
        !VirtualProtect(slot, sizeof(*slot), PAGE_READWRITE, &previous))
        return 0;
    old = dluma_atomic_pointer_compare_exchange(
        (PVOID volatile *)slot, replacement, expected);
    if (!VirtualProtect(slot, sizeof(*slot), previous, &restored)) {
        if (old == expected)
            (void)dluma_atomic_pointer_compare_exchange(
                (PVOID volatile *)slot, expected, replacement);
        (void)VirtualProtect(slot, sizeof(*slot), previous, &restored);
        return 0;
    }
    return old == expected;
}

static int menu_copy_name(MenuAppearanceState *state, MenuGetNameFn getter,
                          void *actor, char out[MENU_NAME_CAPACITY])
{
    MenuString value;
    MenuString *result;
    unsigned int i;
    int ok = 0;
    memset(&value, 0, sizeof(value));
    out[0] = 0;
    result = getter(actor, &value);
    if (result == &value && value.data && value.size > 0U &&
        value.size < MENU_NAME_CAPACITY && value.capacity >= value.size &&
        dluma_is_readable(value.data, (SIZE_T)value.size + 1U) &&
        value.data[value.size] == 0) {
        for (i = 0; i < value.size; ++i) {
            if (!value.data[i])
                break;
            out[i] = value.data[i];
        }
        if (i == value.size) {
            out[i] = 0;
            ok = 1;
        }
    }
    if (value.data)
        state->game_free((void *)value.data);
    return ok;
}

static int menu_read_appearance(MenuAppearanceState *state, void *actor,
                                char mesh[MENU_NAME_CAPACITY],
                                char skin[MENU_NAME_CAPACITY])
{
    return menu_copy_name(state, state->get_mesh, actor, mesh) &&
        !state->retiring &&
        menu_copy_name(state, state->get_skin, actor, skin) &&
        !state->retiring;
}

static int menu_matches(const char *mesh, const char *skin,
                        const DlumaMainMenuAppearance *appearance)
{
    return appearance && dluma_stricmp(mesh, appearance->model) == 0 &&
        dluma_stricmp(skin, appearance->skin) == 0;
}

static MenuString menu_string(const char *text)
{
    MenuString result;
    result.data = text;
    result.size = (unsigned int)strlen(text);
    result.capacity = result.size;
    return result;
}

static int menu_callable(void *address)
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

static int menu_animation_ready(MenuAppearanceState *state, void *actor,
                                const char *mesh)
{
    char current[MENU_NAME_CAPACITY];
    void *model;
    void **model_vtable;
    void *bind;
    void **bind_vtable;
    if (!actor || !mesh || !state->get_bind ||
        !dluma_is_readable(actor, 0x100U))
        return 0;
    if (*(const unsigned int *)((const BYTE *)actor + 0xF0U) == 0xffffffffU)
        return 1;
    if (!menu_copy_name(state, state->get_mesh, actor, current) ||
        state->retiring)
        return 0;
    if (strlen(current) == strlen(mesh) &&
        memcmp(current, mesh, strlen(mesh)) == 0)
        return 1;
    model = *(void **)((BYTE *)actor + sizeof(void *));
    if (!model || !dluma_is_readable(model, sizeof(void *)))
        return 0;
    model_vtable = *(void ***)model;
    if (!model_vtable ||
        !dluma_is_readable((BYTE *)model_vtable + 0x738U, sizeof(void *)) ||
        !menu_callable(*(void **)((BYTE *)model_vtable + 0x738U)))
        return 0;
    bind = state->get_bind(actor);
    if (state->retiring || !bind || !dluma_is_readable(bind, sizeof(void *)))
        return 0;
    bind_vtable = *(void ***)bind;
    return bind_vtable &&
        dluma_is_readable((BYTE *)bind_vtable + 0x118U, sizeof(void *)) &&
        menu_callable(*(void **)((BYTE *)bind_vtable + 0x118U));
}

typedef enum MenuSetResult {
    MENU_SET_DEFERRED,
    MENU_SET_FAILED,
    MENU_SET_APPLIED
} MenuSetResult;

static MenuSetResult menu_set(MenuAppearanceState *state, void *actor,
                    const char *mesh, const char *skin)
{
    MenuString model_value;
    MenuString skin_value;
    if (!dluma_main_menu_animation_prepare_model_change(actor, GetTickCount64()) ||
        !menu_animation_ready(state, actor, mesh))
        return MENU_SET_DEFERRED;
    model_value = menu_string(mesh);
    skin_value = menu_string(skin);
    return state->set_appearance(actor, &model_value, &skin_value)
        ? MENU_SET_APPLIED : MENU_SET_FAILED;
}

static const DlumaMainMenuAppearanceSet *menu_date_appearance(
    MenuAppearanceState *state)
{
    SYSTEMTIME now;
    GetLocalTime(&now);
#ifdef DLUMA_MAIN_MENU_APPEARANCE_DATE
    now.wYear = (WORD)(DLUMA_MAIN_MENU_APPEARANCE_DATE / 10000);
    now.wMonth = (WORD)((DLUMA_MAIN_MENU_APPEARANCE_DATE / 100) % 100);
    now.wDay = (WORD)(DLUMA_MAIN_MENU_APPEARANCE_DATE % 100);
#endif
    state->animation_enabled = (unsigned char)
        dluma_main_menu_presentation_halloween_animation(
            now.wYear, now.wMonth, now.wDay,
            (unsigned int)(state->selection_seed >> 32U));
    if (state->animation_enabled && !state->animation_choice_logged) {
        dluma_log_line("INFO", "Happy Halloween");
        state->animation_choice_logged = 1;
    }
    return dluma_main_menu_presentation_appearances(
        now.wYear, now.wMonth, now.wDay);
}

static int menu_actor_valid(MenuAppearanceState *state, void *background,
                            void *actor)
{
    void *vtable;
    return !state->retiring && background && actor &&
        dluma_is_readable(background, MENU_APPEARANCE_READY_OFFSET + 1U) &&
        *(void **)background ==
            state->gamedll + MENU_BACKGROUND_PRIMARY_VTABLE_RVA &&
        *(void **)((BYTE *)background + MENU_ACTOR_OFFSET) == actor &&
        dluma_is_readable(actor, 0x190U) &&
        (vtable = *(void **)actor) != 0 &&
        vtable == state->gamedll + MENU_MODEL_VTABLE_RVA;
}

static void menu_update(MenuAppearanceState *state, void *background,
                        ULONGLONG now)
{
    const DlumaMainMenuAppearanceSet *desired;
    void *actor;
    char mesh[MENU_NAME_CAPACITY];
    char skin[MENU_NAME_CAPACITY];
    char actual_mesh[MENU_NAME_CAPACITY];
    char actual_skin[MENU_NAME_CAPACITY];
    MenuSetResult set_result;
    if (state->background != background) {
        menu_clear_generation(state);
        state->background = background;
    }
    if (!state->next_date_ms || now >= state->next_date_ms) {
        desired = menu_date_appearance(state);
        if (desired != state->season) {
            state->season = desired;
            state->candidate_index = desired && desired->count
                ? (unsigned int)(state->selection_seed % desired->count) : 0U;
            state->selected = desired && desired->count
                ? &desired->appearances[state->candidate_index] : 0;
            state->candidates_tried = 0;
            state->attempts = 0;
            state->next_attempt_ms = 0;
            state->abandoned = 0;
        }
        state->next_date_ms = now + MENU_DATE_INTERVAL_MS;
    }
    if (!state->selected && !state->applied && !state->recovering)
        return;
    if (state->next_attempt_ms && now < state->next_attempt_ms)
        return;
    state->next_attempt_ms = now + MENU_RETRY_MS;
    if ((state->abandoned && !state->recovering) ||
        state->attempts >= MENU_RETRY_LIMIT)
        return;
    if (!dluma_is_readable(background, MENU_APPEARANCE_READY_OFFSET + 1U) ||
        !*((BYTE *)background + MENU_APPEARANCE_READY_OFFSET)) {
        ++state->attempts;
        return;
    }
    actor = *(void **)((BYTE *)background + MENU_ACTOR_OFFSET);
    if (!menu_actor_valid(state, background, actor)) {
        ++state->attempts;
        return;
    }
    if (state->actor != actor) {
        state->actor = actor;
        state->applied = 0;
        state->recovering = 0;
        state->saved_mesh[0] = 0;
        state->saved_skin[0] = 0;
    }
    if (!menu_read_appearance(state, actor, mesh, skin)) {
        ++state->attempts;
        return;
    }
    if (state->retiring)
        return;
    if (state->recovering) {
        ++state->attempts;
        if (dluma_stricmp(mesh, state->saved_mesh) != 0 ||
            dluma_stricmp(skin, state->saved_skin) != 0) {
            (void)menu_set(state, actor, state->saved_mesh, state->saved_skin);
            if (state->retiring ||
                !menu_read_appearance(state, actor, mesh, skin) ||
                dluma_stricmp(mesh, state->saved_mesh) != 0 ||
                dluma_stricmp(skin, state->saved_skin) != 0)
                return;
        }
        state->recovering = 0;
        state->applied = 0;
        state->abandoned = 1;
        state->saved_mesh[0] = 0;
        state->saved_skin[0] = 0;
        return;
    }
    if (state->applied && !menu_matches(mesh, skin, state->applied)) {
        state->applied = 0;
        state->saved_mesh[0] = 0;
        state->saved_skin[0] = 0;
        state->abandoned = 1;
        return;
    }
    if (!state->selected) {
        if (state->applied && state->saved_mesh[0] && state->saved_skin[0]) {
            ++state->attempts;
            (void)menu_set(state, actor, state->saved_mesh, state->saved_skin);
            if (!state->retiring &&
                menu_read_appearance(state, actor, actual_mesh, actual_skin) &&
                dluma_stricmp(actual_mesh, state->saved_mesh) == 0 &&
                dluma_stricmp(actual_skin, state->saved_skin) == 0) {
                state->applied = 0;
                state->saved_mesh[0] = 0;
                state->saved_skin[0] = 0;
            } else if (!state->retiring) {
                state->recovering = 1;
                state->attempts = 0;
            }
        }
        return;
    }
    if (state->applied == state->selected)
        return;
    if (!state->applied && !state->saved_mesh[0]) {
        memcpy(state->saved_mesh, mesh, strlen(mesh) + 1U);
        memcpy(state->saved_skin, skin, strlen(skin) + 1U);
    }
    ++state->attempts;
    set_result = menu_set(state, actor, state->selected->model, state->selected->skin);
    if (state->retiring || set_result == MENU_SET_DEFERRED)
        return;
    if (set_result == MENU_SET_APPLIED &&
        menu_read_appearance(state, actor, actual_mesh, actual_skin) &&
        menu_matches(actual_mesh, actual_skin, state->selected)) {
        state->applied = state->selected;
        state->attempts = 0;
        return;
    }
    /* Missing resources leave the actor unchanged. Try another outfit from
     * this season without replacing the original snapshot. */
    if (menu_read_appearance(state, actor, actual_mesh, actual_skin) &&
        dluma_stricmp(actual_mesh, mesh) == 0 &&
        dluma_stricmp(actual_skin, skin) == 0) {
        ++state->candidates_tried;
        if (state->season && state->candidates_tried < state->season->count) {
            state->candidate_index =
                (state->candidate_index + 1U) % state->season->count;
            state->selected =
                &state->season->appearances[state->candidate_index];
        } else {
            state->abandoned = 1;
            if (state->applied) {
                state->recovering = 1;
                state->attempts = 0;
            }
        }
        return;
    }
    if (menu_actor_valid(state, background, actor) &&
        state->saved_mesh[0] && state->saved_skin[0]) {
        state->recovering = 1;
        state->attempts = 0;
        (void)menu_set(state, actor, state->saved_mesh, state->saved_skin);
    }
    state->applied = 0;
}

static __int64 __fastcall menu_frame_hook(void *object, float delta)
{
    MenuAppearanceState *state = &g_menu_appearance;
    MenuFrameFn original = state->frame;
    __int64 result = 0;
    dluma_atomic_increment(&state->active_calls);
    if (original)
        result = original(object, delta);
    if (!state->stopping && object &&
        dluma_is_readable(object, sizeof(void *)) &&
        *(void **)object == state->gamedll + MENU_BACKGROUND_VTABLE_RVA) {
        if (dluma_atomic_compare_exchange(&state->lock, 1, 0) == 0) {
            dluma_atomic_exchange(&state->lock_owner, (LONG)GetCurrentThreadId());
            if (!state->stopping && !state->retiring) {
                __try {
                    void *background = (BYTE *)object - 16U;
                    void *actor = 0;
                    ULONGLONG now = GetTickCount64();
                    menu_update(state, background, now);
                    if (!state->retiring && dluma_is_readable(
                            background, MENU_APPEARANCE_READY_OFFSET + 1U) &&
                        *((BYTE *)background + MENU_APPEARANCE_READY_OFFSET))
                        actor = *(void **)((BYTE *)background + MENU_ACTOR_OFFSET);
                    if (menu_actor_valid(state, background, actor))
                        dluma_main_menu_animation_tick(
                            background, actor, state->animation_enabled, now);
                } __except (1) {
                    menu_clear_generation(state);
                    state->background = (BYTE *)object - 16U;
                    state->abandoned = 1;
                }
            }
            if (state->retiring) {
                if (state->destroyed_background) {
                    dluma_main_menu_animation_after_destroy(
                        state->destroyed_background);
                    state->destroyed_background = 0;
                }
                menu_clear_generation(state);
                if (!state->destroying)
                    dluma_atomic_exchange(&state->retiring, 0);
            }
            dluma_atomic_exchange(&state->lock_owner, 0);
            menu_unlock(state);
        }
    }
    dluma_atomic_decrement(&state->active_calls);
    return result;
}

static void __fastcall menu_destroy_hook(void *object)
{
    MenuAppearanceState *state = &g_menu_appearance;
    MenuDestroyFn original = state->destroy;
    void *background = (BYTE *)object - 16U;
    int locked;
    dluma_atomic_increment(&state->active_calls);
    dluma_atomic_increment(&state->destroying);
    dluma_atomic_exchange(&state->retiring, 1);
    locked = dluma_atomic_compare_exchange(&state->lock, 1, 0) == 0;
    if (!locked && state->lock_owner != (LONG)GetCurrentThreadId()) {
        while (dluma_atomic_compare_exchange(&state->lock, 1, 0) != 0)
            Sleep(0);
        locked = 1;
    }
    if (locked) {
        dluma_atomic_exchange(&state->lock_owner, (LONG)GetCurrentThreadId());
        dluma_atomic_exchange(&state->retiring, 1);
        dluma_main_menu_animation_before_destroy(background);
        if (state->background == background) {
            menu_clear_generation(state);
        }
        dluma_atomic_exchange(&state->lock_owner, 0);
        menu_unlock(state);
    } else
        dluma_main_menu_animation_forget(background);
    if (original)
        original(object);
    if (locked) {
        dluma_main_menu_animation_after_destroy(background);
        if (state->destroyed_background == background)
            state->destroyed_background = 0;
    } else
        state->destroyed_background = background;
    if (dluma_atomic_decrement(&state->destroying) == 0 && locked)
        dluma_atomic_exchange(&state->retiring, 0);
    dluma_atomic_decrement(&state->active_calls);
}

int dluma_main_menu_appearance_start(void)
{
    static const BYTE frame_prefix[] = {
        0x48,0x89,0x5c,0x24,0x08,0x57,0x48,0x83,0xec,0x20
    };
    static const BYTE setter_prefix[] = {
        0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x6c,0x24,0x10
    };
    MenuAppearanceState *state = &g_menu_appearance;
    HMODULE gamedll = GetModuleHandleW(L"gamedll_x64_rwdi.dll");
    HMODULE engine = GetModuleHandleW(L"engine_x64_rwdi.dll");
    BYTE *base = (BYTE *)gamedll;
    LARGE_INTEGER counter;
    if (state->installed)
        return 1;
    if (!menu_module_hash(gamedll, MENU_GAMEDLL_HASH) ||
        !menu_module_hash(engine, MENU_ENGINE_HASH) ||
        !menu_bytes_match(base + MENU_FRAME_RVA, frame_prefix,
                          sizeof(frame_prefix)) ||
        !menu_bytes_match(base + MENU_SET_APPEARANCE_RVA, setter_prefix,
                          sizeof(setter_prefix)))
        return 0;
    memset(state, 0, sizeof(*state));
    counter.QuadPart = 0;
    (void)QueryPerformanceCounter(&counter);
    state->selection_seed = (ULONGLONG)counter.QuadPart ^
        GetTickCount64() ^ (ULONGLONG)GetCurrentProcessId();
    state->selection_seed ^= state->selection_seed >> 30U;
    state->selection_seed *= 0xbf58476d1ce4e5b9ULL;
    state->selection_seed ^= state->selection_seed >> 27U;
    state->selection_seed *= 0x94d049bb133111ebULL;
    state->selection_seed ^= state->selection_seed >> 31U;
    state->gamedll = base;
    state->frame_slot = (void **)(base + MENU_FRAME_SLOT);
    state->destroy_slot = (void **)(base + MENU_DESTROY_SLOT);
    state->frame = (MenuFrameFn)(base + MENU_FRAME_RVA);
    state->destroy = (MenuDestroyFn)(base + MENU_DESTROY_RVA);
    state->set_appearance =
        (MenuSetAppearanceFn)(base + MENU_SET_APPEARANCE_RVA);
    state->get_mesh = *(MenuGetNameFn *)(base + MENU_GET_MESH_IAT);
    state->get_skin = *(MenuGetNameFn *)(base + MENU_GET_SKIN_IAT);
    state->get_bind = *(MenuGetBindFn *)(base + MENU_GET_BIND_IAT);
    state->game_free = *(MenuFreeFn *)(base + MENU_FREE_IAT);
    if (!state->get_mesh || !state->get_skin || !state->game_free ||
        !state->get_bind ||
        (void *)state->get_bind != (BYTE *)engine + MENU_GET_BIND_ENGINE_RVA ||
        (void *)state->get_bind != (void *)GetProcAddress(
            engine, "?GetAnimBind@IModelObject@@QEAAPEAVIAnimBind@@XZ") ||
        !menu_callable((void *)state->get_bind))
        return 0;
    (void)dluma_main_menu_animation_start();
    if (!menu_patch_slot(state->destroy_slot, (void *)state->destroy,
                         (void *)menu_destroy_hook))
        return 0;
    state->destroy_hooked = 1;
    state->installed = 1;
    if (!menu_patch_slot(state->frame_slot, (void *)state->frame,
                         (void *)menu_frame_hook)) {
        if (menu_patch_slot(state->destroy_slot, (void *)menu_destroy_hook,
                            (void *)state->destroy)) {
            state->destroy_hooked = 0;
            state->installed = 0;
        }
        return 0;
    }
    state->frame_hooked = 1;
    return 1;
}

int dluma_main_menu_appearance_stop(void)
{
    MenuAppearanceState *state = &g_menu_appearance;
    ULONGLONG started;
    int frame_restored;
    int destroy_restored;
    if (!state->installed)
        return dluma_main_menu_animation_stop();
    dluma_atomic_exchange(&state->stopping, 1);
    started = GetTickCount64();
    while (dluma_atomic_compare_exchange(&state->active_calls, 0, 0) != 0 &&
           GetTickCount64() - started < 2000ULL)
        Sleep(1);
    if (dluma_atomic_compare_exchange(&state->active_calls, 0, 0) != 0 ||
        !dluma_main_menu_animation_stop())
        return 0;
    frame_restored = !state->frame_hooked ||
        menu_patch_slot(state->frame_slot, (void *)menu_frame_hook,
                        (void *)state->frame);
    if (frame_restored)
        state->frame_hooked = 0;
    destroy_restored = !state->destroy_hooked ||
        menu_patch_slot(state->destroy_slot, (void *)menu_destroy_hook,
                        (void *)state->destroy);
    if (destroy_restored)
        state->destroy_hooked = 0;
    if (!frame_restored || !destroy_restored)
        return 0;
    started = GetTickCount64();
    while (dluma_atomic_compare_exchange(&state->active_calls, 0, 0) != 0 &&
           GetTickCount64() - started < 2000ULL)
        Sleep(1);
    if (dluma_atomic_compare_exchange(&state->active_calls, 0, 0) != 0)
        return 0;
    if (!dluma_main_menu_animation_stop())
        return 0;
    memset(state, 0, sizeof(*state));
    return 1;
}
