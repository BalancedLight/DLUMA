from pathlib import Path
import subprocess


def test_position_dispatch_validates_and_returns_backend_failures():
    root = Path(__file__).resolve().parents[1]
    completed = subprocess.run(
        ["cmd.exe", "/d", "/s", "/c", "call tools\\build_xui_element_core_fake_tests.bat"],
        cwd=root, check=True, capture_output=True, text=True, timeout=90,
    )
    assert "xui element core passed" in completed.stdout


def test_position_core_links_without_crt_math():
    root = Path(__file__).resolve().parents[1]
    completed = subprocess.run(
        ["cmd.exe", "/d", "/s", "/c", "call tools\\build_xui_element_nocrt_tests.bat"],
        cwd=root, check=True, capture_output=True, text=True, timeout=90,
    )
    assert "xui element no-CRT link passed" in completed.stdout
