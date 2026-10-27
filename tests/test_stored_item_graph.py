from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def test_stored_item_graph_native_harness():
    subprocess.run(
        ["cmd.exe", "/d", "/s", "/c", "tools\\build_stored_item_graph_fake_tests.bat"],
        cwd=ROOT, check=True,
    )
    subprocess.run(
        [str(ROOT / "build/stored-item-graph-fake/stored_item_graph_fake_harness.exe")],
        cwd=ROOT, check=True,
    )
