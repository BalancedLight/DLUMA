import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def test_character_state_store_real_files(tmp_path: Path) -> None:
    exe = tmp_path / "store.exe"
    script = tmp_path / "compile.cmd"
    script.write_text(
        '@echo off\ncall tools\\setup_msvc.bat\nif errorlevel 1 exit /b 1\n'
        f'cl.exe /nologo /TC /MT /W4 /WX /D_CRT_SECURE_NO_WARNINGS /Fe"{exe}" '
        f'/Fo"{tmp_path}/" tests\\character_state_store_harness.c '
        'src\\game\\dlw_character_state_store.c src\\dlw_sha256.c\nexit /b %errorlevel%\n',
        encoding="utf-8")
    build = subprocess.run(["cmd.exe", "/d", "/c", str(script)], cwd=ROOT,
                           capture_output=True, text=True, timeout=90)
    assert build.returncode == 0, build.stdout + build.stderr
    run = subprocess.run([str(exe), str(tmp_path / "journal.bin")],
                         capture_output=True, text=True, timeout=10)
    assert run.returncode == 0, run.stdout + run.stderr
    assert "PASS: durable character store" in run.stdout
