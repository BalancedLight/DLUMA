from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def test_material_baseline_owner_native_harness():
    subprocess.run(
        ["cmd.exe", "/d", "/s", "/c", "tools\\build_material_baseline_owner_fake_tests.bat"],
        cwd=ROOT, check=True,
    )
    subprocess.run(
        [str(ROOT / "build/material-baseline-owner-fake/material_baseline_owner_fake_harness.exe")],
        cwd=ROOT, check=True,
    )
