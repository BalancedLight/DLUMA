#include "dlw_common.h"
#include "dlw_first_launch.h"

typedef struct DlumaFirstLaunchState {
    WCHAR directory[DLUMA_MAX_WPATH];
    WCHAR path[DLUMA_MAX_WPATH];
    volatile LONG lock;
    unsigned char initialized;
    unsigned char accepted;
} DlumaFirstLaunchState;

static DlumaFirstLaunchState g_dluma_first_launch;

static void dluma_first_launch_lock(void)
{
    while (dluma_atomic_compare_exchange(&g_dluma_first_launch.lock, 1, 0) != 0)
        Sleep(1);
}

static void dluma_first_launch_unlock(void)
{
    dluma_atomic_exchange(&g_dluma_first_launch.lock, 0);
}

static void dluma_first_launch_initialize_locked(void)
{
    WCHAR local_app_data[DLUMA_MAX_WPATH];
    WCHAR accepted[4];
    DWORD length;

    if (g_dluma_first_launch.initialized)
        return;
    g_dluma_first_launch.initialized = 1;
    length = GetEnvironmentVariableW(
        L"LOCALAPPDATA", local_app_data, DLUMA_ARRAY_COUNT(local_app_data));
    if (!length || length + 17U >= DLUMA_ARRAY_COUNT(local_app_data)) {
        dluma_log_line("WARN", "first_launch_state_directory_unavailable");
        return;
    }
    dluma_path_join(
        g_dluma_first_launch.directory,
        DLUMA_ARRAY_COUNT(g_dluma_first_launch.directory),
        local_app_data, L"DLUMA");
    dluma_path_join(
        g_dluma_first_launch.path, DLUMA_ARRAY_COUNT(g_dluma_first_launch.path),
        g_dluma_first_launch.directory, L"state.ini");
    accepted[0] = 0;
    length = GetPrivateProfileStringW(
        L"first_launch", L"accepted", L"0", accepted,
        DLUMA_ARRAY_COUNT(accepted), g_dluma_first_launch.path);
    g_dluma_first_launch.accepted =
        (unsigned char)(length == 1U && accepted[0] == L'1');
}

int dluma_first_launch_is_pending(void)
{
    int pending;
    dluma_first_launch_lock();
    dluma_first_launch_initialize_locked();
    pending = !g_dluma_first_launch.accepted;
    dluma_first_launch_unlock();
    return pending;
}

int dluma_first_launch_accept(void)
{
    int persisted = 0;
    dluma_first_launch_lock();
    dluma_first_launch_initialize_locked();
    g_dluma_first_launch.accepted = 1;
    if (g_dluma_first_launch.path[0] &&
        (CreateDirectoryW(g_dluma_first_launch.directory, 0) ||
         dluma_dir_exists(g_dluma_first_launch.directory))) {
        persisted = WritePrivateProfileStringW(
            L"first_launch", L"accepted", L"1",
            g_dluma_first_launch.path) != 0;
        if (persisted)
            (void)WritePrivateProfileStringW(
                0, 0, 0, g_dluma_first_launch.path);
    }
    dluma_first_launch_unlock();
    dluma_log_line(
        persisted ? "INFO" : "WARN",
        persisted ? "first_launch_accepted_saved" :
                    "first_launch_accepted_for_session_save_failed");
    return persisted;
}
