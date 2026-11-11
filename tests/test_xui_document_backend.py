from pathlib import Path
import subprocess


def test_document_backend_rollback_and_owner_lifetime(tmp_path):
    root = Path(__file__).resolve().parents[1]
    script = tmp_path / "build.cmd"
    script.write_text(
        '@echo off\n'
        f'call "{root / "tools/setup_msvc.bat"}" || exit /b 1\n'
        'cl.exe /nologo /TP /MT /W4 /WX /Oi- /D_CRT_SECURE_NO_WARNINGS '
        f'/Fe"{tmp_path / "document.exe"}" '
        f'"{root / "tests/xui_document_backend_fake_harness.cpp"}" '
        f'"{root / "src/game/dlw_xui_document_backend.c"}" || exit /b 1\n'
        f'"{tmp_path / "document.exe"}" || exit /b 1\n'
    )
    result = subprocess.run(["cmd.exe", "/d", "/c", str(script)], cwd=tmp_path,
                            check=False, capture_output=True, text=True, timeout=90)
    assert result.returncode == 0, result.stdout + result.stderr
    assert "XUI document backend passed" in result.stdout
