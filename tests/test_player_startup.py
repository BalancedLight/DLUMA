from pathlib import Path
import re
import subprocess


ROOT = Path(__file__).resolve().parents[1]
RUNTIME = ROOT / "src" / "dlw_player_startup.c"
CORE = ROOT / "src" / "dlw_player_startup_core.c"
EXPORTS = ROOT / "src" / "dlw_exports.c"
PROJECT = ROOT / "DLUMA.vcxproj"


def test_player_startup_native_fake_harness():
    subprocess.run(
        [
            "cmd.exe",
            "/d",
            "/s",
            "/c",
            r"tools\build_player_startup_fake_tests.bat",
        ],
        cwd=ROOT,
        check=True,
    )
    subprocess.run(
        [
            ROOT
            / "build"
            / "player-startup-fake"
            / "player_startup_fake_harness.exe"
        ],
        cwd=ROOT,
        check=True,
    )


def test_startup_is_player_only_and_uses_exact_profiles_and_imports():
    runtime = RUNTIME.read_text(encoding="utf-8")
    exports = EXPORTS.read_text(encoding="utf-8")
    begin = exports.index("(void)dluma_player_startup_begin();")
    player = exports.index("static void* dluma_player_initialize(void)")
    editor = exports.index("static void* dluma_editor_initialize(void)")
    assert player < begin
    assert not editor < begin < player
    assert "DLUMA_HOST_PLAYER_STANDALONE" in runtime
    assert "DLUMA_HOST_PLAYER_EDITOR_PLAYTEST" in runtime
    assert "DLUMA_HOST_CHROME_EDITOR" not in runtime
    for required in (
        "DLUMA_PLAYER_STARTUP_IGAME_INITIALIZE_IAT_RVA 0x000524C8UL",
        "DLUMA_PLAYER_STARTUP_HIDE_SPLASH_IAT_RVA 0x000524E0UL",
        "DLUMA_PLAYER_STARTUP_SET_FOCUS_IAT_RVA 0x00856CD0UL",
        "DLUMA_PLAYER_STARTUP_SHOW_WINDOW_IAT_RVA 0x00856D48UL",
        '"?Initialize@IGame@@QEAAHPEADHPEAUHICON__@@1KKPEAVIProgressIndicator@@@Z"',
        '"HideSplashscreen"',
        '"ShowWindow"',
        '"SetFocus"',
        "A18CA1103614947AE7FF956C6852F45123B440563A967D9E1E496C42FA574A3C",
    ):
        assert required in runtime
    assert "dluma_player_startup_find_named_iat_slot" in runtime
    assert "dluma_player_startup_core_restore_hooks" in runtime


def test_fixed_loader_milestone_order_and_native_progress_contract():
    exports = EXPORTS.read_text(encoding="utf-8")
    runtime = RUNTIME.read_text(encoding="utf-8")
    core = CORE.read_text(encoding="utf-8")
    milestones = (
        "Initializing Mod Workbench...",
        "Discovering mods...",
        "Preparing content...",
        "Initializing game scripts...",
        "Starting mod hosts...",
    )
    positions = [core.index(f'"{message}"') for message in milestones]
    positions.append(core.index('"Initializing game..."'))
    assert positions == sorted(positions)
    export_stages = (
        "DLUMA_PLAYER_STARTUP_MILESTONE_DISCOVERY",
        "DLUMA_PLAYER_STARTUP_MILESTONE_CONTENT",
        "DLUMA_PLAYER_STARTUP_MILESTONE_SCRIPTS",
        "DLUMA_PLAYER_STARTUP_MILESTONE_HOSTS",
    )
    assert [exports.index(stage) for stage in export_stages] == sorted(
        exports.index(stage) for stage in export_stages
    )
    assert "DLUMA_PLAYER_STARTUP_MILESTONE_WORKBENCH" in runtime
    assert "DLUMA_PLAYER_STARTUP_MILESTONE_GAME" in runtime
    assert "g_dluma_player_startup_progress_vtable[7]" in runtime
    assert "dluma_player_startup_progress_update" in runtime
    assert "dluma_player_startup_core_select_reporter" in runtime
    assert "DLUMA_PLAYER_STARTUP_TTL_COPY_LIMIT" in runtime


def test_stock_splash_and_focus_safe_reveal_contract():
    runtime = RUNTIME.read_text(encoding="utf-8")
    core = CORE.read_text(encoding="utf-8")
    for required in (
        "DLUMA_PLAYER_STARTUP_SPLASH_CONTROL_ID 1002",
        "EnumThreadWindows(",
        "GetDlgItem(window, DLUMA_PLAYER_STARTUP_SPLASH_CONTROL_ID)",
        "DLUMA_SS_BITMAP",
        "DLUMA_WM_PAINT",
        "set_bk_mode(dc, DLUMA_TRANSPARENT)",
        "set_text_color(dc, 0x00FFFFFFUL)",
        "DLUMA_SW_SHOWNOACTIVATE",
        "GetForegroundWindow()",
        "GetWindowThreadProcessId(",
        "dluma_player_startup_restore_engine_hooks();",
    ):
        assert required in runtime
    assert '"techland_game_class"' in core
    restore = runtime.index("(void)dluma_player_startup_restore_engine_hooks();")
    reveal = runtime.index("dluma_player_startup_reveal_or_fail(result);")
    assert restore < reveal
    assert "SetForegroundWindow" not in runtime
    assert "__declspec(dllimport) HGDIOBJ WINAPI GetStockObject" not in runtime
    assert "gdi32.lib" not in PROJECT.read_text(encoding="utf-8")


def test_project_diagnostics_and_repository_hygiene_contract():
    project = PROJECT.read_text(encoding="utf-8")
    bridge = (ROOT / "src" / "dlw_analysis_bridge.c").read_text(
        encoding="utf-8"
    )
    for include in (
        r'<ClCompile Include="src\dlw_player_startup.c" />',
        r'<ClCompile Include="src\dlw_player_startup_core.c" />',
        r'<ClInclude Include="src\dlw_player_startup.h" />',
        r'<ClInclude Include="src\dlw_player_startup_core.h" />',
    ):
        assert include in project
    assert "DLUMA_ANALYSIS_BRIDGE_COMMAND_PLAYER_STARTUP 6U" in bridge
    assert '\\"name\\":\\"player_startup\\",\\"effect\\":false' in bridge
    public_sources = "\n".join(
        path.read_text(encoding="utf-8", errors="ignore")
        for path in (
            RUNTIME,
            CORE,
            EXPORTS,
            ROOT / "src" / "dlw_player_startup.h",
            ROOT / "src" / "dlw_player_startup_core.h",
        )
    )
    assert re.search(r"(?i)\b[a-z]:\\", public_sources) is None


def test_explicit_offline_build_mode_disables_local_deployment():
    helper = (
        ROOT
        / "build_windows_msvc.bat"
    ).read_text(encoding="utf-8")
    assert 'if /I "%~2"=="Offline"' in helper
    assert "/p:DlumaAutoDeployLocal=false" in helper
