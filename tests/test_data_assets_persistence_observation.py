from __future__ import annotations

import re
import subprocess
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
CUSTOM = ROOT / "src" / "fixes" / "custom_map_asset_isolation"
FIX = CUSTOM / "CustomMapAssetIsolationFix.cpp"
LEDGER = CUSTOM / "PersistentConsumerLedger.cpp"
HEADER = LEDGER.with_suffix(".h")


def test_native_targeted_ledger_replay():
    subprocess.run(
        [
            "cmd.exe", "/d", "/s", "/c",
            "call tools\\build_persistent_consumer_ledger_fake_tests.bat",
        ],
        cwd=ROOT,
        check=True,
        capture_output=True,
        text=True,
    )
    completed = subprocess.run(
        [str(ROOT / "build" / "persistent-consumer-fake" /
             "persistent_consumer_ledger_fake_harness.exe")],
        cwd=ROOT,
        check=True,
        capture_output=True,
        text=True,
        timeout=30,
    )
    assert completed.stdout.count("[PASS]") == 10
    assert "persistent consumer ledger cases passed: 10" in completed.stdout


def test_targeted_ledger_is_fixed_capacity_and_io_free():
    header = HEADER.read_text(encoding="utf-8")
    source = LEDGER.read_text(encoding="utf-8")
    assert "kPersistentConsumerCapacity = 256" in header
    assert "PersistentDomainLoss" in header
    assert "DefaultLevels" in header
    assert "AssetsPcFile" in header
    assert "AssetsPcResource" in header
    assert "AudioData" in header
    assert "AudioBank" in header
    for token in (
        "kStagedAssetsSourceCapacity", "kPostSupersessionOpenCapacity",
        "kMaterialReloadCapacity", "kMaterialIdentityCapacity",
        "kProgressionOverlayCapacity", "kPlayerStateSnapshotCapacity",
        "ActiveSuperseded", "PostSupersessionAssetsOpen",
        "MaterialReloadObservation", "ProgressionOverlayObservation",
        "PlayerStateSnapshot",
    ):
        assert token in header
    for forbidden in (
        "std::vector", "std::string", "new ", "malloc(", "fopen(",
        "WriteFile(", "dluma_write_utf8_file(",
    ):
        assert forbidden not in source


def test_exact_player_progression_targets_are_grouped_without_hot_fov_routes():
    source = FIX.read_text(encoding="utf-8")
    expected = {
        "kCachePlayerLevelPresetRva": "0x0045A920U",
        "kRebuildPlayerLevelPresetRva": "0x0045AA90U",
        "kFindPlayerLevelPresetRva": "0x0045AB80U",
        "kRemoveCachedPlayerLevelPresetsRva": "0x0045AC60U",
        "kLoadVariablesFromFileRva": "0x0045ADA0U",
        "kPlayerStateRemoveCachedPresetsRva": "0x00BB84A0U",
        "kPlayerStateInitializeVariablesRva": "0x00BB86E0U",
        "kPlayerStateSetProgressTemplateRva": "0x00BB82D0U",
        "kPlayerStateSetVariablesToLevelRva": "0x00BBD2A0U",
        "kPlayerVariablesGetFValueRva": "0x00BD2100U",
        "kPlayerFppGetDefaultFovRva": "0x00B77130U",
        "kPlayerFppGetCameraDefaultFovRva": "0x00B77290U",
    }
    for name, value in expected.items():
        assert re.search(rf"{name}\s*=\s*{value}", source)
    assert "DisablePlayerProgressionHookPlan" in source
    assert "IsPlayerProgressionHookStatus" in source
    assert "constexpr bool kEnableDefaultLevelsFovObservers = false;" in source
    assert re.search(
        r"if\s*\(\s*!default_plan_ok\s*\|\|\s*"
        r"!kEnableDefaultLevelsFovObservers\s*\)\s*"
        r"DisableDefaultLevelsHookPlan\(\);",
        source,
    )
    assert "unsafe_hot_fov_detours_disabled" in source
    assert "effective_fov_observation=unavailable" in source
    assert "HookPlayerStateSetProgressTemplate" in source
    assert "g_player_progression_trace_available" in source
    assert "g_gamedll_full_profile_valid" in source
    assert "definition_hash_before" in source
    assert "definition_hash_after" in source
    for property_id in (779, 780, 817, 818):
        assert str(property_id) in source
    assert "SetFValue(" not in source
    assert source.count("g_original_reload_player_scripts(") == 1
    # The exact cache owner has one natural observer forwarding site and one
    # registry-owned production replay site.  No other caller may invoke it.
    assert source.count("g_original_remove_cached_player_level_presets(") == 2
    assert "ProductionResetGameplayParamsCache" in source
    plan_start = source.index("void BuildMaterialAndPlayerObservationPlans()")
    live_plan = source[plan_start:source.index("bool AddVtableHook(", plan_start)]
    for unsafe_hook in (
        "HookPlayerVariablesGetFValue", "HookPlayerFppGetDefaultFov",
        "HookPlayerFppGetCameraDefaultFov",
    ):
        assert unsafe_hook not in live_plan
    assert "HookPlayerStateSetVariablesToLevel" in live_plan


def test_assets_pc_watch_and_material_owner_are_exact_observation_only():
    common = (ROOT / "src" / "dlw_common.h").read_text(encoding="utf-8")
    host = (ROOT / "src" / "dlw_host.c").read_text(encoding="utf-8")
    ini = (ROOT / "config" / "dluma.ini").read_text(encoding="utf-8")
    source = FIX.read_text(encoding="utf-8")
    assert "custom_map_asset_isolation_watch_asset" in common
    for index in range(1, 9):
        assert f'L"WatchAsset{index}"' in host
        assert re.search(rf"(?m)^WatchAsset{index}=", ini)
    assert re.search(
        r"MatchWatchedPath\(\s*g_asset_watch_list,\s*logical,", source
    )
    assert re.search(
        r"MatchWatchedPath\(\s*g_asset_watch_list,\s*physical,", source
    )
    assert "assets_pc_resource =\n        g_material_trace_available" in source
    assert "material_observer=exact" in source
    for name, value in (
        ("kMaterialReloadLocalMaterialsRva", "0x00806CC0U"),
        ("kMaterialsPackConstructorRva", "0x0082BA80U"),
        ("kMaterialsPackDestructorRva", "0x0082BC10U"),
        ("kMaterialsPackLoadRva", "0x0082BE30U"),
        ("kMaterialExistingRebindRva", "0x008077C0U"),
    ):
        assert re.search(rf"{name}\s*=\s*{value}", source)
    for field in (
        "kMaterialManagerRegisteredPacksOffset = 0x50U",
        "kMaterialManagerRegisteredPackCountOffset = 0x58U",
        "kMaterialManagerGlobalPackOffset = 0x68U",
        "kMaterialManagerLocalPackOffset = 0x70U",
        "kMaterialsPackCollectionOffset = 0x78U",
        "kMaterialsPackCountOffset = 0x80U",
        "kMaterialsPackPathOffset = 0x1C8U",
    ):
        assert field in source
    assert "DisableMaterialHookPlan" in source
    assert "g_engine_full_profile_valid" in source
    assert "kSupportedEngineSha256" in source
    assert "PostSupersessionAssetsOpen" in source
    for forbidden in ("UnregRpack(", "ReleasePack(", "LoadAsset("):
        assert forbidden not in source


def test_material_candidate_uses_exact_frontend_replacement_contract():
    source = FIX.read_text(encoding="utf-8")
    reload_start = source.index(
        "int __fastcall HookMaterialReloadLocalMaterials"
    )
    reload_end = source.index(
        "bool __fastcall HookMaterialsPackLoad", reload_start
    )
    reload_body = source[reload_start:reload_end]
    destructor_start = source.index(
        "void __fastcall HookMaterialsPackDestructor"
    )
    destructor_end = source.index(
        "bool __fastcall HookMaterialExistingRebind", destructor_start
    )
    destructor_body = source[destructor_start:destructor_end]
    rebind_start = destructor_end
    rebind_end = source.index(
        "bool __fastcall HookAudioScriptLoad", rebind_start
    )
    rebind_body = source[rebind_start:rebind_end]
    load_start = reload_end
    load_end = destructor_start
    load_body = source[load_start:load_end]

    assert "std::uintptr_t local_mp_pack;" in source
    assert "PendingLocalMaterialCandidate" not in source
    assert re.search(
        r"material_scope->local_mp_pack\s*=\s*"
        r"material_scope->new_local_pack\s*;",
        source,
    )
    freeze = source.index("material_first_local_mp_open =")
    pack_capture = source.index(
        "material_scope->local_mp_pack", freeze
    )
    candidate_attach = source.index(
        "material_scope->candidate_recorded = true", pack_capture
    )
    assert freeze < pack_capture < candidate_attach
    assert "material_first_local_mp_open &&" in source[
        pack_capture:candidate_attach
    ]
    assert re.search(
        r"candidate_pack_published\s*=\s*scope->local_mp_pack\s*&&\s*"
        r"scope->local_mp_pack\s*==\s*scope->new_local_pack",
        reload_body,
    )
    assert "exact_frontend_replacement_contract" in reload_body
    assert "scope->old_pack_destructor_entered" in reload_body
    assert "scope->old_pack_destructor_completed" in reload_body
    assert "scope->local_mp_pack_load_completed" in reload_body
    assert "scope->local_mp_pack_load_succeeded" in reload_body
    assert "scope->rebind_attempt_count == scope->rebind_success_count" in reload_body
    assert "!scope->rebind_pack_mismatch" in reload_body
    assert "CandidateRetainingObserver::LocalMaterialManager" in reload_body
    assert "scope->material_manager, scope->local_mp_pack" in reload_body
    assert "NaturallyReplacedByFrontend" in reload_body
    assert "frontend replacement occurs after post-OnDestroy reset" in reload_body
    assert "CandidateRetentionClassification::Retained" not in reload_body
    assert "This is not evidence that the candidate pack was already destroyed" in reload_body
    contract = reload_body.index("exact_frontend_replacement_contract")
    correlation = reload_body.index("CorrelateCandidateOwner")
    classification = reload_body.index("ClassifyCorrelatedCandidate")
    assert contract < correlation < classification
    assert re.search(
        r"scope->old_local_pack\s*==\s*"
        r"reinterpret_cast<std::uintptr_t>\(pack\)",
        destructor_body,
    )
    assert "scope->local_mp_pack_load_completed = true" in load_body
    assert "scope->local_mp_pack_load_succeeded = result" in load_body
    assert "scope->rebind_pack_mismatch = true" in rebind_body


def test_audio_chain_is_exact_optional_and_never_unloads():
    source = FIX.read_text(encoding="utf-8")
    for name, value in (
        ("kAudioScriptLoadRva", "0x00114100U"),
        ("kToolsLoadAudioBanksRva", "0x002948C0U"),
        ("kAudioFmodLoadBanksRva", "0x000FA3B0U"),
        ("kSoundBankManagerLoadRva", "0x00100920U"),
    ):
        assert re.search(rf"{name}\s*=\s*{value}", source)
    assert "manager_cache_hit_return" in source
    assert "DisableAudioHookPlan" in source
    assert "g_audio_trace_available" in source
    assert "baseline_name_collision_provider_unresolved" in source
    assert "SharedBaselineReference" in source
    assert "project_unique_candidate_baseline_inventory_incomplete" in source
    assert "kAudioDirectLoadBanksSlot = 0x0028U" in source
    assert "kAudioScriptLoadBanksSlot = 0x0038U" in source
    assert "kAudioScriptLoadBanksThunkRva = 0x0034CF50U" in source
    assert "audio_script_dispatch_thunk" in source
    assert "0x48,0x8B,0x01,0x48,0xFF,0x60,0x28" in source
    for forbidden in (
        "UnloadAllBanks(", "UnloadAllBanksExcept(",
        "UnloadSourcesExcept(", "UnloadLocalisedAudioBanks(",
        "AudioScript::Clear(", "AudioScript::Reload(",
    ):
        assert forbidden not in source


def test_reporting_finalization_and_independent_completeness_contract():
    source = FIX.read_text(encoding="utf-8")
    start = source.index("bool FinalizeCustomContentReports() noexcept")
    end = source.index("void LogHookPlan() noexcept", start)
    body = source[start:end]
    assert body.index("DrainTraceRecords(true, false)") < body.index(
        "CaptureSnapshot(&g_content_ledger")
    assert body.index("CaptureSnapshot(&g_content_ledger") < body.index(
        "WriteSummaryFile()")
    assert body.index("WriteSummaryFile()") < body.index(
        "WriteContentLedgerReport(false)")
    assert "if (!g_report_finalized)" in body
    assert (
        "g_ledger_snapshot_sequence =\n"
        "            g_trace_records_drained_through_sequence" in body
    )
    assert (
        "g_trace_summary_sequence =\n"
        "            g_trace_records_drained_through_sequence" in body
    )

    orderly_start = source.index(
        "static int FinalizeContentReportsOnGameThread("
    )
    orderly_end = source.index(
        'extern "C" DlumaStartupB0PumpStatus',
        orderly_start,
    )
    orderly = source[orderly_start:orderly_end]
    assert orderly.index("DrainContentDiagnostics()") < orderly.index(
        "FinalizeCustomContentReports()"
    )
    assert orderly.index("FinalizeCustomContentReports()") < orderly.index(
        "DrainColdRebootCriticalDiagnostics(true)"
    )
    assert "!finalized || !diagnostic_reports_finalized" in orderly
    assert "PinLoaderIfNeeded(true)" in orderly
    assert "ActiveConsumerCall" not in orderly
    for field in (
        "report_generation", "report_finalized",
        "trace_records_drained_through_sequence", "ledger_snapshot_sequence",
        "trace_summary_sequence", "default_levels_observation_complete",
        "effective_fov_observation_complete",
        "assets_pc_file_observation_complete",
        "assets_pc_resource_observation_complete",
        "audio_data_observation_complete",
        "audio_bank_observation_complete",
        "staged_assets_file_observation_complete",
        "material_pack_observation_complete",
        "material_resource_identity_complete",
        "player_template_selection_complete",
        "progression_xml_observation_complete",
        "progression_definition_snapshot_complete",
        "material_observer_accepting_events",
        "player_progression_observer_accepting_events",
    ):
        assert field in source


def test_final_reports_share_one_final_sequence():
    source = FIX.read_text(encoding="utf-8")
    finalizer = source.split(
        "static int FinalizeContentReportsOnGameThread(", 1
    )[1].split(
        'extern "C" void dluma_custom_map_content_game_thread_tick', 1
    )[0]
    assert (
        "g_trace_summary_sequence != g_ledger_snapshot_sequence"
        in finalizer
    )
    assert '"report_write_failed"' in finalizer


def test_new_observers_do_not_add_cleanup_authority():
    source = FIX.read_text(encoding="utf-8")
    ledger = LEDGER.read_text(encoding="utf-8")
    for forbidden in (
        "fs::refresh(", "fs::shutdown(", "IGame::FreePack(",
        "AssetManager::UnregRpack(", "AssetManager::ReleasePack(",
        "OnDemandEvictAllFromPack(",
    ):
        assert forbidden not in ledger
    assert "PerformFrontendContentReset(epoch)" not in ledger
    # The production file retains only the pre-existing natural hook names and
    # exact source-reset implementation; targeted observers never call them.
    observer_start = source.index("bool CopyMaterialPackState")
    observer_end = source.index("void* __fastcall HookFsOpen", observer_start)
    observer_code = source[observer_start:observer_end]
    for forbidden in (
        "g_original_fs_remove", "g_original_game_free_pack",
        "g_original_on_demand_evict", "PerformFrontendContentReset(",
        "RemoveAndDestroyPathCreatedSource(",
    ):
        assert forbidden not in observer_code
