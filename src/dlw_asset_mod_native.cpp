#define _HAS_EXCEPTIONS 0
#include <cstdint>
#include <cstring>
#include <intrin.h>
extern "C" {
#include "dlw_asset_mod_native.h"
#include "dlw_asset_mod_runtime.h"
#include "dlw_path_security.h"
}

#pragma intrinsic(_InterlockedCompareExchange128)

namespace {
using OpenFn = void* (__fastcall*)(const char*, int, unsigned int);
using NativeOpenFn = void* (__fastcall*)(void*, const char*, int, unsigned int);
using FullPathFn = void* (__fastcall*)(void*, const char*);
using AddSourceFn = unsigned char (__cdecl*)(const char*, unsigned int);
using FindSourceFn = void* (__cdecl*)(const char*);
using RefreshSourceFn = void (__cdecl*)(const char*);

struct EntryHook {
    BYTE* target;
    BYTE* relay;
    unsigned int stolen;
    BYTE original[20];
    BYTE patch[16];
    bool installed;
};

EntryHook g_public, g_native, g_full_path;
OpenFn g_open;
NativeOpenFn g_open_native;
FullPathFn g_full;
AddSourceFn g_add_source;
FindSourceFn g_find_source;
RefreshSourceFn g_refresh_source;
volatile LONG g_active_calls, g_stopping, g_ready;
bool g_adapter_installed;

class Call {
public:
    Call() noexcept { dluma_atomic_increment(&g_active_calls); }
    ~Call() { dluma_atomic_decrement(&g_active_calls); }
};

void Jump(BYTE* output, const void* destination)
{
    const std::uintptr_t address = reinterpret_cast<std::uintptr_t>(destination);
    output[0] = 0xff; output[1] = 0x25;
    std::memset(output + 2, 0, 4);
    std::memcpy(output + 6, &address, sizeof(address));
}

bool Exchange(EntryHook& hook, const BYTE* expected, const BYTE* desired)
{
    __declspec(align(16)) LONGLONG comparand[2], replacement[2];
    DWORD old_protect = 0, ignored = 0;
    if (!hook.target || (reinterpret_cast<std::uintptr_t>(hook.target) & 15U) ||
        !VirtualProtect(hook.target, 16, PAGE_EXECUTE_READWRITE, &old_protect)) return false;
    std::memcpy(comparand, expected, 16);
    std::memcpy(replacement, desired, 16);
    const bool changed = _InterlockedCompareExchange128(
        reinterpret_cast<volatile LONGLONG*>(hook.target),
        replacement[1], replacement[0], comparand) != 0;
    const bool flushed = !changed || FlushInstructionCache(GetCurrentProcess(), hook.target, 16) != 0;
    const bool protected_again = VirtualProtect(hook.target, 16, old_protect, &ignored) != 0;
    if (changed) hook.installed = desired == hook.patch;
    return changed && flushed && protected_again;
}

bool Prepare(EntryHook& hook, BYTE* target, const BYTE* prefix,
             unsigned int stolen, const void* replacement)
{
    if (!target || stolen < 16 || stolen > sizeof(hook.original) ||
        !dluma_is_readable(target, stolen) || std::memcmp(target, prefix, stolen) != 0) return false;
    hook.target = target;
    hook.stolen = stolen;
    std::memcpy(hook.original, prefix, stolen);
    hook.relay = static_cast<BYTE*>(VirtualAlloc(nullptr, 4096,
        MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    if (!hook.relay) return false;
    std::memcpy(hook.relay, prefix, stolen);
    Jump(hook.relay + stolen, target + stolen);
    DWORD old_protect = 0;
    if (!VirtualProtect(hook.relay, 4096, PAGE_EXECUTE_READ, &old_protect) ||
        !FlushInstructionCache(GetCurrentProcess(), hook.relay, stolen + 14U)) return false;
    std::memset(hook.patch, 0x90, sizeof(hook.patch));
    Jump(hook.patch, replacement);
    return true;
}

bool CopyInput(const char* input, char* output, unsigned int capacity)
{
    if (!input || !output || !capacity || !dluma_is_readable(input, 1)) return false;
    __try {
        for (unsigned int index = 0; index < capacity; ++index) {
            const char value = input[index];
            output[index] = value;
            if (!value) return true;
        }
    } __except (1) {
        output[0] = 0;
        return false;
    }
    output[capacity - 1U] = 0;
    return false;
}

void* __fastcall Open(const char* path, int mode, unsigned int flags)
{
    Call call;
    char input[2048] = {}, alias[2048] = {};
    const bool overridden = mode == 0 && !dluma_atomic_compare_exchange(&g_stopping, 0, 0) &&
        dluma_asset_mod_runtime_has_overrides() &&
        CopyInput(path, input, sizeof(input)) &&
        dluma_asset_mod_runtime_resolve_open(input, alias, sizeof(alias), nullptr, 0, nullptr);
    return g_open(overridden ? alias : path, mode, overridden ? flags & ~1U : flags);
}

void* __fastcall FullPath(void* output, const char* path)
{
    Call call;
    char input[2048] = {}, alias[2048] = {};
    const bool overridden = !dluma_atomic_compare_exchange(&g_stopping, 0, 0) &&
        dluma_asset_mod_runtime_has_overrides() &&
        CopyInput(path, input, sizeof(input)) &&
        dluma_asset_mod_runtime_resolve_open(input, alias, sizeof(alias), nullptr, 0, nullptr);
    return g_full(output, overridden ? alias : path);
}

void* __fastcall NativeOpen(void* source, const char* path, int mode, unsigned int flags)
{
    Call call;
    char input[2048] = {};
    void* asset_source = nullptr;
    if (mode == 0 && (flags == 0 || flags == 0x40U) &&
        !dluma_atomic_compare_exchange(&g_stopping, 0, 0) &&
        dluma_asset_mod_runtime_has_overrides() &&
        CopyInput(path, input, sizeof(input)))
        asset_source = dluma_asset_mod_runtime_source_for_path(input);
    return g_open_native(asset_source ? asset_source : source, path, mode, flags);
}

int Mount(const char* path, unsigned int flags, void** source)
{
    if (!source || !path || !g_add_source || !g_find_source || flags != 8U ||
        dluma_atomic_compare_exchange(&g_stopping, 0, 0)) return 0;
    *source = g_find_source(path);
    if (*source) return 0;
    if (!g_add_source(path, flags)) return 0;
    *source = g_find_source(path);
    return *source != nullptr;
}

int BindInventory() { return 1; }

int RefreshNamespace(const char* path)
{
    if (!path || !g_refresh_source || !g_find_source ||
        dluma_atomic_compare_exchange(&g_stopping, 0, 0)) return 0;
    void* const before = g_find_source(path);
    if (!before) return 0;
    g_refresh_source(path);
    return g_find_source(path) == before;
}

bool ExportAt(HMODULE module, const char* name, unsigned int rva, void** output)
{
    *output = reinterpret_cast<void*>(GetProcAddress(module, name));
    return *output == reinterpret_cast<BYTE*>(module) + rva;
}

bool Restore(EntryHook& hook)
{
    return !hook.installed || Exchange(hook, hook.patch, hook.original);
}
}

extern "C" int dluma_asset_mod_native_start(void)
{
    if (dluma_atomic_compare_exchange(&g_ready, 0, 0)) return 1;
    // Experimental isolation installs its own observing wrappers. Its explicit
    // configuration owns that route; the public route is independent.
    if (g_dluma.config.custom_map_asset_isolation_enabled) return 1;
    HMODULE filesystem = GetModuleHandleW(L"filesystem_x64_rwdi.dll");
    WCHAR path[DLUMA_MAX_WPATH] = {};
    char sha256[DLUMA_SHA256_HEX_CAPACITY] = {};
    if (!filesystem || !dluma_get_module_path_checked(filesystem, path, DLUMA_ARRAY_COUNT(path)) ||
        !dluma_sha256_file_hex(path, 16ULL * 1024ULL * 1024ULL, sha256) ||
        dluma_stricmp(sha256, "cb6ff4b0c9aa62c8a35742d463ae3c34fe803767e8e2ac9b33e4604886b9846d") != 0)
        return 0;
    void *open = nullptr, *native = nullptr, *full = nullptr, *add = nullptr, *find = nullptr, *refresh = nullptr;
    if (!ExportAt(filesystem, "?open@fs@@YAPEAUSFsFile@@PEBDW4TYPE@EFSMode@@W4FLAGS@FFSOpenFlags@@@Z", 0x166b0, &open) ||
        !ExportAt(filesystem, "?open_native@fs@@YAPEAUSFsFile@@PEAVCFSSource@@PEBDW4TYPE@EFSMode@@W4FLAGS@FFSOpenFlags@@@Z", 0x3c80, &native) ||
        !ExportAt(filesystem, "?full_path@fs@@YA?BV?$string_base@D@ttl@@PEBD@Z", 0x14d80, &full) ||
        !ExportAt(filesystem, "?add_source@fs@@YA_NPEBDW4ENUM@FFSAddSourceFlags@@@Z", 0x12dd0, &add) ||
        !ExportAt(filesystem, "?find_source@fs@@YAPEAVCFSSource@@PEBD@Z", 0x14430, &find) ||
        !ExportAt(filesystem, "?refresh@fs@@YAXPEBD@Z", 0x17610, &refresh)) return 0;
    const BYTE public_prefix[] = {0x48,0x89,0x5c,0x24,0x20,0x55,0x56,0x41,0x54,0x48,0x8d,0xac,0x24,0x30,0xff,0xff,0xff};
    const BYTE native_prefix[] = {0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x6c,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x57};
    const BYTE full_prefix[] = {0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x74,0x24,0x10,0x48,0x89,0x7c,0x24,0x18,0x4c,0x89,0x64,0x24,0x20};
    if (!Prepare(g_public, static_cast<BYTE*>(open), public_prefix, sizeof(public_prefix), reinterpret_cast<void*>(&Open)) ||
        !Prepare(g_native, static_cast<BYTE*>(native), native_prefix, sizeof(native_prefix), reinterpret_cast<void*>(&NativeOpen)) ||
        !Prepare(g_full_path, static_cast<BYTE*>(full), full_prefix, sizeof(full_prefix), reinterpret_cast<void*>(&FullPath))) return 0;
    g_open = reinterpret_cast<OpenFn>(g_public.relay);
    g_open_native = reinterpret_cast<NativeOpenFn>(g_native.relay);
    g_full = reinterpret_cast<FullPathFn>(g_full_path.relay);
    g_add_source = reinterpret_cast<AddSourceFn>(add);
    g_find_source = reinterpret_cast<FindSourceFn>(find);
    g_refresh_source = reinterpret_cast<RefreshSourceFn>(refresh);
    HMODULE pinned = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
            reinterpret_cast<LPCWSTR>(reinterpret_cast<std::uintptr_t>(&dluma_asset_mod_native_start)), &pinned))
        return 0;
    dluma_atomic_exchange(&g_stopping, 0);
    if (!Exchange(g_full_path, g_full_path.original, g_full_path.patch) ||
        !Exchange(g_native, g_native.original, g_native.patch) ||
        !Exchange(g_public, g_public.original, g_public.patch)) {
        (void)Restore(g_public); (void)Restore(g_native); (void)Restore(g_full_path);
        return 0;
    }
    const DlumaAssetModNativeAdapter adapter = {&Mount, &BindInventory, &RefreshNamespace};
    dluma_asset_mod_runtime_set_adapter(&adapter);
    g_adapter_installed = true;
    if (!dluma_asset_mod_runtime_apply(1)) {
        dluma_asset_mod_native_stop();
        return 0;
    }
    dluma_atomic_exchange(&g_ready, 1);
    dluma_log_pair("INFO", "asset_mod_native_support", "standalone_filesystem_routes_ready");
    return 1;
}

extern "C" int dluma_asset_mod_native_stop(void)
{
    dluma_atomic_exchange(&g_stopping, 1);
    dluma_atomic_exchange(&g_ready, 0);
    if (g_adapter_installed) {
        const DlumaAssetModNativeAdapter cleared = {};
        dluma_asset_mod_runtime_set_adapter(&cleared);
        g_adapter_installed = false;
    }
    const bool public_restored = Restore(g_public);
    const bool native_restored = Restore(g_native);
    const bool full_restored = Restore(g_full_path);
    const bool restored = public_restored && native_restored && full_restored;
    if (!restored) dluma_log_pair("WARN", "asset_mod_native_stop", "owned_route_restore_failed");
    // Relay code and namespace generations stay allocated until process exit.
    // A native caller may already have entered its original trampoline.
    const ULONGLONG deadline = GetTickCount64() + 2000ULL;
    while (dluma_atomic_compare_exchange(&g_active_calls, 0, 0) && GetTickCount64() < deadline)
        Sleep(1);
    return restored && dluma_atomic_compare_exchange(&g_active_calls, 0, 0) == 0;
}

extern "C" int dluma_asset_mod_native_ready(void)
{
    return dluma_atomic_compare_exchange(&g_ready, 0, 0) != 0;
}
