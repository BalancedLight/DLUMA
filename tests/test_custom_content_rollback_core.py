from __future__ import annotations

import re
import subprocess
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
FIX_DIR = ROOT / "src" / "fixes" / "custom_map_asset_isolation"
HARNESS = ROOT / "tests" / "custom_content_rollback_fake_harness.cpp"
LEDGER_HEADER = (FIX_DIR / "CustomContentLedger.h").read_text(encoding="utf-8")
LEDGER_SOURCE = (FIX_DIR / "CustomContentLedger.cpp").read_text(encoding="utf-8")
RESET_HEADER = (FIX_DIR / "CustomContentResetRegistry.h").read_text(
    encoding="utf-8"
)
AUDIO_HEADER = (FIX_DIR / "CustomAudioBankLedger.h").read_text(encoding="utf-8")
AUDIO_SOURCE = (FIX_DIR / "CustomAudioBankLedger.cpp").read_text(encoding="utf-8")
AUDIO_RESET = (FIX_DIR / "CustomAudioResetAdapter.cpp").read_text(encoding="utf-8")

EXPECTED_CASES = 46
LIFECYCLE_CASES = (
    "frontend_epoch_selection_is_fail_closed",
    "single_map_on_destroy_resets_once_after_original",
    "same_project_two_map_defers_reset_until_final_on_destroy",
    "exact_owned_unload_and_release_are_required",
    "wrong_session_level_shutdown_and_afterfade_are_rejected",
    "nested_on_destroy_resets_once",
    "consumed_binding_cannot_reset_twice",
    "dropped_or_non_exact_sources_block_reset",
    "unlink_or_destructor_failure_is_runtime_failed",
    "frontend_lookup_before_reset_fails",
    "frontend_lookup_after_reset_succeeds",
    "startup_and_non_on_destroy_routes_do_not_reset",
    "latest_null_name_colocated_two_clevel_replay",
    "mutation_loss_categories_are_independent",
    "owned_release_scope_variations",
    "exact_source_identity_and_watched_metadata",
)


def test_native_content_rollback_core_executes_all_named_cases():
    subprocess.run(
        [
            "cmd.exe",
            "/d",
            "/s",
            "/c",
            "call tools\\build_custom_content_rollback_fake_tests.bat",
        ],
        cwd=ROOT,
        check=True,
        capture_output=True,
        text=True,
    )
    completed = subprocess.run(
        [
            str(
                ROOT
                / "build"
                / "content-rollback-fake"
                / "custom_content_rollback_fake_harness.exe"
            )
        ],
        cwd=ROOT,
        check=True,
        capture_output=True,
        text=True,
        timeout=30,
    )
    assert completed.stdout.count("[PASS]") == EXPECTED_CASES
    assert (
        f"custom content rollback cases passed: {EXPECTED_CASES}"
        in completed.stdout
    )
    for case_name in LIFECYCLE_CASES:
        assert f"[PASS] {case_name}" in completed.stdout


def test_verified_retirement_reuses_only_completed_epoch_slots():
    completed = subprocess.run(
        ["cmd.exe", "/d", "/s", "/c",
         "call tools\\build_content_ledger_retirement_fake_tests.bat"],
        cwd=ROOT, check=True, capture_output=True, text=True, timeout=120,
    )
    assert "96 A/B/A cycles" in completed.stdout
    assert "stale token rejection" in completed.stdout
    assert "archive and fail-closed evidence passed" in completed.stdout


def test_ledgers_and_registry_are_bounded_pod_without_dynamic_ownership():
    for capacity in (
        "kMaximumEpochs = 16",
        "kMaximumSources = 128",
        "kMaximumFiles = 384",
        "kMaximumPacks = 128",
        "kMaximumDiagnostics = 256",
    ):
        assert capacity in LEDGER_HEADER
    assert "kMaximumResetAdapters = 16" in RESET_HEADER
    assert "kMaximumBaselineAudioBanks = 128" in AUDIO_HEADER
    assert "kMaximumCustomAudioBanks = 256" in AUDIO_HEADER
    for source in (LEDGER_SOURCE, AUDIO_SOURCE, AUDIO_RESET):
        for forbidden in (
            "new ",
            "delete ",
            "std::vector",
            "std::map",
            "std::string",
            "malloc(",
            "realloc(",
        ):
            assert forbidden not in source


def test_source_reset_snapshot_is_reverse_add_order_and_excludes_nonowned():
    assert "record.loader_owned_custom_content" in LEDGER_SOURCE
    assert re.search(
        r"output\[right\]\.successful_add_ordinal\s*>\s*"
        r"output\[left\]\.successful_add_ordinal",
        LEDGER_SOURCE,
    )
    harness = HARNESS.read_text(encoding="utf-8")
    assert '"source_reset_uses_reverse_successful_add_order"' in harness
    assert (
        '"cancelled_cleanup_verifies_live_registry_exact_minus_sources"'
        in harness
    )
    assert '"custom_source_exclusions_are_non_destructive"' in harness
    assert '"ambiguous_path_removal_cannot_select_an_owner"' in harness
    assert '"duplicate_active_source_kind_fails_closed"' in harness
    assert '"presentation_source_set_canonical_join_and_reasons"' in harness
    assert '"closed_epoch_rejects_late_file_ownership"' in harness
    assert '"exact_map_owner_proof_is_current_and_fail_closed"' in harness


def test_presentation_source_set_snapshot_is_reason_coded_and_canonical():
    for result in (
        "InvalidArgument",
        "Succeeded",
        "LedgerBusy",
        "EpochMissing",
        "ProjectRootInvalid",
        "ProjectRootMismatch",
        "OwnershipIncomplete",
        "ExactHandleIncomplete",
        "DuplicateSourceKind",
        "RequiredSourceKindMissing",
    ):
        assert result in LEDGER_HEADER
        assert f'return "{result}"' in LEDGER_SOURCE
    for contract in (
        "NormalizeProjectRoot",
        "ProjectRootsEqual",
        "presenter_root_canonical",
        "epoch_root_canonical",
        "root_equal",
    ):
        assert contract in LEDGER_HEADER
        assert contract in LEDGER_SOURCE


def test_unknown_persistent_content_blocks_a_baseline_claim():
    assert "CountUnknownPersistentFiles" in LEDGER_SOURCE
    baseline_gate = LEDGER_SOURCE[
        LEDGER_SOURCE.index("bool CanReportBaselineMatch") :
        LEDGER_SOURCE.index("bool CaptureSnapshot")
    ]
    for guard in (
        "!record.classification_complete",
        "ContentLifetimeClass::Unknown",
        "record.reset_adapter_id == 0",
        "observed_after_frontend_reset",
        "ownership_mutation_loss != 0",
    ):
        assert guard in baseline_gate


def test_audio_destructive_reset_requires_exact_identity_and_quiescence():
    gate = AUDIO_SOURCE[
        AUDIO_SOURCE.index("bool CanDestructivelyResetEpoch") :
        AUDIO_SOURCE.index("bool VerifyBaseline")
    ]
    assert "!record.exact_handle_available" in gate
    before = AUDIO_RESET[
        AUDIO_RESET.index("content::ContentResetResult ResetBeforeSourceRemoval") :
        AUDIO_RESET.index("content::ContentResetResult RestoreAfterSourceRemoval")
    ]
    quiesce = before.index("quiesce_project_audio(")
    first_custom_release = before.index("release_exact_custom_instance(")
    first_shared_release = before.index("release_exact_shared_reference(")
    assert quiesce < first_custom_release
    assert quiesce < first_shared_release
    assert "record.bank_name" not in before
    assert "record.logical_path" not in before
    assert "record.resolved_source_path" not in before
    assert '"audio_never_unloads_by_name_alone"' in HARNESS.read_text(
        encoding="utf-8"
    )


def test_incomplete_audio_enumeration_is_a_destructive_reset_blocker():
    for guard in (
        "!ledger->baseline_captured",
        "!ledger->enumeration_complete",
        "ledger->dropped_records",
    ):
        assert guard in AUDIO_SOURCE
    assert "baseline_enumerator_complete" in AUDIO_RESET
    assert '"incomplete_audio_enumeration_disables_reset"' in HARNESS.read_text(
        encoding="utf-8"
    )
