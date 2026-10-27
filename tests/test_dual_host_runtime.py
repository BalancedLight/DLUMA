import subprocess
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def test_dual_host_fake_harness():
    completed = subprocess.run(
        [
            "cmd.exe",
            "/d",
            "/s",
            "/c",
            "call tools\\build_dual_host_fake_tests.bat",
        ],
        cwd=ROOT,
        check=True,
        capture_output=True,
        text=True,
        timeout=90,
    )
    assert completed.stdout.count("[PASS]") == 6
    assert "Dual-host fake cases passed: 6" in completed.stdout


def test_exact_profiles_and_forwarding_only_contract():
    source = (ROOT / "src/core/dlw_dual_host.c").read_text(encoding="utf-8")
    header = (ROOT / "src/core/dlw_dual_host.h").read_text(encoding="utf-8")
    assert "CACD9C96BA5D8C35" in source
    assert "61D9DFB20CFA179C" in source
    assert "dluma_dual_fold_equal(evidence->image.sha256, sha256)" in source
    assert "DLUMA_HANDOFF_EXPIRY_MS 120000ULL" in header
    assert "DLUMA_HANDOFF_MAX_RECORDS 32U" in header
    assert "DLUMA_HOST_LIFECYCLE_RETAINED_REJECT_REINIT" in header
    assert '"unknown executable profile"' in source
    assert "if (evidence->has_editor_debugconf)" in source
    runtime = (ROOT / "src/dlw_dual_host_runtime.c").read_text(encoding="utf-8")
    assert "dluma_host_prune_handoffs" in runtime
    assert "DLUMA_HANDOFF_MAX_RECORDS - 1U" in runtime
    assert "approval_fingerprint" in runtime
    assert "QueryFullProcessImageNameW" in runtime
    assert "parent_editor_profile_verified" in runtime
    assert "(c >= 'a' && c <= 'f')" in runtime
    exports = (ROOT / "src/dlw_exports.c").read_text(encoding="utf-8")
    editor_host = (ROOT / "src/dlw_editor_host.c").read_text(encoding="utf-8")
    assert "dluma_editor_host_current_thread_in_callback()" in exports
    assert "retained without nested teardown" in exports
    assert "DLUMA_EDITOR_CALLBACK_THREAD_CAPACITY" in editor_host
    assert "dluma_crash_reporting_start()" in editor_host
    assert "dluma_editor_host_crash_context_update" in editor_host
    assert "dluma_crash_publish_host_context" in editor_host
    assert '"chrome_editor"' in editor_host


def test_editor_playtest_selected_mod_is_the_exact_loader_file():
    launch = (ROOT / "src/dlw_editor_launch.c").read_text(encoding="utf-8")
    selected = launch.split(
        "static int dluma_editor_selected_loader_matches", 1
    )[1].split("static void dluma_editor_sort_approvals", 1)[0]
    assert '"/mod="' in selected
    assert "dluma_get_module_path_checked" in selected
    assert "FILE_FLAG_OPEN_REPARSE_POINT" in selected
    assert "selected_info.nNumberOfLinks != 1U" in selected
    assert "selected_info.dwVolumeSerialNumber" in selected
    assert "selected_info.nFileIndexHigh" in selected
    assert "selected_info.nFileIndexLow" in selected
    assert "dluma_get_final_path_checked" in selected
    assert "dluma_sha256_handle_hex" in selected
    assert "runtime->evidence.loader_sha256" in selected
    assert "dluma_open_directory_identity(mod_wide" not in selected


def test_editor_playtest_accepts_player_from_application_name():
    launch = (ROOT / "src/dlw_editor_launch.c").read_text(encoding="utf-8")
    publish = launch.split(
        "static int dluma_editor_publish_playtest", 1
    )[1].split("static BOOL WINAPI dluma_editor_create_process_hook", 1)[0]
    assert '"debugconf_editor.scr"' in publish
    assert 'command_size, "DyingLightPlayer.exe"' not in publish
    assert "dluma_editor_resolve_process_path(" in publish
    assert "application_name, command_line, command_size" in publish
    assert "dluma_editor_validate_player(" in publish


def test_editor_playtest_hooks_the_verified_shell_launcher_and_hashes_arguments():
    launch = (ROOT / "src/dlw_editor_launch.c").read_text(encoding="utf-8")
    runtime = (ROOT / "src/dlw_dual_host_runtime.c").read_text(
        encoding="utf-8"
    )
    assert "DLUMA_EDITOR_CREATE_PROCESS_IAT_RVA 0x011456D8UL" in launch
    assert "DLUMA_EDITOR_SHELL_EXECUTE_IAT_RVA 0x01146568UL" in launch
    assert 'L"shell32.dll", "SHELL32.dll", "ShellExecuteA"' in launch
    shell_hook = launch.split(
        "static HINSTANCE WINAPI dluma_editor_shell_execute_hook(", 2
    )[-1].split("static int dluma_editor_replace_iat_target", 1)[0]
    assert "dluma_editor_publish_playtest(" in shell_hook
    assert "file, parameters, directory, 1" in shell_hook
    assert shell_hook.index("dluma_editor_publish_playtest(") < shell_hook.index(
        "result = original ? original("
    )
    assert "original_shell_execute" in shell_hook
    assert "while (*arguments == L' ' || *arguments == L'\\t')" in runtime
    assert "arguments, -1" in runtime
    assert "snapshot->command_line" in runtime


def test_validated_playtest_handoff_seeds_the_authoritative_map_context():
    host = (ROOT / "src/dlw_host.c").read_text(encoding="utf-8")
    prepare = host.split(
        "static int dluma_prepare_editor_playtest_handoff", 1
    )[1].split(
        "static void dluma_activate_editor_playtest_handoff_on_worker", 1
    )[0]
    activate = host.split(
        "static void dluma_activate_editor_playtest_handoff_on_worker", 1
    )[1].split("static int dluma_is_frontend_level", 1)[0]
    worker = host.split("DWORD WINAPI dluma_worker_thread", 1)[1].split(
        "while (!g_dluma.stop_requested)", 1
    )[0]
    start = host.split("int dluma_host_start(void)", 1)[1].split(
        "int dluma_host_stop(void)", 1
    )[0]
    assert "runtime->classification.handoff_valid" in prepare
    assert "dluma_utf8_to_wide(" in prepare
    assert "dluma_open_directory_identity(project_root" in prepare
    assert "dluma_validate_map_anchor_under_project(" in prepare
    assert "dluma_engine_configure_playtest_frame_authority(" in prepare
    assert prepare.index("dluma_validate_map_anchor_under_project(") < prepare.index(
        "dluma_engine_configure_playtest_frame_authority("
    )
    assert "activation->project_identity = project_identity" in prepare
    assert "dluma_activate_map(" not in prepare
    assert "dluma_directory_identity_equal(" in activate
    assert "dluma_validate_map_anchor_under_project(" in activate
    assert "dluma_activate_map(" in activate
    assert "dluma_activate_editor_playtest_handoff_on_worker(" in worker
    assert start.index("dluma_prepare_editor_playtest_handoff(") < start.index(
        "CreateThread("
    )
    assert "playtest_activation, 0, &thread_id" in start
    assert "8UL * 1024UL * 1024UL" in start
