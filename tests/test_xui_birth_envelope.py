from pathlib import Path
import subprocess


def test_qualified_birth_call_envelope(tmp_path):
    root = Path(__file__).resolve().parents[1]
    script = tmp_path / "build.cmd"
    script.write_text('@echo off\n'
                      f'call "{root / "tools/setup_msvc.bat"}" || exit /b 1\n'
                      'cl.exe /nologo /TP /MT /O2 /Oi- /W4 /WX /std:c++17 '
                      f'/Fe"{tmp_path / "envelope.exe"}" '
                      f'"{root / "tests/xui_birth_envelope_fake_harness.cpp"}" || exit /b 1\n'
                      f'"{tmp_path / "envelope.exe"}" || exit /b 1\n')
    result = subprocess.run(["cmd.exe", "/d", "/c", str(script)], cwd=tmp_path,
                            capture_output=True, text=True, timeout=60)
    assert result.returncode == 0, result.stdout + result.stderr
