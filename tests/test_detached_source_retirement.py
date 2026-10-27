from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def test_detached_source_retirement_native_harness():
    subprocess.run(
        ["cmd.exe", "/d", "/s", "/c", "tools\\build_detached_source_retirement_fake_tests.bat"],
        cwd=ROOT, check=True,
    )
    subprocess.run(
        [str(ROOT / "build/detached-source-retirement-fake/detached_source_retirement_fake_harness.exe")],
        cwd=ROOT, check=True,
    )
