from __future__ import annotations

from pathlib import Path
import re


ROOT = Path(__file__).resolve().parents[1]
CUSTOM = ROOT / "src" / "fixes" / "custom_map_asset_isolation"
FIX = CUSTOM / "CustomMapAssetIsolationFix.cpp"
TRACE_H = CUSTOM / "CustomMapAssetTrace.h"
TRACE_CPP = CUSTOM / "CustomMapAssetTrace.cpp"
LEDGER_H = CUSTOM / "CustomContentLedger.h"
LEDGER_CPP = CUSTOM / "CustomContentLedger.cpp"


def _function(source: str, name: str) -> str:
    match = re.search(rf"\b{name}\s*\([^;{{}}]*\)\s*noexcept\s*\{{", source)
    assert match, f"missing {name}"
    start = source.index("{", match.start())
    depth = 0
    for index in range(start, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[match.start() : index + 1]
    raise AssertionError(f"unterminated {name}")


def test_campaign_manager_target_and_persistent_layout_are_exact_build_gated():
    source = FIX.read_text(encoding="utf-8")
    for token in (
        "kSupportedGamedllTimestamp = 0x623397AAU",
        "kSupportedGamedllImageSize = 0x02082000U",
        "kCampaignManagerLoadRva = 0x002D1F10U",
        "kCampaignLoadVtableSlotRva = 0x0155DA88U",
        "kCampaignInterfaceTableOffset = 0x08U",
        "kCampaignInterfaceTableCountOffset = 0x10U",
        "kCampaignInterfaceSelectedIndexOffset = 0x24U",
        "kCampaignStride = 0xA8U",
        "kCampaignMapsOffset = 0x38U",
        "kCampaignLevelNamesOffset = 0x50U",
        "kCampaignMapIdsOffset = 0x68U",
        "kCampaignAfterRespawn0Offset = 0x88U",
        "kCampaignAfterRespawn1Offset = 0x98U",
    ):
        assert token in source
    assert re.search(
        r"using\s+CampaignManagerLoadFn\s*=\s*bool\s*\(__fastcall\*\)\s*\(\s*void\*\s*,\s*const char\*\s*,\s*int\s*\)",
        source,
    )
    assert "g_context.gamedll_base + 0x002D2021U" in source
    assert "g_context.gamedll_base + kParserTextFileRva" in source
    for rva in ("0x002D2266U", "0x002D2274U", "0x002D232EU", "0x002D27BCU"):
        assert rva in source
    assert "campaign_manager_load=exact;scampaign_addmap=exact-inlined-sequence" in source
    assert "campaign_natural_lifecycle=unresolved" in source
    assert "CampaignManagerSlotSnapshot" in TRACE_H.read_text(encoding="utf-8")
    assert "PublishCampaignManagerSlots(" in source


def test_campaign_observer_is_bounded_optional_and_mutation_free():
    source = FIX.read_text(encoding="utf-8")
    hook = _function(source, "HookCampaignManagerLoad")
    assert hook.count("g_original_campaign_manager_load(") == 2
    assert "if (!owns_scope)" in hook
    assert "BuildCampaignStateSnapshot(" in hook
    assert "ClassifyCampaignPersistence(" in hook
    assert "PriorEpochEntriesRemain" in hook
    assert "TryStoreLastCampaignSnapshot(after)" in hook
    assert "PublishCampaignManagerSlots(" in hook
    for forbidden in (
        "CampaignManagerClear(",
        "CampaignManagerReload(",
        "SCampaignClear(",
        "SCampaignDestroy(",
        "fs::refresh(",
        "fs::shutdown(",
    ):
        assert forbidden not in source
    assert "g_campaign_trace_available = campaign_plan_ok" in source
    assert "if (g_fix_enabled)" in source


def test_campaign_fingerprint_is_logical_and_excludes_pointer_metadata():
    trace = TRACE_CPP.read_text(encoding="utf-8")
    fingerprint = _function(trace, "FingerprintCampaignState")
    for logical in (
        "campaign_id",
        "campaign_flags",
        "campaign_name",
        "campaign_source",
        "rewards",
        "after_respawn0",
        "after_respawn1",
        "map_name",
        "level_name",
        "map_id",
    ):
        assert logical in fingerprint
    for pointer_metadata in (
        "selected_campaign",
        "manager",
        "manifest_source",
        "parser_result",
        "capacity",
    ):
        assert pointer_metadata not in fingerprint


def test_release_packs_layout_empty_state_and_natural_dynamic_calls_are_observed():
    source = FIX.read_text(encoding="utf-8")
    hook = _function(source, "HookLevelReleasePacks")
    assert "kSupportedEngineTimestamp = 0x623395D0U" in source
    assert "kSupportedEngineImageSize = 0x00E8D000U" in source
    assert "kReleasePacksRva = 0x002ED420U" in source
    assert "kLevelPackArrayOffset = 0x1000U" in source
    assert "kLevelPackCountOffset = 0x1008U" in source
    assert "std::uint32_t count" in hook
    assert "else if (count == 0)" in hook
    assert "PackCollectionState::Empty" in hook
    assert "scope->collection_complete = true" in hook
    assert "g_original_level_release_packs(level);" in hook
    assert "g_original_on_demand_evict(runtime, pack);" in _function(
        source, "HookOnDemandEvict"
    )
    assert "g_original_internal_pack_release(runtime, pack, release);" in _function(
        source, "HookInternalLevelPackRelease"
    )
    assert "kInternalLevelPackReleaseRva = 0x0046CF60U" in source
    assert "g_context.engine_base + 0x002ED625U" in source


def test_release_labels_keep_on_destroy_transition_annotation_priority():
    hook = _function(FIX.read_text(encoding="utf-8"), "HookLevelReleasePacks")
    on_destroy = hook.index("AssetTraceLabelSource::OnDestroyBinding")
    transition = hook.index("AssetTraceLabelSource::TransitionBinding")
    annotation = hook.index("AssetTraceLabelSource::ActiveMapAnnotation")
    assert on_destroy < transition < annotation
    assert "g_frontend_unload_map" in hook
    assert "g_transition_outgoing_map" in hook


def test_completeness_domains_keep_broad_file_loss_independent():
    header = TRACE_H.read_text(encoding="utf-8")
    trace = TRACE_CPP.read_text(encoding="utf-8")
    ledger_h = LEDGER_H.read_text(encoding="utf-8")
    ledger_cpp = LEDGER_CPP.read_text(encoding="utf-8")
    for field in (
        "ownership_ledger_complete",
        "source_identity_complete",
        "source_reset_evidence_complete",
        "watched_open_observation_complete",
        "parser_observation_complete",
        "campaign_observation_complete",
        "pack_collection_observation_complete",
        "pack_dynamic_observation_complete",
        "broad_file_ledger_complete",
        "trace_event_stream_complete",
        "trace_file_io_complete",
        "summary_coverage_complete",
    ):
        assert field in header or field in trace
    assert "broad_file_observation_loss" in ledger_h
    assert "RecordBroadFileObservationLoss(" in ledger_cpp
    record_file = _function(ledger_cpp, "RecordCustomFileOpen")
    assert "BroadFileObservationContention" in record_file
    assert "BroadFileCapacity" in record_file
    reset = _function(FIX.read_text(encoding="utf-8"), "PerformFrontendContentReset")
    assert "broad_file_observation_loss" not in reset
