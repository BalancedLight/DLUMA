from pathlib import Path
import subprocess


def test_texture_swap_transaction(tmp_path):
    root = Path(__file__).resolve().parents[1]
    script = tmp_path / "build.cmd"
    script.write_text(
        '@echo off\n'
        f'call "{root / "tools/setup_msvc.bat"}" || exit /b 1\n'
        'cl.exe /nologo /TC /MT /W4 /WX /Oi- /D_CRT_SECURE_NO_WARNINGS '
        f'/Fe"{tmp_path / "texture_swap.exe"}" '
        f'"{root / "tests/texture_swap_fake_harness.c"}" '
        f'"{root / "src/game/dlw_texture_swap_core.c"}" || exit /b 1\n'
        f'"{tmp_path / "texture_swap.exe"}" || exit /b 1\n'
        'dumpbin /symbols dlw_texture_swap_core.obj > core-symbols.txt\n'
    )
    result = subprocess.run(["cmd.exe", "/d", "/c", str(script)], cwd=tmp_path,
                            check=False, capture_output=True, text=True, timeout=90)
    assert result.returncode == 0, result.stdout + result.stderr
    assert "texture swap core passed" in result.stdout
    symbols = (tmp_path / "core-symbols.txt").read_text()
    assert "UNDEF  notype ()    External     | strcmp" not in symbols
    assert "_fdclass" not in symbols
