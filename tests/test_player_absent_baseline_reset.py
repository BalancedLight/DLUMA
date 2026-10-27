from pathlib import Path
import subprocess


ROOT = Path(__file__).resolve().parents[1]


def test_player_absent_baseline_reset_native_harness():
    subprocess.run(
        [
            "cmd.exe",
            "/d",
            "/s",
            "/c",
            r"tools\build_player_absent_baseline_reset_fake_tests.bat",
        ],
        cwd=ROOT,
        check=True,
    )
    subprocess.run(
        [
            ROOT
            / "build"
            / "player-absent-reset-fake"
            / "player_absent_baseline_reset_fake_harness.exe"
        ],
        cwd=ROOT,
        check=True,
    )


def test_unvalidated_absent_reset_is_not_invoked_in_production():
    production = (
        ROOT
        / "src"
        / "fixes"
        / "custom_map_asset_isolation"
        / "CustomMapAssetIsolationFix.cpp"
    ).read_text(encoding="utf-8")
    assert '#include "PlayerAbsentBaselineReset.h"' in production
    assert "g_player_absent_reset_adapter" in production
    assert "InitializePlayerAbsentResetAdapter(" in production
    assert "CaptureProductionPlayerAbsentBaseline(" in production
    assert "PrepareProductionPlayerAbsentResetPlan(" in production
    assert "CapturePlayerGameplayParamsAbsentEvidence(" in production
    assert "player_absent_b0_capture_attempt_count" in production
    assert "player_absent_production_wiring_active" in production
    assert "owner_route_validated = false" in production
    assert "ExecuteAndVerifyPlayerAbsentReset(" not in production
    assert "TouchedResetUnavailable" in (
        ROOT
        / "src"
        / "fixes"
        / "custom_map_asset_isolation"
        / "PlayerGameplayParamsRestartAdapter.cpp"
    ).read_text(encoding="utf-8")
