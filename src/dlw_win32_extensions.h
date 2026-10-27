#ifndef DLUMA_WIN32_EXTENSIONS_H
#define DLUMA_WIN32_EXTENSIONS_H

/* Typed Win32 declarations and read-only pointer loads added after the original
 * no-CRT shim snapshot. Keep this header in the common include chain so C and
 * C++ translation units never fall back to implicit-int declarations. */
#ifdef __cplusplus
extern "C" {
#endif

__declspec(dllimport) short WINAPI GetAsyncKeyState(int vKey);
__declspec(dllimport) UINT_PTR WINAPI SetTimer(
    HWND hWnd, UINT_PTR nIDEvent, unsigned int uElapse, TIMERPROC lpTimerFunc);
__declspec(dllimport) BOOL WINAPI KillTimer(HWND hWnd, UINT_PTR uIDEvent);
__declspec(dllimport) HANDLE WINAPI GetCurrentProcess(void);
__declspec(dllimport) HANDLE WINAPI ReOpenFile(
    HANDLE original_file, DWORD desired_access, DWORD share_mode,
    DWORD flags_and_attributes);
__declspec(dllimport) LPVOID WINAPI VirtualAlloc(
    LPVOID address, SIZE_T size, DWORD allocation_type, DWORD protect);
__declspec(dllimport) BOOL WINAPI VirtualFree(
    LPVOID address, SIZE_T size, DWORD free_type);
__declspec(dllimport) BOOL WINAPI VirtualProtect(
    LPVOID address, SIZE_T size, DWORD new_protect, DWORD* old_protect);
__declspec(dllimport) BOOL WINAPI FlushInstructionCache(
    HANDLE process, LPCVOID base_address, SIZE_T size);
__declspec(dllimport) LONG_PTR WINAPI GetWindowLongPtrW(HWND hWnd, int nIndex);
__declspec(dllimport) LONG_PTR WINAPI SetWindowLongPtrW(
    HWND hWnd, int nIndex, LONG_PTR dwNewLong);
__declspec(dllimport) LRESULT WINAPI CallWindowProcW(
    WNDPROC lpPrevWndFunc, HWND hWnd, unsigned int Msg,
    WPARAM wParam, LPARAM lParam);
__declspec(dllimport) LRESULT WINAPI SendMessageTimeoutW(
    HWND hWnd, unsigned int Msg, WPARAM wParam, LPARAM lParam,
    unsigned int fuFlags, unsigned int uTimeout,
    ULONG_PTR* lpdwResult);
__declspec(dllimport) LRESULT WINAPI SendMessageA(
    HWND hWnd, unsigned int Msg, WPARAM wParam, LPARAM lParam);
__declspec(dllimport) int WINAPI GetMenuItemCount(HMENU hMenu);
__declspec(dllimport) HMENU WINAPI GetMenu(HWND hWnd);
__declspec(dllimport) HMENU WINAPI GetSubMenu(HMENU hMenu, int nPos);
__declspec(dllimport) unsigned int WINAPI GetMenuItemID(
    HMENU hMenu, int nPos);
__declspec(dllimport) BOOL WINAPI InsertMenuW(
    HMENU hMenu, unsigned int uPosition, unsigned int uFlags,
    UINT_PTR uIDNewItem, LPCWSTR lpNewItem);
__declspec(dllimport) BOOL WINAPI DrawMenuBar(HWND hWnd);
__declspec(dllimport) VOID WINAPI SetLastError(DWORD dwErrCode);
__declspec(dllimport) BOOL WINAPI EnumWindows(
    WNDENUMPROC lpEnumFunc, LPARAM lParam);
__declspec(dllimport) BOOL WINAPI IsWindow(HWND hWnd);
__declspec(dllimport) BOOL WINAPI IsWindowVisible(HWND hWnd);
__declspec(dllimport) HWND WINAPI GetWindow(HWND hWnd, unsigned int uCmd);
__declspec(dllimport) int WINAPI GetWindowTextW(
    HWND hWnd, LPWSTR lpString, int nMaxCount);
__declspec(dllimport) int WINAPI GetClassNameW(
    HWND hWnd, LPWSTR lpClassName, int nMaxCount);
__declspec(dllimport) BOOL WINAPI GetWindowRect(HWND hWnd, RECT* lpRect);
__declspec(dllimport) DWORD WINAPI GetWindowThreadProcessId(
    HWND hWnd, DWORD* lpdwProcessId);
__declspec(dllimport) HWND WINAPI GetForegroundWindow(void);
__declspec(dllimport) BOOL WINAPI IsIconic(HWND hWnd);
__declspec(dllimport) BOOL WINAPI GetClientRect(
    HWND hWnd, RECT* lpRect);
__declspec(dllimport) BOOL WINAPI ClientToScreen(
    HWND hWnd, POINT* lpPoint);
__declspec(dllimport) BOOL WINAPI GetClipCursor(
    RECT* lpRect);
__declspec(dllimport) BOOL WINAPI ClipCursor(
    const RECT* lpRect);
__declspec(dllimport) int WINAPI MessageBoxW(
    HWND hWnd, LPCWSTR lpText, LPCWSTR lpCaption, unsigned int uType);

#if defined(_MSC_VER) && !defined(__clang__)
/* Some no-CRT C++ fixes use the 64-bit intrinsics directly. They remain
 * compiler intrinsics and must not become KERNEL32 imports. */
long long _InterlockedCompareExchange64(
    volatile long long* destination,
    long long exchange,
    long long comparand);
long long _InterlockedExchange64(
    volatile long long* target,
    long long value);
#pragma intrinsic(_InterlockedCompareExchange64)
#pragma intrinsic(_InterlockedExchange64)
#endif

/* A pointer observation must remain a read-only operation. Do not implement
 * this with compare-exchange against engine vtables: CMPXCHG writes the cache
 * line and can fault on read-only pages even when the value is unchanged. */
#if defined(__clang__)
static __forceinline PVOID dluma_atomic_pointer_load(PVOID volatile* target)
{
    return __atomic_load_n(target, __ATOMIC_SEQ_CST);
}
#elif defined(_MSC_VER)
static __forceinline PVOID dluma_atomic_pointer_load(PVOID volatile* target)
{
    return *target;
}
#else
#error Unsupported compiler for DLUMA pointer loads.
#endif

#ifdef __cplusplus
}
#endif

#endif
