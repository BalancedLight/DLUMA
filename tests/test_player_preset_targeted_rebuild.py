from __future__ import annotations

import re
import subprocess
import json
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
CUSTOM = ROOT / "src" / "fixes" / "custom_map_asset_isolation"
DOCS = ROOT / "docs" / "fixes" / "custom_map_asset_isolation"
FIX = (CUSTOM / "CustomMapAssetIsolationFix.cpp").read_text(encoding="utf-8")
HEADER = (CUSTOM / "PlayerPresetTargetedRebuild.h").read_text(encoding="utf-8")
SOURCE = (CUSTOM / "PlayerPresetTargetedRebuild.cpp").read_text(encoding="utf-8")


def _body(start: str, end: str) -> str:
    begin = FIX.index(start)
    return FIX[begin:FIX.index(end, begin)]


def test_native_targeted_rebuild_replay():
    subprocess.run(
        ["cmd.exe", "/d", "/s", "/c",
         "call tools\\build_player_preset_targeted_rebuild_fake_tests.bat"],
        cwd=ROOT, check=True, capture_output=True, text=True,
    )
    completed = subprocess.run(
        [str(ROOT / "build" / "player-preset-targeted-rebuild-fake" /
             "player_preset_targeted_rebuild_fake_harness.exe")],
        cwd=ROOT, check=True, capture_output=True, text=True, timeout=30,
    )
    assert completed.stdout.count("[PASS]") == 14
    assert "player preset targeted rebuild cases passed: 14" in completed.stdout


def test_retired_targeted_prototype_has_no_independent_execution_owner():
    host = (ROOT / "src" / "dlw_host.c").read_text(encoding="utf-8")
    common = (ROOT / "src" / "dlw_common.h").read_text(encoding="utf-8")
    ini = (ROOT / "config" / "dluma.ini").read_text(encoding="utf-8")
    lua = (ROOT / "src" / "dlw_lua54_helper.c").read_text(encoding="utf-8")
    assert 'L"GameFixes", L"CustomMapAssetIsolation", 0' in host
    assert "CustomMapAssetIsolation=0" in ini
    assert "custom_map_asset_isolation_enabled" in common
    assert "ExperimentalPlayerPresetRebuild" not in host
    assert "ExperimentalPlayerPresetRebuild" not in ini
    assert "experimental_player_preset_rebuild" not in common
    assert "ExperimentalPlayerPresetRebuild" not in lua
    assert re.search(
        r"constexpr\s+bool\s+kEnableTargetedPlayerPresetRepair\s*=\s*false",
        FIX,
    )
    load = _body(
        "std::uint8_t __fastcall HookPlayerStateLoad(",
        "void __fastcall HookPlayerStateParseAppearances(",
    )
    assert "kEnableTargetedPlayerPresetRepair" in load
    assert "ProductionResetGameplayParamsCache" in FIX
    assert "ProductionInitializePlayerStateVariables" in FIX
    assert "kEnableTargetedPlayerPresetRepair" in load
    assert "MakePlayerGameplayParamsContentDomainAdapter" in FIX


def test_disabled_hot_fov_plan_cannot_disable_player_preset_routes():
    membership = _body(
        "bool IsDefaultLevelsHookStatus(",
        "void DisableDefaultLevelsHookPlan()",
    )
    for hot_fov_status in (
        "g_player_variables_get_fvalue_status",
        "g_player_fpp_get_default_fov_status",
        "g_player_fpp_get_camera_default_fov_status",
    ):
        assert hot_fov_status in membership
    for player_preset_status in (
        "g_cache_player_level_preset_status",
        "g_rebuild_player_level_preset_status",
        "g_find_player_level_preset_status",
        "g_remove_cached_player_level_presets_status",
        "g_load_variables_from_file_status",
        "g_player_state_remove_cached_presets_status",
        "g_player_state_initialize_variables_status",
        "g_player_state_set_variables_to_level_status",
    ):
        assert player_preset_status not in membership
    assert '"requested-experimental"' not in FIX
    assert "ProductionResetGameplayParamsCache" in FIX
    assert "ProductionInitializePlayerStateVariables" in FIX
    assert '"hook_relay_prepare_failed"' in FIX
    assert '"optional_hook_install_failed"' in FIX


def test_exact_supported_load_and_natural_application_chain_is_validated():
    expected = {
        "kPlayerStateLoadRva": "0x00BBA6F0U",
        "kPlayerStateLoadAfterClearRva": "0x00BBA77FU",
        "kPlayerDiLoadPlayerStateCallRva": "0x00B24DBDU",
        "kPlayerDiLoadPlayerStateReturnRva": "0x00B24DC2U",
        "kPlayerStateClearRva": "0x00BB8E90U",
        "kPlayerStateSetVariablesToLevelRva": "0x00BBD2A0U",
        "kRebuildPlayerLevelPresetRva": "0x0045AA90U",
        "kGameplayParamsContainerGlobalRva": "0x01D07210U",
    }
    for name, value in expected.items():
        assert re.search(rf"{name}\s*=\s*{value}", FIX)
    for callsite in ("0x00BBA77AU", "0x00BBA7FAU", "0x00BB7767U"):
        assert callsite in FIX
    assert "load_extent" in FIX and "0x00BBB401U" in FIX


def test_historical_per_record_mutator_is_isolated_behind_disabled_gate():
    transaction = _body(
        "bool ExecuteTargetedPlayerPresetRebuild(",
        "std::uint8_t __fastcall HookPlayerStateLoad(",
    )
    assert transaction.count("g_original_rebuild_player_level_preset(") == 1
    for forbidden in (
        "g_original_player_state_clear(",
        "g_original_player_state_set_variables_to_level(",
        "g_original_player_state_initialize_variables(",
        "g_original_player_state_remove_cached_presets(",
        "g_original_remove_cached_player_level_presets(",
        "g_original_reload_player_scripts(",
        "g_original_local_client_recreate_representation(",
        "g_original_player_state_parse_appearances(",
        "PerformFrontendContentReset(",
    ):
        assert forbidden not in transaction
    assert FIX.count("g_original_rebuild_player_level_preset(") == 2
    load = _body(
        "std::uint8_t __fastcall HookPlayerStateLoad(",
        "void __fastcall HookPlayerStateParseAppearances(",
    )
    gate = load.index("kEnableTargetedPlayerPresetRepair")
    historical_call = load.index("ExecuteTargetedPlayerPresetRebuild(")
    original_load = load.index("g_original_player_state_load(state, save_context)")
    assert gate < historical_call < original_load


def test_preflight_maps_cache_membership_without_blind_subtraction():
    plan = _body(
        "bool BuildTargetedPlayerPresetRebuildPlan(",
        "bool PlayerPresetMutationScopesIdle(",
    )
    assert "MapDefinitionVectorToValidatedCacheRecord" in plan
    assert "ReadValidatedPresetRecordMetadata" in plan
    assert "CaptureLiveOwners" in plan
    assert "definition_vector - 0x20" not in FIX
    assert "kPresetDefinitionVectorOffset" in SOURCE
    assert "delta % kPresetRecordSize" in SOURCE


def test_original_load_runs_once_after_optional_transaction():
    load = _body(
        "std::uint8_t __fastcall HookPlayerStateLoad(",
        "void __fastcall HookPlayerStateParseAppearances(",
    )
    assert load.count("g_original_player_state_load(state, save_context)") == 1
    rebuild = load.index("ExecuteTargetedPlayerPresetRebuild(")
    original = load.index("g_original_player_state_load(state, save_context)")
    assert rebuild < original
    assert "PlayerStateLoadAfterClear" in FIX
    assert "set_variables_type_mask == 0xFFU" in load
    assert "state_generation_valid" in load


def test_pending_token_expires_on_cancellation_destroy_and_shutdown():
    assert "playerpreset::ExpirePendingRepair(&g_player_preset_pending);" in _body(
        "std::uintptr_t __fastcall HookSessionOnDestroy",
        "void __fastcall HookUnloadLevel",
    )
    playerdi_destroy = _body(
        "void __fastcall HookPlayerDiOnDestroy",
        "std::uint8_t __fastcall HookTutorialPlayerDiOnCreate",
    )
    assert (
        "playerpreset::ExpirePendingRepair(&g_player_preset_pending);"
        not in playerdi_destroy
    )
    assert "dluma_playerdi_ondestroy_observe_call(" in playerdi_destroy
    assert "playerpreset::ExpirePendingRepair(&g_player_preset_pending);" in _body(
        "FixStatus Shutdown()", "FixStatus CurrentStatus()",
    )


def test_targeted_state_is_fixed_capacity_and_hook_safe():
    for token in (
        "kLogicalTypeCount = 8", "PendingPlayerPresetProviderRepair",
        "TargetedRebuildPlan", "TargetRecord records[kLogicalTypeCount]",
        "old_data_source_inactive", "current_epoch_sources_ready",
    ):
        assert token in HEADER
    for forbidden in (
        "std::vector", "std::string", "new ", "malloc(", "WriteFile(",
        "CreateFile", "throw ",
    ):
        assert forbidden not in SOURCE
