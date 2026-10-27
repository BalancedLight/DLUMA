from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def test_owner_independent_primitive_defaults():
    subprocess.run(
        ["cmd.exe", "/d", "/s", "/c", "call tools\\build_player_primitive_defaults_fake_tests.bat"],
        cwd=ROOT, check=True, capture_output=True, text=True,
    )
    result = subprocess.run(
        [str(ROOT / "build/player-primitive-defaults-fake/player_primitive_defaults_fake_harness.exe")],
        cwd=ROOT, check=True, capture_output=True, text=True,
    )
    assert result.stdout.count("PASS ") == 4
