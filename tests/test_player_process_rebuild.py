from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def test_process_owner_seed_order_and_preservation():
    subprocess.run(
        ["cmd.exe", "/d", "/s", "/c", "call tools\\build_player_process_rebuild_fake_tests.bat"],
        cwd=ROOT, check=True, capture_output=True, text=True,
    )
    result = subprocess.run(
        [str(ROOT / "build/player-process-rebuild-fake/player_process_rebuild_fake_harness.exe")],
        cwd=ROOT, check=True, capture_output=True, text=True,
    )
    assert result.stdout.count("PASS ") == 17
