import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def test_authenticated_replay_native_harness():
    build = subprocess.run(
        ["cmd.exe", "/d", "/c", "tools\\build_content_replay_permit_fake_tests.bat"],
        cwd=ROOT, capture_output=True, text=True, timeout=120,
    )
    assert build.returncode == 0, build.stdout + build.stderr
    run = subprocess.run(
        [str(ROOT / "build/content-replay-permit-fake/content_replay_permit_fake_harness.exe")],
        cwd=ROOT, capture_output=True, text=True, timeout=30,
    )
    assert run.returncode == 0, run.stdout + run.stderr
    assert "PASS: 22 authenticated replay scenarios including actual XUI and native factory authority" in run.stdout
