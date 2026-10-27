from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def test_sealed_audio_baseline_replay():
    subprocess.run(
        ["cmd.exe", "/d", "/s", "/c", "call tools\\build_audio_baseline_replay_fake_tests.bat"],
        cwd=ROOT, check=True, capture_output=True, text=True,
    )
    result = subprocess.run(
        [str(ROOT / "build/audio-baseline-replay-fake/audio_baseline_replay_fake_harness.exe")],
        cwd=ROOT, check=True, capture_output=True, text=True,
    )
    assert result.stdout.count("PASS ") == 12
