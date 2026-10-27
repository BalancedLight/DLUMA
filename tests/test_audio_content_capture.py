from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def test_owned_audio_startup_content_capture():
    subprocess.run(
        ["cmd.exe", "/d", "/s", "/c", "call tools\\build_audio_content_capture_fake_tests.bat"],
        cwd=ROOT, check=True, capture_output=True, text=True,
    )
    result = subprocess.run(
        [str(ROOT / "build/audio-content-capture-fake/audio_content_capture_fake_harness.exe")],
        cwd=ROOT, check=True, capture_output=True, text=True,
    )
    assert result.stdout.count("PASS ") == 18
