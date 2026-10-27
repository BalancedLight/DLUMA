#include "dlw_common.h"
#include "dlw_localization.h"
#include "dlw_internal_hook_attestation.h"

/* DevTools engine_x64_rwdi.dll, timestamp 0x623395D0, image size 0xE8D000.
 *
 * The game's initial stock text load completes before this script DLL can
 * reliably observe it. Loader catalogs are therefore applied explicitly after
 * the original game-script initializer returns and the engine interface is
 * bound, but before the frontend is presented.
 *
 * This subsystem also validates and wraps the exact Windows
 * ITools::Localized_FromFile entry point. GameWithMenu::ForceReloadTexts uses
 * that entry point, so later stock text reloads automatically re-apply English
 * plus the selected text locale from the mounted loader PAK. */
#define DLUMA_LOCALIZATION_ENGINE_TIMESTAMP 0x623395D0UL
#define DLUMA_LOCALIZATION_ENGINE_IMAGE_SIZE 0x00E8D000UL
#define DLUMA_GET_LOCALE_ID_RVA 0x00261620UL
#define DLUMA_LOCALIZED_GET_RVA 0x00287280UL
#define DLUMA_LOCALIZED_FROM_FILE_RVA 0x002873B0UL
#define DLUMA_LOCALIZED_FROM_FILE_CORE_RVA 0x006F97C0UL
#define DLUMA_LOCALIZED_FROM_FILE_PATCH_SIZE 16U
#define DLUMA_LOCALIZATION_HOOK_DRAIN_MS 2000U
#define DLUMA_LOCALIZATION_LOOKUP_TOKEN_CAPACITY 192U

static const BYTE g_dluma_get_locale_id_prefix[16] = {
    0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x6C,
    0x24, 0x18, 0x56, 0x48, 0x83, 0xEC, 0x20, 0x48
};
static const BYTE g_dluma_localized_get_prefix[16] = {
    0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B,
    0xDA, 0x49, 0x8B, 0xD0, 0x48, 0x8B, 0xCB, 0xE8
};
static const BYTE g_dluma_localized_from_file_prefix[16] = {
    0x48, 0x8B, 0x02, 0x48, 0x8D, 0x0D, 0x87, 0xFF,
    0x5D, 0x00, 0x41, 0x0F, 0xB6, 0xD0, 0x48, 0x85
};
static const BYTE g_dluma_localized_from_file_core_prefix[16] = {
    0x40, 0x53, 0x56, 0x41, 0x54, 0x48, 0x83, 0xEC,
    0x30, 0x48, 0x8B, 0xD9, 0xB9, 0x90, 0x02, 0x00
};

typedef DlumaLocalizationTtlString *(__fastcall *DlumaGetLocaleIdFn)(
    void *game_interface, DlumaLocalizationTtlString *out);
typedef DlumaLocalizationTtlWideString *(__fastcall *DlumaLocalizedGetFn)(
    void *tools, DlumaLocalizationTtlWideString *out,
    const DlumaLocalizationTtlWideString *key);
typedef unsigned char (__fastcall *DlumaLocalizedFromFileCoreFn)(
    const char *path, unsigned char replace_existing);
typedef void (__cdecl *DlumaEngineFreeFn)(void *memory);

typedef struct DlumaLocalizationState {
    HMODULE engine_module;
    DlumaGetLocaleIdFn get_locale_id;
    DlumaLocalizedGetFn localized_get;
    DlumaLocalizedFromFileCoreFn localized_from_file_core;
    DlumaEngineFreeFn engine_free;
    BYTE *localized_from_file_entry;
    BYTE localized_from_file_original[DLUMA_LOCALIZED_FROM_FILE_PATCH_SIZE];
    char current_locale[8];
    ULONGLONG generation;
    ULONGLONG last_locale_probe_ms;
    volatile LONG hook_active_calls;
    volatile LONG hook_stopping;
    volatile LONG injecting_catalogs;
    unsigned char bound;
    unsigned char initialized;
    unsigned char quiescing;
    unsigned char warned_missing_locale;
    unsigned char hook_installed;
    unsigned char hook_owned;
    unsigned char catalog_verified;
} DlumaLocalizationState;

static DlumaLocalizationState g_dluma_localization;

static unsigned char __fastcall dluma_localized_from_file_hook(
    void *tools, const DlumaLocalizationTtlString *path,
    unsigned char replace_existing);
static int dluma_localization_apply_catalogs(const char *reason,
                                            int publish_generation);
static int dluma_localization_lookup_text(const char *key, char *out,
                                        unsigned int capacity);
static int dluma_localization_copy_text(const char *key, char *out,
                                      unsigned int capacity);

static DWORD dluma_localization_read_u32(const BYTE *address)
{
    return (DWORD)address[0] |
           ((DWORD)address[1] << 8) |
           ((DWORD)address[2] << 16) |
           ((DWORD)address[3] << 24);
}

static int dluma_localization_get_pe_identity(HMODULE module,
                                             DWORD *timestamp,
                                             DWORD *image_size)
{
    BYTE *base = (BYTE *)module;
    DWORD nt_offset;
    if (!base || !dluma_is_readable(base, 0x100) ||
        base[0] != 'M' || base[1] != 'Z')
        return 0;
    nt_offset = dluma_localization_read_u32(base + 0x3C);
    if (nt_offset > 0x100000 ||
        !dluma_is_readable(base + nt_offset, 0x58) ||
        base[nt_offset] != 'P' || base[nt_offset + 1] != 'E' ||
        base[nt_offset + 2] != 0 || base[nt_offset + 3] != 0)
        return 0;
    if (timestamp) *timestamp = dluma_localization_read_u32(base + nt_offset + 8);
    if (image_size) *image_size = dluma_localization_read_u32(base + nt_offset + 0x50);
    return 1;
}

static void dluma_localization_copy_ascii(char *out, unsigned int cap,
                                         const char *value)
{
    unsigned int i = 0;
    if (!out || cap == 0) return;
    if (!value) value = "En";
    while (i + 1 < cap && value[i] && value[i] >= 0x20 && value[i] <= 0x7E) {
        out[i] = value[i];
        ++i;
    }
    out[i] = 0;
}

static int dluma_localization_valid_code(const char *code, char out[8])
{
    static const char *const codes[] = {
        "En", "De", "Fr", "It", "Es", "Ru", "Jp", "Pl", "Nl",
        "Br", "Ko", "Cn", "Tw", "El", "Tr", "Th", "Cs"
    };
    unsigned int i;
    if (!code || !code[0]) {
        dluma_localization_copy_ascii(out, 8, "En");
        return 1;
    }
    for (i = 0; i < DLUMA_ARRAY_COUNT(codes); ++i) {
        if (dluma_stricmp(code, codes[i]) == 0) {
            dluma_localization_copy_ascii(out, 8, codes[i]);
            return 1;
        }
    }
    dluma_localization_copy_ascii(out, 8, "En");
    return 0;
}

static const char *dluma_localization_path_leaf(const char *path)
{
    const char *leaf = path ? path : "";
    const char *cursor = leaf;
    while (*cursor) {
        if (*cursor == '/' || *cursor == '\\') leaf = cursor + 1;
        ++cursor;
    }
    return leaf;
}

static int dluma_localization_is_stock_terminal_text(const char *path)
{
    const char *leaf = dluma_localization_path_leaf(path);
    return dluma_stricmp(leaf, "texts_temp.scr") == 0 ||
           dluma_stricmp(leaf, "texts.scr") == 0;
}

static void dluma_localization_build_hook_patch(
    BYTE patch[DLUMA_LOCALIZED_FROM_FILE_PATCH_SIZE])
{
    void *hook = (void *)dluma_localized_from_file_hook;
    memset(patch, 0x90, DLUMA_LOCALIZED_FROM_FILE_PATCH_SIZE);
    patch[0] = 0x48;
    patch[1] = 0xB8;
    memcpy(patch + 2, &hook, sizeof(hook));
    patch[10] = 0xFF;
    patch[11] = 0xE0;
}

static int dluma_localization_install_hook(void)
{
    BYTE patch[DLUMA_LOCALIZED_FROM_FILE_PATCH_SIZE];
    DWORD old_protect;
    DWORD restored_protect;
    BYTE *entry = g_dluma_localization.localized_from_file_entry;
    if (!entry || g_dluma_localization.hook_installed) return entry != 0;
    if (!dluma_is_readable(entry, DLUMA_LOCALIZED_FROM_FILE_PATCH_SIZE) ||
        memcmp(entry, g_dluma_localized_from_file_prefix,
               sizeof(g_dluma_localized_from_file_prefix)) != 0)
        return 0;
    memcpy(g_dluma_localization.localized_from_file_original, entry,
           DLUMA_LOCALIZED_FROM_FILE_PATCH_SIZE);
    dluma_localization_build_hook_patch(patch);
    if (!VirtualProtect(entry, sizeof(patch), PAGE_EXECUTE_READWRITE,
                        &old_protect))
        return 0;
    memcpy(entry, patch, sizeof(patch));
    if (!FlushInstructionCache(
            GetCurrentProcess(), entry, sizeof(patch)) ||
        !VirtualProtect(entry, sizeof(patch), old_protect,
                        &restored_protect)) {
        memcpy(entry, g_dluma_localization.localized_from_file_original,
               sizeof(patch));
        (void)FlushInstructionCache(
            GetCurrentProcess(), entry, sizeof(patch));
        (void)VirtualProtect(
            entry, sizeof(patch), old_protect, &restored_protect);
        return 0;
    }
    g_dluma_localization.hook_owned = 1;
    g_dluma_localization.hook_installed = 1;
    dluma_log_line("INFO", "Localized_FromFile bridge installed for future stock text reloads.");
    return 1;
}

static int dluma_localization_restore_hook(void)
{
    BYTE expected[DLUMA_LOCALIZED_FROM_FILE_PATCH_SIZE];
    BYTE *entry = g_dluma_localization.localized_from_file_entry;
    DWORD old_protect;
    DWORD restored_protect;
    ULONGLONG deadline;
    if (!g_dluma_localization.hook_installed || !g_dluma_localization.hook_owned)
        return 1;
    deadline = GetTickCount64() + DLUMA_LOCALIZATION_HOOK_DRAIN_MS;
    while (dluma_atomic_compare_exchange(
               &g_dluma_localization.hook_active_calls, 0, 0) != 0 &&
           GetTickCount64() < deadline)
        Sleep(1);
    if (dluma_atomic_compare_exchange(
            &g_dluma_localization.hook_active_calls, 0, 0) != 0) {
        dluma_log_line("WARN", "Localization hook remained active during shutdown; keeping the pass-through bridge installed until process exit.");
        return 0;
    }
    dluma_localization_build_hook_patch(expected);
    if (!entry || !dluma_is_readable(entry, sizeof(expected)) ||
        memcmp(entry, expected, sizeof(expected)) != 0) {
        dluma_log_line("WARN", "Localization hook ownership changed; the loader did not overwrite another patch during shutdown.");
        return 0;
    }
    if (!VirtualProtect(entry, sizeof(expected), PAGE_EXECUTE_READWRITE,
                        &old_protect))
        return 0;
    memcpy(entry, g_dluma_localization.localized_from_file_original,
           sizeof(expected));
    if (!FlushInstructionCache(
            GetCurrentProcess(), entry, sizeof(expected)) ||
        !VirtualProtect(entry, sizeof(expected), old_protect,
                        &restored_protect)) {
        memcpy(entry, expected, sizeof(expected));
        (void)FlushInstructionCache(
            GetCurrentProcess(), entry, sizeof(expected));
        (void)VirtualProtect(
            entry, sizeof(expected), old_protect, &restored_protect);
        dluma_log_line("WARN", "Localization hook page protection could not be restored; retaining the complete bridge until process exit.");
        return 0;
    }
    g_dluma_localization.hook_installed = 0;
    g_dluma_localization.hook_owned = 0;
    dluma_log_line("INFO", "Localized_FromFile stock-load bridge restored.");
    return 1;
}

static unsigned char dluma_localization_call_core_from_file(
    const char *path, unsigned char replace_existing)
{
    unsigned char loaded;
    if (!g_dluma_localization.localized_from_file_core || !path) return 0;
    loaded = g_dluma_localization.localized_from_file_core(
        path, replace_existing);
    dluma_log_pair("INFO", "localization_catalog_path", path);
    dluma_log_pair("INFO", "localization_catalog_replace_existing",
                 replace_existing ? "true" : "false");
    dluma_log_pair("INFO", "localization_catalog_result",
                 loaded ? "loaded" : "rejected");
    return loaded;
}

static int dluma_localization_query_locale(char out[8])
{
    DlumaLocalizationTtlString locale;
    void *game_interface;
    int valid;
    locale.data = 0;
    locale.size = 0;
    locale.capacity = 0;
    game_interface = dluma_engine_get_game_interface_for_localization(&g_dluma.engine);
    if (!game_interface || !g_dluma_localization.get_locale_id) {
        dluma_localization_copy_ascii(out, 8, "En");
        return 0;
    }
    g_dluma_localization.get_locale_id(game_interface, &locale);
    valid = dluma_localization_valid_code(locale.data, out);
    if (locale.data && g_dluma_localization.engine_free)
        g_dluma_localization.engine_free((void *)locale.data);
    return valid;
}

int dluma_localization_capture_native_locale(char out[8])
{
    BYTE *base = (BYTE *)GetModuleHandleW(L"engine_x64_rwdi.dll");
    HMODULE crt = GetModuleHandleW(L"MSVCR100.dll");
    DWORD timestamp = 0, image_size = 0;
    DlumaLocalizationTtlString locale = {0};
    DlumaEngineFreeFn engine_free;
    void *game_interface;
    int valid;
    if (!out) return 0;
    out[0] = 0;
    if (!base || !crt ||
        !dluma_localization_get_pe_identity(base, &timestamp, &image_size) ||
        timestamp != DLUMA_LOCALIZATION_ENGINE_TIMESTAMP ||
        image_size != DLUMA_LOCALIZATION_ENGINE_IMAGE_SIZE ||
        !dluma_is_readable(base + DLUMA_GET_LOCALE_ID_RVA, 16) ||
        memcmp(base + DLUMA_GET_LOCALE_ID_RVA, g_dluma_get_locale_id_prefix, 16))
        return 0;
    engine_free = (DlumaEngineFreeFn)GetProcAddress(crt, "free");
    game_interface = dluma_engine_get_game_interface_for_localization(&g_dluma.engine);
    if (!engine_free || !game_interface) return 0;
    ((DlumaGetLocaleIdFn)(base + DLUMA_GET_LOCALE_ID_RVA))(game_interface, &locale);
    valid = locale.data && locale.size && locale.size < 8 &&
        locale.capacity >= locale.size &&
        dluma_is_readable(locale.data, locale.size + 1) &&
        locale.data[locale.size] == 0 &&
        dluma_localization_valid_code(locale.data, out);
    if (locale.data) engine_free((void *)locale.data);
    if (!valid) out[0] = 0;
    return valid;
}

static int dluma_localization_core_entry_qualified(const BYTE *base)
{
    DlumaInternalFunctionDetourEvidenceV1 evidence;
    const BYTE *target = base + DLUMA_LOCALIZED_FROM_FILE_CORE_RVA;
    if (!dluma_is_readable(target, 16)) return 0;
    if (!memcmp(target, g_dluma_localized_from_file_core_prefix, 16)) return 1;
    memset(&evidence, 0, sizeof(evidence));
    return dluma_custom_map_asset_isolation_snapshot_text_from_file_detour(
        target, DLUMA_LOCALIZED_FROM_FILE_CORE_RVA,
        g_dluma_localized_from_file_core_prefix, 16, &evidence) &&
        dluma_internal_function_detour_evidence_matches(&evidence);
}

static int dluma_localization_load_catalog(
    const char *code, unsigned char replace_existing)
{
    char path[96];
    unsigned int i = 0;
    const char *prefix = "data/locale/";
    const char *suffix = "/loader_texts_all.scr";
    while (prefix[i] && i + 1 < sizeof(path)) { path[i] = prefix[i]; ++i; }
    {
        unsigned int j = 0;
        while (code[j] && i + 1 < sizeof(path)) { path[i++] = code[j++]; }
    }
    {
        unsigned int j = 0;
        while (suffix[j] && i + 1 < sizeof(path)) { path[i++] = suffix[j++]; }
    }
    path[i] = 0;
    return dluma_localization_call_core_from_file(
        path, replace_existing) ? 1 : 0;
}

static int dluma_localization_build_lookup_token(
    const char *key, DlumaLocalizationTtlWideString *token,
    WCHAR *storage, unsigned int capacity)
{
    static const char prefix[] = "DLUMA_";
    const unsigned int prefix_length = (unsigned int)sizeof(prefix) - 1U;
    unsigned int length = 0;
    if (!key || !token || !storage ||
        capacity < prefix_length + 4U)
        return 0;
    for (length = 0; length < prefix_length; ++length)
        if (key[length] != prefix[length])
            return 0;
    length = 0;
    storage[0] = L'&';
    while (key[length]) {
        unsigned char c = (unsigned char)key[length];
        if (length >= capacity - 3U ||
            c < 0x21U || c > 0x7EU || c == '&')
            return 0;
        storage[length + 1U] = (WCHAR)c;
        ++length;
    }
    if (length <= prefix_length)
        return 0;
    storage[length + 1U] = L'&';
    storage[length + 2U] = 0;
    token->data = storage;
    token->size = length + 2U;
    token->capacity = token->size;
    return 1;
}

static int dluma_localization_value_is_missing_glyphs(const char *value)
{
    const unsigned char *cursor = (const unsigned char *)value;
    unsigned int glyphs = 0;
    if (!cursor) return 0;
    while (*cursor) {
        if (*cursor == ' ' || *cursor == '\t' || *cursor == '\r' ||
            *cursor == '\n') {
            ++cursor;
            continue;
        }
        /* Chrome's missing-localization path can materialize replacement or
         * hollow-square glyphs rather than returning the original key.  Do
         * not pass a row made only of those glyphs to the UI. */
        if (cursor[0] == 0xEFU && cursor[1] == 0xBFU &&
            cursor[2] == 0xBDU) {
            cursor += 3;
            ++glyphs;
            continue;
        }
        if (cursor[0] == 0xE2U && cursor[1] == 0x96U &&
            (cursor[2] == 0xA0U || cursor[2] == 0xA1U)) {
            cursor += 3;
            ++glyphs;
            continue;
        }
        return 0;
    }
    return glyphs != 0;
}

static int dluma_localization_value_is_unresolved(const char *key,
                                                 const char *value)
{
    char wrapped[DLUMA_MAX_TEXT];
    if (!key || !value || !value[0]) return 1;
    if (dluma_stricmp(key, value) == 0 ||
        dluma_localization_value_is_missing_glyphs(value)) return 1;
    wrapped[0] = 0;
    dluma_str_append(wrapped, DLUMA_ARRAY_COUNT(wrapped), "&");
    dluma_str_append(wrapped, DLUMA_ARRAY_COUNT(wrapped), key);
    dluma_str_append(wrapped, DLUMA_ARRAY_COUNT(wrapped), "&");
    return dluma_stricmp(wrapped, value) == 0;
}

static int dluma_localization_verify_sentinel(void)
{
    char text[128];
    text[0] = 0;
    if (!dluma_localization_lookup_text(
            "DLUMA_Menu_Mods", text, DLUMA_ARRAY_COUNT(text))) {
        dluma_log_pair("ERROR", "localization_lookup_failed",
                     "DLUMA_Menu_Mods");
        g_dluma_localization.catalog_verified = 0;
        return 0;
    }
    if (dluma_localization_value_is_unresolved("DLUMA_Menu_Mods", text)) {
        dluma_log_pair("ERROR", "localization_sentinel_unresolved",
                     text[0] ? text : "empty");
        g_dluma_localization.catalog_verified = 0;
        return 0;
    }
    g_dluma_localization.catalog_verified = 1;
    dluma_log_pair("INFO", "localization_sentinel_DLUMA_Menu_Mods", text);
    return 1;
}

static int dluma_localization_apply_catalogs(const char *reason,
                                            int publish_generation)
{
    char selected[8];
    int english_loaded;
    int selected_loaded = 1;
    int verified;
    if (!g_dluma_localization.bound || g_dluma_localization.quiescing)
        return 0;
    english_loaded = dluma_localization_load_catalog("En", 1);
    dluma_localization_query_locale(selected);
    dluma_log_pair("INFO", "localization_selected_locale", selected);
    if (!english_loaded) {
        dluma_log_line("ERROR", "The English loader localization catalog could not be applied with replacement enabled.");
        return 0;
    }
    if (dluma_stricmp(selected, "En") != 0 &&
        !(selected_loaded = dluma_localization_load_catalog(selected, 1))) {
        if (!g_dluma_localization.warned_missing_locale) {
            g_dluma_localization.warned_missing_locale = 1;
            dluma_log_pair("WARN", "localization_missing_catalog", selected);
        }
        dluma_localization_copy_ascii(selected, 8, "En");
        dluma_log_pair("WARN", "localization_catalog_fallback", "En");
    } else if (selected_loaded) {
        g_dluma_localization.warned_missing_locale = 0;
    }
    verified = dluma_localization_verify_sentinel();
    if (!verified) return 0;
    dluma_localization_copy_ascii(g_dluma_localization.current_locale, 8, selected);
    dluma_log_pair("INFO", "localization_active_locale",
                 g_dluma_localization.current_locale);
    if (publish_generation) {
        if (!g_dluma_localization.generation)
            g_dluma_localization.generation = 1;
        else
            ++g_dluma_localization.generation;
        g_dluma_localization.initialized = 1;
        dluma_crash_localization_refresh();
    }
    dluma_log_pair("INFO", "localization_apply_reason",
                 reason ? reason : "unspecified");
    return 1;
}

static unsigned char __fastcall dluma_localized_from_file_hook(
    void *tools, const DlumaLocalizationTtlString *path,
    unsigned char replace_existing)
{
    const char *raw_path = "";
    unsigned char result = 0;
    (void)tools;
    dluma_atomic_increment(&g_dluma_localization.hook_active_calls);
    if (path && path->data) raw_path = path->data;
    if (g_dluma_localization.localized_from_file_core)
        result = g_dluma_localization.localized_from_file_core(
            raw_path, replace_existing);
    if (result && !g_dluma_localization.hook_stopping &&
        !g_dluma_localization.quiescing &&
        dluma_localization_is_stock_terminal_text(raw_path) &&
        dluma_atomic_compare_exchange(
            &g_dluma_localization.injecting_catalogs, 1, 0) == 0) {
        (void)dluma_localization_apply_catalogs(
            "stock_GameWithMenu_ForceReloadTexts",
            g_dluma_localization.initialized);
        dluma_atomic_exchange(&g_dluma_localization.injecting_catalogs, 0);
    }
    dluma_atomic_decrement(&g_dluma_localization.hook_active_calls);
    return result;
}

int dluma_localization_bind(void)
{
    BYTE *base;
    HMODULE crt;
    DWORD timestamp;
    DWORD image_size;
    if (g_dluma_localization.bound) return 1;
    base = (BYTE *)GetModuleHandleW(L"engine_x64_rwdi.dll");
    if (!base || !dluma_localization_get_pe_identity(base, &timestamp, &image_size) ||
        timestamp != DLUMA_LOCALIZATION_ENGINE_TIMESTAMP ||
        image_size != DLUMA_LOCALIZATION_ENGINE_IMAGE_SIZE)
        return 0;
    if (!dluma_is_readable(base + DLUMA_GET_LOCALE_ID_RVA, 16) ||
        memcmp(base + DLUMA_GET_LOCALE_ID_RVA, g_dluma_get_locale_id_prefix, 16) != 0 ||
        !dluma_is_readable(base + DLUMA_LOCALIZED_GET_RVA, 16) ||
        memcmp(base + DLUMA_LOCALIZED_GET_RVA, g_dluma_localized_get_prefix, 16) != 0 ||
        !dluma_is_readable(base + DLUMA_LOCALIZED_FROM_FILE_RVA, 16) ||
        memcmp(base + DLUMA_LOCALIZED_FROM_FILE_RVA,
               g_dluma_localized_from_file_prefix, 16) != 0 ||
        !dluma_localization_core_entry_qualified(base))
        return 0;
    g_dluma_localization.engine_module = (HMODULE)base;
    g_dluma_localization.get_locale_id =
        (DlumaGetLocaleIdFn)(base + DLUMA_GET_LOCALE_ID_RVA);
    g_dluma_localization.localized_get =
        (DlumaLocalizedGetFn)(base + DLUMA_LOCALIZED_GET_RVA);
    g_dluma_localization.localized_from_file_core =
        (DlumaLocalizedFromFileCoreFn)(base + DLUMA_LOCALIZED_FROM_FILE_CORE_RVA);
    g_dluma_localization.localized_from_file_entry =
        base + DLUMA_LOCALIZED_FROM_FILE_RVA;
    /* The exact DevTools engine imports the VS2010 CRT. Returned ttl strings
     * must be released by that same CRT. */
    crt = GetModuleHandleW(L"MSVCR100.dll");
    if (!crt) crt = GetModuleHandleW(L"ucrtbase.dll");
    if (!crt) crt = GetModuleHandleW(L"msvcrt.dll");
    if (crt)
        g_dluma_localization.engine_free =
            (DlumaEngineFreeFn)GetProcAddress(crt, "free");
    if (!g_dluma_localization.engine_free) return 0;
    g_dluma_localization.quiescing = 0;
    g_dluma_localization.hook_stopping = 0;
    g_dluma_localization.injecting_catalogs = 0;
    g_dluma_localization.catalog_verified = 0;
    g_dluma_localization.bound = 1;
    dluma_localization_copy_ascii(g_dluma_localization.current_locale, 8, "En");
    if (!dluma_localization_install_hook()) {
        g_dluma_localization.bound = 0;
        g_dluma_localization.localized_from_file_core = 0;
        return 0;
    }
    dluma_log_line("INFO", "Game-native localization bindings and stock text-load bridge validated for the exact DevTools engine PE.");
    return 1;
}

int dluma_localization_load_bootstrap(void)
{
    /* Initial stock text loading has already completed before this script DLL
     * can reliably observe it. Only prepare the future-reload bridge here;
     * initial loader catalogs are applied after the original manager and
     * engine interface are available. */
    return g_dluma_localization.bound && g_dluma_localization.hook_installed;
}

int dluma_localization_load_initial(void)
{
    int loaded;
    if (!g_dluma_localization.bound || g_dluma_localization.quiescing) return 0;
    loaded = dluma_localization_apply_catalogs(
        "post_initialize_initial_catalogs", 0);
    if (!loaded) return 0;
    g_dluma_localization.generation = 1;
    g_dluma_localization.initialized = 1;
    dluma_crash_localization_refresh();
    return 1;
}

void dluma_localization_game_thread_tick(void)
{
    char selected[8];
    ULONGLONG now;
    if (!g_dluma_localization.initialized || g_dluma_localization.quiescing) return;
    now = GetTickCount64();
    if (now - g_dluma_localization.last_locale_probe_ms < 1000) return;
    g_dluma_localization.last_locale_probe_ms = now;
    if (!dluma_localization_query_locale(selected) ||
        dluma_stricmp(selected, g_dluma_localization.current_locale) == 0)
        return;
    if (dluma_atomic_compare_exchange(
            &g_dluma_localization.injecting_catalogs, 1, 0) != 0)
        return;
    (void)dluma_localization_apply_catalogs("text_locale_changed", 1);
    dluma_atomic_exchange(&g_dluma_localization.injecting_catalogs, 0);
}

static int dluma_localization_lookup_text(const char *key, char *out,
                                        unsigned int capacity)
{
    WCHAR token_storage[DLUMA_LOCALIZATION_LOOKUP_TOKEN_CAPACITY];
    DlumaLocalizationTtlWideString key_token;
    DlumaLocalizationTtlWideString value;
    char utf8[DLUMA_MAX_TEXT * 4];
    if (!g_dluma_localization.localized_get || !key || !out || capacity == 0)
        return 0;
    out[0] = 0;
    if (!dluma_localization_build_lookup_token(
            key, &key_token, token_storage,
            DLUMA_ARRAY_COUNT(token_storage)))
        return 0;
    value.data = 0;
    value.size = 0;
    value.capacity = 0;
    if (!g_dluma_localization.localized_get(0, &value, &key_token) ||
        !value.data || value.capacity < value.size ||
        !dluma_is_readable(value.data, ((SIZE_T)value.size + 1) * sizeof(WCHAR))) {
        if (value.data && g_dluma_localization.engine_free)
            g_dluma_localization.engine_free((void *)value.data);
        return 0;
    }
    if (!dluma_wide_to_utf8(value.data, utf8, sizeof(utf8))) {
        g_dluma_localization.engine_free((void *)value.data);
        return 0;
    }
    g_dluma_localization.engine_free((void *)value.data);
    dluma_str_copy(out, capacity, utf8);
    return 1;
}

static int dluma_localization_copy_text(const char *key, char *out,
                                      unsigned int capacity)
{
    if (!dluma_localization_lookup_text(key, out, capacity))
        return 0;
    return !dluma_localization_value_is_unresolved(key, out);
}

int dluma_text_get_utf8(const char *key, const char *english_fallback,
                      char *out, unsigned int capacity)
{
    if (!out || capacity == 0) return 0;
    if (!dluma_localization_copy_text(key, out, capacity)) {
        dluma_str_copy(out, capacity, english_fallback ? english_fallback : "");
        return 0;
    }
    return 1;
}

int dluma_text_get_wide(const char *key, const WCHAR *english_fallback,
                      WCHAR *out, unsigned int capacity)
{
    char utf8[DLUMA_MAX_TEXT * 4];
    if (!out || capacity == 0) return 0;
    if (dluma_text_get_utf8(key, 0, utf8, sizeof(utf8)) &&
        dluma_utf8_to_wide(utf8, out, capacity))
        return 1;
    dluma_wstr_copy(out, capacity, english_fallback ? english_fallback : L"");
    return 0;
}

const char *dluma_localization_current_locale(void)
{
    return g_dluma_localization.current_locale[0] ?
        g_dluma_localization.current_locale : "En";
}

ULONGLONG dluma_localization_generation(void)
{
    return g_dluma_localization.generation;
}

void dluma_localization_quiesce(void)
{
    g_dluma_localization.quiescing = 1;
    dluma_atomic_exchange(&g_dluma_localization.hook_stopping, 1);
}

void dluma_localization_shutdown(void)
{
    int restored;
    g_dluma_localization.quiescing = 1;
    dluma_atomic_exchange(&g_dluma_localization.hook_stopping, 1);
    restored = dluma_localization_restore_hook();
    if (!restored) return;
    g_dluma_localization.initialized = 0;
    g_dluma_localization.bound = 0;
    g_dluma_localization.get_locale_id = 0;
    g_dluma_localization.localized_get = 0;
    g_dluma_localization.localized_from_file_core = 0;
    g_dluma_localization.engine_free = 0;
    g_dluma_localization.localized_from_file_entry = 0;
}
