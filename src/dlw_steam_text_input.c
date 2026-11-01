#include "dlw_common.h"
#include "dlw_steam_text_input.h"
#include <stddef.h>

#define DLUMA_STEAM_TEXT_CAPACITY 8192U
#define DLUMA_STEAM_TEXT_CALLBACK 714

typedef void *(__cdecl *DlumaSteamTextAccessor)(void);
typedef unsigned char (__cdecl *DlumaSteamTextShow)(void *, int, int, const char *, unsigned int, const char *);
typedef unsigned int (__cdecl *DlumaSteamTextLength)(void *);
typedef unsigned char (__cdecl *DlumaSteamTextRead)(void *, char *, unsigned int);
typedef unsigned char (__cdecl *DlumaSteamTextOverlay)(void *);
typedef void (__cdecl *DlumaSteamTextRegister)(void *, int);

typedef struct DlumaSteamTextCallback {
    void **vtable;
    unsigned char flags;
    unsigned char padding[3];
    int callback_id;
} DlumaSteamTextCallback;

typedef char DlumaSteamTextFlagsOffset[offsetof(DlumaSteamTextCallback, flags) == 8 ? 1 : -1];
typedef char DlumaSteamTextIdOffset[offsetof(DlumaSteamTextCallback, callback_id) == 12 ? 1 : -1];

static struct {
    DlumaSteamTextCallback callback;
    DlumaSteamTextShow show;
    DlumaSteamTextLength length;
    DlumaSteamTextRead read;
    DlumaSteamTextOverlay overlay;
    DlumaSteamTextAccessor accessor;
    void *utils;
    volatile LONG state; /* 0 idle, 1 pending, 2 submitted, 3 cancelled, 4 writing, -1 error */
    volatile LONG generation;
    unsigned int capacity;
    unsigned char registered;
    char text[DLUMA_STEAM_TEXT_CAPACITY];
} g_dluma_steam_text;

/* Steam owns the source buffer only during this callback. Copy it here. */
static void __fastcall dluma_steam_text_run(void *self, void *payload)
{
    unsigned int length, end;
    unsigned char submitted;
    LONG result = -1;
    (void)self;
    if (dluma_atomic_compare_exchange(&g_dluma_steam_text.state, 4, 1) != 1)
        return;
    if (!payload || !dluma_is_readable(payload, 8))
        goto done;
    memcpy(&submitted, payload, sizeof(submitted));
    if (!submitted) { result = 3; goto done; }
    length = g_dluma_steam_text.length(g_dluma_steam_text.utils);
    if (length > g_dluma_steam_text.capacity || length > sizeof(g_dluma_steam_text.text))
        goto done;
    memset(g_dluma_steam_text.text, 0, sizeof(g_dluma_steam_text.text));
    if (length && !g_dluma_steam_text.read(g_dluma_steam_text.utils, g_dluma_steam_text.text, length))
        goto done;
    for (end = 0; end < sizeof(g_dluma_steam_text.text) && g_dluma_steam_text.text[end]; ++end) {}
    if (end >= (length ? length : 1) || end >= g_dluma_steam_text.capacity) goto done;
    result = 2;
done:
    dluma_atomic_exchange(&g_dluma_steam_text.state, result);
}

static int __fastcall dluma_steam_text_size(void *self)
{
    (void)self;
    /* Only the original submitted/byte-count prefix is consumed. */
    return 8;
}

static void *g_dluma_steam_text_vtable[] = {
    (void *)dluma_steam_text_run,
    (void *)dluma_steam_text_run,
    (void *)dluma_steam_text_size
};

static int dluma_steam_text_prepare(void)
{
    HMODULE module, pinned;
    DlumaSteamTextRegister register_callback;
    const BYTE *base;
    DWORD pe, timestamp, image_size;
    if (g_dluma_steam_text.registered)
        return 1;
    module = GetModuleHandleW(L"steam_api64.dll");
    base = (const BYTE *)module;
    if (!base || !dluma_is_readable(base, 0x40)) return 0;
    memcpy(&pe, base + 0x3c, sizeof(pe));
    if (pe > 0x100000U || !dluma_is_readable(base + pe, 0x54)) return 0;
    memcpy(&timestamp, base + pe + 8, sizeof(timestamp));
    memcpy(&image_size, base + pe + 0x50, sizeof(image_size));
    if (timestamp != 0x56995865UL || image_size != 0x3d000UL) return 0;
    g_dluma_steam_text.accessor = (DlumaSteamTextAccessor)GetProcAddress(module, "SteamUtils");
    g_dluma_steam_text.show = (DlumaSteamTextShow)GetProcAddress(module, "SteamAPI_ISteamUtils_ShowGamepadTextInput");
    g_dluma_steam_text.length = (DlumaSteamTextLength)GetProcAddress(module, "SteamAPI_ISteamUtils_GetEnteredGamepadTextLength");
    g_dluma_steam_text.read = (DlumaSteamTextRead)GetProcAddress(module, "SteamAPI_ISteamUtils_GetEnteredGamepadTextInput");
    g_dluma_steam_text.overlay = (DlumaSteamTextOverlay)GetProcAddress(module, "SteamAPI_ISteamUtils_IsOverlayEnabled");
    register_callback = (DlumaSteamTextRegister)GetProcAddress(module, "SteamAPI_RegisterCallback");
    if (!g_dluma_steam_text.accessor || !g_dluma_steam_text.show ||
        !g_dluma_steam_text.length || !g_dluma_steam_text.read || !g_dluma_steam_text.overlay ||
        !register_callback) return 0;
    g_dluma_steam_text.utils = g_dluma_steam_text.accessor();
    if (!g_dluma_steam_text.utils) return 0;
    if (!GetModuleHandleExW(5UL, (LPCWSTR)(const void *)&dluma_steam_text_run, &pinned)) return 0;
    /* Retain a passive callback for this process. Cancel only invalidates its
       request; an outstanding overlay must dismiss before another request. */
    g_dluma_steam_text.callback.vtable = g_dluma_steam_text_vtable;
    g_dluma_steam_text.callback.callback_id = DLUMA_STEAM_TEXT_CALLBACK;
    register_callback(&g_dluma_steam_text.callback, DLUMA_STEAM_TEXT_CALLBACK);
    g_dluma_steam_text.registered = 1;
    return 1;
}

int dluma_steam_text_open(const char *initial, unsigned int capacity,
                          const char *title, unsigned int generation)
{
    if (!initial || !title || !generation || capacity < 2 ||
        capacity > DLUMA_STEAM_TEXT_CAPACITY || strlen(initial) >= capacity)
        return 0;
    /* Reap a dismissed request abandoned by a closed editor. */
    if (!dluma_atomic_compare_exchange(&g_dluma_steam_text.generation, 0, 0)) {
        LONG state = dluma_atomic_compare_exchange(&g_dluma_steam_text.state, 0, 0);
        if (state == 2 || state == 3 || state == -1)
            (void)dluma_atomic_compare_exchange(&g_dluma_steam_text.state, 0, state);
    }
    if (dluma_atomic_compare_exchange(&g_dluma_steam_text.state, 4, 0) != 0)
        return 0;
    if (!dluma_steam_text_prepare() || !g_dluma_steam_text.overlay(g_dluma_steam_text.utils)) {
        dluma_atomic_exchange(&g_dluma_steam_text.state, 0);
        return 0;
    }
    g_dluma_steam_text.capacity = capacity;
    dluma_atomic_exchange(&g_dluma_steam_text.generation, (LONG)generation);
    dluma_atomic_exchange(&g_dluma_steam_text.state, 1);
    if (!g_dluma_steam_text.show(g_dluma_steam_text.utils, 0, 0, title, capacity - 1, initial)) {
        dluma_atomic_exchange(&g_dluma_steam_text.state, 0);
        dluma_atomic_exchange(&g_dluma_steam_text.generation, 0);
        return 0;
    }
    return 1;
}

int dluma_steam_text_poll(unsigned int generation, char *out, unsigned int capacity)
{
    LONG state = dluma_atomic_compare_exchange(&g_dluma_steam_text.state, 0, 0);
    int result;
    if (state == 1 || state == 4) return 0;
    if (!state || !out || !capacity) return -1;
    result = state == 2 ? 1 : state == 3 ? 2 : -1;
    if ((unsigned int)g_dluma_steam_text.generation != generation) result = 2;
    if (result == 1) {
        if (strlen(g_dluma_steam_text.text) >= capacity) result = -1;
        else dluma_str_copy(out, capacity, g_dluma_steam_text.text);
    }
    dluma_atomic_exchange(&g_dluma_steam_text.generation, 0);
    dluma_atomic_exchange(&g_dluma_steam_text.state, 0);
    return result;
}

void dluma_steam_text_cancel(unsigned int generation)
{
    (void)dluma_atomic_compare_exchange(&g_dluma_steam_text.generation, 0, (LONG)generation);
}
