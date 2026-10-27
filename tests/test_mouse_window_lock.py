from pathlib import Path
import re
import subprocess


ROOT = Path(__file__).resolve().parents[1]
ADAPTER = ROOT / "src" / "dlw_mouse_lock.c"
CORE = ROOT / "src" / "dlw_mouse_lock_core.c"
ENGINE = ROOT / "src" / "dlw_engine.c"
SHUTDOWN = ROOT / "src" / "dlw_shutdown.c"
HOST = ROOT / "src" / "dlw_host.c"
COMMON = ROOT / "src" / "dlw_common.h"
UI = ROOT / "src" / "dlw_ui.c"
INI = (
    ROOT
    / "config"
    / "dluma.ini"
)


def test_mouse_window_lock_native_harness():
    subprocess.run(
        [
            "cmd.exe",
            "/d",
            "/s",
            "/c",
            r"tools\build_mouse_window_lock_fake_tests.bat",
        ],
        cwd=ROOT,
        check=True,
    )
    subprocess.run(
        [
            ROOT
            / "build"
            / "mouse-window-lock-fake"
            / "mouse_window_lock_fake_harness.exe"
        ],
        cwd=ROOT,
        check=True,
    )


def test_win32_adapter_is_foreground_only_and_has_no_new_callback_owner():
    source = ADAPTER.read_text(encoding="utf-8")
    core = CORE.read_text(encoding="utf-8")
    for required in (
        "GetForegroundWindow()",
        "IsIconic((HWND)window)",
        "GetClientRect((HWND)window",
        "ClientToScreen((HWND)window",
        "GetClipCursor(&rect)",
        "ClipCursor(&native_rect)",
        "ClipCursor(0)",
    ):
        assert required in source
    for forbidden in (
        "SetForegroundWindow",
        "SetCursorPos",
        "SDL_",
        "CreateThread",
        "SetTimer",
        "SetWindowLongPtr",
    ):
        assert forbidden not in source
    assert "GetForegroundWindow" not in core
    assert "ClipCursor" not in core


def test_frame_reassertion_runs_after_techland_original():
    source = ENGINE.read_text(encoding="utf-8")
    hook = source[
        source.index("static ULONG_PTR __fastcall dluma_game_frame_hook(") :
        source.index("static void __fastcall dluma_i_level_destructor_hook")
    ]
    original = hook.index(
        "result = original(game_subobject, gesture_manager, delta_seconds);"
    )
    lock = hook.index("dluma_mouse_lock_post_game_frame();")
    bookkeeping = hook.index(
        "dluma_game_frame_authority_note_post_original("
    )
    assert original < lock < bookkeeping


def test_window_messages_release_before_delegate_and_refresh_after():
    source = SHUTDOWN.read_text(encoding="utf-8")
    window_proc = source[
        source.rindex("static LRESULT CALLBACK dluma_game_window_proc(") :
        source.rindex("static void dluma_shutdown_restore_window_hook")
    ]
    pre = window_proc.index("dluma_mouse_lock_window_message_pre(")
    # The frontend error panel has an earlier, separately gated user-quit
    # delegation. Check the ordinary message route's original call precisely.
    delegate = window_proc.index(
        "CallWindowProcW(original, window, message, wparam, lparam)"
    )
    post = window_proc.index("dluma_mouse_lock_window_message_post(")
    assert pre < delegate < post

    adapter = ADAPTER.read_text(encoding="utf-8")
    for message in (
        "WM_ACTIVATEAPP",
        "WM_ACTIVATE",
        "WM_KILLFOCUS",
        "WM_SIZE",
        "WM_DESTROY",
        "WM_NCDESTROY",
        "WM_NCLBUTTONDOWN",
        "WM_SETFOCUS",
        "WM_MOVE",
        "WM_WINDOWPOSCHANGED",
        "WM_DISPLAYCHANGE",
        "WM_DPICHANGED",
        "WM_ENTERSIZEMOVE",
        "WM_EXITSIZEMOVE",
    ):
        assert message in adapter
    assert "SIZE_MINIMIZED" in adapter
    assert "WA_INACTIVE" in adapter
    assert "HTCAPTION" in adapter


def test_interactive_window_move_suspends_reassertion_until_exit():
    adapter = ADAPTER.read_text(encoding="utf-8")
    core = CORE.read_text(encoding="utf-8")
    pre = adapter[
        adapter.index("void dluma_mouse_lock_window_message_pre(") :
        adapter.index("void dluma_mouse_lock_window_message_post(")
    ]
    post = adapter[
        adapter.index("void dluma_mouse_lock_window_message_post(") :
        adapter.index("void dluma_mouse_lock_post_game_frame(void)")
    ]
    assert pre.index("dluma_mouse_lock_is_interactive_move_start(") < pre.index(
        "dluma_mouse_lock_is_deactivation_message("
    )
    assert "dluma_mouse_lock_suspend_locked();" in pre
    assert "if (message == WM_EXITSIZEMOVE)" in post
    assert post.index("dluma_mouse_lock_core_resume(") < post.index(
        "dluma_mouse_lock_maintain_locked();"
    )
    assert "state->suspended = 1;" in core
    assert "state->suspended = 0;" in core
    maintain = core[
        core.index("DlumaMouseLockCoreResult dluma_mouse_lock_core_maintain(") :
        core.index("DlumaMouseLockCoreResult dluma_mouse_lock_core_quiesce(")
    ]
    assert "state->suspended" in maintain


def test_shutdown_releases_before_hook_detach_and_resets_after_drain():
    source = SHUTDOWN.read_text(encoding="utf-8")
    quiesce = source[
        source.index("static void dluma_request_quiesce_impl(") :
        source.index("void dluma_request_quiesce(")
    ]
    assert quiesce.index("dluma_mouse_lock_quiesce();") < quiesce.index(
        "dluma_engine_quiesce(&g_dluma.engine);"
    )
    assert quiesce.index("dluma_mouse_lock_quiesce();") < quiesce.index(
        "dluma_shutdown_restore_window_hook();"
    )
    stop = source[
        source.index("int dluma_shutdown_hooks_stop(void)") :
    ]
    assert stop.index("g_dluma_window_active_calls") < stop.index(
        "dluma_mouse_lock_reset();"
    )


def test_setting_defaults_on_and_is_exposed_as_seventh_game_fix():
    host = HOST.read_text(encoding="utf-8")
    common = COMMON.read_text(encoding="utf-8")
    ui = UI.read_text(encoding="utf-8")
    sample = INI.read_text(encoding="utf-8")
    assert "unsigned char mouse_window_lock_enabled;" in common
    assert re.search(
        r'L"GameFixes",\s*L"MouseWindowLock",\s*1', host
    )
    assert "mouse_window_lock_enabled" in host
    row = re.search(
        r'\{L"GameFixes",\s*L"MouseWindowLock",\s*L"1"[\s\S]*?\},',
        ui,
    )
    assert row is not None
    assert "DLUMA_LOADER_SETTING_BOOL" in row.group(0)
    assert "DLUMA_LOADER_CATEGORY_GAME_FIXES" in row.group(0)
    table = ui[
        ui.index(
            "static const DlumaLoaderSettingDefinition g_dluma_loader_settings"
        ) :
        ui.index("static DWORD dluma_ui_read_u32")
    ]
    assert table.count("DLUMA_LOADER_CATEGORY_GAME_FIXES") == 7
    assert "MouseWindowLock=1" in sample
    assert "dluma_mouse_lock" not in ui
    shutdown = SHUTDOWN.read_text(encoding="utf-8")
    startup = shutdown[
        shutdown.index("int dluma_shutdown_hooks_start(void)") :
        shutdown.index("void dluma_shutdown_try_bind_window(void)")
    ]
    assert "g_dluma.config.mouse_window_lock_enabled" in startup


def test_mouse_window_lock_localization_exists_in_all_catalogs():
    catalogs = sorted(
        (ROOT / "PakAssets" / "Locale").glob(
            "*/loader_texts_all.scr"
        )
    )
    assert len(catalogs) == 17
    keys = (
        "DLUMA_Settings_GameFixes_MouseWindowLock_Label",
        "DLUMA_Settings_GameFixes_MouseWindowLock_Hint",
    )
    for catalog in catalogs:
        text = catalog.read_text(encoding="utf-8")
        for key in keys:
            assert text.count(f'String("{key}",') == 1, catalog


def test_explicit_build_manifest_and_win32_surface_are_preserved():
    project = (ROOT / "DLUMA.vcxproj").read_text(
        encoding="utf-8"
    )
    win32_base = (ROOT / "src" / "dlw_win32_base.h").read_text(
        encoding="utf-8"
    )
    win32_extensions = (
        ROOT / "src" / "dlw_win32_extensions.h"
    ).read_text(encoding="utf-8")
    assert r'<ClCompile Include="src\dlw_mouse_lock.c" />' in project
    assert r'<ClCompile Include="src\dlw_mouse_lock_core.c" />' in project
    assert "typedef struct tagPOINT" in win32_base
    assert "typedef struct tagRECT" in win32_base
    for declaration in (
        "HWND WINAPI GetForegroundWindow(void);",
        "BOOL WINAPI IsIconic(HWND hWnd);",
        "BOOL WINAPI GetClientRect(",
        "BOOL WINAPI ClientToScreen(",
        "BOOL WINAPI GetClipCursor(",
        "BOOL WINAPI ClipCursor(",
    ):
        assert declaration in win32_extensions
