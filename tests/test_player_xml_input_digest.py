from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def test_native_xml_input_digest():
    subprocess.run(
        ["cmd.exe", "/d", "/s", "/c", "call tools\\build_player_xml_input_digest_fake_tests.bat"],
        cwd=ROOT, check=True, capture_output=True, text=True,
    )
    result = subprocess.run(
        [str(ROOT / "build/player-xml-input-fake/player_xml_input_digest_fake_harness.exe")],
        cwd=ROOT, check=True, capture_output=True, text=True,
    )
    assert result.stdout.count("PASS ") == 7
