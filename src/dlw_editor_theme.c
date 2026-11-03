#include "dlw_common.h"
#include "dlw_atomic_ini.h"
#include "dlw_dual_host_runtime.h"
#include "dlw_editor_theme.h"
#include "dlw_editor_theme_controls.h"
#include "dlw_editor_theme_properties.h"
#include "dlw_editor_theme_scintilla.h"
#include "dlw_editor_theme_profile.h"
#include "dlw_editor_theme_framework.h"
#include "dlw_editor_theme_grid.h"
#include "dlw_editor_theme_mfc.h"
#include "dlw_log.h"
#include "dlw_path_security.h"
#include "dlw_project_owner_identity.h"
#include "core/dlw_runtime_tree.h"
#include "generated/dluma_api_catalog.generated.h"

__declspec(dllimport) void WINAPI AcquireSRWLockExclusive(void *lock);
__declspec(dllimport) void WINAPI ReleaseSRWLockExclusive(void *lock);
__declspec(dllimport) BOOL WINAPI SetWindowPos(HWND, HWND, int, int, int, int, UINT);
__declspec(dllimport) HWND WINAPI GetDlgItem(HWND, int);

typedef int (*ThemeStartFn)(DlumaEditorThemeSystemChangedFn, void *,
                             DlumaEditorThemeSystemState *);
typedef int (*ThemeResolveFn)(unsigned int, const DlumaEditorThemeSystemState *,
                               DlumaEditorThemeResolved *);
typedef void (*ThemeStopFn)(void);

static struct {
    void *lock;
    volatile LONG state;
    unsigned int preference;
    DlumaEditorThemeSystemState system;
    DlumaEditorThemeSchedule schedule;
    DlumaEditorThemeResolved current;
    HMODULE helper;
    ThemeStartFn start;
    ThemeResolveFn resolve;
    ThemeStopFn stop;
    DlumaRuntimeTreeLease runtime;
    HWND page, label, combo;
} theme;

static const WCHAR *theme_preference_name(unsigned int preference)
{
    if (preference == DLUMA_EDITOR_THEME_LIGHT) return L"light";
    if (preference == DLUMA_EDITOR_THEME_DARK) return L"dark";
    return L"system";
}

static unsigned int theme_read_preference(void)
{
    WCHAR value[32];
    (void)GetPrivateProfileStringW(L"loader", L"editor_theme", L"system",
                                   value, DLUMA_ARRAY_COUNT(value),
                                   g_dluma.config_path);
    if (!dluma_wstricmp(value, L"light")) return DLUMA_EDITOR_THEME_LIGHT;
    if (!dluma_wstricmp(value, L"dark")) return DLUMA_EDITOR_THEME_DARK;
    return DLUMA_EDITOR_THEME_SYSTEM;
}

static void DLUMA_THEME_CALL theme_system_changed(
    void *context, const DlumaEditorThemeSystemState *state)
{
    DlumaEditorThemeResolved resolved;
    int changed = 0;
    (void)context;
    if (!state || state->size != sizeof(*state) || state->version != 1U) return;
    AcquireSRWLockExclusive(&theme.lock);
    if (theme.state == 2) {
        theme.system = *state;
        memset(&resolved, 0, sizeof(resolved));
        resolved.size = sizeof(resolved);
        resolved.version = 1;
        if (theme.resolve(theme.preference, &theme.system, &resolved)) {
            changed = dluma_editor_theme_schedule_publish(&theme.schedule, &resolved);
            if (changed > 0) (void)dluma_editor_theme_schedule_take(
                &theme.schedule, &theme.current);
            theme.current.preference = theme.preference;
        }
    }
    ReleaseSRWLockExclusive(&theme.lock);
    if (changed > 0) (void)dluma_editor_ui_request_refresh();
}

int dluma_editor_theme_copy_snapshot(DlumaEditorThemeResolved *out)
{
    int available;
    if (!out || out->size != sizeof(*out) || out->version != 1U) return 0;
    AcquireSRWLockExclusive(&theme.lock);
    available = theme.state == 2 && theme.current.generation != 0;
    if (available) *out = theme.current;
    ReleaseSRWLockExclusive(&theme.lock);
    return available;
}

int dluma_editor_theme_set_preference(unsigned int preference)
{
    WCHAR temporary[DLUMA_MAX_WPATH];
    DlumaEditorThemeSystemState system;
    temporary[0] = 0;
    if (preference > DLUMA_EDITOR_THEME_DARK) preference = DLUMA_EDITOR_THEME_SYSTEM;
    if (dluma_atomic_compare_exchange(&theme.state, 0, 0) != 2) return 0;
    if (!dluma_atomic_ini_begin(g_dluma.config_path, temporary,
                                DLUMA_ARRAY_COUNT(temporary)) ||
        !WritePrivateProfileStringW(L"loader", L"editor_theme",
                                    theme_preference_name(preference), temporary)) {
        if (temporary[0]) dluma_atomic_ini_abort(temporary);
        dluma_log_line("WARN", "Could not save the Editor theme setting.");
        return 0;
    }
    if (!dluma_atomic_ini_commit(g_dluma.config_path, temporary)) {
        dluma_log_line("WARN", "Could not save the Editor theme setting.");
        return 0;
    }
    AcquireSRWLockExclusive(&theme.lock);
    theme.preference = preference;
    system = theme.system;
    ReleaseSRWLockExclusive(&theme.lock);
    theme_system_changed(0, &system);
    return 1;
}

static int theme_listener(HWND window, unsigned int stage, UINT message,
                           WPARAM wparam, LPARAM lparam, LRESULT *result,
                           void *context)
{
    DlumaEditorThemeResolved current;
    (void)context;
    if (stage == DLUMA_EDITOR_UI_DESTROY) {
        if (window == theme.page) dluma_editor_theme_options_detach(window);
        dluma_editor_theme_controls_detach(window);
        (void)dluma_editor_theme_scintilla_message(window, stage, message, wparam, lparam, result);
        dluma_editor_theme_grid_detach(window);
        dluma_editor_theme_mfc_detach(window);
        return 0;
    }
    if (stage == DLUMA_EDITOR_UI_BEFORE &&
        (message == DLUMA_EDITOR_UI_ENROLLED ||
         message == DLUMA_EDITOR_UI_REFRESH)) {
        if (theme.state != 2) {
            dluma_editor_theme_controls_detach(window);
            dluma_editor_theme_scintilla_detach(window);
            dluma_editor_theme_grid_detach(window);
            dluma_editor_theme_mfc_detach(window);
            return 0;
        }
        memset(&current, 0, sizeof(current));
        current.size = sizeof(current); current.version = 1;
        if (dluma_editor_theme_copy_snapshot(&current)) {
            if (dluma_editor_theme_controls_attach(window))
                dluma_editor_theme_controls_apply(window, &current);
            if (dluma_editor_theme_profile_admit(DLUMA_EDITOR_THEME_PROFILE_QUEST) &&
                dluma_editor_theme_scintilla_attach(window)) {
                dluma_editor_theme_scintilla_apply(window, &current);
                if (dluma_editor_theme_controls_attach(window))
                    dluma_editor_theme_controls_apply(window, &current);
            }
            (void)dluma_editor_theme_framework_apply(window, &current);
            if (dluma_editor_theme_grid_attach(window))
                dluma_editor_theme_grid_apply(window, &current);
            if (dluma_editor_theme_mfc_attach(window))
                dluma_editor_theme_mfc_apply(window, &current);
        }
        return 0;
    }
    if (stage == DLUMA_EDITOR_UI_BEFORE && message == WM_COMMAND &&
        window == theme.page && (HWND)lparam == theme.combo &&
        ((wparam >> 16) & 0xFFFFU) == 1U) {
        unsigned int selection = (unsigned int)SendMessageW(theme.combo, 0x147, 0, 0);
        if (!dluma_editor_theme_set_preference(selection))
            (void)SendMessageW(theme.combo, 0x14E, theme.preference, 0);
        *result = 0;
        return 1;
    }
    if (stage == DLUMA_EDITOR_UI_AFTER &&
        (message == 0x0001U || message == 0x0018U || message == 0x0110U)) {
        memset(&current, 0, sizeof(current));
        current.size = sizeof(current); current.version = 1;
        if (dluma_editor_theme_copy_snapshot(&current)) {
            if (dluma_editor_theme_controls_attach(window))
                dluma_editor_theme_controls_apply(window, &current);
            (void)dluma_editor_theme_framework_apply(window, &current);
            if (dluma_editor_theme_profile_admit(DLUMA_EDITOR_THEME_PROFILE_QUEST)) {
                HWND source;
                dluma_editor_theme_scintilla_retry_dialog(window, &current);
                source=GetDlgItem(window,3581);
                if (source && dluma_editor_theme_scintilla_owns_parent(window) &&
                    dluma_editor_theme_controls_attach(source))
                    dluma_editor_theme_controls_apply(source,&current);
            }
            if (dluma_editor_theme_grid_attach(window))
                dluma_editor_theme_grid_apply(window, &current);
            if (dluma_editor_theme_mfc_attach(window))
                dluma_editor_theme_mfc_apply(window, &current);
        }
    }
    (void)dluma_editor_theme_grid_message(window, stage, message);
    (void)dluma_editor_theme_mfc_message(window, stage, message);
    if (dluma_editor_theme_scintilla_message(window, stage, message, wparam, lparam, result))
        return 1;
    if (stage == DLUMA_EDITOR_UI_AFTER &&
        (message == 0x001AU || message == 0x031AU || message == 0x0015U)) {
        dluma_editor_theme_framework_settings_changed();
        (void)dluma_editor_ui_request_refresh();
    }
    return dluma_editor_theme_controls_message(
        window, stage, message, wparam, lparam, result);
}

int dluma_editor_theme_start(void)
{
    WCHAR module_path[DLUMA_MAX_WPATH], root[DLUMA_MAX_WPATH];
    WCHAR helper_path[DLUMA_MAX_WPATH];
    SIZE_T length;
    DlumaEditorThemeResolved resolved;
    if (!dluma_dual_host_runtime_is_editor()) return 0;
    if (dluma_atomic_compare_exchange(&theme.state, 1, 0)) return theme.state == 2;
    theme.preference = theme_read_preference();
    dluma_editor_theme_schedule_initialize(&theme.schedule);
    if (!dluma_get_module_path_checked(g_dluma.self_module, module_path,
                                        DLUMA_ARRAY_COUNT(module_path))) goto failed;
    length = wcslen(module_path);
    while (length && module_path[length - 1U] != L'\\' &&
           module_path[length - 1U] != L'/') --length;
    if (!length) goto failed;
    module_path[length - 1U] = 0;
    if (!dluma_checked_path_join(root, DLUMA_ARRAY_COUNT(root), module_path,
                                  L"dluma_runtime") ||
        !dluma_checked_path_join(helper_path, DLUMA_ARRAY_COUNT(helper_path), root,
                                  L"editor\\dluma_editor_theme_system.dll")) goto failed;
    theme.runtime.struct_size = sizeof(theme.runtime);
    if (!dluma_runtime_tree_acquire(root, "10.0.13", DLUMA_API_CATALOG_SHA256,
                                     &theme.runtime)) goto failed;
    theme.helper = LoadLibraryExW(helper_path, 0, 0x00000100U | 0x00000800U);
    if (!theme.helper) goto failed;
    theme.start = (ThemeStartFn)GetProcAddress(theme.helper, "dluma_editor_theme_system_start");
    theme.resolve = (ThemeResolveFn)GetProcAddress(theme.helper, "dluma_editor_theme_system_resolve");
    theme.stop = (ThemeStopFn)GetProcAddress(theme.helper, "dluma_editor_theme_system_stop");
    if (!theme.start || !theme.resolve || !theme.stop) goto failed;
    theme.system.size = sizeof(theme.system); theme.system.version = 1;
    if (!theme.start(theme_system_changed, 0, &theme.system)) goto failed;
    memset(&resolved, 0, sizeof(resolved));
    resolved.size = sizeof(resolved); resolved.version = 1;
    if (!theme.resolve(theme.preference, &theme.system, &resolved)) goto failed;
    (void)dluma_editor_theme_schedule_publish(&theme.schedule, &resolved);
    (void)dluma_editor_theme_schedule_take(&theme.schedule, &theme.current);
    dluma_atomic_exchange(&theme.state, 2);
    if (!dluma_editor_ui_subscribe_observed(DLUMA_EDITOR_UI_OWNER_THEME,
            theme_listener, dluma_editor_theme_properties_observe, 0)) goto failed;
    return 1;
failed:
    dluma_atomic_exchange(&theme.state, -1);
    if (theme.stop) theme.stop();
    if (theme.helper) { FreeLibrary(theme.helper); theme.helper = 0; }
    dluma_runtime_tree_release(&theme.runtime);
    dluma_log_line("WARN", "Editor theme initialization failed.");
    return 0;
}

int dluma_editor_theme_callbacks_drained(void)
{
    return theme.state != 2 && dluma_editor_theme_controls_drained() &&
        dluma_editor_theme_scintilla_callbacks_drained() &&
        dluma_editor_theme_grid_callbacks_drained() &&
        dluma_editor_theme_mfc_callbacks_drained() &&
        dluma_editor_theme_framework_callbacks_drained() &&
        dluma_editor_ui_unsubscribe(DLUMA_EDITOR_UI_OWNER_THEME);
}

int dluma_editor_theme_stop(void)
{
    int managed_stopped = dluma_editor_theme_framework_stop();
    int grids_stopped = dluma_editor_theme_grid_stop();
    dluma_editor_theme_controls_stop();
    dluma_editor_theme_scintilla_stop();
    if (!theme.helper) return managed_stopped && grids_stopped && dluma_editor_theme_controls_drained();
    AcquireSRWLockExclusive(&theme.lock);
    theme.state = 3;
    dluma_editor_theme_schedule_stop(&theme.schedule);
    ReleaseSRWLockExclusive(&theme.lock);
    theme.stop();
    if (!managed_stopped || !grids_stopped || !dluma_editor_theme_controls_drained() ||
        !dluma_editor_theme_scintilla_callbacks_drained() ||
        !dluma_editor_theme_grid_callbacks_drained() ||
        !dluma_editor_theme_mfc_callbacks_drained()) {
        (void)dluma_editor_ui_request_refresh();
        return 0;
    }
    if (!dluma_editor_ui_unsubscribe(DLUMA_EDITOR_UI_OWNER_THEME)) return 0;
    FreeLibrary(theme.helper); theme.helper = 0;
    dluma_runtime_tree_release(&theme.runtime);
    return 1;
}

int dluma_editor_theme_options_attach(HWND page, HWND dialog, UINT first_id)
{
    (void)dialog;
    if (theme.state != 2 || !page || theme.page) return 0;
    theme.label = CreateWindowExW(0, L"STATIC", L"Theme", 0x50000000U,
        0, 0, 1, 1, page, (HMENU)(UINT_PTR)first_id, g_dluma.self_module, 0);
    theme.combo = CreateWindowExW(0, L"COMBOBOX", L"", 0x50210003U,
        0, 0, 1, 1, page, (HMENU)(UINT_PTR)(first_id + 1U), g_dluma.self_module, 0);
    if (!theme.label || !theme.combo) {
        if (theme.label) (void)DestroyWindow(theme.label);
        if (theme.combo) (void)DestroyWindow(theme.combo);
        theme.label = 0; theme.combo = 0;
        return 0;
    }
    theme.page = page;
    (void)SendMessageW(theme.combo, 0x143, 0, (LPARAM)L"System");
    (void)SendMessageW(theme.combo, 0x143, 0, (LPARAM)L"Light");
    (void)SendMessageW(theme.combo, 0x143, 0, (LPARAM)L"Dark");
    (void)SendMessageW(theme.combo, 0x14E, theme.preference, 0);
    (void)dluma_editor_ui_request_enroll(page);
    (void)dluma_editor_ui_request_enroll(theme.label);
    (void)dluma_editor_ui_request_enroll(theme.combo);
    dluma_editor_theme_options_layout(page, 98);
    return 1;
}

void dluma_editor_theme_options_layout(HWND page, int top_dip)
{
    if (page != theme.page) return;
    dluma_editor_theme_controls_layout_options(page, theme.label, theme.combo, top_dip);
}

void dluma_editor_theme_options_detach(HWND page)
{
    if (page != theme.page) return;
    theme.page = 0; theme.label = 0; theme.combo = 0;
}
