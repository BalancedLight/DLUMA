import subprocess
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def test_quest_phase_fake_harness():
    completed = subprocess.run(
        [
            "cmd.exe",
            "/d",
            "/s",
            "/c",
            "call tools\\build_quest_phase_fake_tests.bat",
        ],
        cwd=ROOT,
        check=True,
        capture_output=True,
        text=True,
        timeout=90,
    )
    assert completed.stdout.count("[PASS]") == 5
    assert "Quest phase fake cases passed: 5" in completed.stdout


def test_quest_phase_security_contract_is_bounded():
    header = (ROOT / "src/core/dlw_quest_phase.h").read_text(encoding="utf-8")
    source = (ROOT / "src/core/dlw_quest_phase.c").read_text(encoding="utf-8")
    for token in (
        "DLUMA_QUEST_PHASE_MAX_ARGUMENT_BYTES 8192U",
        "DLUMA_QUEST_PHASE_MAX_JSON_DEPTH 8U",
        "DLUMA_QUEST_PHASE_MAX_JSON_NODES 256U",
        "DLUMA_QUEST_PHASE_QUEUE_CAPACITY 256U",
        "DLUMA_QUEST_PHASE_MAX_SIDECAR_BYTES",
    ):
        assert token in header
    assert "json_duplicate_key" in source
    assert "DLUMA_QUEST_PHASE_COMPLETION_STOCK_TIMED_WAIT" in source
    assert "registration_epoch" in source
    player = (ROOT / "src/dlw_quest_phase_player.c").read_text(
        encoding="utf-8"
    )
    assert "dluma_qpp_current_thread_in_callback()" in player
    assert "shutdown_from_hook_callback_retained_without_wait" in player
