from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def test_audio_worker_reset_lifetime_and_cancellation():
    subprocess.run(
        ["cmd.exe", "/d", "/s", "/c", "call tools\\build_audio_worker_reset_fake_tests.bat"],
        cwd=ROOT, check=True, capture_output=True, text=True,
    )
    result = subprocess.run(
        [str(ROOT / "build/audio-worker-reset-fake/audio_worker_reset_fake_harness.exe")],
        cwd=ROOT, check=True, capture_output=True, text=True,
    )
    assert result.stdout.count("PASS ") == 15
