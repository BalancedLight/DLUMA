#include "dlw_path_security.h"

int dluma_get_final_path_checked(
    HANDLE file, WCHAR* out, unsigned int capacity)
{
    DWORD length;
    if (!file || file == INVALID_HANDLE_VALUE || !out || !capacity)
        return 0;
    out[0] = 0;
    length = GetFinalPathNameByHandleW(file, out, capacity, 0);
    if (!length || length >= capacity) {
        out[0] = 0;
        return 0;
    }
    return 1;
}

int dluma_get_module_path_checked(
    HMODULE module, WCHAR* out, unsigned int capacity)
{
    DWORD length;
    if (!out || !capacity) return 0;
    out[0] = 0;
    length = GetModuleFileNameW(module, out, capacity);
    if (!length || length >= capacity) {
        out[0] = 0;
        return 0;
    }
    return 1;
}
