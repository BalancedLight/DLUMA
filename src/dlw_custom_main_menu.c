#include "dlw_custom_main_menu.h"

#include "dlw_companion_archive.h"
#include "dlw_asset_mods.h"
#include "dlw_pack.h"
#include "dlw_path_security.h"

#define DLUMA_CUSTOM_MENU_MAX_STAGE_FILES 4096U
#define DLUMA_CUSTOM_MENU_MAX_TREE_DEPTH 32U
#define DLUMA_CUSTOM_MENU_MAX_RUNTIME_FILE_BYTES (512ULL * 1024ULL * 1024ULL)
#define DLUMA_CUSTOM_MENU_MAX_XUI_BYTES (2ULL * 1024ULL * 1024ULL)
#define DLUMA_CUSTOM_MENU_MAX_BANK_BYTES (512ULL * 1024ULL * 1024ULL)
#define DLUMA_CUSTOM_MENU_MAX_BANK_TOTAL_BYTES (512ULL * 1024ULL * 1024ULL)
#define DLUMA_CUSTOM_MENU_MAX_PROMOTED_BYTES (64ULL * 1024ULL * 1024ULL)
#define DLUMA_CUSTOM_MENU_STAGE_COPY_BUFFER_BYTES (1024U * 1024U)
#define DLUMA_CUSTOM_MENU_TEMP_FINGERPRINT_CHARS 16U
#define DLUMA_CUSTOM_MENU_MAIN_XUI_DIGEST_CHARS 16U
#define DLUMA_CUSTOM_MENU_MAIN_XUI_COMPAT "DLM_Main_PC_workshop.xui"
#ifndef ERROR_ALREADY_EXISTS
#define ERROR_ALREADY_EXISTS 183UL
#endif
#ifndef ERROR_INVALID_DATA
#define ERROR_INVALID_DATA 13UL
#endif
#ifndef ERROR_FILE_EXISTS
#define ERROR_FILE_EXISTS 80UL
#endif
#ifndef ERROR_BUFFER_OVERFLOW
#define ERROR_BUFFER_OVERFLOW 111UL
#endif

typedef struct DlumaCustomMenuCandidate
{
    DlumaCustomMainMenuCandidateSnapshot snapshot;
    DlumaCustomMainMenuManifest manifest;
    unsigned char *manifest_bytes;
    unsigned int manifest_size;
    char manifest_sha256[DLUMA_SHA256_HEX_CAPACITY];
    WCHAR canonical_root[DLUMA_MAX_WPATH];
    BY_HANDLE_FILE_INFORMATION root_identity;
    char staging_identity[DLUMA_SHA256_HEX_CAPACITY];
} DlumaCustomMenuCandidate;

typedef struct DlumaCustomMenuStageFile
{
    HANDLE source;
    BY_HANDLE_FILE_INFORMATION source_identity;
    WCHAR source_path[DLUMA_MAX_WPATH];
    WCHAR destination_relative[DLUMA_MAX_WPATH];
    char destination_utf8[DLUMA_MAX_WPATH * 2U];
    char output_sha256[DLUMA_SHA256_HEX_CAPACITY];
    char asset_owner[DLUMA_ASSET_MOD_ID_CAPACITY];
    ULONGLONG source_size;
    ULONGLONG output_size;
    DWORD output_crc32;
    unsigned char promote_root;
    unsigned char overlay_output;
    unsigned char declared_bank_index_plus_one;
    /* An empty overlay record with no source file.  It shadows a stock level
     * companion script the selected project does not provide, so the published
     * level does not inherit one belonging to the stock level. */
    unsigned char neutral_stub;
} DlumaCustomMenuStageFile;

typedef struct DlumaCustomMenuPreparedBank
{
    DlumaCompanionArchiveMaterializedBank materialized;
    DlumaCompanionArchiveAudioManifest catalog;
    char alias[DLUMA_COMPANION_ARCHIVE_MAX_ENGINE_BANK_NAME];
    char sha256[DLUMA_SHA256_HEX_CAPACITY];
    char owner_id[DLUMA_PROCESS_SOURCE_OWNER_ID_CAPACITY];
    unsigned char mounted;
    unsigned char reserved[3];
} DlumaCustomMenuPreparedBank;

typedef struct DlumaCustomMenuStageInventory
{
    DlumaCustomMenuStageFile *files;
    unsigned int count;
    ULONGLONG total_source_bytes;
    unsigned int overlay_file_count;
    unsigned int data_file_count;
    unsigned int assets_pc_file_count;
    unsigned int fx_definition_count;
    unsigned int material_pack_count;
    unsigned int compiled_mesh_count;
    unsigned int map_found;
    unsigned int exp_found;
    char diagnostic[DLUMA_CUSTOM_MENU_DIAGNOSTIC_CAPACITY];
    ULONGLONG asset_revision;
} DlumaCustomMenuStageInventory;

static DlumaCustomMenuCandidate g_dluma_custom_menu_candidates[
    DLUMA_CUSTOM_MENU_MAX_CANDIDATES];
static unsigned int g_dluma_custom_menu_candidate_count;
static unsigned int g_dluma_custom_menu_candidate_total;
static unsigned char g_dluma_custom_menu_candidate_overflow;
static DlumaCustomMenuCandidate *g_dluma_custom_menu_selected;
static char g_dluma_custom_menu_requested_id[DLUMA_CUSTOM_MENU_ID_CAPACITY];
static char g_dluma_custom_menu_requested_source[512];
static WCHAR g_dluma_custom_menu_overlay_path[DLUMA_MAX_WPATH];
static unsigned char g_dluma_custom_menu_sources_prepared;
static unsigned char g_dluma_custom_menu_sources_mounted;
static DlumaCustomMenuPreparedBank g_dluma_custom_menu_banks[
    DLUMA_CUSTOM_MENU_MAX_BANKS];
static unsigned int g_dluma_custom_menu_bank_count;
static unsigned char g_dluma_custom_menu_activation_pending;
static ULONGLONG g_dluma_custom_menu_asset_revision;
static ULONGLONG g_dluma_custom_menu_asset_refresh_attempt;

typedef struct DlumaCustomMenuRetainedBanks {
    struct DlumaCustomMenuRetainedBanks *next;
    DlumaCompanionArchiveMaterializedBank banks[DLUMA_CUSTOM_MENU_MAX_BANKS];
} DlumaCustomMenuRetainedBanks;
static DlumaCustomMenuRetainedBanks *g_dluma_custom_menu_retained_banks;

static int dluma_custom_menu_source_still_same(
    DlumaCustomMenuStageFile *record);

static int dluma_custom_menu_main_xui_alias(
    const DlumaCustomMenuCandidate *candidate,
    char *output,
    unsigned int capacity)
{
    unsigned int index;
    unsigned int offset;
    const char *identity = candidate && candidate->staging_identity[0]
        ? candidate->staging_identity : candidate ? candidate->manifest_sha256 : "";
    if (output && capacity)
        output[0] = 0;
    if (!candidate || !output ||
        capacity <= strlen(DLUMA_CUSTOM_MENU_MAIN_XUI_COMPAT) ||
        strlen(identity) <
            DLUMA_CUSTOM_MENU_MAIN_XUI_DIGEST_CHARS)
        return 0;
    dluma_str_copy(output, capacity, "CMM_");
    offset = (unsigned int)strlen(output);
    for (index = 0; index < DLUMA_CUSTOM_MENU_MAIN_XUI_DIGEST_CHARS; ++index)
    {
        char value = identity[index];
        if (!((value >= '0' && value <= '9') ||
              (value >= 'a' && value <= 'f')))
        {
            output[0] = 0;
            return 0;
        }
        output[offset + index] = value;
    }
    output[offset + DLUMA_CUSTOM_MENU_MAIN_XUI_DIGEST_CHARS] = 0;
    dluma_str_append(output, capacity, ".xui");
    return strlen(output) == strlen(DLUMA_CUSTOM_MENU_MAIN_XUI_COMPAT);
}

static void dluma_custom_menu_text(
    char *output, unsigned int capacity, const char *value)
{
    if (!output || !capacity)
        return;
    dluma_str_copy(output, capacity, value ? value : "custom_menu_error");
}

static void dluma_custom_menu_wide_hex_append(
    WCHAR *output, unsigned int capacity,
    ULONGLONG value, unsigned int digits)
{
    static const WCHAR alphabet[] = L"0123456789abcdef";
    WCHAR encoded[17];
    unsigned int index;
    if (!output || !capacity || digits > 16U)
        return;
    for (index = 0; index < digits; ++index)
    {
        unsigned int shift = (digits - index - 1U) * 4U;
        encoded[index] = alphabet[(value >> shift) & 0xFU];
    }
    encoded[digits] = 0;
    dluma_wstr_append(output, capacity, encoded);
}

static int dluma_custom_menu_wide_under(
    const WCHAR *child, const WCHAR *root)
{
    SIZE_T index;
    SIZE_T root_length;
    if (!child || !root || !root[0])
        return 0;
    root_length = wcslen(root);
    for (index = 0; index < root_length; ++index)
    {
        if (!child[index] ||
            dluma_wascii_lower(child[index]) != dluma_wascii_lower(root[index]))
            return 0;
    }
    return child[root_length] == 0 || child[root_length] == L'\\' ||
           child[root_length] == L'/';
}

static int dluma_custom_menu_join(
    WCHAR *output, unsigned int capacity,
    const WCHAR *left, const WCHAR *right)
{
    SIZE_T left_length;
    SIZE_T right_length;
    if (!output || !capacity || !left || !right)
        return 0;
    left_length = wcslen(left);
    right_length = wcslen(right);
    if (!left_length || !right_length ||
        left_length + right_length + 2U > capacity)
        return 0;
    dluma_path_join(output, capacity, left, right);
    return output[0] && wcslen(output) < capacity;
}

static int dluma_custom_menu_identity_equal(
    const BY_HANDLE_FILE_INFORMATION *left,
    const BY_HANDLE_FILE_INFORMATION *right)
{
    return left && right &&
           left->dwVolumeSerialNumber == right->dwVolumeSerialNumber &&
           left->nFileIndexHigh == right->nFileIndexHigh &&
           left->nFileIndexLow == right->nFileIndexLow &&
           left->nFileSizeHigh == right->nFileSizeHigh &&
           left->nFileSizeLow == right->nFileSizeLow &&
           left->ftLastWriteTime.dwHighDateTime ==
               right->ftLastWriteTime.dwHighDateTime &&
           left->ftLastWriteTime.dwLowDateTime ==
               right->ftLastWriteTime.dwLowDateTime &&
           left->nNumberOfLinks == right->nNumberOfLinks;
}

static int dluma_custom_menu_open_directory(
    const WCHAR *path,
    HANDLE *handle_out,
    WCHAR *canonical,
    unsigned int canonical_capacity,
    BY_HANDLE_FILE_INFORMATION *identity)
{
    HANDLE directory;
    BY_HANDLE_FILE_INFORMATION information;
    if (handle_out)
        *handle_out = INVALID_HANDLE_VALUE;
    directory = CreateFileW(
        path, GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        0, OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, 0);
    if (directory == INVALID_HANDLE_VALUE)
        return 0;
    memset(&information, 0, sizeof(information));
    if (!GetFileInformationByHandle(directory, &information) ||
        !(information.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ||
        (information.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) ||
        !dluma_get_final_path_checked(directory, canonical, canonical_capacity))
    {
        CloseHandle(directory);
        return 0;
    }
    if (identity)
        *identity = information;
    if (handle_out)
        *handle_out = directory;
    else
        CloseHandle(directory);
    return 1;
}

static int dluma_custom_menu_directory_is_under_root(
    const WCHAR *directory_path,
    const WCHAR *root_path)
{
    HANDLE directory = INVALID_HANDLE_VALUE;
    HANDLE root = INVALID_HANDLE_VALUE;
    WCHAR canonical_directory[DLUMA_MAX_WPATH];
    WCHAR canonical_root[DLUMA_MAX_WPATH];
    int valid = 0;
    if (!directory_path || !directory_path[0] ||
        !root_path || !root_path[0])
        return 0;
    if (!dluma_custom_menu_open_directory(
            root_path, &root, canonical_root,
            DLUMA_ARRAY_COUNT(canonical_root), 0) ||
        !dluma_custom_menu_open_directory(
            directory_path, &directory, canonical_directory,
            DLUMA_ARRAY_COUNT(canonical_directory), 0))
        goto done;
    valid = dluma_custom_menu_wide_under(
        canonical_directory, canonical_root);
done:
    if (directory != INVALID_HANDLE_VALUE)
        CloseHandle(directory);
    if (root != INVALID_HANDLE_VALUE)
        CloseHandle(root);
    return valid;
}

static HANDLE dluma_custom_menu_open_regular_file(
    const WCHAR *path,
    const WCHAR *canonical_root,
    ULONGLONG maximum_size,
    int allow_empty,
    BY_HANDLE_FILE_INFORMATION *identity,
    ULONGLONG *size_out,
    WCHAR *canonical_out,
    unsigned int canonical_capacity)
{
    HANDLE file;
    BY_HANDLE_FILE_INFORMATION information;
    LARGE_INTEGER size;
    file = CreateFileW(
        path, GENERIC_READ, FILE_SHARE_READ, 0, OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, 0);
    if (file == INVALID_HANDLE_VALUE)
        return INVALID_HANDLE_VALUE;
    memset(&information, 0, sizeof(information));
    if (!GetFileInformationByHandle(file, &information) ||
        (information.dwFileAttributes &
            (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) ||
        information.nNumberOfLinks != 1U ||
        !GetFileSizeEx(file, &size) || size.QuadPart < 0 ||
        (!allow_empty && size.QuadPart == 0) ||
        (ULONGLONG)size.QuadPart > maximum_size ||
        !dluma_get_final_path_checked(file, canonical_out, canonical_capacity) ||
        !dluma_custom_menu_wide_under(canonical_out, canonical_root))
    {
        CloseHandle(file);
        return INVALID_HANDLE_VALUE;
    }
    if (identity)
        *identity = information;
    if (size_out)
        *size_out = (ULONGLONG)size.QuadPart;
    return file;
}

/* Resolve the original project path before map/XUI aliases are generated. */
static HANDLE dluma_custom_menu_open_effective_file(
    const WCHAR *path, const WCHAR *canonical_root,
    ULONGLONG maximum_size, int allow_empty,
    BY_HANDLE_FILE_INFORMATION *identity, ULONGLONG *size_out,
    WCHAR *canonical_out, unsigned int canonical_capacity,
    char *asset_owner, unsigned int owner_capacity)
{
    const WCHAR *source = path;
    const WCHAR *root = canonical_root;
    unsigned int index = 0;
    WCHAR override_path[DLUMA_MAX_WPATH];
    WCHAR override_root[DLUMA_MAX_WPATH];
    char relative[DLUMA_MAX_WPATH * 2U];
    int resolved;
    if (asset_owner && owner_capacity) asset_owner[0] = 0;
    if (!path || !canonical_root) return INVALID_HANDLE_VALUE;
    if (source[0] == L'\\' && source[1] == L'\\' &&
        source[2] == L'?' && source[3] == L'\\') source += 4;
    if (root[0] == L'\\' && root[1] == L'\\' &&
        root[2] == L'?' && root[3] == L'\\') root += 4;
    while (root[index] && source[index]) {
        WCHAR a = root[index] == L'/' ? L'\\' : root[index];
        WCHAR b = source[index] == L'/' ? L'\\' : source[index];
        if (dluma_wascii_lower(a) != dluma_wascii_lower(b)) break;
        ++index;
    }
    if (root[index] || (source[index] != L'\\' && source[index] != L'/') ||
        !dluma_wide_to_utf8(source + index + 1U, relative,
                          DLUMA_ARRAY_COUNT(relative)))
        return INVALID_HANDLE_VALUE;
    {
        unsigned int part_length = 0;
        char part[16];
        while (relative[part_length] && relative[part_length] != '/' &&
               relative[part_length] != '\\' && part_length + 1U < sizeof(part)) {
            part[part_length] = relative[part_length];
            ++part_length;
        }
        part[part_length] = 0;
        if (dluma_stricmp(part, "data") != 0 &&
            dluma_stricmp(part, "assets_pc") != 0)
            return dluma_custom_menu_open_regular_file(
                path, canonical_root, maximum_size, allow_empty,
                identity, size_out, canonical_out, canonical_capacity);
    }
    resolved = dluma_asset_mods_resolve_file_root(
        relative, 1, override_path, DLUMA_ARRAY_COUNT(override_path),
        override_root, DLUMA_ARRAY_COUNT(override_root),
        asset_owner, owner_capacity);
    if (resolved < 0) return INVALID_HANDLE_VALUE;
    return dluma_custom_menu_open_regular_file(
        resolved ? override_path : path,
        resolved ? override_root : canonical_root,
        maximum_size, allow_empty, identity, size_out,
        canonical_out, canonical_capacity);
}

static int dluma_custom_menu_read_handle(
    HANDLE file,
    const BY_HANDLE_FILE_INFORMATION *before,
    ULONGLONG size,
    unsigned char **bytes_out)
{
    unsigned char *bytes;
    DWORD read = 0;
    LARGE_INTEGER zero;
    BY_HANDLE_FILE_INFORMATION after;
    if (!bytes_out || size > 0xFFFFFFFFULL)
        return 0;
    *bytes_out = 0;
    bytes = (unsigned char *)dluma_alloc((SIZE_T)size + 1U);
    if (!bytes)
        return 0;
    zero.QuadPart = 0;
    memset(&after, 0, sizeof(after));
    if (!SetFilePointerEx(file, zero, 0, FILE_BEGIN) ||
        (size && (!ReadFile(file, bytes, (DWORD)size, &read, 0) ||
                  read != (DWORD)size)) ||
        !GetFileInformationByHandle(file, &after) ||
        !dluma_custom_menu_identity_equal(before, &after))
    {
        dluma_free(bytes);
        return 0;
    }
    bytes[(SIZE_T)size] = 0;
    *bytes_out = bytes;
    return 1;
}

static int dluma_custom_menu_open_manifest(
    const WCHAR *project_root,
    DlumaCustomMenuCandidate *candidate)
{
    WCHAR path[DLUMA_MAX_WPATH];
    WCHAR canonical[DLUMA_MAX_WPATH];
    HANDLE file;
    BY_HANDLE_FILE_INFORMATION identity;
    ULONGLONG size = 0;
    if (!dluma_custom_menu_join(path, DLUMA_ARRAY_COUNT(path),
                              project_root, L"custommenu.json"))
        return 0;
    file = dluma_custom_menu_open_regular_file(
        path, candidate->canonical_root,
        DLUMA_CUSTOM_MENU_MANIFEST_MAX_BYTES, 0,
        &identity, &size, canonical, DLUMA_ARRAY_COUNT(canonical));
    if (file == INVALID_HANDLE_VALUE)
        return 0;
    if (!dluma_custom_menu_read_handle(
            file, &identity, size, &candidate->manifest_bytes))
    {
        CloseHandle(file);
        return 0;
    }
    CloseHandle(file);
    candidate->manifest_size = (unsigned int)size;
    dluma_sha256_bytes_hex(candidate->manifest_bytes,
                         candidate->manifest_size,
                         candidate->manifest_sha256);
    return 1;
}

static int dluma_custom_menu_validate_xui(
    const DlumaCustomMenuCandidate *candidate,
    DlumaCustomMainMenuOverride *item)
{
    WCHAR relative[DLUMA_MAX_WPATH];
    WCHAR path[DLUMA_MAX_WPATH];
    WCHAR canonical[DLUMA_MAX_WPATH];
    HANDLE file;
    BY_HANDLE_FILE_INFORMATION identity;
    ULONGLONG size = 0;
    unsigned char *bytes = 0;
    int valid = 0;
    if (!dluma_utf8_to_wide(item->source_path, relative,
                          DLUMA_ARRAY_COUNT(relative)) ||
        !dluma_custom_menu_join(path, DLUMA_ARRAY_COUNT(path),
                              candidate->snapshot.project_root, relative))
        return 0;
    file = dluma_custom_menu_open_effective_file(
        path, candidate->canonical_root, DLUMA_CUSTOM_MENU_MAX_XUI_BYTES, 0,
        &identity, &size, canonical, DLUMA_ARRAY_COUNT(canonical), 0, 0);
    if (file == INVALID_HANDLE_VALUE)
        return 0;
    if (dluma_custom_menu_read_handle(file, &identity, size, &bytes))
    {
        valid = dluma_custom_main_menu_xui_contract_valid(
            bytes, (unsigned int)size) &&
            dluma_custom_main_menu_xui_music_override(
                bytes, (unsigned int)size, item->music_override,
                DLUMA_ARRAY_COUNT(item->music_override));
    }
    if (bytes)
        dluma_free(bytes);
    CloseHandle(file);
    return valid;
}

static int dluma_custom_menu_find_tlv_u32(
    const unsigned char *bytes, unsigned int begin, unsigned int end,
    unsigned int tag, unsigned int *value_out)
{
    unsigned int index;
    for (index = begin; index + 10U <= end; ++index)
    {
        if (((unsigned int)bytes[index] |
             ((unsigned int)bytes[index + 1U] << 8U)) == tag &&
            bytes[index + 2U] == 4U && !bytes[index + 3U] &&
            !bytes[index + 4U] && !bytes[index + 5U])
        {
            *value_out = (unsigned int)bytes[index + 6U] |
                         ((unsigned int)bytes[index + 7U] << 8U) |
                         ((unsigned int)bytes[index + 8U] << 16U) |
                         ((unsigned int)bytes[index + 9U] << 24U);
            return 1;
        }
    }
    return 0;
}

static int dluma_custom_menu_find_tlv_string(
    const unsigned char *bytes, unsigned int begin, unsigned int end,
    unsigned int tag, const char *expected)
{
    unsigned int index;
    unsigned int expected_length = (unsigned int)strlen(expected);
    for (index = begin; index + 8U <= end; ++index)
    {
        unsigned int record_length;
        unsigned int string_length;
        if (((unsigned int)bytes[index] |
             ((unsigned int)bytes[index + 1U] << 8U)) != tag)
            continue;
        record_length = (unsigned int)bytes[index + 2U] |
                        ((unsigned int)bytes[index + 3U] << 8U) |
                        ((unsigned int)bytes[index + 4U] << 16U) |
                        ((unsigned int)bytes[index + 5U] << 24U);
        string_length = (unsigned int)bytes[index + 6U] |
                        ((unsigned int)bytes[index + 7U] << 8U);
        if (record_length == string_length + 2U &&
            string_length == expected_length &&
            index + 8U + string_length <= end &&
            memcmp(bytes + index + 8U, expected, string_length) == 0)
            return 1;
    }
    return 0;
}

static int dluma_custom_menu_camera_contract_valid(
    const unsigned char *bytes,
    unsigned int size,
    const DlumaCustomMainMenuManifest *manifest)
{
    static const char position_class[] = "CameraPostionHelper";
    static const char scene_class[] = "CameraHelperMenuScene3dDI";
    unsigned int index;
    unsigned int camera_objects = 0;
    unsigned int matches = 0;
    for (index = 0; index + 16U < size; ++index)
    {
        unsigned int object_length;
        unsigned int object_end;
        unsigned int record_length;
        unsigned int class_length;
        unsigned int id;
        if (bytes[index] != 0x76U || bytes[index + 1U] != 0U)
            continue;
        object_length = (unsigned int)bytes[index + 2U] |
                        ((unsigned int)bytes[index + 3U] << 8U) |
                        ((unsigned int)bytes[index + 4U] << 16U) |
                        ((unsigned int)bytes[index + 5U] << 24U);
        if (object_length > size - index - 6U)
            continue;
        object_end = index + 6U + object_length;
        if (bytes[index + 6U] != 0x77U || bytes[index + 7U] != 0U)
            continue;
        record_length = (unsigned int)bytes[index + 8U] |
                        ((unsigned int)bytes[index + 9U] << 8U) |
                        ((unsigned int)bytes[index + 10U] << 16U) |
                        ((unsigned int)bytes[index + 11U] << 24U);
        class_length = (unsigned int)bytes[index + 12U] |
                       ((unsigned int)bytes[index + 13U] << 8U);
        if (record_length != class_length + 2U ||
            index + 14U + class_length > object_end ||
            !((class_length == sizeof(position_class) - 1U &&
               memcmp(bytes + index + 14U, position_class,
                      class_length) == 0) ||
              (class_length == sizeof(scene_class) - 1U &&
               memcmp(bytes + index + 14U, scene_class,
                      class_length) == 0)))
            continue;
        ++camera_objects;
        if (!dluma_custom_menu_find_tlv_u32(
                bytes, index + 14U + class_length, object_end, 0x14U, &id) ||
            id != (unsigned int)manifest->camera_id ||
            !dluma_custom_menu_find_tlv_string(
                bytes, index + 14U + class_length, object_end,
                0x16U, manifest->camera_name))
            continue;
        ++matches;
    }
    return camera_objects == 1U && matches == 1U;
}

static int dluma_custom_menu_validate_scene(
    DlumaCustomMenuCandidate *candidate)
{
    WCHAR map_name[DLUMA_CUSTOM_MENU_MAP_CAPACITY];
    WCHAR relative[DLUMA_MAX_WPATH];
    WCHAR map_path[DLUMA_MAX_WPATH];
    WCHAR exp_path[DLUMA_MAX_WPATH];
    WCHAR canonical[DLUMA_MAX_WPATH];
    HANDLE map_file = INVALID_HANDLE_VALUE;
    HANDLE exp_file = INVALID_HANDLE_VALUE;
    BY_HANDLE_FILE_INFORMATION map_identity;
    BY_HANDLE_FILE_INFORMATION exp_identity;
    ULONGLONG map_size = 0;
    ULONGLONG exp_size = 0;
    unsigned char *map_bytes = 0;
    unsigned char *promoted = 0;
    unsigned int promoted_size = 0;
    int already = 0;
    char diagnostic[DLUMA_CUSTOM_MENU_DIAGNOSTIC_CAPACITY];
    int valid = 0;
    if (!dluma_utf8_to_wide(candidate->manifest.map, map_name,
                          DLUMA_ARRAY_COUNT(map_name)))
        return 0;
    relative[0] = 0;
    dluma_wstr_append(relative, DLUMA_ARRAY_COUNT(relative), L"data\\maps\\");
    dluma_wstr_append(relative, DLUMA_ARRAY_COUNT(relative), map_name);
    dluma_wstr_append(relative, DLUMA_ARRAY_COUNT(relative), L"\\");
    dluma_wstr_append(relative, DLUMA_ARRAY_COUNT(relative), map_name);
    dluma_wstr_append(relative, DLUMA_ARRAY_COUNT(relative), L".map");
    if (!dluma_custom_menu_join(map_path, DLUMA_ARRAY_COUNT(map_path),
                              candidate->snapshot.project_root, relative))
        return 0;
    relative[wcslen(relative) - 3U] = L'e';
    relative[wcslen(relative) - 2U] = L'x';
    relative[wcslen(relative) - 1U] = L'p';
    if (!dluma_custom_menu_join(exp_path, DLUMA_ARRAY_COUNT(exp_path),
                              candidate->snapshot.project_root, relative))
        return 0;
    map_file = dluma_custom_menu_open_effective_file(
        map_path, candidate->canonical_root,
        DLUMA_CUSTOM_MENU_MAX_PROMOTED_BYTES, 0,
        &map_identity, &map_size, canonical, DLUMA_ARRAY_COUNT(canonical), 0, 0);
    exp_file = dluma_custom_menu_open_effective_file(
        exp_path, candidate->canonical_root,
        DLUMA_CUSTOM_MENU_MAX_PROMOTED_BYTES, 0,
        &exp_identity, &exp_size, canonical, DLUMA_ARRAY_COUNT(canonical), 0, 0);
    if (map_file == INVALID_HANDLE_VALUE || exp_file == INVALID_HANDLE_VALUE ||
        !dluma_custom_menu_read_handle(
            map_file, &map_identity, map_size, &map_bytes))
        goto done;
    promoted = (unsigned char *)dluma_alloc(
        (SIZE_T)(map_size > exp_size ? map_size : exp_size) + 32U);
    if (!promoted ||
        !dluma_custom_main_menu_promote_root_record(
            map_bytes, (unsigned int)map_size,
            promoted, (unsigned int)map_size + 32U,
            &promoted_size, &already,
            diagnostic, DLUMA_ARRAY_COUNT(diagnostic)) ||
        !dluma_custom_menu_camera_contract_valid(
            map_bytes, (unsigned int)map_size, &candidate->manifest))
        goto done;
    dluma_free(map_bytes);
    map_bytes = 0;
    if (!dluma_custom_menu_read_handle(
            exp_file, &exp_identity, exp_size, &map_bytes))
        goto done;
    if (!dluma_custom_main_menu_promote_root_record(
            map_bytes, (unsigned int)exp_size,
            promoted, (unsigned int)exp_size + 32U,
            &promoted_size, &already,
            diagnostic, DLUMA_ARRAY_COUNT(diagnostic)) ||
        !dluma_custom_menu_camera_contract_valid(
            map_bytes, (unsigned int)exp_size, &candidate->manifest))
        goto done;
    valid = 1;
done:
    if (promoted)
        dluma_free(promoted);
    if (map_bytes)
        dluma_free(map_bytes);
    if (map_file != INVALID_HANDLE_VALUE)
        CloseHandle(map_file);
    if (exp_file != INVALID_HANDLE_VALUE)
        CloseHandle(exp_file);
    return valid;
}

static unsigned int dluma_custom_menu_cue_hash(const char *cue)
{
    unsigned int hash = 0;
    unsigned int index;
    for (index = 0; cue && cue[index]; ++index)
        hash = (unsigned int)(unsigned char)cue[index] + 41U * hash;
    return hash;
}

static int dluma_custom_menu_text_equal_exact(
    const char *left, const char *right)
{
    unsigned int index = 0;
    if (!left || !right)
        return left == right;
    while (left[index] && right[index] && left[index] == right[index])
        ++index;
    return left[index] == right[index];
}

static int dluma_custom_menu_catalog_has_cue(
    const DlumaCompanionArchiveAudioManifest *catalogs,
    unsigned int catalog_count,
    const char *cue)
{
    unsigned int bank_index;
    for (bank_index = 0; bank_index < catalog_count; ++bank_index)
    {
        unsigned int cue_index;
        for (cue_index = 0; cue_index < catalogs[bank_index].cue_count;
             ++cue_index)
            if (dluma_custom_menu_text_equal_exact(
                    catalogs[bank_index].cues[cue_index].name, cue))
                return 1;
    }
    return 0;
}

static int dluma_custom_menu_catalogs_valid(
    const DlumaCustomMainMenuManifest *manifest,
    const DlumaCompanionArchiveAudioManifest *catalogs,
    const char hashes[DLUMA_CUSTOM_MENU_MAX_BANKS][DLUMA_SHA256_HEX_CAPACITY])
{
    unsigned int bank_index;
    unsigned int music_index;
    unsigned int override_index;
    if (!manifest || !catalogs || !hashes ||
        manifest->bank_count == 0U ||
        manifest->bank_count > DLUMA_CUSTOM_MENU_MAX_BANKS)
        return 0;
    for (bank_index = 0; bank_index < manifest->bank_count; ++bank_index)
    {
        unsigned int prior_bank;
        unsigned int cue_index;
        if (!catalogs[bank_index].valid ||
            !catalogs[bank_index].cue_count ||
            strlen(hashes[bank_index]) != 64U)
            return 0;
        for (prior_bank = 0; prior_bank < bank_index; ++prior_bank)
            if (dluma_custom_menu_text_equal_exact(
                    hashes[prior_bank], hashes[bank_index]))
                return 0;
        for (cue_index = 0;
             cue_index < catalogs[bank_index].cue_count; ++cue_index)
        {
            const char *cue = catalogs[bank_index].cues[cue_index].name;
            unsigned int hash = dluma_custom_menu_cue_hash(cue);
            unsigned int compare_bank;
            for (compare_bank = 0; compare_bank <= bank_index;
                 ++compare_bank)
            {
                unsigned int compare_limit =
                    compare_bank == bank_index ? cue_index :
                    catalogs[compare_bank].cue_count;
                unsigned int compare_cue;
                for (compare_cue = 0; compare_cue < compare_limit;
                     ++compare_cue)
                {
                    const char *prior =
                        catalogs[compare_bank].cues[compare_cue].name;
                    if (!dluma_custom_menu_text_equal_exact(prior, cue) &&
                        dluma_custom_menu_cue_hash(prior) == hash)
                        return 0;
                }
            }
        }
    }
    for (music_index = 0;
         music_index < manifest->music_override_count; ++music_index)
        if (!dluma_custom_menu_catalog_has_cue(
                catalogs, manifest->bank_count,
                manifest->music_overrides[music_index].name))
            return 0;
    for (override_index = 0;
         override_index < manifest->override_count; ++override_index)
        if (manifest->overrides[override_index].music_override[0] &&
            !dluma_custom_menu_catalog_has_cue(
                catalogs, manifest->bank_count,
                manifest->overrides[override_index].music_override))
            return 0;
    return 1;
}

static int dluma_custom_menu_validate_banks(
    const DlumaCustomMenuCandidate *candidate)
{
    DlumaCompanionArchiveAudioManifest catalogs[DLUMA_CUSTOM_MENU_MAX_BANKS];
    char hashes[DLUMA_CUSTOM_MENU_MAX_BANKS][DLUMA_SHA256_HEX_CAPACITY];
    ULONGLONG total_size = 0;
    unsigned int bank_index;
    memset(catalogs, 0, sizeof(catalogs));
    memset(hashes, 0, sizeof(hashes));
    if (!candidate->manifest.use_custom_banks)
    {
        unsigned int override_index;
        if (candidate->manifest.music_override_count)
            return 0;
        for (override_index = 0;
             override_index < candidate->manifest.override_count;
             ++override_index)
            if (candidate->manifest.overrides[override_index]
                    .music_override[0])
                return 0;
        return 1;
    }
    for (bank_index = 0;
         bank_index < candidate->manifest.bank_count; ++bank_index)
    {
        WCHAR relative[DLUMA_MAX_WPATH];
        WCHAR path[DLUMA_MAX_WPATH];
        WCHAR canonical[DLUMA_MAX_WPATH];
        HANDLE file;
        BY_HANDLE_FILE_INFORMATION identity;
        ULONGLONG size = 0;
        if (!dluma_utf8_to_wide(
                candidate->manifest.banks[bank_index].path,
                relative, DLUMA_ARRAY_COUNT(relative)) ||
            !dluma_custom_menu_join(
                path, DLUMA_ARRAY_COUNT(path),
                candidate->snapshot.project_root, relative))
            return 0;
        file = dluma_custom_menu_open_effective_file(
            path, candidate->canonical_root,
            DLUMA_CUSTOM_MENU_MAX_BANK_BYTES, 0,
            &identity, &size, canonical, DLUMA_ARRAY_COUNT(canonical), 0, 0);
        if (file == INVALID_HANDLE_VALUE)
            return 0;
        if (total_size > DLUMA_CUSTOM_MENU_MAX_BANK_TOTAL_BYTES - size ||
            size > 0xffffffffULL ||
            !dluma_sha256_handle_hex(
                file, DLUMA_CUSTOM_MENU_MAX_BANK_BYTES, hashes[bank_index]) ||
            !dluma_companion_archive_catalog_locked_bank(
                file, &identity, (DWORD)size, &catalogs[bank_index]))
        {
            CloseHandle(file);
            return 0;
        }
        CloseHandle(file);
        total_size += size;
    }
    return dluma_custom_menu_catalogs_valid(
        &candidate->manifest, catalogs, hashes);
}

static void dluma_custom_menu_candidate_validate(
    DlumaCustomMenuCandidate *candidate)
{
    unsigned int index;
    candidate->snapshot.valid = 0;
    if (!dluma_custom_menu_open_manifest(
            candidate->snapshot.project_root, candidate))
    {
        dluma_custom_menu_text(candidate->snapshot.diagnostic,
                             DLUMA_ARRAY_COUNT(candidate->snapshot.diagnostic),
                             "custommenu.json could not be read safely");
        return;
    }
    if (!dluma_custom_main_menu_manifest_parse(
            candidate->manifest_bytes, candidate->manifest_size,
            &candidate->manifest,
            candidate->snapshot.diagnostic,
            DLUMA_ARRAY_COUNT(candidate->snapshot.diagnostic)))
        return;
    dluma_str_copy(candidate->snapshot.id,
                 DLUMA_ARRAY_COUNT(candidate->snapshot.id),
                 candidate->manifest.id);
    dluma_str_copy(candidate->snapshot.name,
                 DLUMA_ARRAY_COUNT(candidate->snapshot.name),
                 candidate->manifest.name);
    dluma_str_copy(candidate->snapshot.author,
                 DLUMA_ARRAY_COUNT(candidate->snapshot.author),
                 candidate->manifest.author);
    dluma_str_copy(candidate->snapshot.description,
                 DLUMA_ARRAY_COUNT(candidate->snapshot.description),
                 candidate->manifest.description);
    dluma_str_copy(candidate->snapshot.version,
                 DLUMA_ARRAY_COUNT(candidate->snapshot.version),
                 candidate->manifest.version);
    dluma_str_copy(candidate->snapshot.map,
                 DLUMA_ARRAY_COUNT(candidate->snapshot.map),
                 candidate->manifest.map);
    if (!dluma_custom_menu_validate_scene(candidate))
    {
        dluma_custom_menu_text(candidate->snapshot.diagnostic,
                             DLUMA_ARRAY_COUNT(candidate->snapshot.diagnostic),
                             "scene map, root record, or declared camera is invalid");
        return;
    }
    for (index = 0; index < candidate->manifest.override_count; ++index)
    {
        if (!dluma_custom_menu_validate_xui(
                candidate, &candidate->manifest.overrides[index]))
        {
            dluma_custom_menu_text(candidate->snapshot.diagnostic,
                                 DLUMA_ARRAY_COUNT(candidate->snapshot.diagnostic),
                                 "declared XUI could not be read safely");
            return;
        }
    }
    if (!dluma_custom_menu_validate_banks(candidate))
    {
        dluma_custom_menu_text(candidate->snapshot.diagnostic,
                             DLUMA_ARRAY_COUNT(candidate->snapshot.diagnostic),
                             "declared CSB bank could not be read safely");
        return;
    }
    candidate->snapshot.valid = 1U;
    candidate->snapshot.diagnostic[0] = 0;
}

static const WCHAR *dluma_custom_menu_leaf(const WCHAR *path)
{
    const WCHAR *leaf = path;
    unsigned int index;
    if (!path)
        return L"";
    for (index = 0; path[index]; ++index)
        if (path[index] == L'\\' || path[index] == L'/')
            leaf = path + index + 1U;
    return leaf;
}

static int dluma_custom_menu_root_already_known(const WCHAR *canonical)
{
    unsigned int index;
    for (index = 0; index < g_dluma_custom_menu_candidate_count; ++index)
        if (dluma_wstricmp(g_dluma_custom_menu_candidates[index].canonical_root,
                         canonical) == 0)
            return 1;
    return 0;
}

static int dluma_custom_menu_marker_exists(const WCHAR *project_root)
{
    WCHAR canonical_root[DLUMA_MAX_WPATH];
    WCHAR path[DLUMA_MAX_WPATH];
    WCHAR canonical_file[DLUMA_MAX_WPATH];
    BY_HANDLE_FILE_INFORMATION root_identity;
    BY_HANDLE_FILE_INFORMATION file_identity;
    ULONGLONG size;
    HANDLE directory = INVALID_HANDLE_VALUE;
    HANDLE file = INVALID_HANDLE_VALUE;
    int result = 0;
    if (!project_root || !project_root[0] ||
        !dluma_custom_menu_open_directory(
            project_root, &directory, canonical_root,
            DLUMA_ARRAY_COUNT(canonical_root), &root_identity) ||
        !dluma_custom_menu_join(path, DLUMA_ARRAY_COUNT(path),
                              project_root, L"custommenu.json"))
        goto done;
    file = dluma_custom_menu_open_regular_file(
        path, canonical_root, DLUMA_CUSTOM_MENU_MANIFEST_MAX_BYTES, 1,
        &file_identity, &size, canonical_file,
        DLUMA_ARRAY_COUNT(canonical_file));
    result = file != INVALID_HANDLE_VALUE;
done:
    if (file != INVALID_HANDLE_VALUE)
        CloseHandle(file);
    if (directory != INVALID_HANDLE_VALUE)
        CloseHandle(directory);
    return result;
}

static void dluma_custom_menu_add_candidate(const WCHAR *project_root)
{
    DlumaCustomMenuCandidate *candidate;
    WCHAR canonical_root[DLUMA_MAX_WPATH];
    BY_HANDLE_FILE_INFORMATION root_identity;
    HANDLE directory = INVALID_HANDLE_VALUE;
    DlumaProjectSourceDescription source;
    if (!project_root || !dluma_custom_menu_marker_exists(project_root))
        return;
    if (!dluma_custom_menu_open_directory(
            project_root, &directory, canonical_root,
            DLUMA_ARRAY_COUNT(canonical_root), &root_identity))
        return;
    if (dluma_custom_menu_root_already_known(canonical_root))
    {
        CloseHandle(directory);
        return;
    }
    if (g_dluma_custom_menu_candidate_count >= DLUMA_CUSTOM_MENU_MAX_CANDIDATES)
    {
        /* The exact count is deliberately saturated. Once the public bound is
           exceeded, every retained row is invalidated and no candidate can be
           selected or staged. */
        g_dluma_custom_menu_candidate_total =
            DLUMA_CUSTOM_MENU_MAX_CANDIDATES + 1U;
        g_dluma_custom_menu_candidate_overflow = 1U;
        CloseHandle(directory);
        return;
    }
    candidate = &g_dluma_custom_menu_candidates[
        g_dluma_custom_menu_candidate_count];
    memset(candidate, 0, sizeof(*candidate));
    dluma_wstr_copy(candidate->canonical_root,
                  DLUMA_ARRAY_COUNT(candidate->canonical_root), canonical_root);
    candidate->root_identity = root_identity;
    CloseHandle(directory);
    dluma_wstr_copy(candidate->snapshot.project_root,
                  DLUMA_ARRAY_COUNT(candidate->snapshot.project_root),
                  project_root);
    if (dluma_classify_project_source(project_root, &source)) {
        dluma_str_copy(candidate->snapshot.source_name,
                     DLUMA_ARRAY_COUNT(candidate->snapshot.source_name),
                     source.project_name);
        dluma_str_copy(candidate->snapshot.workshop_id,
                     DLUMA_ARRAY_COUNT(candidate->snapshot.workshop_id),
                     source.published_id);
        dluma_str_copy(candidate->snapshot.source_identity,
                     DLUMA_ARRAY_COUNT(candidate->snapshot.source_identity),
                     source.source_identity);
        candidate->snapshot.source_is_subscribed =
            source.is_subscribed_workshop;
    }
    if (!dluma_wide_to_utf8(
            dluma_custom_menu_leaf(project_root), candidate->snapshot.name,
            DLUMA_ARRAY_COUNT(candidate->snapshot.name)))
        dluma_str_copy(candidate->snapshot.name,
                     DLUMA_ARRAY_COUNT(candidate->snapshot.name),
                     "Invalid custom menu project");
    ++g_dluma_custom_menu_candidate_count;
    ++g_dluma_custom_menu_candidate_total;
    dluma_custom_menu_candidate_validate(candidate);
}

static void dluma_custom_menu_invalidate_capacity_overflow(void)
{
    unsigned int index;
    if (!g_dluma_custom_menu_candidate_overflow)
        return;
    for (index = 0; index < g_dluma_custom_menu_candidate_count; ++index)
    {
        g_dluma_custom_menu_candidates[index].snapshot.valid = 0;
        dluma_custom_menu_text(
            g_dluma_custom_menu_candidates[index].snapshot.diagnostic,
            DLUMA_ARRAY_COUNT(
                g_dluma_custom_menu_candidates[index].snapshot.diagnostic),
            "custom menu project count exceeds the supported limit");
    }
}

static void dluma_custom_menu_enumerate_root(const WCHAR *root)
{
    WCHAR pattern[DLUMA_MAX_WPATH];
    WCHAR candidate_path[DLUMA_MAX_WPATH];
    WIN32_FIND_DATAW data;
    HANDLE find;
    dluma_custom_menu_add_candidate(root);
    if (!dluma_custom_menu_join(pattern, DLUMA_ARRAY_COUNT(pattern), root, L"*"))
        return;
    find = FindFirstFileW(pattern, &data);
    if (find == INVALID_HANDLE_VALUE)
        return;
    do
    {
        if (!(data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ||
            (data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) ||
            (data.cFileName[0] == L'.' && !data.cFileName[1]) ||
            (data.cFileName[0] == L'.' && data.cFileName[1] == L'.' &&
             !data.cFileName[2]) ||
            !dluma_custom_menu_join(candidate_path,
                                  DLUMA_ARRAY_COUNT(candidate_path),
                                  root, data.cFileName))
            continue;
        dluma_custom_menu_add_candidate(candidate_path);
    } while (FindNextFileW(find, &data));
    FindClose(find);
}

static int dluma_custom_menu_candidate_compare(
    const DlumaCustomMenuCandidate *left,
    const DlumaCustomMenuCandidate *right)
{
    int result = dluma_stricmp(left->snapshot.name, right->snapshot.name);
    if (result)
        return result;
    result = dluma_stricmp(left->snapshot.id, right->snapshot.id);
    if (result)
        return result;
    return dluma_wstricmp(left->canonical_root, right->canonical_root);
}

static void dluma_custom_menu_sort_candidates(void)
{
    unsigned int index;
    for (index = 1U; index < g_dluma_custom_menu_candidate_count; ++index)
    {
        DlumaCustomMenuCandidate value = g_dluma_custom_menu_candidates[index];
        unsigned int position = index;
        while (position && dluma_custom_menu_candidate_compare(
                   &value, &g_dluma_custom_menu_candidates[position - 1U]) < 0)
        {
            g_dluma_custom_menu_candidates[position] =
                g_dluma_custom_menu_candidates[position - 1U];
            --position;
        }
        g_dluma_custom_menu_candidates[position] = value;
    }
}

static void dluma_custom_menu_invalidate_duplicate_ids(void)
{
    unsigned int left;
    unsigned int right;
    for (left = 0; left < g_dluma_custom_menu_candidate_count; ++left)
    {
        if (!g_dluma_custom_menu_candidates[left].snapshot.id[0])
            continue;
        for (right = left + 1U;
             right < g_dluma_custom_menu_candidate_count; ++right)
        {
            if (!g_dluma_custom_menu_candidates[right].snapshot.id[0] ||
                !dluma_custom_main_menu_id_equal(
                    g_dluma_custom_menu_candidates[left].snapshot.id,
                    g_dluma_custom_menu_candidates[right].snapshot.id) ||
                dluma_stricmp(
                    g_dluma_custom_menu_candidates[left]
                        .snapshot.source_identity,
                    g_dluma_custom_menu_candidates[right]
                        .snapshot.source_identity) != 0)
                continue;
            g_dluma_custom_menu_candidates[left].snapshot.valid = 0;
            g_dluma_custom_menu_candidates[right].snapshot.valid = 0;
            g_dluma_custom_menu_candidates[left].snapshot.duplicate_id = 1U;
            g_dluma_custom_menu_candidates[right].snapshot.duplicate_id = 1U;
            dluma_custom_menu_text(
                g_dluma_custom_menu_candidates[left].snapshot.diagnostic,
                DLUMA_ARRAY_COUNT(
                    g_dluma_custom_menu_candidates[left].snapshot.diagnostic),
                "duplicate meta.id within the same source is ambiguous");
            dluma_custom_menu_text(
                g_dluma_custom_menu_candidates[right].snapshot.diagnostic,
                DLUMA_ARRAY_COUNT(
                    g_dluma_custom_menu_candidates[right].snapshot.diagnostic),
                "duplicate meta.id within the same source is ambiguous");
        }
    }
}

static int dluma_custom_menu_suffix_equal(
    const WCHAR *name, const WCHAR *suffix)
{
    SIZE_T name_length = name ? wcslen(name) : 0;
    SIZE_T suffix_length = suffix ? wcslen(suffix) : 0;
    if (!suffix_length || name_length < suffix_length)
        return 0;
    return dluma_wstricmp(name + name_length - suffix_length, suffix) == 0;
}

static int dluma_custom_menu_relative_suffix(
    const WCHAR *relative, const WCHAR *suffix)
{
    SIZE_T relative_length = relative ? wcslen(relative) : 0;
    SIZE_T suffix_length = suffix ? wcslen(suffix) : 0;
    const WCHAR *candidate;
    if (!relative_length || !suffix_length || relative_length < suffix_length)
        return 0;
    candidate = relative + relative_length - suffix_length;
    if (dluma_wstricmp(candidate, suffix) != 0)
        return 0;
    return candidate == relative || candidate[-1] == L'\\' ||
           candidate[-1] == L'/';
}

static int dluma_custom_menu_runtime_file_excluded(
    const WCHAR *relative, int directory)
{
    if (!relative || !relative[0])
        return 1;
    if (directory)
    {
        return dluma_custom_menu_relative_suffix(relative, L"scripts\\lua") ||
               dluma_custom_menu_relative_suffix(relative, L"scripts/lua");
    }
    return dluma_custom_menu_suffix_equal(relative, L".lua") ||
           dluma_custom_menu_suffix_equal(relative, L".bak") ||
           dluma_custom_menu_suffix_equal(relative, L".ba1") ||
           dluma_custom_menu_suffix_equal(relative, L".ba2") ||
           dluma_custom_menu_suffix_equal(relative, L".ba3") ||
           dluma_custom_menu_suffix_equal(relative, L".log") ||
           dluma_custom_menu_suffix_equal(relative, L".tmp") ||
           dluma_custom_menu_suffix_equal(relative, L".temp") ||
           dluma_custom_menu_suffix_equal(relative, L".old") ||
           dluma_custom_menu_suffix_equal(relative, L".orig") ||
           dluma_custom_menu_suffix_equal(relative, L".rej") ||
           dluma_custom_menu_suffix_equal(relative, L"_log") ||
           dluma_custom_menu_suffix_equal(relative, L"_log_err");
}

static int dluma_custom_menu_inventory_fail(
    DlumaCustomMenuStageInventory *inventory, const char *diagnostic)
{
    if (inventory && !inventory->diagnostic[0])
        dluma_custom_menu_text(
            inventory->diagnostic,
            DLUMA_ARRAY_COUNT(inventory->diagnostic), diagnostic);
    return 0;
}

static int dluma_custom_menu_inventory_fail_destination(
    DlumaCustomMenuStageInventory *inventory,
    const char *prefix,
    const char *destination)
{
    if (inventory && !inventory->diagnostic[0])
    {
        dluma_str_copy(
            inventory->diagnostic,
            DLUMA_ARRAY_COUNT(inventory->diagnostic), prefix);
        dluma_str_append(
            inventory->diagnostic,
            DLUMA_ARRAY_COUNT(inventory->diagnostic), destination);
    }
    return 0;
}

static int dluma_custom_menu_inventory_add(
    DlumaCustomMenuStageInventory *inventory,
    const WCHAR *source_path,
    const WCHAR *canonical_root,
    const WCHAR *destination_relative,
    ULONGLONG maximum_size,
    unsigned char promote,
    unsigned char overlay_output)
{
    DlumaCustomMenuStageFile *record;
    WCHAR canonical[DLUMA_MAX_WPATH];
    char destination_utf8[DLUMA_MAX_WPATH * 2U];
    SIZE_T destination_length;
    unsigned int index;
    if (!inventory || !inventory->files ||
        inventory->count >= DLUMA_CUSTOM_MENU_MAX_STAGE_FILES)
        return dluma_custom_menu_inventory_fail(
            inventory, "custom-menu stage file-count limit exceeded");
    if (!dluma_wide_to_utf8(
            destination_relative, destination_utf8,
            DLUMA_ARRAY_COUNT(destination_utf8)))
        return dluma_custom_menu_inventory_fail(
            inventory, "staged destination is not valid UTF-8");
    destination_length = strlen(destination_utf8);
    if (!destination_length || destination_length > 0xffffU)
        return dluma_custom_menu_inventory_fail(
            inventory, "staged destination length is invalid");
    for (index = 0; index < inventory->count; ++index)
    {
        if (dluma_stricmp(
                inventory->files[index].destination_utf8,
                destination_utf8) == 0)
            return dluma_custom_menu_inventory_fail_destination(
                inventory, "duplicate staged destination: ",
                destination_utf8);
    }
    record = &inventory->files[inventory->count];
    memset(record, 0, sizeof(*record));
    record->source = dluma_custom_menu_open_effective_file(
        source_path, canonical_root, maximum_size, 1,
        &record->source_identity, &record->source_size,
        canonical, DLUMA_ARRAY_COUNT(canonical),
        record->asset_owner, DLUMA_ARRAY_COUNT(record->asset_owner));
    if (record->source == INVALID_HANDLE_VALUE)
        return dluma_custom_menu_inventory_fail_destination(
            inventory, "runtime content could not be opened safely: ",
            destination_utf8);
    record->output_size = record->source_size;
    record->promote_root = promote;
    record->overlay_output = overlay_output;
    dluma_wstr_copy(record->source_path,
                  DLUMA_ARRAY_COUNT(record->source_path), canonical);
    dluma_wstr_copy(record->destination_relative,
                  DLUMA_ARRAY_COUNT(record->destination_relative),
                  destination_relative);
    dluma_str_copy(record->destination_utf8,
                 DLUMA_ARRAY_COUNT(record->destination_utf8),
                 destination_utf8);
    if (inventory->total_source_bytes >
            ~record->source_size)
    {
        CloseHandle(record->source);
        record->source = INVALID_HANDLE_VALUE;
        return dluma_custom_menu_inventory_fail(
            inventory, "custom-menu source byte count overflowed");
    }
    inventory->total_source_bytes += record->source_size;
    if (overlay_output)
        ++inventory->overlay_file_count;
    ++inventory->count;
    return 1;
}

/*
 * Adds an empty overlay record for a level companion the selected project does
 * not provide.  Without it the engine resolves that name from the stock level
 * and runs the stock city's hooks, weather, and events against the published
 * scene, which is not what "shipped with the base game" means.
 */
static int dluma_custom_menu_inventory_add_stub(
    DlumaCustomMenuStageInventory *inventory,
    const WCHAR *destination_relative)
{
    DlumaCustomMenuStageFile *record;
    char destination_utf8[DLUMA_MAX_WPATH * 2U];
    SIZE_T destination_length;
    unsigned int index;
    if (!inventory || !inventory->files ||
        inventory->count >= DLUMA_CUSTOM_MENU_MAX_STAGE_FILES)
        return dluma_custom_menu_inventory_fail(
            inventory, "custom-menu stage file-count limit exceeded");
    if (!dluma_wide_to_utf8(
            destination_relative, destination_utf8,
            DLUMA_ARRAY_COUNT(destination_utf8)))
        return dluma_custom_menu_inventory_fail(
            inventory, "staged destination is not valid UTF-8");
    destination_length = strlen(destination_utf8);
    if (!destination_length || destination_length > 0xffffU)
        return dluma_custom_menu_inventory_fail(
            inventory, "staged destination length is invalid");
    for (index = 0; index < inventory->count; ++index)
        if (dluma_stricmp(
                inventory->files[index].destination_utf8,
                destination_utf8) == 0)
            return 1; /* The project supplies it; nothing to shadow. */
    record = &inventory->files[inventory->count];
    memset(record, 0, sizeof(*record));
    record->source = INVALID_HANDLE_VALUE;
    record->source_size = 0;
    record->output_size = 0;
    record->overlay_output = 1U;
    record->neutral_stub = 1U;
    dluma_sha256_bytes_hex((const unsigned char *)"", 0U,
                         record->output_sha256);
    record->output_crc32 = 0UL; /* CRC32 over no bytes. */
    dluma_wstr_copy(record->destination_relative,
                  DLUMA_ARRAY_COUNT(record->destination_relative),
                  destination_relative);
    dluma_str_copy(record->destination_utf8,
                 DLUMA_ARRAY_COUNT(record->destination_utf8),
                 destination_utf8);
    ++inventory->overlay_file_count;
    ++inventory->count;
    return 1;
}

/*
 * Stock menu_lvl_new companions that are keyed to the stock city.  Publishing
 * an empty file for each one the project does not supply keeps the custom level
 * self-contained.  Build-time only records (.navmesh, lightmap and mip
 * configuration) are deliberately left alone: the engine tolerates the stock
 * copies and an empty binary would be worse than an unused one.
 */
static const WCHAR *const g_dluma_custom_menu_level_companions[] = {
    L"_events.scr", L"_hooks.scr", L"_weather_a.scr", L"_grass.scr",
    L"_night_horde.scr", L"_forbidden_meshes.scr", L"_map_info.scr",
    L"_quest_hubs.scr", L"_spawn.scr"
};

static int dluma_custom_menu_complete_published_level(
    DlumaCustomMenuStageInventory *inventory)
{
    WCHAR destination[DLUMA_MAX_WPATH];
    unsigned int index;
    if (!inventory)
        return 0;
    for (index = 0;
         index < DLUMA_ARRAY_COUNT(g_dluma_custom_menu_level_companions);
         ++index)
    {
        destination[0] = 0;
        dluma_wstr_append(destination, DLUMA_ARRAY_COUNT(destination),
                        L"data\\maps\\");
        dluma_wstr_append(destination, DLUMA_ARRAY_COUNT(destination),
                        DLUMA_CUSTOM_MENU_STOCK_LEVEL_W);
        dluma_wstr_append(destination, DLUMA_ARRAY_COUNT(destination), L"\\");
        dluma_wstr_append(destination, DLUMA_ARRAY_COUNT(destination),
                        DLUMA_CUSTOM_MENU_STOCK_LEVEL_W);
        dluma_wstr_append(destination, DLUMA_ARRAY_COUNT(destination),
                        g_dluma_custom_menu_level_companions[index]);
        if (!dluma_custom_menu_inventory_add_stub(inventory, destination))
            return 0;
    }
    return 1;
}

static int dluma_custom_menu_wide_contains_separator(const WCHAR *value)
{
    unsigned int i;
    if (!value)
        return 0;
    for (i = 0; value[i]; ++i)
        if (value[i] == L'\\' || value[i] == L'/')
            return 1;
    return 0;
}

static unsigned int dluma_custom_menu_wide_prefix_length(
    const WCHAR *value, const WCHAR *prefix)
{
    unsigned int i = 0;
    if (!value || !prefix)
        return 0;
    for (;;)
    {
        if (!prefix[i])
            return i;
        if (dluma_wascii_lower(value[i]) != dluma_wascii_lower(prefix[i]))
            return 0;
        ++i;
    }
}

/*
 * Rewrites a staged destination inside the project's own map directory to the
 * stock frontend level directory, renaming the level-name-keyed file prefix
 * with it.  Companion records such as chromedterrainoptions.scr carry no level
 * prefix and keep their names.  Returns 0 when the destination is not part of
 * the level, leaving it untouched.
 */
static int dluma_custom_menu_stock_level_destination(
    const WCHAR *destination,
    const WCHAR *map_directory_prefix,
    const WCHAR *map_name,
    WCHAR *output,
    unsigned int capacity)
{
    unsigned int directory_length;
    unsigned int name_length;
    const WCHAR *leaf;
    if (!destination || !map_directory_prefix || !map_name || !output ||
        !capacity)
        return 0;
    directory_length = dluma_custom_menu_wide_prefix_length(
        destination, map_directory_prefix);
    if (!directory_length)
        return 0;
    leaf = destination + directory_length;
    if (!leaf[0] || dluma_custom_menu_wide_contains_separator(leaf))
        return 0;
    output[0] = 0;
    dluma_wstr_append(output, capacity, L"data\\maps\\");
    dluma_wstr_append(output, capacity, DLUMA_CUSTOM_MENU_STOCK_LEVEL_W);
    dluma_wstr_append(output, capacity, L"\\");
    name_length = dluma_custom_menu_wide_prefix_length(leaf, map_name);
    if (name_length)
    {
        dluma_wstr_append(output, capacity, DLUMA_CUSTOM_MENU_STOCK_LEVEL_W);
        dluma_wstr_append(output, capacity, leaf + name_length);
    }
    else
        dluma_wstr_append(output, capacity, leaf);
    return output[0] != 0;
}

static int dluma_custom_menu_scan_runtime_tree(
    DlumaCustomMenuStageInventory *inventory,
    const DlumaCustomMenuCandidate *candidate,
    const WCHAR *runtime_root,
    const WCHAR *runtime_prefix,
    const WCHAR *relative,
    unsigned int depth, const WCHAR *source_root, int merge)
{
    WCHAR directory[DLUMA_MAX_WPATH];
    WCHAR canonical_directory[DLUMA_MAX_WPATH];
    WCHAR pattern[DLUMA_MAX_WPATH];
    WCHAR child_relative[DLUMA_MAX_WPATH];
    WCHAR source[DLUMA_MAX_WPATH];
    WCHAR destination[DLUMA_MAX_WPATH];
    WCHAR expected_map[DLUMA_MAX_WPATH];
    WCHAR expected_exp[DLUMA_MAX_WPATH];
    WCHAR map_directory[DLUMA_MAX_WPATH];
    WCHAR stock_destination[DLUMA_MAX_WPATH];
    WCHAR map_name[DLUMA_CUSTOM_MENU_MAP_CAPACITY];
    HANDLE directory_handle = INVALID_HANDLE_VALUE;
    BY_HANDLE_FILE_INFORMATION directory_identity;
    WIN32_FIND_DATAW data;
    HANDLE find;
    int result = 1;
    if (!inventory || !candidate || !runtime_root || !runtime_prefix ||
        depth > DLUMA_CUSTOM_MENU_MAX_TREE_DEPTH)
        return dluma_custom_menu_inventory_fail(
            inventory, "runtime content tree depth is invalid");
    if (relative && relative[0])
    {
        if (!dluma_custom_menu_join(directory, DLUMA_ARRAY_COUNT(directory),
                                  runtime_root, relative))
            return dluma_custom_menu_inventory_fail(
                inventory, "runtime directory path is too long");
    }
    else
        dluma_wstr_copy(directory, DLUMA_ARRAY_COUNT(directory), runtime_root);
    if (!dluma_custom_menu_open_directory(
            directory, &directory_handle, canonical_directory,
            DLUMA_ARRAY_COUNT(canonical_directory), &directory_identity) ||
        !dluma_custom_menu_wide_under(
            canonical_directory, source_root) ||
        !dluma_custom_menu_join(pattern, DLUMA_ARRAY_COUNT(pattern),
                              directory, L"*"))
        goto fail;
    if (!dluma_utf8_to_wide(candidate->manifest.map, map_name,
                          DLUMA_ARRAY_COUNT(map_name)))
        goto fail;
    expected_map[0] = 0;
    dluma_wstr_append(expected_map, DLUMA_ARRAY_COUNT(expected_map), L"data\\maps\\");
    dluma_wstr_append(expected_map, DLUMA_ARRAY_COUNT(expected_map), map_name);
    dluma_wstr_append(expected_map, DLUMA_ARRAY_COUNT(expected_map), L"\\");
    dluma_wstr_append(expected_map, DLUMA_ARRAY_COUNT(expected_map), map_name);
    dluma_wstr_append(expected_map, DLUMA_ARRAY_COUNT(expected_map), L".map");
    expected_exp[0] = 0;
    dluma_wstr_append(expected_exp, DLUMA_ARRAY_COUNT(expected_exp), L"data\\maps\\");
    dluma_wstr_append(expected_exp, DLUMA_ARRAY_COUNT(expected_exp), map_name);
    dluma_wstr_append(expected_exp, DLUMA_ARRAY_COUNT(expected_exp), L"\\");
    dluma_wstr_append(expected_exp, DLUMA_ARRAY_COUNT(expected_exp), map_name);
    dluma_wstr_append(expected_exp, DLUMA_ARRAY_COUNT(expected_exp), L".exp");
    /* Every record of the selected level is published under the stock frontend
     * level directory and file prefix, so the engine loads it as its own menu
     * level rather than being redirected to a name it never ships. */
    map_directory[0] = 0;
    dluma_wstr_append(map_directory, DLUMA_ARRAY_COUNT(map_directory),
                    L"data\\maps\\");
    dluma_wstr_append(map_directory, DLUMA_ARRAY_COUNT(map_directory), map_name);
    dluma_wstr_append(map_directory, DLUMA_ARRAY_COUNT(map_directory), L"\\");
    find = FindFirstFileW(pattern, &data);
    if (find == INVALID_HANDLE_VALUE)
        goto fail;
    do
    {
        if ((data.cFileName[0] == L'.' && !data.cFileName[1]) ||
            (data.cFileName[0] == L'.' && data.cFileName[1] == L'.' &&
             !data.cFileName[2]))
            continue;
        if (data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)
        {
            result = dluma_custom_menu_inventory_fail(
                inventory, "runtime content contains a reparse point");
            break;
        }
        child_relative[0] = 0;
        if (relative && relative[0])
        {
            dluma_wstr_append(child_relative,
                            DLUMA_ARRAY_COUNT(child_relative), relative);
            dluma_wstr_append(child_relative,
                            DLUMA_ARRAY_COUNT(child_relative), L"\\");
        }
        dluma_wstr_append(child_relative,
                        DLUMA_ARRAY_COUNT(child_relative), data.cFileName);
        if (dluma_custom_menu_runtime_file_excluded(
                child_relative,
                (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0))
            continue;
        if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
        {
            if (!dluma_custom_menu_scan_runtime_tree(
                    inventory, candidate, runtime_root, runtime_prefix,
                    child_relative, depth + 1U, source_root, merge))
            {
                result = 0;
                break;
            }
            continue;
        }
        if (!dluma_custom_menu_join(source, DLUMA_ARRAY_COUNT(source),
                                  runtime_root, child_relative))
        {
            result = dluma_custom_menu_inventory_fail(
                inventory, "runtime file path is too long");
            break;
        }
        destination[0] = 0;
        dluma_wstr_append(destination, DLUMA_ARRAY_COUNT(destination), runtime_prefix);
        dluma_wstr_append(destination, DLUMA_ARRAY_COUNT(destination), L"\\");
        dluma_wstr_append(destination, DLUMA_ARRAY_COUNT(destination), child_relative);
        {
            unsigned char promote = 0;
            unsigned char overlay = 0;
            if (dluma_wstricmp(destination, expected_map) == 0)
            {
                promote = 1U;
                inventory->map_found = 1U;
            }
            else if (dluma_wstricmp(destination, expected_exp) == 0)
            {
                promote = 1U;
                inventory->exp_found = 1U;
            }
            /* Level records move to the stock level name and must therefore be
             * served from the generated overlay: the project root still holds
             * them under the author's own name. */
            if (dluma_custom_menu_stock_level_destination(
                    destination, map_directory, map_name,
                    stock_destination, DLUMA_ARRAY_COUNT(stock_destination)))
            {
                dluma_wstr_copy(destination, DLUMA_ARRAY_COUNT(destination),
                              stock_destination);
                overlay = 1U;
            }
            if (merge) {
                unsigned int prior;
                for (prior = 0; prior < inventory->count; ++prior)
                    if (dluma_wstricmp(inventory->files[prior].destination_relative,
                                     destination) == 0)
                        break;
                if (prior < inventory->count) continue;
            }
            if (!dluma_custom_menu_inventory_add(
                    inventory, source, source_root,
                    destination, DLUMA_CUSTOM_MENU_MAX_RUNTIME_FILE_BYTES,
                    promote, overlay))
            {
                result = 0;
                break;
            }
            if (dluma_wstricmp(runtime_prefix, L"data") == 0)
                ++inventory->data_file_count;
            else
                ++inventory->assets_pc_file_count;
            if (dluma_custom_menu_suffix_equal(destination, L".fx"))
                ++inventory->fx_definition_count;
            if (dluma_custom_menu_suffix_equal(destination, L".mp"))
                ++inventory->material_pack_count;
            if (dluma_custom_menu_suffix_equal(destination, L".msh_obj"))
                ++inventory->compiled_mesh_count;
        }
    } while (FindNextFileW(find, &data));
    if (result && GetLastError() != ERROR_NO_MORE_FILES)
        result = 0;
    FindClose(find);
    CloseHandle(directory_handle);
    return result;
fail:
    if (directory_handle != INVALID_HANDLE_VALUE)
        CloseHandle(directory_handle);
    return dluma_custom_menu_inventory_fail(
        inventory, "runtime content directory could not be opened safely");
}

static int dluma_custom_menu_scan_runtime_root(
    DlumaCustomMenuStageInventory *inventory,
    const DlumaCustomMenuCandidate *candidate,
    const WCHAR *runtime_prefix,
    int required)
{
    WCHAR runtime_root[DLUMA_MAX_WPATH];
    DWORD attributes;
    DWORD error;
    if (!inventory || !candidate || !runtime_prefix || !runtime_prefix[0] ||
        !dluma_custom_menu_join(
            runtime_root, DLUMA_ARRAY_COUNT(runtime_root),
            candidate->snapshot.project_root, runtime_prefix))
        return dluma_custom_menu_inventory_fail(
            inventory, "runtime root path is invalid");
    attributes = GetFileAttributesW(runtime_root);
    if (attributes == INVALID_FILE_ATTRIBUTES)
    {
        error = GetLastError();
        if (!required &&
            (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND))
            return 1;
        return dluma_custom_menu_inventory_fail_destination(
            inventory, "required runtime root is unavailable: ",
            dluma_wstricmp(runtime_prefix, L"data") == 0 ? "data" :
                "assets_pc");
    }
    if (!(attributes & FILE_ATTRIBUTE_DIRECTORY) ||
        (attributes & FILE_ATTRIBUTE_REPARSE_POINT))
        return dluma_custom_menu_inventory_fail_destination(
            inventory, "runtime root is not a safe directory: ",
            dluma_wstricmp(runtime_prefix, L"data") == 0 ? "data" :
                "assets_pc");
    return dluma_custom_menu_scan_runtime_tree(
        inventory, candidate, runtime_root, runtime_prefix, L"", 0U,
        candidate->canonical_root, 0);
}

static int dluma_custom_menu_scan_asset_levels(
    DlumaCustomMenuStageInventory *inventory,
    const DlumaCustomMenuCandidate *candidate)
{
    DlumaAssetModSourceSnapshot *sources;
    unsigned int count, index;
    WCHAR map_name[DLUMA_CUSTOM_MENU_MAP_CAPACITY];
    WCHAR relative[DLUMA_MAX_WPATH];
    int result = 1;
    sources = (DlumaAssetModSourceSnapshot *)dluma_alloc(
        sizeof(*sources) * DLUMA_ASSET_MOD_MAX_ITEMS);
    if (!sources || !dluma_utf8_to_wide(candidate->manifest.map, map_name,
                                     DLUMA_ARRAY_COUNT(map_name))) {
        if (sources) dluma_free(sources);
        return 0;
    }
    relative[0] = 0;
    dluma_wstr_append(relative, DLUMA_ARRAY_COUNT(relative), L"maps\\");
    dluma_wstr_append(relative, DLUMA_ARRAY_COUNT(relative), map_name);
    count = dluma_asset_mods_snapshot_sources(sources,
        DLUMA_ASSET_MOD_MAX_ITEMS, 0);
    for (index = 0; index < count; ++index) {
        WCHAR runtime_root[DLUMA_MAX_WPATH];
        WCHAR map_root[DLUMA_MAX_WPATH];
        DWORD attributes;
        if (!dluma_custom_menu_join(runtime_root, DLUMA_ARRAY_COUNT(runtime_root),
                                  sources[index].project_root, L"data") ||
            !dluma_custom_menu_join(map_root, DLUMA_ARRAY_COUNT(map_root),
                                  runtime_root, relative)) {
            result = 0; break;
        }
        attributes = GetFileAttributesW(map_root);
        if (attributes == INVALID_FILE_ATTRIBUTES) continue;
        if (!dluma_custom_menu_scan_runtime_tree(inventory, candidate,
                runtime_root, L"data", relative, 0U,
                sources[index].project_root, 1)) {
            result = 0; break;
        }
    }
    dluma_free(sources);
    return result;
}

static void dluma_custom_menu_inventory_release(
    DlumaCustomMenuStageInventory *inventory)
{
    unsigned int index;
    if (!inventory || !inventory->files)
        return;
    for (index = 0; index < inventory->count; ++index)
        if (inventory->files[index].source != INVALID_HANDLE_VALUE)
            CloseHandle(inventory->files[index].source);
    dluma_free(inventory->files);
    memset(inventory, 0, sizeof(*inventory));
}

static int dluma_custom_menu_stage_file_compare(
    const DlumaCustomMenuStageFile *left,
    const DlumaCustomMenuStageFile *right)
{
    return dluma_stricmp(left->destination_utf8, right->destination_utf8);
}

static void dluma_custom_menu_inventory_sort(
    DlumaCustomMenuStageInventory *inventory)
{
    unsigned int index;
    for (index = 1U; index < inventory->count; ++index)
    {
        DlumaCustomMenuStageFile value = inventory->files[index];
        unsigned int position = index;
        while (position && dluma_custom_menu_stage_file_compare(
                   &value, &inventory->files[position - 1U]) < 0)
        {
            inventory->files[position] = inventory->files[position - 1U];
            --position;
        }
        inventory->files[position] = value;
    }
}

static DWORD dluma_custom_menu_crc32_update(
    DWORD crc, const unsigned char *bytes, unsigned int size)
{
    static const DWORD table[16] = {
        0x00000000UL, 0x1db71064UL, 0x3b6e20c8UL, 0x26d930acUL,
        0x76dc4190UL, 0x6b6b51f4UL, 0x4db26158UL, 0x5005713cUL,
        0xedb88320UL, 0xf00f9344UL, 0xd6d6a3e8UL, 0xcb61b38cUL,
        0x9b64c2b0UL, 0x86d3d2d4UL, 0xa00ae278UL, 0xbdbdf21cUL
    };
    unsigned int index;
    if (!bytes && size)
        return crc;
    for (index = 0; index < size; ++index)
    {
        crc ^= bytes[index];
        crc = (crc >> 4U) ^ table[crc & 0x0fU];
        crc = (crc >> 4U) ^ table[crc & 0x0fU];
    }
    return crc;
}

static int dluma_custom_menu_hash_and_crc_handle(
    DlumaCustomMenuStageFile *record)
{
    LARGE_INTEGER zero;
    DlumaSha256Context context;
    BYTE digest[32];
    BYTE buffer[16384];
    ULONGLONG remaining;
    DWORD crc = 0xffffffffUL;
    if (!record || record->source == INVALID_HANDLE_VALUE)
        return 0;
    zero.QuadPart = 0;
    remaining = record->source_size;
    if (!SetFilePointerEx(record->source, zero, 0, FILE_BEGIN))
        return 0;
    dluma_sha256_init(&context);
    while (remaining)
    {
        DWORD wanted = remaining > sizeof(buffer)
            ? (DWORD)sizeof(buffer) : (DWORD)remaining;
        DWORD read = 0;
        if (!ReadFile(record->source, buffer, wanted, &read, 0) ||
            read != wanted)
            return 0;
        dluma_sha256_update(&context, buffer, read);
        crc = dluma_custom_menu_crc32_update(crc, buffer, read);
        remaining -= read;
    }
    if (!dluma_custom_menu_source_still_same(record))
        return 0;
    dluma_sha256_final(&context, digest);
    dluma_sha256_hex(digest, record->output_sha256);
    record->output_crc32 = crc ^ 0xffffffffUL;
    return 1;
}

static int dluma_custom_menu_hash_stage_record(
    DlumaCustomMenuStageFile *record)
{
    if (!record)
        return 0;
    /* A stub has no source to read; its empty-content hash is already set. */
    if (record->neutral_stub)
        return record->source == INVALID_HANDLE_VALUE &&
               !record->output_size;
    if (record->source == INVALID_HANDLE_VALUE)
        return 0;
    if (record->promote_root)
    {
        unsigned char *input = 0;
        unsigned char *output = 0;
        unsigned int output_size = 0;
        int already = 0;
        char diagnostic[DLUMA_CUSTOM_MENU_DIAGNOSTIC_CAPACITY];
        int result = 0;
        if (record->source_size > DLUMA_CUSTOM_MENU_MAX_PROMOTED_BYTES ||
            !dluma_custom_menu_read_handle(
                record->source, &record->source_identity,
                record->source_size, &input))
            return 0;
        output = (unsigned char *)dluma_alloc((SIZE_T)record->source_size + 32U);
        if (output && dluma_custom_main_menu_promote_root_record(
                input, (unsigned int)record->source_size,
                output, (unsigned int)record->source_size + 32U,
                &output_size, &already,
                diagnostic, DLUMA_ARRAY_COUNT(diagnostic)))
        {
            record->output_size = output_size;
            dluma_sha256_bytes_hex(output, output_size, record->output_sha256);
            record->output_crc32 =
                dluma_custom_menu_crc32_update(
                    0xffffffffUL, output, output_size) ^ 0xffffffffUL;
            result = 1;
        }
        if (output)
            dluma_free(output);
        dluma_free(input);
        return result;
    }
    record->output_size = record->source_size;
    return dluma_custom_menu_hash_and_crc_handle(record);
}

static void dluma_custom_menu_hash_u32(
    DlumaSha256Context *context, unsigned int value)
{
    unsigned char encoded[4];
    encoded[0] = (unsigned char)value;
    encoded[1] = (unsigned char)(value >> 8U);
    encoded[2] = (unsigned char)(value >> 16U);
    encoded[3] = (unsigned char)(value >> 24U);
    dluma_sha256_update(context, encoded, sizeof(encoded));
}

static void dluma_custom_menu_hash_u64(
    DlumaSha256Context *context, ULONGLONG value)
{
    unsigned char encoded[8];
    unsigned int index;
    for (index = 0; index < 8U; ++index)
        encoded[index] = (unsigned char)(value >> (index * 8U));
    dluma_sha256_update(context, encoded, sizeof(encoded));
}

static void dluma_custom_menu_hash_text(
    DlumaSha256Context *context, const char *value)
{
    unsigned int length = value ? (unsigned int)strlen(value) : 0;
    dluma_custom_menu_hash_u32(context, length);
    if (length)
        dluma_sha256_update(context, value, length);
}

static int dluma_custom_menu_inventory_fingerprint(
    DlumaCustomMenuStageInventory *inventory,
    const DlumaCustomMenuCandidate *candidate,
    char output[DLUMA_SHA256_HEX_CAPACITY])
{
    DlumaSha256Context context;
    BYTE digest[32];
    unsigned int index;
    if (!inventory || !candidate || !output)
        return 0;
    if (!inventory->map_found || !inventory->exp_found)
        return dluma_custom_menu_inventory_fail(
            inventory,
            "selected map root .map and .exp were not both staged");
    dluma_custom_menu_inventory_sort(inventory);
    for (index = 0; index < inventory->count; ++index)
    {
        if (index && dluma_stricmp(
                inventory->files[index - 1U].destination_utf8,
                inventory->files[index].destination_utf8) == 0)
            return dluma_custom_menu_inventory_fail_destination(
                inventory, "duplicate staged destination: ",
                inventory->files[index].destination_utf8);
        if (!dluma_custom_menu_hash_stage_record(&inventory->files[index]))
            return dluma_custom_menu_inventory_fail_destination(
                inventory, "runtime content changed while staging: ",
                inventory->files[index].destination_utf8);
    }
    dluma_sha256_init(&context);
    dluma_custom_menu_hash_text(
        &context, "dluma.custom-main-menu.loose-overlay.v10");
    dluma_custom_menu_hash_text(&context, candidate->manifest_sha256);
    dluma_custom_menu_hash_u64(&context, inventory->asset_revision);
    dluma_custom_menu_hash_u32(&context, inventory->count);
    for (index = 0; index < inventory->count; ++index)
    {
        dluma_custom_menu_hash_text(
            &context, inventory->files[index].destination_utf8);
        dluma_custom_menu_hash_text(
            &context, inventory->files[index].asset_owner);
        dluma_custom_menu_hash_u64(
            &context, inventory->files[index].output_size);
        dluma_custom_menu_hash_text(
            &context, inventory->files[index].output_sha256);
        dluma_custom_menu_hash_u32(
            &context, inventory->files[index].output_crc32);
        dluma_custom_menu_hash_u32(
            &context, inventory->files[index].overlay_output);
    }
    dluma_sha256_final(&context, digest);
    dluma_sha256_hex(digest, output);
    return 1;
}

static int dluma_custom_menu_add_declared_assets(
    DlumaCustomMenuStageInventory *inventory,
    DlumaCustomMenuCandidate *candidate)
{
    unsigned int index;
    WCHAR relative[DLUMA_MAX_WPATH];
    WCHAR source[DLUMA_MAX_WPATH];
    WCHAR destination[DLUMA_MAX_WPATH];
    for (index = 0; index < candidate->manifest.override_count; ++index)
    {
        if (!dluma_utf8_to_wide(
                candidate->manifest.overrides[index].source_path,
                relative, DLUMA_ARRAY_COUNT(relative)) ||
            !dluma_custom_menu_join(source, DLUMA_ARRAY_COUNT(source),
                                  candidate->snapshot.project_root, relative))
            return 0;
        destination[0] = 0;
        dluma_wstr_append(destination, DLUMA_ARRAY_COUNT(destination),
                        L"data\\menu\\scr\\");
        if (!dluma_utf8_to_wide(
                candidate->manifest.overrides[index].destination,
                relative, DLUMA_ARRAY_COUNT(relative)))
            return 0;
        dluma_wstr_append(destination, DLUMA_ARRAY_COUNT(destination), relative);
        if (!dluma_custom_menu_inventory_add(
                inventory, source, candidate->canonical_root,
                destination, DLUMA_CUSTOM_MENU_MAX_XUI_BYTES, 0, 1))
            return 0;
        {
            DlumaCustomMenuStageFile *record =
                &inventory->files[inventory->count - 1U];
            unsigned char *bytes = 0;
            if (!dluma_custom_menu_read_handle(
                    record->source, &record->source_identity,
                    record->source_size, &bytes) ||
                !dluma_custom_main_menu_xui_music_override(
                    bytes, (unsigned int)record->source_size,
                    candidate->manifest.overrides[index].music_override,
                    DLUMA_ARRAY_COUNT(candidate->manifest.overrides[index]
                                        .music_override)))
            {
                if (bytes)
                    dluma_free(bytes);
                return dluma_custom_menu_inventory_fail_destination(
                    inventory, "declared XUI MusicOverride is invalid: ",
                    candidate->manifest.overrides[index].destination);
            }
            dluma_free(bytes);
        }
        if (dluma_stricmp(
                candidate->manifest.overrides[index].destination,
                DLUMA_CUSTOM_MENU_MAIN_XUI_COMPAT) == 0)
        {
            char alias_utf8[DLUMA_CUSTOM_MENU_XUI_NAME_CAPACITY];
            WCHAR alias_wide[DLUMA_CUSTOM_MENU_XUI_NAME_CAPACITY];
            destination[0] = 0;
            dluma_wstr_append(destination, DLUMA_ARRAY_COUNT(destination),
                            L"data\\menu\\scr\\");
            if (!dluma_custom_menu_main_xui_alias(
                    candidate, alias_utf8, DLUMA_ARRAY_COUNT(alias_utf8)) ||
                !dluma_utf8_to_wide(
                    alias_utf8, alias_wide, DLUMA_ARRAY_COUNT(alias_wide)))
                return dluma_custom_menu_inventory_fail(
                    inventory,
                    "custom main-menu XUI alias could not be generated");
            dluma_wstr_append(
                destination, DLUMA_ARRAY_COUNT(destination), alias_wide);
            if (!dluma_custom_menu_inventory_add(
                    inventory, source, candidate->canonical_root,
                    destination, DLUMA_CUSTOM_MENU_MAX_XUI_BYTES, 0, 1))
                return 0;
        }
    }
    for (index = 0; index < candidate->manifest.texture_def_count; ++index) {
        unsigned int prior;
        const char *path = candidate->manifest.texture_defs[index].source_path;
        char destination_utf8[DLUMA_MAX_WPATH * 2U];
        if (!dluma_utf8_to_wide(path, relative, DLUMA_ARRAY_COUNT(relative)) ||
            !dluma_custom_menu_join(source, DLUMA_ARRAY_COUNT(source),
                                  candidate->canonical_root, relative))
            return 0;
        dluma_wstr_copy(destination, DLUMA_ARRAY_COUNT(destination), relative);
        if (!dluma_wide_to_utf8(destination, destination_utf8,
                              DLUMA_ARRAY_COUNT(destination_utf8)))
            return 0;
        for (prior = 0; prior < inventory->count; ++prior)
            if (dluma_stricmp(inventory->files[prior].destination_utf8,
                            destination_utf8) == 0)
                break;
        if (prior == inventory->count &&
            !dluma_custom_menu_inventory_add(inventory, source,
                candidate->canonical_root, destination,
                DLUMA_CUSTOM_MENU_MAX_RUNTIME_FILE_BYTES, 0, 0))
            return 0;
    }
    if (candidate->manifest.use_custom_banks)
    {
        ULONGLONG total_bank_bytes = 0;
        static const char hex[] = "0123456789abcdef";
        for (index = 0; index < candidate->manifest.bank_count; ++index)
        {
            WCHAR alias[96];
            char alias_utf8[96];
            unsigned int copy_index;
            unsigned int length;
            if (!dluma_utf8_to_wide(
                    candidate->manifest.banks[index].path,
                    relative, DLUMA_ARRAY_COUNT(relative)) ||
                !dluma_custom_menu_join(
                    source, DLUMA_ARRAY_COUNT(source),
                    candidate->snapshot.project_root, relative))
                return 0;
            dluma_str_copy(
                alias_utf8, DLUMA_ARRAY_COUNT(alias_utf8), "dluma_cmm_");
            length = (unsigned int)strlen(alias_utf8);
            alias_utf8[length] = hex[(index >> 4U) & 0xfU];
            alias_utf8[length + 1U] = hex[index & 0xfU];
            alias_utf8[length + 2U] = '_';
            alias_utf8[length + 3U] = 0;
            for (copy_index = 0; copy_index < 20U; ++copy_index)
            {
                length = (unsigned int)strlen(alias_utf8);
                alias_utf8[length] =
                    candidate->manifest_sha256[copy_index];
                alias_utf8[length + 1U] = 0;
            }
            dluma_str_append(
                alias_utf8, DLUMA_ARRAY_COUNT(alias_utf8), ".csb");
            if (!dluma_utf8_to_wide(
                    alias_utf8, alias, DLUMA_ARRAY_COUNT(alias)))
                return 0;
            destination[0] = 0;
            dluma_wstr_append(
                destination, DLUMA_ARRAY_COUNT(destination),
                L"data\\custom_main_menu\\");
            dluma_wstr_append(
                destination, DLUMA_ARRAY_COUNT(destination), alias);
            if (!dluma_custom_menu_inventory_add(
                    inventory, source, candidate->canonical_root,
                    destination, DLUMA_CUSTOM_MENU_MAX_BANK_BYTES, 0, 0))
                return 0;
            if (total_bank_bytes >
                    DLUMA_CUSTOM_MENU_MAX_BANK_TOTAL_BYTES -
                        inventory->files[inventory->count - 1U].source_size)
                return dluma_custom_menu_inventory_fail(
                    inventory,
                    "custom-menu bank aggregate byte limit exceeded");
            total_bank_bytes +=
                inventory->files[inventory->count - 1U].source_size;
            inventory->files[inventory->count - 1U]
                .declared_bank_index_plus_one = (unsigned char)(index + 1U);
        }
    }
    return 1;
}

static int dluma_custom_menu_validate_pinned_audio(
    DlumaCustomMenuStageInventory *inventory,
    const DlumaCustomMenuCandidate *candidate)
{
    DlumaCompanionArchiveAudioManifest catalogs[DLUMA_CUSTOM_MENU_MAX_BANKS];
    char hashes[DLUMA_CUSTOM_MENU_MAX_BANKS][DLUMA_SHA256_HEX_CAPACITY];
    unsigned char found[DLUMA_CUSTOM_MENU_MAX_BANKS];
    unsigned int index;
    if (!candidate->manifest.use_custom_banks)
        return 1;
    memset(catalogs, 0, sizeof(catalogs));
    memset(hashes, 0, sizeof(hashes));
    memset(found, 0, sizeof(found));
    for (index = 0; index < inventory->count; ++index)
    {
        DlumaCustomMenuStageFile *record = &inventory->files[index];
        unsigned int bank_index;
        if (!record->declared_bank_index_plus_one)
            continue;
        bank_index = (unsigned int)record->declared_bank_index_plus_one - 1U;
        if (bank_index >= candidate->manifest.bank_count ||
            found[bank_index] || record->source_size > 0xffffffffULL ||
            strlen(record->output_sha256) != 64U ||
            !dluma_companion_archive_catalog_locked_bank(
                record->source, &record->source_identity,
                (DWORD)record->source_size, &catalogs[bank_index]))
            return dluma_custom_menu_inventory_fail(
                inventory, "pinned custom-menu CSB catalog is invalid");
        found[bank_index] = 1U;
        dluma_str_copy(
            hashes[bank_index], DLUMA_ARRAY_COUNT(hashes[bank_index]),
            record->output_sha256);
    }
    for (index = 0; index < candidate->manifest.bank_count; ++index)
        if (!found[index])
            return dluma_custom_menu_inventory_fail(
                inventory, "pinned custom-menu CSB is missing");
    if (!dluma_custom_menu_catalogs_valid(
            &candidate->manifest, catalogs, hashes))
        return dluma_custom_menu_inventory_fail(
            inventory, "custom-menu cue catalog contract is invalid");
    return 1;
}

static int dluma_custom_menu_validate_declared_texture_defs(
    DlumaCustomMenuStageInventory *inventory,
    const DlumaCustomMenuCandidate *candidate)
{
    unsigned int texture_index;
    if (!inventory || !candidate)
        return 0;
    if (!candidate->manifest.use_custom_textures)
        return candidate->manifest.texture_def_count == 0U;
    for (texture_index = 0;
         texture_index < candidate->manifest.texture_def_count;
         ++texture_index)
    {
        const DlumaCustomMainMenuTextureDef *definition =
            &candidate->manifest.texture_defs[texture_index];
        unsigned int inventory_index;
        int found = 0;
        for (inventory_index = 0;
             inventory_index < inventory->count; ++inventory_index)
        {
            const DlumaCustomMenuStageFile *record =
                &inventory->files[inventory_index];
            const char *left = record->destination_utf8;
            const char *right = definition->source_path;
            int paths_equal = 1;
            while (*left || *right)
            {
                unsigned char left_value = (unsigned char)*left;
                unsigned char right_value = (unsigned char)*right;
                if (left_value == '/') left_value = '\\';
                if (right_value == '/') right_value = '\\';
                if (left_value >= 'A' && left_value <= 'Z')
                    left_value = (unsigned char)(left_value - 'A' + 'a');
                if (right_value >= 'A' && right_value <= 'Z')
                    right_value = (unsigned char)(right_value - 'A' + 'a');
                if (left_value != right_value)
                {
                    paths_equal = 0;
                    break;
                }
                if (*left) ++left;
                if (*right) ++right;
            }
            if (!paths_equal)
                continue;
            if (found || record->source == INVALID_HANDLE_VALUE ||
                record->source_size == 0ULL || record->neutral_stub)
                return dluma_custom_menu_inventory_fail_destination(
                    inventory,
                    "declared texture definition is missing or empty: ",
                    definition->name);
            found = 1;
        }
        if (!found)
            return dluma_custom_menu_inventory_fail_destination(
                inventory,
                "declared texture definition is missing or empty: ",
                definition->name);
    }
    return 1;
}

static int dluma_custom_menu_build_inventory(
    DlumaCustomMenuCandidate *candidate,
    DlumaCustomMenuStageInventory *inventory)
{
    if (!candidate || !inventory)
        return 0;
    memset(inventory, 0, sizeof(*inventory));
    inventory->asset_revision = dluma_asset_mods_desired_revision();
    {
        DlumaSha256Context context;
        BYTE digest[32];
        dluma_sha256_init(&context);
        dluma_custom_menu_hash_text(&context, candidate->manifest_sha256);
        dluma_custom_menu_hash_u64(&context, inventory->asset_revision);
        dluma_sha256_final(&context, digest);
        dluma_sha256_hex(digest, candidate->staging_identity);
    }
    inventory->files = (DlumaCustomMenuStageFile *)dluma_alloc(
        (SIZE_T)DLUMA_CUSTOM_MENU_MAX_STAGE_FILES *
        sizeof(*inventory->files));
    if (!inventory->files)
        return 0;
    memset(inventory->files, 0,
           (SIZE_T)DLUMA_CUSTOM_MENU_MAX_STAGE_FILES *
               sizeof(*inventory->files));
    if (!dluma_custom_menu_scan_runtime_root(
            inventory, candidate, L"data", 1) ||
        !dluma_custom_menu_scan_runtime_root(
            inventory, candidate, L"assets_pc", 0) ||
        !dluma_custom_menu_scan_asset_levels(inventory, candidate) ||
        !dluma_custom_menu_add_declared_assets(inventory, candidate) ||
        !dluma_custom_menu_complete_published_level(inventory) ||
        !dluma_custom_menu_validate_declared_texture_defs(
            inventory, candidate) ||
        !dluma_custom_menu_inventory_fingerprint(
            inventory, candidate, candidate->snapshot.fingerprint) ||
        !dluma_custom_menu_validate_pinned_audio(inventory, candidate) ||
        inventory->asset_revision != dluma_asset_mods_desired_revision())
    {
        if (inventory->diagnostic[0])
            dluma_custom_menu_text(
                candidate->snapshot.diagnostic,
                DLUMA_ARRAY_COUNT(candidate->snapshot.diagnostic),
                inventory->diagnostic);
        dluma_custom_menu_inventory_release(inventory);
        return 0;
    }
    return 1;
}

static int dluma_custom_menu_materialize_declared_banks(
    const DlumaCustomMenuCandidate *candidate,
    DlumaCustomMenuStageInventory *inventory,
    DlumaCustomMenuPreparedBank *banks, unsigned int *bank_count)
{
    DlumaCustomMenuStageFile *records[DLUMA_CUSTOM_MENU_MAX_BANKS];
    DlumaCompanionArchiveAudioManifest catalogs[DLUMA_CUSTOM_MENU_MAX_BANKS];
    char hashes[DLUMA_CUSTOM_MENU_MAX_BANKS][DLUMA_SHA256_HEX_CAPACITY];
    static const char hex[] = "0123456789abcdef";
    unsigned int index;
    if (!candidate || !inventory || !inventory->files || !banks || !bank_count)
        return 0;
    *bank_count = 0;
    if (!candidate->manifest.use_custom_banks)
        return 1;
    memset(records, 0, sizeof(records));
    memset(catalogs, 0, sizeof(catalogs));
    memset(hashes, 0, sizeof(hashes));
    memset(banks, 0, sizeof(*banks) * DLUMA_CUSTOM_MENU_MAX_BANKS);
    for (index = 0; index < inventory->count; ++index)
    {
        DlumaCustomMenuStageFile *record = &inventory->files[index];
        if (record->declared_bank_index_plus_one)
        {
            unsigned int bank_index =
                (unsigned int)record->declared_bank_index_plus_one - 1U;
            if (bank_index >= candidate->manifest.bank_count ||
                records[bank_index])
                return 0;
            records[bank_index] = record;
        }
    }
    for (index = 0; index < candidate->manifest.bank_count; ++index)
    {
        DlumaCustomMenuStageFile *record = records[index];
        DlumaCustomMenuPreparedBank *prepared =
            &banks[index];
        unsigned int owner_length;
        if (!record || !record->source_size ||
            record->source_size > 0xffffffffULL ||
            strlen(record->output_sha256) != 64U ||
            !dluma_companion_archive_catalog_locked_bank(
                record->source, &record->source_identity,
                (DWORD)record->source_size, &catalogs[index]) ||
            !dluma_companion_archive_materialize_locked_bank(
                &prepared->materialized, record->source,
                &record->source_identity, (DWORD)record->source_size,
                record->output_sha256))
            return 0;
        prepared->catalog = catalogs[index];
        dluma_str_copy(
            hashes[index], DLUMA_ARRAY_COUNT(hashes[index]),
            record->output_sha256);
        dluma_str_copy(
            prepared->sha256, DLUMA_ARRAY_COUNT(prepared->sha256),
            record->output_sha256);
        dluma_str_copy(
            prepared->alias, DLUMA_ARRAY_COUNT(prepared->alias),
            prepared->materialized.bank_name);
        dluma_str_copy(
            prepared->owner_id, DLUMA_ARRAY_COUNT(prepared->owner_id),
            DLUMA_SOURCE_OWNER_ID_CUSTOM_MAIN_MENU_BANK);
        owner_length = (unsigned int)strlen(prepared->owner_id);
        prepared->owner_id[owner_length] = '.';
        prepared->owner_id[owner_length + 1U] =
            hex[(index >> 4U) & 0xfU];
        prepared->owner_id[owner_length + 2U] = hex[index & 0xfU];
        prepared->owner_id[owner_length + 3U] = '.';
        memcpy(prepared->owner_id + owner_length + 4U,
               record->output_sha256, 16U);
        prepared->owner_id[owner_length + 20U] = 0;
        if (!prepared->alias[0])
            return 0;
    }
    if (!dluma_custom_menu_catalogs_valid(
            &candidate->manifest, catalogs, hashes))
        return 0;
    *bank_count = candidate->manifest.bank_count;
    dluma_log_pair(
        "INFO", "custom_main_menu_bank_materialization",
        "locked_physical_caches_ready");
    return 1;
}

static int dluma_custom_menu_source_still_same(
    DlumaCustomMenuStageFile *record)
{
    BY_HANDLE_FILE_INFORMATION after;
    memset(&after, 0, sizeof(after));
    return record && record->source != INVALID_HANDLE_VALUE &&
           GetFileInformationByHandle(record->source, &after) &&
           dluma_custom_menu_identity_equal(&record->source_identity, &after);
}

static int dluma_custom_menu_stage_copy_failure(
    const DlumaCustomMenuStageFile *record,
    const char *reason,
    DWORD error)
{
    dluma_log_pair(
        "WARN", "custom_main_menu_overlay_copy_asset",
        record && record->destination_utf8[0]
            ? record->destination_utf8 : "unknown");
    dluma_log_pair(
        "WARN", "custom_main_menu_overlay_copy_failure",
        reason ? reason : "unknown");
    if (error)
        dluma_log_hex(
            "WARN", "custom_main_menu_overlay_copy_win32_error", error);
    return 0;
}

static int dluma_custom_menu_write_exact(
    HANDLE output, const unsigned char *bytes, ULONGLONG size)
{
    ULONGLONG offset = 0;
    while (offset < size)
    {
        DWORD wanted = size - offset > 1024U * 1024U
            ? 1024U * 1024U : (DWORD)(size - offset);
        DWORD written = 0;
        if (!WriteFile(output, bytes + (SIZE_T)offset,
                       wanted, &written, 0) || written != wanted)
            return 0;
        offset += wanted;
    }
    return 1;
}

#if 0
static void dluma_custom_menu_zip_put_u16(
    unsigned char *bytes, unsigned int offset, unsigned int value)
{
    bytes[offset] = (unsigned char)value;
    bytes[offset + 1U] = (unsigned char)(value >> 8U);
}

static void dluma_custom_menu_zip_put_u32(
    unsigned char *bytes, unsigned int offset, DWORD value)
{
    bytes[offset] = (unsigned char)value;
    bytes[offset + 1U] = (unsigned char)(value >> 8U);
    bytes[offset + 2U] = (unsigned char)(value >> 16U);
    bytes[offset + 3U] = (unsigned char)(value >> 24U);
}

static int dluma_custom_menu_zip_name(
    const DlumaCustomMenuStageFile *record,
    char *output, unsigned int capacity, unsigned int *length_out)
{
    unsigned int index = 0;
    if (length_out)
        *length_out = 0;
    if (!record || !output || capacity < 2U ||
        !record->destination_utf8[0])
        return 0;
    while (record->destination_utf8[index])
    {
        char value;
        if (index + 1U >= capacity || index >= 0xffffU)
            return 0;
        value = record->destination_utf8[index];
        output[index] = value == '\\' ? '/' : value;
        ++index;
    }
    output[index] = 0;
    if (!index || output[0] == '/' || output[index - 1U] == '/')
        return 0;
    if (length_out)
        *length_out = index;
    return 1;
}

static int dluma_custom_menu_archive_emit(
    HANDLE output, DlumaSha256Context *archive_hash,
    ULONGLONG *position, const unsigned char *bytes, unsigned int size)
{
    if (!archive_hash || !position || (!bytes && size) ||
        *position > DLUMA_CUSTOM_MENU_MAX_ARCHIVE_BYTES ||
        (ULONGLONG)size >
            DLUMA_CUSTOM_MENU_MAX_ARCHIVE_BYTES - *position ||
        *position + size > 0xffffffffULL)
        return 0;
    if (size)
    {
        dluma_sha256_update(archive_hash, bytes, size);
        if (output != INVALID_HANDLE_VALUE &&
            !dluma_custom_menu_write_exact(output, bytes, size))
            return 0;
    }
    *position += size;
    return 1;
}

static int dluma_custom_menu_archive_emit_payload(
    HANDLE output, DlumaSha256Context *archive_hash,
    ULONGLONG *position, DlumaCustomMenuStageFile *record)
{
    LARGE_INTEGER zero;
    DlumaSha256Context payload_hash;
    BYTE payload_digest[32];
    char payload_sha256[DLUMA_SHA256_HEX_CAPACITY];
    DWORD crc = 0xffffffffUL;
    ULONGLONG remaining;
    BYTE *buffer = 0;
    int result = 0;
    if (!record)
        return 0;
    /* A stub contributes an empty payload to the archive. */
    if (record->neutral_stub)
        return dluma_custom_menu_archive_emit(
            output, archive_hash, position, 0, 0);
    if (record->source == INVALID_HANDLE_VALUE)
        return 0;
    if (record->promote_root)
    {
        unsigned char *input = 0;
        unsigned char *promoted = 0;
        unsigned int promoted_size = 0;
        int already = 0;
        char diagnostic[DLUMA_CUSTOM_MENU_DIAGNOSTIC_CAPACITY];
        if (!dluma_custom_menu_read_handle(
                record->source, &record->source_identity,
                record->source_size, &input))
            return dluma_custom_menu_stage_copy_failure(
                record, "promoted_source_snapshot_changed", 0);
        promoted = (unsigned char *)dluma_alloc(
            (SIZE_T)record->source_size + 32U);
        if (promoted && dluma_custom_main_menu_promote_root_record(
                input, (unsigned int)record->source_size,
                promoted, (unsigned int)record->source_size + 32U,
                &promoted_size, &already,
                diagnostic, DLUMA_ARRAY_COUNT(diagnostic)) &&
            promoted_size == record->output_size)
        {
            dluma_sha256_bytes_hex(
                promoted, promoted_size, payload_sha256);
            crc = dluma_custom_menu_crc32_update(
                crc, promoted, promoted_size) ^ 0xffffffffUL;
            if (dluma_stricmp(payload_sha256, record->output_sha256) == 0 &&
                crc == record->output_crc32)
                result = dluma_custom_menu_archive_emit(
                    output, archive_hash, position,
                    promoted, promoted_size);
        }
        if (promoted)
            dluma_free(promoted);
        dluma_free(input);
        if (!result)
            return dluma_custom_menu_stage_copy_failure(
                record, "promoted_archive_payload_changed", 0);
        return 1;
    }
    buffer = (BYTE *)dluma_alloc(DLUMA_CUSTOM_MENU_STAGE_COPY_BUFFER_BYTES);
    if (!buffer)
        return dluma_custom_menu_stage_copy_failure(
            record, "stage_copy_buffer_allocation_failed", 0);
    zero.QuadPart = 0;
    remaining = record->source_size;
    if (!SetFilePointerEx(record->source, zero, 0, FILE_BEGIN))
        goto done;
    dluma_sha256_init(&payload_hash);
    result = 1;
    while (remaining)
    {
        DWORD wanted = remaining > DLUMA_CUSTOM_MENU_STAGE_COPY_BUFFER_BYTES
            ? DLUMA_CUSTOM_MENU_STAGE_COPY_BUFFER_BYTES : (DWORD)remaining;
        DWORD read = 0;
        if (!ReadFile(record->source, buffer, wanted, &read, 0) ||
            read != wanted ||
            !dluma_custom_menu_archive_emit(
                output, archive_hash, position, buffer, read))
        {
            result = 0;
            break;
        }
        dluma_sha256_update(&payload_hash, buffer, read);
        crc = dluma_custom_menu_crc32_update(crc, buffer, read);
        remaining -= read;
    }
    if (result)
    {
        dluma_sha256_final(&payload_hash, payload_digest);
        dluma_sha256_hex(payload_digest, payload_sha256);
        result = dluma_custom_menu_source_still_same(record) &&
                 dluma_stricmp(
                     payload_sha256, record->output_sha256) == 0 &&
                 (crc ^ 0xffffffffUL) == record->output_crc32;
    }
done:
    dluma_free(buffer);
    if (!result)
        return dluma_custom_menu_stage_copy_failure(
            record, "archive_payload_changed_or_write_failed",
            GetLastError());
    return 1;
}

static int dluma_custom_menu_stream_archive(
    HANDLE output, DlumaCustomMenuStageInventory *inventory,
    char archive_sha256[DLUMA_SHA256_HEX_CAPACITY],
    ULONGLONG *archive_size_out)
{
    DWORD *local_offsets = 0;
    DlumaSha256Context archive_hash;
    BYTE digest[32];
    ULONGLONG position = 0;
    ULONGLONG central_start;
    ULONGLONG central_size;
    unsigned int index;
    int result = 0;
    if (archive_sha256)
        archive_sha256[0] = 0;
    if (archive_size_out)
        *archive_size_out = 0;
    if (!inventory || !inventory->files || !inventory->count ||
        inventory->count > DLUMA_CUSTOM_MENU_MAX_STAGE_FILES ||
        inventory->count > 0xffffU)
        return 0;
    local_offsets = (DWORD *)dluma_alloc(
        (SIZE_T)inventory->count * sizeof(*local_offsets));
    if (!local_offsets)
        return 0;
    dluma_sha256_init(&archive_hash);
    for (index = 0; index < inventory->count; ++index)
    {
        DlumaCustomMenuStageFile *record = &inventory->files[index];
        unsigned char header[30];
        char name[DLUMA_MAX_WPATH * 2U];
        unsigned int name_length = 0;
        if (record->output_size > 0xffffffffULL ||
            position > 0xffffffffULL ||
            !dluma_custom_menu_zip_name(
                record, name, DLUMA_ARRAY_COUNT(name), &name_length))
            goto done;
        local_offsets[index] = (DWORD)position;
        memset(header, 0, sizeof(header));
        dluma_custom_menu_zip_put_u32(
            header, 0U, DLUMA_CUSTOM_MENU_ZIP_LOCAL_SIGNATURE);
        dluma_custom_menu_zip_put_u16(
            header, 4U, DLUMA_CUSTOM_MENU_ZIP_VERSION);
        dluma_custom_menu_zip_put_u16(
            header, 6U, DLUMA_CUSTOM_MENU_ZIP_UTF8_FLAG);
        dluma_custom_menu_zip_put_u16(
            header, 12U, DLUMA_CUSTOM_MENU_ZIP_FIXED_DATE);
        dluma_custom_menu_zip_put_u32(
            header, 14U, record->output_crc32);
        dluma_custom_menu_zip_put_u32(
            header, 18U, (DWORD)record->output_size);
        dluma_custom_menu_zip_put_u32(
            header, 22U, (DWORD)record->output_size);
        dluma_custom_menu_zip_put_u16(header, 26U, name_length);
        if (!dluma_custom_menu_archive_emit(
                output, &archive_hash, &position,
                header, sizeof(header)) ||
            !dluma_custom_menu_archive_emit(
                output, &archive_hash, &position,
                (const unsigned char *)name, name_length) ||
            !dluma_custom_menu_archive_emit_payload(
                output, &archive_hash, &position, record))
            goto done;
    }
    central_start = position;
    for (index = 0; index < inventory->count; ++index)
    {
        DlumaCustomMenuStageFile *record = &inventory->files[index];
        unsigned char header[46];
        char name[DLUMA_MAX_WPATH * 2U];
        unsigned int name_length = 0;
        if (!dluma_custom_menu_zip_name(
                record, name, DLUMA_ARRAY_COUNT(name), &name_length))
            goto done;
        memset(header, 0, sizeof(header));
        dluma_custom_menu_zip_put_u32(
            header, 0U, DLUMA_CUSTOM_MENU_ZIP_CENTRAL_SIGNATURE);
        dluma_custom_menu_zip_put_u16(
            header, 4U, DLUMA_CUSTOM_MENU_ZIP_VERSION);
        dluma_custom_menu_zip_put_u16(
            header, 6U, DLUMA_CUSTOM_MENU_ZIP_VERSION);
        dluma_custom_menu_zip_put_u16(
            header, 8U, DLUMA_CUSTOM_MENU_ZIP_UTF8_FLAG);
        dluma_custom_menu_zip_put_u16(
            header, 14U, DLUMA_CUSTOM_MENU_ZIP_FIXED_DATE);
        dluma_custom_menu_zip_put_u32(
            header, 16U, record->output_crc32);
        dluma_custom_menu_zip_put_u32(
            header, 20U, (DWORD)record->output_size);
        dluma_custom_menu_zip_put_u32(
            header, 24U, (DWORD)record->output_size);
        dluma_custom_menu_zip_put_u16(header, 28U, name_length);
        dluma_custom_menu_zip_put_u32(
            header, 42U, local_offsets[index]);
        if (!dluma_custom_menu_archive_emit(
                output, &archive_hash, &position,
                header, sizeof(header)) ||
            !dluma_custom_menu_archive_emit(
                output, &archive_hash, &position,
                (const unsigned char *)name, name_length))
            goto done;
    }
    central_size = position - central_start;
    if (central_start > 0xffffffffULL || central_size > 0xffffffffULL)
        goto done;
    {
        unsigned char eocd[22];
        memset(eocd, 0, sizeof(eocd));
        dluma_custom_menu_zip_put_u32(
            eocd, 0U, DLUMA_CUSTOM_MENU_ZIP_EOCD_SIGNATURE);
        dluma_custom_menu_zip_put_u16(
            eocd, 8U, inventory->count);
        dluma_custom_menu_zip_put_u16(
            eocd, 10U, inventory->count);
        dluma_custom_menu_zip_put_u32(
            eocd, 12U, (DWORD)central_size);
        dluma_custom_menu_zip_put_u32(
            eocd, 16U, (DWORD)central_start);
        if (!dluma_custom_menu_archive_emit(
                output, &archive_hash, &position, eocd, sizeof(eocd)))
            goto done;
    }
    dluma_sha256_final(&archive_hash, digest);
    if (archive_sha256)
        dluma_sha256_hex(digest, archive_sha256);
    if (archive_size_out)
        *archive_size_out = position;
    result = 1;
done:
    dluma_free(local_offsets);
    return result;
}

static void dluma_custom_menu_release_stage_handles(void)
{
    unsigned int index;
    for (index = 0; index < g_dluma_custom_menu_stage_handle_count; ++index)
        if (g_dluma_custom_menu_stage_handles[index] != INVALID_HANDLE_VALUE)
            CloseHandle(g_dluma_custom_menu_stage_handles[index]);
    memset(g_dluma_custom_menu_stage_handles, 0,
           sizeof(g_dluma_custom_menu_stage_handles));
    g_dluma_custom_menu_stage_handle_count = 0;
}

static int dluma_custom_menu_validate_and_lock_stage(
    const WCHAR *stage_path,
    const WCHAR *stage_parent,
    const DlumaCustomMenuStageInventory *inventory)
{
    WCHAR canonical_root[DLUMA_MAX_WPATH];
    HANDLE directory = INVALID_HANDLE_VALUE;
    BY_HANDLE_FILE_INFORMATION directory_identity;
    WCHAR canonical[DLUMA_MAX_WPATH];
    BY_HANDLE_FILE_INFORMATION identity;
    ULONGLONG size = 0;
    ULONGLONG expected_size = 0;
    char expected_sha256[DLUMA_SHA256_HEX_CAPACITY];
    char actual_sha256[DLUMA_SHA256_HEX_CAPACITY];
    HANDLE file = INVALID_HANDLE_VALUE;
    DlumaCustomMenuStageInventory *mutable_inventory =
        (DlumaCustomMenuStageInventory *)inventory;
    dluma_custom_menu_release_stage_handles();
    if (!dluma_custom_menu_open_directory(
            stage_parent, &directory, canonical_root,
            DLUMA_ARRAY_COUNT(canonical_root), &directory_identity))
        goto fail;
    file = dluma_custom_menu_open_regular_file(
        stage_path, canonical_root, DLUMA_CUSTOM_MENU_MAX_ARCHIVE_BYTES,
        0, &identity, &size, canonical, DLUMA_ARRAY_COUNT(canonical));
    if (file == INVALID_HANDLE_VALUE ||
        !dluma_custom_menu_stream_archive(
            INVALID_HANDLE_VALUE, mutable_inventory,
            expected_sha256, &expected_size) ||
        size != expected_size ||
        !dluma_sha256_handle_hex(
            file, DLUMA_CUSTOM_MENU_MAX_ARCHIVE_BYTES, actual_sha256) ||
        dluma_stricmp(actual_sha256, expected_sha256) != 0)
        goto fail;
    g_dluma_custom_menu_stage_handles[0] = file;
    g_dluma_custom_menu_stage_handle_count = 1U;
    CloseHandle(directory);
    return 1;
fail:
    if (file != INVALID_HANDLE_VALUE)
        CloseHandle(file);
    if (directory != INVALID_HANDLE_VALUE)
        CloseHandle(directory);
    dluma_custom_menu_release_stage_handles();
    return 0;
}

static int dluma_custom_menu_stage_inventory(
    DlumaCustomMenuCandidate *candidate,
    DlumaCustomMenuStageInventory *inventory)
{
    WCHAR runtime_root[DLUMA_MAX_WPATH];
    WCHAR stage_parent[DLUMA_MAX_WPATH];
    WCHAR fingerprint_wide[DLUMA_SHA256_HEX_CAPACITY];
    WCHAR fingerprint_prefix[DLUMA_CUSTOM_MENU_TEMP_FINGERPRINT_CHARS + 1U];
    WCHAR final_stage[DLUMA_MAX_WPATH];
    WCHAR temporary_name[64];
    WCHAR temporary_stage[DLUMA_MAX_WPATH];
    HANDLE archive = INVALID_HANDLE_VALUE;
    char archive_sha256[DLUMA_SHA256_HEX_CAPACITY];
    ULONGLONG archive_size = 0;
    unsigned int prefix_length;
    int archive_written = 0;
    if (!dluma_custom_menu_join(runtime_root, DLUMA_ARRAY_COUNT(runtime_root),
                              g_dluma.module_dir, L"dluma_runtime") ||
        !dluma_custom_menu_join(stage_parent, DLUMA_ARRAY_COUNT(stage_parent),
                              runtime_root, L"custom_main_menu\\stages") ||
        !dluma_mkdirs(stage_parent) ||
        !dluma_utf8_to_wide(candidate->snapshot.fingerprint,
                          fingerprint_wide,
                          DLUMA_ARRAY_COUNT(fingerprint_wide)) ||
        !dluma_custom_menu_join(final_stage, DLUMA_ARRAY_COUNT(final_stage),
                              stage_parent, fingerprint_wide))
        return 0;
    dluma_wstr_append(final_stage, DLUMA_ARRAY_COUNT(final_stage), L".pak");
    if (!dluma_custom_menu_suffix_equal(final_stage, L".pak"))
        return 0;
    if (dluma_path_exists(final_stage))
    {
        if (!dluma_custom_menu_validate_and_lock_stage(
                final_stage, stage_parent, inventory))
            return 0;
        dluma_wstr_copy(g_dluma_custom_menu_stage_path,
                      DLUMA_ARRAY_COUNT(g_dluma_custom_menu_stage_path),
                      final_stage);
        return 1;
    }
    /* The full 64-character fingerprint remains the final content-addressed
     * PAK name. Do not repeat it in the temporary name: a bounded fingerprint
     * prefix plus PID and monotonic tick gives this process-private atomic
     * staging file a collision-resistant name without approaching MAX_PATH. */
    prefix_length = 0;
    while (prefix_length < DLUMA_CUSTOM_MENU_TEMP_FINGERPRINT_CHARS &&
           fingerprint_wide[prefix_length])
    {
        fingerprint_prefix[prefix_length] =
            fingerprint_wide[prefix_length];
        ++prefix_length;
    }
    fingerprint_prefix[prefix_length] = 0;
    if (prefix_length != DLUMA_CUSTOM_MENU_TEMP_FINGERPRINT_CHARS)
        return 0;
    temporary_name[0] = 0;
    dluma_wstr_append(temporary_name, DLUMA_ARRAY_COUNT(temporary_name), L".tmp.");
    dluma_wstr_append(temporary_name, DLUMA_ARRAY_COUNT(temporary_name),
                    fingerprint_prefix);
    dluma_wstr_append(temporary_name, DLUMA_ARRAY_COUNT(temporary_name), L".");
    dluma_custom_menu_wide_hex_append(
        temporary_name, DLUMA_ARRAY_COUNT(temporary_name),
        GetCurrentProcessId(), 8U);
    dluma_wstr_append(temporary_name, DLUMA_ARRAY_COUNT(temporary_name), L".");
    dluma_custom_menu_wide_hex_append(
        temporary_name, DLUMA_ARRAY_COUNT(temporary_name),
        GetTickCount64(), 16U);
    dluma_wstr_append(temporary_name, DLUMA_ARRAY_COUNT(temporary_name), L".pak");
    if (!dluma_custom_menu_join(temporary_stage,
                              DLUMA_ARRAY_COUNT(temporary_stage),
                              stage_parent, temporary_name))
        return 0;
    archive = CreateFileW(
        temporary_stage, GENERIC_WRITE, 0, 0, CREATE_NEW,
        FILE_ATTRIBUTE_NORMAL, 0);
    if (archive == INVALID_HANDLE_VALUE)
        return dluma_custom_menu_stage_copy_failure(
            0, "stage_archive_create_failed", GetLastError());
    archive_written =
        dluma_custom_menu_stream_archive(
            archive, inventory, archive_sha256, &archive_size) &&
        archive_size > 0U && archive_sha256[0] &&
        FlushFileBuffers(archive);
    CloseHandle(archive);
    archive = INVALID_HANDLE_VALUE;
    if (!archive_written)
    {
        DWORD failure_error = GetLastError();
        DeleteFileW(temporary_stage);
        return dluma_custom_menu_stage_copy_failure(
            0, "stage_archive_write_or_flush_failed", failure_error);
    }
    if (!MoveFileExW(temporary_stage, final_stage,
                     MOVEFILE_WRITE_THROUGH))
    {
        DWORD move_error = GetLastError();
        if (move_error != ERROR_ALREADY_EXISTS ||
            !dluma_path_exists(final_stage))
        {
            DeleteFileW(temporary_stage);
            return 0;
        }
        DeleteFileW(temporary_stage);
    }
    if (!dluma_custom_menu_validate_and_lock_stage(
            final_stage, stage_parent, inventory))
        return 0;
    dluma_wstr_copy(g_dluma_custom_menu_stage_path,
                  DLUMA_ARRAY_COUNT(g_dluma_custom_menu_stage_path), final_stage);
    return 1;
}
#endif

static int dluma_custom_menu_overlay_payload(
    HANDLE output, DlumaCustomMenuStageFile *record)
{
    LARGE_INTEGER zero;
    unsigned char *buffer = 0;
    int result = 0;
    if (output == INVALID_HANDLE_VALUE || !record ||
        !record->overlay_output)
        return 0;
    /* A neutral stub carries no payload: the empty file is the whole point. */
    if (record->neutral_stub)
        return record->source == INVALID_HANDLE_VALUE &&
               !record->output_size;
    if (record->source == INVALID_HANDLE_VALUE)
        return 0;
    zero.QuadPart = 0;
    if (!SetFilePointerEx(record->source, zero, 0, FILE_BEGIN))
        return 0;
    if (record->promote_root)
    {
        unsigned char *input = 0;
        unsigned char *promoted = 0;
        unsigned int promoted_size = 0;
        int already = 0;
        char diagnostic[DLUMA_CUSTOM_MENU_DIAGNOSTIC_CAPACITY];
        if (record->source_size > DLUMA_CUSTOM_MENU_MAX_PROMOTED_BYTES ||
            !dluma_custom_menu_read_handle(
                record->source, &record->source_identity,
                record->source_size, &input))
            return 0;
        promoted = (unsigned char *)dluma_alloc(
            (SIZE_T)record->source_size + 32U);
        if (promoted &&
            dluma_custom_main_menu_promote_root_record(
                input, (unsigned int)record->source_size,
                promoted, (unsigned int)record->source_size + 32U,
                &promoted_size, &already, diagnostic,
                DLUMA_ARRAY_COUNT(diagnostic)) &&
            promoted_size == record->output_size &&
            dluma_custom_menu_write_exact(
                output, promoted, promoted_size) &&
            dluma_custom_menu_source_still_same(record))
            result = 1;
        if (promoted)
            dluma_free(promoted);
        dluma_free(input);
        return result;
    }
    buffer = (unsigned char *)dluma_alloc(
        DLUMA_CUSTOM_MENU_STAGE_COPY_BUFFER_BYTES);
    if (buffer)
    {
        ULONGLONG remaining = record->source_size;
        result = 1;
        while (remaining)
        {
            DWORD wanted = remaining >
                    DLUMA_CUSTOM_MENU_STAGE_COPY_BUFFER_BYTES
                ? DLUMA_CUSTOM_MENU_STAGE_COPY_BUFFER_BYTES
                : (DWORD)remaining;
            DWORD read = 0;
            if (!ReadFile(record->source, buffer, wanted, &read, 0) ||
                read != wanted ||
                !dluma_custom_menu_write_exact(output, buffer, read))
            {
                result = 0;
                break;
            }
            remaining -= read;
        }
        if (result && !dluma_custom_menu_source_still_same(record))
            result = 0;
        dluma_free(buffer);
    }
    return result;
}

static int dluma_custom_menu_overlay_count_files(
    const WCHAR *root, const WCHAR *directory,
    unsigned int depth, unsigned int *count)
{
    WCHAR canonical[DLUMA_MAX_WPATH];
    WCHAR pattern[DLUMA_MAX_WPATH];
    WCHAR child[DLUMA_MAX_WPATH];
    HANDLE directory_handle = INVALID_HANDLE_VALUE;
    BY_HANDLE_FILE_INFORMATION identity;
    WIN32_FIND_DATAW data;
    HANDLE find = INVALID_HANDLE_VALUE;
    int result = 1;
    if (!root || !directory || !count ||
        depth > DLUMA_CUSTOM_MENU_MAX_TREE_DEPTH ||
        !dluma_custom_menu_open_directory(
            directory, &directory_handle, canonical,
            DLUMA_ARRAY_COUNT(canonical), &identity) ||
        !dluma_custom_menu_wide_under(canonical, root) ||
        !dluma_custom_menu_join(
            pattern, DLUMA_ARRAY_COUNT(pattern), directory, L"*"))
        goto failed;
    find = FindFirstFileW(pattern, &data);
    if (find == INVALID_HANDLE_VALUE)
        goto failed;
    do
    {
        if ((data.cFileName[0] == L'.' && !data.cFileName[1]) ||
            (data.cFileName[0] == L'.' && data.cFileName[1] == L'.' &&
             !data.cFileName[2]))
            continue;
        if ((data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) ||
            !dluma_custom_menu_join(
                child, DLUMA_ARRAY_COUNT(child),
                directory, data.cFileName))
        {
            result = 0;
            break;
        }
        if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
        {
            if (!dluma_custom_menu_overlay_count_files(
                    root, child, depth + 1U, count))
            {
                result = 0;
                break;
            }
        }
        else if (*count == DLUMA_CUSTOM_MENU_MAX_STAGE_FILES)
        {
            result = 0;
            break;
        }
        else
            ++*count;
    } while (FindNextFileW(find, &data));
    if (result && GetLastError() != ERROR_NO_MORE_FILES)
        result = 0;
    FindClose(find);
    CloseHandle(directory_handle);
    return result;
failed:
    if (find != INVALID_HANDLE_VALUE)
        FindClose(find);
    if (directory_handle != INVALID_HANDLE_VALUE)
        CloseHandle(directory_handle);
    return 0;
}

static int dluma_custom_menu_validate_overlay(
    const WCHAR *overlay_path,
    const DlumaCustomMenuStageInventory *inventory)
{
    WCHAR canonical_root[DLUMA_MAX_WPATH];
    HANDLE directory = INVALID_HANDLE_VALUE;
    BY_HANDLE_FILE_INFORMATION directory_identity;
    unsigned int expected_count = 0;
    unsigned int actual_count = 0;
    unsigned int index;
    if (!overlay_path || !inventory || !inventory->files)
    {
        dluma_log_pair(
            "WARN", "custom_main_menu_overlay_validation",
            "invalid_arguments");
        return 0;
    }
    if (!dluma_custom_menu_open_directory(
            overlay_path, &directory, canonical_root,
            DLUMA_ARRAY_COUNT(canonical_root), &directory_identity))
    {
        dluma_log_pair(
            "WARN", "custom_main_menu_overlay_validation",
            "root_open_or_identity_failed");
        dluma_log_hex(
            "WARN", "custom_main_menu_overlay_validation_win32_error",
            GetLastError());
        return 0;
    }
    for (index = 0; index < inventory->count; ++index)
    {
        const DlumaCustomMenuStageFile *record = &inventory->files[index];
        WCHAR path[DLUMA_MAX_WPATH];
        WCHAR canonical[DLUMA_MAX_WPATH];
        BY_HANDLE_FILE_INFORMATION identity;
        ULONGLONG size = 0;
        char sha256[DLUMA_SHA256_HEX_CAPACITY];
        HANDLE file;
        if (!record->overlay_output)
            continue;
        ++expected_count;
        /*
         * The content-addressed final directory is longer than its temporary
         * name.  Build every child path from the handle-derived extended
         * canonical root so a valid final overlay cannot fall back merely
         * because its absolute path crosses legacy MAX_PATH.
         */
        if (!dluma_custom_menu_join(
                path, DLUMA_ARRAY_COUNT(path), canonical_root,
                record->destination_relative))
        {
            dluma_log_pair(
                "WARN", "custom_main_menu_overlay_validation",
                "destination_path_overflow");
            dluma_log_pair(
                "WARN", "custom_main_menu_overlay_validation_asset",
                record->destination_utf8[0]
                    ? record->destination_utf8 : "unknown");
            goto failed;
        }
        file = dluma_custom_menu_open_regular_file(
            path, canonical_root,
            record->output_size ? record->output_size : 1U,
            1, &identity, &size, canonical,
            DLUMA_ARRAY_COUNT(canonical));
        if (file == INVALID_HANDLE_VALUE)
        {
            dluma_log_pair(
                "WARN", "custom_main_menu_overlay_validation",
                "file_open_or_identity_failed");
            dluma_log_pair(
                "WARN", "custom_main_menu_overlay_validation_asset",
                record->destination_utf8[0]
                    ? record->destination_utf8 : "unknown");
            dluma_log_hex(
                "WARN", "custom_main_menu_overlay_validation_win32_error",
                GetLastError());
            goto failed;
        }
        if (size != record->output_size)
        {
            dluma_log_pair(
                "WARN", "custom_main_menu_overlay_validation",
                "file_size_mismatch");
            dluma_log_pair(
                "WARN", "custom_main_menu_overlay_validation_asset",
                record->destination_utf8[0]
                    ? record->destination_utf8 : "unknown");
            dluma_log_u64(
                "WARN", "custom_main_menu_overlay_expected_size",
                record->output_size);
            dluma_log_u64(
                "WARN", "custom_main_menu_overlay_actual_size", size);
            CloseHandle(file);
            goto failed;
        }
        if (!dluma_sha256_handle_hex(
                file, record->output_size ? record->output_size : 1U,
                sha256))
        {
            dluma_log_pair(
                "WARN", "custom_main_menu_overlay_validation",
                "file_hash_failed");
            dluma_log_pair(
                "WARN", "custom_main_menu_overlay_validation_asset",
                record->destination_utf8[0]
                    ? record->destination_utf8 : "unknown");
            CloseHandle(file);
            goto failed;
        }
        if (dluma_stricmp(sha256, record->output_sha256) != 0)
        {
            dluma_log_pair(
                "WARN", "custom_main_menu_overlay_validation",
                "file_hash_mismatch");
            dluma_log_pair(
                "WARN", "custom_main_menu_overlay_validation_asset",
                record->destination_utf8[0]
                    ? record->destination_utf8 : "unknown");
            dluma_log_pair(
                "WARN", "custom_main_menu_overlay_expected_sha256",
                record->output_sha256);
            dluma_log_pair(
                "WARN", "custom_main_menu_overlay_actual_sha256",
                sha256);
            CloseHandle(file);
            goto failed;
        }
        CloseHandle(file);
    }
    if (!expected_count || expected_count != inventory->overlay_file_count)
    {
        dluma_log_pair(
            "WARN", "custom_main_menu_overlay_validation",
            "inventory_count_mismatch");
        dluma_log_u64(
            "WARN", "custom_main_menu_overlay_expected_count",
            expected_count);
        dluma_log_u64(
            "WARN", "custom_main_menu_overlay_inventory_count",
            inventory->overlay_file_count);
        goto failed;
    }
    if (!dluma_custom_menu_overlay_count_files(
            canonical_root, canonical_root, 0U, &actual_count))
    {
        dluma_log_pair(
            "WARN", "custom_main_menu_overlay_validation",
            "tree_count_failed");
        dluma_log_hex(
            "WARN", "custom_main_menu_overlay_validation_win32_error",
            GetLastError());
        goto failed;
    }
    if (actual_count != expected_count)
    {
        dluma_log_pair(
            "WARN", "custom_main_menu_overlay_validation",
            "tree_count_mismatch");
        dluma_log_u64(
            "WARN", "custom_main_menu_overlay_expected_count",
            expected_count);
        dluma_log_u64(
            "WARN", "custom_main_menu_overlay_actual_count",
            actual_count);
        goto failed;
    }
    CloseHandle(directory);
    return 1;
failed:
    CloseHandle(directory);
    return 0;
}

static void dluma_custom_menu_cleanup_temporary_overlay(
    const WCHAR *temporary_overlay,
    const DlumaCustomMenuStageInventory *inventory)
{
    unsigned int index;
    if (!temporary_overlay || !temporary_overlay[0] ||
        !inventory || !inventory->files)
        return;
    for (index = 0; index < inventory->count; ++index)
    {
        const DlumaCustomMenuStageFile *record = &inventory->files[index];
        WCHAR path[DLUMA_MAX_WPATH];
        WCHAR parent[DLUMA_MAX_WPATH];
        if (!record->overlay_output ||
            !dluma_custom_menu_join(
                path, DLUMA_ARRAY_COUNT(path), temporary_overlay,
                record->destination_relative))
            continue;
        (void)DeleteFileW(path);
        dluma_wstr_copy(parent, DLUMA_ARRAY_COUNT(parent), path);
        dluma_path_dirname(parent);
        while (parent[0] &&
               dluma_wstricmp(parent, temporary_overlay) != 0 &&
               dluma_custom_menu_wide_under(parent, temporary_overlay))
        {
            (void)RemoveDirectoryW(parent);
            dluma_path_dirname(parent);
        }
    }
    (void)RemoveDirectoryW(temporary_overlay);
}

/*
 * Generated overlays are caches, not user content.  A killed process, an old
 * overlay-format implementation, or external modification can leave a
 * content-addressed directory whose bytes no longer match the inventory.  Do
 * not let that cache entry permanently disable an otherwise valid project.
 * Rename the exact generated directory out of the active namespace and build
 * a fresh one.  The rename is deliberately recoverable; this routine never
 * recursively deletes a directory or touches the selected Workshop project.
 */
static int dluma_custom_menu_quarantine_invalid_overlay(
    const WCHAR *overlay_parent,
    const WCHAR *invalid_overlay,
    const WCHAR *fingerprint)
{
    WCHAR fingerprint_prefix[DLUMA_CUSTOM_MENU_TEMP_FINGERPRINT_CHARS + 1U];
    WCHAR stale_name[64];
    WCHAR stale_path[DLUMA_MAX_WPATH];
    unsigned int prefix_length = 0;
    unsigned int attempt;
    DWORD error = ERROR_INVALID_DATA;
    if (!overlay_parent || !invalid_overlay || !fingerprint ||
        !dluma_custom_menu_directory_is_under_root(
            invalid_overlay, overlay_parent))
        return dluma_custom_menu_stage_copy_failure(
            0, "invalid_overlay_cache_identity", ERROR_INVALID_DATA);
    while (prefix_length < DLUMA_CUSTOM_MENU_TEMP_FINGERPRINT_CHARS &&
           fingerprint[prefix_length])
    {
        fingerprint_prefix[prefix_length] = fingerprint[prefix_length];
        ++prefix_length;
    }
    fingerprint_prefix[prefix_length] = 0;
    if (prefix_length != DLUMA_CUSTOM_MENU_TEMP_FINGERPRINT_CHARS)
        return dluma_custom_menu_stage_copy_failure(
            0, "invalid_overlay_cache_fingerprint", ERROR_INVALID_DATA);
    for (attempt = 0; attempt < 16U; ++attempt)
    {
        stale_name[0] = 0;
        dluma_wstr_append(stale_name, DLUMA_ARRAY_COUNT(stale_name), L".stale.");
        dluma_wstr_append(
            stale_name, DLUMA_ARRAY_COUNT(stale_name), fingerprint_prefix);
        dluma_wstr_append(stale_name, DLUMA_ARRAY_COUNT(stale_name), L".");
        dluma_custom_menu_wide_hex_append(
            stale_name, DLUMA_ARRAY_COUNT(stale_name),
            GetCurrentProcessId(), 8U);
        dluma_wstr_append(stale_name, DLUMA_ARRAY_COUNT(stale_name), L".");
        dluma_custom_menu_wide_hex_append(
            stale_name, DLUMA_ARRAY_COUNT(stale_name),
            GetTickCount64(), 16U);
        dluma_wstr_append(stale_name, DLUMA_ARRAY_COUNT(stale_name), L".");
        dluma_custom_menu_wide_hex_append(
            stale_name, DLUMA_ARRAY_COUNT(stale_name), attempt, 2U);
        if (!dluma_custom_menu_join(
                stale_path, DLUMA_ARRAY_COUNT(stale_path),
                overlay_parent, stale_name))
            return dluma_custom_menu_stage_copy_failure(
                0, "invalid_overlay_cache_quarantine_path",
                ERROR_BUFFER_OVERFLOW);
        if (MoveFileExW(
                invalid_overlay, stale_path, MOVEFILE_WRITE_THROUGH))
        {
            char stale_utf8[DLUMA_MAX_WPATH * 2U];
            dluma_log_pair(
                "WARN", "custom_main_menu_overlay_cache",
                "invalid_generated_overlay_quarantined");
            if (dluma_wide_to_utf8(
                    stale_path, stale_utf8, DLUMA_ARRAY_COUNT(stale_utf8)))
                dluma_log_pair(
                    "INFO", "custom_main_menu_overlay_quarantine",
                    stale_utf8);
            return 1;
        }
        error = GetLastError();
        if (error != ERROR_ALREADY_EXISTS && error != ERROR_FILE_EXISTS)
            break;
    }
    return dluma_custom_menu_stage_copy_failure(
        0, "invalid_overlay_cache_quarantine_failed", error);
}

static int dluma_custom_menu_materialize_overlay(
    DlumaCustomMenuCandidate *candidate,
    DlumaCustomMenuStageInventory *inventory)
{
    WCHAR runtime_root[DLUMA_MAX_WPATH];
    WCHAR overlay_parent[DLUMA_MAX_WPATH];
    WCHAR fingerprint_wide[DLUMA_SHA256_HEX_CAPACITY];
    WCHAR fingerprint_prefix[DLUMA_CUSTOM_MENU_TEMP_FINGERPRINT_CHARS + 1U];
    WCHAR final_overlay[DLUMA_MAX_WPATH];
    WCHAR temporary_name[64];
    WCHAR temporary_overlay[DLUMA_MAX_WPATH];
    unsigned int prefix_length = 0;
    unsigned int index;
    if (!candidate || !inventory || !inventory->overlay_file_count ||
        !dluma_custom_menu_join(
            runtime_root, DLUMA_ARRAY_COUNT(runtime_root),
            g_dluma.module_dir, L"dluma_runtime") ||
        !dluma_custom_menu_join(
            overlay_parent, DLUMA_ARRAY_COUNT(overlay_parent),
            runtime_root, L"custom_main_menu\\overlays") ||
        !dluma_mkdirs(overlay_parent) ||
        !dluma_custom_menu_directory_is_under_root(
            overlay_parent, g_dluma.module_dir) ||
        !dluma_utf8_to_wide(
            candidate->snapshot.fingerprint, fingerprint_wide,
            DLUMA_ARRAY_COUNT(fingerprint_wide)) ||
        !dluma_custom_menu_join(
            final_overlay, DLUMA_ARRAY_COUNT(final_overlay),
            overlay_parent, fingerprint_wide))
        return 0;
    if (dluma_path_exists(final_overlay))
    {
        if (dluma_custom_menu_validate_overlay(final_overlay, inventory))
        {
            dluma_log_pair(
                "INFO", "custom_main_menu_overlay_cache",
                "validated_reuse");
            dluma_wstr_copy(
                g_dluma_custom_menu_overlay_path,
                DLUMA_ARRAY_COUNT(g_dluma_custom_menu_overlay_path),
                final_overlay);
            return 1;
        }
        if (!dluma_custom_menu_quarantine_invalid_overlay(
                overlay_parent, final_overlay, fingerprint_wide))
            return 0;
    }
    while (prefix_length < DLUMA_CUSTOM_MENU_TEMP_FINGERPRINT_CHARS &&
           fingerprint_wide[prefix_length])
    {
        fingerprint_prefix[prefix_length] =
            fingerprint_wide[prefix_length];
        ++prefix_length;
    }
    fingerprint_prefix[prefix_length] = 0;
    if (prefix_length != DLUMA_CUSTOM_MENU_TEMP_FINGERPRINT_CHARS)
        return 0;
    temporary_name[0] = 0;
    dluma_wstr_append(
        temporary_name, DLUMA_ARRAY_COUNT(temporary_name), L".tmp.");
    dluma_wstr_append(
        temporary_name, DLUMA_ARRAY_COUNT(temporary_name),
        fingerprint_prefix);
    dluma_wstr_append(
        temporary_name, DLUMA_ARRAY_COUNT(temporary_name), L".");
    dluma_custom_menu_wide_hex_append(
        temporary_name, DLUMA_ARRAY_COUNT(temporary_name),
        GetCurrentProcessId(), 8U);
    dluma_wstr_append(
        temporary_name, DLUMA_ARRAY_COUNT(temporary_name), L".");
    dluma_custom_menu_wide_hex_append(
        temporary_name, DLUMA_ARRAY_COUNT(temporary_name),
        GetTickCount64(), 16U);
    if (!dluma_custom_menu_join(
            temporary_overlay, DLUMA_ARRAY_COUNT(temporary_overlay),
            overlay_parent, temporary_name) ||
        !CreateDirectoryW(temporary_overlay, 0))
        return dluma_custom_menu_stage_copy_failure(
            0, "overlay_directory_create_failed", GetLastError());
    for (index = 0; index < inventory->count; ++index)
    {
        DlumaCustomMenuStageFile *record = &inventory->files[index];
        WCHAR output_path[DLUMA_MAX_WPATH];
        WCHAR output_parent[DLUMA_MAX_WPATH];
        HANDLE output;
        if (!record->overlay_output)
            continue;
        if (!dluma_custom_menu_join(
                output_path, DLUMA_ARRAY_COUNT(output_path),
                temporary_overlay, record->destination_relative))
            goto failed;
        dluma_wstr_copy(
            output_parent, DLUMA_ARRAY_COUNT(output_parent), output_path);
        dluma_path_dirname(output_parent);
        if (!dluma_mkdirs(output_parent))
            goto failed;
        output = CreateFileW(
            output_path, GENERIC_WRITE, 0, 0, CREATE_NEW,
            FILE_ATTRIBUTE_NORMAL, 0);
        if (output == INVALID_HANDLE_VALUE)
            goto failed;
        if (!dluma_custom_menu_overlay_payload(output, record) ||
            !FlushFileBuffers(output))
        {
            CloseHandle(output);
            goto failed;
        }
        CloseHandle(output);
    }
    if (!dluma_custom_menu_validate_overlay(temporary_overlay, inventory))
        goto failed;
    if (!MoveFileExW(
            temporary_overlay, final_overlay, MOVEFILE_WRITE_THROUGH))
    {
        DWORD move_error = GetLastError();
        if (move_error != ERROR_ALREADY_EXISTS ||
            !dluma_path_exists(final_overlay) ||
            !dluma_custom_menu_validate_overlay(final_overlay, inventory))
            goto failed;
        dluma_custom_menu_cleanup_temporary_overlay(
            temporary_overlay, inventory);
    }
    if (!dluma_custom_menu_validate_overlay(final_overlay, inventory))
        return 0;
    dluma_wstr_copy(
        g_dluma_custom_menu_overlay_path,
        DLUMA_ARRAY_COUNT(g_dluma_custom_menu_overlay_path), final_overlay);
    return 1;
failed:
    dluma_custom_menu_cleanup_temporary_overlay(
        temporary_overlay, inventory);
    return dluma_custom_menu_stage_copy_failure(
        index < inventory->count ? &inventory->files[index] : 0,
        "atomic_overlay_creation_or_validation_failed", GetLastError());
}

static void dluma_custom_menu_receipt_path(
    WCHAR *output, unsigned int capacity)
{
    WCHAR runtime[DLUMA_MAX_WPATH];
    if (!dluma_custom_menu_join(runtime, DLUMA_ARRAY_COUNT(runtime),
                              g_dluma.module_dir, L"dluma_runtime"))
    {
        output[0] = 0;
        return;
    }
    (void)dluma_custom_menu_join(output, capacity, runtime,
                               L"custom_main_menu_activation.ini");
}

static int dluma_custom_menu_receipt_blocks_candidate(
    DlumaCustomMenuCandidate *candidate)
{
    WCHAR path[DLUMA_MAX_WPATH];
    WCHAR state_wide[32];
    WCHAR id_wide[DLUMA_CUSTOM_MENU_ID_CAPACITY];
    WCHAR fingerprint_wide[DLUMA_SHA256_HEX_CAPACITY];
    char state[32];
    char id[DLUMA_CUSTOM_MENU_ID_CAPACITY];
    char fingerprint[DLUMA_SHA256_HEX_CAPACITY];
    dluma_custom_menu_receipt_path(path, DLUMA_ARRAY_COUNT(path));
    if (!path[0] || !dluma_path_exists(path))
        return 0;
    GetPrivateProfileStringW(L"activation", L"state", L"", state_wide,
                             DLUMA_ARRAY_COUNT(state_wide), path);
    GetPrivateProfileStringW(L"activation", L"active_id", L"", id_wide,
                             DLUMA_ARRAY_COUNT(id_wide), path);
    GetPrivateProfileStringW(L"activation", L"fingerprint", L"",
                             fingerprint_wide,
                             DLUMA_ARRAY_COUNT(fingerprint_wide), path);
    if (!dluma_wide_to_utf8(state_wide, state, DLUMA_ARRAY_COUNT(state)) ||
        !dluma_wide_to_utf8(id_wide, id, DLUMA_ARRAY_COUNT(id)) ||
        !dluma_wide_to_utf8(fingerprint_wide, fingerprint,
                          DLUMA_ARRAY_COUNT(fingerprint)))
        return 0;
    return dluma_custom_main_menu_boot_receipt_blocks(
        candidate->snapshot.id, candidate->snapshot.fingerprint,
        id, fingerprint, state);
}

static void dluma_custom_menu_write_receipt(const char *state)
{
    WCHAR path[DLUMA_MAX_WPATH];
    WCHAR directory[DLUMA_MAX_WPATH];
    WCHAR state_wide[32];
    WCHAR id_wide[DLUMA_CUSTOM_MENU_ID_CAPACITY];
    WCHAR fingerprint_wide[DLUMA_SHA256_HEX_CAPACITY];
    if (!g_dluma_custom_menu_selected)
        return;
    dluma_custom_menu_receipt_path(path, DLUMA_ARRAY_COUNT(path));
    dluma_wstr_copy(directory, DLUMA_ARRAY_COUNT(directory), path);
    dluma_path_dirname(directory);
    if (!path[0] || !dluma_mkdirs(directory) ||
        !dluma_utf8_to_wide(state, state_wide, DLUMA_ARRAY_COUNT(state_wide)) ||
        !dluma_utf8_to_wide(g_dluma_custom_menu_selected->snapshot.id,
                          id_wide, DLUMA_ARRAY_COUNT(id_wide)) ||
        !dluma_utf8_to_wide(g_dluma_custom_menu_selected->snapshot.fingerprint,
                          fingerprint_wide,
                          DLUMA_ARRAY_COUNT(fingerprint_wide)))
        return;
    WritePrivateProfileStringW(L"activation", L"active_id", id_wide, path);
    WritePrivateProfileStringW(L"activation", L"fingerprint",
                               fingerprint_wide, path);
    WritePrivateProfileStringW(L"activation", L"state", state_wide, path);
    WritePrivateProfileStringW(0, 0, 0, path);
}

static DlumaCustomMenuCandidate *dluma_custom_menu_find_id_source(
    const char *id, const char *source_identity)
{
    unsigned int index;
    for (index = 0; index < g_dluma_custom_menu_candidate_count; ++index)
        if (g_dluma_custom_menu_candidates[index].snapshot.id[0] &&
            dluma_custom_main_menu_id_equal(
                g_dluma_custom_menu_candidates[index].snapshot.id, id) &&
            (!source_identity || !source_identity[0] ||
             dluma_stricmp(
                 g_dluma_custom_menu_candidates[index]
                     .snapshot.source_identity,
                 source_identity) == 0))
            return &g_dluma_custom_menu_candidates[index];
    return 0;
}

void dluma_custom_main_menu_discover_and_prepare(void)
{
    unsigned int index;
    WCHAR requested_wide[DLUMA_CUSTOM_MENU_ID_CAPACITY];
    WCHAR requested_source_wide[1024];
    DWORD requested_source_length;
    int explicit_source = 0;
    DlumaCustomMenuStageInventory inventory;
    for (index = 0; index < g_dluma_custom_menu_candidate_count; ++index)
        if (g_dluma_custom_menu_candidates[index].manifest_bytes)
            dluma_free(g_dluma_custom_menu_candidates[index].manifest_bytes);
    memset(g_dluma_custom_menu_candidates, 0,
           sizeof(g_dluma_custom_menu_candidates));
    g_dluma_custom_menu_candidate_count = 0;
    g_dluma_custom_menu_candidate_total = 0;
    g_dluma_custom_menu_candidate_overflow = 0;
    g_dluma_custom_menu_selected = 0;
    g_dluma_custom_menu_sources_prepared = 0;
    g_dluma_custom_menu_sources_mounted = 0;
    memset(g_dluma_custom_menu_banks, 0, sizeof(g_dluma_custom_menu_banks));
    g_dluma_custom_menu_bank_count = 0;
    g_dluma_custom_menu_overlay_path[0] = 0;
    g_dluma_custom_menu_requested_id[0] = 0;
    g_dluma_custom_menu_requested_source[0] = 0;
    GetPrivateProfileStringW(
        L"custom_main_menu", L"active_id", L"base_game",
        requested_wide, DLUMA_ARRAY_COUNT(requested_wide), g_dluma.config_path);
    if (!dluma_wide_to_utf8(requested_wide, g_dluma_custom_menu_requested_id,
                          DLUMA_ARRAY_COUNT(g_dluma_custom_menu_requested_id)))
        dluma_str_copy(g_dluma_custom_menu_requested_id,
                     DLUMA_ARRAY_COUNT(g_dluma_custom_menu_requested_id),
                     "base_game");
    requested_source_wide[0] = 0;
    requested_source_length = GetPrivateProfileStringW(
        L"custom_main_menu", L"active_source", L"",
        requested_source_wide, DLUMA_ARRAY_COUNT(requested_source_wide),
        g_dluma.config_path);
    explicit_source = requested_source_length && requested_source_wide[0];
    if (explicit_source &&
        !dluma_wide_to_utf8(requested_source_wide,
                          g_dluma_custom_menu_requested_source,
                          DLUMA_ARRAY_COUNT(
                              g_dluma_custom_menu_requested_source))) {
        explicit_source = 0;
        g_dluma_custom_menu_requested_source[0] = 0;
    }
    for (index = 0; index < g_dluma.workshop_root_count; ++index)
        dluma_custom_menu_enumerate_root(g_dluma.workshop_roots[index]);
    dluma_custom_menu_sort_candidates();
    dluma_custom_menu_invalidate_duplicate_ids();
    dluma_custom_menu_invalidate_capacity_overflow();
    for (index = 0; index < g_dluma_custom_menu_candidate_count; ++index)
    {
        const DlumaCustomMainMenuCandidateSnapshot *snapshot =
            &g_dluma_custom_menu_candidates[index].snapshot;
        if (!snapshot->valid)
        {
            dluma_log_pair(
                "WARN", "custom_main_menu_candidate_invalid",
                snapshot->id[0] ? snapshot->id :
                    (snapshot->name[0] ? snapshot->name : "unknown"));
            dluma_log_pair(
                "WARN", "custom_main_menu_candidate_invalid_reason",
                snapshot->diagnostic[0] ? snapshot->diagnostic :
                    "validation_failed_without_diagnostic");
        }
    }
    dluma_log_u64("INFO", "custom_main_menu_candidate_count",
                g_dluma_custom_menu_candidate_count);
    dluma_log_u64("INFO", "custom_main_menu_candidate_total",
                g_dluma_custom_menu_candidate_total);
    if (dluma_custom_main_menu_id_equal(
            g_dluma_custom_menu_requested_id, "base_game"))
    {
        dluma_log_pair("INFO", "custom_main_menu_selection", "base_game");
        return;
    }
    if (explicit_source) {
        g_dluma_custom_menu_selected = dluma_custom_menu_find_id_source(
            g_dluma_custom_menu_requested_id,
            g_dluma_custom_menu_requested_source);
    } else {
        DlumaCustomMenuCandidate* local = 0;
        DlumaCustomMenuCandidate* workshop = 0;
        for (index = 0; index < g_dluma_custom_menu_candidate_count; ++index) {
            DlumaCustomMenuCandidate* candidate =
                &g_dluma_custom_menu_candidates[index];
            if (!candidate->snapshot.valid ||
                !dluma_custom_main_menu_id_equal(
                    candidate->snapshot.id,
                    g_dluma_custom_menu_requested_id))
                continue;
            if (!candidate->snapshot.source_is_subscribed) {
                if (!local || dluma_wstricmp(candidate->canonical_root,
                                           local->canonical_root) < 0)
                    local = candidate;
            } else if (!workshop ||
                       dluma_wstricmp(candidate->canonical_root,
                                   workshop->canonical_root) < 0) {
                workshop = candidate;
            }
        }
        g_dluma_custom_menu_selected = local ? local : workshop;
        if (g_dluma_custom_menu_selected) {
            WCHAR source_wide[1024];
            dluma_str_copy(
                g_dluma_custom_menu_requested_source,
                DLUMA_ARRAY_COUNT(g_dluma_custom_menu_requested_source),
                g_dluma_custom_menu_selected->snapshot.source_identity);
            if (dluma_utf8_to_wide(
                    g_dluma_custom_menu_requested_source, source_wide,
                    DLUMA_ARRAY_COUNT(source_wide))) {
                WritePrivateProfileStringW(
                    L"custom_main_menu", L"active_source", source_wide,
                    g_dluma.config_path);
                WritePrivateProfileStringW(0, 0, 0, g_dluma.config_path);
            }
        }
    }
    for (index = 0; index < g_dluma_custom_menu_candidate_count; ++index) {
        DlumaCustomMainMenuCandidateSnapshot* snapshot =
            &g_dluma_custom_menu_candidates[index].snapshot;
        if (!dluma_custom_main_menu_id_equal(
                snapshot->id, g_dluma_custom_menu_requested_id))
            continue;
        snapshot->source_selected = (unsigned char)
            (g_dluma_custom_menu_selected ==
                &g_dluma_custom_menu_candidates[index] ? 1 : 0);
        snapshot->source_selection_missing = (unsigned char)
            (explicit_source && !g_dluma_custom_menu_selected ? 1 : 0);
    }
    if (!g_dluma_custom_menu_selected ||
        !g_dluma_custom_menu_selected->snapshot.valid)
    {
        dluma_log_pair("WARN", "custom_main_menu_fallback",
                     explicit_source
                         ? "selected_source_missing_or_invalid"
                         : "selected_id_missing_or_invalid");
        g_dluma_custom_menu_selected = 0;
        return;
    }
    memset(&inventory, 0, sizeof(inventory));
    if (!dluma_custom_menu_build_inventory(
            g_dluma_custom_menu_selected, &inventory))
    {
        dluma_log_pair("WARN", "custom_main_menu_fallback",
                     "secure_inventory_or_fingerprint_failed");
        g_dluma_custom_menu_selected->snapshot.valid = 0;
        if (!g_dluma_custom_menu_selected->snapshot.diagnostic[0])
            dluma_custom_menu_text(
                g_dluma_custom_menu_selected->snapshot.diagnostic,
                DLUMA_ARRAY_COUNT(
                    g_dluma_custom_menu_selected->snapshot.diagnostic),
                "secure staging inventory could not be prepared");
        dluma_log_pair(
            "WARN", "custom_main_menu_stage_inventory_reason",
            g_dluma_custom_menu_selected->snapshot.diagnostic);
        g_dluma_custom_menu_selected = 0;
        return;
    }
    dluma_log_u64("INFO", "custom_main_menu_stage_file_count",
                inventory.count);
    dluma_log_u64("INFO", "custom_main_menu_stage_source_bytes",
                inventory.total_source_bytes);
    dluma_log_u64("INFO", "custom_main_menu_overlay_file_count",
                inventory.overlay_file_count);
    dluma_log_u64("INFO", "custom_main_menu_stage_data_file_count",
                inventory.data_file_count);
    dluma_log_u64("INFO", "custom_main_menu_stage_assets_pc_file_count",
                inventory.assets_pc_file_count);
    dluma_log_u64("INFO", "custom_main_menu_stage_fx_definition_count",
                inventory.fx_definition_count);
    dluma_log_u64("INFO", "custom_main_menu_stage_material_pack_count",
                inventory.material_pack_count);
    dluma_log_u64("INFO", "custom_main_menu_stage_compiled_mesh_count",
                inventory.compiled_mesh_count);
    if (dluma_custom_menu_receipt_blocks_candidate(g_dluma_custom_menu_selected))
    {
        g_dluma_custom_menu_selected->snapshot.quarantined = 1U;
        dluma_log_pair("WARN", "custom_main_menu_fallback",
                     "unfinished_activation_receipt_quarantined");
        dluma_custom_menu_inventory_release(&inventory);
        g_dluma_custom_menu_selected = 0;
        return;
    }
    if (!dluma_custom_menu_materialize_overlay(
            g_dluma_custom_menu_selected, &inventory))
    {
        dluma_log_pair("WARN", "custom_main_menu_fallback",
                     "atomic_overlay_creation_or_validation_failed");
        dluma_custom_menu_inventory_release(&inventory);
        g_dluma_custom_menu_selected = 0;
        return;
    }
    if (g_dluma_custom_menu_selected->manifest.use_custom_banks &&
        !dluma_custom_menu_materialize_declared_banks(
            g_dluma_custom_menu_selected, &inventory,
            g_dluma_custom_menu_banks, &g_dluma_custom_menu_bank_count))
        dluma_log_pair(
            "WARN", "custom_main_menu_bank_materialization",
            "unavailable_visual_menu_retained");
    g_dluma_custom_menu_asset_revision = inventory.asset_revision;
    dluma_custom_menu_inventory_release(&inventory);
    g_dluma_custom_menu_sources_prepared = 1U;
    dluma_log_pair("INFO", "custom_main_menu_selection",
                 g_dluma_custom_menu_selected->snapshot.id);
    dluma_log_pair("INFO", "custom_main_menu_source_fingerprint",
                 g_dluma_custom_menu_selected->snapshot.fingerprint);
    {
        char overlay_utf8[DLUMA_MAX_WPATH * 2U];
        if (dluma_wide_to_utf8(
                g_dluma_custom_menu_overlay_path, overlay_utf8,
                DLUMA_ARRAY_COUNT(overlay_utf8)))
            dluma_log_pair(
                "INFO", "custom_main_menu_overlay", overlay_utf8);
    }
}

unsigned int dluma_custom_main_menu_snapshot(
    DlumaCustomMainMenuCandidateSnapshot *output,
    unsigned int capacity,
    unsigned int *total_out)
{
    unsigned int count = g_dluma_custom_menu_candidate_count;
    unsigned int index;
    if (total_out)
        *total_out = g_dluma_custom_menu_candidate_total;
    if (!output || !capacity)
        return count;
    if (count > capacity)
        count = capacity;
    for (index = 0; index < count; ++index)
        output[index] = g_dluma_custom_menu_candidates[index].snapshot;
    return count;
}

int dluma_custom_main_menu_select_next_launch(
    const char *id,
    const char *source_identity,
    char *diagnostic,
    unsigned int diagnostic_capacity)
{
    WCHAR id_wide[DLUMA_CUSTOM_MENU_ID_CAPACITY];
    DlumaCustomMenuCandidate *candidate = 0;
    if (diagnostic && diagnostic_capacity)
        diagnostic[0] = 0;
    if (!id || !id[0])
    {
        dluma_custom_menu_text(diagnostic, diagnostic_capacity,
                             "selection id is empty");
        return 0;
    }
    if (!dluma_custom_main_menu_id_equal(id, "base_game"))
    {
        if (!source_identity || !source_identity[0])
        {
            dluma_custom_menu_text(diagnostic, diagnostic_capacity,
                                 "selection source is empty");
            return 0;
        }
        candidate = dluma_custom_menu_find_id_source(id, source_identity);
        if (!candidate || !candidate->snapshot.valid)
        {
            dluma_custom_menu_text(diagnostic, diagnostic_capacity,
                                 "selected custom menu is invalid");
            return 0;
        }
    }
    if (!dluma_utf8_to_wide(id, id_wide, DLUMA_ARRAY_COUNT(id_wide)) ||
        !WritePrivateProfileStringW(
            L"custom_main_menu", L"active_id", id_wide,
            g_dluma.config_path))
    {
        dluma_custom_menu_text(diagnostic, diagnostic_capacity,
                             "selection could not be persisted");
        return 0;
    }
    if (candidate)
    {
        WCHAR source_wide[1024];
        if (!dluma_utf8_to_wide(source_identity, source_wide,
                              DLUMA_ARRAY_COUNT(source_wide)) ||
            !WritePrivateProfileStringW(
                L"custom_main_menu", L"active_source", source_wide,
                g_dluma.config_path))
        {
            dluma_custom_menu_text(diagnostic, diagnostic_capacity,
                                 "selection source could not be persisted");
            return 0;
        }
    }
    else
    {
        WritePrivateProfileStringW(
            L"custom_main_menu", L"active_source", 0,
            g_dluma.config_path);
    }
    WritePrivateProfileStringW(0, 0, 0, g_dluma.config_path);
    if (candidate && candidate->snapshot.quarantined)
    {
        DlumaCustomMenuCandidate *prior = g_dluma_custom_menu_selected;
        g_dluma_custom_menu_selected = candidate;
        dluma_custom_menu_write_receipt("cleared");
        g_dluma_custom_menu_selected = prior;
        candidate->snapshot.quarantined = 0;
    }
    dluma_str_copy(
        g_dluma_custom_menu_requested_id,
        DLUMA_ARRAY_COUNT(g_dluma_custom_menu_requested_id), id);
    dluma_str_copy(
        g_dluma_custom_menu_requested_source,
        DLUMA_ARRAY_COUNT(g_dluma_custom_menu_requested_source),
        candidate ? source_identity : "");
    return 1;
}

int dluma_custom_main_menu_project_marked(const WCHAR *project_root)
{
    unsigned int index;
    WCHAR canonical[DLUMA_MAX_WPATH];
    HANDLE directory = INVALID_HANDLE_VALUE;
    BY_HANDLE_FILE_INFORMATION identity;
    if (!project_root || !project_root[0])
        return 0;
    if (dluma_custom_menu_open_directory(
            project_root, &directory, canonical,
            DLUMA_ARRAY_COUNT(canonical), &identity))
    {
        CloseHandle(directory);
        for (index = 0; index < g_dluma_custom_menu_candidate_count; ++index)
            if (dluma_wstricmp(
                    canonical,
                    g_dluma_custom_menu_candidates[index].canonical_root) == 0)
                return 1;
    }
    return dluma_custom_menu_marker_exists(project_root);
}

int dluma_custom_main_menu_project_identity_marked(
    const char *project_root_utf8)
{
    WCHAR project_root[DLUMA_MAX_WPATH];
    return project_root_utf8 && project_root_utf8[0] &&
           dluma_utf8_to_wide(project_root_utf8, project_root,
                            DLUMA_ARRAY_COUNT(project_root)) &&
           dluma_custom_main_menu_project_marked(project_root);
}

int dluma_custom_main_menu_selected_ready(void)
{
    return g_dluma_custom_menu_selected &&
           g_dluma_custom_menu_selected->snapshot.valid &&
           g_dluma_custom_menu_sources_prepared;
}

int dluma_custom_main_menu_selected_main_xui_alias(
    char *output, unsigned int capacity)
{
    if (output && capacity)
        output[0] = 0;
    return dluma_custom_main_menu_selected_ready() &&
           dluma_custom_menu_main_xui_alias(
               g_dluma_custom_menu_selected, output, capacity);
}

const DlumaCustomMainMenuManifest *dluma_custom_main_menu_selected_manifest(void)
{
    return dluma_custom_main_menu_selected_ready()
        ? &g_dluma_custom_menu_selected->manifest : 0;
}

const char *dluma_custom_main_menu_selected_id(void)
{
    return dluma_custom_main_menu_selected_ready()
        ? g_dluma_custom_menu_selected->snapshot.id : "base_game";
}

const char *dluma_custom_main_menu_requested_id(void)
{
    return g_dluma_custom_menu_requested_id[0]
        ? g_dluma_custom_menu_requested_id : "base_game";
}

const char *dluma_custom_main_menu_selected_map(void)
{
    return dluma_custom_main_menu_selected_ready()
        ? g_dluma_custom_menu_selected->manifest.map : "";
}

const char *dluma_custom_main_menu_selected_fingerprint(void)
{
    return dluma_custom_main_menu_selected_ready()
        ? g_dluma_custom_menu_selected->snapshot.fingerprint : "";
}

const WCHAR *dluma_custom_main_menu_selected_project_root(void)
{
    return dluma_custom_main_menu_selected_ready()
        ? g_dluma_custom_menu_selected->canonical_root : L"";
}

const WCHAR *dluma_custom_main_menu_selected_overlay(void)
{
    return dluma_custom_main_menu_selected_ready()
        ? g_dluma_custom_menu_overlay_path : L"";
}

int dluma_custom_main_menu_selected_bank_alias(
    char *output, unsigned int capacity)
{
    return dluma_custom_main_menu_selected_bank_alias_at(
        0U, output, capacity);
}

unsigned int dluma_custom_main_menu_selected_bank_count(void)
{
    return dluma_custom_main_menu_selected_ready() &&
           g_dluma_custom_menu_selected->manifest.use_custom_banks
        ? g_dluma_custom_menu_bank_count : 0U;
}

int dluma_custom_main_menu_selected_bank_alias_at(
    unsigned int index, char *output, unsigned int capacity)
{
    const DlumaCustomMenuPreparedBank *bank;
    if (output && capacity)
        output[0] = 0;
    if (!output || !capacity ||
        !dluma_custom_main_menu_selected_ready() ||
        !g_dluma_custom_menu_selected->manifest.use_custom_banks ||
        index >= g_dluma_custom_menu_bank_count)
        return 0;
    bank = &g_dluma_custom_menu_banks[index];
    if (!bank->mounted || !bank->alias[0] ||
        strlen(bank->alias) + 1U > capacity)
        return 0;
    dluma_str_copy(output, capacity, bank->alias);
    return output[0] != 0;
}

int dluma_custom_main_menu_selected_bank_source_snapshot(
    char *path_out, unsigned int path_capacity,
    char *fingerprint_out, unsigned int fingerprint_capacity)
{
    return dluma_custom_main_menu_selected_bank_source_snapshot_at(
        0U, path_out, path_capacity,
        fingerprint_out, fingerprint_capacity);
}

int dluma_custom_main_menu_selected_bank_source_snapshot_at(
    unsigned int index,
    char *path_out, unsigned int path_capacity,
    char *fingerprint_out, unsigned int fingerprint_capacity)
{
    const DlumaCustomMenuPreparedBank *bank;
    if (path_out && path_capacity)
        path_out[0] = 0;
    if (fingerprint_out && fingerprint_capacity)
        fingerprint_out[0] = 0;
    if (!path_out || !path_capacity ||
        !fingerprint_out || !fingerprint_capacity ||
        index >= g_dluma_custom_menu_bank_count)
        return 0;
    bank = &g_dluma_custom_menu_banks[index];
    if (!bank->mounted || !bank->materialized.active ||
        !bank->materialized.source_registered ||
        !bank->materialized.bank_dir_utf8[0] ||
        strlen(bank->sha256) != 64U ||
        strlen(bank->materialized.bank_dir_utf8) + 1U > path_capacity ||
        strlen(bank->sha256) + 1U > fingerprint_capacity)
        return 0;
    dluma_str_copy(
        path_out, path_capacity, bank->materialized.bank_dir_utf8);
    dluma_str_copy(
        fingerprint_out, fingerprint_capacity, bank->sha256);
    return path_out[0] && fingerprint_out[0];
}

const DlumaCompanionArchiveAudioManifest *
dluma_custom_main_menu_selected_bank_catalog_at(unsigned int index)
{
    if (!dluma_custom_main_menu_selected_ready() ||
        index >= g_dluma_custom_menu_bank_count ||
        !g_dluma_custom_menu_banks[index].mounted ||
        !g_dluma_custom_menu_banks[index].catalog.valid)
        return 0;
    return &g_dluma_custom_menu_banks[index].catalog;
}

int dluma_custom_main_menu_selected_source_pinned(void)
{
    unsigned int index;
    if (!dluma_custom_main_menu_selected_ready() ||
        !g_dluma_custom_menu_sources_mounted ||
        !dluma_custom_main_menu_sources_ready())
        return 0;
    if (!g_dluma_custom_menu_selected->manifest.use_custom_banks)
        return 1;
    if (g_dluma_custom_menu_bank_count !=
        g_dluma_custom_menu_selected->manifest.bank_count)
        return 0;
    for (index = 0; index < g_dluma_custom_menu_bank_count; ++index)
        if (!g_dluma_custom_menu_banks[index].mounted ||
            !g_dluma_custom_menu_banks[index].materialized.active ||
            !g_dluma_custom_menu_banks[index].materialized.source_registered)
            return 0;
    return 1;
}

static int dluma_custom_menu_bank_registry_has_capacity(
    const DlumaCustomMenuPreparedBank *banks, unsigned int bank_count)
{
    DlumaProcessLifetimeSourceSnapshot *snapshot;
    unsigned int index, needed = 0;
    int result;
    snapshot = (DlumaProcessLifetimeSourceSnapshot *)dluma_alloc(sizeof(*snapshot));
    if (!snapshot) return 0;
    memset(snapshot, 0, sizeof(*snapshot));
    result = dluma_process_lifetime_sources_snapshot(snapshot) &&
        snapshot->complete && snapshot->count <= DLUMA_PROCESS_LIFETIME_SOURCE_CAPACITY;
    for (index = 0; result && index < bank_count; ++index) {
        unsigned int prior;
        for (prior = 0; prior < snapshot->count; ++prior)
            if (snapshot->records[prior].owner_kind == DLUMA_SOURCE_OWNER_CUSTOM_MAIN_MENU &&
                dluma_stricmp(snapshot->records[prior].owner_id,
                             banks[index].owner_id) == 0)
                break;
        if (prior == snapshot->count) ++needed;
    }
    if (result) result = needed <= DLUMA_PROCESS_LIFETIME_SOURCE_CAPACITY - snapshot->count;
    dluma_free(snapshot);
    return result;
}

static void dluma_custom_menu_close_unmounted_banks(
    DlumaCustomMenuPreparedBank *banks)
{
    unsigned int index;
    for (index = 0; index < DLUMA_CUSTOM_MENU_MAX_BANKS; ++index) {
        DlumaCompanionArchiveMaterializedBank *bank = &banks[index].materialized;
        HANDLE handles[4];
        unsigned int handle_index;
        if (!bank->active || bank->source_registered) continue;
        handles[0] = bank->bank_file; handles[1] = bank->bank_dir;
        handles[2] = bank->cache_dir; handles[3] = bank->runtime_dir;
        for (handle_index = 0; handle_index < 4U; ++handle_index)
            if (handles[handle_index] &&
                handles[handle_index] != INVALID_HANDLE_VALUE)
                CloseHandle(handles[handle_index]);
        memset(bank, 0, sizeof(*bank));
    }
}

int dluma_custom_main_menu_refresh_asset_sources(void)
{
    ULONGLONG revision = dluma_asset_mods_desired_revision();
    DlumaCustomMenuCandidate *candidate;
    DlumaCustomMenuPreparedBank *banks;
    DlumaCustomMenuRetainedBanks *retained;
    DlumaCustomMenuStageInventory inventory;
    WCHAR prior_overlay[DLUMA_MAX_WPATH];
    unsigned int bank_count = 0;
    unsigned int index;
    int result = 0;
    int retained_previous = 0;
    if (!dluma_custom_main_menu_selected_ready() ||
        revision == g_dluma_custom_menu_asset_revision)
        return 1;
    if (revision != dluma_asset_mods_effective_revision())
        return 1;
    /* A displayed menu and its published files remain one generation. */
    if (g_dluma_custom_menu_sources_mounted ||
        !dluma_custom_main_menu_can_replace_sources()) {
        if (g_dluma_custom_menu_asset_refresh_attempt != revision) {
            g_dluma_custom_menu_asset_refresh_attempt = revision;
            dluma_log_pair("INFO", "custom_main_menu_asset_update",
                         "menu_sources_in_use_restart_may_be_required");
        }
        return 1;
    }
    candidate = (DlumaCustomMenuCandidate *)dluma_alloc(sizeof(*candidate));
    banks = (DlumaCustomMenuPreparedBank *)dluma_alloc(
        sizeof(*banks) * DLUMA_CUSTOM_MENU_MAX_BANKS);
    retained = (DlumaCustomMenuRetainedBanks *)dluma_alloc(sizeof(*retained));
    if (!candidate || !banks || !retained) {
        if (candidate) dluma_free(candidate);
        if (banks) dluma_free(banks);
        if (retained) dluma_free(retained);
        return 0;
    }
    *candidate = *g_dluma_custom_menu_selected;
    memset(banks, 0, sizeof(*banks) * DLUMA_CUSTOM_MENU_MAX_BANKS);
    memset(retained, 0, sizeof(*retained));
    memset(&inventory, 0, sizeof(inventory));
    dluma_wstr_copy(prior_overlay, DLUMA_ARRAY_COUNT(prior_overlay),
                   g_dluma_custom_menu_overlay_path);
    if (!dluma_custom_main_menu_manifest_parse(candidate->manifest_bytes,
            candidate->manifest_size, &candidate->manifest,
            candidate->snapshot.diagnostic,
            DLUMA_ARRAY_COUNT(candidate->snapshot.diagnostic)) ||
        !dluma_custom_menu_validate_scene(candidate))
        goto done;
    for (index = 0; index < candidate->manifest.override_count; ++index)
        if (!dluma_custom_menu_validate_xui(candidate,
                &candidate->manifest.overrides[index]))
            goto done;
    if (!dluma_custom_menu_validate_banks(candidate) ||
        !dluma_custom_menu_build_inventory(candidate, &inventory) ||
        !dluma_custom_menu_materialize_overlay(candidate, &inventory) ||
        !dluma_custom_menu_materialize_declared_banks(candidate, &inventory,
                                                   banks, &bank_count) ||
        revision != dluma_asset_mods_desired_revision())
        goto done;
    if (!dluma_custom_menu_bank_registry_has_capacity(banks, bank_count)) {
        result = retained_previous = 1;
        dluma_log_pair("INFO", "custom_main_menu_asset_update",
                     "bank_source_capacity_restart_required");
        goto done;
    }
    if (!dluma_prepare_custom_main_menu_sources(candidate->canonical_root,
            g_dluma_custom_menu_overlay_path, candidate->snapshot.fingerprint))
        goto done;
    for (index = 0; index < g_dluma_custom_menu_bank_count; ++index)
        retained->banks[index] = g_dluma_custom_menu_banks[index].materialized;
    retained->next = g_dluma_custom_menu_retained_banks;
    g_dluma_custom_menu_retained_banks = retained;
    retained = 0;
    /* Restore while the redirect still contains the previous alias. */
    dluma_ui_restore_main_menu_redirect();
    *g_dluma_custom_menu_selected = *candidate;
    memcpy(g_dluma_custom_menu_banks, banks,
           sizeof(*banks) * DLUMA_CUSTOM_MENU_MAX_BANKS);
    g_dluma_custom_menu_bank_count = bank_count;
    g_dluma_custom_menu_asset_revision = revision;
    g_dluma_custom_menu_asset_refresh_attempt = 0;
    result = 1;
    if (!dluma_ui_prepare_main_menu_redirect()) {
        dluma_log_pair("WARN", "custom_main_menu_asset_update",
                     "refreshed_xui_redirect_unavailable");
        dluma_custom_menu_inventory_release(&inventory);
        dluma_free(banks);
        dluma_free(candidate);
        return 0;
    }
    dluma_log_u64("INFO", "custom_main_menu_asset_revision", revision);
    dluma_log_pair("INFO", "custom_main_menu_asset_update",
                 "staged_for_returning_frontend");
done:
    if (!result || retained_previous) {
        dluma_wstr_copy(g_dluma_custom_menu_overlay_path,
            DLUMA_ARRAY_COUNT(g_dluma_custom_menu_overlay_path), prior_overlay);
        dluma_custom_menu_close_unmounted_banks(banks);
        if (!retained_previous)
            dluma_log_pair("WARN", "custom_main_menu_asset_update",
                         "staging_failed_previous_menu_retained");
    }
    dluma_custom_menu_inventory_release(&inventory);
    if (retained) dluma_free(retained);
    dluma_free(banks);
    dluma_free(candidate);
    return result;
}

int dluma_custom_main_menu_prepare_selected_sources(void)
{
    if (!dluma_custom_main_menu_selected_ready())
        return 1;
    return dluma_prepare_custom_main_menu_sources(
        g_dluma_custom_menu_selected->canonical_root,
        g_dluma_custom_menu_overlay_path,
        g_dluma_custom_menu_selected->snapshot.fingerprint);
}

static void dluma_custom_main_menu_mount_selected_banks(void)
{
    if (g_dluma_custom_menu_selected->manifest.use_custom_banks)
    {
        unsigned int index;
        int group_ready =
            g_dluma_custom_menu_bank_count ==
                g_dluma_custom_menu_selected->manifest.bank_count;
        for (index = 0; group_ready &&
             index < g_dluma_custom_menu_bank_count; ++index)
        {
            DlumaCustomMenuPreparedBank *bank =
                &g_dluma_custom_menu_banks[index];
            if (!bank->alias[0] || !bank->owner_id[0] ||
                !dluma_companion_archive_mount_materialized_bank(
                    &bank->materialized,
                    DLUMA_SOURCE_OWNER_CUSTOM_MAIN_MENU,
                    bank->owner_id, bank->sha256,
                    g_dluma_custom_menu_selected->snapshot.fingerprint,
                    g_dluma_custom_menu_selected->manifest_sha256,
                    bank->alias, DLUMA_ARRAY_COUNT(bank->alias)))
                group_ready = 0;
            else
                bank->mounted = 1U;
        }
        if (!group_ready)
        {
            dluma_log_pair(
                "WARN", "custom_main_menu_bank_source",
                "group_mount_failed_visual_menu_retained");
        }
        else
        {
            dluma_log_pair(
                "INFO", "custom_main_menu_bank_source",
                "physical_directories_mounted_and_pinned");
        }
    }
}

const char *dluma_custom_main_menu_requested_source(void)
{
    return g_dluma_custom_menu_requested_source;
}

int dluma_custom_main_menu_mount_selected_sources(void)
{
    if (!dluma_custom_main_menu_selected_ready())
        return 1;
    if (g_dluma_custom_menu_sources_mounted)
    {
        if (dluma_custom_main_menu_sources_ready())
            return 1;
        g_dluma_custom_menu_sources_mounted = 0U;
        dluma_log_pair(
            "INFO", "custom_main_menu_generation",
            "cached_source_generation_retired_republication_required");
    }
    /* Project roots are published through the stock content route.  Their
     * process-lifetime ownership join may lag the synchronous publication
     * call, but that bookkeeping must not suppress the selected frontend.
     * The generated overlay is the actual presentation requirement: it owns
     * the promoted map and XUI destination aliases. */
    if (!dluma_custom_main_menu_native_sources_ready())
        dluma_log_pair(
            "WARN", "custom_main_menu_native_sources",
            "live_ownership_pending_overlay_mount_continues");
    if (!dluma_mount_custom_main_menu_overlay(
            g_dluma_custom_menu_overlay_path,
            g_dluma_custom_menu_selected->snapshot.fingerprint))
    {
        dluma_log_pair(
            "WARN", "custom_main_menu_fallback",
            "generated_overlay_mount_failed");
        return 0;
    }
    g_dluma_custom_menu_sources_mounted = 1U;
    dluma_custom_main_menu_mount_selected_banks();
    return 1;
}

void dluma_custom_main_menu_retire_generation(void)
{
    unsigned int index;
    g_dluma_custom_menu_sources_mounted = 0U;
    for (index = 0; index < g_dluma_custom_menu_bank_count; ++index)
        g_dluma_custom_menu_banks[index].mounted = 0U;
    dluma_log_pair(
        "INFO", "custom_main_menu_generation",
        "presentation_and_bank_readiness_cleared");
}

void dluma_custom_main_menu_disable_for_process(const char *reason)
{
    if (!g_dluma_custom_menu_sources_mounted)
    {
        g_dluma_custom_menu_selected = 0;
        g_dluma_custom_menu_sources_prepared = 0;
        g_dluma_custom_menu_overlay_path[0] = 0;
    }
    dluma_log_pair(
        "WARN", "custom_main_menu_fallback",
        reason && reason[0] ? reason : "runtime_bridge_unavailable");
}

void dluma_custom_main_menu_mark_activation_pending(void)
{
    if (!dluma_custom_main_menu_selected_ready() ||
        g_dluma_custom_menu_activation_pending)
        return;
    dluma_custom_menu_write_receipt("pending");
    g_dluma_custom_menu_activation_pending = 1U;
    dluma_log_pair("INFO", "custom_main_menu_activation_receipt", "pending");
}

void dluma_custom_main_menu_mark_activation_good(void)
{
    if (!dluma_custom_main_menu_selected_ready() ||
        !g_dluma_custom_menu_activation_pending)
        return;
    dluma_custom_menu_write_receipt("good");
    g_dluma_custom_menu_activation_pending = 0;
    dluma_log_pair("INFO", "custom_main_menu_activation_receipt", "good");
}
