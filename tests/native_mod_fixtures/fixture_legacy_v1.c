#include <windows.h>

#define DLUMA_NATIVE_MOD_ABI_V1 1U

typedef struct DlumaLegacyNativeModInfoV1 {
    unsigned int size;
    unsigned int abi_version;
    const char *id;
    const char *name;
    const char *version;
    unsigned int flags;
} DlumaLegacyNativeModInfoV1;

typedef struct DlumaLegacyNativeHostV1 {
    unsigned int size;
    unsigned int abi_version;
    const char *loader_version;
    HMODULE original_gamedll;
    void (__cdecl *log_utf8)(const char *level, const char *message);
} DlumaLegacyNativeHostV1;

static const DlumaLegacyNativeModInfoV1 g_info = {
    sizeof(g_info), DLUMA_NATIVE_MOD_ABI_V1,
    "test.fixture.legacy-v1", "Legacy ABI V1 Fixture", "1.0.0", 0U
};
static unsigned int g_callback_calls;

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
    (void)instance;
    (void)reason;
    (void)reserved;
    return TRUE;
}

__declspec(dllexport) const DlumaLegacyNativeModInfoV1 *__cdecl
DLUMANativeMod_QueryV1(void)
{
    ++g_callback_calls;
    return &g_info;
}

__declspec(dllexport) int __cdecl
DLUMANativeMod_PreGameInitV1(const DlumaLegacyNativeHostV1 *host)
{
    (void)host;
    ++g_callback_calls;
    return 1;
}

__declspec(dllexport) int __cdecl
DLUMANativeMod_PostGameInitV1(
    const DlumaLegacyNativeHostV1 *host,
    void *manager)
{
    (void)host;
    (void)manager;
    ++g_callback_calls;
    return 1;
}

__declspec(dllexport) void __cdecl
DLUMANativeMod_ShutdownV1(const DlumaLegacyNativeHostV1 *host)
{
    (void)host;
    ++g_callback_calls;
}

__declspec(dllexport) unsigned int __cdecl
FixtureLegacyV1GetCallbackCount(void)
{
    return g_callback_calls;
}
