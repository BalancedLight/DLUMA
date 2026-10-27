from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def test_speech_historical_content_and_preflight_lease(tmp_path):
    subprocess.run(
        ["cmd.exe", "/d", "/s", "/c", "call tools\\build_speech_content_lease_tests.bat"],
        cwd=ROOT, check=True, capture_output=True, text=True,
    )
    bank = tmp_path / "baseline.spb"
    other = tmp_path / "different.spb"
    data = b"0123456789" + bytes(range(256)) * 600
    bank.write_bytes(data)
    other.write_bytes(data)
    result = subprocess.run(
        [str(ROOT / "build/speech-content-lease/speech_content_lease_harness.exe"), str(bank), str(other)],
        cwd=ROOT, check=True, capture_output=True, text=True,
    )
    assert result.stdout.count("PASS ") == 8
    assert bank.read_bytes() == data
