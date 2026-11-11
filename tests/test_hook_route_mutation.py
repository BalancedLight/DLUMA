from pathlib import Path
import subprocess


def test_actual_hook_route_mutation(tmp_path):
    root = Path(__file__).resolve().parents[1]
    script = tmp_path / "compile.cmd"
    exe = tmp_path / "route.exe"
    script.write_text('@echo off\n'
                      f'call "{root / "tools/setup_msvc.bat"}" || exit /b 1\n'
                      'cl.exe /nologo /TP /MT /W4 /WX /EHs-c- /GR- /O2 /std:c++17 '
                      f'/D_HAS_EXCEPTIONS=0 /Fo"{tmp_path / "route.obj"}" /Fe"{exe}" '
                      f'"{root / "tests/hook_route_mutation_windows.cpp"}" /link kernel32.lib || exit /b 1\n'
                      f'"{exe}" || exit /b 1\n')
    result = subprocess.run(["cmd.exe", "/d", "/c", str(script)], cwd=tmp_path,
                            capture_output=True, text=True, timeout=90)
    assert result.returncode == 0, result.stdout + result.stderr
