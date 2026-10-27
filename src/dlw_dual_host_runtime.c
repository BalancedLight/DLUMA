#include "dlw_dual_host_runtime.h"
#include "dlw_build_identity.h"
#include "dlw_log.h"
#include "dlw_path_security.h"
#include "dlw_project_owner_identity.h"

#define DLUMA_HOST_IMAGE_MAX_BYTES (1024ULL * 1024ULL * 1024ULL)
#define DLUMA_HANDOFF_MAX_FILE_BYTES (2UL * 1024UL * 1024UL)
#define DLUMA_TH32CS_SNAPPROCESS 0x00000002UL
#define DLUMA_PROCESS_QUERY_LIMITED_INFORMATION 0x1000UL
#define DLUMA_STILL_ACTIVE 259UL
#define DLUMA_DELETE_ACCESS 0x00010000UL
#define DLUMA_FILE_DISPOSITION_INFO_CLASS 4

typedef WCHAR* (WINAPI* DlumaGetCommandLineWFn)(void);
typedef HANDLE (WINAPI* DlumaCreateToolhelp32SnapshotFn)(DWORD, DWORD);
typedef BOOL (WINAPI* DlumaProcess32FirstWFn)(HANDLE, void*);
typedef BOOL (WINAPI* DlumaProcess32NextWFn)(HANDLE, void*);
typedef HANDLE (WINAPI* DlumaOpenProcessFn)(DWORD, BOOL, DWORD);
typedef BOOL (WINAPI* DlumaGetExitCodeProcessFn)(HANDLE, DWORD*);
typedef BOOL (WINAPI* DlumaQueryFullProcessImageNameWFn)(
    HANDLE, DWORD, WCHAR*, DWORD*);
typedef BOOL (WINAPI* DlumaSetFileInformationByHandleFn)(
    HANDLE, int, void*, DWORD);

typedef struct DlumaFileDispositionInfo {
    BOOL delete_file;
} DlumaFileDispositionInfo;

typedef struct DlumaProcessEntry32W {
    DWORD dwSize;
    DWORD cntUsage;
    DWORD th32ProcessID;
    ULONG_PTR th32DefaultHeapID;
    DWORD th32ModuleID;
    DWORD cntThreads;
    DWORD th32ParentProcessID;
    LONG pcPriClassBase;
    DWORD dwFlags;
    WCHAR szExeFile[260];
} DlumaProcessEntry32W;

static DlumaDualHostRuntimeSnapshot g_dluma_dual_runtime;
static volatile LONG g_dluma_dual_runtime_state;

static ULONGLONG dluma_host_filetime_u64(const FILETIME* value)
{
    return value ? ((ULONGLONG)value->dwHighDateTime << 32) |
        value->dwLowDateTime : 0ULL;
}

static int dluma_host_sha256_text(const char* value)
{
    unsigned int index;
    if (!value) return 0;
    for (index = 0; index < 64U; ++index) {
        char c = value[index];
        if (!((c >= '0' && c <= '9') ||
              (c >= 'a' && c <= 'f') ||
              (c >= 'A' && c <= 'F')))
            return 0;
    }
    return value[64] == 0;
}

static const WCHAR* dluma_host_skip_extended(const WCHAR* value)
{
    if (value && value[0] == L'\\' && value[1] == L'\\' &&
        value[2] == L'?' && value[3] == L'\\')
        return value + 4;
    return value;
}

static int dluma_host_capture_image(DlumaDualHostRuntimeSnapshot* snapshot)
{
    BYTE disk_header[4096];
    BYTE* loaded = (BYTE*)GetModuleHandleW(0);
    WCHAR lexical[DLUMA_MAX_WPATH];
    WCHAR final_path[DLUMA_MAX_WPATH];
    BY_HANDLE_FILE_INFORMATION file_info;
    LARGE_INTEGER zero;
    DWORD read = 0;
    DWORD loaded_timestamp = 0;
    DWORD loaded_image_size = 0;
    DWORD disk_timestamp = 0;
    DWORD disk_image_size = 0;
    DWORD attributes;
    HANDLE file = INVALID_HANDLE_VALUE;
    int result = 0;
    memset(lexical, 0, sizeof(lexical));
    memset(final_path, 0, sizeof(final_path));
    memset(&file_info, 0, sizeof(file_info));
    zero.QuadPart = 0;
    if (!snapshot || !loaded ||
        !dluma_get_module_path_checked(0, lexical, DLUMA_ARRAY_COUNT(lexical)) ||
        !dluma_build_identity_parse_pe_header(
            loaded, 4096U, &loaded_timestamp, &loaded_image_size))
        return 0;
    attributes = GetFileAttributesW(lexical);
    if (attributes == INVALID_FILE_ATTRIBUTES ||
        (attributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)))
        return 0;
    file = CreateFileW(
        lexical, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE,
        0, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);
    if (file == INVALID_HANDLE_VALUE) return 0;
    if (!dluma_get_final_path_checked(
            file, final_path, DLUMA_ARRAY_COUNT(final_path)) ||
        !GetFileInformationByHandle(file, &file_info) ||
        (file_info.dwFileAttributes &
            (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) ||
        dluma_wstricmp(dluma_host_skip_extended(lexical),
                     dluma_host_skip_extended(final_path)) != 0 ||
        !dluma_sha256_handle_hex(
            file, DLUMA_HOST_IMAGE_MAX_BYTES, snapshot->evidence.image.sha256) ||
        !SetFilePointerEx(file, zero, 0, FILE_BEGIN) ||
        !ReadFile(file, disk_header, sizeof(disk_header), &read, 0) ||
        !dluma_build_identity_parse_pe_header(
            disk_header, read, &disk_timestamp, &disk_image_size) ||
        disk_timestamp != loaded_timestamp ||
        disk_image_size != loaded_image_size)
        goto done;
    snapshot->evidence.image.timestamp = loaded_timestamp;
    snapshot->evidence.image.size_of_image = loaded_image_size;
    if (!dluma_wide_to_utf8(
            final_path, snapshot->evidence.canonical_image_utf8,
            DLUMA_ARRAY_COUNT(snapshot->evidence.canonical_image_utf8)))
        goto done;
    dluma_wstr_copy(snapshot->executable_path,
                  DLUMA_ARRAY_COUNT(snapshot->executable_path), final_path);
    snapshot->evidence.canonical_image_verified = 1U;
    result = 1;
done:
    CloseHandle(file);
    return result;
}

static int dluma_host_capture_command_line(
    DlumaDualHostRuntimeSnapshot* snapshot,
    const WCHAR** full_command_line)
{
    HMODULE kernel = GetModuleHandleW(L"kernel32.dll");
    DlumaGetCommandLineWFn get_command_line = kernel
        ? (DlumaGetCommandLineWFn)GetProcAddress(kernel, "GetCommandLineW") : 0;
    WCHAR* command;
    const WCHAR* arguments;
    int needed;
    char* utf8;
    if (!snapshot || !full_command_line || !get_command_line) return 0;
    command = get_command_line();
    if (!command || !command[0]) return 0;
    arguments = command;
    while (*arguments == L' ' || *arguments == L'\t') ++arguments;
    if (*arguments == L'"') {
        ++arguments;
        while (*arguments && *arguments != L'"') ++arguments;
        if (*arguments != L'"') return 0;
        ++arguments;
        if (*arguments && *arguments != L' ' && *arguments != L'\t')
            return 0;
    } else {
        while (*arguments && *arguments != L' ' && *arguments != L'\t')
            ++arguments;
    }
    while (*arguments == L' ' || *arguments == L'\t') ++arguments;
    needed = WideCharToMultiByte(
        CP_UTF8, WC_ERR_INVALID_CHARS, arguments, -1, 0, 0, 0, 0);
    if (needed <= 0 || needed > 131072) return 0;
    utf8 = (char*)dluma_alloc((SIZE_T)needed);
    if (!utf8) return 0;
    if (WideCharToMultiByte(
            CP_UTF8, WC_ERR_INVALID_CHARS, arguments, -1,
            utf8, needed, 0, 0)
            != needed) {
        dluma_free(utf8);
        return 0;
    }
    dluma_sha256_bytes_hex(
        utf8, (SIZE_T)(needed - 1), snapshot->evidence.command_line_sha256);
    dluma_free(utf8);
    dluma_wstr_copy(snapshot->command_line,
                  DLUMA_ARRAY_COUNT(snapshot->command_line), command);
    *full_command_line = command;
    return 1;
}

static int dluma_host_parent_evidence(
    DWORD* parent_pid,
    ULONGLONG* parent_start_time,
    int* parent_alive,
    int* parent_editor_profile_verified)
{
    HMODULE kernel = GetModuleHandleW(L"kernel32.dll");
    DlumaCreateToolhelp32SnapshotFn create_snapshot;
    DlumaProcess32FirstWFn first;
    DlumaProcess32NextWFn next;
    DlumaOpenProcessFn open_process;
    DlumaGetExitCodeProcessFn get_exit_code;
    DlumaQueryFullProcessImageNameWFn query_image;
    DlumaProcessEntry32W entry;
    HANDLE processes = INVALID_HANDLE_VALUE;
    HANDLE parent = 0;
    HANDLE parent_file = INVALID_HANDLE_VALUE;
    FILETIME created;
    FILETIME exited;
    FILETIME kernel_time;
    FILETIME user_time;
    BY_HANDLE_FILE_INFORMATION file_info;
    LARGE_INTEGER zero;
    DlumaPeIdentity parent_identity;
    BYTE disk_header[4096];
    WCHAR parent_path[DLUMA_MAX_WPATH];
    WCHAR parent_final_path[DLUMA_MAX_WPATH];
    char parent_final_utf8[1024];
    DWORD exit_code = 0;
    DWORD parent_path_size;
    DWORD read = 0;
    DWORD attributes;
    DWORD parent_timestamp = 0;
    DWORD parent_image_size = 0;
    int found = 0;
    if (!kernel || !parent_pid || !parent_start_time || !parent_alive ||
        !parent_editor_profile_verified)
        return 0;
    *parent_pid = 0;
    *parent_start_time = 0;
    *parent_alive = 0;
    *parent_editor_profile_verified = 0;
    create_snapshot = (DlumaCreateToolhelp32SnapshotFn)GetProcAddress(
        kernel, "CreateToolhelp32Snapshot");
    first = (DlumaProcess32FirstWFn)GetProcAddress(kernel, "Process32FirstW");
    next = (DlumaProcess32NextWFn)GetProcAddress(kernel, "Process32NextW");
    open_process = (DlumaOpenProcessFn)GetProcAddress(kernel, "OpenProcess");
    get_exit_code = (DlumaGetExitCodeProcessFn)GetProcAddress(
        kernel, "GetExitCodeProcess");
    query_image = (DlumaQueryFullProcessImageNameWFn)GetProcAddress(
        kernel, "QueryFullProcessImageNameW");
    if (!create_snapshot || !first || !next || !open_process ||
        !get_exit_code || !query_image)
        return 0;
    processes = create_snapshot(DLUMA_TH32CS_SNAPPROCESS, 0);
    if (processes == INVALID_HANDLE_VALUE) return 0;
    memset(&entry, 0, sizeof(entry));
    entry.dwSize = sizeof(entry);
    if (first(processes, &entry)) {
        do {
            if (entry.th32ProcessID == GetCurrentProcessId()) {
                *parent_pid = entry.th32ParentProcessID;
                found = *parent_pid != 0;
                break;
            }
            entry.dwSize = sizeof(entry);
        } while (next(processes, &entry));
    }
    CloseHandle(processes);
    if (!found) return 0;
    parent = open_process(
        DLUMA_PROCESS_QUERY_LIMITED_INFORMATION, FALSE, *parent_pid);
    if (!parent) return 0;
    memset(&created, 0, sizeof(created));
    memset(&exited, 0, sizeof(exited));
    memset(&kernel_time, 0, sizeof(kernel_time));
    memset(&user_time, 0, sizeof(user_time));
    if (GetProcessTimes(parent, &created, &exited, &kernel_time, &user_time) &&
        get_exit_code(parent, &exit_code) && exit_code == DLUMA_STILL_ACTIVE) {
        *parent_start_time = dluma_host_filetime_u64(&created);
        *parent_alive = *parent_start_time != 0;
    }
    memset(&file_info, 0, sizeof(file_info));
    memset(&parent_identity, 0, sizeof(parent_identity));
    memset(parent_path, 0, sizeof(parent_path));
    memset(parent_final_path, 0, sizeof(parent_final_path));
    memset(parent_final_utf8, 0, sizeof(parent_final_utf8));
    zero.QuadPart = 0;
    parent_path_size = DLUMA_ARRAY_COUNT(parent_path);
    if (*parent_alive &&
        query_image(parent, 0, parent_path, &parent_path_size) &&
        parent_path_size && parent_path_size < DLUMA_ARRAY_COUNT(parent_path)) {
        attributes = GetFileAttributesW(parent_path);
        if (attributes != INVALID_FILE_ATTRIBUTES &&
            !(attributes & (FILE_ATTRIBUTE_DIRECTORY |
                            FILE_ATTRIBUTE_REPARSE_POINT)))
            parent_file = CreateFileW(
                parent_path, GENERIC_READ,
                FILE_SHARE_READ | FILE_SHARE_DELETE, 0, OPEN_EXISTING,
                FILE_ATTRIBUTE_NORMAL, 0);
    }
    if (parent_file != INVALID_HANDLE_VALUE &&
        dluma_get_final_path_checked(
            parent_file, parent_final_path,
            DLUMA_ARRAY_COUNT(parent_final_path)) &&
        GetFileInformationByHandle(parent_file, &file_info) &&
        !(file_info.dwFileAttributes &
          (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) &&
        dluma_wstricmp(dluma_host_skip_extended(parent_path),
                     dluma_host_skip_extended(parent_final_path)) == 0 &&
        dluma_sha256_handle_hex(
            parent_file, DLUMA_HOST_IMAGE_MAX_BYTES,
            parent_identity.sha256) &&
        SetFilePointerEx(parent_file, zero, 0, FILE_BEGIN) &&
        ReadFile(parent_file, disk_header, sizeof(disk_header), &read, 0) &&
        dluma_build_identity_parse_pe_header(
            disk_header, read, &parent_timestamp,
            &parent_image_size) &&
        dluma_wide_to_utf8(
            parent_final_path, parent_final_utf8,
            DLUMA_ARRAY_COUNT(parent_final_utf8))) {
        parent_identity.timestamp = (uint32_t)parent_timestamp;
        parent_identity.size_of_image = (uint32_t)parent_image_size;
        *parent_editor_profile_verified =
            dluma_dual_host_is_editor_identity(
                parent_final_utf8, &parent_identity);
    }
    if (parent_file != INVALID_HANDLE_VALUE) CloseHandle(parent_file);
    CloseHandle(parent);
    return *parent_alive;
}

int dluma_dual_host_runtime_handoff_directory(
    WCHAR* output, unsigned int capacity, int create)
{
    WCHAR runtime[DLUMA_MAX_WPATH];
    if (!output || !capacity || !g_dluma.module_dir[0]) return 0;
    dluma_path_join(runtime, DLUMA_ARRAY_COUNT(runtime),
                  g_dluma.module_dir, L"dluma_runtime");
    if (create && !dluma_mkdirs(runtime)) return 0;
    dluma_path_join(output, capacity, runtime, L"editor_sessions");
    return !create || dluma_mkdirs(output);
}

static int dluma_host_handoff_payload_valid(
    const DlumaHandoffDiskHeader* header,
    const BYTE* payload,
    unsigned int payload_size)
{
    char inventory_sha256[DLUMA_SHA256_HEX_CAPACITY];
    unsigned int expected_size;
    unsigned int index;
    if (!header ||
        memcmp(header->magic, DLUMA_HANDOFF_DISK_MAGIC, 8U) != 0 ||
        header->header_size != sizeof(*header) ||
        header->payload_size != payload_size ||
        header->handoff.struct_size < sizeof(header->handoff) ||
        header->handoff.schema_version != 1U ||
        !header->handoff.parent_pid ||
        !header->handoff.parent_start_time ||
        !header->handoff.generation ||
        header->handoff.expires_ms < header->handoff.created_ms ||
        header->handoff.expires_ms - header->handoff.created_ms >
            DLUMA_HANDOFF_EXPIRY_MS ||
        header->handoff.approval_count > DLUMA_HANDOFF_MAX_APPROVALS ||
        header->handoff.sidecar_count > DLUMA_HANDOFF_MAX_SIDECARS ||
        !dluma_host_sha256_text(header->handoff.command_line_sha256) ||
        !dluma_host_sha256_text(header->handoff.loader_sha256) ||
        !dluma_host_sha256_text(header->handoff.inventory_sha256) ||
        !header->handoff.project_root_utf8[0] ||
        header->handoff.project_root_utf8[
            DLUMA_ARRAY_COUNT(header->handoff.project_root_utf8) - 1U] != 0 ||
        !header->handoff.project_name[0] ||
        header->handoff.project_name[
            DLUMA_ARRAY_COUNT(header->handoff.project_name) - 1U] != 0 ||
        !header->handoff.map_name[0] ||
        header->handoff.map_name[
            DLUMA_ARRAY_COUNT(header->handoff.map_name) - 1U] != 0)
        return 0;
    expected_size =
        header->handoff.approval_count * sizeof(DlumaHandoffApprovalRecord) +
        header->handoff.sidecar_count * sizeof(DlumaHandoffSidecarRecord);
    if (expected_size != payload_size || (payload_size && !payload)) return 0;
    dluma_sha256_bytes_hex(payload, payload_size, inventory_sha256);
    if (dluma_stricmp(
            inventory_sha256, header->handoff.inventory_sha256) != 0)
        return 0;
    for (index = 0; index < header->handoff.approval_count; ++index) {
        const DlumaHandoffApprovalRecord* approval =
            ((const DlumaHandoffApprovalRecord*)payload) + index;
        if (!approval->mod_id[0] ||
            approval->mod_id[
                DLUMA_ARRAY_COUNT(approval->mod_id) - 1U] != 0 ||
            !approval->language[0] ||
            approval->language[
                DLUMA_ARRAY_COUNT(approval->language) - 1U] != 0 ||
            (dluma_stricmp(approval->language, "native") != 0 &&
             dluma_stricmp(approval->language, "lua") != 0 &&
             dluma_stricmp(approval->language, "csharp") != 0) ||
            !dluma_host_sha256_text(approval->approval_fingerprint) ||
            !dluma_host_sha256_text(approval->manifest_sha256) ||
            !dluma_host_sha256_text(approval->artifact_sha256) ||
            (index && dluma_stricmp(
                ((const DlumaHandoffApprovalRecord*)payload)[index - 1U].mod_id,
                approval->mod_id) >= 0))
            return 0;
    }
    for (index = 0; index < header->handoff.sidecar_count; ++index) {
        const DlumaHandoffSidecarRecord* sidecar =
            (const DlumaHandoffSidecarRecord*)(
                payload + header->handoff.approval_count *
                    sizeof(DlumaHandoffApprovalRecord)) + index;
        if (!sidecar->canonical_path_utf8[0] ||
            sidecar->canonical_path_utf8[
                DLUMA_ARRAY_COUNT(sidecar->canonical_path_utf8) - 1U] != 0 ||
            !dluma_host_sha256_text(sidecar->sidecar_sha256) ||
            !dluma_host_sha256_text(sidecar->compiled_xml_sha256) ||
            (index && dluma_stricmp(
                ((const DlumaHandoffSidecarRecord*)(
                    payload + header->handoff.approval_count *
                        sizeof(DlumaHandoffApprovalRecord)))[index - 1U]
                        .canonical_path_utf8,
                sidecar->canonical_path_utf8) >= 0))
            return 0;
    }
    return 1;
}

static int dluma_host_owned_handoff_file(
    const WCHAR* path,
    const WCHAR* final_directory,
    DlumaPlaytestHandoff* handoff,
    int remove_file)
{
    HANDLE file = INVALID_HANDLE_VALUE;
    BY_HANDLE_FILE_INFORMATION information;
    LARGE_INTEGER size;
    DlumaHandoffDiskHeader header;
    BYTE* payload = 0;
    WCHAR final_path[DLUMA_MAX_WPATH];
    DWORD read = 0;
    DlumaFileDispositionInfo disposition;
    HMODULE kernel = GetModuleHandleW(L"kernel32.dll");
    DlumaSetFileInformationByHandleFn set_information =
        kernel ? (DlumaSetFileInformationByHandleFn)GetProcAddress(
            kernel, "SetFileInformationByHandle") : 0;
    int result = 0;
    memset(&information, 0, sizeof(information));
    memset(&header, 0, sizeof(header));
    memset(&disposition, 0, sizeof(disposition));
    if (!path || !final_directory || !handoff ||
        (remove_file && !set_information))
        return 0;
    file = CreateFileW(
        path, GENERIC_READ | (remove_file ? DLUMA_DELETE_ACCESS : 0U),
        FILE_SHARE_READ,
        0, OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, 0);
    if (file == INVALID_HANDLE_VALUE ||
        !GetFileInformationByHandle(file, &information) ||
        (information.dwFileAttributes &
            (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) ||
        information.nNumberOfLinks != 1U ||
        !GetFileSizeEx(file, &size) ||
        size.QuadPart < (LONGLONG)sizeof(header) ||
        size.QuadPart > DLUMA_HANDOFF_MAX_FILE_BYTES ||
        !dluma_get_final_path_checked(
            file, final_path, DLUMA_ARRAY_COUNT(final_path)) ||
        !dluma_final_path_equal_or_under(final_path, final_directory) ||
        !ReadFile(file, &header, sizeof(header), &read, 0) ||
        read != sizeof(header) ||
        header.payload_size !=
            (unsigned int)(size.QuadPart - (LONGLONG)sizeof(header)))
        goto done;
    if (header.payload_size) {
        payload = (BYTE*)dluma_alloc(header.payload_size);
        if (!payload ||
            !ReadFile(file, payload, header.payload_size, &read, 0) ||
            read != header.payload_size)
            goto done;
    }
    if (!dluma_host_handoff_payload_valid(
            &header, payload, header.payload_size))
        goto done;
    *handoff = header.handoff;
    if (remove_file) {
        disposition.delete_file = TRUE;
        if (!set_information(
                file, DLUMA_FILE_DISPOSITION_INFO_CLASS,
                &disposition, sizeof(disposition)))
            goto done;
    }
    result = 1;
done:
    if (payload) dluma_free(payload);
    if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
    return result;
}

static int dluma_host_prune_handoffs(
    const WCHAR* directory,
    unsigned int retained_limit)
{
    DlumaDirectoryIdentity directory_identity;
    unsigned int pass;
    if (!directory || retained_limit > DLUMA_HANDOFF_MAX_RECORDS ||
        !dluma_open_directory_identity(directory, &directory_identity))
        return 0;
    for (pass = 0; pass < 4096U; ++pass) {
        WCHAR pattern[DLUMA_MAX_WPATH];
        WCHAR path[DLUMA_MAX_WPATH];
        WCHAR oldest_path[DLUMA_MAX_WPATH];
        WIN32_FIND_DATAW found;
        HANDLE search;
        DlumaPlaytestHandoff oldest;
        unsigned int count = 0;
        int have_oldest = 0;
        memset(&found, 0, sizeof(found));
        memset(&oldest, 0, sizeof(oldest));
        oldest_path[0] = 0;
        dluma_path_join(
            pattern, DLUMA_ARRAY_COUNT(pattern), directory, L"*.dlumah");
        search = FindFirstFileW(pattern, &found);
        if (search == INVALID_HANDLE_VALUE)
            return GetLastError() == ERROR_FILE_NOT_FOUND;
        do {
            DlumaPlaytestHandoff candidate;
            if (found.dwFileAttributes &
                (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT))
                continue;
            dluma_path_join(
                path, DLUMA_ARRAY_COUNT(path), directory, found.cFileName);
            memset(&candidate, 0, sizeof(candidate));
            if (!dluma_host_owned_handoff_file(
                    path, directory_identity.final_path, &candidate, 0))
                continue;
            ++count;
            if (!have_oldest ||
                candidate.parent_start_time < oldest.parent_start_time ||
                (candidate.parent_start_time == oldest.parent_start_time &&
                 candidate.generation < oldest.generation) ||
                (candidate.parent_start_time == oldest.parent_start_time &&
                 candidate.generation == oldest.generation &&
                 candidate.created_ms < oldest.created_ms)) {
                oldest = candidate;
                dluma_wstr_copy(
                    oldest_path, DLUMA_ARRAY_COUNT(oldest_path), path);
                have_oldest = 1;
            }
        } while (FindNextFileW(search, &found));
        FindClose(search);
        if (count <= retained_limit) return 1;
        if (!have_oldest || !dluma_host_owned_handoff_file(
                oldest_path, directory_identity.final_path, &oldest, 1))
            return 0;
    }
    return 0;
}

static int dluma_host_read_handoff_candidate(
    const WCHAR* path,
    const DlumaHostEvidence* evidence,
    DlumaPlaytestHandoff* output)
{
    WCHAR directory[DLUMA_MAX_WPATH];
    DlumaDirectoryIdentity identity;
    if (!path || !evidence || !output ||
        !dluma_dual_host_runtime_handoff_directory(
            directory, DLUMA_ARRAY_COUNT(directory), 0) ||
        !dluma_open_directory_identity(directory, &identity) ||
        !dluma_host_owned_handoff_file(
            path, identity.final_path, output, 0))
        return 0;
    return dluma_playtest_handoff_validate(output, evidence);
}

static int dluma_host_find_handoff(
    DlumaDualHostRuntimeSnapshot* snapshot)
{
    WCHAR directory[DLUMA_MAX_WPATH];
    WCHAR pattern[DLUMA_MAX_WPATH];
    WCHAR path[DLUMA_MAX_WPATH];
    WIN32_FIND_DATAW found;
    HANDLE search;
    DlumaPlaytestHandoff best;
    int have_best = 0;
    if (!snapshot ||
        !dluma_dual_host_runtime_handoff_directory(
            directory, DLUMA_ARRAY_COUNT(directory), 0))
        return 0;
    dluma_path_join(pattern, DLUMA_ARRAY_COUNT(pattern), directory, L"*.dlumah");
    memset(&found, 0, sizeof(found));
    memset(&best, 0, sizeof(best));
    search = FindFirstFileW(pattern, &found);
    if (search == INVALID_HANDLE_VALUE) return 0;
    do {
        DlumaPlaytestHandoff candidate;
        if (found.dwFileAttributes &
            (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT))
            continue;
        dluma_path_join(path, DLUMA_ARRAY_COUNT(path), directory, found.cFileName);
        memset(&candidate, 0, sizeof(candidate));
        if (dluma_host_read_handoff_candidate(
                path, &snapshot->evidence, &candidate) &&
            (!have_best || candidate.generation > best.generation)) {
            best = candidate;
            have_best = 1;
        }
    } while (FindNextFileW(search, &found));
    FindClose(search);
    if (!have_best) return 0;
    snapshot->handoff = best;
    snapshot->evidence.handoff = &snapshot->handoff;
    return 1;
}

static void dluma_host_capture_process_start(DlumaDualHostRuntimeSnapshot* snapshot)
{
    FILETIME created;
    FILETIME exited;
    FILETIME kernel_time;
    FILETIME user_time;
    memset(&created, 0, sizeof(created));
    memset(&exited, 0, sizeof(exited));
    memset(&kernel_time, 0, sizeof(kernel_time));
    memset(&user_time, 0, sizeof(user_time));
    if (snapshot && GetProcessTimes(
            (HANDLE)(ULONG_PTR)-1, &created, &exited,
            &kernel_time, &user_time))
        snapshot->process_start_time = dluma_host_filetime_u64(&created);
}

int dluma_dual_host_runtime_initialize(void)
{
    DlumaDualHostRuntimeSnapshot snapshot;
    const DlumaBuildIdentity* loader_identity;
    const WCHAR* command = 0;
    DWORD parent_pid = 0;
    ULONGLONG parent_start = 0;
    int parent_alive = 0;
    int parent_editor_profile_verified = 0;
    LONG state = dluma_atomic_compare_exchange(
        &g_dluma_dual_runtime_state, 1, 0);
    if (state != 0)
        return dluma_atomic_compare_exchange(
                   &g_dluma_dual_runtime_state, 0, 0) == 2 &&
               g_dluma_dual_runtime.initialized;
    memset(&snapshot, 0, sizeof(snapshot));
    snapshot.evidence.struct_size = sizeof(snapshot.evidence);
    snapshot.evidence.now_ms = GetTickCount64();
    dluma_host_capture_process_start(&snapshot);
    (void)dluma_build_identity_initialize(g_dluma.self_module);
    loader_identity = dluma_build_identity_snapshot();
    if (loader_identity && loader_identity->complete)
        dluma_str_copy(snapshot.evidence.loader_sha256,
                     DLUMA_ARRAY_COUNT(snapshot.evidence.loader_sha256),
                     loader_identity->sha256);
    (void)dluma_host_capture_image(&snapshot);
    if (dluma_host_capture_command_line(&snapshot, &command)) {
        snapshot.evidence.has_editor_debugconf =
            dluma_wcontains_icase(command, L"debugconf_editor.scr") ? 1U : 0U;
        snapshot.evidence.selected_loader =
            dluma_wcontains_icase(command, L"/mod=") ? 1U : 0U;
    }
    if (dluma_host_parent_evidence(
            &parent_pid, &parent_start, &parent_alive,
            &parent_editor_profile_verified)) {
        snapshot.evidence.parent_pid = parent_pid;
        snapshot.evidence.parent_start_time = parent_start;
        snapshot.evidence.parent_alive = parent_alive ? 1U : 0U;
        snapshot.evidence.parent_editor_profile_verified =
            parent_editor_profile_verified ? 1U : 0U;
    }
    if (snapshot.evidence.has_editor_debugconf ||
        snapshot.evidence.selected_loader)
        (void)dluma_host_find_handoff(&snapshot);
    (void)dluma_dual_host_classify(
        &snapshot.evidence, &snapshot.classification);
    snapshot.initialized = 1U;
    g_dluma_dual_runtime = snapshot;
    if (g_dluma_dual_runtime.evidence.handoff)
        g_dluma_dual_runtime.evidence.handoff = &g_dluma_dual_runtime.handoff;
    dluma_atomic_exchange(&g_dluma_dual_runtime_state, 2);
    dluma_log_pair(
        g_dluma_dual_runtime.classification.forwarding_only ? "WARN" : "INFO",
        "dual_host_classification",
        g_dluma_dual_runtime.classification.reason);
    dluma_log_u64("INFO", "dual_host_kind",
                (ULONGLONG)g_dluma_dual_runtime.classification.kind);
    dluma_log_u64("INFO", "dual_host_forwarding_only",
                (ULONGLONG)g_dluma_dual_runtime.classification.forwarding_only);
    return 1;
}

const DlumaDualHostRuntimeSnapshot* dluma_dual_host_runtime_snapshot(void)
{
    return dluma_atomic_compare_exchange(
               &g_dluma_dual_runtime_state, 0, 0) == 2
        ? &g_dluma_dual_runtime : 0;
}

DlumaHostKind dluma_dual_host_runtime_kind(void)
{
    const DlumaDualHostRuntimeSnapshot* snapshot =
        dluma_dual_host_runtime_snapshot();
    return snapshot ? snapshot->classification.kind : DLUMA_HOST_UNKNOWN;
}

int dluma_dual_host_runtime_forwarding_only(void)
{
    const DlumaDualHostRuntimeSnapshot* snapshot =
        dluma_dual_host_runtime_snapshot();
    return !snapshot || snapshot->classification.forwarding_only;
}

int dluma_dual_host_runtime_is_editor(void)
{
    return dluma_dual_host_runtime_kind() == DLUMA_HOST_CHROME_EDITOR;
}

int dluma_dual_host_runtime_is_playtest(void)
{
    return dluma_dual_host_runtime_kind() == DLUMA_HOST_PLAYER_EDITOR_PLAYTEST;
}

int dluma_dual_host_runtime_pin_editor_module(void)
{
    HMODULE pinned = 0;
    if (!dluma_dual_host_runtime_is_editor()) return 0;
    if (g_dluma_dual_runtime.module_pinned) return 1;
    if (!GetModuleHandleExW(
            GET_MODULE_HANDLE_EX_FLAG_PIN |
                GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
            (LPCWSTR)(const void*)&g_dluma_dual_runtime, &pinned) ||
        pinned != g_dluma.self_module) {
        dluma_log_hex("ERROR", "editor_module_pin_failed", GetLastError());
        return 0;
    }
    g_dluma_dual_runtime.module_pinned = 1U;
    dluma_log_pair("INFO", "editor_module_pinned", "true");
    return 1;
}

static int dluma_host_write_all(HANDLE file, const void* data, DWORD size)
{
    const BYTE* cursor = (const BYTE*)data;
    while (size) {
        DWORD written = 0;
        if (!WriteFile(file, cursor, size, &written, 0) || !written)
            return 0;
        cursor += written;
        size -= written;
    }
    return 1;
}

int dluma_dual_host_runtime_publish_handoff_file(
    const DlumaPlaytestHandoff* handoff,
    const DlumaHandoffApprovalRecord* approvals,
    unsigned int approval_count,
    const DlumaHandoffSidecarRecord* sidecars,
    unsigned int sidecar_count,
    WCHAR* published_path,
    unsigned int published_path_capacity)
{
    DlumaHandoffDiskHeader header;
    BYTE* payload = 0;
    unsigned int approval_bytes;
    unsigned int sidecar_bytes;
    unsigned int payload_size;
    WCHAR directory[DLUMA_MAX_WPATH];
    WCHAR filename[256];
    WCHAR temporary[DLUMA_MAX_WPATH];
    WCHAR final_path[DLUMA_MAX_WPATH];
    char number[64];
    WCHAR wide_number[64];
    HANDLE file = INVALID_HANDLE_VALUE;
    DlumaPlaytestHandoffStore validation_store;
    int result = 0;
    temporary[0] = 0;
    final_path[0] = 0;
    if (!handoff || approval_count > DLUMA_HANDOFF_MAX_APPROVALS ||
        sidecar_count > DLUMA_HANDOFF_MAX_SIDECARS ||
        (approval_count && !approvals) || (sidecar_count && !sidecars) ||
        !dluma_dual_host_runtime_handoff_directory(
            directory, DLUMA_ARRAY_COUNT(directory), 1))
        return 0;
    if (!dluma_host_prune_handoffs(
            directory, DLUMA_HANDOFF_MAX_RECORDS - 1U))
        return 0;
    approval_bytes = approval_count * sizeof(DlumaHandoffApprovalRecord);
    sidecar_bytes = sidecar_count * sizeof(DlumaHandoffSidecarRecord);
    payload_size = approval_bytes + sidecar_bytes;
    if (payload_size) {
        payload = (BYTE*)dluma_alloc(payload_size);
        if (!payload) return 0;
        if (approval_bytes) memcpy(payload, approvals, approval_bytes);
        if (sidecar_bytes) memcpy(payload + approval_bytes, sidecars, sidecar_bytes);
    }
    memset(&header, 0, sizeof(header));
    memcpy(header.magic, DLUMA_HANDOFF_DISK_MAGIC, 8U);
    header.header_size = sizeof(header);
    header.payload_size = payload_size;
    header.handoff = *handoff;
    header.handoff.struct_size = sizeof(header.handoff);
    header.handoff.schema_version = 1U;
    header.handoff.approval_count = approval_count;
    header.handoff.sidecar_count = sidecar_count;
    dluma_sha256_bytes_hex(
        payload, payload_size, header.handoff.inventory_sha256);
    dluma_playtest_handoff_store_initialize(&validation_store);
    if (!dluma_playtest_handoff_publish(
            &validation_store, &header.handoff))
        goto done;
    filename[0] = 0;
    dluma_u64_to_text(header.handoff.parent_pid, number, DLUMA_ARRAY_COUNT(number));
    if (!dluma_utf8_to_wide(number, wide_number, DLUMA_ARRAY_COUNT(wide_number)))
        goto done;
    dluma_wstr_append(filename, DLUMA_ARRAY_COUNT(filename), wide_number);
    dluma_wstr_append(filename, DLUMA_ARRAY_COUNT(filename), L"-");
    dluma_u64_to_text(header.handoff.parent_start_time, number, DLUMA_ARRAY_COUNT(number));
    if (!dluma_utf8_to_wide(number, wide_number, DLUMA_ARRAY_COUNT(wide_number)))
        goto done;
    dluma_wstr_append(filename, DLUMA_ARRAY_COUNT(filename), wide_number);
    dluma_wstr_append(filename, DLUMA_ARRAY_COUNT(filename), L"-");
    dluma_u64_to_text(header.handoff.generation, number, DLUMA_ARRAY_COUNT(number));
    if (!dluma_utf8_to_wide(number, wide_number, DLUMA_ARRAY_COUNT(wide_number)))
        goto done;
    dluma_wstr_append(filename, DLUMA_ARRAY_COUNT(filename), wide_number);
    dluma_wstr_append(filename, DLUMA_ARRAY_COUNT(filename), L".dlumah");
    dluma_path_join(final_path, DLUMA_ARRAY_COUNT(final_path), directory, filename);
    dluma_wstr_copy(temporary, DLUMA_ARRAY_COUNT(temporary), final_path);
    dluma_wstr_append(temporary, DLUMA_ARRAY_COUNT(temporary), L".tmp");
    file = CreateFileW(
        temporary, GENERIC_WRITE, 0, 0, CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL, 0);
    if (file == INVALID_HANDLE_VALUE ||
        !dluma_host_write_all(file, &header, sizeof(header)) ||
        (payload_size && !dluma_host_write_all(file, payload, payload_size)) ||
        !FlushFileBuffers(file))
        goto done;
    CloseHandle(file);
    file = INVALID_HANDLE_VALUE;
    if (!MoveFileExW(
            temporary, final_path,
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        goto done;
    if (published_path && published_path_capacity)
        dluma_wstr_copy(published_path, published_path_capacity, final_path);
    result = 1;
done:
    if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
    if (!result && temporary[0]) DeleteFileW(temporary);
    if (payload) dluma_free(payload);
    return result;
}
