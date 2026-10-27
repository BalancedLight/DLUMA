from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def test_item_intern_pool_snapshot_native_harness():
    subprocess.run(
        ["cmd.exe", "/d", "/s", "/c", "tools\\build_item_intern_pool_snapshot_fake_tests.bat"],
        cwd=ROOT, check=True,
    )
    subprocess.run(
        [str(ROOT / "build/item-intern-pool-snapshot-fake/item_intern_pool_snapshot_fake_harness.exe")],
        cwd=ROOT, check=True,
    )
