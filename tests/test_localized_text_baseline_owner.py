from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def test_localized_text_baseline_owner_native_harness():
    subprocess.run(
        ["cmd.exe", "/d", "/s", "/c", "tools\\build_localized_text_baseline_owner_fake_tests.bat"],
        cwd=ROOT, check=True,
    )
    subprocess.run(
        [str(ROOT / "build/localized-text-baseline-owner-fake/localized_text_baseline_owner_fake_harness.exe")],
        cwd=ROOT, check=True,
    )
