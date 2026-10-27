import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def test_sound_property_native_harness():
    build = subprocess.run(
        ["cmd.exe", "/d", "/c", "tools\\build_audio_sound_property_snapshot_fake_tests.bat"],
        cwd=ROOT, capture_output=True, text=True, timeout=120,
    )
    assert build.returncode == 0, build.stdout + build.stderr
    run = subprocess.run(
        [str(ROOT / "build/audio-sound-property-fake/audio_sound_property_snapshot_fake_harness.exe")],
        cwd=ROOT, capture_output=True, text=True, timeout=30,
    )
    assert run.returncode == 0, run.stdout + run.stderr
    assert "PASS: 15 sound-property ownership scenarios" in run.stdout
