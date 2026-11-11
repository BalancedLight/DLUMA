from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def test_shared_detour_memory(tmp_path: Path) -> None:
    exe = tmp_path / "detour_memory.exe"
    script = tmp_path / "compile.cmd"
    script.write_text(
        '@echo off\ncall tools\\setup_msvc.bat\nif errorlevel 1 exit /b 1\n'
        'cl.exe /nologo /TP /MT /W4 /WX /EHs-c- /GR- /std:c++17 /D_HAS_EXCEPTIONS=0 '
        f'/Fe"{exe}" /Fo"{tmp_path / "test.obj"}" tests/native_detour_memory_fake_harness.cpp\nexit /b %errorlevel%\n', encoding="utf-8")
    result = subprocess.run(["cmd.exe", "/d", "/c", str(script)], cwd=ROOT,
                            capture_output=True, text=True, timeout=90)
    assert result.returncode == 0, result.stdout + result.stderr
    result = subprocess.run([str(exe)], capture_output=True, text=True, timeout=10)
    assert result.returncode == 0, result.stdout + result.stderr
    assert "PASS: shared atomic detour memory" in result.stdout
