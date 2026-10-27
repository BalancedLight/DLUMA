/* Offline-only DLL-chain harness.  It loads fixture MODs into this test
 * process, records only harness-owned trace text, and never starts Dying
 * Light, DyingLightPlayer, ChromeEd, or a game-script manager. */
#include <windows.h>
#include "../sdk/c/dluma_api.h"
#include <stdio.h>

#define FAKE_TRACE_CAPACITY 64U
#define FAKE_PATH_CAPACITY 1024U

typedef struct FakeModule {
    const wchar_t *filename;
    int priority;
    HMODULE handle;
    DlumaNativeModGetApiV2Fn get_api;
    DlumaNativeModApiV2 callbacks;
} FakeModule;

static char g_trace[FAKE_TRACE_CAPACITY];
static unsigned int g_trace_count;
static unsigned int g_original_initialize_count;

static void fake_trace_push(char value)
{
    if (g_trace_count + 1U < FAKE_TRACE_CAPACITY)
        g_trace[g_trace_count++] = value;
}

static DlumaStatus DLUMA_API_CALL fake_core_log(
    void *context,
    const DlumaLogRequest *request,
    DlumaCallResult *result)
{
    (void)context;
    if (!request || !result || !request->message.data ||
        request->message.size != 1U)
        return DLUMA_STATUS_INVALID_ARGUMENT;
    fake_trace_push(request->message.data[0]);
    result->call.status = DLUMA_STATUS_OK;
    return DLUMA_STATUS_OK;
}

static int fake_string_equals(DlumaStringView view, const char *expected)
{
    uint64_t index = 0;
    if (!view.data || !expected) return 0;
    while (expected[index]) {
        if (index >= view.size || view.data[index] != expected[index])
            return 0;
        ++index;
    }
    return index == view.size;
}

static DlumaStatus DLUMA_API_CALL fake_get_service(
    void *context,
    DlumaStringView service_id,
    uint32_t minimum_version,
    void *output_table,
    uint32_t output_size)
{
    DlumaCoreV1 table;
    (void)context;
    if (!output_table || minimum_version > 1U)
        return DLUMA_STATUS_INVALID_ARGUMENT;
    if (!fake_string_equals(service_id, DLUMA_SERVICE_CORE_V1))
        return DLUMA_STATUS_NOT_FOUND;
    if (output_size < sizeof(table))
        return DLUMA_STATUS_INSUFFICIENT_BUFFER;
    ZeroMemory(&table, sizeof(table));
    table.struct_size = (uint32_t)sizeof(table);
    table.version = 1U;
    table.log = fake_core_log;
    CopyMemory(output_table, &table, sizeof(table));
    return DLUMA_STATUS_OK;
}

static const DlumaApiV1 *fake_api(void)
{
    static DlumaApiV1 api;
    ZeroMemory(&api, sizeof(api));
    api.struct_size = (uint32_t)sizeof(api);
    api.abi_version = DLUMA_API_ABI_VERSION;
    api.api_major = DLUMA_API_VERSION_MAJOR;
    api.api_minor = DLUMA_API_VERSION_MINOR;
    api.catalog_sha256 = DLUMA_API_CATALOG_SHA256;
    api.get_service = fake_get_service;
    return &api;
}

static void fake_trace_reset(void)
{
    g_trace_count = 0;
    g_trace[0] = 0;
}

static int fake_trace_equals(const char *expected)
{
    unsigned int i;
    for (i = 0; i < g_trace_count && expected[i]; ++i)
        if (g_trace[i] != expected[i]) return 0;
    return i == g_trace_count && expected[i] == 0;
}

static int fake_path_join(wchar_t *out, unsigned int capacity,
                          const wchar_t *root, const wchar_t *filename)
{
    unsigned int i = 0;
    if (!out || !capacity || !root || !filename) return 0;
    while (*root && i + 1U < capacity) out[i++] = *root++;
    if (i && out[i - 1U] != L'\\' && out[i - 1U] != L'/')
    {
        if (i + 1U >= capacity) return 0;
        out[i++] = L'\\';
    }
    while (*filename && i + 1U < capacity) out[i++] = *filename++;
    out[i] = 0;
    return *filename == 0;
}

static int fake_load_v2(FakeModule *module, const wchar_t *root)
{
    wchar_t path[FAKE_PATH_CAPACITY];
    const DlumaNativeModApiV2 *callbacks;
    if (!module || !fake_path_join(path, FAKE_PATH_CAPACITY,
                                   root, module->filename))
        return 0;
    module->handle = LoadLibraryExW(path, 0, 0);
    if (!module->handle) return 0;
    module->get_api = (DlumaNativeModGetApiV2Fn)GetProcAddress(
        module->handle, DLUMA_NATIVE_MOD_GET_API_V2_EXPORT);
    if (!module->get_api) return 0;
    callbacks = module->get_api();
    if (!callbacks || callbacks->struct_size < sizeof(*callbacks) ||
        callbacks->abi_version != DLUMA_NATIVE_MOD_ABI_V2 ||
        !callbacks->pre_game_bootstrap || !callbacks->post_game_ready ||
        !callbacks->scope_activated || !callbacks->scope_deactivated ||
        !callbacks->shutdown || !callbacks->event)
        return 0;
    CopyMemory(&module->callbacks, callbacks, sizeof(module->callbacks));
    return 1;
}

static void fake_unload(FakeModule *module)
{
    if (module && module->handle)
    {
        (void)FreeLibrary(module->handle);
        module->handle = 0;
    }
}

static void fake_sort_by_priority(FakeModule *modules, unsigned int count)
{
    unsigned int i;
    for (i = 1; i < count; ++i)
    {
        FakeModule value = modules[i];
        unsigned int position = i;
        while (position && modules[position - 1U].priority > value.priority)
        {
            modules[position] = modules[position - 1U];
            --position;
        }
        modules[position] = value;
    }
}

static int fake_case_v2_chain_order(const wchar_t *root)
{
    FakeModule modules[2] = {
        {L"fixture_v2_a.mod", 20, 0, 0, {0}},
        {L"fixture_v2_b.mod", 10, 0, 0, {0}}
    };
    DlumaScopeActivationV1 activation;
    const DlumaApiV1 *api = fake_api();
    unsigned int i;
    int result = 0;
    fake_trace_reset();
    g_original_initialize_count = 0;
    ZeroMemory(&activation, sizeof(activation));
    activation.struct_size = (uint32_t)sizeof(activation);
    activation.scope = DLUMA_SCOPE_GLOBAL;
    if (!fake_load_v2(&modules[0], root) || !fake_load_v2(&modules[1], root))
        goto done;
    fake_sort_by_priority(modules, 2U);
    for (i = 0; i < 2U; ++i)
        if (modules[i].callbacks.pre_game_bootstrap(api) != DLUMA_STATUS_OK)
            goto done;
    ++g_original_initialize_count;
    fake_trace_push('G');
    for (i = 0; i < 2U; ++i) {
        if (modules[i].callbacks.post_game_ready(api) != DLUMA_STATUS_OK ||
            modules[i].callbacks.scope_activated(api, &activation) !=
                DLUMA_STATUS_OK)
            goto done;
    }
    fake_trace_push('L');
    for (i = 2U; i; --i) {
        modules[i - 1U].callbacks.scope_deactivated(api, &activation);
        modules[i - 1U].callbacks.shutdown(api, DLUMA_STOP_SHUTDOWN);
    }
    result = g_original_initialize_count == 1U &&
             fake_trace_equals("BAGb3a1L2x4y");
done:
    fake_unload(&modules[0]);
    fake_unload(&modules[1]);
    return result;
}

static int fake_case_failed_callback_unwinds_only_it(const wchar_t *root)
{
    FakeModule module = {L"fixture_v2_fail.mod", 0, 0, 0, {0}};
    const DlumaApiV1 *api = fake_api();
    int result = 0;
    fake_trace_reset();
    if (!fake_load_v2(&module, root)) goto done;
    if (module.callbacks.pre_game_bootstrap(api) == DLUMA_STATUS_OK) goto done;
    module.callbacks.shutdown(api, DLUMA_STOP_RECOVERY);
    result = fake_trace_equals("Ff");
done:
    fake_unload(&module);
    return result;
}

static int fake_case_legacy_is_never_initialized(const wchar_t *root)
{
    typedef unsigned int(__cdecl *LegacyCountFn)(void);
    wchar_t path[FAKE_PATH_CAPACITY];
    HMODULE legacy;
    HMODULE legacy_v1;
    LegacyCountFn count;
    LegacyCountFn count_v1;
    int result = 0;
    if (!fake_path_join(path, FAKE_PATH_CAPACITY, root, L"fixture_legacy.mod"))
        return 0;
    legacy = LoadLibraryExW(path, 0, 0);
    if (!legacy) return 0;
    count = (LegacyCountFn)GetProcAddress(
        legacy, "FixtureLegacyGetInitializeCount");
    result = GetProcAddress(legacy, "InitializeGameScriptDLL") != 0 &&
        GetProcAddress(legacy, "ShutdownGameScriptDLL") != 0 &&
        GetProcAddress(legacy, DLUMA_NATIVE_MOD_GET_API_V2_EXPORT) == 0 &&
        count && count() == 0U;
    (void)FreeLibrary(legacy);
    if (!result || !fake_path_join(
            path, FAKE_PATH_CAPACITY, root, L"fixture_legacy_v1.mod"))
        return 0;
    legacy_v1 = LoadLibraryExW(path, 0, 0);
    if (!legacy_v1) return 0;
    count_v1 = (LegacyCountFn)GetProcAddress(
        legacy_v1, "FixtureLegacyV1GetCallbackCount");
    result = GetProcAddress(legacy_v1, "DLUMANativeMod_QueryV1") != 0 &&
        GetProcAddress(legacy_v1, "DLUMANativeMod_PreGameInitV1") != 0 &&
        GetProcAddress(legacy_v1, "DLUMANativeMod_PostGameInitV1") != 0 &&
        GetProcAddress(legacy_v1, "DLUMANativeMod_ShutdownV1") != 0 &&
        GetProcAddress(legacy_v1, DLUMA_NATIVE_MOD_GET_API_V2_EXPORT) == 0 &&
        count_v1 && count_v1() == 0U;
    (void)FreeLibrary(legacy_v1);
    return result;
}

static unsigned int fake_u32(const unsigned char *bytes)
{
    return (unsigned int)bytes[0] |
           ((unsigned int)bytes[1] << 8) |
           ((unsigned int)bytes[2] << 16) |
           ((unsigned int)bytes[3] << 24);
}

static int fake_is_amd64_dll(const unsigned char *bytes, unsigned int size)
{
    unsigned int nt;
    unsigned int machine;
    unsigned int characteristics;
    if (!bytes || size < 0x60U || bytes[0] != 'M' || bytes[1] != 'Z')
        return 0;
    nt = fake_u32(bytes + 0x3cU);
    if (nt > size || size - nt < 24U || bytes[nt] != 'P' ||
        bytes[nt + 1U] != 'E' || bytes[nt + 2U] || bytes[nt + 3U])
        return 0;
    machine = (unsigned int)bytes[nt + 4U] |
              ((unsigned int)bytes[nt + 5U] << 8);
    characteristics = (unsigned int)bytes[nt + 22U] |
                      ((unsigned int)bytes[nt + 23U] << 8);
    return machine == 0x8664U && (characteristics & 0x2000U) != 0;
}

static int fake_case_rejects_malformed_and_wrong_machine(const wchar_t *root)
{
    HANDLE file;
    wchar_t path[FAKE_PATH_CAPACITY];
    unsigned char bytes[8192];
    DWORD read = 0;
    unsigned int nt;
    int result = 0;
    if (!fake_path_join(path, FAKE_PATH_CAPACITY, root, L"fixture_v2_a.mod"))
        return 0;
    file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, 0,
                       OPEN_EXISTING, 0, 0);
    if (file == INVALID_HANDLE_VALUE) return 0;
    if (!ReadFile(file, bytes, sizeof(bytes), &read, 0) || read < 0x60U)
        goto done;
    if (!fake_is_amd64_dll(bytes, read)) goto done;
    nt = fake_u32(bytes + 0x3cU);
    if (nt > read || read - nt < 24U) goto done;
    bytes[nt + 4U] = 0x4cU;
    bytes[nt + 5U] = 0x01U;
    if (fake_is_amd64_dll(bytes, read)) goto done;
    bytes[0] = 0;
    result = !fake_is_amd64_dll(bytes, read);
done:
    (void)CloseHandle(file);
    return result;
}

int wmain(int argc, wchar_t **argv)
{
    const wchar_t *root;
    int passed = 0;
    if (argc != 2)
    {
        fputs("usage: native_mod_chain_fake_harness <fixture-directory>\n",
              stderr);
        return 2;
    }
    root = argv[1];
    if (!fake_case_v2_chain_order(root))
    {
        fputs("[FAIL] v2_chain_order\n", stderr);
        return 1;
    }
    puts("[PASS] v2_chain_order");
    ++passed;
    if (!fake_case_failed_callback_unwinds_only_it(root))
    {
        fputs("[FAIL] failed_callback_unwinds_only_it\n", stderr);
        return 1;
    }
    puts("[PASS] failed_callback_unwinds_only_it");
    ++passed;
    if (!fake_case_legacy_is_never_initialized(root))
    {
        fputs("[FAIL] legacy_is_never_initialized\n", stderr);
        return 1;
    }
    puts("[PASS] legacy_is_never_initialized");
    ++passed;
    if (!fake_case_rejects_malformed_and_wrong_machine(root))
    {
        fputs("[FAIL] rejects_malformed_and_wrong_machine\n", stderr);
        return 1;
    }
    puts("[PASS] rejects_malformed_and_wrong_machine");
    ++passed;
    printf("native DLL MOD fake-chain cases passed: %d\n", passed);
    return 0;
}
