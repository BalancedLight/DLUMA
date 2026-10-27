#include "dlw_discovery_watch.h"

#define DLUMA_DISCOVERY_WATCH_MAX_HANDLES \
    (DLUMA_MAX_ROOTS + DLUMA_MAX_LUA_FILES * 4U)
#define DLUMA_DISCOVERY_NAME_FILTER \
    (FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME)
#define DLUMA_DISCOVERY_CONTENT_FILTER \
    (DLUMA_DISCOVERY_NAME_FILTER | FILE_NOTIFY_CHANGE_SIZE | \
     FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_CREATION)

typedef struct DlumaDiscoveryWatchHandle {
    HANDLE notification;
    unsigned char names_only;
} DlumaDiscoveryWatchHandle;

typedef struct DlumaDiscoveryWatchState {
    DlumaDiscoveryWatchHandle handles[
        DLUMA_DISCOVERY_WATCH_MAX_HANDLES];
    unsigned int handle_count;
    unsigned int root_count;
    ULONGLONG roots_hash;
    unsigned char fallback_required;
    unsigned char rebuild_required;
} DlumaDiscoveryWatchState;

static DlumaDiscoveryWatchState g_dluma_discovery_watch;

static int dluma_discovery_watch_join(
    WCHAR* out, unsigned int capacity,
    const WCHAR* left, const WCHAR* right)
{
    unsigned int left_length = 0;
    unsigned int right_length = 0;
    unsigned int write = 0;
    int separator;
    if (!out || !capacity || !left || !right)
        return 0;
    while (left_length < capacity && left[left_length])
        ++left_length;
    while (right_length < capacity && right[right_length])
        ++right_length;
    if (left_length >= capacity || right_length >= capacity)
        return 0;
    separator = left_length &&
        left[left_length - 1U] != L'\\' &&
        left[left_length - 1U] != L'/';
    if (left_length + (separator ? 1U : 0U) >= capacity ||
        right_length >
            capacity - left_length -
                (separator ? 1U : 0U) - 1U)
        return 0;
    while (write < left_length) {
        out[write] = left[write];
        ++write;
    }
    if (separator)
        out[write++] = L'\\';
    {
        unsigned int index;
        for (index = 0; index < right_length; ++index)
            out[write++] = right[index];
    }
    out[write] = 0;
    return 1;
}

static ULONGLONG dluma_discovery_watch_roots_hash(void)
{
    ULONGLONG hash = 1469598103934665603ULL;
    unsigned int root_index;
    for (root_index = 0;
         root_index < g_dluma.workshop_root_count;
         ++root_index) {
        unsigned int character_index;
        for (character_index = 0;
             g_dluma.workshop_roots[root_index][character_index];
             ++character_index) {
            hash ^= (ULONGLONG)dluma_wascii_lower(
                g_dluma.workshop_roots[root_index][character_index]);
            hash *= 1099511628211ULL;
        }
        hash ^= 0xFFU;
        hash *= 1099511628211ULL;
    }
    hash ^= g_dluma.workshop_root_count;
    return hash;
}

void dluma_discovery_watch_shutdown(void)
{
    unsigned int index;
    for (index = 0;
         index < g_dluma_discovery_watch.handle_count;
         ++index) {
        if (g_dluma_discovery_watch.handles[index].notification !=
            INVALID_HANDLE_VALUE)
            FindCloseChangeNotification(
                g_dluma_discovery_watch.handles[index].notification);
    }
    memset(
        &g_dluma_discovery_watch, 0,
        sizeof(g_dluma_discovery_watch));
}

static void dluma_discovery_watch_add(
    const WCHAR* path, unsigned char names_only,
    unsigned char subtree)
{
    HANDLE notification;
    DWORD filter = names_only
        ? DLUMA_DISCOVERY_NAME_FILTER
        : DLUMA_DISCOVERY_CONTENT_FILTER;
    DlumaDiscoveryWatchHandle* entry;
    if (!path || !path[0] || !dluma_dir_exists(path))
        return;
    if (g_dluma_discovery_watch.handle_count >=
        DLUMA_DISCOVERY_WATCH_MAX_HANDLES) {
        g_dluma_discovery_watch.fallback_required = 1;
        return;
    }
    notification = FindFirstChangeNotificationW(
        path, subtree ? TRUE : FALSE, filter);
    if (notification == INVALID_HANDLE_VALUE) {
        g_dluma_discovery_watch.fallback_required = 1;
        return;
    }
    entry = &g_dluma_discovery_watch.handles[
        g_dluma_discovery_watch.handle_count++];
    entry->notification = notification;
    entry->names_only = names_only;
}

static void dluma_discovery_watch_add_project(
    const WCHAR* project_root)
{
    WCHAR path[DLUMA_MAX_WPATH];
    dluma_discovery_watch_add(project_root, 0, 0);
    if (dluma_discovery_watch_join(
            path, DLUMA_ARRAY_COUNT(path), project_root,
            L"out"))
        dluma_discovery_watch_add(path, 0, 1);
    if (dluma_discovery_watch_join(
            path, DLUMA_ARRAY_COUNT(path), project_root,
            L"data\\scripts\\lua"))
        dluma_discovery_watch_add(path, 0, 1);
    if (dluma_discovery_watch_join(
            path, DLUMA_ARRAY_COUNT(path), project_root,
            L"data\\maps"))
        dluma_discovery_watch_add(path, 0, 1);
}

static void dluma_discovery_watch_add_root(
    const WCHAR* root)
{
    WCHAR pattern[DLUMA_MAX_WPATH];
    WIN32_FIND_DATAW data;
    HANDLE find;
    DWORD error;
    dluma_discovery_watch_add(root, 1, 1);
    dluma_discovery_watch_add_project(root);
    if (!dluma_discovery_watch_join(
            pattern, DLUMA_ARRAY_COUNT(pattern), root, L"*")) {
        g_dluma_discovery_watch.fallback_required = 1;
        return;
    }
    find = FindFirstFileW(pattern, &data);
    if (find == INVALID_HANDLE_VALUE) {
        error = GetLastError();
        if (error != ERROR_FILE_NOT_FOUND &&
            error != ERROR_NO_MORE_FILES)
            g_dluma_discovery_watch.fallback_required = 1;
        return;
    }
    do {
        WCHAR project[DLUMA_MAX_WPATH];
        if (!(data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ||
            (data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) ||
            (data.cFileName[0] == L'.' &&
             (!data.cFileName[1] ||
              (data.cFileName[1] == L'.' &&
               !data.cFileName[2]))))
            continue;
        if (!dluma_discovery_watch_join(
                project, DLUMA_ARRAY_COUNT(project),
                root, data.cFileName)) {
            g_dluma_discovery_watch.fallback_required = 1;
            continue;
        }
        dluma_discovery_watch_add_project(project);
    } while (FindNextFileW(find, &data));
    error = GetLastError();
    FindClose(find);
    if (error != ERROR_NO_MORE_FILES)
        g_dluma_discovery_watch.fallback_required = 1;
}

int dluma_discovery_watch_sync_roots(void)
{
    ULONGLONG current_hash = dluma_discovery_watch_roots_hash();
    unsigned int root_index;
    int changed =
        g_dluma_discovery_watch.rebuild_required ||
        current_hash != g_dluma_discovery_watch.roots_hash ||
        g_dluma_discovery_watch.root_count !=
            g_dluma.workshop_root_count;
    if (!changed) return 0;
    dluma_discovery_watch_shutdown();
    g_dluma_discovery_watch.roots_hash = current_hash;
    g_dluma_discovery_watch.root_count =
        g_dluma.workshop_root_count;
    for (root_index = 0;
         root_index < g_dluma.workshop_root_count;
         ++root_index)
        dluma_discovery_watch_add_root(
            g_dluma.workshop_roots[root_index]);
    if (!g_dluma.workshop_root_count)
        g_dluma_discovery_watch.fallback_required = 1;
    return 1;
}

int dluma_discovery_watch_poll(void)
{
    unsigned int index;
    int dirty = 0;
    for (index = 0;
         index < g_dluma_discovery_watch.handle_count;
         ++index) {
        DlumaDiscoveryWatchHandle* entry =
            &g_dluma_discovery_watch.handles[index];
        HANDLE notification = entry->notification;
        if (notification == INVALID_HANDLE_VALUE) continue;
        if (WaitForSingleObject(notification, 0) != WAIT_OBJECT_0)
            continue;
        dirty = 1;
        if (entry->names_only)
            g_dluma_discovery_watch.rebuild_required = 1;
        if (!FindNextChangeNotification(notification)) {
            FindCloseChangeNotification(notification);
            entry->notification =
                INVALID_HANDLE_VALUE;
            g_dluma_discovery_watch.fallback_required = 1;
        }
    }
    return dirty;
}

int dluma_discovery_watch_requires_fallback(void)
{
    return g_dluma_discovery_watch.fallback_required != 0;
}
