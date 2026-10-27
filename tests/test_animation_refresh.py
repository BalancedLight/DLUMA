import subprocess
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def _function_body(source: str, signature: str) -> str:
    search = 0
    while True:
        start = source.index(signature, search)
        brace = source.index("{", start)
        semicolon = source.find(";", start, brace)
        if semicolon < 0:
            break
        search = semicolon + 1
    depth = 0
    for index in range(brace, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[brace + 1 : index]
    raise AssertionError(f"unterminated function: {signature}")


def test_animation_refresh_fake_harness():
    completed = subprocess.run(
        [
            "cmd.exe",
            "/d",
            "/s",
            "/c",
            "call tools\\build_animation_refresh_fake_tests.bat",
        ],
        cwd=ROOT,
        check=True,
        capture_output=True,
        text=True,
        timeout=120,
    )
    assert completed.stdout.count("[PASS]") == 8
    assert "Animation refresh fake cases passed: 8" in completed.stdout


def test_refresh_is_separate_enabled_and_never_targets_retail_game():
    header = (ROOT / "src/core/dlw_animation_refresh.h").read_text(
        encoding="utf-8"
    )
    core = (ROOT / "src/core/dlw_animation_refresh.c").read_text(
        encoding="utf-8"
    )
    runtime = (ROOT / "src/dlw_animation_refresh_runtime.c").read_text(
        encoding="utf-8"
    )
    guard = (ROOT / "src/core/dlw_editor_model_animation_guard.c").read_text(
        encoding="utf-8"
    )
    config = (ROOT / "config/dluma.ini").read_text(
        encoding="utf-8"
    )

    assert "DLUMA_ANIMATION_REFRESH_HOST_EDITOR" in header
    assert "DLUMA_ANIMATION_REFRESH_HOST_PLAYER" in header
    assert "DyingLightGame.exe" not in header + core + runtime
    assert "AnimationRefresh" in config
    assert "Enabled=1" in config
    assert "Route=ProjectRPack" in config
    assert "dluma_editor_model_animation_guard_probe" not in core + runtime
    assert "dluma_animation_refresh" not in guard


def test_runtime_uses_selected_model_bridge_and_keeps_process_controls_out():
    runtime = (ROOT / "src/dlw_animation_refresh_runtime.c").read_text(
        encoding="utf-8"
    )
    bridge = (ROOT / "src/dlw_editor_animation_bridge.c").read_text(
        encoding="utf-8"
    )
    assert '#include "dlw_editor_animation_bridge.h"' in runtime
    assert "operations.selected_model_reload_supported = 1" in runtime
    assert runtime.count("operations.reload_selected_model =") == 2
    assert "dluma_animation_refresh_editor_selected_model" in runtime
    assert "dluma_animation_refresh_player_runtime_pack" in runtime
    assert "dluma_editor_animation_bridge_dispatch(&request, result)" in runtime
    assert "dluma_animation_pack_runtime_swap(runtime_pack, &swap)" in runtime
    assert "DLUMA_EDITOR_RELOAD_ANIMATIONS_COMMAND" not in runtime
    assert "dluma_animation_refresh_editor_reload" not in runtime
    assert "dluma_editor_exit_observer_dispatch_command" not in runtime
    assert "0x001CB3C0" not in runtime
    assert "DLUMA_ANIMATION_REFRESH_TOTAL_ARTIFACT_MAX_BYTES" in runtime
    assert "resolvedFilesTruncated" in runtime
    assert "primaryFailureStage" in runtime
    assert "fallbackUnmount" in runtime
    assert "hostKind" in runtime
    assert "exactProfile" in runtime
    assert "hostImageSha256" in runtime
    assert "hostImageTimestamp" in runtime
    assert "hostImageSizeOfImage" in runtime
    assert "canonical_image_utf8" not in runtime
    assert "dluma_animation_refresh_coalesce_state" in runtime
    assert "dluma_animation_refresh_write_coalesce_marker" in runtime
    assert "failureStage\\\":null" in runtime
    assert "return (DlumaAnimationRefreshRoute)0" in runtime
    assert "offline_validated_live_behavior_unproven" in runtime
    for source in (runtime, bridge):
        assert "CreateProcess" not in source
        assert "OpenProcess" not in source
        assert "WriteProcessMemory" not in source



def test_reload_animations_command_is_consumed_by_selected_model_bridge():
    bridge = (ROOT / "src/dlw_editor_animation_bridge.c").read_text(
        encoding="utf-8"
    )
    body = _function_body(
        bridge, "static LRESULT CALLBACK dluma_editor_animation_window_proc("
    )
    command = body.index("if (message == WM_COMMAND")
    manual = body.index(
        'dluma_editor_animation_reload_manual("refresh-menu");', command
    )
    consume = body.index("return 0;", manual)
    predecessor = body.index("CallWindowProcW(", consume)

    assert "#define DLUMA_EDITOR_ANIMATION_RELOAD_SELECTED_COMMAND 0x807EU" in bridge
    assert 'L"Reload Animations (Selected Model)"' in bridge
    assert "0x001CB3C0" not in bridge
    assert command < manual < consume < predecessor
    assert "CallWindowProcW(" not in body[command:consume]


def test_shortcut_handler_is_exact_byte_gated_and_never_falls_through():
    bridge = (ROOT / "src/dlw_editor_animation_bridge.c").read_text(
        encoding="utf-8"
    )
    profile = _function_body(
        bridge, "static int dluma_editor_animation_profile_bind("
    )
    install = _function_body(
        bridge, "static int dluma_editor_animation_install_stock_reload_detour("
    )
    hook = _function_body(
        bridge, "static void __fastcall dluma_editor_animation_stock_reload_hook("
    )

    assert (
        "#define DLUMA_EDITOR_ANIMATION_STOCK_RELOAD_HANDLER_RVA "
        "0x000B1CB0U"
    ) in bridge
    assert (
        "#define DLUMA_EDITOR_ANIMATION_STOCK_RELOAD_PATCH_SIZE 14U"
        in bridge
    )
    assert "g_dluma_editor_animation_stock_reload_prefix" in profile
    assert "DLUMA_EDITOR_ANIMATION_STOCK_RELOAD_HANDLER_RVA" in profile
    assert "g_dluma_editor_animation_main_frame_title_prefix" in profile
    assert "DLUMA_EDITOR_ANIMATION_MAIN_FRAME_TITLE_RVA" in profile
    assert "dluma_editor_animation_prefix_matches(" in install
    assert "patch[0] = 0xFF" in install
    assert "patch[1] = 0x25" in install
    assert "memcpy(patch + 6U, &target, sizeof(target))" in install
    assert 'dluma_editor_animation_reload_manual("shortcut");' in hook
    assert "CallWindowProcW" not in hook
    assert "0x001CB3C0" not in bridge

def test_native_project_wires_animation_bank_runtime_and_editor_bridge():
    project = (ROOT / "DLUMA.vcxproj").read_text(encoding="utf-8")
    expected_items = (
        r'<ClCompile Include="src\dlw_animation_pack_runtime.c" />',
        r'<ClCompile Include="src\dlw_editor_animation_bridge.c" />',
        r'<ClInclude Include="src\dlw_animation_pack_runtime.h" />',
        r'<ClInclude Include="src\dlw_editor_animation_bridge.h" />',
    )

    for item in expected_items:
        assert project.count(item) == 1


def test_selected_bank_release_and_mixer_modes_are_scoped_and_restored():
    bridge = (ROOT / "src/dlw_editor_animation_bridge.c").read_text(
        encoding="utf-8"
    )
    exclusive = _function_body(
        bridge, "static int dluma_editor_animation_bank_is_exclusive("
    )
    shutdown = _function_body(
        bridge, "static int dluma_editor_animation_shutdown_selected_bank("
    )
    init = _function_body(
        bridge, "static int dluma_editor_animation_init_with_mode("
    )

    assert "occurrences == 1U && reference_count == 1U" in exclusive
    validate = shutdown.index("occurrences != 1U || reference_count != 1U")
    enable = shutdown.index("*((unsigned char*)mixer + 9U) = 1U", validate)
    call = shutdown.index("shutdown(model);", enable)
    restore = shutdown.index(
        "*((unsigned char*)mixer + 9U) = release_mode", call
    )
    assert validate < enable < call < restore
    assert "dluma_animation_pack_runtime_shutdown" not in shutdown

    snapshot = init.index("compiled_mode = *((unsigned char*)mixer + 8U)")
    set_mode = init.index(
        "*((unsigned char*)mixer + 8U) = requested_mode", snapshot
    )
    init_call = init.index("init(model, script)", set_mode)
    restore_mode = init.index(
        "*((unsigned char*)mixer + 8U) = compiled_mode", init_call
    )
    assert snapshot < set_mode < init_call < restore_mode


def test_reload_reresolves_selection_and_stops_after_partial_shutdown():
    bridge = (ROOT / "src/dlw_editor_animation_bridge.c").read_text(
        encoding="utf-8"
    )
    reload = _function_body(
        bridge, "static int dluma_editor_animation_reload_ui("
    )
    refresh = reload.index('"filesystem_refresh"')
    reresolve = reload.index('"selected_model_reresolve"', refresh)
    init = reload.index('"model_anim_init"', reresolve)
    assert refresh < reresolve < init
    assert "shutdown_partial = shutdown_status < 0" in reload
    partial = reload.index("if (shutdown_partial)")
    rollback_failed = reload.index(
        "DLUMA_ANIMATION_REFRESH_REFUSAL_ROLLBACK_FAILED", partial
    )
    stop = reload.index("goto rollback_finished;", rollback_failed)
    restore = reload.index("dluma_editor_animation_init_with_mode(", stop)
    assert partial < rollback_failed < stop < restore


def test_profile_and_main_window_discovery_remain_retryable_and_exact():
    bridge = (ROOT / "src/dlw_editor_animation_bridge.c").read_text(
        encoding="utf-8"
    )
    profile = _function_body(
        bridge, "static int dluma_editor_animation_profile_bind("
    )
    window = _function_body(
        bridge, "static int dluma_editor_animation_main_window_matches("
    )
    assert "retryable = 1" in profile
    assert "accepted ? 2 : (retryable ? 0 : -1)" in profile
    assert "GetMenu(window)" not in window
    assert "WS_CAPTION" in window
    assert "WS_SYSMENU" in window
    assert "WS_THICKFRAME" in window
    assert "WS_EX_TOOLWINDOW" in window
    assert "GetClassNameW(" in window
    assert 'class_name, L"Afx:"' in window
    assert "GetWindowTextW(" in window
    assert 'title, L"Dying Light Developer Tools"' in window

    finder = _function_body(
        bridge, "static BOOL CALLBACK dluma_editor_animation_find_window("
    )
    assert "GetClientRect(" in finder
    assert "area > candidate->area" in finder

    popup = _function_body(
        bridge, "static void dluma_editor_animation_insert_menu_item("
    )
    assert "DLUMA_EDITOR_ANIMATION_REFRESH_FILESYSTEM_COMMAND" in popup
    assert "DLUMA_EDITOR_ANIMATION_RELOAD_SELECTED_COMMAND" in popup
    assert "return;" in popup


def test_manual_command_requires_latest_manifest_backed_active_project():
    bridge = (ROOT / "src/dlw_editor_animation_bridge.c").read_text(
        encoding="utf-8"
    )
    manual = _function_body(
        bridge, "static void dluma_editor_animation_reload_manual("
    )
    assert "dluma_editor_launch_resolve_current_project(" in manual
    assert "dluma_animation_refresh_runtime_poll_project_now(" in manual
    assert "stale_runtime_pack_requires_project_refresh" in manual
    assert "manifest_backed_request_required" in manual
    assert "request.manifest = &bridge->last_manifest" in manual
    assert "request.route = DLUMA_ANIMATION_REFRESH_ROUTE_PROJECT_RPACK" not in manual
    assert "request.manifest = 0" not in manual


def test_active_document_selects_one_project_and_stale_debugconf_cannot_override():
    launch = (ROOT / "src/dlw_editor_launch.c").read_text(encoding="utf-8")
    resolver = _function_body(
        launch, "int dluma_editor_launch_resolve_current_project("
    )

    active = resolver.index(
        "dluma_editor_animation_bridge_copy_active_document_map("
    )
    workshop = resolver.index("dluma_open_directory_identity(", active)
    scan = resolver.index("FindFirstFileW(pattern", workshop)
    anchor = resolver.index("dluma_validate_map_anchor_under_project(", scan)
    ambiguity = resolver.index("if (matches != 1U)", anchor)
    assign = resolver.index("project_root, project_capacity, selected_project", ambiguity)
    debugconf = resolver.index("dluma_editor_read_debugconf(", assign)

    assert active < workshop < scan < anchor < ambiguity < assign < debugconf
    assert "DLUMA_EDITOR_WORKSHOP_PROJECT_LIMIT" in resolver
    assert "FILE_ATTRIBUTE_REPARSE_POINT" in resolver
    assert '"active_document_project_ambiguous"' in resolver
    assert '"stale_context_ignored"' in resolver
    assert "selected_project = debug_project" not in resolver


def test_active_document_title_contract_is_exact_and_bounded():
    bridge = (ROOT / "src/dlw_editor_animation_bridge.c").read_text(
        encoding="utf-8"
    )
    title = _function_body(
        bridge,
        "int dluma_editor_animation_bridge_copy_active_document_map(",
    )
    assert (
        "#define DLUMA_EDITOR_ANIMATION_MAIN_FRAME_TITLE_RVA 0x000B23A0U"
        in bridge
    )
    assert 'static const WCHAR prefix[] = L"data/maps/"' in title
    assert 'L" - Dying Light Developer Tools"' in title
    assert 'L".map"' in title
    assert "dluma_valid_map_segment(output)" in title
    assert "dluma_stricmp(file_stem, output) != 0" in title
