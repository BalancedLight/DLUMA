from pathlib import Path
import os,subprocess

def test_actual_backend_private_diagnostic_context(tmp_path: Path) -> None:
    root=Path(__file__).resolve().parents[1]; exe=tmp_path/'hud_diag.exe'; obj=tmp_path/'hud_diag.obj'
    config=tmp_path/'diag.ini'
    config.write_text('[diagnostics]\nxui_private_enable=1\nxui_project=C:\\owned-project\nxui_map=test_map\nxui_subtree=EffectPanel\nxui_screen=HUD.xui\n',encoding='ascii')
    script=tmp_path/'compile.cmd';script.write_text('@echo off\ncall tools\\setup_msvc.bat\nif errorlevel 1 exit /b 1\n'
      f'cl.exe /nologo /TC /MT /O2 /Gy /Gw /W4 /WX /D_CRT_SECURE_NO_WARNINGS /DDLUMA_PRIVATE_XUI_DIAGNOSTIC=1 /DDLUMA_XUI_DIAGNOSTIC_TEST=1 /Fo"{obj}" '
      f'tests\\hud_tree_backend_fake_harness.c /Fe"{exe}" /link /OPT:REF user32.lib\nexit /b %errorlevel%\n',encoding='ascii')
    built=subprocess.run(['cmd.exe','/d','/c',str(script)],cwd=root,capture_output=True,text=True,timeout=90)
    assert built.returncode==0,built.stdout+built.stderr
    env=os.environ.copy();env['DLUMA_XUI_TEST_CONFIG']=str(config)
    result=subprocess.run([str(exe)],env=env,capture_output=True,text=True,timeout=15)
    assert result.returncode==0,result.stdout+result.stderr
