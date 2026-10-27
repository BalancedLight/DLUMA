from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def test_crafting_manager_snapshot_native_harness():
    subprocess.run(
        ["cmd.exe", "/d", "/s", "/c", "tools\\build_crafting_manager_snapshot_fake_tests.bat"],
        cwd=ROOT, check=True,
    )
    subprocess.run(
        [str(ROOT / "build/crafting-manager-snapshot-fake/crafting_manager_snapshot_fake_harness.exe")],
        cwd=ROOT, check=True,
    )
