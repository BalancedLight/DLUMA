from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def test_native_retained_resource_owner_harness():
    subprocess.run(
        ["cmd.exe", "/d", "/s", "/c", "tools\\build_native_retained_resource_owner_fake_tests.bat"],
        cwd=ROOT, check=True,
    )
    subprocess.run(
        [str(ROOT / "build/native-retained-resource-fake/native_retained_resource_owner_fake_harness.exe")],
        cwd=ROOT, check=True,
    )
