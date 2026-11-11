from pathlib import Path
import subprocess


def test_worker_map_destruction_and_native_thread_boundary(tmp_path):
    root = Path(__file__).resolve().parents[1]
    script = tmp_path / "build.cmd"
    script.write_text(
        '@echo off\n'
        f'call "{root / "tools/setup_msvc.bat"}" || exit /b 1\n'
        'cl.exe /nologo /TP /MT /W4 /WX /Oi- /D_CRT_SECURE_NO_WARNINGS '
        f'/Fe"{tmp_path / "service.exe"}" '
        f'"{root / "tests/xui_document_service_fake_harness.cpp"}" '
        f'"{root / "src/game/dlw_xui_document_backend.c"}" '
        f'"{root / "src/game/dlw_xui_document_sources.cpp"}" '
        f'"{root / "src/game/dlw_xui_document_service.cpp"}" || exit /b 1\n'
        f'"{tmp_path / "service.exe"}" || exit /b 1\n'
    )
    result = subprocess.run(["cmd.exe", "/d", "/c", str(script)], cwd=tmp_path,
                            check=False, capture_output=True, text=True, timeout=90)
    assert result.returncode == 0, result.stdout + result.stderr
    assert "XUI document service passed" in result.stdout
