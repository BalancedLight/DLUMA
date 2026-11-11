from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def compile_and_run(tmp_path: Path, sources: list[str], marker: str) -> None:
    exe = tmp_path / "harness.exe"
    script = tmp_path / "compile.cmd"
    script.write_text(
        '@echo off\ncall tools\\setup_msvc.bat\nif errorlevel 1 exit /b 1\n'
        'cl.exe /nologo /TP /MT /O2 /Oi- /W4 /WX /EHs-c- /GR- /std:c++17 '
        '/D_HAS_EXCEPTIONS=0 /D_CRT_SECURE_NO_WARNINGS /I src '
        f'/Fo"{tmp_path.as_posix()}/" /Fe"{exe}" ' + " ".join(sources) +
        ' /link /stack:16777216 kernel32.lib user32.lib\nexit /b %errorlevel%\n', encoding="utf-8")
    result = subprocess.run(["cmd.exe", "/d", "/c", str(script)], cwd=ROOT,
                            capture_output=True, text=True, timeout=90)
    assert result.returncode == 0, result.stdout + result.stderr
    result = subprocess.run([str(exe)], capture_output=True, text=True, timeout=30)
    assert result.returncode == 0, result.stdout + result.stderr
    assert marker in result.stdout


def test_inactive_respawn_detour_planner(tmp_path: Path) -> None:
    compile_and_run(tmp_path, ["tests/infection_detour_fake_harness.cpp",
        "tests/infection_entry_unwind_windows.cpp", "src/game/dlw_player_infection_detour.cpp"],
        "PASS: inactive infection detour planner")


def test_existing_renderer_unwind_delegation(tmp_path: Path) -> None:
    compile_and_run(tmp_path, ["tests/renderer_streaming_owner_fake_harness.cpp",
        "tests/renderer_relay_unwind_windows.cpp",
        "src/fixes/custom_map_asset_isolation/RendererStreamingOwner.cpp",
        "src/fixes/custom_map_asset_isolation/PackedAssetOwner.cpp",
        "src/fixes/custom_map_asset_isolation/NativeRetainedResourceOwner.cpp"], "PASS")


def test_explicit_skipped_xmm_unwind(tmp_path: Path) -> None:
    compile_and_run(tmp_path, ["tests/ui_entry_unwind_fake_harness.cpp",
        "tests/ui_entry_unwind_windows.cpp"], "PASS: bounded skipped XMM unwind")
