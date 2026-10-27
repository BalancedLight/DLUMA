from __future__ import annotations

import subprocess
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def test_native_quest_graph_edit_parser_and_validator_execute():
    completed = subprocess.run(
        [
            "cmd.exe",
            "/d",
            "/s",
            "/c",
            "call tools\\build_quest_graph_edits_fake_tests.bat",
        ],
        cwd=ROOT,
        check=True,
        capture_output=True,
        text=True,
        timeout=120,
    )
    assert completed.stdout.count("[PASS]") == 4
    assert "quest graph edit cases passed: 4" in completed.stdout
