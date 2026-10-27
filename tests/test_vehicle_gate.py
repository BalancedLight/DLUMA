from __future__ import annotations

import subprocess
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
CORE = (ROOT / "src/dlw_vehicle_gate_core.h").read_text(encoding="utf-8")
ADAPTER = (ROOT / "src/dlw_player_media_adapter.c").read_text(encoding="utf-8")


def test_vehicle_gate_fake_harness() -> None:
    completed = subprocess.run(
        ["cmd.exe", "/d", "/s", "/c",
         "call tools\\build_vehicle_gate_fake_tests.bat"],
        cwd=ROOT,
        check=True,
        capture_output=True,
        text=True,
        timeout=90,
    )
    assert completed.stdout.count("[PASS]") == 5
    assert "Vehicle gate fake cases passed: 5" in completed.stdout


def test_vehicle_gate_requires_stable_owner_and_velocity_evidence() -> None:
    for evidence in (
        "driver_matches_local_player",
        "stock_buggy_matches_vehicle",
        "identities_stable",
        "velocity_available",
        "velocity_finite",
    ):
        assert evidence in CORE
    for operation in (
        "profile->current_vehicle(actor)",
        "profile->vehicle_driver(buggy)",
        "profile->actor_is_driver(actor)",
        "dluma_vehicle_gate_evaluate(",
    ):
        assert operation in ADAPTER
