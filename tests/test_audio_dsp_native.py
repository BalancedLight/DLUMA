from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def test_audio_dsp_restoration_and_native_readback():
    subprocess.run(
        ["cmd.exe", "/d", "/s", "/c", "call tools\\build_audio_dsp_native_fake_tests.bat"],
        cwd=ROOT, check=True, capture_output=True, text=True,
    )
    result = subprocess.run(
        [str(ROOT / "build/audio-dsp-native-fake/audio_dsp_native_fake_harness.exe")],
        cwd=ROOT, check=True, capture_output=True, text=True,
    )
    assert result.stdout.count("PASS ") == 13
