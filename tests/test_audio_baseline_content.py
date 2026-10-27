from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def test_actual_bank_content_pin_and_baseline_joins(tmp_path):
    build = subprocess.run(
        ["cmd.exe", "/d", "/s", "/c", "call tools\\build_audio_baseline_content_tests.bat"],
        cwd=ROOT, capture_output=True, text=True,
    )
    assert build.returncode == 0, build.stdout + build.stderr
    bank = tmp_path / "baseline.csb"
    data = bytes(range(256)) * 600
    bank.write_bytes(data)
    result = subprocess.run(
        [str(ROOT / "build/audio-baseline-content/audio_baseline_content_harness.exe"), str(bank)],
        cwd=ROOT, check=True, capture_output=True, text=True,
    )
    assert result.stdout.count("PASS ") == 7
    assert bank.read_bytes() == data
