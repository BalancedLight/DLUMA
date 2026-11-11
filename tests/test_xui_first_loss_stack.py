from pathlib import Path
import subprocess


def test_first_loss_captures_bounded_real_windows_stack_and_preserves_it(tmp_path):
    root=Path(__file__).resolve().parents[1]
    script=tmp_path/'build.cmd'
    script.write_text('@echo off\n'
        f'call "{root / "tools/setup_msvc.bat"}" || exit /b 1\n'
        'cl.exe /nologo /TP /MT /O2 /Oi- /W4 /WX /std:c++17 /EHs-c- /GR- '
        '/D_HAS_EXCEPTIONS=0 /DDLUMA_PRIVATE_XUI_DIAGNOSTIC=1 '
        f'/I"{root / "src"}" /Fo"{str(tmp_path).replace(chr(92), "/")}/" '
        f'/Fe"{tmp_path / "stack.exe"}" "{root / "tests/xui_first_loss_stack_windows.cpp"}" '
        f'"{root / "src/fixes/custom_map_asset_isolation/NativeXuiInstanceOwner.cpp"}" '
        '/link kernel32.lib || exit /b 1\n'
        f'"{tmp_path / "stack.exe"}" || exit /b 1\n')
    result=subprocess.run(['cmd.exe','/d','/c',str(script)],cwd=tmp_path,capture_output=True,text=True,timeout=90)
    assert result.returncode==0,result.stdout+result.stderr
