from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def test_curve_borrow_boundary_native_harness():
    subprocess.run(
        ["cmd.exe", "/d", "/s", "/c", "tools\\build_curve_borrow_boundary_fake_tests.bat"],
        cwd=ROOT, check=True,
    )
    subprocess.run(
        [str(ROOT / "build/curve-borrow-boundary-fake/curve_borrow_boundary_fake_harness.exe")],
        cwd=ROOT, check=True,
    )
