#ifndef DLUMA_RUNTIME_UI_FIXES_H
#define DLUMA_RUNTIME_UI_FIXES_H

#include <string.h>

/*
 * Exact-build runtime adapter for the Chrome UI list contract that is not
 * represented correctly by the stock MenuSteamWorkshop-derived screen.
 *
 * MenuSteamWorkshop initializes a bounded number of visible rows. The loader
 * MODS screen has a much taller viewport, while the redesigned Workshop screen
 * intentionally retains its stock-sized map list. Capacity is changed only for
 * the exact visible loader screen/list instance.
 */

#define DLUMA_RUNTIME_UI_GAMEDLL_TIMESTAMP 0x623397AAUL
#define DLUMA_RUNTIME_UI_GAMEDLL_IMAGE_SIZE 0x02082000UL

#define DLUMA_RUNTIME_LIST_SET_CAPACITY_RVA 0x011242D0UL
#define DLUMA_RUNTIME_UI_LIST_VTABLE_RVA 0x01917C98UL
#define DLUMA_RUNTIME_UI_LIST_INTERFACE_VTABLE_RVA 0x01918688UL
#define DLUMA_RUNTIME_UI_LIST_INTERFACE_OFFSET 0x58UL
#define DLUMA_RUNTIME_LIST_PREFIX_SIZE 16U
#define DLUMA_RUNTIME_CAPACITY_TIMER_ID 0xD17CU
#define DLUMA_RUNTIME_CAPACITY_TIMER_MS 250U
#define DLUMA_RUNTIME_MODS_VISIBLE_ROWS 10

typedef struct DlumaRuntimeUiTtlString {
    const char *data;
    unsigned int size;
    unsigned int capacity;
} DlumaRuntimeUiTtlString;

typedef __int64 (__fastcall *DlumaRuntimeListSetCapacityFn)(
    void *list, int capacity, unsigned char resize_items);

typedef struct DlumaRuntimeListCapacityState {
    UINT_PTR timer_id;
    HWND window;
    HMODULE gamedll;
    DlumaRuntimeListSetCapacityFn set_capacity;
    void *mods_list;
    unsigned char ready;
} DlumaRuntimeListCapacityState;

static DlumaRuntimeListCapacityState g_dluma_runtime_list_capacity;

static DWORD dluma_runtime_ui_read_u32(const BYTE *address)
{
    return (DWORD)address[0] |
           ((DWORD)address[1] << 8) |
           ((DWORD)address[2] << 16) |
           ((DWORD)address[3] << 24);
}

static int dluma_runtime_ui_pe_identity(
    HMODULE module, DWORD *timestamp, DWORD *image_size)
{
    BYTE *base = (BYTE *)module;
    DWORD nt_offset;
    if (timestamp) *timestamp = 0;
    if (image_size) *image_size = 0;
    if (!base || !dluma_is_readable(base, 0x100) ||
        base[0] != 'M' || base[1] != 'Z')
        return 0;
    nt_offset = dluma_runtime_ui_read_u32(base + 0x3C);
    if (nt_offset > 0x100000 ||
        !dluma_is_readable(base + nt_offset, 0x58) ||
        memcmp(base + nt_offset, "PE\0\0", 4) != 0)
        return 0;
    if (timestamp)
        *timestamp = dluma_runtime_ui_read_u32(base + nt_offset + 8);
    if (image_size)
        *image_size = dluma_runtime_ui_read_u32(base + nt_offset + 0x50);
    return 1;
}

static void dluma_runtime_ui_ttl_string(
    DlumaRuntimeUiTtlString *value, const char *text)
{
    value->data = text;
    value->size = text ? (unsigned int)strlen(text) : 0;
    value->capacity = value->size;
}

static void *dluma_runtime_find_visible_list(
    void *level, const char *script_name)
{
    DlumaRuntimeUiTtlString script;
    DlumaRuntimeUiTtlString list_id;
    int screen_id;
    void *root;
    if (!level || !script_name || !g_dluma.engine.ui_exports_ready ||
        !g_dluma.engine.get_ui_screen_id_by_script_name ||
        !g_dluma.engine.is_ui_screen_visible ||
        !g_dluma.engine.find_ui_screen || !g_dluma.engine.find_ui_element)
        return 0;
    dluma_runtime_ui_ttl_string(&script, script_name);
    screen_id = g_dluma.engine.get_ui_screen_id_by_script_name(level, &script);
    if (screen_id < 0 ||
        !g_dluma.engine.is_ui_screen_visible(level, screen_id))
        return 0;
    root = g_dluma.engine.find_ui_screen(level, screen_id);
    if (!root) return 0;
    dluma_runtime_ui_ttl_string(&list_id, "L_Maps");
    return g_dluma.engine.find_ui_element(root, &list_id, 1);
}

static int dluma_runtime_list_contract_valid(void *list)
{
    void *primary;
    void *secondary;
    BYTE *base = (BYTE *)g_dluma_runtime_list_capacity.gamedll;
    if (!list || !base ||
        !dluma_is_readable(list, sizeof(void *)) ||
        !dluma_is_readable((BYTE *)list +
                             DLUMA_RUNTIME_UI_LIST_INTERFACE_OFFSET,
                         sizeof(void *)))
        return 0;
    primary = *(void **)list;
    secondary = *(void **)((BYTE *)list +
                            DLUMA_RUNTIME_UI_LIST_INTERFACE_OFFSET);
    return primary == base + DLUMA_RUNTIME_UI_LIST_VTABLE_RVA &&
           secondary == base +
                            DLUMA_RUNTIME_UI_LIST_INTERFACE_VTABLE_RVA;
}

static void dluma_runtime_apply_list_capacity(
    void *list, void **remembered, int capacity, const char *label)
{
    if (!list || list == *remembered ||
        !g_dluma_runtime_list_capacity.set_capacity ||
        !dluma_runtime_list_contract_valid(list))
        return;
    (void)g_dluma_runtime_list_capacity.set_capacity(list, capacity, 1);
    *remembered = list;
    dluma_log_u64("INFO", label, (ULONGLONG)(unsigned int)capacity);
}

static VOID CALLBACK dluma_runtime_list_capacity_timer(
    HWND window, unsigned int message, UINT_PTR timer_id, DWORD tick)
{
    void *game_interface;
    void *level;
    void *list;
    (void)window;
    (void)message;
    (void)timer_id;
    (void)tick;
    if (!g_dluma_runtime_list_capacity.ready ||
        g_dluma.stop_requested || !g_dluma.engine.ready ||
        !g_dluma.engine.get_active_level)
        return;
    game_interface = dluma_engine_get_game_interface(&g_dluma.engine);
    if (!game_interface) return;
    level = g_dluma.engine.get_active_level(game_interface);
    if (!level) return;

    list = dluma_runtime_find_visible_list(level, "MenuDLUMAMods.xui");
    dluma_runtime_apply_list_capacity(
        list, &g_dluma_runtime_list_capacity.mods_list,
        DLUMA_RUNTIME_MODS_VISIBLE_ROWS,
        "mods_menu_visible_capacity");

}

static int dluma_runtime_list_capacity_start(void)
{
    static const BYTE set_capacity_prefix[DLUMA_RUNTIME_LIST_PREFIX_SIZE] = {
        0xC6, 0x81, 0x18, 0x01, 0x00, 0x00, 0x00, 0x48,
        0x8B, 0x81, 0x20, 0x01, 0x00, 0x00, 0x45, 0x0F
    };
    DWORD timestamp;
    DWORD image_size;
    if (g_dluma_runtime_list_capacity.timer_id)
        return 1;
    g_dluma_runtime_list_capacity.gamedll =
        GetModuleHandleW(L"gamedll_x64_rwdi.dll");
    if (!g_dluma_runtime_list_capacity.gamedll ||
        !dluma_runtime_ui_pe_identity(
            g_dluma_runtime_list_capacity.gamedll,
            &timestamp, &image_size) ||
        timestamp != DLUMA_RUNTIME_UI_GAMEDLL_TIMESTAMP ||
        image_size != DLUMA_RUNTIME_UI_GAMEDLL_IMAGE_SIZE)
        return 0;
    g_dluma_runtime_list_capacity.set_capacity =
        (DlumaRuntimeListSetCapacityFn)(
            (BYTE *)g_dluma_runtime_list_capacity.gamedll +
            DLUMA_RUNTIME_LIST_SET_CAPACITY_RVA);
    if (!dluma_is_readable(
            (const void *)g_dluma_runtime_list_capacity.set_capacity,
            sizeof(set_capacity_prefix)) ||
        memcmp((const void *)g_dluma_runtime_list_capacity.set_capacity,
               set_capacity_prefix, sizeof(set_capacity_prefix)) != 0)
        return 0;
    g_dluma_runtime_list_capacity.window =
        dluma_engine_get_window(&g_dluma.engine);
    g_dluma_runtime_list_capacity.timer_id = SetTimer(
        g_dluma_runtime_list_capacity.window,
        DLUMA_RUNTIME_CAPACITY_TIMER_ID,
        DLUMA_RUNTIME_CAPACITY_TIMER_MS,
        dluma_runtime_list_capacity_timer);
    if (!g_dluma_runtime_list_capacity.timer_id) {
        memset(&g_dluma_runtime_list_capacity, 0,
               sizeof(g_dluma_runtime_list_capacity));
        return 0;
    }
    g_dluma_runtime_list_capacity.ready = 1;
    dluma_log_line("INFO", "Loader-owned MODS list-capacity adapter started.");
    return 1;
}

static void dluma_runtime_list_capacity_stop(void)
{
    if (g_dluma_runtime_list_capacity.timer_id)
        KillTimer(g_dluma_runtime_list_capacity.window,
                  g_dluma_runtime_list_capacity.timer_id);
    memset(&g_dluma_runtime_list_capacity, 0,
           sizeof(g_dluma_runtime_list_capacity));
}

#endif
