from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def test_native_user_gains_require_successful_completed_mixer_work():
    subprocess.run(
        ["cmd.exe", "/d", "/c", "tools\\build_audio_settings_native_fake_tests.bat"],
        cwd=ROOT, check=True,
    )
    subprocess.run(
        [str(ROOT / "build/audio-settings-native-fake/audio_settings_native_fake_harness.exe")],
        cwd=ROOT, check=True,
    )
