#include "dlw_common.h"
#include "dlw_mouse_lock_core.h"

#define DLUMA_MOUSE_LOCK_LOG_PLATFORM (1UL << 0)
#define DLUMA_MOUSE_LOCK_LOG_GEOMETRY (1UL << 1)
#define DLUMA_MOUSE_LOCK_LOG_APPLY (1UL << 2)
#define DLUMA_MOUSE_LOCK_LOG_RELEASE (1UL << 3)
#define DLUMA_MOUSE_LOCK_LOG_FOREIGN (1UL << 4)

static DlumaMouseLockCoreState g_dluma_mouse_lock;
static volatile LONG g_dluma_mouse_lock_state_lock;
static DWORD g_dluma_mouse_lock_logged_failures;

static void dluma_mouse_lock_state_lock(void)
{
    while (dluma_atomic_compare_exchange(
               &g_dluma_mouse_lock_state_lock, 1, 0) != 0)
        Sleep(0);
}

static void dluma_mouse_lock_state_unlock(void)
{
    dluma_atomic_exchange(&g_dluma_mouse_lock_state_lock, 0);
}

static void *dluma_mouse_lock_get_foreground(void *context)
{
    (void)context;
    return GetForegroundWindow();
}

static int dluma_mouse_lock_is_iconic(
    void *context, void *window)
{
    (void)context;
    return IsIconic((HWND)window) ? 1 : 0;
}

static int dluma_mouse_lock_get_client_rect(
    void *context, void *window, DlumaMouseLockRect *rect_out)
{
    RECT rect;
    (void)context;
    if (!rect_out ||
        !GetClientRect((HWND)window, &rect))
        return 0;
    rect_out->left = rect.left;
    rect_out->top = rect.top;
    rect_out->right = rect.right;
    rect_out->bottom = rect.bottom;
    return 1;
}

static int dluma_mouse_lock_client_to_screen(
    void *context, void *window, DlumaMouseLockPoint *point)
{
    POINT native_point;
    (void)context;
    if (!point)
        return 0;
    native_point.x = point->x;
    native_point.y = point->y;
    if (!ClientToScreen((HWND)window, &native_point))
        return 0;
    point->x = native_point.x;
    point->y = native_point.y;
    return 1;
}

static int dluma_mouse_lock_get_clip_cursor(
    void *context, DlumaMouseLockRect *rect_out)
{
    RECT rect;
    (void)context;
    if (!rect_out || !GetClipCursor(&rect))
        return 0;
    rect_out->left = rect.left;
    rect_out->top = rect.top;
    rect_out->right = rect.right;
    rect_out->bottom = rect.bottom;
    return 1;
}

static int dluma_mouse_lock_clip_cursor(
    void *context, const DlumaMouseLockRect *rect)
{
    RECT native_rect;
    (void)context;
    if (!rect)
        return ClipCursor(0) ? 1 : 0;
    native_rect.left = rect->left;
    native_rect.top = rect->top;
    native_rect.right = rect->right;
    native_rect.bottom = rect->bottom;
    return ClipCursor(&native_rect) ? 1 : 0;
}

static const DlumaMouseLockPlatform g_dluma_mouse_lock_platform = {
    0,
    dluma_mouse_lock_get_foreground,
    dluma_mouse_lock_is_iconic,
    dluma_mouse_lock_get_client_rect,
    dluma_mouse_lock_client_to_screen,
    dluma_mouse_lock_get_clip_cursor,
    dluma_mouse_lock_clip_cursor};

static void dluma_mouse_lock_log_failure_once(
    DWORD bit, const char *message)
{
    if ((g_dluma_mouse_lock_logged_failures & bit) != 0)
        return;
    g_dluma_mouse_lock_logged_failures |= bit;
    dluma_log_line("WARN", message);
}

static void dluma_mouse_lock_note_result(
    DlumaMouseLockCoreResult result, int previously_owned)
{
    if (result == DLUMA_MOUSE_LOCK_CORE_APPLIED)
    {
        if (!previously_owned)
            dluma_log_pair(
                "INFO", "mouse_window_lock", "confined");
    }
    else if (result == DLUMA_MOUSE_LOCK_CORE_RELEASED)
    {
        if (previously_owned)
            dluma_log_pair(
                "INFO", "mouse_window_lock", "released");
    }
    else if (result ==
             DLUMA_MOUSE_LOCK_CORE_FOREIGN_CLIP_PRESERVED)
    {
        dluma_mouse_lock_log_failure_once(
            DLUMA_MOUSE_LOCK_LOG_FOREIGN,
            "Mouse-window lock observed a different shared cursor clip while losing focus; the foreign clip was preserved.");
    }
    else if (result ==
             DLUMA_MOUSE_LOCK_CORE_GEOMETRY_UNAVAILABLE)
    {
        dluma_mouse_lock_log_failure_once(
            DLUMA_MOUSE_LOCK_LOG_GEOMETRY,
            "Mouse-window lock could not resolve a valid client rectangle; confinement is temporarily unavailable.");
    }
    else if (result == DLUMA_MOUSE_LOCK_CORE_APPLY_FAILED)
    {
        dluma_mouse_lock_log_failure_once(
            DLUMA_MOUSE_LOCK_LOG_APPLY,
            "ClipCursor failed while applying the mouse-window lock; the game continues without loader confinement.");
    }
    else if (result == DLUMA_MOUSE_LOCK_CORE_RELEASE_FAILED)
    {
        dluma_mouse_lock_log_failure_once(
            DLUMA_MOUSE_LOCK_LOG_RELEASE,
            "ClipCursor failed while releasing the mouse-window lock.");
    }
    else if (result ==
             DLUMA_MOUSE_LOCK_CORE_PLATFORM_UNAVAILABLE)
    {
        dluma_mouse_lock_log_failure_once(
            DLUMA_MOUSE_LOCK_LOG_PLATFORM,
            "Mouse-window lock Win32 services are unavailable; the game continues without loader confinement.");
    }
}

static void dluma_mouse_lock_maintain_locked(void)
{
    DlumaMouseLockCoreResult result;
    const int previously_owned =
        g_dluma_mouse_lock.owns_clip ? 1 : 0;
    result = dluma_mouse_lock_core_maintain(
        &g_dluma_mouse_lock, &g_dluma_mouse_lock_platform);
    dluma_mouse_lock_note_result(result, previously_owned);
}

static void dluma_mouse_lock_release_locked(int synchronous)
{
    DlumaMouseLockCoreResult result;
    const int previously_owned =
        g_dluma_mouse_lock.owns_clip ? 1 : 0;
    result = dluma_mouse_lock_core_release(
        &g_dluma_mouse_lock,
        &g_dluma_mouse_lock_platform,
        synchronous);
    dluma_mouse_lock_note_result(result, previously_owned);
}

static void dluma_mouse_lock_suspend_locked(void)
{
    DlumaMouseLockCoreResult result;
    const int previously_owned =
        g_dluma_mouse_lock.owns_clip ? 1 : 0;
    result = dluma_mouse_lock_core_suspend(
        &g_dluma_mouse_lock, &g_dluma_mouse_lock_platform);
    dluma_mouse_lock_note_result(result, previously_owned);
}

void dluma_mouse_lock_start(int enabled)
{
    dluma_mouse_lock_state_lock();
    if (g_dluma_mouse_lock.owns_clip)
        dluma_mouse_lock_release_locked(1);
    dluma_mouse_lock_core_initialize(
        &g_dluma_mouse_lock, enabled);
    g_dluma_mouse_lock_logged_failures = 0;
    dluma_mouse_lock_state_unlock();
    dluma_log_pair(
        "INFO", "mouse_window_lock",
        enabled ? "enabled" : "disabled");
}

void dluma_mouse_lock_bind_window(HWND window)
{
    if (!window)
        return;
    dluma_mouse_lock_state_lock();
    if (g_dluma_mouse_lock.bound &&
        g_dluma_mouse_lock.window != window)
    {
        DlumaMouseLockCoreResult result;
        const int previously_owned =
            g_dluma_mouse_lock.owns_clip ? 1 : 0;
        result = dluma_mouse_lock_core_unbind(
            &g_dluma_mouse_lock,
            &g_dluma_mouse_lock_platform);
        dluma_mouse_lock_note_result(
            result, previously_owned);
    }
    dluma_mouse_lock_core_bind(
        &g_dluma_mouse_lock, window);
    dluma_mouse_lock_maintain_locked();
    dluma_mouse_lock_state_unlock();
}

static int dluma_mouse_lock_is_deactivation_message(
    unsigned int message, WPARAM wparam)
{
    if (message == WM_ACTIVATEAPP)
        return wparam == 0;
    if (message == WM_ACTIVATE)
        return ((unsigned int)wparam & 0xFFFFU) ==
            WA_INACTIVE;
    if (message == WM_KILLFOCUS)
        return 1;
    if (message == WM_SIZE)
        return (unsigned int)wparam == SIZE_MINIMIZED;
    return message == WM_DESTROY ||
           message == WM_NCDESTROY;
}

static int dluma_mouse_lock_is_interactive_move_start(
    unsigned int message, WPARAM wparam)
{
    return message == WM_ENTERSIZEMOVE ||
           (message == WM_NCLBUTTONDOWN &&
            ((unsigned int)wparam & 0xFFFFU) == HTCAPTION);
}

static int dluma_mouse_lock_is_refresh_message(
    unsigned int message, WPARAM wparam)
{
    if (message == WM_ACTIVATEAPP)
        return wparam != 0;
    if (message == WM_ACTIVATE)
        return ((unsigned int)wparam & 0xFFFFU) !=
            WA_INACTIVE;
    if (message == WM_SIZE)
        return (unsigned int)wparam != SIZE_MINIMIZED;
    return message == WM_SETFOCUS ||
           message == WM_MOVE ||
           message == WM_WINDOWPOSCHANGED ||
           message == WM_DISPLAYCHANGE ||
           message == WM_DPICHANGED;
}

void dluma_mouse_lock_window_message_pre(
    HWND window, unsigned int message,
    WPARAM wparam, LPARAM lparam)
{
    (void)lparam;
    dluma_mouse_lock_state_lock();
    if (g_dluma_mouse_lock.bound &&
        g_dluma_mouse_lock.window == window)
    {
        if (dluma_mouse_lock_is_interactive_move_start(
                message, wparam))
        {
            dluma_mouse_lock_suspend_locked();
        }
        else if (dluma_mouse_lock_is_deactivation_message(
                     message, wparam))
        {
            if (message == WM_DESTROY ||
                message == WM_NCDESTROY)
            {
                DlumaMouseLockCoreResult result;
                const int previously_owned =
                    g_dluma_mouse_lock.owns_clip ? 1 : 0;
                result = dluma_mouse_lock_core_unbind(
                    &g_dluma_mouse_lock,
                    &g_dluma_mouse_lock_platform);
                dluma_mouse_lock_note_result(
                    result, previously_owned);
            }
            else
            {
                dluma_mouse_lock_release_locked(1);
            }
        }
    }
    dluma_mouse_lock_state_unlock();
}

void dluma_mouse_lock_window_message_post(
    HWND window, unsigned int message,
    WPARAM wparam, LPARAM lparam)
{
    (void)lparam;
    if (message == WM_EXITSIZEMOVE)
    {
        dluma_mouse_lock_state_lock();
        if (g_dluma_mouse_lock.bound &&
            g_dluma_mouse_lock.window == window)
        {
            dluma_mouse_lock_core_resume(&g_dluma_mouse_lock);
            dluma_mouse_lock_maintain_locked();
        }
        dluma_mouse_lock_state_unlock();
        return;
    }
    if (!dluma_mouse_lock_is_refresh_message(
            message, wparam))
        return;
    dluma_mouse_lock_state_lock();
    if (g_dluma_mouse_lock.bound &&
        g_dluma_mouse_lock.window == window)
        dluma_mouse_lock_maintain_locked();
    dluma_mouse_lock_state_unlock();
}

void dluma_mouse_lock_post_game_frame(void)
{
    if (g_dluma.stop_requested)
        return;
    dluma_mouse_lock_state_lock();
    dluma_mouse_lock_maintain_locked();
    dluma_mouse_lock_state_unlock();
}

void dluma_mouse_lock_quiesce(void)
{
    DlumaMouseLockCoreResult result;
    int previously_owned;
    dluma_mouse_lock_state_lock();
    previously_owned =
        g_dluma_mouse_lock.owns_clip ? 1 : 0;
    result = dluma_mouse_lock_core_quiesce(
        &g_dluma_mouse_lock,
        &g_dluma_mouse_lock_platform);
    dluma_mouse_lock_note_result(
        result, previously_owned);
    dluma_mouse_lock_state_unlock();
}

void dluma_mouse_lock_reset(void)
{
    DlumaMouseLockCoreResult result;
    int previously_owned;
    dluma_mouse_lock_state_lock();
    previously_owned =
        g_dluma_mouse_lock.owns_clip ? 1 : 0;
    result = dluma_mouse_lock_core_unbind(
        &g_dluma_mouse_lock,
        &g_dluma_mouse_lock_platform);
    dluma_mouse_lock_note_result(
        result, previously_owned);
    dluma_mouse_lock_core_initialize(
        &g_dluma_mouse_lock, 0);
    g_dluma_mouse_lock_logged_failures = 0;
    dluma_mouse_lock_state_unlock();
}
