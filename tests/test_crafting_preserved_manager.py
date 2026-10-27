from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def test_crafting_preserved_manager_native_harness():
    subprocess.run(
        ["cmd.exe", "/d", "/s", "/c", "tools\\build_crafting_preserved_manager_fake_tests.bat"],
        cwd=ROOT, check=True,
    )
    subprocess.run(
        [str(ROOT / "build/crafting-preserved-manager-fake/crafting_preserved_manager_fake_harness.exe")],
        cwd=ROOT, check=True,
    )
