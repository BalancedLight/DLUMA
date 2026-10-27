from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def test_item_curve_bindings_native_harness():
    subprocess.run(
        ["cmd.exe", "/d", "/s", "/c", "tools\\build_item_curve_bindings_fake_tests.bat"],
        cwd=ROOT, check=True,
    )
    subprocess.run(
        [str(ROOT / "build/item-curve-bindings-fake/item_curve_bindings_fake_harness.exe")],
        cwd=ROOT, check=True,
    )
