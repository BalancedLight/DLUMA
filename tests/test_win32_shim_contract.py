from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
WIN32 = ROOT / "src" / "dlw_win32.h"
BASE = ROOT / "src" / "dlw_win32_base.h"
EXTENSIONS = ROOT / "src" / "dlw_win32_extensions.h"


def read(path: Path) -> str:
    return path.read_text(encoding="utf-8")


def test_win32_wrapper_reaches_base_and_extension_for_direct_includes():
    wrapper = read(WIN32)
    assert '#include "dlw_win32_base.h"' in wrapper
    assert '#include "dlw_win32_extensions.h"' in wrapper
    assert wrapper.index("dlw_win32_base.h") < wrapper.index(
        "dlw_win32_extensions.h"
    )


def test_base_retains_reset_event_and_no_crt_contract():
    source = read(BASE)
    assert "BOOL WINAPI ResetEvent(HANDLE hEvent);" in source
    assert "void* __cdecl memset(" in source
    assert "PVOID _InterlockedCompareExchangePointer(" in source


def test_win32_extension_declares_every_reported_runtime_symbol():
    source = read(EXTENSIONS)
    required = (
        "short WINAPI GetAsyncKeyState(int vKey);",
        "UINT_PTR WINAPI SetTimer(",
        "BOOL WINAPI KillTimer(",
        "HANDLE WINAPI GetCurrentProcess(void);",
        "BOOL WINAPI VirtualProtect(",
        "BOOL WINAPI FlushInstructionCache(",
        "LONG_PTR WINAPI GetWindowLongPtrW(",
        "LONG_PTR WINAPI SetWindowLongPtrW(",
        "LRESULT WINAPI CallWindowProcW(",
        "BOOL WINAPI EnumWindows(",
        "BOOL WINAPI IsWindow(HWND hWnd);",
        "BOOL WINAPI IsWindowVisible(HWND hWnd);",
        "HWND WINAPI GetWindow(HWND hWnd, unsigned int uCmd);",
        "BOOL WINAPI GetWindowRect(HWND hWnd, RECT* lpRect);",
        "DWORD WINAPI GetWindowThreadProcessId(",
        "int WINAPI MessageBoxW(",
    )
    for declaration in required:
        assert declaration in source


def test_pointer_load_is_typed_and_read_only_for_both_compilers():
    source = read(EXTENSIONS)
    assert source.count(
        "static __forceinline PVOID dluma_atomic_pointer_load("
    ) == 2
    assert "return __atomic_load_n(target, __ATOMIC_SEQ_CST);" in source
    assert "return *target;" in source


def test_direct_msvc_64_bit_intrinsics_remain_declared_without_imports():
    source = read(EXTENSIONS)
    assert "long long _InterlockedCompareExchange64(" in source
    assert "long long _InterlockedExchange64(" in source
    assert "#pragma intrinsic(_InterlockedCompareExchange64)" in source
    assert "#pragma intrinsic(_InterlockedExchange64)" in source
    assert "__declspec(dllimport) long long _Interlocked" not in source


