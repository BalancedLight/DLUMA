from __future__ import annotations

import re
import subprocess
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def test_project_launch_admission_native_harness() -> None:
    completed = subprocess.run(
        ["cmd.exe", "/d", "/s", "/c",
         r"tools\build_project_launch_admission_fake_tests.bat"],
        cwd=ROOT,
        check=False,
        capture_output=True,
        text=True,
    )
    assert completed.returncode == 0, (
        completed.stdout + "\n" + completed.stderr
    )


def test_b0_feedback_precedes_request_age_and_is_localized() -> None:
    admission = (ROOT / "src/dlw_project_launch_admission.c").read_text(
        encoding="utf-8"
    )
    ui = (ROOT / "src/dlw_ui.c").read_text(encoding="utf-8")
    locales = list((ROOT / "PakAssets/Locale").glob(
        "*/loader_texts_all.scr"
    ))
    assert admission.index("b0_result =") < admission.index(
        "DLUMA_PROJECT_LAUNCH_ADMISSION_STALE_REQUEST"
    )
    for result in (
        "b0_preparing",
        "b0_permanent_source_failure",
        "b0_restart_required",
    ):
        assert result in admission
    assert "DLUMA_PROJECT_DENIED_ACTIVATION_DEBOUNCE_MS" in admission
    assert "last_premount_delivery_debounced" in ui
    assert "permanent_validation_failure" in ui
    assert re.search(
        r"evidence->startup_b0_state,\s*\"PermanentFailure\"",
        ui,
    )
    harness = (
        ROOT / "tests/project_launch_admission_fake_harness.c"
    ).read_text(encoding="utf-8")
    assert (
        "test_runtime10_complete_authorized_permits_one_play_activation"
        in harness
    )
    assert (
        "test_runtime10_permanent_failure_blocks_before_mutation"
        in harness
    )
    for key in (
        "DLUMA_Workshop_ContentIsolationPreparing",
        "DLUMA_Workshop_ContentIsolationRestartRequired",
        "DLUMA_Workshop_ContentIsolationValidationFailed",
        "DLUMA_Workshop_PlayerBaselineNotVerified",
        "DLUMA_Workshop_SeeLoaderLog",
    ):
        assert key in ui
        assert len(locales) == 17
        assert all(
            key in path.read_text(encoding="utf-8")
            for path in locales
        )


def test_change_after_fade_uses_exact_cross_thread_authority() -> None:
    header = (ROOT / "src/dlw_project_launch_admission.h").read_text(
        encoding="utf-8"
    )
    source = (ROOT / "src/dlw_project_launch_admission.c").read_text(
        encoding="utf-8"
    )
    harness = (
        ROOT / "tests/project_launch_admission_fake_harness.c"
    ).read_text(encoding="utf-8")

    for field in (
        "premount_thread_id",
        "transition_thread_id",
        "premount_frame_route_token",
        "current_frame_route_token",
        "exact_change_after_fade_hook",
        "transition_context_generation",
        "transition_context_depth",
        "transition_context_serialized",
        "transition_context_recursive",
        "transition_context_concurrent",
        "source_ownership_complete",
        "source_set_snapshot_result",
        "project_root_source_count",
        "data_directory_source_count",
        "assets_pc_source_count",
    ):
        assert field in header
    assert "attestation->thread_id != state->token_thread_id" not in source
    assert "transition_thread_id != state->token_thread_id" not in source
    assert "custom_project_transition_thread_migrated" in source
    assert "custom_project_last_consumed_transition_generation" in source
    assert "42112U" in harness
    assert re.search(
        r"attestation\.transition_thread_id\s*!=\s*"
        r"state\.token_thread_id",
        harness,
    )

    for reason in (
        "change_after_fade_hook_unvalidated",
        "change_after_fade_context_generation_invalid",
        "change_after_fade_context_recursive",
        "change_after_fade_context_concurrent",
        "change_after_fade_frame_route_changed",
        "change_after_fade_frame_authority_conflict",
        "change_after_fade_epoch_mismatch",
        "change_after_fade_project_root_mismatch",
        "change_after_fade_source_set_incomplete",
        "change_after_fade_source_set_duplicate_kind",
        "change_after_fade_source_set_snapshot_ledger_busy",
        "change_after_fade_session_missing",
        "change_after_fade_destination_frontend_or_empty",
        "change_after_fade_generation_not_fresh",
    ):
        assert reason in source
        assert reason in harness


def test_transition_attestation_failure_classifies_cleanup_authority() -> None:
    source = (ROOT / "src/dlw_project_launch_admission.c").read_text(
        encoding="utf-8"
    )
    harness = (
        ROOT / "tests/project_launch_admission_fake_harness.c"
    ).read_text(encoding="utf-8")
    assert "exact_source_ownership" in source
    assert "DLUMA_PROJECT_CHANGE_AFTER_FADE_QUARANTINED_FOR_CLEANUP" in source
    assert "DLUMA_PROJECT_CHANGE_AFTER_FADE_FATAL_SOURCE_CONTRADICTION" in source
    assert "cleanup_still_authorized" in source
    assert "dluma_project_launch_admission_note_quarantined_cleanup_result" in (
        source
    )
    assert "test_change_after_fade_exact_failure_reasons" in harness


def test_project_root_identity_is_canonical_and_preserves_presentation_text() -> None:
    root_header = (ROOT / "src/dlw_project_root_identity.h").read_text(
        encoding="utf-8"
    )
    admission_header = (
        ROOT / "src/dlw_project_launch_admission.h"
    ).read_text(encoding="utf-8")
    harness = (
        ROOT / "tests/project_launch_admission_fake_harness.c"
    ).read_text(encoding="utf-8")

    for contract in (
        "dluma_project_root_canonicalize",
        "dluma_project_root_identity_equal",
        "dluma_project_root_is_canonical",
    ):
        assert contract in root_header
    assert "length == 1U && output[segment_begin] == '.'" in root_header
    assert "output_index + 1U >= capacity" in root_header
    assert "output[0] = 0;" in root_header
    assert "display_project_root" in admission_header
    for live_shape in (
        r'"X:\\FixtureLibrary\\steamapps\\common\\Dying Light\\DevTools\\"',
        r'"\\\\?\\X:\\FixtureLibrary\\steamapps\\common\\Dying Light\\"',
        r'"\\?\\X:\\FixtureLibrary\\steamapps\\common\\Dying Light\\"',
        r'"//?/X:/FixtureLibrary/steamapps/common/Dying Light/DevTools/"',
        "42112U",
        "77U",
    ):
        assert live_shape in harness
