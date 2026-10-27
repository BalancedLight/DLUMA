import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def test_player_vitals_owner_generation_and_native_failure():
    result = subprocess.run(
        ["cmd.exe", "/d", "/c", "tools\\build_player_vitals_fake_tests.bat"],
        cwd=ROOT, capture_output=True, text=True, timeout=180,
    )
    assert result.returncode == 0, result.stdout + result.stderr
    assert "player vitals service: ok" in result.stdout
