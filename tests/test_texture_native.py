from pathlib import Path
import subprocess


def test_native_texture_adapter(tmp_path):
    root = Path(__file__).resolve().parents[1]
    script = tmp_path / "build.cmd"
    script.write_text(
        '@echo off\n'
        f'call "{root / "tools/setup_msvc.bat"}" || exit /b 1\n'
        'cl.exe /nologo /std:c++17 /MT /W4 /WX /EHs-c- /GR- /DDLUMA_PRIVATE_TEXTURE_SWAP_DIAGNOSTIC=1 '
        f'/Fe"{tmp_path / "native.exe"}" '
        f'"{root / "tests/texture_native_fake_harness.cpp"}" '
        f'"{root / "src/game/dlw_texture_swap_core.c"}" '
        f'"{root / "src/game/dlw_texture_swap_resources.c"}" || exit /b 1\n'
        f'"{tmp_path / "native.exe"}" || exit /b 1\n'
    )
    result = subprocess.run(["cmd.exe", "/d", "/c", str(script)], cwd=tmp_path,
                            check=False, capture_output=True, text=True, timeout=90)
    assert result.returncode == 0, result.stdout + result.stderr
    assert "native texture adapter passed" in result.stdout
