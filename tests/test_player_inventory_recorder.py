import re
import subprocess
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "src" / "fixes" / "custom_map_asset_isolation" / (
    "CustomMapAssetIsolationFix.cpp"
)
RECORDER = ROOT / "src" / "dlw_player_inventory_recorder.c"
RECORDER_HEADER = ROOT / "src" / "dlw_player_inventory_recorder.h"
HARNESS = ROOT / "tests" / "player_inventory_recorder_fake_harness.c"
CRASH = ROOT / "src" / "dlw_crash.c"
PROJECT = ROOT / "DLUMA.vcxproj"
WINDOWS_SNAPSHOT = (
    ROOT
    / "src"
    / "fixes"
    / "custom_map_asset_isolation"
    / "PlayerGameplayParamsWindowsSnapshot.cpp"
)


def test_player_inventory_recorder_native_harness():
    subprocess.run(
        [
            "cmd.exe",
            "/d",
            "/s",
            "/c",
            r"tools\build_player_inventory_recorder_fake_tests.bat",
        ],
        cwd=ROOT,
        check=True,
    )
    subprocess.run(
        [
            str(
                ROOT
                / "build"
                / "player-inventory-recorder-fake"
                / "player_inventory_recorder_fake_harness.exe"
            )
        ],
        cwd=ROOT,
        check=True,
    )


def test_runtime15_release_opt_in_is_build_controlled_and_ondestroy_stays_off():
    project = PROJECT.read_text(encoding="utf-8")
    source = SOURCE.read_text(encoding="utf-8")
    assert re.search(
        r"DlumaRuntime15InventoryDiagnostic[\s\S]*?"
        r"Release\|x64'\">true",
        project,
    )
    assert "DLUMA_ENABLE_RUNTIME15_INVENTORY_DIAGNOSTIC=1" in project
    assert "kEnableRuntime15InventoryDiagnostic" in source
    assert "kEnablePlayerDiOnDestroyDiagnosticObserver" in source
    assert re.search(
        r"DlumaPlayerDiOnDestroyObserverDiagnosticOptIn[\s\S]*?"
        r"== ''\">false",
        project,
    )


def test_exact_windows_inventory_profile_and_minimum_hooks_are_validated():
    source = SOURCE.read_text(encoding="utf-8")
    for token in (
        "kInventoryBaseClearRva = 0x00C5BD50U",
        "kInventoryBaseGetAllItemsRva = 0x00C5C510U",
        "kPlayerInventoryObserverRva = 0x00B45970U",
        "0x0170A708U",
        "0x017DF1A8U",
        "0x017F05C8U",
        "inventory_clear_prefix",
        "inventory_get_all_prefix",
        "AddPointerRoute(",
        "HookRuntime15InventoryObserverRemoved",
    ):
        assert token in source
    for raw_file_offset in (
        "0x01709B08U",
        "0x017DE5A8U",
        "0x017EF9C8U",
    ):
        assert raw_file_offset not in source
    assert "InventoryContainer::GetAllItems recorder" not in source


def test_runtime15_optional_hook_failure_cannot_disable_player_b0_ownership():
    source = SOURCE.read_text(encoding="utf-8")
    plan_start = source.index("bool player_inventory_plan_ok =")
    runtime15_start = source.index(
        "bool runtime15_inventory_plan_ok =", plan_start
    )
    plan_end = source.index("bool player_plan_ok =", runtime15_start)
    player_plan = source[plan_start:runtime15_start]
    runtime15_plan = source[runtime15_start:plan_end]

    for diagnostic_status in (
        "g_inventory_base_clear_status",
        "g_inventory_base_get_all_items_status",
        "g_inventory_observer_slot_statuses",
    ):
        assert diagnostic_status not in player_plan
        assert diagnostic_status in runtime15_plan
    assert "DisablePlayerStateInventoryHookPlan();" not in runtime15_plan
    assert "DisableRuntime15InventoryHookPlan();" in runtime15_plan

    install_start = source.index("void InstallOptionalRoutes() noexcept")
    install_end = source.index(
        "void AppendIdentity(", install_start
    )
    install = source[install_start:install_end]
    assert "bool runtime15_inventory_failed = false;" in install
    assert "IsRuntime15InventoryHookStatus(route->owner)" in install
    assert "DisableRuntime15InventoryHookPlan();" in install


def test_recorder_is_bounded_header_only_and_has_no_engine_writes():
    source = RECORDER.read_text(encoding="utf-8")
    assert "DLUMA_PLAYER_INVENTORY_ROW_COUNT" in (
        ROOT / "src" / "dlw_player_inventory_recorder.h"
    ).read_text(encoding="utf-8")
    assert "DLUMA_PLAYER_INVENTORY_EVENT_CAPACITY" in source
    assert "dluma_player_inventory_capture_vector(" in source
    for forbidden in (
        "WriteProcessMemory(",
        "VirtualProtect(",
        "HeapAlloc(",
        "malloc(",
        "free(",
        "TerminateProcess(",
    ):
        assert forbidden not in source


def test_runtime16_exact_child_profile_is_rtti_gated_and_kind_separated():
    header = RECORDER_HEADER.read_text(encoding="utf-8")
    recorder = RECORDER.read_text(encoding="utf-8")
    production = SOURCE.read_text(encoding="utf-8")
    for token in (
        "DLUMA_PLAYER_INVENTORY_CHILD_ABSENT",
        "DLUMA_PLAYER_INVENTORY_CHILD_ITEM_VECTOR",
        "DLUMA_PLAYER_INVENTORY_CHILD_NON_ITEM_INVENTORY_CHILD",
        "DLUMA_PLAYER_INVENTORY_CHILD_UNKNOWN_OR_CONFLICTING",
        "DLUMA_PLAYER_INVENTORY_VTABLE_AMMO_RVA 0x01804608U",
        "DLUMA_PLAYER_INVENTORY_VTABLE_COLLECTABLE_RVA 0x018046B8U",
        "DLUMA_PLAYER_INVENTORY_VTABLE_LOOSE_ITEMS_RVA 0x01804C88U",
        "DLUMA_PLAYER_INVENTORY_VTABLE_MAIN_RVA 0x01804D38U",
        "DLUMA_PLAYER_INVENTORY_VTABLE_MONEY_RVA 0x01804E08U",
        "DLUMA_PLAYER_INVENTORY_VTABLE_HELLRAID_MONEY_RVA 0x01804EC8U",
        "DLUMA_PLAYER_INVENTORY_VTABLE_REPAIR_PART_RVA 0x01805038U",
        "DLUMA_PLAYER_INVENTORY_VTABLE_SPECIAL_RVA 0x018051F8U",
    ):
        assert token in header
    assert "DLUMA_PLAYER_INVENTORY_EXPECTED_ITEM_VECTOR_ROWS" in header
    assert "DLUMA_PLAYER_INVENTORY_EXPECTED_NON_ITEM_ROWS" in header
    assert "DLUMA_PLAYER_INVENTORY_EXPECTED_ABSENT_ROWS" in header
    assert "g_player_inventory_expected_children" in recorder
    assert "dluma_player_inventory_vtable_rva(" in recorder
    assert "item_vector_contract_applicable" in recorder
    assert "raw_header" in header
    for field in (
        "item_vector_row_count",
        "valid_item_vector_row_count",
        "invalid_item_vector_row_count",
        "non_item_row_count",
        "absent_row_count",
        "unknown_row_count",
        "tree_profile_complete",
        "item_vector_tree_valid",
        "last_complete_tree",
        "last_valid_item_vector_tree",
    ):
        assert field in header
        assert field in recorder
        assert field in production


def test_runtime16_transition_latches_and_immutable_context_are_distinct():
    header = RECORDER_HEADER.read_text(encoding="utf-8")
    recorder = RECORDER.read_text(encoding="utf-8")
    crash = CRASH.read_text(encoding="utf-8")
    for field in (
        "first_any_header_change",
        "first_invalid_transition",
        "first_count_underflow_transition",
        "first_count_greater_than_capacity",
        "first_unreadable_transition",
        "first_header_instability",
        "first_clear_reentrancy_transition",
        "first_invalid_evidence",
    ):
        assert field in header
        assert field in recorder
    assert "event.previous_valid && !event.current_valid" in recorder
    assert "event.previous.count == 0U" in recorder
    assert "event.current.count == UINT32_MAX" in recorder
    assert "DLUMA_PLAYER_INVENTORY_INVALID_CONTEXT_CAPACITY" in recorder
    assert "player_inventory_first_invalid_transition_phase" in crash
    assert "player_inventory_first_invalid_preceding_count" in crash
    assert "player_inventory_first_invalid_following_count" in crash


def test_runtime16_native_harness_covers_profile_latches_and_read_only_hooks():
    harness = HARNESS.read_text(encoding="utf-8")
    for case in (
        "test_exact_five_by_ten_profile",
        "test_unknown_is_incomplete_not_corrupt_vector",
        "test_last_complete_and_valid_item_tree",
        "test_benign_growth_and_clean_clear",
        "test_distinct_invalid_latches",
        "test_reentrancy_and_original_call_contract",
        "test_immutable_context_survives_ring_overwrite",
    ):
        assert case in harness
    assert "tree.item_vector_row_count == 15U" in harness
    assert "tree.non_item_row_count == 25U" in harness
    assert "tree.absent_row_count == 10U" in harness
    assert "memcmp(before, memory.arena, sizeof(before)) == 0" in harness


def test_safe_lifecycle_boundaries_and_direct_crash_state_are_wired():
    source = SOURCE.read_text(encoding="utf-8")
    crash = CRASH.read_text(encoding="utf-8")
    for phase in (
        "DLUMA_PLAYER_INVENTORY_PHASE_STABLE_GAMEPLAY",
        "DLUMA_PLAYER_INVENTORY_PHASE_C08_ENTRY",
        "DLUMA_PLAYER_INVENTORY_PHASE_C08_RETURN",
        "DLUMA_PLAYER_INVENTORY_PHASE_C10_ENTRY",
        "DLUMA_PLAYER_INVENTORY_PHASE_SESSION_ON_DESTROY_ENTRY",
        "DLUMA_PLAYER_INVENTORY_PHASE_UNLOAD_LEVEL_ENTRY",
    ):
        assert phase in source
    assert "dluma_player_inventory_state_five_publish(" in source
    assert "dluma_player_inventory_runtime_snapshot(" in crash
    assert "player_inventory_periodic_report_stale" in crash
    assert "player_inventory_first_transition_phase" in crash


def test_snapshot_and_crash_paths_do_not_mutate_stock_inventory():
    source = SOURCE.read_text(encoding="utf-8")
    crash = CRASH.read_text(encoding="utf-8")
    recorder = RECORDER.read_text(encoding="utf-8")
    combined = source[
        source.index("int Runtime15InventoryRead(") :
        source.index("void __fastcall HookPlayerDiOnCreate(")
    ] + crash[
        crash.index("static void dluma_crash_emit_player_inventory_integrity(") :
        crash.index("static void dluma_crash_emit_playerdi_vector_fault(")
    ] + recorder
    for forbidden in (
        "WriteProcessMemory(",
        "TerminateProcess(",
        "count = capacity",
        "header.count = 0",
        "g_original_player_di_on_destroy(",
    ):
        assert forbidden not in combined


def test_player_gameplay_snapshot_callback_direction_is_read_only():
    source = SOURCE.read_text(encoding="utf-8")
    snapshot = WINDOWS_SNAPSHOT.read_text(encoding="utf-8")
    read_callback = source[
        source.index("bool ReadProductionPlayerMemory(") :
        source.index("bool PlayerObservationLossEmpty(")
    ]
    read_helper = snapshot[
        snapshot.index("bool ReadMemory(") :
        snapshot.index("bool ValidSpan(")
    ]
    assert "return ReadBytes(address, output, size);" in read_callback
    assert (
        "context.read_memory(context.read_memory_user, address, output, size)"
        in read_helper
    )
    assert "production_player_mutation_operations.production_enabled = false" in source
    assert re.search(
        r"kEnableTargetedPlayerPresetRepair\s*=\s*false",
        source,
    )
    assert "ReportAppendRuntime15InventoryIntegrity(&builder);" in source
    assert source.count(
        "ReportAppendRuntime15InventoryIntegrity(&builder);"
    ) == 2
