#include "dlw_editor_missing_asset_probe_fix.h"
#include "core/dlw_editor_missing_asset_probe_guard.h"
#include "dlw_dual_host_runtime.h"
#include "dlw_log.h"
#include "dlw_path_security.h"

#define DLUMA_EDITOR_FS_TIMESTAMP 0x623394CDUL
#define DLUMA_EDITOR_PE64_MAGIC 0x020BU
#define DLUMA_EDITOR_FS_IMAGE_SIZE 0x00060000UL
#define DLUMA_EDITOR_FS_SHA256 \
    "C833EBD635572EC9D8D773479FA0DD6E4FA80D9F958A0876C8F07E5F1189B65A"

#define DLUMA_EDITOR_FS_GET_CURRENT_DIRECTORY_IAT_RVA 0x0003E040UL
#define DLUMA_EDITOR_FS_CREATE_FILE_IAT_RVA 0x0003E060UL
#define DLUMA_EDITOR_FS_GET_FILE_ATTRIBUTES_IAT_RVA 0x0003E098UL
#define DLUMA_EDITOR_FS_FIND_FIRST_FILE_EX_IAT_RVA 0x0003E110UL
#define DLUMA_EDITOR_FS_FIND_FIRST_FILE_IAT_RVA 0x0003E228UL

#define DLUMA_EDITOR_DELETE_ACCESS 0x00010000UL
#define DLUMA_EDITOR_GENERIC_WRITE 0x40000000UL
#define DLUMA_EDITOR_GENERIC_ALL 0x10000000UL
#define DLUMA_EDITOR_FILE_FLAG_DELETE_ON_CLOSE 0x04000000UL

typedef DWORD (WINAPI* DlumaEditorGetCurrentDirectoryAFn)(DWORD, LPSTR);
typedef HANDLE (WINAPI* DlumaEditorCreateFileAFn)(
    LPCSTR, DWORD, DWORD, LPVOID, DWORD, DWORD, HANDLE);
typedef DWORD (WINAPI* DlumaEditorGetFileAttributesAFn)(LPCSTR);
typedef HANDLE (WINAPI* DlumaEditorFindFirstFileExAFn)(
    LPCSTR, int, LPVOID, int, LPVOID, DWORD);
typedef HANDLE (WINAPI* DlumaEditorFindFirstFileAFn)(LPCSTR, LPVOID);
typedef BOOL (WINAPI* DlumaEditorFindCloseFn)(HANDLE);

typedef struct DlumaEditorMissingAssetProbeFixState {
    volatile LONG install_state;
    volatile LONG armed;
    volatile LONG root_state;
    volatile LONG create_file_rejections;
    volatile LONG attribute_rejections;
    volatile LONG enumeration_rejections;
    char guarded_drive_letter;
    DlumaEditorGetCurrentDirectoryAFn get_current_directory;
    DlumaEditorCreateFileAFn original_create_file;
    DlumaEditorGetFileAttributesAFn original_get_file_attributes;
    DlumaEditorFindFirstFileExAFn original_find_first_file_ex;
    DlumaEditorFindFirstFileAFn original_find_first_file;
    DlumaEditorFindCloseFn find_close;
    void** create_file_slot;
    void** get_file_attributes_slot;
    void** find_first_file_ex_slot;
    void** find_first_file_slot;
} DlumaEditorMissingAssetProbeFixState;

static DlumaEditorMissingAssetProbeFixState g_dluma_editor_missing_asset_probe_fix;

static int dluma_editor_probe_root_has_no_dmt(
    DlumaEditorFindFirstFileAFn find_first,
    DlumaEditorFindCloseFn find_close,
    char drive_letter);

static WORD dluma_editor_probe_read_u16(const BYTE* value)
{
    return (WORD)((WORD)value[0] | ((WORD)value[1] << 8));
}

static DWORD dluma_editor_probe_read_u32(const BYTE* value)
{
    return (DWORD)value[0] | ((DWORD)value[1] << 8) |
        ((DWORD)value[2] << 16) | ((DWORD)value[3] << 24);
}

static int dluma_editor_probe_module_identity(
    HMODULE module,
    DWORD* image_size_out)
{
    BYTE* base = (BYTE*)module;
    DWORD nt;
    DWORD timestamp;
    DWORD image_size;
    WCHAR path[DLUMA_MAX_WPATH];
    char sha256[DLUMA_SHA256_HEX_CAPACITY];
    if (!base || !image_size_out || !dluma_is_readable(base, 0x100U) ||
        base[0] != 'M' || base[1] != 'Z')
        return 0;
    nt = dluma_editor_probe_read_u32(base + 0x3CU);
    if (nt > 0x100000UL || !dluma_is_readable(base + nt, 0x108U) ||
        base[nt] != 'P' || base[nt + 1U] != 'E' ||
        dluma_editor_probe_read_u16(base + nt + 24U) != DLUMA_EDITOR_PE64_MAGIC)
        return 0;
    timestamp = dluma_editor_probe_read_u32(base + nt + 8U);
    image_size = dluma_editor_probe_read_u32(base + nt + 0x50U);
    path[0] = 0;
    sha256[0] = 0;
    if (timestamp != DLUMA_EDITOR_FS_TIMESTAMP ||
        image_size != DLUMA_EDITOR_FS_IMAGE_SIZE ||
        !dluma_get_module_path_checked(
            module, path, DLUMA_ARRAY_COUNT(path)) ||
        !dluma_sha256_file_hex(
            path, 16ULL * 1024ULL * 1024ULL, sha256) ||
        dluma_stricmp(sha256, DLUMA_EDITOR_FS_SHA256) != 0)
        return 0;
    *image_size_out = image_size;
    return 1;
}

static void** dluma_editor_probe_exact_iat_slot(
    HMODULE module,
    DWORD image_size,
    DWORD exact_rva,
    FARPROC expected)
{
    BYTE* base = (BYTE*)module;
    void** slot;
    if (!base || !expected || !exact_rva ||
        exact_rva > image_size - sizeof(void*))
        return 0;
    slot = (void**)(base + exact_rva);
    return dluma_is_readable(slot, sizeof(void*)) &&
        *slot == (void*)expected ? slot : 0;
}

static int dluma_editor_probe_replace_iat(
    void** slot,
    void* expected,
    void* replacement)
{
    DWORD old_protect = 0;
    DWORD restored_protect = 0;
    if (!slot || !expected || !replacement ||
        !dluma_is_readable(slot, sizeof(void*)) || *slot != expected ||
        !VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &old_protect))
        return 0;
    if (dluma_atomic_pointer_compare_exchange(
            (PVOID volatile*)slot, replacement, expected) != expected) {
        (void)VirtualProtect(
            slot, sizeof(void*), old_protect, &restored_protect);
        return 0;
    }
    if (!VirtualProtect(
            slot, sizeof(void*), old_protect, &restored_protect)) {
        (void)dluma_atomic_pointer_compare_exchange(
            (PVOID volatile*)slot, expected, replacement);
        (void)VirtualProtect(
            slot, sizeof(void*), old_protect, &restored_protect);
        return 0;
    }
    FlushInstructionCache(GetCurrentProcess(), slot, sizeof(void*));
    return 1;
}

static int dluma_editor_probe_restore_iat(
    void** slot,
    void* hook,
    void* original)
{
    if (!slot || !hook || !original ||
        !dluma_is_readable(slot, sizeof(void*)))
        return 0;
    if (*slot == original) return 1;
    return *slot == hook &&
        dluma_editor_probe_replace_iat(slot, hook, original);
}

static int dluma_editor_probe_should_reject(const char* path)
{
    char current_directory[8];
    DWORD entry_error = GetLastError();
    DWORD size;
    LONG root_state;
    int reject;
    if (dluma_atomic_compare_exchange(
            &g_dluma_editor_missing_asset_probe_fix.armed, 0, 0) != 1 ||
        !path || !g_dluma_editor_missing_asset_probe_fix.get_current_directory)
        return 0;
    current_directory[0] = 0;
    size = g_dluma_editor_missing_asset_probe_fix.get_current_directory(
        DLUMA_ARRAY_COUNT(current_directory), current_directory);
    if (size != 3U ||
        !dluma_editor_missing_asset_probe_is_drive_root(
            current_directory, current_directory[0])) {
        SetLastError(entry_error);
        return 0;
    }
    root_state = dluma_atomic_compare_exchange(
        &g_dluma_editor_missing_asset_probe_fix.root_state, 0, 0);
    if (root_state == 0 &&
        dluma_atomic_compare_exchange(
            &g_dluma_editor_missing_asset_probe_fix.root_state, 1, 0) == 0) {
        int root_safe = dluma_editor_probe_root_has_no_dmt(
            g_dluma_editor_missing_asset_probe_fix.original_find_first_file,
            g_dluma_editor_missing_asset_probe_fix.find_close,
            current_directory[0]);
        if (root_safe)
            g_dluma_editor_missing_asset_probe_fix.guarded_drive_letter =
                current_directory[0];
        dluma_atomic_exchange(
            &g_dluma_editor_missing_asset_probe_fix.root_state,
            root_safe ? 2 : -1);
    }
    root_state = dluma_atomic_compare_exchange(
        &g_dluma_editor_missing_asset_probe_fix.root_state, 0, 0);
    reject = root_state == 2 &&
        dluma_editor_missing_asset_probe_should_reject(
            path, current_directory,
            g_dluma_editor_missing_asset_probe_fix.guarded_drive_letter);
    SetLastError(entry_error);
    return reject;
}

static HANDLE WINAPI dluma_editor_probe_create_file_hook(
    LPCSTR path,
    DWORD desired_access,
    DWORD share_mode,
    LPVOID security_attributes,
    DWORD creation_disposition,
    DWORD flags_and_attributes,
    HANDLE template_file)
{
    if (creation_disposition == OPEN_EXISTING &&
        !(desired_access & (DLUMA_EDITOR_DELETE_ACCESS |
                            DLUMA_EDITOR_GENERIC_WRITE |
                            DLUMA_EDITOR_GENERIC_ALL)) &&
        !(flags_and_attributes & DLUMA_EDITOR_FILE_FLAG_DELETE_ON_CLOSE) &&
        dluma_editor_probe_should_reject(path)) {
        dluma_atomic_increment(
            &g_dluma_editor_missing_asset_probe_fix.create_file_rejections);
        SetLastError(ERROR_FILE_NOT_FOUND);
        return INVALID_HANDLE_VALUE;
    }
    return g_dluma_editor_missing_asset_probe_fix.original_create_file(
        path, desired_access, share_mode, security_attributes,
        creation_disposition, flags_and_attributes, template_file);
}

static DWORD WINAPI dluma_editor_probe_get_file_attributes_hook(LPCSTR path)
{
    if (dluma_editor_probe_should_reject(path)) {
        dluma_atomic_increment(
            &g_dluma_editor_missing_asset_probe_fix.attribute_rejections);
        SetLastError(ERROR_FILE_NOT_FOUND);
        return INVALID_FILE_ATTRIBUTES;
    }
    return g_dluma_editor_missing_asset_probe_fix.original_get_file_attributes(
        path);
}

static HANDLE WINAPI dluma_editor_probe_find_first_file_ex_hook(
    LPCSTR path,
    int info_level,
    LPVOID find_data,
    int search_operation,
    LPVOID search_filter,
    DWORD additional_flags)
{
    if (dluma_editor_probe_should_reject(path)) {
        dluma_atomic_increment(
            &g_dluma_editor_missing_asset_probe_fix.enumeration_rejections);
        SetLastError(ERROR_FILE_NOT_FOUND);
        return INVALID_HANDLE_VALUE;
    }
    return g_dluma_editor_missing_asset_probe_fix.original_find_first_file_ex(
        path, info_level, find_data, search_operation, search_filter,
        additional_flags);
}

static HANDLE WINAPI dluma_editor_probe_find_first_file_hook(
    LPCSTR path,
    LPVOID find_data)
{
    if (dluma_editor_probe_should_reject(path)) {
        dluma_atomic_increment(
            &g_dluma_editor_missing_asset_probe_fix.enumeration_rejections);
        SetLastError(ERROR_FILE_NOT_FOUND);
        return INVALID_HANDLE_VALUE;
    }
    return g_dluma_editor_missing_asset_probe_fix.original_find_first_file(
        path, find_data);
}

static int dluma_editor_probe_root_has_no_dmt(
    DlumaEditorFindFirstFileAFn find_first,
    DlumaEditorFindCloseFn find_close,
    char drive_letter)
{
    char pattern[9];
    ULONGLONG find_data[128];
    HANDLE search;
    DWORD error;
    if (!find_first || !find_close || !drive_letter) return 0;
    pattern[0] = drive_letter;
    pattern[1] = ':';
    pattern[2] = '\\';
    pattern[3] = '*';
    pattern[4] = '.';
    pattern[5] = 'd';
    pattern[6] = 'm';
    pattern[7] = 't';
    pattern[8] = 0;
    memset(find_data, 0, sizeof(find_data));
    search = find_first(pattern, find_data);
    if (search != INVALID_HANDLE_VALUE) {
        (void)find_close(search);
        return 0;
    }
    error = GetLastError();
    return error == ERROR_FILE_NOT_FOUND;
}

int dluma_editor_missing_asset_probe_fix_start(void)
{
    HMODULE kernel;
    HMODULE filesystem;
    DWORD image_size = 0;
    FARPROC get_current_directory;
    FARPROC create_file;
    FARPROC get_file_attributes;
    FARPROC find_first_file_ex;
    FARPROC find_first_file;
    DlumaEditorFindCloseFn find_close;
    unsigned int installed_count = 0;
    LONG state;
    if (!dluma_dual_host_runtime_is_editor()) return 0;
    state = dluma_atomic_compare_exchange(
        &g_dluma_editor_missing_asset_probe_fix.install_state, 1, 0);
    if (state != 0) return state == 2;
    kernel = GetModuleHandleW(L"kernel32.dll");
    filesystem = GetModuleHandleW(L"filesystem_x64_rwdi.dll");
    if (!kernel || !filesystem ||
        !dluma_editor_probe_module_identity(filesystem, &image_size))
        goto failed;
    get_current_directory = GetProcAddress(kernel, "GetCurrentDirectoryA");
    create_file = GetProcAddress(kernel, "CreateFileA");
    get_file_attributes = GetProcAddress(kernel, "GetFileAttributesA");
    find_first_file_ex = GetProcAddress(kernel, "FindFirstFileExA");
    find_first_file = GetProcAddress(kernel, "FindFirstFileA");
    find_close = (DlumaEditorFindCloseFn)GetProcAddress(kernel, "FindClose");
    if (!get_current_directory || !create_file || !get_file_attributes ||
        !find_first_file_ex || !find_first_file || !find_close)
        goto failed;
    g_dluma_editor_missing_asset_probe_fix.get_current_directory =
        (DlumaEditorGetCurrentDirectoryAFn)get_current_directory;
    g_dluma_editor_missing_asset_probe_fix.original_create_file =
        (DlumaEditorCreateFileAFn)create_file;
    g_dluma_editor_missing_asset_probe_fix.original_get_file_attributes =
        (DlumaEditorGetFileAttributesAFn)get_file_attributes;
    g_dluma_editor_missing_asset_probe_fix.original_find_first_file_ex =
        (DlumaEditorFindFirstFileExAFn)find_first_file_ex;
    g_dluma_editor_missing_asset_probe_fix.original_find_first_file =
        (DlumaEditorFindFirstFileAFn)find_first_file;
    g_dluma_editor_missing_asset_probe_fix.find_close = find_close;
    g_dluma_editor_missing_asset_probe_fix.create_file_slot =
        dluma_editor_probe_exact_iat_slot(
            filesystem, image_size,
            DLUMA_EDITOR_FS_CREATE_FILE_IAT_RVA, create_file);
    g_dluma_editor_missing_asset_probe_fix.get_file_attributes_slot =
        dluma_editor_probe_exact_iat_slot(
            filesystem, image_size,
            DLUMA_EDITOR_FS_GET_FILE_ATTRIBUTES_IAT_RVA,
            get_file_attributes);
    g_dluma_editor_missing_asset_probe_fix.find_first_file_ex_slot =
        dluma_editor_probe_exact_iat_slot(
            filesystem, image_size,
            DLUMA_EDITOR_FS_FIND_FIRST_FILE_EX_IAT_RVA,
            find_first_file_ex);
    g_dluma_editor_missing_asset_probe_fix.find_first_file_slot =
        dluma_editor_probe_exact_iat_slot(
            filesystem, image_size,
            DLUMA_EDITOR_FS_FIND_FIRST_FILE_IAT_RVA, find_first_file);
    if (!dluma_editor_probe_exact_iat_slot(
            filesystem, image_size,
            DLUMA_EDITOR_FS_GET_CURRENT_DIRECTORY_IAT_RVA,
            get_current_directory) ||
        !g_dluma_editor_missing_asset_probe_fix.create_file_slot ||
        !g_dluma_editor_missing_asset_probe_fix.get_file_attributes_slot ||
        !g_dluma_editor_missing_asset_probe_fix.find_first_file_ex_slot ||
        !g_dluma_editor_missing_asset_probe_fix.find_first_file_slot)
        goto failed;
    if (!dluma_editor_probe_replace_iat(
            g_dluma_editor_missing_asset_probe_fix.create_file_slot,
            create_file, (void*)dluma_editor_probe_create_file_hook))
        goto failed;
    ++installed_count;
    if (!dluma_editor_probe_replace_iat(
            g_dluma_editor_missing_asset_probe_fix.get_file_attributes_slot,
            get_file_attributes,
            (void*)dluma_editor_probe_get_file_attributes_hook))
        goto failed;
    ++installed_count;
    if (!dluma_editor_probe_replace_iat(
            g_dluma_editor_missing_asset_probe_fix.find_first_file_ex_slot,
            find_first_file_ex,
            (void*)dluma_editor_probe_find_first_file_ex_hook))
        goto failed;
    ++installed_count;
    if (!dluma_editor_probe_replace_iat(
            g_dluma_editor_missing_asset_probe_fix.find_first_file_slot,
            find_first_file,
            (void*)dluma_editor_probe_find_first_file_hook))
        goto failed;
    ++installed_count;
    dluma_atomic_exchange(&g_dluma_editor_missing_asset_probe_fix.armed, 1);
    dluma_atomic_exchange(
        &g_dluma_editor_missing_asset_probe_fix.install_state, 2);
    dluma_log_line(
        "INFO",
        "Editor missing-asset root-probe guard installed; nonexistent flat root .dmt reads are rejected before Win32 filesystem I/O.");
    return 1;
failed:
    dluma_atomic_exchange(&g_dluma_editor_missing_asset_probe_fix.armed, 0);
    if (installed_count >= 4U)
        (void)dluma_editor_probe_restore_iat(
            g_dluma_editor_missing_asset_probe_fix.find_first_file_slot,
            (void*)dluma_editor_probe_find_first_file_hook,
            (void*)g_dluma_editor_missing_asset_probe_fix.original_find_first_file);
    if (installed_count >= 3U)
        (void)dluma_editor_probe_restore_iat(
            g_dluma_editor_missing_asset_probe_fix.find_first_file_ex_slot,
            (void*)dluma_editor_probe_find_first_file_ex_hook,
            (void*)g_dluma_editor_missing_asset_probe_fix.original_find_first_file_ex);
    if (installed_count >= 2U)
        (void)dluma_editor_probe_restore_iat(
            g_dluma_editor_missing_asset_probe_fix.get_file_attributes_slot,
            (void*)dluma_editor_probe_get_file_attributes_hook,
            (void*)g_dluma_editor_missing_asset_probe_fix.original_get_file_attributes);
    if (installed_count >= 1U)
        (void)dluma_editor_probe_restore_iat(
            g_dluma_editor_missing_asset_probe_fix.create_file_slot,
            (void*)dluma_editor_probe_create_file_hook,
            (void*)g_dluma_editor_missing_asset_probe_fix.original_create_file);
    dluma_atomic_exchange(
        &g_dluma_editor_missing_asset_probe_fix.install_state, -1);
    dluma_log_line(
        "INFO",
        "Editor missing-asset root-probe guard was not applicable; stock filesystem behavior remains unchanged.");
    return 0;
}

int dluma_editor_missing_asset_probe_fix_snapshot(
    DlumaEditorMissingAssetProbeFixSnapshot* output)
{
    if (!output) return 0;
    memset(output, 0, sizeof(*output));
    output->installed = dluma_atomic_compare_exchange(
        &g_dluma_editor_missing_asset_probe_fix.install_state, 0, 0) == 2 &&
        dluma_atomic_compare_exchange(
            &g_dluma_editor_missing_asset_probe_fix.armed, 0, 0) == 1;
    output->guarded_drive_letter =
        g_dluma_editor_missing_asset_probe_fix.guarded_drive_letter;
    output->create_file_rejections = (ULONGLONG)(unsigned long)
        dluma_atomic_compare_exchange(
            &g_dluma_editor_missing_asset_probe_fix.create_file_rejections,
            0, 0);
    output->attribute_rejections = (ULONGLONG)(unsigned long)
        dluma_atomic_compare_exchange(
            &g_dluma_editor_missing_asset_probe_fix.attribute_rejections,
            0, 0);
    output->enumeration_rejections = (ULONGLONG)(unsigned long)
        dluma_atomic_compare_exchange(
            &g_dluma_editor_missing_asset_probe_fix.enumeration_rejections,
            0, 0);
    return 1;
}
