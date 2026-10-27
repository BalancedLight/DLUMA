from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def test_audio_speech_consumer_graph_and_native_release_guards():
    subprocess.run(
        ["cmd.exe", "/d", "/s", "/c", "call tools\\build_audio_speech_consumers_fake_tests.bat"],
        cwd=ROOT, check=True, capture_output=True, text=True,
    )
    result = subprocess.run(
        [str(ROOT / "build/audio-speech-consumers-fake/audio_speech_consumers_fake_harness.exe")],
        cwd=ROOT, check=True, capture_output=True, text=True,
    )
    assert result.stdout.count("PASS ") == 10
