from pathlib import Path
import subprocess


ROOT = Path(__file__).resolve().parents[1]


def test_orderly_close_provenance_native_harness():
    subprocess.run(
        [
            "cmd.exe",
            "/d",
            "/s",
            "/c",
            r"tools\build_orderly_close_provenance_fake_tests.bat",
        ],
        cwd=ROOT,
        check=True,
    )
    subprocess.run(
        [
            ROOT
            / "build"
            / "orderly-close-provenance-fake"
            / "orderly_close_provenance_fake_harness.exe"
        ],
        cwd=ROOT,
        check=True,
    )


def test_window_route_uses_private_delivery_and_tagged_close():
    shutdown = (ROOT / "src" / "dlw_shutdown.c").read_text(
        encoding="utf-8"
    )
    production = (
        ROOT
        / "src"
        / "fixes"
        / "custom_map_asset_isolation"
        / "CustomMapAssetIsolationFix.cpp"
    ).read_text(encoding="utf-8")
    assert "DLUMA_ORDERLY_CLOSE_DELIVERY_MESSAGE" in shutdown
    assert "DLUMA_ORDERLY_CLOSE_TAG_MAGIC" in shutdown
    assert "dluma_orderly_close_origin_name(internal.origin)" in shutdown
    assert '"user_alt_f4"' in shutdown
    assert '"system_menu_close"' in shutdown
    assert "WM_CLOSE / Alt+F4" not in shutdown
    schedule = shutdown.split(
        "int dluma_shutdown_schedule_orderly_close(const char* reason)", 1
    )[1].split("int dluma_shutdown_snapshot_orderly_close(", 1)[0]
    assert "dluma_shutdown_schedule_orderly_close_ex(" in schedule
    assert "DLUMA_CLOSE_ORIGIN_ASSET_ISOLATION_FAIL_CLOSED" in schedule

    # Failed project cleanup now presents a blocked frontend with an explicit
    # user quit action. It must not manufacture an automatic close request.
    blocked_menu = production.split("void __fastcall HookShowMenuMain(", 1)[1].split(
        "std::uintptr_t __fastcall HookSessionOnDestroy(", 1
    )[0]
    assert "dluma_frontend_error_present(" in blocked_menu
    assert "cold::NoteNoLevelFailurePresentation(" in blocked_menu
    assert "dluma_shutdown_schedule_orderly_close" not in blocked_menu


def test_frontend_error_quit_requires_user_request_before_close():
    shutdown = (ROOT / "src" / "dlw_shutdown.c").read_text(encoding="utf-8")
    window_proc = shutdown[shutdown.rindex("static LRESULT CALLBACK dluma_game_window_proc("):]
    quit_route = window_proc.split(
        "if (message == DLUMA_FRONTEND_ERROR_QUIT_MESSAGE)", 1
    )[1].split("dluma_mouse_lock_window_message_pre(", 1)[0]
    consume = quit_route.index("dluma_frontend_error_consume_user_quit(window)")
    quiesce = quit_route.index('dluma_request_quiesce("user_frontend_error_quit")')
    delegate = quit_route.index("CallWindowProcW(original, window, WM_CLOSE, 0, 0)")
    assert consume < quiesce < delegate
    assert "} else {\n            result = 0;" in quit_route

    frontend = (ROOT / "src" / "dlw_frontend_error.c").read_text(encoding="utf-8")
    user_quit = frontend.split("int dluma_frontend_error_consume_user_quit(HWND window)", 1)[1].split(
        "int dluma_frontend_error_stop(void)", 1
    )[0]
    assert "if (!error_is_owner(window)) return 0;" in user_quit
    assert "!dluma_frontend_error_blocks_project_launch()" in user_quit
    assert "!dluma_atomic_compare_exchange(&g_error_quit_requested, 0, 0)" in user_quit
    assert "dluma_atomic_compare_exchange(&g_error_quit_delivered, 1, 0) == 0" in user_quit
