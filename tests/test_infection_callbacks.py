import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def test_disabled_callback_host(tmp_path: Path) -> None:
    exe = tmp_path / "infection_callbacks.exe"
    script = tmp_path / "compile.cmd"
    script.write_text(
        '@echo off\ncall tools\\setup_msvc.bat\nif errorlevel 1 exit /b 1\n'
        f'cl.exe /nologo /TC /MT /W4 /WX /D_WIN64 /Ibuild/generated /Fe"{exe}" '
        f'/Fo"{tmp_path / "test.obj"}" tests\\infection_callback_fake_harness.c '
        '/link kernel32.lib\nexit /b %errorlevel%\n', encoding="utf-8")
    built = subprocess.run(["cmd.exe", "/d", "/c", str(script)], cwd=ROOT,
                           capture_output=True, text=True, timeout=90)
    assert built.returncode == 0, built.stdout + built.stderr
    run = subprocess.run([str(exe)], capture_output=True, text=True, timeout=10)
    assert run.returncode == 0, run.stdout + run.stderr
    assert "PASS: disabled infection callback" in run.stdout
