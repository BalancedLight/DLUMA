from pathlib import Path
import subprocess


ROOT = Path(__file__).resolve().parents[1]
CUSTOM = ROOT / "src" / "fixes" / "custom_map_asset_isolation"
HEADER = CUSTOM / "AudioStartupBaselineReconciler.h"
SOURCE = CUSTOM / "AudioStartupBaselineReconciler.cpp"


def test_native_audio_startup_baseline_reconciler_harness():
    subprocess.run(
        [
            "cmd.exe",
            "/d",
            "/s",
            "/c",
            "tools\\build_audio_startup_baseline_reconciler_fake_tests.bat",
        ],
        cwd=ROOT,
        check=True,
    )
    completed = subprocess.run(
        [
            str(
                ROOT
                / "build"
                / "audio-startup-baseline-reconciler-fake"
                / "audio_startup_baseline_reconciler_fake_harness.exe"
            )
        ],
        cwd=ROOT,
        check=True,
        capture_output=True,
        text=True,
    )
    assert "audio_startup_baseline_reconciler_fake_harness: 19/19 ok" in (
        completed.stdout
    )


def test_reconciler_is_staging_only_and_has_no_mutation_or_publication():
    joined = HEADER.read_text(encoding="utf-8") + SOURCE.read_text(
        encoding="utf-8"
    )
    for forbidden in (
        "std::vector",
        "std::string",
        "std::map",
        "std::unordered",
        "new ",
        "delete ",
        "malloc(",
        "realloc(",
        "WriteFile(",
        "fopen(",
        "printf(",
        "throw ",
        "try {",
        "catch (",
        "SetBaselineAudioSnapshot(",
        "AddBaselineAudioBank(",
        "UnloadBank(",
        "LoadBank(",
        "ReloadSoundBanks(",
        "UnloadAllBanks(",
        "LoadAudioBanks(",
        "UnloadLocalisedAudioBanks(",
    ):
        assert forbidden not in joined


def test_exact_inputs_and_bounded_frontend_staging_are_explicit():
    header = HEADER.read_text(encoding="utf-8")
    for contract in (
        "AudioBaselineWindowsSnapshot",
        "AudioStartupObservationSnapshot",
        "FilesystemSourceInventory",
        "BaselineAudioSettings settings",
        "BaselineAudioScript scripts[kBaselineAudioScriptCapacity]",
        "BaselineAudioBank fmod_banks[kBaselineAudioBankCapacity]",
        "BaselineSpeechBank speech_banks[kBaselineSpeechBankCapacity]",
        "ObservationalInventoryReconciled",
        "fully_reconciled_observational_inventory",
        "authoritative_b0_complete",
        "reset_ready",
        "reset_owner_proven",
        "exact_source_coverage_complete",
        "fmod_localized_evidence_complete",
        "speech_localized_evidence_complete",
    ):
        assert contract in header


def test_fail_closed_evidence_contract_is_complete():
    joined = HEADER.read_text(encoding="utf-8") + SOURCE.read_text(
        encoding="utf-8"
    )
    for contract in (
        "AudioReconcileReasonObservationLoss",
        "AudioReconcileReasonLateHookCoverage",
        "AudioReconcileReasonHookPlanIncomplete",
        "AudioReconcileReasonProviderUnknown",
        "AudioReconcileReasonProviderPrefixAmbiguous",
        "AudioReconcileReasonOutputCapacityExceeded",
        "AudioReconcileReasonInvalidObservationInterval",
        "AudioReconcileReasonDuplicateObservationEndpoint",
        "AudioReconcileReasonSourceRegistryGenerationMismatch",
        "AudioReconcileReasonScriptDuplicate",
        "AudioReconcileReasonFmodDuplicate",
        "AudioReconcileReasonSpeechDuplicate",
        "AudioReconcileReasonFmodCacheHitWithoutPriorExact",
        "AudioReconcileReasonFmodReloadObservation",
        "AudioStartupObservationLossEmpty",
        "coverage_started_before_audio_singletons",
        "hook_plan_complete",
        "PathWithinRoot",
        "selected_length",
    ):
        assert contract in joined


def test_natural_sequence_not_tree_order_owns_staged_order():
    source = SOURCE.read_text(encoding="utf-8")
    assert "SortMatches(matches, raw.bank_count)" in source
    assert "staged.dependency_order = order + 1U" in source
    assert "SortMatches(matches, raw.speech_bank_count)" in source
    assert "staged.load_order = order + 1U" in source
    assert "raw_index + 1U" not in source


def test_registry_generation_overlap_and_duplicate_occurrence_replays_are_explicit():
    harness = (
        ROOT / "tests" / "audio_startup_baseline_reconciler_fake_harness.cpp"
    ).read_text(encoding="utf-8")
    for replay in (
        "TestSourceRegistryGenerationMismatchRejected",
        "AudioReconcileReasonSourceRegistryGenerationMismatch",
        "TestOverlappingIntervalsLeaveOrderingUnresolved",
        "AudioReconcileUnresolvedDependencyOrdering",
        "TestDuplicateLogicalPathOccurrencesRequireDistinctOrder",
        "output.scripts[0].load_order != output.scripts[1].load_order",
    ):
        assert replay in harness


def test_provider_identity_and_cache_inheritance_are_exact():
    source = SOURCE.read_text(encoding="utf-8")
    assert "observed.source != source.source" in source
    assert "observed.source_registry_generation !=" in source
    assert "sources.registry_generation" in source
    assert "sources.sources[index].source != observed.source" in source
    assert "cache.enter_sequence <=" in source
    assert "exact_observation.exit_sequence" in source
    assert "inherited_fmod_cache_hit_count" in source
    assert "CacheHitProviderMissing" in source
    assert "CacheHitPriorExact" in source
    assert "observed.exact_source ? observed.source : 0" in source
    assert "observed.exact_source\n        ? selected" in source
    assert "observed.exact_source\n        ? source.list_ordinal" in source
    assert "source_inventory_ordinal" in source
    assert "mapped->identity.exact_source = observed.exact_source" in source
    assert "mapped->identity.exact_source = true" not in source


def test_audio_source_identity_uses_frozen_list_position_not_ledger_add_order():
    joined = HEADER.read_text(encoding="utf-8") + SOURCE.read_text(
        encoding="utf-8"
    )
    inventory = (CUSTOM / "FilesystemSourceInventory.h").read_text(
        encoding="utf-8"
    )
    assert "std::uint32_t list_ordinal;" in inventory
    assert "not the content ledger's successful-add ordinal" in inventory
    assert "source.list_ordinal" in joined
    assert "source_inventory_ordinal" in joined
    assert "source_registry_generation" in joined
    assert "source_registry_generation_observed" in joined
    assert "successful_add_ordinal" not in joined


def test_localized_value_is_compared_only_when_observed():
    ledger = (CUSTOM / "AudioStartupObservationLedger.h").read_text(
        encoding="utf-8"
    )
    source = SOURCE.read_text(encoding="utf-8")
    assert "bool localized_observed" in ledger
    assert source.count("localized_observed) {") == 3
    assert "AudioReconcileUnresolvedFmodLocalizedEvidence" in source
    assert "AudioReconcileUnresolvedSpeechLocalizedEvidence" in source


def test_speech_hash_uses_engine_signed_byte_semantics():
    source = SOURCE.read_text(encoding="utf-8")
    assert "static_cast<std::int8_t>(*name++)" in source
    assert "hash * 0x29U" in source
    assert "EngineBankHash(name) != member.bank_hash" in source


def test_no_reference_count_content_bytes_or_instance_generation_invented():
    joined = HEADER.read_text(encoding="utf-8") + SOURCE.read_text(
        encoding="utf-8"
    )
    assert "staged.reference_count = 0" in SOURCE.read_text(encoding="utf-8")
    assert "staged.reference_count_observed = false" in SOURCE.read_text(
        encoding="utf-8"
    )
    assert SOURCE.read_text(encoding="utf-8").count(
        "staged.instance_generation = 0"
    ) == 2
    for unresolved in (
        "AudioReconcileUnresolvedScriptContentBytes",
        "AudioReconcileUnresolvedFmodContentBytes",
        "AudioReconcileUnresolvedFmodReferenceCounts",
        "AudioReconcileUnresolvedFmodInstanceGeneration",
        "AudioReconcileUnresolvedSpeechSynchronization",
        "AudioReconcileUnresolvedSpeechContentBytes",
        "AudioReconcileUnresolvedSpeechReferenceCounts",
        "AudioReconcileUnresolvedSpeechInstanceGeneration",
        "AudioReconcileUnresolvedResetOwner",
        "AudioReconcileUnresolvedExactSourceCoverage",
        "AudioReconcileUnresolvedFmodLocalizedEvidence",
        "AudioReconcileUnresolvedSpeechLocalizedEvidence",
    ):
        assert unresolved in joined
    assert "output->authoritative_b0_complete = false" in joined
    assert "output->reset_ready = false" in joined
    assert "output->reset_owner_proven = false" in joined


def test_reconciler_is_registered_in_native_project():
    project = (ROOT / "DLUMA.vcxproj").read_text(encoding="utf-8")
    assert "AudioStartupBaselineReconciler.cpp" in project
    assert "AudioStartupBaselineReconciler.h" in project
