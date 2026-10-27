#include "dlw_common.h"
#include "dlw_chrome_log_core.h"
#include "dlw_path_security.h"

typedef struct DlumaChromeModuleIdentity {
    DWORD timestamp;
    DWORD image_size;
} DlumaChromeModuleIdentity;

static volatile LONG g_dluma_chrome_log_notice_state;

static const WCHAR* dluma_chrome_skip_extended_prefix(const WCHAR* path)
{
    if (path && path[0] == L'\\' && path[1] == L'\\' &&
        path[2] == L'?' && path[3] == L'\\')
        return path + 4;
    return path;
}

static int dluma_chrome_module_final_path(HMODULE module, WCHAR* out,
                                        unsigned int capacity)
{
    WCHAR loaded[DLUMA_MAX_WPATH];
    HANDLE file;
    if (!module || !out || !capacity) return 0;
    out[0] = 0;
    if (!dluma_get_module_path_checked(
            module, loaded, DLUMA_ARRAY_COUNT(loaded))) return 0;
    file = CreateFileW(loaded, 0,
                       FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                       0, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);
    if (file == INVALID_HANDLE_VALUE) return 0;
    if (!dluma_get_final_path_checked(file, out, capacity)) {
        CloseHandle(file);
        return 0;
    }
    CloseHandle(file);
    return 1;
}

static int dluma_chrome_same_directory(const WCHAR* first, const WCHAR* second)
{
    WCHAR first_dir[DLUMA_MAX_WPATH];
    WCHAR second_dir[DLUMA_MAX_WPATH];
    const WCHAR* first_compare;
    const WCHAR* second_compare;
    if (!first || !second || !first[0] || !second[0]) return 0;
    dluma_wstr_copy(first_dir, DLUMA_ARRAY_COUNT(first_dir), first);
    dluma_wstr_copy(second_dir, DLUMA_ARRAY_COUNT(second_dir), second);
    dluma_path_dirname(first_dir);
    dluma_path_dirname(second_dir);
    first_compare = dluma_chrome_skip_extended_prefix(first_dir);
    second_compare = dluma_chrome_skip_extended_prefix(second_dir);
    return first_compare[0] && second_compare[0] &&
           dluma_wstricmp(first_compare, second_compare) == 0;
}

static DWORD dluma_chrome_read_u32(const BYTE* address)
{
    DWORD value = 0;
    if (address && dluma_is_readable(address, sizeof(value)))
        memcpy(&value, address, sizeof(value));
    return value;
}

static int dluma_chrome_get_identity(HMODULE module,
                                   DlumaChromeModuleIdentity* identity)
{
    BYTE* base = (BYTE*)module;
    DWORD pe_offset;
    WORD machine;
    WORD optional_magic;
    if (identity) memset(identity, 0, sizeof(*identity));
    if (!module || !identity || !dluma_is_readable(base, 0x100) ||
        base[0] != 'M' || base[1] != 'Z') return 0;
    pe_offset = dluma_chrome_read_u32(base + 0x3C);
    if (!pe_offset || pe_offset > 0x100000UL ||
        !dluma_is_readable(base + pe_offset, 0x58) ||
        memcmp(base + pe_offset, "PE\0\0", 4) != 0) return 0;
    machine = 0;
    optional_magic = 0;
    memcpy(&machine, base + pe_offset + 4, sizeof(machine));
    memcpy(&optional_magic, base + pe_offset + 24, sizeof(optional_magic));
    if (machine != 0x8664 || optional_magic != 0x20B) return 0;
    identity->timestamp = dluma_chrome_read_u32(base + pe_offset + 8);
    identity->image_size = dluma_chrome_read_u32(base + pe_offset + 24 + 56);
    return identity->timestamp != 0 && identity->image_size != 0;
}

static int dluma_chrome_target_in_image(HMODULE module, FARPROC target,
                                      DWORD image_size, SIZE_T prefix_size)
{
    BYTE* base = (BYTE*)module;
    BYTE* address = (BYTE*)target;
    SIZE_T offset;
    if (!base || !address || !image_size || prefix_size > image_size ||
        address < base) return 0;
    offset = (SIZE_T)(address - base);
    return offset <= (SIZE_T)image_size - prefix_size &&
           dluma_is_readable(address, prefix_size);
}

static int dluma_chrome_log_notice_bind_and_emit(void)
{
    static const char log_export[] =
        "?_CLog@@YAXW4TYPE@ELevel@Log@@PEBD1HW4ENUM@CLFilterAction@@W44CLLineMode@@1ZZ";
    static const char level_export[] =
        "?_CLogLevelFromId@@YA?AW4TYPE@ELevel@Log@@I@Z";
    static const unsigned char log_prefix[] = {
        0x4C, 0x8B, 0xDC, 0x48, 0x83, 0xEC, 0x48
    };
    static const unsigned char level_prefix[] = {
        0x81, 0xF9, 0x52, 0x52, 0x52, 0x45, 0x74, 0x39,
        0x81, 0xF9
    };
    HMODULE filesystem;
    WCHAR filesystem_path[DLUMA_MAX_WPATH];
    WCHAR engine_path[DLUMA_MAX_WPATH];
    WCHAR executable_path[DLUMA_MAX_WPATH];
    DlumaChromeModuleIdentity identity;
    FARPROC log_target;
    FARPROC level_target;
    DlumaChromeLogCoreInput input;
    DlumaChromeLogCoreResult result;
    if (!dluma_chrome_module_final_path(GetModuleHandleW(0), executable_path,
                                      DLUMA_ARRAY_COUNT(executable_path)) ||
        !dluma_chrome_module_final_path(
            GetModuleHandleW(L"engine_x64_rwdi.dll"), engine_path,
            DLUMA_ARRAY_COUNT(engine_path)) ||
        !dluma_chrome_module_final_path(
            GetModuleHandleW(L"filesystem_x64_rwdi.dll"), filesystem_path,
            DLUMA_ARRAY_COUNT(filesystem_path)))
        return 0;
    if (!dluma_chrome_same_directory(filesystem_path, engine_path) ||
        !dluma_chrome_same_directory(filesystem_path, executable_path))
        return 0;
    filesystem = GetModuleHandleW(L"filesystem_x64_rwdi.dll");
    if (!dluma_chrome_get_identity(filesystem, &identity)) return 0;
    log_target = GetProcAddress(filesystem, log_export);
    level_target = GetProcAddress(filesystem, level_export);
    if (!log_target || !level_target ||
        !dluma_chrome_target_in_image(filesystem, log_target,
                                    identity.image_size, sizeof(log_prefix)) ||
        !dluma_chrome_target_in_image(filesystem, level_target,
                                    identity.image_size, sizeof(level_prefix)))
        return 0;
    memset(&input, 0, sizeof(input));
    input.path_verified = 1;
    input.timestamp = identity.timestamp;
    input.image_size = identity.image_size;
    input.log_prefix = (const unsigned char*)log_target;
    input.log_prefix_size = sizeof(log_prefix);
    input.level_prefix = (const unsigned char*)level_target;
    input.level_prefix_size = sizeof(level_prefix);
    input.log = (DlumaChromeLogFn)log_target;
    input.level_from_id = (DlumaChromeLogLevelFromIdFn)level_target;
    if (!dluma_chrome_log_core_emit(&input, &result)) {
        dluma_log_pair("WARN", "chrome_log_notice", "validation_failed");
        return 0;
    }
    return 1;
}

int dluma_chrome_log_notice(void)
{
    LONG state;
    if (dluma_atomic_compare_exchange(
            &g_dluma_chrome_log_notice_state, 1, 0) != 0) {
        state = dluma_atomic_compare_exchange(
            &g_dluma_chrome_log_notice_state, 0, 0);
        return state == 2;
    }
    if (dluma_chrome_log_notice_bind_and_emit()) {
        dluma_atomic_exchange(&g_dluma_chrome_log_notice_state, 2);
        return 1;
    }
    dluma_atomic_exchange(&g_dluma_chrome_log_notice_state, 3);
    return 0;
}
