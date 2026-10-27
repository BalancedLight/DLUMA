from pathlib import Path
import subprocess


ROOT = Path(__file__).resolve().parents[1]


def test_post_destroy_preflight_report_native_harness():
    subprocess.run(
        [
            "cmd.exe",
            "/d",
            "/s",
            "/c",
            r"tools\build_post_destroy_preflight_report_fake_tests.bat",
        ],
        cwd=ROOT,
        check=True,
    )
    subprocess.run(
        [
            ROOT
            / "build"
            / "post-destroy-preflight-report-fake"
            / "post_destroy_preflight_report_fake_harness.exe"
        ],
        cwd=ROOT,
        check=True,
    )


def test_structured_report_is_wired_at_post_destroy_boundary():
    production = (
        ROOT
        / "src"
        / "fixes"
        / "custom_map_asset_isolation"
        / "CustomMapAssetIsolationFix.cpp"
    ).read_text(encoding="utf-8")
    assert '#include "PostDestroyPreflightReport.h"' in production
    assert "BuildPostDestroyPreflightReport" in production
    assert "post_destroy_preflight_blocking_domain_mask" in production
    assert "post_destroy_transaction_preparation_attempted" in production
    assert "player_persistent_owner_release_path_unvalidated" in (
        ROOT
        / "src"
        / "fixes"
        / "custom_map_asset_isolation"
        / "PostDestroyPreflightReport.cpp"
    ).read_text(encoding="utf-8")
    assert "post_destroy_audio_script_delta" in production
    assert "post_destroy_audio_bank_tree_delta" in production
    assert "post_destroy_audio_bank_reference_delta" in production
    report = (
        ROOT
        / "src"
        / "fixes"
        / "custom_map_asset_isolation"
        / "PostDestroyPreflightReport.cpp"
    ).read_text(encoding="utf-8")
    exact_audio = "audio_touched_script_registry_and_bank_tree_reset_unavailable"
    assert exact_audio in report
    assert report.index(exact_audio) < report.index(
        "audio_bank_tree_release_replay_unvalidated"
    )
