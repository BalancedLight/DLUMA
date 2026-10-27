from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def test_decoded_archive_content_and_native_cursor_contract():
    built = subprocess.run(
        ["cmd.exe", "/d", "/c", "tools\\build_native_archive_content_fake_tests.bat"],
        cwd=ROOT, capture_output=True, text=True, timeout=120,
    )
    assert built.returncode == 0, built.stdout + built.stderr
    result = subprocess.run(
        [str(ROOT / "build/native-archive-content-fake/native_archive_content_fake_harness.exe")],
        cwd=ROOT, capture_output=True, text=True, timeout=30,
    )
    assert result.returncode == 0, result.stdout + result.stderr
    assert "17 scenarios passed" in result.stdout
