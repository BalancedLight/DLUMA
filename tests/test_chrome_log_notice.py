from __future__ import annotations

from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def source(name: str) -> str:
    return (ROOT / "src" / name).read_text(encoding="utf-8")


def test_native_notice_uses_the_exact_chrome_log_contract():
    core = source("dlw_chrome_log_core.c")
    header = source("dlw_chrome_log_core.h")
    assert "0x5741524EUL" in header
    assert "Dying Light has been modded with unofficial code created by the community." in header
    assert "These logs should not represent the stability of the core game." in header
    assert 'DLUMA_CHROME_LOG_CATEGORY "DLUMA"' in header
    assert 'result->filter_action = 2' in core
    assert 'result->line_mode = 1' in core
    assert '"%s\\n", DLUMA_CHROME_LOG_MESSAGE' in core
    assert "LogSetPrintCallback" not in source("dlw_chrome_log.c")
    assert "CrashSetPrintCallback" not in source("dlw_chrome_log.c")


def test_notice_is_profile_and_prefix_gated_before_engine_call():
    adapter = source("dlw_chrome_log.c")
    core = source("dlw_chrome_log_core.c")
    path_security = source("dlw_path_security.c")
    assert "dluma_get_final_path_checked(file, out, capacity)" in adapter
    assert "GetFinalPathNameByHandleW" in path_security
    assert "machine != 0x8664" in adapter
    assert "optional_magic != 0x20B" in adapter
    assert "dluma_chrome_same_directory(filesystem_path, engine_path)" in adapter
    assert "dluma_chrome_same_directory(filesystem_path, executable_path)" in adapter
    assert "GetProcAddress(filesystem, log_export)" in adapter
    assert "GetProcAddress(filesystem, level_export)" in adapter
    assert "dluma_chrome_target_in_image" in adapter
    assert "DLUMA_CHROME_SHIPPING_TIMESTAMP" in core
    assert "DLUMA_CHROME_DEVTOOLS_TIMESTAMP" in core
    assert "DLUMA_CHROME_LOG_CORE_PREFIX_MISMATCH" in core


def test_notice_is_one_shot_and_runs_before_third_party_startup():
    adapter = source("dlw_chrome_log.c")
    exports = source("dlw_exports.c")
    assert "g_dluma_chrome_log_notice_state" in adapter
    assert "dluma_atomic_compare_exchange" in adapter
    assert "dluma_atomic_exchange(&g_dluma_chrome_log_notice_state, 2)" in adapter
    player = exports.split(
        "static void* dluma_player_initialize(void)", 1
    )[1].split("static __int64 dluma_player_shutdown(void)", 1)[0]
    entered = player.index('"InitializeGameScriptDLL entered."')
    notice = player.index("dluma_chrome_log_notice();")
    recovery = player.index("dluma_startup_recovery_begin();")
    native = player.index("dluma_native_mods_discover();")
    original = player.index("manager = g_original_initialize();")
    assert entered < notice < recovery < native < original


def test_project_and_offline_harness_include_the_new_adapter():
    project = (ROOT / "DLUMA.vcxproj").read_text(encoding="utf-8")
    harness = (ROOT / "tests" / "chrome_log_core_fake_harness.c").read_text(
        encoding="utf-8"
    )
    build = (ROOT / "tools" / "build_chrome_log_core_fake_tests.bat").read_text(
        encoding="utf-8"
    )
    assert '<ClCompile Include="src\\dlw_chrome_log.c" />' in project
    assert '<ClCompile Include="src\\dlw_chrome_log_core.c" />' in project
    assert '<ClInclude Include="src\\dlw_chrome_log_core.h" />' in project
    assert "case_one_shot_reentry_and_concurrency" in harness
    assert "chrome_log_core_fake_harness.exe" in build
