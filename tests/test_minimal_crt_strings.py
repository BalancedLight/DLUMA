from pathlib import Path
import subprocess


def test_real_minimal_string_helpers_link_without_crt(tmp_path):
    root = Path(__file__).resolve().parents[1]
    script = tmp_path / "build.cmd"
    script.write_text(
        '@echo off\n'
        f'call "{root / "tools/setup_msvc.bat"}" || exit /b 1\n'
        'cl.exe /nologo /c /TC /MT /O2 /Oi- /W4 /WX /Gy /Zl /GS- '
        f'/Fo"{tmp_path / "util.obj"}" "{root / "src/dlw_util.c"}" || exit /b 1\n'
        'cl.exe /nologo /c /TC /MT /O2 /Oi- /W4 /WX /Gy /Zl /GS- '
        f'/Fo"{tmp_path / "test.obj"}" "{root / "tests/minimal_crt_strings_nocrt_harness.c"}" || exit /b 1\n'
        'link.exe /nologo /NODEFAULTLIB /OPT:REF /INCREMENTAL:NO /ENTRY:entry '
        '/SUBSYSTEM:CONSOLE /MACHINE:X64 '
        f'/OUT:"{tmp_path / "strings.exe"}" "{tmp_path / "util.obj"}" '
        f'"{tmp_path / "test.obj"}" kernel32.lib libvcruntime.lib libcmt.lib || exit /b 1\n'
        f'"{tmp_path / "strings.exe"}" || exit /b 1\n'
        f'dumpbin.exe /nologo /dependents "{tmp_path / "strings.exe"}" || exit /b 1\n'
    )
    result = subprocess.run(["cmd.exe", "/d", "/c", str(script)], cwd=tmp_path,
                            check=False, capture_output=True, text=True, timeout=90)
    assert result.returncode == 0, result.stdout + result.stderr
    imports = result.stdout.lower()
    assert "kernel32.dll" in imports
    assert all(name not in imports for name in ("ucrt", "msvcr", "vcruntime"))
