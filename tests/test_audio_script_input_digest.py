from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def test_exact_audio_script_parser_representation():
    built = subprocess.run(
        ["cmd.exe", "/d", "/c", "tools\\build_audio_script_input_digest_tests.bat"],
        cwd=ROOT, capture_output=True, text=True,
    )
    assert built.returncode == 0, built.stdout + built.stderr
    result = subprocess.run(
        [str(ROOT / "build/audio-script-input-digest/audio_script_input_digest_harness.exe")],
        cwd=ROOT, capture_output=True, text=True,
    )
    assert result.returncode == 0, result.stdout + result.stderr
    assert "PASS 8 exact script representation scenarios" in result.stdout
