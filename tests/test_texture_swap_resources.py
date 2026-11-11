from pathlib import Path
import subprocess

def test_texture_resource_transaction(tmp_path):
    root = Path(__file__).resolve().parents[1]
    script = tmp_path / "build.cmd"
    script.write_text(
        '@echo off\n'
        f'call "{root / "tools/setup_msvc.bat"}" || exit /b 1\n'
        'cl.exe /nologo /TC /MT /W4 /WX /Oi- /D_CRT_SECURE_NO_WARNINGS '
        f'/Fe"{tmp_path / "resources.exe"}" '
        f'"{root / "tests/texture_swap_resources_fake_harness.c"}" '
        f'"{root / "src/game/dlw_texture_swap_core.c"}" '
        f'"{root / "src/game/dlw_texture_swap_resources.c"}" || exit /b 1\n'
        f'"{tmp_path / "resources.exe"}" || exit /b 1\n'
    )
    result = subprocess.run(["cmd.exe", "/d", "/c", str(script)], cwd=tmp_path,
                            check=False, capture_output=True, text=True, timeout=90)
    assert result.returncode == 0, result.stdout + result.stderr
    assert "texture resource transaction passed" in result.stdout
