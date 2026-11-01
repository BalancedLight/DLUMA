import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def test_nightly_offer_contract():
    result = subprocess.run(
        ["cmd.exe", "/d", "/c", "tools\\build_nightly_offer_tests.bat"],
        cwd=ROOT, capture_output=True, text=True, timeout=90,
    )
    assert result.returncode == 0, result.stdout + result.stderr
    assert "Nightly offer core checks passed" in result.stdout


def test_after_exit_installer_contract():
    result = subprocess.run(
        [sys.executable, str(ROOT / "tests" / "nightly_apply_core.py")],
        cwd=ROOT, capture_output=True, text=True, timeout=120,
    )
    assert result.returncode == 0, result.stdout + result.stderr
    assert "Installer core checks passed" in result.stdout
