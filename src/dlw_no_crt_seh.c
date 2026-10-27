#include "dlw_common.h"

__declspec(dllimport) BOOL WINAPI TerminateProcess(
    HANDLE process, DWORD exit_code);

/* libvcruntime supplies the Windows x64 SEH personality routine used by the
 * guarded third-party callback boundary.  The loader is deliberately linked
 * with /NODEFAULTLIB, so provide the handful of C allocation/termination
 * hooks that personality code references without introducing a CRT DLL
 * import.  They are not a general CRT replacement. */
void *__cdecl calloc(size_t count, size_t size)
{
    SIZE_T bytes;
    if (count && size > ((SIZE_T)-1) / count)
        return 0;
    bytes = (SIZE_T)(count * size);
    if (!bytes)
        bytes = 1;
    {
        void *memory = HeapAlloc(GetProcessHeap(), 0, bytes);
        if (memory)
            memset(memory, 0, bytes);
        return memory;
    }
}

void __cdecl free(void *memory)
{
    if (memory)
        (void)HeapFree(GetProcessHeap(), 0, memory);
}

__declspec(noreturn) void __cdecl abort(void)
{
    (void)TerminateProcess(GetCurrentProcess(), 0xE00000D1UL);
    for (;;)
        Sleep(1000);
}

__declspec(noreturn) void __cdecl terminate(void)
{
    abort();
}
