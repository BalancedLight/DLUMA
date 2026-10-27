#include "dlw_animation_pack_runtime.h"

#include "dlw_dual_host_runtime.h"
#include "dlw_log.h"
#include "dlw_path_security.h"

#include <string.h>

#define DLUMA_ANIMATION_PACK_GAMEDLL_GAME_RVA 0x01D03440U

typedef struct DlumaAnimationPackStringView {
    const char* data;
    uint32_t size;
    uint32_t capacity;
} DlumaAnimationPackStringView;

typedef unsigned char (__fastcall *DlumaAnimationLoadPackFn)(
    void* game,
    const DlumaAnimationPackStringView* name,
    unsigned char first_flag,
    unsigned char second_flag,
    void** output_handle);
typedef void (__fastcall *DlumaAnimationFreePackFn)(
    void* game,
    const DlumaAnimationPackStringView* name);

typedef struct DlumaAnimationPackRuntime {
    volatile LONG lock;
    HMODULE engine;
    HMODULE gamedll;
    void* game;
    DlumaAnimationLoadPackFn load_pack;
    DlumaAnimationFreePackFn free_pack;
    char current_path[DLUMA_MAX_WPATH];
    void* current_handle;
    unsigned int exact_profile;
} DlumaAnimationPackRuntime;

static DlumaAnimationPackRuntime g_dluma_animation_pack;

static const char g_dluma_animation_pack_load_export[] =
    "?LoadPack@IGame@@QEAA_NAEBV?$string_base@D@ttl@@_N1PEAPEAUSRPackHandle@1@@Z";
static const char g_dluma_animation_pack_free_export[] =
    "?FreePack@IGame@@QEAAXAEBV?$string_base@D@ttl@@@Z";
static const BYTE g_dluma_animation_pack_load_prefix[] = {
    0x48,0x83,0xEC,0x38,0x45,0x33,0xD2,0x45,
    0x84,0xC0,0xB8,0x04,0x00,0x00,0x00,0x44};
static const BYTE g_dluma_animation_pack_free_prefix[] = {
    0x48,0x89,0x5C,0x24,0x18,0x48,0x89,0x6C,
    0x24,0x20,0x56,0x57,0x41,0x55,0x48,0x83};

static void dluma_animation_pack_lock(void)
{
    while (dluma_atomic_compare_exchange(
               &g_dluma_animation_pack.lock, 1, 0) != 0)
        Sleep(0);
}

static void dluma_animation_pack_unlock(void)
{
    dluma_atomic_exchange(&g_dluma_animation_pack.lock, 0);
}

static int dluma_animation_pack_module_sha256(
    HMODULE module,
    char output[DLUMA_SHA256_HEX_CAPACITY])
{
    WCHAR path[DLUMA_MAX_WPATH];
    output[0] = 0;
    return module &&
        dluma_get_module_path_checked(
            module, path, DLUMA_ARRAY_COUNT(path)) &&
        dluma_sha256_file_hex(
            path, 512ULL * 1024ULL * 1024ULL, output);
}

static int dluma_animation_pack_profile_matches(
    HMODULE engine,
    HMODULE gamedll)
{
    static const char editor_engine[] =
        "5B8090BFC3D3A25A24DA1846A3AAFD65C97E0D1E700B5DFEBFC7132737B6BFF6";
    static const char editor_gamedll[] =
        "8E19C89C316E319E1E88D0C7A6F6A935C4B9C381905CAB48449CEE8FD6CCBA54";
    static const char player_engine[] =
        "A18CA1103614947AE7FF956C6852F45123B440563A967D9E1E496C42FA574A3C";
    static const char player_gamedll[] =
        "CBC7DD7923778936341CDFB755474A0C714079D910849DF16FFC5EC88A103B95";
    char engine_sha256[DLUMA_SHA256_HEX_CAPACITY];
    char gamedll_sha256[DLUMA_SHA256_HEX_CAPACITY];
    int editor_pair;
    int player_pair;
    if (!dluma_animation_pack_module_sha256(engine, engine_sha256) ||
        !dluma_animation_pack_module_sha256(gamedll, gamedll_sha256))
        return 0;
    editor_pair =
        dluma_stricmp(engine_sha256, editor_engine) == 0 &&
        dluma_stricmp(gamedll_sha256, editor_gamedll) == 0;
    player_pair =
        dluma_stricmp(engine_sha256, player_engine) == 0 &&
        dluma_stricmp(gamedll_sha256, player_gamedll) == 0;
    return editor_pair || player_pair;
}

static int dluma_animation_pack_bind(void)
{
    HMODULE engine;
    HMODULE gamedll;
    BYTE* load;
    BYTE* free_pack;
    void* game = 0;
    if (g_dluma_animation_pack.exact_profile &&
        g_dluma_animation_pack.load_pack &&
        g_dluma_animation_pack.free_pack &&
        g_dluma_animation_pack.game)
        return 1;
    engine = GetModuleHandleW(L"engine_x64_rwdi.dll");
    gamedll = GetModuleHandleW(L"gamedll_x64_rwdi.dll");
    if (!engine || !gamedll ||
        !dluma_animation_pack_profile_matches(engine, gamedll))
        return 0;
    load = (BYTE*)GetProcAddress(
        engine, g_dluma_animation_pack_load_export);
    free_pack = (BYTE*)GetProcAddress(
        engine, g_dluma_animation_pack_free_export);
    if (!load || !free_pack ||
        !dluma_is_readable(load, sizeof(g_dluma_animation_pack_load_prefix)) ||
        !dluma_is_readable(
            free_pack, sizeof(g_dluma_animation_pack_free_prefix)) ||
        memcmp(
            load, g_dluma_animation_pack_load_prefix,
            sizeof(g_dluma_animation_pack_load_prefix)) != 0 ||
        memcmp(
            free_pack, g_dluma_animation_pack_free_prefix,
            sizeof(g_dluma_animation_pack_free_prefix)) != 0 ||
        !dluma_is_readable(
            (BYTE*)gamedll + DLUMA_ANIMATION_PACK_GAMEDLL_GAME_RVA,
            sizeof(game)))
        return 0;
    memcpy(
        &game,
        (BYTE*)gamedll + DLUMA_ANIMATION_PACK_GAMEDLL_GAME_RVA,
        sizeof(game));
    if (!game || !dluma_is_readable(game, sizeof(void*))) return 0;
    g_dluma_animation_pack.engine = engine;
    g_dluma_animation_pack.gamedll = gamedll;
    g_dluma_animation_pack.game = game;
    g_dluma_animation_pack.load_pack =
        (DlumaAnimationLoadPackFn)load;
    g_dluma_animation_pack.free_pack =
        (DlumaAnimationFreePackFn)free_pack;
    g_dluma_animation_pack.exact_profile = 1U;
    return 1;
}

static int dluma_animation_pack_load(
    const char* path,
    void** handle)
{
    DlumaAnimationPackStringView view;
    size_t length;
    if (!path || !path[0] || !handle || !dluma_animation_pack_bind())
        return 0;
    length = strlen(path);
    if (!length || length >= UINT32_MAX) return 0;
    view.data = path;
    view.size = (uint32_t)length;
    view.capacity = (uint32_t)length;
    *handle = 0;
    return g_dluma_animation_pack.load_pack(
               g_dluma_animation_pack.game, &view, 0, 0, handle) != 0;
}

static void dluma_animation_pack_free(const char* path)
{
    DlumaAnimationPackStringView view;
    size_t length;
    if (!path || !path[0] || !dluma_animation_pack_bind()) return;
    length = strlen(path);
    if (!length || length >= UINT32_MAX) return;
    view.data = path;
    view.size = (uint32_t)length;
    view.capacity = (uint32_t)length;
    g_dluma_animation_pack.free_pack(g_dluma_animation_pack.game, &view);
}

int dluma_animation_pack_runtime_swap(
    const WCHAR* absolute_pack_path,
    DlumaAnimationPackSwap* swap)
{
    char requested[DLUMA_MAX_WPATH];
    void* handle = 0;
    int result = 0;
    if (!absolute_pack_path || !absolute_pack_path[0] || !swap)
        return 0;
    memset(swap, 0, sizeof(*swap));
    requested[0] = 0;
    if (!dluma_wide_to_utf8(
            absolute_pack_path, requested, DLUMA_ARRAY_COUNT(requested)))
        return 0;
    dluma_animation_pack_lock();
    dluma_str_copy(
        swap->requested_path, DLUMA_ARRAY_COUNT(swap->requested_path),
        requested);
    if (g_dluma_animation_pack.current_path[0] &&
        dluma_stricmp(g_dluma_animation_pack.current_path, requested) == 0) {
        swap->requested_handle = g_dluma_animation_pack.current_handle;
        swap->registered = 1U;
        result = 1;
        goto done;
    }
    dluma_str_copy(
        swap->previous_path, DLUMA_ARRAY_COUNT(swap->previous_path),
        g_dluma_animation_pack.current_path);
    swap->previous_handle = g_dluma_animation_pack.current_handle;
    swap->changed = 1U;
    if (g_dluma_animation_pack.current_path[0]) {
        dluma_animation_pack_free(g_dluma_animation_pack.current_path);
        swap->previous_unmounted = 1U;
        g_dluma_animation_pack.current_path[0] = 0;
        g_dluma_animation_pack.current_handle = 0;
    }
    if (!dluma_animation_pack_load(requested, &handle)) {
        if (swap->previous_path[0]) {
            swap->rollback_attempted = 1U;
            if (dluma_animation_pack_load(
                    swap->previous_path,
                    &g_dluma_animation_pack.current_handle)) {
                dluma_str_copy(
                    g_dluma_animation_pack.current_path,
                    DLUMA_ARRAY_COUNT(g_dluma_animation_pack.current_path),
                    swap->previous_path);
                swap->rollback_succeeded = 1U;
            }
        }
        goto done;
    }
    dluma_str_copy(
        g_dluma_animation_pack.current_path,
        DLUMA_ARRAY_COUNT(g_dluma_animation_pack.current_path), requested);
    g_dluma_animation_pack.current_handle = handle;
    swap->requested_handle = handle;
    swap->registered = 1U;
    result = 1;
done:
    dluma_animation_pack_unlock();
    return result;
}

int dluma_animation_pack_runtime_rollback(DlumaAnimationPackSwap* swap)
{
    int result = 1;
    if (!swap) return 0;
    dluma_animation_pack_lock();
    swap->rollback_attempted = 1U;
    if (swap->changed && g_dluma_animation_pack.current_path[0] &&
        dluma_stricmp(
            g_dluma_animation_pack.current_path,
            swap->requested_path) == 0) {
        dluma_animation_pack_free(g_dluma_animation_pack.current_path);
        g_dluma_animation_pack.current_path[0] = 0;
        g_dluma_animation_pack.current_handle = 0;
    }
    if (swap->previous_path[0]) {
        void* handle = 0;
        if (!dluma_animation_pack_load(swap->previous_path, &handle)) {
            result = 0;
        } else {
            dluma_str_copy(
                g_dluma_animation_pack.current_path,
                DLUMA_ARRAY_COUNT(g_dluma_animation_pack.current_path),
                swap->previous_path);
            g_dluma_animation_pack.current_handle = handle;
        }
    }
    swap->rollback_succeeded = result ? 1U : 0U;
    dluma_animation_pack_unlock();
    return result;
}

int dluma_animation_pack_runtime_retained(void)
{
    int retained;
    dluma_animation_pack_lock();
    retained = g_dluma_animation_pack.current_path[0] != 0;
    dluma_animation_pack_unlock();
    return retained;
}

void dluma_animation_pack_runtime_shutdown(void)
{
    dluma_animation_pack_lock();
    if (g_dluma_animation_pack.current_path[0])
        dluma_animation_pack_free(g_dluma_animation_pack.current_path);
    g_dluma_animation_pack.current_path[0] = 0;
    g_dluma_animation_pack.current_handle = 0;
    dluma_animation_pack_unlock();
}
