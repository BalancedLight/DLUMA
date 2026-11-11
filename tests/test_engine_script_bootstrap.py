from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def test_engine_script_bootstrap(tmp_path: Path) -> None:
    exe = tmp_path / "engine_bootstrap.exe"
    script = tmp_path / "compile.cmd"
    script.write_text(
        '@echo off\ncall tools\\setup_msvc.bat\nif errorlevel 1 exit /b 1\n'
        f'cl.exe /nologo /TC /MT /W4 /WX /Fe"{exe}" /Fo"{tmp_path / "test.obj"}" '
        'tests\\engine_script_bootstrap_fake_harness.c\nexit /b %errorlevel%\n', encoding="utf-8")
    result = subprocess.run(["cmd.exe", "/d", "/c", str(script)], cwd=ROOT,
                            capture_output=True, text=True, timeout=90)
    assert result.returncode == 0, result.stdout + result.stderr
    result = subprocess.run([str(exe)], capture_output=True, text=True, timeout=10)
    assert result.returncode == 0, result.stdout + result.stderr
    assert "PASS: exact engine cold publication" in result.stdout


def test_legacy_inventory_proof_delegation(tmp_path: Path) -> None:
    exe = tmp_path / "legacy_bootstrap.exe"
    script = tmp_path / "compile.cmd"
    script.write_text(
        '@echo off\ncall tools\\setup_msvc.bat\nif errorlevel 1 exit /b 1\n'
        'cl.exe /nologo /TP /MT /O2 /Oi- /W4 /WX /EHs-c- /GR- /std:c++17 '
        '/D_HAS_EXCEPTIONS=0 /D_CRT_SECURE_NO_WARNINGS /I src '
        f'/Fo"{tmp_path.as_posix()}/" /Fe"{exe}" '
        'tests/player_state_inventory_bootstrap_proof_fake_harness.cpp '
        'src/core/dlw_engine_script_bootstrap.c '
        'src/fixes/custom_map_asset_isolation/PlayerStateInventoryBootstrapProof.cpp '
        ' /link /stack:8388608 kernel32.lib user32.lib\nexit /b %errorlevel%\n', encoding="utf-8")
    result = subprocess.run(["cmd.exe", "/d", "/c", str(script)], cwd=ROOT,
                            capture_output=True, text=True, timeout=90)
    assert result.returncode == 0, result.stdout + result.stderr
    result = subprocess.run([str(exe)], capture_output=True, text=True, timeout=10)
    assert result.returncode == 0, result.stdout + result.stderr
    assert "player_state_inventory_bootstrap_proof_fake_harness: ok" in result.stdout
