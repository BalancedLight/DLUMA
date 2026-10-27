#include "dlw_editor_missing_asset_probe_guard.h"

static char dluma_editor_probe_fold_ascii(char value)
{
    return value >= 'A' && value <= 'Z'
        ? (char)(value + ('a' - 'A')) : value;
}

static int dluma_editor_probe_drive_equal(char left, char right)
{
    return dluma_editor_probe_fold_ascii(left) ==
        dluma_editor_probe_fold_ascii(right);
}

static int dluma_editor_probe_leaf_is_dmt(const char* leaf)
{
    unsigned int size = 0;
    unsigned int index;
    if (!leaf || !leaf[0]) return 0;
    while (size < 1024U && leaf[size]) {
        char value = leaf[size];
        if (value == '\\' || value == '/' ||
            value == ':' || value == '*' || value == '?')
            return 0;
        ++size;
    }
    if (size == 1024U || size <= 4U || leaf[0] == '.' ||
        leaf[size - 4U] != '.' ||
        dluma_editor_probe_fold_ascii(leaf[size - 3U]) != 'd' ||
        dluma_editor_probe_fold_ascii(leaf[size - 2U]) != 'm' ||
        dluma_editor_probe_fold_ascii(leaf[size - 1U]) != 't')
        return 0;
    for (index = 0; index + 1U < size; ++index)
        if (leaf[index] == '.' && leaf[index + 1U] == '.') return 0;
    return 1;
}

int dluma_editor_missing_asset_probe_is_drive_root(
    const char* path,
    char drive_letter)
{
    return path && drive_letter &&
        dluma_editor_probe_drive_equal(path[0], drive_letter) &&
        path[1] == ':' && (path[2] == '\\' || path[2] == '/') &&
        path[3] == 0;
}

int dluma_editor_missing_asset_probe_should_reject(
    const char* requested_path,
    const char* current_directory,
    char guarded_drive_letter)
{
    if (!requested_path || !requested_path[0] ||
        !guarded_drive_letter) return 0;
    if (dluma_editor_probe_drive_equal(
            requested_path[0], guarded_drive_letter) &&
        requested_path[1] == ':' &&
        (requested_path[2] == '\\' || requested_path[2] == '/'))
        return dluma_editor_probe_leaf_is_dmt(requested_path + 3U);
    return dluma_editor_missing_asset_probe_is_drive_root(
               current_directory, guarded_drive_letter) &&
        dluma_editor_probe_leaf_is_dmt(requested_path);
}
