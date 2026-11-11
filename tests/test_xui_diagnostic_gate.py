from pathlib import Path
import subprocess

def test_private_xui_diagnostic_gate(tmp_path: Path) -> None:
    root=Path(__file__).resolve().parents[1]
    exe=tmp_path/'xui_gate.exe'; obj=tmp_path/'xui_gate.obj'; script=tmp_path/'compile.cmd'
    script.write_text('@echo off\ncall tools\\setup_msvc.bat\nif errorlevel 1 exit /b 1\n'
        f'cl.exe /nologo /TC /MT /W4 /WX /Fe"{exe}" /Fo"{obj}" tests\\xui_diagnostic_gate_fake_harness.c\nexit /b %errorlevel%\n',encoding='utf-8')
    built=subprocess.run(['cmd.exe','/d','/c',str(script)],cwd=root,capture_output=True,text=True,timeout=90)
    assert built.returncode==0,built.stdout+built.stderr
    result=subprocess.run([str(exe)],capture_output=True,text=True,timeout=10)
    assert result.returncode==0,result.stdout+result.stderr
    assert 'PASS: private XUI diagnostic gate' in result.stdout
