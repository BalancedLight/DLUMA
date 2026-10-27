from __future__ import annotations

import subprocess
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def test_loader_pak_exact_join_harness() -> None:
    completed = subprocess.run(
        [
            "cmd.exe",
            "/d",
            "/s",
            "/c",
            r"tools\build_loader_pak_exact_join_fake_tests.bat",
        ],
        cwd=ROOT,
        check=False,
        capture_output=True,
        text=True,
    )
    assert completed.returncode == 0, (
        completed.stdout + "\n" + completed.stderr
    )
