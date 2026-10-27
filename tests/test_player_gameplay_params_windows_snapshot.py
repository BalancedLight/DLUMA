from pathlib import Path
import subprocess


ROOT = Path(__file__).resolve().parents[1]
CUSTOM = ROOT / "src" / "fixes" / "custom_map_asset_isolation"
HEADER = CUSTOM / "PlayerGameplayParamsWindowsSnapshot.h"
SOURCE = CUSTOM / "PlayerGameplayParamsWindowsSnapshot.cpp"


def test_native_player_gameplay_params_windows_snapshot_harness():
    subprocess.run(
        [
            "cmd.exe",
            "/d",
            "/s",
            "/c",
            "tools\\build_player_gameplay_params_windows_snapshot_fake_tests.bat",
        ],
        cwd=ROOT,
        check=True,
    )
    subprocess.run(
        [
            str(
                ROOT
                / "build"
                / "player-gameplay-params-windows-snapshot-fake"
                / "player_gameplay_params_windows_snapshot_fake_harness.exe"
            )
        ],
        cwd=ROOT,
        check=True,
    )


def test_exact_windows_layout_contract_is_visible_and_bounded():
    joined = HEADER.read_text(encoding="utf-8") + SOURCE.read_text(
        encoding="utf-8"
    )
    for contract in (
        "kWindowsGameplayParamsContainerGlobalRva",
        "0x01D07210U",
        "kWindowsLocalPlayerStateGlobalRva = 0x01D07698U",
        "0x01D078F0U",
        "kWindowsGameDiCurrentSessionOffset = 0x0508U",
        "kWindowsSessionActiveLevelOffset = 0x00B0U",
        "kWindowsPlayerStateSize = 0x04B0U",
        "kWindowsPlayerStateRowTableOffset = 0x0040U",
        "kWindowsPlayerStateProgressionSelectorOffset = 0x00B0U",
        "kWindowsPlayerStateClassSelectorOffset = 0x00C0U",
        "kWindowsPlayerStatePlayerVariablesOffset = 0x00D0U",
        "kWindowsPlayerStateSkillRootOffset = 0x00D8U",
        "kWindowsPlayerStateSkillCountOffset = 0x00E0U",
        "kWindowsPlayerStateRegularBuffOffset = 0x00F0U",
        "kWindowsPlayerStateTimedBuffOffset = 0x0108U",
        "kWindowsPlayerStateAppearanceSize = 0x003CU",
        "kWindowsPlayerLogicalTypeCount = 8U",
        "kWindowsPlayerRowStride = 0x20U",
        "kWindowsPlayerRowPresetOffset = 0x00U",
        "kWindowsPlayerRowEarnedExperienceOffset = 0x08U",
        "kWindowsPlayerRowUnknown100Offset = 0x10U",
        "kWindowsPlayerRowCurrentLevelOffset = 0x14U",
        "kWindowsPlayerRowMaximumLevelOffset = 0x16U",
        "kWindowsPlayerRowTrailingQwordOffset = 0x18U",
        "kWindowsSkillNodeCapacity = 1024U",
        "kWindowsBuffCapacity = 4096U",
        "kWindowsDefinitionLevelCapacity = 512U",
        "kWindowsPlayerDefinitionLevelStride = 0x28U",
        "kWindowsPlayerDefinitionPropertyStride = 0x20U",
        "kWindowsPlayerDefinitionLevelVectorOffset = 0x00U",
        "kWindowsPlayerDefinitionLevelCountOffset = 0x08U",
        "kWindowsPlayerDefinitionLevelCapacityOffset = 0x0CU",
        "kWindowsPlayerDefinitionPropertyVectorOffset = 0x00U",
        "kWindowsPlayerDefinitionPropertyCountOffset = 0x08U",
        "kWindowsPlayerDefinitionPropertyCapacityOffset = 0x0CU",
        "PlayerDefinitionTraversalFailure",
        "CaptureWindowsPlayerDefinitionSemantics",
        "SemanticFingerprintChanged",
        "WindowsFieldDefinitionSemantics",
        "WindowsSnapshotReasonDefinitionTraversalIncomplete",
        "player_skill_tree_v1",
        "player_regular_buffs_v1",
        "player_timed_buffs_v1",
        "definition_level_count",
        "AbsentPresetRow",
        "AbsentPresetAccepted",
        "dluma_capture_stable_main_menu_player_semantic_snapshot",
        "IsSupportedWindowsPlayerRowStride",
        "player_semantic_snapshot_complete",
        "player_restart_snapshot_complete",
        "first_failed_row_index",
        "first_failed_row_reason",
    ):
        assert contract in joined
    assert "stride == kWindowsPlayerRowStride" in joined
    assert (
        "stride == kWindowsPlayerDefinitionLevelStride"
        in joined
    )
    assert (
        "stride == kWindowsPlayerDefinitionPropertyStride"
        in joined
    )
    assert "kWindowsPlayerDefinitionLevelStride = 0x30U" not in joined
    assert "local_player_di" not in joined
    assert "LocalPlayerDi" not in joined
    assert "restart_barrier_ready" not in joined
    assert "no_session_environment_ready" in joined
    assert "WindowsFieldDefinitionVectorHeaders" in joined
    assert "definition_vector_header_readable" in joined
    assert "379/385" in joined


def test_unresolved_domains_have_no_fake_fingerprint_fields():
    header = HEADER.read_text(encoding="utf-8")
    for unresolved in (
        "WindowsFieldInventory",
        "WindowsFieldEquipment",
        "WindowsFieldQuests",
        "WindowsFieldProviderIdentity",
        "WindowsSnapshotReasonInventoryOwnerUnresolved",
        "WindowsSnapshotReasonEquipmentOwnerUnresolved",
        "WindowsSnapshotReasonQuestOwnerUnresolved",
        "WindowsSnapshotReasonProviderIdentityUnresolved",
    ):
        assert unresolved in header
    for guessed in (
        "inventory_fingerprint",
        "equipment_fingerprint",
        "quest_fingerprint",
        "provider_fingerprint",
    ):
        assert guessed not in header
    assert "restart_snapshot_complete" in header
    assert "output->unresolved_fields == 0" in SOURCE.read_text(
        encoding="utf-8"
    )


def test_snapshot_helper_is_read_only_fixed_capacity_and_no_engine_calls():
    joined = HEADER.read_text(encoding="utf-8") + SOURCE.read_text(
        encoding="utf-8"
    )
    assert "ReadPlayerGameplayParamsWindowsMemoryFn" in joined
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
        "PlayerState::Clear(",
        "RemovePrecachedPlayerLevelPresets(",
        "InitializeVariables(",
        "ReloadPlayerScripts(",
        "RecreateRepresentation(",
        "reset_global_cache(",
        "initialize_variables(",
    ):
        assert forbidden not in joined


def test_snapshot_helper_is_registered_in_the_native_project():
    project = (ROOT / "DLUMA.vcxproj").read_text(encoding="utf-8")
    assert "PlayerGameplayParamsWindowsSnapshot.cpp" in project
    assert "PlayerGameplayParamsWindowsSnapshot.h" in project
