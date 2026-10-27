from pathlib import Path
import os
import subprocess


ROOT = Path(__file__).resolve().parents[1]


def test_native_game_frame_authority_harness():
    command = ["cmd.exe", "/d", "/s", "/c", "tools\\build_game_frame_authority_fake_tests.bat"]
    completed = subprocess.run(
        command,
        cwd=ROOT,
        capture_output=True,
        text=True,
        timeout=180,
        env=os.environ.copy(),
    )
    assert completed.returncode == 0, (
        f"stdout:\n{completed.stdout}\n"
        f"stderr:\n{completed.stderr}"
    )
    assert "game frame authority fake harness passed" in completed.stdout


def test_production_separates_exact_route_from_dispatch_thread():
    common = (ROOT / "src" / "dlw_common.h").read_text(encoding="utf-8")
    engine = (ROOT / "src" / "dlw_engine.c").read_text(encoding="utf-8")
    authority = (
        ROOT / "src" / "dlw_game_frame_authority.h"
    ).read_text(encoding="utf-8")

    for field in (
        "exact_route_game_subobject",
        "exact_route_game_primary",
        "exact_route_caller_module_base",
        "exact_route_caller_rva",
        "exact_route_hook_slot",
        "exact_route_serialization_token",
        "current_dispatch_thread_id",
        "previous_callback_thread_id",
        "scheduler_migration_count",
        "game_frame_authority_proof",
        "game_frame_authority_sequence",
        "game_frame_authority_frozen",
        "game_frame_authority_conflict",
        "game_frame_message_timer_thread_id",
        "game_frame_loader_dispatch_started",
        "game_frame_frontend_identity_latched",
        "game_frame_playtest_level_configured",
        "game_frame_playtest_handoff_validated",
        "game_frame_playtest_handoff_latched",
        "game_frame_project_admitted",
        "game_frame_registry_mutation_started",
    ):
        assert field in authority
    assert "game_frame_thread_id" not in common
    assert "&state->game_frame_thread_id, current_thread, 0" not in engine
    assert "DLUMA_GAME_FRAME_AUTHORITY_MIN_STABLE_CALLBACKS" in authority
    assert "dluma_game_frame_authority_observe_pre" in engine
    required = authority.split(
        "#define DLUMA_GAME_FRAME_AUTHORITY_REQUIRED_PROOF", 1
    )[1].split(
        "typedef enum DlumaGameFrameAuthorityConflictReason", 1
    )[0]
    assert "CURRENT_GLOBAL_IDENTITY" not in required
    assert "SUBSEQUENT_ROUTE_CONFIRMATION" in required
    static = authority.split(
        "#define DLUMA_GAME_FRAME_AUTHORITY_STATIC_PROOF", 1
    )[1].split(
        "#define DLUMA_GAME_FRAME_AUTHORITY_REQUIRED_PROOF", 1
    )[0]
    assert "EXACT_CALLER_ROUTE" in static
    assert "game_frame_authority_window_armed" in authority
    harness = (
        ROOT / "tests" / "game_frame_authority_fake_harness.cpp"
    ).read_text(encoding="utf-8")
    for regression in (
        "TestRoundRobinSerializedRouteLatchesAfterPositiveEvidence",
        "TestValidatedPlaytestHandoffLatchesFirstExactSerializedRoute",
        "TestEightAssignmentsNeverElectAWorker",
        "TestMigratedDispatchAcceptsOnlyIdenticalRoute",
        "TestDifferentCallerFailsClosed",
        "TestDifferentObjectFailsClosed",
        "TestSerializationTokenChangeFailsClosed",
        "TestConcurrentCallbackFailsClosed",
        "TestRecursiveCallbackDoesNotDispatchTwice",
        "TestReadOnlyDiscoveryNeverMarksRegistryMutation",
        "TestValidatedFrontendReplacementRebindsWithoutChangingRoute",
        "TestUnconfirmedFrontendMismatchDoesNotRebind",
        "TestPositiveFrontendReachesInventoryAndClassifier",
        "TestShutdownAcceptsRouteOrWindowOwnerOnly",
    ):
        assert regression in harness
    implementation = (
        ROOT / "src" / "dlw_game_frame_authority.c"
    ).read_text(encoding="utf-8")
    assert "DLUMA_GAME_FRAME_AUTHORITY_MAX_REPEATING_GAP_MS" in implementation
    assert (
        "dluma_game_frame_authority_configure_validated_playtest_handoff"
        in implementation
    )
    playtest_required = authority.split(
        "#define DLUMA_GAME_FRAME_AUTHORITY_PLAYTEST_REQUIRED_PROOF", 1
    )[1].split(
        "typedef enum DlumaGameFrameAuthorityConflictReason", 1
    )[0]
    assert "VALIDATED_PLAYTEST_HANDOFF" in playtest_required
    assert "BOUNDED_REPEATING_ROUTE" not in playtest_required
    assert "ACTIVE_LEVEL_QUERY" not in playtest_required
    assert "observation->serialization_token" in implementation
    assert "observation->sequence <= state->last_route_sequence" in implementation
    status = (
        ROOT / "src" / "dlw_game_frame_authority.c"
    ).read_text(encoding="utf-8").split(
        "DlumaGameFrameAuthorityStatus dluma_game_frame_authority_status", 1
    )[1].split(
        "void dluma_game_frame_authority_mark_loader_dispatch_started", 1
    )[0]
    assert "dluma_game_frame_arm_selection_window" not in status


def test_message_timer_is_read_only_diagnostic_and_shutdown_owner():
    common = (ROOT / "src" / "dlw_common.h").read_text(encoding="utf-8")
    engine = (ROOT / "src" / "dlw_engine.c").read_text(encoding="utf-8")
    ui = (ROOT / "src" / "dlw_ui.c").read_text(encoding="utf-8")
    header = (
        ROOT / "src" / "dlw_game_frame_authority.h"
    ).read_text(encoding="utf-8")
    assert "window_timer_probe_count" in header
    assert "game_frame_window_timer_owner_conflict" in header
    assert "dluma_engine_note_game_frame_message_timer_probe" in common
    assert "dluma_game_frame_authority_note_message_timer_probe" in engine
    timer = ui.split(
        "static VOID CALLBACK dluma_ui_timer_proc", 1
    )[1].split(
        "void dluma_ui_game_thread_tick(", 1
    )[0]
    assert "dluma_engine_readonly_frontend_probe" in timer
    assert "dluma_engine_note_game_frame_message_timer_probe" in timer
    for mutation_capable_call in (
        "dluma_media_service_tick",
        "dluma_localization_game_thread_tick",
        "dluma_lua_runtime_game_thread_pump",
        "dluma_ui_game_thread_tick_impl",
    ):
        assert mutation_capable_call not in timer


def test_candidate_callbacks_do_not_enter_loader_subsystems():
    engine = (ROOT / "src" / "dlw_engine.c").read_text(encoding="utf-8")
    hook = engine.split(
        "static ULONG_PTR __fastcall dluma_game_frame_hook(", 2
    )[-1].split(
        "static void __fastcall dluma_i_level_destructor_hook", 1
    )[0]
    decision = hook.index("decision.run_loader_dispatch")
    native = hook.index("dluma_media_service_tick")
    localization = hook.index("dluma_localization_game_thread_tick")
    ui = hook.index("dluma_ui_game_thread_tick")
    assert decision < native < localization < ui


def test_project_admission_checks_authority_before_b0_state():
    fix = (
        ROOT
        / "src"
        / "fixes"
        / "custom_map_asset_isolation"
        / "CustomMapAssetIsolationFix.cpp"
    ).read_text(encoding="utf-8")
    start = fix.index("dluma_custom_map_project_admission_evidence(")
    end = fix.index(
        'extern "C" void dluma_custom_map_content_game_thread_tick',
        start,
    )
    admission = fix[start:end]
    authority = admission.index(
        "dluma_engine_game_frame_authority_status"
    )
    b0 = admission.index("EvaluateStartupB0ProjectAdmission")
    assert authority < b0
    assert "game_frame_authority_selection_pending" in admission
    assert "game_frame_authority_selection_timeout_restart_required" in admission
    assert "game_frame_authority_ownership_lost_restart_required" in admission


def test_authority_diagnostics_are_bounded_and_reported():
    header = (
        ROOT / "src" / "dlw_game_frame_authority.h"
    ).read_text(encoding="utf-8")
    fix = (
        ROOT
        / "src"
        / "fixes"
        / "custom_map_asset_isolation"
        / "CustomMapAssetIsolationFix.cpp"
    ).read_text(encoding="utf-8")
    assert "DLUMA_GAME_FRAME_AUTHORITY_THREAD_CAPACITY 16U" in header
    assert "DLUMA_GAME_FRAME_AUTHORITY_TRACE_CAPACITY 64U" in header
    for key in (
        "game_frame_thread_aggregate",
        "game_frame_callback_trace",
        "pre_original_query",
        "post_original_query",
        "previous_callback_thread_id",
        "scheduler_migration_count",
        "caller_return",
        "caller_module",
        "caller_rva",
        "callback_duration_qpc",
        "concurrent_depth",
        "recursive_depth",
        "original_invoked",
        "authority_result",
        "game_frame_authority_window_armed",
        "game_frame_authority_initialized_ms",
        "game_frame_authority_timeout_ms",
    ):
        assert key in fix
