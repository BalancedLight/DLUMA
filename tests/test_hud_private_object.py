from pathlib import Path
import re,subprocess

def test_private_hud_backend_compiles_without_new_crt_dependencies(tmp_path: Path) -> None:
    root=Path(__file__).resolve().parents[1];obj=tmp_path/'hud_private.obj';symbols=tmp_path/'symbols.txt';script=tmp_path/'compile.cmd'
    script.write_text('@echo off\ncall tools\\setup_msvc.bat\nif errorlevel 1 exit /b 1\n'
      f'cl.exe /nologo /TC /O2 /Oi- /W4 /WX /DDLUMA_PRIVATE_XUI_DIAGNOSTIC=1 /c src\\game\\dlw_hud_visibility_backend.c /Fo"{obj}"\n'
      'if errorlevel 1 exit /b 1\n'
      f'dumpbin.exe /nologo /symbols "{obj}" > "{symbols}"\nexit /b %errorlevel%\n',encoding='ascii')
    result=subprocess.run(['cmd.exe','/d','/c',str(script)],cwd=root,capture_output=True,text=True,timeout=90)
    assert result.returncode==0,result.stdout+result.stderr
    undefined='\n'.join(line for line in symbols.read_text(errors='replace').splitlines() if 'UNDEF' in line)
    assert not re.search(r'\b(?:strcmp|strncmp|strrchr|strstr|_fdclass)\b',undefined),undefined
