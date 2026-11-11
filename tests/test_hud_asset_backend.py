from pathlib import Path
import subprocess

def test_actual_hud_asset_backend_marshals_and_scopes_requests(tmp_path: Path) -> None:
    root=Path(__file__).resolve().parents[1]
    exe=tmp_path/'hud_asset.exe'; obj=tmp_path/'hud_asset.obj'; script=tmp_path/'build.cmd'
    script.write_text('@echo off\ncall tools\\setup_msvc.bat\nif errorlevel 1 exit /b 1\n'
      f'cl.exe /nologo /TC /MT /O2 /Gy /Gw /W4 /WX /D_CRT_SECURE_NO_WARNINGS /Fo"{obj}" tests\\hud_asset_backend_fake_harness.c /Fe"{exe}" /link /OPT:REF user32.lib\nexit /b %errorlevel%\n',encoding='ascii')
    result=subprocess.run(['cmd.exe','/d','/c',str(script)],cwd=root,capture_output=True,text=True,timeout=90)
    assert result.returncode==0,result.stdout+result.stderr
    run=subprocess.run([str(exe)],capture_output=True,text=True,timeout=15)
    assert run.returncode==0,run.stdout+run.stderr
