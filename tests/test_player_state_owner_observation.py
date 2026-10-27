from __future__ import annotations

import re
import subprocess
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
CUSTOM = ROOT / "src" / "fixes" / "custom_map_asset_isolation"
FIX = (CUSTOM / "CustomMapAssetIsolationFix.cpp").read_text(encoding="utf-8")
HEADER = (CUSTOM / "PlayerStateObservationLedger.h").read_text(encoding="utf-8")
LEDGER = (CUSTOM / "PlayerStateObservationLedger.cpp").read_text(encoding="utf-8")


def test_native_player_state_owner_replay():
    subprocess.run(
        ["cmd.exe", "/d", "/s", "/c",
         "call tools\\build_player_state_observation_fake_tests.bat"],
        cwd=ROOT, check=True, capture_output=True, text=True,
    )
    completed = subprocess.run(
        [str(ROOT / "build" / "player-state-observation-fake" /
             "player_state_observation_ledger_fake_harness.exe")],
        cwd=ROOT, check=True, capture_output=True, text=True, timeout=30,
    )
    assert completed.stdout.count("[PASS]") == 15
    assert "player state observation cases passed: 15" in completed.stdout


def test_every_observation_loss_path_advances_revision():
    loss_fields = (
        "owner_overflow", "vector_overflow", "application_overflow",
        "membership_overflow", "side_effect_overflow",
        "appearance_overflow", "natural_clear_overflow",
    )
    for field in loss_fields:
        match = re.search(
            rf"\+\+ledger->loss\.{field};\s*TouchRevision\(ledger\);",
            LEDGER,
        )
        assert match, f"{field} must invalidate a frozen observation revision"
    acquire = LEDGER[LEDGER.index("bool Acquire("):
                     LEDGER.index("void Release(")]
    assert "ledger->loss.lock_contention" in acquire
    assert "TouchRevision(ledger);" in acquire


def test_ledger_is_fixed_capacity_pod_and_hook_io_free():
    for token in (
        "kOwnerCapacity = 64", "kDefinitionVectorCapacity = 256",
        "kApplicationCapacity = 128", "kMembershipCapacity = 512",
        "kNaturalClearCapacity = 64",
        "LocalProcessGlobal", "CharacterManagerGlobal", "PlayerDi",
        "TutorialPlayerDi", "OtherRttiObserved", "cache_generation",
        "same_selector_provider_changed",
        "provider_change_rebuild_skipped",
        "stale_progression_definition_retained",
        "row_pointer_readable", "vector_header_readable",
        "bounded_partial_fingerprint_available",
        "deep_definition_complete",
    ):
        assert token in HEADER
    for forbidden in (
        "std::vector", "std::string", "new ", "malloc(", "fopen(",
        "WriteFile(", "dluma_write_utf8_file(", "throw ",
    ):
        assert forbidden not in LEDGER


def test_exact_supported_player_targets_and_row_model_are_encoded():
    expected = {
        "kLocalClientRecreateRepresentationRva": "0x0040C830U",
        "kCachePlayerLevelPresetRva": "0x0045A920U",
        "kRebuildPlayerLevelPresetRva": "0x0045AA90U",
        "kFindPlayerLevelPresetRva": "0x0045AB80U",
        "kRemoveCachedPlayerLevelPresetsRva": "0x0045AC60U",
        "kLoadVariablesFromFileRva": "0x0045ADA0U",
        "kReloadPlayerScriptsRva": "0x0045C030U",
        "kPlayerStateSetProgressTemplateRva": "0x00BB82D0U",
        "kPlayerStateRemoveCachedPresetsRva": "0x00BB84A0U",
        "kPlayerStateInitializeVariablesRva": "0x00BB86E0U",
        "kPlayerStateClearRva": "0x00BB8E90U",
        "kPlayerStateParseAppearancesRva": "0x00BBC460U",
        "kPlayerStateSetVariablesToLevelRva": "0x00BBD2A0U",
    }
    for name, value in expected.items():
        assert re.search(rf"{name}\s*=\s*{value}", FIX)
    assert "index * 0x20U" in FIX
    assert "row + 0x14U" in FIX
    assert "row + 0x16U" in FIX
    assert "definition_vectors" in FIX
    assert "result is record+0x20, never a complete preset record" in FIX


def _hook_body(name: str, next_name: str) -> str:
    start = FIX.index(name)
    end = FIX.index(next_name, start)
    return FIX[start:end]


def test_side_effect_hooks_only_call_captured_original_once():
    hooks = {
        "void __fastcall HookPlayerStateClear":
            ("void __fastcall HookPlayerStateParseAppearances",
             "g_original_player_state_clear(state);"),
        "void __fastcall HookPlayerStateParseAppearances":
            ("void* __fastcall HookReloadPlayerScripts",
             "g_original_player_state_parse_appearances(state);"),
        "void* __fastcall HookReloadPlayerScripts":
            ("void __fastcall HookLocalClientRecreateRepresentation",
             "g_original_reload_player_scripts(output, manager)"),
        "void __fastcall HookLocalClientRecreateRepresentation":
            ("void PublishPlayerOwner",
             "g_original_local_client_recreate_representation("),
    }
    for hook, (next_hook, original_call) in hooks.items():
        body = _hook_body(hook, next_hook)
        assert body.count(original_call) == 1
        for forbidden in (
            "fs::refresh", "PerformFrontendContentReset(",
            "g_original_player_state_initialize_variables(",
            "g_original_player_state_remove_cached_presets(",
        ):
            assert forbidden not in body


def test_selector_provider_identity_keeps_no_request_unresolved():
    body = _hook_body(
        "void __fastcall HookPlayerStateSetProgressTemplate",
        "void __fastcall HookCachePlayerLevelPreset",
    )
    assert "playerstate::RecordApplication" in body
    assert "SameSelectorProviderChanged" in body
    assert "SameSelectorProviderChangeRebuildSkipped" in body
    assert "StaleProgressionDefinitionRetained" in body
    assert '"<no-request>"' in body
    assert "provider identity not synthesized" in body


def test_whole_cache_reset_never_dereferences_destroyed_generation():
    body = _hook_body(
        "void __fastcall HookRemoveCachedPlayerLevelPresets",
        "void __fastcall HookLoadVariablesFromFile",
    )
    original = body.index("g_original_remove_cached_player_level_presets")
    assert body.index("CapturePlayerStateRows") < original
    after = body[original:]
    assert "CaptureDefinitionSnapshot" not in after
    assert "pointer comparison only after reset" in after
    assert "destroyed generation never dereferenced" in after


def test_natural_cache_transaction_advances_one_generation_per_outer_owner():
    helper = _hook_body(
        "std::uint64_t BeginPlayerCacheObservationTransaction",
        "void FailProductionPlayerMutationCallback",
    )
    assert "if (!g_player_cache_transaction_depth)" in helper
    assert helper.count("playerstate::AdvanceCacheGeneration(") == 1
    assert "++g_player_cache_transaction_depth" in helper
    assert "if (!--g_player_cache_transaction_depth)" in helper

    append = _hook_body(
        "void __fastcall HookCachePlayerLevelPreset",
        "void __fastcall HookRebuildPlayerLevelPreset",
    )
    append_original = append.index(
        "g_original_cache_player_level_preset(container, primary, secondary);"
    )
    assert append.index("BeginPlayerCacheObservationTransaction(") < append_original
    assert append.index("EndPlayerCacheObservationTransaction();") > append_original

    rebuild = _hook_body(
        "void __fastcall HookRebuildPlayerLevelPreset",
        "std::uintptr_t __fastcall HookFindPlayerLevelPreset",
    )
    rebuild_original = rebuild.index(
        "g_original_rebuild_player_level_preset(preset);"
    )
    assert rebuild.index("BeginPlayerCacheObservationTransaction(") < rebuild_original
    assert rebuild.index("EndPlayerCacheObservationTransaction();") > rebuild_original
    assert "vector.cache_generation = cache_generation;" in rebuild


def test_player_observer_group_is_optional_and_excludes_hot_fov_routes():
    progression_group = _hook_body(
        "bool IsPlayerProgressionHookStatus",
        "void DisablePlayerProgressionHookPlan",
    )
    inventory_group = _hook_body(
        "bool IsPlayerStateInventoryHookStatus",
        "void DisablePlayerStateInventoryHookPlan",
    )
    for token in (
        "g_player_state_clear_status",
        "g_player_state_load_status",
        "g_player_state_set_variables_to_level_status",
        "g_player_state_parse_appearances_status",
        "g_reload_player_scripts_status",
        "g_local_client_recreate_representation_status",
    ):
        assert token in progression_group
    for token in (
        "g_player_state_constructor_status",
        "g_player_state_complete_destructor_status",
        "g_player_di_on_create_status",
        "g_player_di_on_destroy_status",
        "g_tutorial_player_di_on_create_status",
    ):
        assert token in inventory_group
        assert token not in progression_group
    assert "g_player_fpp_get_default_fov_status" not in progression_group
    assert "g_player_fpp_get_camera_default_fov_status" not in progression_group
    assert "false);" in FIX  # observer HookStatus records are non-mandatory


def test_player_reset_uses_only_the_coordinated_exact_production_owner():
    assert "player_gameplay_params=production_exact_targets" in FIX
    assert "ProductionResetGameplayParamsCache" in FIX
    assert "ProductionInitializePlayerStateVariables" in FIX
    assert "PlayerDI Applied" not in FIX
    prohibited_independent_calls = (
        "PlayerState::Clear(",
        "PlayerState::RemovePrecachedLevelPresets(",
        "PlayerState::InitializeVariables(",
        "GameplayParamsManager::ReloadPlayerScripts(",
        "LocalClientDI::RecreateRepresentation(",
    )
    for token in prohibited_independent_calls:
        assert token not in FIX


def test_drain_time_report_enumerates_observed_side_effects():
    assert 'ReportAppend(&builder, " side_effects=")' in FIX
    assert '"player_state_side_effect seq="' in FIX
    assert "g_player_state_snapshot.side_effects[index]" in FIX
    assert "playerstate::SideEffectKindName(item.kind)" in FIX


def test_late_global_owner_promotion_handles_startup_null_slots():
    assert "void ObserveKnownGlobalPlayerStates" in FIX
    observe = _hook_body(
        "void ObserveKnownGlobalPlayerStates",
        "std::uint64_t ObservePlayerState",
    )
    assert "kLocalPlayerStateGlobalRva" in observe
    assert "kCharacterPlayerStateGlobalRva" in observe
    assert "OwnerKind::LocalProcessGlobal" in observe
    assert "OwnerKind::CharacterManagerGlobal" in observe
    wrapper = _hook_body(
        "std::uint64_t ObservePlayerState",
        "void RecordPlayerSideEffect",
    )
    assert "ObserveKnownGlobalPlayerStates(sequence);" in wrapper
    finalize = _hook_body(
        "bool FinalizeCustomContentReports",
        "void LogHookPlan",
    )
    assert finalize.index("ObserveKnownGlobalPlayerStates(") < \
        finalize.index("playerstate::CaptureSnapshot(")


def test_natural_clear_captures_type0_before_and_after_original():
    body = _hook_body(
        "void __fastcall HookPlayerStateClear",
        "void __fastcall HookPlayerStateParseAppearances",
    )
    original = body.index("g_original_player_state_clear(state);")
    assert body.index("&natural_clear.before") < original
    assert body.index("&natural_clear.after") > original
    assert body.count("g_original_player_state_clear(state);") == 1
    assert "RecordNaturalClear" in body
    assert "SameSelectorProviderChangeRebuildSkipped" in body
    assert "StaleProgressionDefinitionRetained" in body
    assert "_ReturnAddress()" in body


def test_report_exposes_exact_application_rows_and_post_clear_result():
    for token in (
        '"player_state_application_row app_seq="',
        '" type0_vector="',
        '" post_selection_clear="',
        '"player_state_natural_clear enter_seq="',
        '"player_state_natural_clear_row enter_seq="',
        '" rebuild_skipped_after_clear="',
        '" stale_type0_after_clear="',
        '" before_combined="',
        '" after_vector="',
        '" cache_resets="',
    ):
        assert token in FIX
