from pathlib import Path
import subprocess


def test_actual_hud_backend_large_tree_and_duplicate_limits():
    root = Path(__file__).resolve().parents[1]
    completed = subprocess.run(
        ["cmd.exe", "/d", "/s", "/c", "call tools\\build_hud_tree_backend_tests.bat"],
        cwd=root, check=False, capture_output=True, text=True, timeout=90,
    )
    assert completed.returncode == 0, completed.stdout + completed.stderr
    assert "actual HUD backend large-tree and duplicate checks passed" in completed.stdout
