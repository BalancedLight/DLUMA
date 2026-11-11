from pathlib import Path
import subprocess


def test_resolver_admission_shutdown_and_map_reset_interleavings():
    root = Path(__file__).resolve().parents[1]
    completed = subprocess.run(
        ["cmd.exe", "/d", "/s", "/c", "call tools\\build_hud_resolver_gate_fake_tests.bat"],
        cwd=root, check=True, capture_output=True, text=True, timeout=90,
    )
    assert "hud resolver gate passed" in completed.stdout
