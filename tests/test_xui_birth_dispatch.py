from pathlib import Path
import subprocess


def test_actual_shared_birth_callbacks_and_call_unwind(tmp_path):
    root = Path(__file__).resolve().parents[1]
    script = tmp_path / "build.cmd"
    script.write_text('@echo off\n'
                      f'call "{root / "tools/setup_msvc.bat"}" || exit /b 1\n'
                      f'ml64.exe /nologo /c /Fo"{tmp_path / "caller.obj"}" "{root / "tests/xui_birth_caller_masm.asm"}" || exit /b 1\n'
                      'cl.exe /nologo /TP /MT /O2 /Oi- /W4 /WX /std:c++17 /EHs-c- /GR- '
                      f'/D_HAS_EXCEPTIONS=0 /I"{root / "src"}" /Fo"{str(tmp_path).replace(chr(92), "/")}/" '
                      f'/Fe"{tmp_path / "birth.exe"}" "{root / "tests/xui_birth_dispatch_fake_harness.cpp"}" '
                      f'"{root / "src/fixes/custom_map_asset_isolation/NativeXuiInstanceOwner.cpp"}" '
                      f'/link kernel32.lib "{tmp_path / "caller.obj"}" || exit /b 1\n'
                      f'"{tmp_path / "birth.exe"}" || exit /b 1\n')
    result = subprocess.run(["cmd.exe", "/d", "/c", str(script)], cwd=tmp_path,
                            capture_output=True, text=True, timeout=90)
    assert result.returncode == 0, result.stdout + result.stderr
