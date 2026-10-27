from __future__ import annotations

import re
import subprocess
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SOURCE = (
    ROOT
    / "src"
    / "fixes"
    / "custom_map_asset_isolation"
    / "CustomMapAssetTrace.cpp"
)
HEADER = SOURCE.with_suffix(".h")
HARNESS = ROOT / "tests" / "custom_map_asset_trace_fake_harness.cpp"
EXPECTED_CASES = 30
ROLLBACK_BARRIER_CASES = (
    "frontend_rollback_barrier_events_preserve_order",
    "frontend_lookup_before_reset_is_visible",
    "boundary_publish_commits_exactly_once",
)
TRACE_V2_CASES = (
    "epoch_native_watched_open_replay",
    "parser_outcome_classifier_is_signal_driven",
    "parser_provenance_does_not_infer_from_pointer_or_hash",
    "release_label_sources_are_explicit",
    "completeness_is_domain_aware",
)
TRACE_RESERVE_CASES = (
    "optional_flood_preserves_critical_reserve",
    "critical_overflow_is_independent",
    "optional_overflow_preserves_transaction_evidence",
)


MANDATORY_EVENT_NAMES = (
    "ChangeActiveLevelEnter",
    "ChangeActiveLevelBranch",
    "ChangeActiveLevelExit",
    "ChangeAfterFadeEnter",
    "ChangeAfterFadePostUnload",
    "ChangeAfterFadeExit",
    "SessionOnDestroyEnter",
    "OnDestroyOwnedUnloadObserved",
    "OutgoingLevelReleaseObserved",
    "SessionOnDestroyOriginalReturned",
    "CancelledPresentationDetected",
    "CancelledCleanupBegin",
    "CancelledCleanupSourceRemoval",
    "CancelledCleanupVerification",
    "CancelledEpochConsumed",
    "FreshPresentationEpochCreated",
    "ChangeAfterFadeAttestation",
    "PendingPostC10TokenPublished",
    "C10OriginalReturned",
    "PostC10SessionSlotSampled",
    "PendingPostC10TokenConsumed",
    "PostC10PreflightBegin",
    "PostC10PlayerPass1",
    "PostC10PlayerPass2",
    "PostC10PlayerAssessment",
    "PostC10AudioAssessment",
    "PostC10TransactionPreparationResult",
    "PostC10PreflightResult",
    "C10ScopeCleared",
    "FrontendSourceResetBegin",
    "FrontendSourceResetEnd",
    "FirstFrontendPackOrModuleLookup",
    "FirstFrontendLookupDisposition",
    "ShowMenuMainDisposition",
    "UnloadLevelEnter",
    "UnloadLevelExit",
    "DestroyActiveLevelEnter",
    "DestroyActiveLevelExit",
    "UpdateDLCContentEnter",
    "UpdateDLCContentExit",
    "LoadLevelEnter",
    "LoadModuleBeginCall",
    "LoadModuleBeginReturn",
    "LoadModuleEndCall",
    "LoadModuleEndReturn",
    "LoadLevelExit",
    "FsAddSourceEnter",
    "FsAddSourceExit",
    "FsRemoveSourceEnter",
    "FsRemoveSourceExit",
    "FsOpenWatchedEnter",
    "FsOpenWatchedResult",
    "GameLoadPackEnter",
    "GameLoadPackExit",
    "GameFreePackEnter",
    "GameFreePackExit",
    "LevelReleasePacksEnter",
    "LevelReleasePacksPack",
    "LevelReleasePacksExit",
    "ActiveLevelObserved",
    "QueueOverflow",
)


def test_native_trace_core_executes_all_mock_cases():
    subprocess.run(
        [
            "cmd.exe",
            "/d",
            "/s",
            "/c",
            "call tools\\build_custom_map_asset_trace_fake_tests.bat",
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
                / "asset-trace-fake"
                / "custom_map_asset_trace_fake_harness.exe"
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
        f"custom map asset trace core cases passed: {EXPECTED_CASES}"
        in completed.stdout
    )
    for case_name in ROLLBACK_BARRIER_CASES:
        assert f"[PASS] {case_name}" in completed.stdout
    for case_name in TRACE_V2_CASES:
        assert f"[PASS] {case_name}" in completed.stdout
    for case_name in TRACE_RESERVE_CASES:
        assert f"[PASS] {case_name}" in completed.stdout


def test_trace_core_is_fixed_capacity_pod_and_has_no_hook_time_io():
    source = SOURCE.read_text(encoding="utf-8")
    header = HEADER.read_text(encoding="utf-8")

    assert "kQueueCapacity = 512" in header
    assert "kCriticalTraceReservedCapacity = 32" in header
    assert "AssetTraceTransactionEvidence" in header
    assert "transaction_evidence_loss" in source
    assert "kMaxWatchedPaths = 8" in header
    assert "std::is_trivial<AssetTraceEvent>" in source
    assert "std::is_standard_layout<AssetTraceEvent>" in source
    assert "thread_local std::uint64_t g_thread_generation" in source
    assert "if (!TryQueueLock())" in source
    assert "QueueOverflow" in source and "overflow_latched" in source
    for forbidden_io in (
        "WriteFile(",
        "CreateFileW(",
        "fopen(",
        "fprintf(",
        "ofstream",
        "dluma_log_",
    ):
        assert forbidden_io not in source


def test_trace_record_and_mandatory_event_vocabulary_are_bounded_and_explicit():
    header = HEADER.read_text(encoding="utf-8")

    for event_name in MANDATORY_EVENT_NAMES:
        assert re.search(rf"(?m)^\s*{event_name},?$", header)
    for field in (
        "std::uint64_t sequence;",
        "std::uint64_t qpc;",
        "std::uint64_t transition_generation;",
        "std::uint32_t thread_id;",
        "AssetTraceEventType type;",
        "std::uintptr_t object;",
        "std::uintptr_t first;",
        "std::uintptr_t second;",
        "std::uint64_t value0;",
        "std::uint64_t value1;",
        "char text0[kEventTextCapacity];",
        "char text1[kEventTextCapacity];",
    ):
        assert field in header
    assert "constexpr std::size_t kEventTextCapacity = 192" in header
    assert "No" in header and "Unknown" in header and "Yes" in header


def test_trace_core_has_no_loader_initiated_cleanup_call_surface():
    source = SOURCE.read_text(encoding="utf-8")
    # Event labels such as GameFreePackEnter are data. A function-style token
    # would be an executable call surface and is prohibited in this core.
    forbidden_calls = (
        r"(?<![A-Za-z0-9_])rem_source\s*\(",
        r"(?<![A-Za-z0-9_])refresh\s*\(",
        r"(?<![A-Za-z0-9_])FreePack\s*\(",
        r"(?<![A-Za-z0-9_])UnregRpack\s*\(",
        r"(?<![A-Za-z0-9_])ReleasePack\s*\(",
        r"(?<![A-Za-z0-9_])OnDemandEvictAllFromPack\s*\(",
    )
    for pattern in forbidden_calls:
        assert not re.search(pattern, source)


def test_formatter_and_summary_contracts_are_explicit():
    source = SOURCE.read_text(encoding="utf-8")
    header = HEADER.read_text(encoding="utf-8")
    harness = HARNESS.read_text(encoding="utf-8")

    for field in (
        '"seq="',
        '"qpc"',
        '"thread"',
        '"gen"',
        '"category"',
        '"event"',
        '"path"',
        '"source"',
    ):
        assert field in source
    assert "script_cache_trace_unresolved" in source
    assert "CleanupBeforeDestinationLookup" in header
    assert "AssetTraceEvidenceOutgoing" in header
    assert "AssetTraceEvidenceDestination" in header
    assert "summary_does_not_invent_cleanup_completion" in harness
    assert "summary_output_incomplete_is_conservative" in harness
    assert "summary_optional_coverage_is_required_for_completion" in harness
    assert "summary_pack_pointer_truncation_is_conservative" in harness
    assert "summary_handles_bounded_worst_case_escaping" in harness
