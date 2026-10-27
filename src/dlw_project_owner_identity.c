#include "dlw_project_owner_identity.h"

int dluma_checked_path_join(
    WCHAR* out, unsigned int capacity,
    const WCHAR* left, const WCHAR* right)
{
    SIZE_T left_length;
    SIZE_T right_start = 0;
    SIZE_T right_length;
    SIZE_T separator;
    SIZE_T i;
    WCHAR joined[DLUMA_MAX_WPATH];
    if (!out || !capacity ||
        capacity > DLUMA_ARRAY_COUNT(joined) ||
        !left || !right) return 0;
    left_length = wcslen(left);
    while (right[right_start] == L'\\' ||
           right[right_start] == L'/') ++right_start;
    right_length = wcslen(right + right_start);
    separator = left_length &&
        left[left_length - 1] != L'\\' &&
        left[left_length - 1] != L'/' ? 1U : 0U;
    if (left_length + separator + right_length + 1U >
        capacity) {
        out[0] = 0;
        return 0;
    }
    for (i = 0; i < left_length; ++i)
        joined[i] = left[i];
    if (separator) joined[i++] = L'\\';
    while (right_length--)
        joined[i++] = right[right_start++];
    joined[i] = 0;
    dluma_wstr_copy(out, capacity, joined);
    return 1;
}

static int dluma_map_segment_is_reserved_device(
    const char* map_name)
{
    char base[16];
    unsigned int length = 0;
    while (map_name && map_name[length] &&
           map_name[length] != '.' &&
           length + 1 < DLUMA_ARRAY_COUNT(base)) {
        base[length] = (char)(
            map_name[length] >= 'A' &&
            map_name[length] <= 'Z'
                ? map_name[length] + ('a' - 'A')
                : map_name[length]);
        ++length;
    }
    base[length] = 0;
    if (dluma_stricmp(base, "con") == 0 ||
        dluma_stricmp(base, "prn") == 0 ||
        dluma_stricmp(base, "aux") == 0 ||
        dluma_stricmp(base, "nul") == 0 ||
        dluma_stricmp(base, "clock$") == 0)
        return 1;
    return length == 4 &&
        ((base[0] == 'c' && base[1] == 'o' &&
          base[2] == 'm') ||
         (base[0] == 'l' && base[1] == 'p' &&
          base[2] == 't')) &&
        base[3] >= '1' && base[3] <= '9';
}

int dluma_valid_map_segment(const char* map_name)
{
    unsigned int length = 0;
    unsigned int i;
    if (!map_name || !map_name[0]) return 0;
    while (map_name[length]) {
        unsigned char value =
            (unsigned char)map_name[length];
        if (length + 1 >= DLUMA_MAX_MAP_NAME ||
            value < 0x20 || value >= 0x7f ||
            map_name[length] == '\\' ||
            map_name[length] == '/' ||
            map_name[length] == ':' ||
            map_name[length] == '*' ||
            map_name[length] == '?' ||
            map_name[length] == '"' ||
            map_name[length] == '<' ||
            map_name[length] == '>' ||
            map_name[length] == '|')
            return 0;
        ++length;
    }
    if (!length || map_name[length - 1] == '.' ||
        map_name[length - 1] == ' ' ||
        (length == 1 && map_name[0] == '.') ||
        (length == 2 && map_name[0] == '.' &&
         map_name[1] == '.'))
        return 0;
    for (i = 0; i < length; ++i)
        if ((unsigned char)map_name[i] >= 0x7f) return 0;
    return !dluma_map_segment_is_reserved_device(map_name);
}

int dluma_open_directory_identity(
    const WCHAR* path, DlumaDirectoryIdentity* identity)
{
    HANDLE directory;
    BY_HANDLE_FILE_INFORMATION information;
    DWORD final_length;
    if (!path || !path[0] || !identity) return 0;
    memset(identity, 0, sizeof(*identity));
    directory = CreateFileW(
        path, 0,
        FILE_SHARE_READ | FILE_SHARE_WRITE |
            FILE_SHARE_DELETE,
        0, OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS, 0);
    if (directory == INVALID_HANDLE_VALUE) return 0;
    memset(&information, 0, sizeof(information));
    final_length = GetFinalPathNameByHandleW(
        directory, identity->final_path,
        DLUMA_ARRAY_COUNT(identity->final_path), 0);
    if (!final_length ||
        final_length >=
            DLUMA_ARRAY_COUNT(identity->final_path) ||
        !GetFileInformationByHandle(
            directory, &information) ||
        !(information.dwFileAttributes &
          FILE_ATTRIBUTE_DIRECTORY)) {
        CloseHandle(directory);
        memset(identity, 0, sizeof(*identity));
        return 0;
    }
    CloseHandle(directory);
    identity->volume_serial =
        information.dwVolumeSerialNumber;
    identity->file_index_high =
        information.nFileIndexHigh;
    identity->file_index_low =
        information.nFileIndexLow;
    return 1;
}

int dluma_directory_identity_equal(
    const DlumaDirectoryIdentity* left,
    const DlumaDirectoryIdentity* right)
{
    return left && right &&
        left->volume_serial == right->volume_serial &&
        left->file_index_high == right->file_index_high &&
        left->file_index_low == right->file_index_low &&
        dluma_wstricmp(
            left->final_path, right->final_path) == 0;
}

int dluma_final_path_equal_or_under(
    const WCHAR* path, const WCHAR* root)
{
    SIZE_T root_length;
    SIZE_T i;
    if (!path || !root || !root[0]) return 0;
    root_length = wcslen(root);
    while (root_length > 1 &&
           (root[root_length - 1] == L'\\' ||
            root[root_length - 1] == L'/'))
        --root_length;
    for (i = 0; i < root_length; ++i) {
        if (!path[i] ||
            dluma_wascii_lower(path[i]) !=
                dluma_wascii_lower(root[i]))
            return 0;
    }
    return path[root_length] == 0 ||
        path[root_length] == L'\\' ||
        path[root_length] == L'/';
}

static int dluma_open_regular_final(
    const WCHAR* path, WCHAR* final_path,
    unsigned int final_capacity)
{
    HANDLE file;
    BY_HANDLE_FILE_INFORMATION information;
    DWORD final_length;
    if (!path || !path[0] || !final_path ||
        !final_capacity) return 0;
    final_path[0] = 0;
    file = CreateFileW(
        path, GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE |
            FILE_SHARE_DELETE,
        0, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);
    if (file == INVALID_HANDLE_VALUE) return 0;
    memset(&information, 0, sizeof(information));
    final_length = GetFinalPathNameByHandleW(
        file, final_path, final_capacity, 0);
    if (!final_length ||
        final_length >= final_capacity ||
        !GetFileInformationByHandle(
            file, &information) ||
        (information.dwFileAttributes &
         FILE_ATTRIBUTE_DIRECTORY)) {
        CloseHandle(file);
        final_path[0] = 0;
        return 0;
    }
    CloseHandle(file);
    return 1;
}

int dluma_validate_candidate_under_root(
    const WCHAR* candidate,
    const DlumaDirectoryIdentity* discovery_identity,
    DlumaDirectoryIdentity* candidate_identity)
{
    return candidate && discovery_identity &&
        candidate_identity &&
        dluma_open_directory_identity(
            candidate, candidate_identity) &&
        dluma_final_path_equal_or_under(
            candidate_identity->final_path,
            discovery_identity->final_path);
}

int dluma_validate_map_anchor_under_project(
    const WCHAR* project, const char* map_name,
    const DlumaDirectoryIdentity* project_identity)
{
    WCHAR map_wide[DLUMA_MAX_MAP_NAME];
    WCHAR map_directory[DLUMA_MAX_WPATH];
    WCHAR anchor_name[DLUMA_MAX_MAP_NAME + 8];
    WCHAR anchor_path[DLUMA_MAX_WPATH];
    WCHAR final_anchor[DLUMA_MAX_WPATH];
    DlumaDirectoryIdentity map_identity;
    if (!project || !map_name || !project_identity ||
        !dluma_valid_map_segment(map_name) ||
        !dluma_utf8_to_wide(
            map_name, map_wide,
            DLUMA_ARRAY_COUNT(map_wide)) ||
        !dluma_checked_path_join(
            map_directory,
            DLUMA_ARRAY_COUNT(map_directory),
            project, L"data\\maps") ||
        !dluma_checked_path_join(
            map_directory,
            DLUMA_ARRAY_COUNT(map_directory),
            map_directory, map_wide) ||
        !dluma_open_directory_identity(
            map_directory, &map_identity) ||
        !dluma_final_path_equal_or_under(
            map_identity.final_path,
            project_identity->final_path))
        return 0;
    dluma_wstr_copy(
        anchor_name, DLUMA_ARRAY_COUNT(anchor_name),
        map_wide);
    if (wcslen(anchor_name) + wcslen(L".scr") + 1U >
        DLUMA_ARRAY_COUNT(anchor_name))
        return 0;
    dluma_wstr_append(
        anchor_name, DLUMA_ARRAY_COUNT(anchor_name),
        L".scr");
    if (!dluma_checked_path_join(
            anchor_path, DLUMA_ARRAY_COUNT(anchor_path),
            map_directory, anchor_name) ||
        !dluma_open_regular_final(
            anchor_path, final_anchor,
            DLUMA_ARRAY_COUNT(final_anchor)) ||
        !dluma_final_path_equal_or_under(
            final_anchor, map_identity.final_path))
        return 0;
    return 1;
}

int dluma_project_roots_same_identity(
    const WCHAR* first, const WCHAR* second)
{
    DlumaDirectoryIdentity first_identity;
    DlumaDirectoryIdentity second_identity;
    return dluma_open_directory_identity(
               first, &first_identity) &&
        dluma_open_directory_identity(
            second, &second_identity) &&
        dluma_directory_identity_equal(
            &first_identity, &second_identity);
}

int dluma_project_candidate_root_is_contained(
    const WCHAR* discovery_root,
    const WCHAR* candidate)
{
    DlumaDirectoryIdentity discovery_identity;
    DlumaDirectoryIdentity candidate_identity;
    return dluma_open_directory_identity(
               discovery_root, &discovery_identity) &&
        dluma_validate_candidate_under_root(
            candidate, &discovery_identity,
            &candidate_identity);
}

int dluma_project_map_anchor_is_contained(
    const WCHAR* project_root, const char* map_name)
{
    DlumaDirectoryIdentity project_identity;
    return dluma_open_directory_identity(
               project_root, &project_identity) &&
        dluma_validate_map_anchor_under_project(
            project_root, map_name, &project_identity);
}

int dluma_project_regular_file_is_contained(
    const WCHAR* project_root, const WCHAR* file_path)
{
    DlumaDirectoryIdentity project_identity;
    WCHAR final_file[DLUMA_MAX_WPATH];
    return dluma_open_directory_identity(
               project_root, &project_identity) &&
        dluma_open_regular_final(
            file_path, final_file,
            DLUMA_ARRAY_COUNT(final_file)) &&
        dluma_final_path_equal_or_under(
            final_file, project_identity.final_path);
}

int dluma_project_owner_pending_proof_retains_context(
    unsigned int active_owner_epoch,
    ULONGLONG active_owner_transition_generation,
    const DlumaProjectOwnerProofSnapshot* proof)
{
    return active_owner_epoch != 0 &&
        active_owner_transition_generation != 0 &&
        proof &&
        proof->status == DLUMA_PROJECT_OWNER_PROOF_PENDING;
}
