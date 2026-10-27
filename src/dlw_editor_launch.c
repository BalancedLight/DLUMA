#include "dlw_editor_launch.h"
#include "dlw_editor_host.h"
#include "dlw_build_identity.h"
#include "dlw_dual_host_runtime.h"
#include "dlw_editor_animation_bridge.h"
#include "dlw_editor_presence.h"
#include "dlw_log.h"
#include "dlw_native_mods.h"
#include "dlw_path_security.h"
#include "dlw_project_owner_identity.h"
#include "dlw_quest_phase_runtime.h"

#define DLUMA_EDITOR_CREATE_PROCESS_IAT_RVA 0x011456D8UL
#define DLUMA_EDITOR_SHELL_EXECUTE_IAT_RVA 0x01146568UL
#define DLUMA_EDITOR_TIMESTAMP 0x623396E1UL
#define DLUMA_EDITOR_IMAGE_SIZE 0x01E4A000UL
#define DLUMA_PLAYER_TIMESTAMP 0x623397B8UL
#define DLUMA_PLAYER_IMAGE_SIZE 0x00103000UL
#define DLUMA_PLAYER_SHA256 \
    "61D9DFB20CFA179C6D097537DA46BA9DBE3D58797938762EF4EACFF5DFCC4B3A"
#define DLUMA_PE64_MAGIC 0x020BU
#define DLUMA_IMPORT_DIRECTORY_OFFSET 0x78UL
#define DLUMA_EDITOR_DEBUGCONF_MAX_BYTES (64UL * 1024UL)
#define DLUMA_EDITOR_PLAYER_MAX_BYTES (1024ULL * 1024ULL * 1024ULL)
#define DLUMA_EDITOR_LOADER_MAX_BYTES (1024ULL * 1024ULL * 1024ULL)
#define DLUMA_EDITOR_LAUNCH_COMMAND_MAX 131071U
#define DLUMA_EDITOR_WORKSHOP_PROJECT_LIMIT 4096U

typedef BOOL (WINAPI* DlumaCreateProcessAFn)(
    LPCSTR application_name,
    LPSTR command_line,
    LPVOID process_attributes,
    LPVOID thread_attributes,
    BOOL inherit_handles,
    DWORD creation_flags,
    LPVOID environment,
    LPCSTR current_directory,
    LPVOID startup_info,
    LPVOID process_information);

typedef HINSTANCE (WINAPI* DlumaShellExecuteAFn)(
    HWND window,
    LPCSTR operation,
    LPCSTR file,
    LPCSTR parameters,
    LPCSTR directory,
    int show_command);

typedef struct DlumaEditorLaunchState {
    void** iat_slot;
    DlumaCreateProcessAFn original;
    void** shell_execute_iat_slot;
    DlumaShellExecuteAFn original_shell_execute;
    volatile LONG installed;
    volatile LONG stopping;
    volatile LONG active_calls;
    volatile LONG detached;
    volatile LONGLONG next_generation;
    volatile LONGLONG published_count;
    volatile LONGLONG rejected_count;
} DlumaEditorLaunchState;

static DlumaEditorLaunchState g_dluma_editor_launch;
static volatile LONG g_dluma_editor_project_resolution_state;

static BOOL WINAPI dluma_editor_create_process_hook(
    LPCSTR application_name,
    LPSTR command_line,
    LPVOID process_attributes,
    LPVOID thread_attributes,
    BOOL inherit_handles,
    DWORD creation_flags,
    LPVOID environment,
    LPCSTR current_directory,
    LPVOID startup_info,
    LPVOID process_information);

static HINSTANCE WINAPI dluma_editor_shell_execute_hook(
    HWND window,
    LPCSTR operation,
    LPCSTR file,
    LPCSTR parameters,
    LPCSTR directory,
    int show_command);

static unsigned int dluma_editor_bounded_length(
    const char* value,
    unsigned int maximum);

static WORD dluma_editor_read_u16(const void* address)
{
    const BYTE* value = (const BYTE*)address;
    return (WORD)((WORD)value[0] | ((WORD)value[1] << 8));
}

static DWORD dluma_editor_read_u32(const void* address)
{
    const BYTE* value = (const BYTE*)address;
    return (DWORD)value[0] | ((DWORD)value[1] << 8) |
        ((DWORD)value[2] << 16) | ((DWORD)value[3] << 24);
}

static ULONGLONG dluma_editor_read_u64(const void* address)
{
    const BYTE* value = (const BYTE*)address;
    ULONGLONG result = 0;
    unsigned int index;
    for (index = 0; index < 8U; ++index)
        result |= (ULONGLONG)value[index] << (index * 8U);
    return result;
}

static int dluma_editor_rva_valid(
    DWORD rva,
    SIZE_T bytes,
    DWORD image_size)
{
    return rva && rva < image_size && bytes <= image_size - rva;
}

static int dluma_editor_ascii_equal(const char* left, const char* right)
{
    return left && right && dluma_stricmp(left, right) == 0;
}

static int dluma_editor_section_is_rdata(
    BYTE* base,
    DWORD nt,
    DWORD image_size,
    DWORD rva)
{
    WORD section_count;
    WORD optional_size;
    BYTE* section;
    unsigned int index;
    if (!base || !dluma_is_readable(base + nt + 4, 20U)) return 0;
    section_count = dluma_editor_read_u16(base + nt + 6);
    optional_size = dluma_editor_read_u16(base + nt + 20);
    if (!section_count || section_count > 96U) return 0;
    section = base + nt + 24U + optional_size;
    if (!dluma_is_readable(section, (SIZE_T)section_count * 40U)) return 0;
    for (index = 0; index < section_count; ++index, section += 40U) {
        DWORD virtual_size = dluma_editor_read_u32(section + 8U);
        DWORD virtual_address = dluma_editor_read_u32(section + 12U);
        DWORD raw_size = dluma_editor_read_u32(section + 16U);
        DWORD span = virtual_size > raw_size ? virtual_size : raw_size;
        DWORD characteristics = dluma_editor_read_u32(section + 36U);
        if (rva < virtual_address || rva - virtual_address >= span) continue;
        return section[0] == '.' && section[1] == 'r' &&
            section[2] == 'd' && section[3] == 'a' &&
            section[4] == 't' && section[5] == 'a' && !section[6] &&
            (characteristics & 0x40000000UL) != 0 &&
            (characteristics & (0x80000000UL | 0x20000000UL)) == 0 &&
            dluma_editor_rva_valid(rva, sizeof(void*), image_size);
    }
    return 0;
}

static void** dluma_editor_find_exact_iat(
    const WCHAR* loaded_library,
    const char* imported_library,
    const char* imported_function,
    DWORD exact_rva)
{
    BYTE* base = (BYTE*)GetModuleHandleW(0);
    DWORD nt;
    DWORD timestamp;
    DWORD image_size;
    DWORD import_rva;
    DWORD import_size;
    BYTE* descriptor;
    HMODULE library_module;
    FARPROC expected;
    unsigned int imported_library_size;
    unsigned int imported_function_size;
    if (!loaded_library || !imported_library || !imported_function)
        return 0;
    imported_library_size = dluma_editor_bounded_length(
        imported_library, 128U);
    imported_function_size = dluma_editor_bounded_length(
        imported_function, 128U);
    if (!imported_library_size || imported_library_size >= 128U ||
        !imported_function_size || imported_function_size >= 128U)
        return 0;
    if (!base || !dluma_is_readable(base, 0x100U) ||
        base[0] != 'M' || base[1] != 'Z')
        return 0;
    nt = dluma_editor_read_u32(base + 0x3CU);
    if (nt > 0x100000UL || !dluma_is_readable(base + nt, 0x108U) ||
        base[nt] != 'P' || base[nt + 1U] != 'E' ||
        dluma_editor_read_u16(base + nt + 24U) != DLUMA_PE64_MAGIC)
        return 0;
    timestamp = dluma_editor_read_u32(base + nt + 8U);
    image_size = dluma_editor_read_u32(base + nt + 0x50U);
    if (timestamp != DLUMA_EDITOR_TIMESTAMP ||
        image_size != DLUMA_EDITOR_IMAGE_SIZE ||
        !dluma_editor_section_is_rdata(
            base, nt, image_size, exact_rva))
        return 0;
    import_rva = dluma_editor_read_u32(
        base + nt + 24U + DLUMA_IMPORT_DIRECTORY_OFFSET);
    import_size = dluma_editor_read_u32(
        base + nt + 24U + DLUMA_IMPORT_DIRECTORY_OFFSET + 4U);
    if (!dluma_editor_rva_valid(import_rva, 20U, image_size) ||
        import_size < 20U || import_size > image_size - import_rva)
        return 0;
    library_module = GetModuleHandleW(loaded_library);
    expected = library_module
        ? GetProcAddress(library_module, imported_function) : 0;
    if (!expected) return 0;
    descriptor = base + import_rva;
    while ((SIZE_T)(descriptor - (base + import_rva)) + 20U <= import_size) {
        DWORD original_thunk = dluma_editor_read_u32(descriptor);
        DWORD name_rva = dluma_editor_read_u32(descriptor + 12U);
        DWORD first_thunk = dluma_editor_read_u32(descriptor + 16U);
        const char* library;
        unsigned int index;
        if (!original_thunk && !first_thunk) break;
        if (!dluma_editor_rva_valid(
                name_rva, imported_library_size + 1U, image_size) ||
            !dluma_editor_rva_valid(original_thunk, 8U, image_size) ||
            !dluma_editor_rva_valid(first_thunk, 8U, image_size)) {
            descriptor += 20U;
            continue;
        }
        library = (const char*)(base + name_rva);
        if (!dluma_is_readable(library, imported_library_size + 1U) ||
            !dluma_editor_ascii_equal(library, imported_library)) {
            descriptor += 20U;
            continue;
        }
        for (index = 0; ; ++index) {
            DWORD oft_rva = original_thunk + index * 8U;
            DWORD iat_rva = first_thunk + index * 8U;
            ULONGLONG thunk;
            DWORD import_name_rva;
            const char* import_name;
            void** slot;
            if (!dluma_editor_rva_valid(oft_rva, 8U, image_size) ||
                !dluma_editor_rva_valid(iat_rva, 8U, image_size))
                break;
            thunk = dluma_editor_read_u64(base + oft_rva);
            if (!thunk) break;
            if ((thunk & 0x8000000000000000ULL) || thunk > 0xFFFFFFFFULL)
                continue;
            import_name_rva = (DWORD)thunk;
            if (!dluma_editor_rva_valid(
                    import_name_rva, imported_function_size + 3U,
                    image_size))
                continue;
            import_name = (const char*)(base + import_name_rva + 2U);
            if (!dluma_is_readable(
                    import_name, imported_function_size + 1U) ||
                !dluma_editor_ascii_equal(import_name, imported_function))
                continue;
            if (iat_rva != exact_rva) return 0;
            slot = (void**)(base + iat_rva);
            return dluma_is_readable(slot, sizeof(void*)) &&
                *slot == (void*)expected ? slot : 0;
        }
        descriptor += 20U;
    }
    return 0;
}

static void** dluma_editor_find_create_process_iat(void)
{
    return dluma_editor_find_exact_iat(
        L"kernel32.dll", "KERNEL32.dll", "CreateProcessA",
        DLUMA_EDITOR_CREATE_PROCESS_IAT_RVA);
}

static void** dluma_editor_find_shell_execute_iat(void)
{
    return dluma_editor_find_exact_iat(
        L"shell32.dll", "SHELL32.dll", "ShellExecuteA",
        DLUMA_EDITOR_SHELL_EXECUTE_IAT_RVA);
}

static unsigned int dluma_editor_bounded_length(
    const char* value,
    unsigned int maximum)
{
    unsigned int size = 0;
    if (!value) return 0;
    while (size < maximum && value[size]) ++size;
    return size;
}

static const char* dluma_editor_find_icase(
    const char* text,
    unsigned int text_size,
    const char* needle)
{
    unsigned int needle_size = dluma_editor_bounded_length(needle, 256U);
    unsigned int index;
    if (!text || !needle_size || needle_size > text_size) return 0;
    for (index = 0; index + needle_size <= text_size; ++index) {
        unsigned int part;
        for (part = 0; part < needle_size; ++part) {
            char left = text[index + part];
            char right = needle[part];
            if (left >= 'A' && left <= 'Z') left += (char)('a' - 'A');
            if (right >= 'A' && right <= 'Z') right += (char)('a' - 'A');
            if (left != right) break;
        }
        if (part == needle_size) return text + index;
    }
    return 0;
}

static int dluma_editor_command_hash(
    const char* command,
    unsigned int command_size,
    int command_is_arguments,
    char sha256[DLUMA_SHA256_HEX_CAPACITY])
{
    const char* arguments = command;
    unsigned int argument_size = command_size;
    WCHAR* wide = 0;
    char* utf8 = 0;
    int wide_size;
    int utf8_size;
    int result = 0;
    if (!command || !command_size ||
        command_size >= DLUMA_EDITOR_LAUNCH_COMMAND_MAX || !sha256)
        return 0;
    if (!command_is_arguments) {
        unsigned int cursor = 0;
        while (cursor < command_size &&
               (command[cursor] == ' ' || command[cursor] == '\t'))
            ++cursor;
        if (cursor >= command_size) return 0;
        if (command[cursor] == '"') {
            ++cursor;
            while (cursor < command_size && command[cursor] != '"')
                ++cursor;
            if (cursor >= command_size) return 0;
            ++cursor;
            if (cursor < command_size && command[cursor] != ' ' &&
                command[cursor] != '\t')
                return 0;
        } else {
            while (cursor < command_size && command[cursor] != ' ' &&
                   command[cursor] != '\t')
                ++cursor;
        }
        while (cursor < command_size &&
               (command[cursor] == ' ' || command[cursor] == '\t'))
            ++cursor;
        arguments = command + cursor;
        argument_size = command_size - cursor;
    } else {
        while (argument_size &&
               (*arguments == ' ' || *arguments == '\t')) {
            ++arguments;
            --argument_size;
        }
    }
    if (!argument_size) return 0;
    wide_size = MultiByteToWideChar(
        0U, 0U, arguments, (int)argument_size, 0, 0);
    if (wide_size <= 0) return 0;
    wide = (WCHAR*)dluma_alloc(((SIZE_T)wide_size + 1U) * sizeof(WCHAR));
    if (!wide || MultiByteToWideChar(
            0U, 0U, arguments, (int)argument_size,
            wide, wide_size) != wide_size)
        goto done;
    wide[wide_size] = 0;
    utf8_size = WideCharToMultiByte(
        CP_UTF8, WC_ERR_INVALID_CHARS, wide, wide_size,
        0, 0, 0, 0);
    if (utf8_size <= 0) goto done;
    utf8 = (char*)dluma_alloc((SIZE_T)utf8_size);
    if (!utf8 || WideCharToMultiByte(
            CP_UTF8, WC_ERR_INVALID_CHARS, wide, wide_size,
            utf8, utf8_size, 0, 0) != utf8_size)
        goto done;
    dluma_sha256_bytes_hex(utf8, (SIZE_T)utf8_size, sha256);
    result = 1;
done:
    if (wide) dluma_free(wide);
    if (utf8) dluma_free(utf8);
    return result;
}

static int dluma_editor_extract_command_token(
    const char* command,
    unsigned int command_size,
    const char* marker,
    char* output,
    unsigned int capacity)
{
    const char* value = dluma_editor_find_icase(command, command_size, marker);
    const char* end = command + command_size;
    unsigned int size = 0;
    char quote = 0;
    if (!value || !output || !capacity) return 0;
    value += dluma_editor_bounded_length(marker, 256U);
    if (value < end && (*value == '"' || *value == '\'')) quote = *value++;
    while (value < end) {
        char current = *value++;
        if ((quote && current == quote) ||
            (!quote && (current == ' ' || current == '\t' ||
                        current == '\r' || current == '\n')))
            break;
        if (!current || size + 1U >= capacity) return 0;
        output[size++] = current;
    }
    if (!size || (quote && (value > end || value[-1] != quote))) return 0;
    output[size] = 0;
    return 1;
}

static int dluma_editor_resolve_process_path(
    const char* application_name,
    const char* command,
    unsigned int command_size,
    const char* current_directory,
    WCHAR* output,
    unsigned int capacity)
{
    char lexical_utf8[DLUMA_MAX_WPATH * 2U];
    WCHAR lexical[DLUMA_MAX_WPATH];
    WCHAR directory[DLUMA_MAX_WPATH];
    const char* begin;
    unsigned int size = 0;
    char quote = 0;
    if (!output || !capacity) return 0;
    output[0] = 0;
    if (application_name && application_name[0]) {
        size = dluma_editor_bounded_length(
            application_name, DLUMA_ARRAY_COUNT(lexical_utf8));
        if (!size || size >= DLUMA_ARRAY_COUNT(lexical_utf8)) return 0;
        memcpy(lexical_utf8, application_name, size);
    } else {
        begin = command;
        if (!begin || !command_size) return 0;
        while (size < command_size &&
               (begin[size] == ' ' || begin[size] == '\t')) ++size;
        begin += size;
        command_size -= size;
        size = 0;
        if (command_size && (*begin == '"' || *begin == '\''))
            quote = *begin++;
        while (size < command_size && size + 1U <
               DLUMA_ARRAY_COUNT(lexical_utf8)) {
            char current = begin[size];
            if ((quote && current == quote) ||
                (!quote && (current == ' ' || current == '\t'))) break;
            lexical_utf8[size++] = current;
        }
        if (!size || size + 1U >= DLUMA_ARRAY_COUNT(lexical_utf8)) return 0;
    }
    lexical_utf8[size] = 0;
    if (!dluma_utf8_to_wide(
            lexical_utf8, lexical, DLUMA_ARRAY_COUNT(lexical)))
        return 0;
    if ((lexical[0] && lexical[1] == L':') ||
        (lexical[0] == L'\\' && lexical[1] == L'\\'))
        return dluma_wstr_copy(output, capacity, lexical), 1;
    if (current_directory && current_directory[0]) {
        if (!dluma_utf8_to_wide(
                current_directory, directory, DLUMA_ARRAY_COUNT(directory)))
            return 0;
    } else {
        DWORD got = GetCurrentDirectoryW(
            DLUMA_ARRAY_COUNT(directory), directory);
        if (!got || got >= DLUMA_ARRAY_COUNT(directory)) return 0;
    }
    return dluma_checked_path_join(output, capacity, directory, lexical);
}

static int dluma_editor_validate_player(
    const WCHAR* lexical,
    WCHAR* final_path,
    unsigned int final_capacity,
    WCHAR* directory_out,
    unsigned int directory_capacity)
{
    HANDLE file = INVALID_HANDLE_VALUE;
    BYTE header[4096];
    DWORD read = 0;
    DWORD timestamp = 0;
    DWORD image_size = 0;
    BY_HANDLE_FILE_INFORMATION info;
    const WCHAR* leaf;
    SIZE_T final_size;
    int result = 0;
    memset(&info, 0, sizeof(info));
    if (!lexical || !final_path || !directory_out) return 0;
    file = CreateFileW(
        lexical, GENERIC_READ, FILE_SHARE_READ, 0, OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL, 0);
    if (file == INVALID_HANDLE_VALUE ||
        !GetFileInformationByHandle(file, &info) ||
        (info.dwFileAttributes &
            (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) ||
        info.nNumberOfLinks != 1U ||
        !dluma_get_final_path_checked(file, final_path, final_capacity))
        goto done;
    leaf = final_path;
    for (final_size = 0; final_path[final_size]; ++final_size)
        if (final_path[final_size] == L'\\' || final_path[final_size] == L'/')
            leaf = final_path + final_size + 1U;
    if (dluma_wstricmp(leaf, L"DyingLightPlayer.exe") != 0 ||
        !ReadFile(file, header, sizeof(header), &read, 0) ||
        !dluma_build_identity_parse_pe_header(
            header, read, &timestamp, &image_size) ||
        timestamp != DLUMA_PLAYER_TIMESTAMP || image_size != DLUMA_PLAYER_IMAGE_SIZE ||
        !SetFilePointerEx(file, (LARGE_INTEGER){0}, 0, FILE_BEGIN))
        goto done;
    {
        char sha256[DLUMA_SHA256_HEX_CAPACITY];
        if (!dluma_sha256_handle_hex(
                file, DLUMA_EDITOR_PLAYER_MAX_BYTES, sha256) ||
            dluma_stricmp(sha256, DLUMA_PLAYER_SHA256) != 0)
            goto done;
    }
    dluma_wstr_copy(directory_out, directory_capacity, final_path);
    final_size = wcslen(directory_out);
    while (final_size && directory_out[final_size - 1U] != L'\\' &&
           directory_out[final_size - 1U] != L'/')
        --final_size;
    if (!final_size) goto done;
    directory_out[final_size - 1U] = 0;
    result = 1;
done:
    if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
    return result;
}

static int dluma_editor_debugconf_value(
    const char* bytes,
    unsigned int size,
    const char* key,
    char* output,
    unsigned int capacity)
{
    char marker[96];
    const char* match;
    const char* cursor;
    const char* end;
    unsigned int output_size = 0;
    marker[0] = 0;
    dluma_str_append(marker, DLUMA_ARRAY_COUNT(marker), key);
    dluma_str_append(marker, DLUMA_ARRAY_COUNT(marker), "(\"");
    match = dluma_editor_find_icase(bytes, size, marker);
    if (!match || dluma_editor_find_icase(
            match + 1U, (unsigned int)(bytes + size - match - 1U), marker))
        return 0;
    cursor = match + strlen(marker);
    end = bytes + size;
    while (cursor < end) {
        char current = *cursor++;
        if (current == '"') break;
        if (!current || current == '\r' || current == '\n' ||
            output_size + 1U >= capacity)
            return 0;
        if (current == '\\') {
            if (cursor >= end || (*cursor != '\\' && *cursor != '"'))
                return 0;
            current = *cursor++;
        }
        output[output_size++] = current;
    }
    if (!output_size || cursor > end || cursor[-1] != '"' ||
        cursor >= end || *cursor != ')')
        return 0;
    output[output_size] = 0;
    return 1;
}

static int dluma_editor_read_debugconf(
    const WCHAR* player_directory,
    char* map_name,
    unsigned int map_capacity,
    WCHAR* selected_project,
    unsigned int selected_capacity)
{
    WCHAR data_directory[DLUMA_MAX_WPATH];
    WCHAR path[DLUMA_MAX_WPATH];
    WCHAR final_path[DLUMA_MAX_WPATH];
    WCHAR selected_lexical[DLUMA_MAX_WPATH];
    DlumaDirectoryIdentity player_identity;
    BYTE* bytes = 0;
    unsigned int size = 0;
    char sha256[DLUMA_SHA256_HEX_CAPACITY];
    char workshop_item[DLUMA_MAX_WPATH * 2U];
    int result = 0;
    if (!player_directory || !map_name || !selected_project) return 0;
    if (!dluma_open_directory_identity(player_directory, &player_identity) ||
        !dluma_checked_path_join(
            data_directory, DLUMA_ARRAY_COUNT(data_directory),
            player_directory, L"DW\\Data") ||
        !dluma_checked_path_join(
            path, DLUMA_ARRAY_COUNT(path), data_directory,
            L"debugconf_editor.scr"))
        return 0;
    {
        HANDLE file = CreateFileW(
            path, GENERIC_READ, FILE_SHARE_READ, 0, OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL, 0);
        BY_HANDLE_FILE_INFORMATION info;
        LARGE_INTEGER file_size;
        DWORD read = 0;
        memset(&info, 0, sizeof(info));
        if (file == INVALID_HANDLE_VALUE) return 0;
        if (!GetFileInformationByHandle(file, &info) ||
            (info.dwFileAttributes &
                (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) ||
            info.nNumberOfLinks != 1U ||
            !GetFileSizeEx(file, &file_size) || file_size.QuadPart <= 0 ||
            file_size.QuadPart > DLUMA_EDITOR_DEBUGCONF_MAX_BYTES ||
            !dluma_get_final_path_checked(
                file, final_path, DLUMA_ARRAY_COUNT(final_path)) ||
            !dluma_final_path_equal_or_under(
                final_path, player_identity.final_path)) {
            CloseHandle(file);
            return 0;
        }
        bytes = (BYTE*)dluma_alloc((SIZE_T)file_size.QuadPart + 1U);
        if (!bytes || !ReadFile(
                file, bytes, (DWORD)file_size.QuadPart, &read, 0) ||
            read != (DWORD)file_size.QuadPart) {
            if (bytes) dluma_free(bytes);
            CloseHandle(file);
            return 0;
        }
        bytes[read] = 0;
        size = read;
        dluma_sha256_bytes_hex(bytes, size, sha256);
        CloseHandle(file);
    }
    if (!dluma_editor_debugconf_value(
            (const char*)bytes, size, "AutoStart",
            map_name, map_capacity) ||
        !dluma_valid_map_segment(map_name) ||
        !dluma_editor_debugconf_value(
            (const char*)bytes, size, "WorkshopItem",
            workshop_item, DLUMA_ARRAY_COUNT(workshop_item)) ||
        !dluma_utf8_to_wide(
            workshop_item, selected_lexical,
            DLUMA_ARRAY_COUNT(selected_lexical)))
        goto done;
    {
        DlumaDirectoryIdentity selected_identity;
        if (!dluma_open_directory_identity(
                selected_lexical, &selected_identity))
            goto done;
        dluma_wstr_copy(selected_project, selected_capacity,
                      selected_identity.final_path);
    }
    result = 1;
done:
    if (bytes) dluma_free(bytes);
    return result;
}

static const WCHAR* dluma_editor_path_leaf(const WCHAR* path)
{
    const WCHAR* leaf = path;
    unsigned int index;
    if (!path) return 0;
    for (index = 0; path[index]; ++index)
        if (path[index] == L'\\' || path[index] == L'/')
            leaf = path + index + 1U;
    return leaf;
}

static const WCHAR* dluma_editor_skip_extended_path_prefix(
    const WCHAR* path)
{
    if (path && path[0] == L'\\' && path[1] == L'\\' &&
        path[2] == L'?' && path[3] == L'\\')
        return path + 4;
    return path;
}

static int dluma_editor_derive_player_directory(
    WCHAR* output,
    unsigned int capacity)
{
    DlumaDirectoryIdentity module_directory;
    WCHAR ancestor[DLUMA_MAX_WPATH];
    WCHAR candidate[DLUMA_MAX_WPATH];
    WCHAR player[DLUMA_MAX_WPATH];
    WCHAR player_final[DLUMA_MAX_WPATH];
    WCHAR player_directory[DLUMA_MAX_WPATH];
    unsigned int depth;
    if (!output || !capacity || !g_dluma.module_dir[0] ||
        !dluma_open_directory_identity(
            g_dluma.module_dir, &module_directory))
        return 0;
    output[0] = 0;
    dluma_wstr_copy(
        ancestor, DLUMA_ARRAY_COUNT(ancestor),
        module_directory.final_path);
    for (depth = 0; depth < 16U && ancestor[0]; ++depth) {
        const WCHAR* leaf = dluma_editor_path_leaf(ancestor);
        candidate[0] = 0;
        if (leaf && dluma_wstricmp(leaf, L"DevTools") == 0)
            dluma_wstr_copy(
                candidate, DLUMA_ARRAY_COUNT(candidate), ancestor);
        else if (leaf && dluma_wstricmp(leaf, L"steamapps") == 0 &&
                 !dluma_checked_path_join(
                     candidate, DLUMA_ARRAY_COUNT(candidate), ancestor,
                     L"common\\Dying Light\\DevTools"))
            return 0;
        if (candidate[0] &&
            dluma_checked_path_join(
                player, DLUMA_ARRAY_COUNT(player), candidate,
                L"DyingLightPlayer.exe") &&
            dluma_editor_validate_player(
                player, player_final, DLUMA_ARRAY_COUNT(player_final),
                player_directory, DLUMA_ARRAY_COUNT(player_directory))) {
            dluma_wstr_copy(output, capacity, player_directory);
            return output[0] != 0;
        }
        {
            SIZE_T before = wcslen(ancestor);
            dluma_path_parent(ancestor);
            if (!ancestor[0] || wcslen(ancestor) >= before) break;
        }
    }
    return 0;
}

int dluma_editor_launch_resolve_workshop_root(
    WCHAR* workshop_root,
    unsigned int workshop_capacity)
{
    WCHAR player_directory[DLUMA_MAX_WPATH];
    WCHAR lexical_root[DLUMA_MAX_WPATH];
    DlumaDirectoryIdentity root_identity;
    if (!workshop_root || !workshop_capacity)
        return 0;
    workshop_root[0] = 0;
    if (!dluma_editor_derive_player_directory(
            player_directory, DLUMA_ARRAY_COUNT(player_directory)) ||
        !dluma_checked_path_join(
            lexical_root, DLUMA_ARRAY_COUNT(lexical_root),
            player_directory, L"workshop") ||
        !dluma_open_directory_identity(
            lexical_root, &root_identity) ||
        wcslen(root_identity.final_path) + 1U >
            workshop_capacity)
        return 0;
    dluma_wstr_copy(
        workshop_root, workshop_capacity,
        root_identity.final_path);
    return workshop_root[0] != 0;
}

static void dluma_editor_log_project_resolution(
    LONG state,
    const char* detail)
{
    if (dluma_atomic_exchange(
            &g_dluma_editor_project_resolution_state, state) != state)
        dluma_log_pair(
            state == 6 ? "INFO" : "WARN",
            "editor_animation_active_project_resolution",
            detail);
}

int dluma_editor_launch_resolve_current_project(
    WCHAR* project_root,
    unsigned int project_capacity,
    char* map_name,
    unsigned int map_capacity)
{
    WCHAR player_directory[DLUMA_MAX_WPATH];
    WCHAR workshop_root[DLUMA_MAX_WPATH];
    WCHAR pattern[DLUMA_MAX_WPATH];
    WCHAR candidate[DLUMA_MAX_WPATH];
    WCHAR selected_project[DLUMA_MAX_WPATH];
    WCHAR debug_project[DLUMA_MAX_WPATH];
    char active_map[DLUMA_MAX_MAP_NAME];
    char debug_map[DLUMA_MAX_MAP_NAME];
    DlumaDirectoryIdentity workshop_identity;
    DlumaDirectoryIdentity candidate_identity;
    WIN32_FIND_DATAW data;
    HANDLE find = INVALID_HANDLE_VALUE;
    unsigned int scanned = 0;
    unsigned int matches = 0;
    int overflow = 0;
    if (!project_root || !project_capacity || !map_name || !map_capacity)
        return 0;
    project_root[0] = 0;
    map_name[0] = 0;
    active_map[0] = 0;
    if (!dluma_editor_animation_bridge_copy_active_document_map(
            active_map, DLUMA_ARRAY_COUNT(active_map))) {
        dluma_editor_log_project_resolution(
            1, "active_document_map_unavailable");
        return 0;
    }
    if (!dluma_editor_derive_player_directory(
            player_directory, DLUMA_ARRAY_COUNT(player_directory)) ||
        !dluma_checked_path_join(
            workshop_root,
            DLUMA_ARRAY_COUNT(workshop_root),
            player_directory,
            L"workshop") ||
        !dluma_open_directory_identity(
            workshop_root, &workshop_identity) ||
        !dluma_checked_path_join(
            pattern,
            DLUMA_ARRAY_COUNT(pattern),
            workshop_identity.final_path,
            L"*")) {
        dluma_editor_log_project_resolution(
            2, "validated_workshop_root_unavailable");
        return 0;
    }
    selected_project[0] = 0;
    find = FindFirstFileW(pattern, &data);
    if (find == INVALID_HANDLE_VALUE) {
        dluma_editor_log_project_resolution(
            3, "workshop_project_scan_failed");
        return 0;
    }
    do {
        if (dluma_wstricmp(data.cFileName, L".") == 0 ||
            dluma_wstricmp(data.cFileName, L"..") == 0)
            continue;
        if (++scanned > DLUMA_EDITOR_WORKSHOP_PROJECT_LIMIT) {
            overflow = 1;
            break;
        }
        if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0 ||
            (data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
            continue;
        if (!dluma_checked_path_join(
                candidate,
                DLUMA_ARRAY_COUNT(candidate),
                workshop_identity.final_path,
                data.cFileName) ||
            !dluma_project_candidate_root_is_contained(
                workshop_identity.final_path, candidate) ||
            !dluma_open_directory_identity(
                candidate, &candidate_identity) ||
            !dluma_validate_map_anchor_under_project(
                candidate, active_map, &candidate_identity))
            continue;
        ++matches;
        if (matches == 1U)
            dluma_wstr_copy(
                selected_project,
                DLUMA_ARRAY_COUNT(selected_project),
                candidate_identity.final_path);
        if (matches > 1U) break;
    } while (FindNextFileW(find, &data));
    FindClose(find);
    if (overflow) {
        dluma_editor_log_project_resolution(
            3, "workshop_project_scan_bound_exceeded");
        return 0;
    }
    if (!matches) {
        dluma_editor_log_project_resolution(
            4, "active_document_project_not_found");
        return 0;
    }
    if (matches != 1U) {
        dluma_editor_log_project_resolution(
            5, "active_document_project_ambiguous");
        return 0;
    }
    if (wcslen(selected_project) + 1U > project_capacity ||
        strlen(active_map) + 1U > map_capacity)
        return 0;
    dluma_wstr_copy(
        project_root, project_capacity, selected_project);
    dluma_str_copy(map_name, map_capacity, active_map);
    debug_map[0] = 0;
    debug_project[0] = 0;
    if (dluma_editor_read_debugconf(
            player_directory,
            debug_map,
            DLUMA_ARRAY_COUNT(debug_map),
            debug_project,
            DLUMA_ARRAY_COUNT(debug_project))) {
        dluma_log_pair(
            "INFO",
            "editor_animation_debugconf_context",
            dluma_stricmp(debug_map, active_map) == 0 &&
                dluma_project_roots_same_identity(
                    debug_project, selected_project)
                ? "confirmed_active_document"
                : "stale_context_ignored");
    }
    dluma_editor_log_project_resolution(
        6, "active_document_project_resolved");
    return project_root[0] != 0 && map_name[0] != 0;
}

static int dluma_editor_selected_loader_matches(
    const char* command,
    unsigned int command_size,
    const char* current_directory)
{
    const DlumaDualHostRuntimeSnapshot* runtime =
        dluma_dual_host_runtime_snapshot();
    char mod_value[DLUMA_MAX_WPATH * 2U];
    WCHAR mod_wide[DLUMA_MAX_WPATH];
    WCHAR directory[DLUMA_MAX_WPATH];
    WCHAR selected_lexical[DLUMA_MAX_WPATH];
    WCHAR selected_absolute[DLUMA_MAX_WPATH];
    WCHAR selected_final[DLUMA_MAX_WPATH];
    WCHAR loader_lexical[DLUMA_MAX_WPATH];
    WCHAR loader_absolute[DLUMA_MAX_WPATH];
    WCHAR loader_final[DLUMA_MAX_WPATH];
    BY_HANDLE_FILE_INFORMATION selected_info;
    BY_HANDLE_FILE_INFORMATION loader_info;
    HANDLE selected_file = INVALID_HANDLE_VALUE;
    HANDLE loader_file = INVALID_HANDLE_VALUE;
    DWORD selected_length;
    DWORD loader_length;
    DWORD selected_attributes;
    DWORD loader_attributes;
    char selected_sha256[DLUMA_SHA256_HEX_CAPACITY];
    int result = 0;
    memset(&selected_info, 0, sizeof(selected_info));
    memset(&loader_info, 0, sizeof(loader_info));
    if (!dluma_editor_extract_command_token(
            command, command_size, "/mod=", mod_value,
            DLUMA_ARRAY_COUNT(mod_value)) ||
        !dluma_utf8_to_wide(mod_value, mod_wide, DLUMA_ARRAY_COUNT(mod_wide)) ||
        !runtime || !runtime->evidence.loader_sha256[0] ||
        !dluma_get_module_path_checked(
            g_dluma.self_module, loader_lexical,
            DLUMA_ARRAY_COUNT(loader_lexical)))
        goto done;
    if ((mod_wide[0] && mod_wide[1] == L':') ||
        (mod_wide[0] == L'\\' && mod_wide[1] == L'\\')) {
        dluma_wstr_copy(
            selected_lexical, DLUMA_ARRAY_COUNT(selected_lexical), mod_wide);
    } else {
        if (current_directory && current_directory[0]) {
            if (!dluma_utf8_to_wide(
                    current_directory, directory,
                    DLUMA_ARRAY_COUNT(directory)))
                goto done;
        } else {
            DWORD directory_length = GetCurrentDirectoryW(
                DLUMA_ARRAY_COUNT(directory), directory);
            if (!directory_length ||
                directory_length >= DLUMA_ARRAY_COUNT(directory))
                goto done;
        }
        if (!dluma_checked_path_join(
                selected_lexical, DLUMA_ARRAY_COUNT(selected_lexical),
                directory, mod_wide))
            goto done;
    }
    selected_length = GetFullPathNameW(
        selected_lexical, DLUMA_ARRAY_COUNT(selected_absolute),
        selected_absolute, 0);
    loader_length = GetFullPathNameW(
        loader_lexical, DLUMA_ARRAY_COUNT(loader_absolute),
        loader_absolute, 0);
    if (!selected_length ||
        selected_length >= DLUMA_ARRAY_COUNT(selected_absolute) ||
        !loader_length || loader_length >= DLUMA_ARRAY_COUNT(loader_absolute))
        goto done;
    selected_attributes = GetFileAttributesW(selected_absolute);
    loader_attributes = GetFileAttributesW(loader_absolute);
    if (selected_attributes == INVALID_FILE_ATTRIBUTES ||
        loader_attributes == INVALID_FILE_ATTRIBUTES ||
        (selected_attributes &
            (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) ||
        (loader_attributes &
            (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)))
        goto done;
    selected_file = CreateFileW(
        selected_absolute, GENERIC_READ, FILE_SHARE_READ, 0, OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, 0);
    loader_file = CreateFileW(
        loader_absolute, GENERIC_READ, FILE_SHARE_READ, 0, OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, 0);
    if (selected_file == INVALID_HANDLE_VALUE ||
        loader_file == INVALID_HANDLE_VALUE ||
        !GetFileInformationByHandle(selected_file, &selected_info) ||
        !GetFileInformationByHandle(loader_file, &loader_info) ||
        (selected_info.dwFileAttributes &
            (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) ||
        (loader_info.dwFileAttributes &
            (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) ||
        selected_info.nNumberOfLinks != 1U ||
        loader_info.nNumberOfLinks != 1U ||
        !dluma_get_final_path_checked(
            selected_file, selected_final,
            DLUMA_ARRAY_COUNT(selected_final)) ||
        !dluma_get_final_path_checked(
            loader_file, loader_final, DLUMA_ARRAY_COUNT(loader_final)) ||
        dluma_wstricmp(
            dluma_editor_skip_extended_path_prefix(selected_absolute),
            dluma_editor_skip_extended_path_prefix(selected_final)) != 0 ||
        dluma_wstricmp(
            dluma_editor_skip_extended_path_prefix(loader_absolute),
            dluma_editor_skip_extended_path_prefix(loader_final)) != 0 ||
        dluma_wstricmp(selected_final, loader_final) != 0 ||
        selected_info.dwVolumeSerialNumber !=
            loader_info.dwVolumeSerialNumber ||
        selected_info.nFileIndexHigh != loader_info.nFileIndexHigh ||
        selected_info.nFileIndexLow != loader_info.nFileIndexLow ||
        !dluma_sha256_handle_hex(
            selected_file, DLUMA_EDITOR_LOADER_MAX_BYTES,
            selected_sha256) ||
        dluma_stricmp(
            selected_sha256, runtime->evidence.loader_sha256) != 0)
        goto done;
    result = 1;
done:
    if (selected_file != INVALID_HANDLE_VALUE) CloseHandle(selected_file);
    if (loader_file != INVALID_HANDLE_VALUE) CloseHandle(loader_file);
    return result;
}

static void dluma_editor_sort_approvals(
    DlumaHandoffApprovalRecord* output,
    unsigned int count)
{
    unsigned int index;
    for (index = 1U; index < count; ++index) {
        DlumaHandoffApprovalRecord value = output[index];
        unsigned int cursor = index;
        while (cursor &&
               (dluma_stricmp(
                    output[cursor - 1U].mod_id, value.mod_id) > 0 ||
                (dluma_stricmp(
                     output[cursor - 1U].mod_id, value.mod_id) == 0 &&
                 dluma_stricmp(
                     output[cursor - 1U].language, value.language) > 0))) {
            output[cursor] = output[cursor - 1U];
            --cursor;
        }
        output[cursor] = value;
    }
}

static int dluma_editor_approval_inventory(
    DlumaHandoffApprovalRecord* output,
    unsigned int capacity,
    unsigned int* count_out)
{
    DlumaNativeModSnapshot* items;
    DlumaLuaMetadata* lua_items = 0;
    unsigned int item_count;
    unsigned int index;
    unsigned int count = 0;
    if (!output || !capacity || !count_out) return 0;
    *count_out = 0;
    items = (DlumaNativeModSnapshot*)dluma_alloc(
        DLUMA_MAX_NATIVE_MODS * sizeof(*items));
    if (!items) return 0;
    memset(items, 0, DLUMA_MAX_NATIVE_MODS * sizeof(*items));
    item_count = dluma_native_mods_snapshot(
        items, DLUMA_MAX_NATIVE_MODS, 0);
    for (index = 0; index < item_count; ++index) {
        const DlumaNativeModSnapshot* item = &items[index];
        if (item->duplicate_id ||
            !(item->requested_host_flags & DLUMA_NATIVE_HOST_PLAYER) ||
            !item->player_approval_valid || !item->manifest_present)
            continue;
        if (count >= capacity || !item->id[0] ||
            !item->player_approval_fingerprint[0] ||
            !item->manifest_sha256[0] || !item->module_sha256[0]) {
            dluma_free(items);
            return 0;
        }
        dluma_str_copy(output[count].mod_id,
                     DLUMA_ARRAY_COUNT(output[count].mod_id), item->id);
        dluma_str_copy(output[count].manifest_sha256,
                     DLUMA_ARRAY_COUNT(output[count].manifest_sha256),
                     item->manifest_sha256);
        dluma_str_copy(output[count].approval_fingerprint,
                     DLUMA_ARRAY_COUNT(output[count].approval_fingerprint),
                     item->player_approval_fingerprint);
        dluma_str_copy(output[count].artifact_sha256,
                     DLUMA_ARRAY_COUNT(output[count].artifact_sha256),
                     item->module_sha256);
        dluma_str_copy(
            output[count].language,
            DLUMA_ARRAY_COUNT(output[count].language),
            item->kind == DLUMA_MOD_KIND_CSHARP ? "csharp" : "native");
        ++count;
    }
    dluma_free(items);
    lua_items = (DlumaLuaMetadata*)dluma_alloc(
        DLUMA_MAX_LUA_FILES * sizeof(*lua_items));
    if (!lua_items) return 0;
    memset(lua_items, 0, DLUMA_MAX_LUA_FILES * sizeof(*lua_items));
    item_count = dluma_lua_snapshot_globals(
        lua_items, DLUMA_MAX_LUA_FILES, 0);
    if (item_count > DLUMA_MAX_LUA_FILES) {
        dluma_free(lua_items);
        return 0;
    }
    for (index = 0; index < item_count; ++index) {
        const DlumaLuaMetadata* item = &lua_items[index];
        if (!item->valid || item->duplicate_id || !item->manifest_present ||
            !(item->declared_host_flags & DLUMA_MOD_HOST_PLAYER) ||
            !item->enabled_requested || !item->approval_valid)
            continue;
        if (count >= capacity || !item->id[0] ||
            !item->approved_fingerprint[0] ||
            !item->manifest_sha256[0] || !item->content_sha256[0]) {
            dluma_free(lua_items);
            return 0;
        }
        dluma_str_copy(output[count].mod_id,
                     DLUMA_ARRAY_COUNT(output[count].mod_id), item->id);
        dluma_str_copy(output[count].language,
                     DLUMA_ARRAY_COUNT(output[count].language), "lua");
        dluma_str_copy(output[count].approval_fingerprint,
                     DLUMA_ARRAY_COUNT(output[count].approval_fingerprint),
                     item->approved_fingerprint);
        dluma_str_copy(output[count].manifest_sha256,
                     DLUMA_ARRAY_COUNT(output[count].manifest_sha256),
                     item->manifest_sha256);
        dluma_str_copy(output[count].artifact_sha256,
                     DLUMA_ARRAY_COUNT(output[count].artifact_sha256),
                     item->content_sha256);
        ++count;
    }
    dluma_free(lua_items);
    dluma_editor_sort_approvals(output, count);
    for (index = 1; index < count; ++index)
        if (dluma_stricmp(
                output[index - 1U].mod_id, output[index].mod_id) == 0)
            return 0;
    *count_out = count;
    return 1;
}

static int dluma_editor_publish_playtest(
    const char* application_name,
    const char* command_line,
    const char* current_directory,
    int command_is_arguments,
    DlumaEditorPresencePlaytestLaunch* presence_launch)
{
    const DlumaDualHostRuntimeSnapshot* runtime =
        dluma_dual_host_runtime_snapshot();
    unsigned int command_size = dluma_editor_bounded_length(
        command_line, DLUMA_EDITOR_LAUNCH_COMMAND_MAX);
    WCHAR player_lexical[DLUMA_MAX_WPATH];
    WCHAR player_final[DLUMA_MAX_WPATH];
    WCHAR player_directory[DLUMA_MAX_WPATH];
    WCHAR selected_project[DLUMA_MAX_WPATH];
    WCHAR project_root[DLUMA_MAX_WPATH];
    char map_name[DLUMA_MAX_MAP_NAME];
    char project_name[128];
    char workshop_id[64];
    unsigned char is_workshop = 0;
    DlumaHandoffApprovalRecord* approvals = 0;
    DlumaHandoffSidecarRecord* sidecars = 0;
    unsigned int approval_count = 0;
    unsigned int sidecar_count = 0;
    DlumaPlaytestHandoff handoff;
    ULONGLONG generation;
    const char* rejection = "initial evidence";
    int candidate = 0;
    int result = 0;
    memset(&handoff, 0, sizeof(handoff));
    if (presence_launch)
        memset(presence_launch, 0, sizeof(*presence_launch));
    if (!runtime || !runtime->process_start_time || !command_line ||
        !command_size || command_size >= DLUMA_EDITOR_LAUNCH_COMMAND_MAX)
        goto done;
    if (!dluma_editor_find_icase(
            command_line, command_size, "debugconf_editor.scr"))
        goto done;
    candidate = 1;
    rejection = "player path resolution";
    if (!dluma_editor_resolve_process_path(
            application_name, command_line, command_size,
            current_directory, player_lexical,
            DLUMA_ARRAY_COUNT(player_lexical)))
        goto done;
    rejection = "player profile validation";
    if (!dluma_editor_validate_player(
            player_lexical, player_final, DLUMA_ARRAY_COUNT(player_final),
            player_directory, DLUMA_ARRAY_COUNT(player_directory)))
        goto done;
    rejection = "editor debug configuration";
    if (!dluma_editor_read_debugconf(
            player_directory, map_name, DLUMA_ARRAY_COUNT(map_name),
            selected_project, DLUMA_ARRAY_COUNT(selected_project)))
        goto done;
    rejection = "selected loader identity";
    if (!dluma_editor_selected_loader_matches(
            command_line, command_size, current_directory))
        goto done;
    rejection = "project map anchor";
    if (!dluma_project_map_anchor_is_contained(selected_project, map_name))
        goto done;
    dluma_wstr_copy(
        project_root, DLUMA_ARRAY_COUNT(project_root), selected_project);
    dluma_describe_project_source(
        project_root, project_name, DLUMA_ARRAY_COUNT(project_name),
        workshop_id, DLUMA_ARRAY_COUNT(workshop_id), &is_workshop);
    rejection = "project identity description";
    if (!project_name[0] || !dluma_wide_to_utf8(
            project_root, handoff.project_root_utf8,
            DLUMA_ARRAY_COUNT(handoff.project_root_utf8)))
        goto done;
    approvals = (DlumaHandoffApprovalRecord*)dluma_alloc(
        DLUMA_HANDOFF_MAX_APPROVALS * sizeof(*approvals));
    sidecars = (DlumaHandoffSidecarRecord*)dluma_alloc(
        DLUMA_HANDOFF_MAX_SIDECARS * sizeof(*sidecars));
    rejection = "inventory allocation";
    if (!approvals || !sidecars) goto done;
    memset(approvals, 0,
           DLUMA_HANDOFF_MAX_APPROVALS * sizeof(*approvals));
    memset(sidecars, 0,
           DLUMA_HANDOFF_MAX_SIDECARS * sizeof(*sidecars));
    rejection = "approval inventory";
    if (!dluma_editor_approval_inventory(
            approvals, DLUMA_HANDOFF_MAX_APPROVALS, &approval_count))
        goto done;
    rejection = "sidecar inventory";
    if (!dluma_quest_phase_inventory_project(
            project_root, sidecars, DLUMA_HANDOFF_MAX_SIDECARS,
            &sidecar_count))
        goto done;
    handoff.struct_size = sizeof(handoff);
    handoff.schema_version = 1U;
    handoff.parent_pid = GetCurrentProcessId();
    handoff.parent_start_time = runtime->process_start_time;
    handoff.created_ms = GetTickCount64();
    handoff.expires_ms = handoff.created_ms + DLUMA_HANDOFF_EXPIRY_MS;
    generation = (ULONGLONG)_InterlockedIncrement64(
        &g_dluma_editor_launch.next_generation);
    if (!generation) generation = (ULONGLONG)_InterlockedIncrement64(
        &g_dluma_editor_launch.next_generation);
    handoff.generation = generation;
    rejection = "command line hash";
    if (!dluma_editor_command_hash(
            command_line, command_size, command_is_arguments,
            handoff.command_line_sha256))
        goto done;
    dluma_str_copy(handoff.loader_sha256,
                 DLUMA_ARRAY_COUNT(handoff.loader_sha256),
                 runtime->evidence.loader_sha256);
    dluma_str_copy(handoff.project_name,
                 DLUMA_ARRAY_COUNT(handoff.project_name), project_name);
    dluma_str_copy(handoff.map_name,
                 DLUMA_ARRAY_COUNT(handoff.map_name), map_name);
    rejection = "editor context callback";
    if (!dluma_native_mods_editor_context_changed(
            project_root, project_name, map_name, generation))
        goto done;
    rejection = "handoff publication";
    if (!dluma_dual_host_runtime_publish_handoff_file(
            &handoff, approvals, approval_count,
            sidecars, sidecar_count, 0, 0))
        goto done;
    dluma_log_u64("INFO", "editor_playtest_generation", generation);
    dluma_log_u64("INFO", "editor_playtest_sidecars", sidecar_count);
    dluma_log_u64("INFO", "editor_playtest_approved_mods", approval_count);
    dluma_log_pair("INFO", "editor_playtest_project_root",
                 handoff.project_root_utf8);
    dluma_log_pair("INFO", "editor_playtest_map", map_name);
    result = 1;
    if (presence_launch) {
        presence_launch->handoff = handoff;
        dluma_wstr_copy(presence_launch->player_path,
                      DLUMA_ARRAY_COUNT(presence_launch->player_path),
                      player_final);
    }
done:
    if (!result && candidate) {
        dluma_log_pair("WARN", "editor_playtest_handoff_rejected", rejection);
    }
    if (approvals) dluma_free(approvals);
    if (sidecars) dluma_free(sidecars);
    return result;
}

static BOOL WINAPI dluma_editor_create_process_hook(
    LPCSTR application_name,
    LPSTR command_line,
    LPVOID process_attributes,
    LPVOID thread_attributes,
    BOOL inherit_handles,
    DWORD creation_flags,
    LPVOID environment,
    LPCSTR current_directory,
    LPVOID startup_info,
    LPVOID process_information)
{
    DlumaCreateProcessAFn original;
    DlumaEditorPresencePlaytestLaunch presence_launch;
    BOOL result;
    int published = 0;
    int tracked = dluma_editor_host_callback_enter();
    dluma_atomic_increment(&g_dluma_editor_launch.active_calls);
    original = (DlumaCreateProcessAFn)dluma_atomic_pointer_compare_exchange(
        (PVOID volatile*)&g_dluma_editor_launch.original, 0, 0);
    if (dluma_atomic_compare_exchange(
            &g_dluma_editor_launch.installed, 0, 0) == 2 &&
        !dluma_atomic_compare_exchange(
            &g_dluma_editor_launch.stopping, 0, 0) &&
        command_line && dluma_editor_publish_playtest(
            application_name, command_line, current_directory,
            application_name && application_name[0],
            &presence_launch)) {
        published = 1;
        _InterlockedIncrement64(&g_dluma_editor_launch.published_count);
    } else {
        _InterlockedIncrement64(&g_dluma_editor_launch.rejected_count);
    }
    result = original ? original(
        application_name, command_line, process_attributes,
        thread_attributes, inherit_handles, creation_flags,
        environment, current_directory, startup_info,
        process_information) : FALSE;
    dluma_atomic_decrement(&g_dluma_editor_launch.active_calls);
    if (result && published)
        dluma_editor_presence_note_playtest_launch(
            &presence_launch, process_information);
    dluma_editor_host_callback_leave(tracked);
    return result;
}

static HINSTANCE WINAPI dluma_editor_shell_execute_hook(
    HWND window,
    LPCSTR operation,
    LPCSTR file,
    LPCSTR parameters,
    LPCSTR directory,
    int show_command)
{
    DlumaShellExecuteAFn original;
    HINSTANCE result;
    DlumaEditorPresencePlaytestLaunch presence_launch;
    int tracked = dluma_editor_host_callback_enter();
    int published = 0;
    dluma_atomic_increment(&g_dluma_editor_launch.active_calls);
    original = (DlumaShellExecuteAFn)dluma_atomic_pointer_compare_exchange(
        (PVOID volatile*)&g_dluma_editor_launch.original_shell_execute, 0, 0);
    if (dluma_atomic_compare_exchange(
            &g_dluma_editor_launch.installed, 0, 0) == 2 &&
        !dluma_atomic_compare_exchange(
            &g_dluma_editor_launch.stopping, 0, 0) &&
        file && parameters && dluma_editor_publish_playtest(
            file, parameters, directory, 1, &presence_launch)) {
        published = 1;
        _InterlockedIncrement64(&g_dluma_editor_launch.published_count);
    } else {
        _InterlockedIncrement64(&g_dluma_editor_launch.rejected_count);
    }
    result = original ? original(
        window, operation, file, parameters, directory, show_command) : 0;
    dluma_atomic_decrement(&g_dluma_editor_launch.active_calls);
    dluma_editor_host_callback_leave(tracked);
    if ((ULONG_PTR)result > 32U && published)
        dluma_editor_presence_note_playtest_launch(
            &presence_launch, 0);
    return result;
}

static int dluma_editor_replace_iat_target(
    void** slot,
    void* expected,
    void* replacement)
{
    DWORD old_protect = 0;
    DWORD restored_protect = 0;
    if (!slot || !expected || !replacement ||
        !dluma_is_readable(slot, sizeof(void*)) || *slot != expected ||
        !VirtualProtect(
            slot, sizeof(void*), PAGE_READWRITE, &old_protect))
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

static int dluma_editor_restore_iat_target(
    void** slot,
    void* hook,
    void* original)
{
    if (!slot || !hook || !original ||
        !dluma_is_readable(slot, sizeof(void*)))
        return 0;
    if (*slot == original) return 1;
    return *slot == hook &&
        dluma_editor_replace_iat_target(slot, hook, original);
}

int dluma_editor_launch_hook_start(void)
{
    void** slot;
    void** shell_slot;
    void* original;
    void* shell_original;
    LONG state;
    if (!dluma_dual_host_runtime_is_editor()) return 0;
    state = dluma_atomic_compare_exchange(
        &g_dluma_editor_launch.installed, 1, 0);
    if (state != 0) return state == 2;
    slot = dluma_editor_find_create_process_iat();
    shell_slot = dluma_editor_find_shell_execute_iat();
    if (!slot || !shell_slot || slot == shell_slot)
        goto failed;
    original = *slot;
    shell_original = *shell_slot;
    g_dluma_editor_launch.iat_slot = slot;
    g_dluma_editor_launch.original = (DlumaCreateProcessAFn)original;
    g_dluma_editor_launch.shell_execute_iat_slot = shell_slot;
    g_dluma_editor_launch.original_shell_execute =
        (DlumaShellExecuteAFn)shell_original;
    if (!dluma_editor_replace_iat_target(
            slot, original, (void*)dluma_editor_create_process_hook) ||
        !dluma_editor_replace_iat_target(
            shell_slot, shell_original,
            (void*)dluma_editor_shell_execute_hook))
        goto failed;
    dluma_atomic_exchange(&g_dluma_editor_launch.detached, 0);
    dluma_atomic_exchange(&g_dluma_editor_launch.stopping, 0);
    dluma_atomic_exchange(&g_dluma_editor_launch.installed, 2);
    dluma_log_line(
        "INFO",
        "Validated Chrome Editor CreateProcessA and ShellExecuteA playtest hooks installed.");
    return 1;
failed:
    if ((!g_dluma_editor_launch.iat_slot ||
         dluma_editor_restore_iat_target(
            g_dluma_editor_launch.iat_slot,
            (void*)dluma_editor_create_process_hook,
            (void*)g_dluma_editor_launch.original)) &&
        (!g_dluma_editor_launch.shell_execute_iat_slot ||
         dluma_editor_restore_iat_target(
            g_dluma_editor_launch.shell_execute_iat_slot,
            (void*)dluma_editor_shell_execute_hook,
            (void*)g_dluma_editor_launch.original_shell_execute))) {
        g_dluma_editor_launch.iat_slot = 0;
        g_dluma_editor_launch.original = 0;
        g_dluma_editor_launch.shell_execute_iat_slot = 0;
        g_dluma_editor_launch.original_shell_execute = 0;
        dluma_atomic_exchange(&g_dluma_editor_launch.installed, -1);
    } else {
        dluma_atomic_exchange(&g_dluma_editor_launch.stopping, 1);
        dluma_atomic_exchange(&g_dluma_editor_launch.installed, -2);
    }
    dluma_log_line(
        "WARN",
        "Chrome Editor launch IAT ownership validation failed; playtest handoff is disabled.");
    return 0;
}

int dluma_editor_launch_hook_stop(void)
{
    void** slot = g_dluma_editor_launch.iat_slot;
    void* original = (void*)g_dluma_editor_launch.original;
    void** shell_slot = g_dluma_editor_launch.shell_execute_iat_slot;
    void* shell_original =
        (void*)g_dluma_editor_launch.original_shell_execute;
    ULONGLONG started;
    LONG state = dluma_atomic_compare_exchange(
        &g_dluma_editor_launch.installed, 0, 0);
    if (state == 0 || state == -1)
        return 1;
    if (state != 2 && state != -2) return 0;
    dluma_atomic_exchange(&g_dluma_editor_launch.stopping, 1);
    if (!slot || !shell_slot ||
        !dluma_editor_restore_iat_target(
            slot, (void*)dluma_editor_create_process_hook, original) ||
        !dluma_editor_restore_iat_target(
            shell_slot, (void*)dluma_editor_shell_execute_hook,
            shell_original))
        return 0;
    dluma_atomic_exchange(&g_dluma_editor_launch.detached, 1);
    started = GetTickCount64();
    while (dluma_atomic_compare_exchange(
               &g_dluma_editor_launch.active_calls, 0, 0) != 0 &&
           GetTickCount64() - started < 2000ULL)
        Sleep(1);
    if (dluma_atomic_compare_exchange(
            &g_dluma_editor_launch.active_calls, 0, 0) != 0)
        return 0;
    g_dluma_editor_launch.iat_slot = 0;
    g_dluma_editor_launch.original = 0;
    g_dluma_editor_launch.shell_execute_iat_slot = 0;
    g_dluma_editor_launch.original_shell_execute = 0;
    dluma_atomic_exchange(&g_dluma_editor_launch.installed, 0);
    return 1;
}

int dluma_editor_launch_hook_callbacks_drained(void)
{
    return dluma_atomic_compare_exchange(
               &g_dluma_editor_launch.active_calls, 0, 0) == 0;
}

int dluma_editor_launch_hook_snapshot(DlumaEditorLaunchHookSnapshot* output)
{
    if (!output) return 0;
    memset(output, 0, sizeof(*output));
    output->iat_slot = (ULONG_PTR)g_dluma_editor_launch.iat_slot;
    output->original_target = (ULONG_PTR)g_dluma_editor_launch.original;
    output->shell_execute_iat_slot =
        (ULONG_PTR)g_dluma_editor_launch.shell_execute_iat_slot;
    output->shell_execute_original_target =
        (ULONG_PTR)g_dluma_editor_launch.original_shell_execute;
    output->next_generation = (ULONGLONG)_InterlockedCompareExchange64(
        &g_dluma_editor_launch.next_generation, 0, 0);
    output->published_count = (ULONGLONG)_InterlockedCompareExchange64(
        &g_dluma_editor_launch.published_count, 0, 0);
    output->rejected_count = (ULONGLONG)_InterlockedCompareExchange64(
        &g_dluma_editor_launch.rejected_count, 0, 0);
    output->installed = (unsigned int)dluma_atomic_compare_exchange(
        &g_dluma_editor_launch.installed, 0, 0);
    output->stopping = (unsigned int)dluma_atomic_compare_exchange(
        &g_dluma_editor_launch.stopping, 0, 0);
    output->active_calls = (unsigned int)dluma_atomic_compare_exchange(
        &g_dluma_editor_launch.active_calls, 0, 0);
    output->detached = (unsigned int)dluma_atomic_compare_exchange(
        &g_dluma_editor_launch.detached, 0, 0);
    return 1;
}
