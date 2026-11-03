#define WIN32_LEAN_AND_MEAN
#define DLUMA_EDITOR_THEME_SYSTEM_EXPORTS
#include <windows.h>
#include <roapi.h>
#include <windows.ui.viewmanagement.h>
#include <wrl.h>
#include "../../src/core/dlw_editor_theme_core.h"

using namespace Microsoft::WRL;
using namespace ABI::Windows::UI::ViewManagement;
using ABI::Windows::UI::Color;
typedef ABI::Windows::Foundation::__FITypedEventHandler_2_Windows__CUI__CViewManagement__CUISettings_IInspectable_t ThemeHandler;

namespace {
SRWLOCK lock = SRWLOCK_INIT;
CONDITION_VARIABLE event_drained = CONDITION_VARIABLE_INIT;
unsigned int event_callbacks = 0;
HANDLE worker = nullptr, ready = nullptr;
DWORD worker_id = 0;
HWND watcher = nullptr;
DlumaEditorThemeSystemState current = {};
DlumaEditorThemeSystemChangedFn changed = nullptr;
void *changed_context = nullptr;
bool stopping = false, initialized = false;
const UINT update_message = WM_APP + 13;

uint32_t argb(COLORREF c) { return 0xff000000u | (uint32_t(GetRValue(c)) << 16) | (uint32_t(GetGValue(c)) << 8) | uint32_t(GetBValue(c)); }
void sample(IUISettings3 *settings, DlumaEditorThemeSystemState &s) {
    HIGHCONTRASTW hc = {}; hc.cbSize = sizeof(hc);
    s = {}; s.size = sizeof(s); s.version = 1;
    s.high_contrast = SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(hc), &hc, 0) && (hc.dwFlags & HCF_HIGHCONTRASTON) ? 1u : 0u;
    Color fg = {};
    if (settings && SUCCEEDED(settings->GetColorValue(UIColorType_Foreground, &fg))) {
        s.apps_mode_available = 1; s.apps_dark = (5u * fg.G + 2u * fg.R + fg.B) > 8u * 128u ? 1u : 0u;
    }
    const int colors[17] = {COLOR_WINDOW, COLOR_BTNFACE, COLOR_WINDOW, COLOR_WINDOW, COLOR_WINDOWTEXT, COLOR_BTNTEXT, COLOR_WINDOWTEXT, COLOR_GRAYTEXT, COLOR_WINDOWFRAME, COLOR_HIGHLIGHT, COLOR_HIGHLIGHT, COLOR_HIGHLIGHT, COLOR_HIGHLIGHTTEXT, COLOR_HIGHLIGHT, COLOR_WINDOWTEXT, COLOR_WINDOWTEXT, COLOR_WINDOWTEXT};
    for (unsigned int i = 0; i < 17; ++i) s.system_argb[i] = argb(GetSysColor(colors[i]));
}
IUISettings3 *thread_settings = nullptr;
void refresh() {
    DlumaEditorThemeSystemState s; sample(thread_settings, s);
    DlumaEditorThemeSystemChangedFn callback; void *context;
    AcquireSRWLockExclusive(&lock);
    if (stopping) { ReleaseSRWLockExclusive(&lock); return; }
    s.generation = current.generation + 1; current = s; callback = changed; context = changed_context;
    ReleaseSRWLockExclusive(&lock);
    if (callback) callback(context, &s);
}
LRESULT CALLBACK window_proc(HWND w, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == update_message || msg == WM_SETTINGCHANGE || msg == WM_THEMECHANGED || msg == WM_SYSCOLORCHANGE) { refresh(); return 0; }
    if (msg == WM_CLOSE) { AcquireSRWLockExclusive(&lock); watcher = nullptr; ReleaseSRWLockExclusive(&lock); DestroyWindow(w); PostQuitMessage(0); return 0; }
    return DefWindowProcW(w, msg, wp, lp);
}
DWORD WINAPI run(void *) {
    HRESULT hr = RoInitialize(RO_INIT_MULTITHREADED);
    bool ro_initialized = SUCCEEDED(hr);
    ComPtr<IInspectable> instance; ComPtr<IUISettings3> settings;
    ComPtr<ThemeHandler> handler; EventRegistrationToken token = {}; bool subscribed = false;
    HSTRING name = nullptr;
    if (ro_initialized && SUCCEEDED(WindowsCreateString(RuntimeClass_Windows_UI_ViewManagement_UISettings, (UINT32)wcslen(RuntimeClass_Windows_UI_ViewManagement_UISettings), &name))) {
        hr = RoActivateInstance(name, &instance);
        if (SUCCEEDED(hr)) hr = instance.As(&settings);
        WindowsDeleteString(name);
        if (SUCCEEDED(hr) && settings) {
            handler = Callback<ThemeHandler>([](IUISettings *, IInspectable *) -> HRESULT {
                AcquireSRWLockExclusive(&lock); ++event_callbacks;
                if (!stopping && watcher) PostMessageW(watcher, update_message, 0, 0);
                ReleaseSRWLockExclusive(&lock);
                AcquireSRWLockExclusive(&lock); --event_callbacks; if (!event_callbacks) WakeAllConditionVariable(&event_drained); ReleaseSRWLockExclusive(&lock);
                return S_OK;
            });
            if (handler) subscribed = SUCCEEDED(settings->add_ColorValuesChanged(handler.Get(), &token));
        }
    }
    thread_settings = settings.Get();
    WNDCLASSW wc = {}; wc.lpfnWndProc = window_proc; wc.hInstance = GetModuleHandleW(nullptr); wc.lpszClassName = L"DlumaEditorThemeWatcher";
    ATOM registered = RegisterClassW(&wc);
    HWND window = registered ? CreateWindowExW(0, wc.lpszClassName, L"", WS_POPUP, 0, 0, 0, 0, nullptr, nullptr, wc.hInstance, nullptr) : nullptr;
    DlumaEditorThemeSystemState initial; sample(settings.Get(), initial); initial.generation = 1;
    AcquireSRWLockExclusive(&lock); watcher = window; current = initial; initialized = window != nullptr && !stopping; bool quit = stopping; ReleaseSRWLockExclusive(&lock);
    SetEvent(ready);
    if (window && quit) DestroyWindow(window);
    if (window && !quit) { MSG msg; while (GetMessageW(&msg, nullptr, 0, 0) > 0) { TranslateMessage(&msg); DispatchMessageW(&msg); } }
    if (subscribed) (void)settings->remove_ColorValuesChanged(token);
    AcquireSRWLockExclusive(&lock); while (event_callbacks) SleepConditionVariableSRW(&event_drained, &lock, INFINITE, 0); ReleaseSRWLockExclusive(&lock);
    handler.Reset(); thread_settings = nullptr; settings.Reset(); instance.Reset();
    if (registered) UnregisterClassW(wc.lpszClassName, wc.hInstance);
    AcquireSRWLockExclusive(&lock); watcher = nullptr; initialized = false; ReleaseSRWLockExclusive(&lock);
    if (ro_initialized) RoUninitialize();
    return 0;
}
}

extern "C" __declspec(dllexport) int dluma_editor_theme_system_start(DlumaEditorThemeSystemChangedFn callback, void *context, DlumaEditorThemeSystemState *initial) {
    if (!callback || !initial || initial->size != sizeof(*initial) || initial->version != 1) return 0;
    // A WinRT event source can retain an invocation snapshot past removal.
    // Keep its small internal handler available; client callbacks are confined
    // to the watcher thread, which stop joins before returning.
    HMODULE pinned = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
        reinterpret_cast<LPCWSTR>(reinterpret_cast<ULONG_PTR>(&dluma_editor_theme_system_start)), &pinned)) return 0;
    AcquireSRWLockExclusive(&lock);
    if (worker) { ReleaseSRWLockExclusive(&lock); return 0; }
    ready = CreateEventW(nullptr, TRUE, FALSE, nullptr); changed = callback; changed_context = context; stopping = false;
    if (ready) worker = CreateThread(nullptr, 0, run, nullptr, 0, &worker_id);
    HANDLE thread = worker, signal = ready;
    ReleaseSRWLockExclusive(&lock);
    if (!thread) { if (signal) CloseHandle(signal); AcquireSRWLockExclusive(&lock); ready = nullptr; ReleaseSRWLockExclusive(&lock); return 0; }
    WaitForSingleObject(signal, INFINITE);
    AcquireSRWLockShared(&lock); bool ok = initialized; if (ok) *initial = current; ReleaseSRWLockShared(&lock);
    if (!ok) dluma_editor_theme_system_stop();
    return ok ? 1 : 0;
}
extern "C" __declspec(dllexport) int dluma_editor_theme_system_read(DlumaEditorThemeSystemState *out) {
    if (!out || out->size != sizeof(*out) || out->version != 1) return 0;
    AcquireSRWLockShared(&lock); bool ok = initialized && !stopping; if (ok) *out = current; ReleaseSRWLockShared(&lock); return ok ? 1 : 0;
}
extern "C" __declspec(dllexport) int dluma_editor_theme_system_resolve(unsigned int p, const DlumaEditorThemeSystemState *s, DlumaEditorThemeResolved *out) { return dluma_editor_theme_core_resolve(p, s, out); }
extern "C" __declspec(dllexport) void dluma_editor_theme_system_stop(void) {
    AcquireSRWLockExclusive(&lock); HANDLE thread = worker; HWND window = watcher; DWORD id = worker_id; stopping = true; ReleaseSRWLockExclusive(&lock);
    if (!thread) return;
    if (window) PostMessageW(window, WM_CLOSE, 0, 0);
    if (GetCurrentThreadId() == id) return;
    WaitForSingleObject(thread, INFINITE);
    AcquireSRWLockExclusive(&lock);
    if (worker == thread) { CloseHandle(worker); CloseHandle(ready); worker = ready = nullptr; worker_id = 0; changed = nullptr; changed_context = nullptr; }
    ReleaseSRWLockExclusive(&lock);
}
