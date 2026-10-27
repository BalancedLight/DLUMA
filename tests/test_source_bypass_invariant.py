from __future__ import annotations

import subprocess
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
FIX = ROOT / "src/fixes/custom_map_asset_isolation/CustomMapAssetIsolationFix.cpp"


def test_source_bypass_invariant_harness() -> None:
    completed = subprocess.run(
        [
            "cmd.exe",
            "/d",
            "/s",
            "/c",
            r"tools\build_source_bypass_invariant_fake_tests.bat",
        ],
        cwd=ROOT,
        check=False,
        capture_output=True,
        text=True,
    )
    assert completed.returncode == 0, (
        completed.stdout + "\n" + completed.stderr
    )


def test_source_add_observer_records_complete_first_caller_and_fails_closed():
    source = FIX.read_text(encoding="utf-8")
    ui = (ROOT / "src/dlw_ui.c").read_text(encoding="utf-8")
    for field in (
        "bypass.source_object",
        "bypass.source_path",
        "bypass.source_kind",
        "bypass.caller_return_address",
        "bypass.caller_module",
        "bypass.caller_module_rva",
        "bypass.thread_id",
    ):
        assert field in source
    assert "dluma_project_launch_note_custom_source_ex(" in source
    assert "MarkContentDirty();" in source
    assert "g_dluma_active_project_launch_controller" in ui
    assert "allowed_token_generation" in ui
    assert "suppress_premount_update = true" in source
    assert "if (!suppress_premount_update)" in source
    assert "premount-token-validation-failed;original-suppressed" in source
