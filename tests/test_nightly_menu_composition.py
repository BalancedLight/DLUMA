from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def test_nightly_popup_matches_bundled_xui():
    from tools.generate_nightly_popup import validate
    validate()


def test_nightly_menu_composition(tmp_path):
    executable = tmp_path / 'nightly_menu_compose.exe'
    obj = tmp_path / 'nightly_menu_compose.obj'
    command = (
        f'call "{ROOT / "tools/setup_msvc.bat"}" >nul && '
        f'cl /nologo /W4 /WX /TC /I "{ROOT / "src"}" '
        f'/Fo:"{obj}" /Fe:"{executable}" '
        f'"{ROOT / "tests/nightly_menu_compose_harness.c"}"'
    )
    batch = tmp_path / 'compile.bat'
    batch.write_text('@echo off\n' + command + '\n', encoding='ascii')
    subprocess.run(['cmd', '/d', '/c', str(batch)], cwd=tmp_path, check=True)
    subprocess.run([str(executable)], check=True)
