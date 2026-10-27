from __future__ import annotations

import subprocess
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
CUSTOM = ROOT / "src" / "fixes" / "custom_map_asset_isolation"
HEADER = (CUSTOM / "CandidateRetainingConsumerProvenance.h").read_text(
    encoding="utf-8"
)
SOURCE = (CUSTOM / "CandidateRetainingConsumerProvenance.cpp").read_text(
    encoding="utf-8"
)
HARNESS = (
    ROOT
    / "tests"
    / "candidate_retaining_consumer_provenance_fake_harness.cpp"
).read_text(encoding="utf-8")


def test_native_candidate_provenance_replay():
    subprocess.run(
        [
            "cmd.exe",
            "/d",
            "/s",
            "/c",
            "call tools\\build_candidate_retaining_consumer_provenance_fake_tests.bat",
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
                / "candidate-retaining-provenance-fake"
                / "candidate_retaining_consumer_provenance_fake_harness.exe"
            )
        ],
        cwd=ROOT,
        check=True,
        capture_output=True,
        text=True,
        timeout=30,
    )
    assert completed.stdout.count("[PASS]") == 19
    assert (
        "candidate retaining-consumer provenance cases passed: 19"
        in completed.stdout
    )


def test_candidate_classification_is_two_stage_and_bf8_owned():
    for classification in (
        "Candidate",
        "Ephemeral",
        "NaturallyReplaced",
        "NaturallyReplacedByFrontend",
        "Retained",
        "UnknownAtBarrier",
    ):
        assert classification in HEADER
    for operation in (
        "RecordCandidateRetainingConsumer",
        "CorrelateCandidateRetainingOwner",
        "ClassifyCandidateRetainingConsumer",
        "RecordCandidateDomainObserverCoverage",
        "EvaluateCandidateRetainingConsumersAtBf8",
    ):
        assert operation in HEADER
        assert operation in SOURCE
    assert "BroadFilesystemOpen" in HEADER
    assert "classification requires exact owner correlation" in SOURCE
    assert "exact retention owner unresolved at BF8" in SOURCE
    assert "ContentDomainId::UnclassifiedPersistentConsumers" in SOURCE


def test_true_touches_are_monotonic_and_natural_resolution_is_not_a_touch():
    assert "immutable_true_touch_mask |= bit" in SOURCE
    assert "if (touch->touched) return;" in SOURCE
    retained_branch = SOURCE.index(
        "CandidateRetentionClassification::Retained) {"
    )
    promote = SOURCE.index("PromoteTrueTouch(", retained_branch)
    assert retained_branch < promote
    for case in (
        "natural_replacement_does_not_touch",
        "local_material_contract_must_complete_before_bf8",
        "retained_owner_produces_immutable_true_touch",
        "retained_content_on_demand_touches_exact_domain",
        "natural_record_cannot_clear_prior_true_touch",
        "unknown_at_bf8_remains_unresolved",
        "all_resolved_requires_configured_observer_coverage",
    ):
        assert f'"{case}"' in HARNESS
    assert "decision.unresolved_domain_mask |= DomainBit(record->domain)" in SOURCE
    assert "PromoteTrueTouch(" not in SOURCE[
        SOURCE.index("if (decision.observer_loss_count)") :
        SOURCE.index("void MakeConservativeBusyDecision")
    ]


def test_ledger_is_fixed_capacity_pod_and_hook_safe():
    for capacity in (
        "kCandidateRetainingEpochCapacity = 64",
        "kCandidateRetainingRecordCapacity = 256",
        "kCandidateRetainingLossEpochCapacity = 64",
    ):
        assert capacity in HEADER
    assert "CandidateRetainingConsumerRecord" in HEADER
    assert "CandidateRetainingEpochState" in HEADER
    assert "CandidateDomainTouchProvenance" in HEADER
    assert "std::is_trivial" in HARNESS
    assert "std::is_standard_layout" in HARNESS

    for forbidden in (
        "new ",
        "delete ",
        "malloc(",
        "calloc(",
        "realloc(",
        "std::vector",
        "std::map",
        "std::string",
        "throw ",
        "try {",
        "catch (",
        "Sleep(",
        "MarkDomainTouched(",
        "RunRegistryTransaction(",
        "fs::rem_source",
        "PlayerState::Clear",
        "PlayerState::RemovePrecachedLevelPresets",
    ):
        assert forbidden not in SOURCE


def test_lock_and_capacity_loss_are_nonblocking_and_fail_closed():
    assert "dluma_atomic_compare_exchange(&ledger_->lock, 1, 0)" in SOURCE
    assert "while (dluma_atomic" not in SOURCE
    assert "LatchEpochLoss" in SOURCE
    assert "observer_loss_count" in HEADER
    assert "observer_loss_latched" in HEADER
    for case in (
        "record_capacity_loss_fails_closed",
        "lock_contention_is_nonblocking_and_fails_closed",
        "late_candidate_cannot_reopen_sealed_epoch",
        "repeated_bf8_decision_is_deterministic",
    ):
        assert f'"{case}"' in HARNESS


def test_provenance_compiles_into_loader_without_direct_registry_mutation():
    project = (ROOT / "DLUMA.vcxproj").read_text(
        encoding="utf-8"
    )
    marker = (
        '<ClCompile Include="src\\fixes\\custom_map_asset_isolation\\'
        'CandidateRetainingConsumerProvenance.cpp">'
    )
    start = project.index(marker)
    block = project[start : project.index("</ClCompile>", start)]
    for option in (
        "<CompileAs>CompileAsCpp</CompileAs>",
        "<ExceptionHandling>false</ExceptionHandling>",
        "<RuntimeTypeInfo>false</RuntimeTypeInfo>",
        "<LanguageStandard>stdcpp17</LanguageStandard>",
    ):
        assert option in block
    assert (
        '<ClInclude Include="src\\fixes\\custom_map_asset_isolation\\'
        'CandidateRetainingConsumerProvenance.h" />'
    ) in project

def test_existing_required_domain_rule_remains_mandatory_or_touched():
    registry = (CUSTOM / "ContentDomainRestartRegistry.cpp").read_text(
        encoding="utf-8"
    )
    assert "registry->mandatory_mask | LoadTouchedMask(registry)" in registry
    assert "prepared_required_mask" in registry
