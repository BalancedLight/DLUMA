from pathlib import Path
import subprocess


def test_actual_document_screen_validation(tmp_path):
    root = Path(__file__).resolve().parents[1]
    source = (root / 'src/fixes/custom_map_asset_isolation/NativeXuiDocumentIntegration.inl').read_text()
    start = source.index('    const auto hud_length=')
    end = source.index('    DlumaXuiDocumentSnapshot locations=', start)
    (tmp_path / 'document_screen_validation.inl').write_text(source[start:end])
    start = source.index('std::uintptr_t __fastcall HookXuiCompiledAttempt(')
    end = source.index('bool CallNativeDocumentReload(', start)
    (tmp_path / 'document_compiled_bypass.inl').write_text(source[start:end])
    command = tmp_path / 'build.cmd'
    command.write_text('@echo off\n'
        f'call "{root / "tools/setup_msvc.bat"}" || exit /b 1\n'
        'cl.exe /nologo /TP /MT /W4 /WX /std:c++17 '
        f'/I"{root / "src"}" /I"{tmp_path}" /Fo"{tmp_path}/" '
        f'/Fe"{tmp_path / "screen.exe"}" "{root / "tests/xui_document_screen_identity_harness.cpp"}" || exit /b 1\n'
        f'"{tmp_path / "screen.exe"}"\n')
    result = subprocess.run(['cmd.exe', '/d', '/c', str(command)], cwd=tmp_path,
                            capture_output=True, text=True, timeout=90)
    assert result.returncode == 0, result.stdout + result.stderr
