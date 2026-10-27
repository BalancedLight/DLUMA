from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def test_localized_text_observation_native_harness():
    subprocess.run(
        ["cmd.exe", "/d", "/s", "/c", "tools\\build_localized_text_observation_fake_tests.bat"],
        cwd=ROOT, check=True,
    )
    # The bridge is deliberately process-lifetime. Separate processes check
    # permanent poison cases without adding a production 'forget borrows' API.
    executable = ROOT / "build/localized-text-observation-fake/localized_text_observation_fake_harness.exe"
    for case in range(21):
        subprocess.run([str(executable), str(case)], cwd=ROOT, check=True, timeout=15)
