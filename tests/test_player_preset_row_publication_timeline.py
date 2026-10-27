from __future__ import annotations

from pathlib import Path
import re
import subprocess


ROOT = Path(__file__).resolve().parents[1]
PRODUCTION = (
    ROOT
    / "src"
    / "fixes"
    / "custom_map_asset_isolation"
    / "CustomMapAssetIsolationFix.cpp"
)


def read(relative: str) -> str:
    return (ROOT / relative).read_text(encoding="utf-8")


def between(source: str, start: str, end: str) -> str:
    offset = source.index(start)
    return source[offset : source.index(end, offset)]


def between_last(source: str, start: str, end: str) -> str:
    offset = source.rindex(start)
    return source[offset : source.index(end, offset)]


def test_player_preset_row_publication_timeline_native_harness() -> None:
    completed = subprocess.run(
        [
            "cmd.exe",
            "/d",
            "/s",
            "/c",
            "call tools\\build_player_preset_row_publication_timeline_fake_tests.bat"
            " && build\\player-preset-row-publication-timeline-fake\\"
            "player_preset_row_publication_timeline_fake_harness.exe",
        ],
        cwd=ROOT,
        check=False,
        capture_output=True,
        text=True,
        encoding="utf-8",
        errors="replace",
    )
    assert completed.returncode == 0, (
        f"stdout:\n{completed.stdout}\nstderr:\n{completed.stderr}"
    )
    assert (
        "player preset row publication timeline fake harness passed"
        in completed.stdout
    )


def test_all_thirteen_production_checkpoints_are_wired_at_owned_boundaries() -> None:
    source = PRODUCTION.read_text(encoding="utf-8")
    header = read(
        "src/fixes/custom_map_asset_isolation/"
        "PlayerPresetRowPublicationTimeline.h"
    )
    checkpoints = (
        "StartupB0Commit",
        "BeforeProjectPremountOriginal",
        "UpdateDlcContentReturn",
        "ChangeAfterFadeEntry",
        "ChangeAfterFadeReturn",
        "PlayerDiOnCreateReturn",
        "FirstStableGameplayFrame",
        "C08Entry",
        "C10Entry",
        "SessionOnDestroyEntry",
        "SessionOnDestroyOriginalReturn",
        "PostC10Pass1",
        "PostC10Pass2",
    )
    for checkpoint in checkpoints:
        assert re.search(
            rf"Checkpoint::\s*{checkpoint}", source
        )
        assert checkpoint in header
    assert header.index("PostC10Pass2") < header.index("Count,")

    ui = read("src/dlw_ui.c")
    premount = between(
        ui,
        "if (result == DLUMA_PROJECT_LAUNCH_ADMISSION_ALLOWED) {",
        "if (dluma_ui_project_launch_lock())",
    )
    assert premount.index(
        "dluma_custom_map_asset_isolation_note_premount_original();"
    ) < premount.index("hook_state->premount_original(screen, argument)")

    after_fade = between(
        source,
        "void __fastcall HookChangeAfterFade",
        "void __fastcall HookPleaseWaitFinished",
    )
    assert after_fade.index("Checkpoint::ChangeAfterFadeEntry") < (
        after_fade.index("g_original_change_after_fade(session)")
    ) < after_fade.index("Checkpoint::ChangeAfterFadeReturn")

    on_destroy = between(
        source,
        "std::uintptr_t __fastcall HookSessionOnDestroy",
        "void __fastcall HookUnloadLevel",
    )
    outer = on_destroy.index("Checkpoint::SessionOnDestroyEntry")
    original = on_destroy.index(
        "g_original_session_on_destroy(session)", outer
    )
    returned = on_destroy.index(
        "Checkpoint::", original
    )
    assert "SessionOnDestroyOriginalReturn" in on_destroy[
        returned : returned + 180
    ]


def test_timeline_sampling_is_read_only_generation_checked_and_loss_explicit() -> None:
    source = PRODUCTION.read_text(encoding="utf-8")
    capture = between_last(
        source,
        "void CapturePlayerPublicationCheckpoint(",
        "void CapturePlayerPublicationB0Commit(",
    )
    assert capture.index("FindTimelineOwnerRecord(") < capture.index(
        "SnapshotProductionPlayerState("
    )
    assert "if (!owner)" in capture
    assert "owner->generation" in capture
    assert "TimelineOwnerSetMatches(" in capture
    assert "TimelineEnvironmentEqual(" in capture
    assert "inventory_revision_before == inventory_revision_after" in capture
    assert "project_admission_epoch" in capture
    assert not re.search(
        r"(?:const\s+)?playerstate::Snapshot\s+\w+\s*=",
        capture,
    ), "checkpoint capture must not copy the large Player ledger to the stack"
    assert (
        "g_player_publication_checkpoint_ledger_snapshot"
        in capture
    )
    assert capture.count("g_production_player_live_snapshot") >= 2
    second_pass = capture.index(
        "const bool environment_after_ready"
    )
    frozen_ledger = capture.index(
        "&g_player_publication_checkpoint_ledger_snapshot",
        second_pass,
    )
    final_revision = capture.index(
        "SnapshotTimelineInventoryRevision(",
        frozen_ledger,
    )
    assert second_pass < frozen_ledger < final_revision
    for forbidden in (
        "ProductionResetGameplayParamsCache(",
        "ProductionInitializePlayerStateVariables(",
        "g_original_player_state_clear(",
        "g_original_remove_cached_player_level_presets(",
        "g_original_player_state_initialize_variables(",
        "AudioScriptReload",
        "RemoveAndDestroyPathCreatedSource(",
        "g_original_fs_remove_object(",
    ):
        assert forbidden not in capture

    header = read(
        "src/fixes/custom_map_asset_isolation/"
        "PlayerPresetRowPublicationTimeline.h"
    )
    for loss in (
        "invalid_checkpoint",
        "duplicate_checkpoint",
        "critical_lock_contention",
        "cache_publication_overflow",
        "observer_event_overflow",
    ):
        assert loss in header


def test_exact_observers_and_publication_report_contract() -> None:
    source = PRODUCTION.read_text(encoding="utf-8")
    for route in (
        "CachePlayerLevelPreset",
        "FindPlayerLevelPreset",
        "SetProgressTemplate",
        "RemovePrecachedLevelPresets",
        "InitializeVariables",
        "SetVariablesToLevel",
        "PlayerDiOnCreate",
    ):
        assert f"ObserverRoute::{route}" in source
    validation = between(
        source,
        "bool TimelineObserverRouteValidated(",
        "void PopulateTimelineObservedProvider(",
    )
    for guard in (
        "g_gamedll_full_profile_valid",
        "status->validated",
        "status->installed",
        "status->rva == expected_rva",
        "status->target == expected_target",
    ):
        assert guard in validation
    assert "constexpr bool kEnableDefaultLevelsFovObservers = false;" in source
    assert (
        "constexpr bool kEnablePlayerDiOnDestroyDiagnosticObserver ="
        in source
    )

    required_fields = (
        "player_first_publication_available",
        "player_first_publication_checkpoint",
        "player_first_publication_sequence",
        "player_first_publication_thread",
        "player_first_publication_owner_kind",
        "player_first_publication_owner_generation",
        "player_first_publication_row",
        "player_first_publication_baseline_kind",
        "player_first_publication_post_kind",
        "player_first_publication_baseline_preset",
        "player_first_publication_post_preset",
        "player_first_publication_baseline_definition_count",
        "player_first_publication_post_definition_count",
        "player_first_publication_baseline_fingerprint",
        "player_first_publication_post_fingerprint",
        "player_first_publication_provider",
        "player_first_publication_source",
        "player_first_publication_cache_membership",
        "player_first_publication_attribution",
        "player_rows_changed_mask",
        "player_all_eight_rows_changed_same_direction",
        "player_other_owner_remained_semantically_stable",
    )
    for field in required_fields:
        assert field in source
    report = between(
        source,
        "bool WriteContentLedgerReport(",
        "bool FinalizeCustomContentReports(",
    )
    assert report.index(
        "ReportAppendPlayerPresetPublicationTimeline(&builder)"
    ) < report.index("post_destroy_audio_assessment")
