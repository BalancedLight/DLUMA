from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
CUSTOM = ROOT / "src" / "fixes" / "custom_map_asset_isolation"
UI = (ROOT / "src" / "dlw_ui.c").read_text(encoding="utf-8")
ENGINE = (ROOT / "src" / "dlw_engine.c").read_text(encoding="utf-8")
COMMON = (ROOT / "src" / "dlw_common.h").read_text(encoding="utf-8")
FIX = (CUSTOM / "CustomMapAssetIsolationFix.cpp").read_text(encoding="utf-8")
STATE = (CUSTOM / "StartupBaselineCaptureState.cpp").read_text(
    encoding="utf-8"
)
STATE_HEADER = (CUSTOM / "StartupBaselineCaptureState.h").read_text(
    encoding="utf-8"
)
HARNESS = (
    ROOT / "tests" / "startup_baseline_capture_state_fake_harness.cpp"
).read_text(encoding="utf-8")


def _function(source: str, marker: str, next_marker: str) -> str:
    start = source.index(marker)
    return source[start : source.index(next_marker, start)]


def test_01_b0_accepts_hundreds_of_consecutive_onframe_samples():
    assert "TestHundredsOfConsecutiveOnFrameSamples" in HARNESS
    assert "startup_b0_on_frame_tick_count == 512" in HARNESS
    assert "StartupB0StateRequiresOnFramePump" in STATE


def test_02_ui_exports_unavailable_cannot_stop_the_b0_pump():
    pump = _function(
        UI,
        "static int dluma_ui_startup_b0_onframe_pump(",
        "static int dluma_ui_is_authoritative_game_thread_tick(",
    )
    wrapper = _function(
        UI,
        "void dluma_ui_game_thread_tick(",
        "static void dluma_ui_game_thread_tick_impl(",
    )
    assert "ui_exports_ready" not in pump
    assert "dluma_ui_startup_b0_onframe_pump(" in wrapper
    assert wrapper.index("dluma_ui_startup_b0_onframe_pump(") < wrapper.index(
        "dluma_ui_game_thread_tick_impl("
    )


def test_03_infopanel_unavailable_cannot_stop_the_b0_pump():
    pump = _function(
        UI,
        "static int dluma_ui_startup_b0_onframe_pump(",
        "static int dluma_ui_is_authoritative_game_thread_tick(",
    )
    assert "InfoPanel" not in pump
    assert "g_dluma_info" not in pump
    assert "DLUMA_ENABLE_INFO_PANEL_RUNTIME" not in pump


def test_04_migrating_onframe_route_can_complete_b0_without_timer_dispatch():
    assert "TestMigratingOnFrameLatchedIdentityReplay" in HARNESS
    assert "startup_b0_on_frame_tick_count == 65" in HARNESS
    assert "startup_b0_fallback_timer_tick_count == 0" in HARNESS
    assert "sample->latched_frontend_identity_readable" in STATE
    assert "sample->tick_source == StartupB0TickSource::OnFrame" in STATE


def test_05_worker_only_frontend_never_authorizes_b0():
    worker = _function(
        FIX,
        "dluma_custom_map_asset_trace_active_level_observed(",
        '\nextern "C" {',
    )
    assert "AdvanceStartupB0Admission" not in worker
    assert "g_startup_b0_on_frame_frontend_level" not in worker
    assert "g_startup_b0_worker_frontend_level" in worker


def test_06_worker_onframe_invariant_reports_the_exact_skipped_stage():
    for reason in (
        "onframe_active_level_query_differs",
        "b0_pump_stopped",
        "level_snapshot_stale",
        "query_result_discarded",
        "thread_ownership_validation_failed",
    ):
        assert reason in STATE
    assert "TestWorkerOnFrameInvariantReasons" in HARNESS
    assert "startup_b0_worker_onframe_invariant_failure" in FIX


def test_07_project_launch_is_deferred_while_b0_is_transient():
    assert "StartupB0ProjectAdmissionDisposition::Defer" in STATE
    assert "DLUMA_CUSTOM_PROJECT_ADMISSION_DEFERRED" in FIX
    assert "DLUMA_Workshop_ContentIsolationPreparing" in UI
    assert "workshop_project_launch_deferred" in UI
    button = _function(
        UI,
        "static void __fastcall dluma_ui_workshop_button_hook(",
        "static void __fastcall dluma_ui_workshop_double_hook(",
    )
    assert (
        "/* Stock project admission is a content-isolation boundary, "
        "independent\n"
        "     * of whether the loader-owned registry row UI remains "
        "available. */\n"
        "    else if (!g_dluma_registry_hooks_stopping &&\n"
        "             !g_dluma.stop_requested &&\n"
        "             dluma_ui_is_stock_workshop_screen(screen))"
        in button
    )
    assert (
        "else if (dluma_ui_object_id_equals(button, \"B_Start\") &&\n"
        "                 !dluma_ui_workshop_custom_project_launch_allowed(\n"
        "                     screen,\n"
        "                     DLUMA_PROJECT_LAUNCH_ROUTE_MOUSE_START))"
        in button
    )
    double_click = _function(
        UI,
        "static void __fastcall dluma_ui_workshop_double_hook(",
        "static unsigned char __fastcall "
        "dluma_ui_workshop_uses_3d_rotation_hook(",
    )
    stock_double_click = double_click[
        double_click.index("/* Keep list activation") :
    ]
    assert "g_dluma_registry_runtime_disabled" not in stock_double_click


def test_08_project_launch_proceeds_after_complete_authorized():
    assert "cold::CanAdmitCustomProject(&g_cold_reboot)" in FIX
    assert "DLUMA_CUSTOM_PROJECT_ADMISSION_ALLOWED" in FIX
    assert (
        "if (admission.admission ==\n"
        "            DLUMA_CUSTOM_PROJECT_ADMISSION_ALLOWED)\n"
        "        return 1;" in UI
    )


def test_09_permanent_b0_failure_blocks_launch_with_bounded_summary():
    assert "StartupB0State::PermanentFailure" in FIX
    assert "b0_permanent_source_failure" in FIX
    assert "b0_restart_required" in FIX
    assert "DLUMA_CUSTOM_PROJECT_ADMISSION_BLOCKED_RESTART_REQUIRED" in FIX
    assert "dluma_ui_build_content_isolation_permanent_failure_summary" in UI
    assert "DLUMA_Workshop_ContentIsolationValidationFailed" in UI
    assert "DLUMA_Workshop_PlayerBaselineNotVerified" in UI
    assert "DLUMA_Workshop_SeeLoaderLog" in UI
    assert "See dluma.log for the exact reason." in UI


def test_10_source_inventory_and_classifier_run_after_frontend_stabilizes():
    tick = _function(
        FIX,
        'extern "C" void dluma_custom_map_content_game_thread_tick(',
        'extern "C" const char* dluma_custom_map_asset_trace_status_text(',
    )
    admission = tick.index("cold::AdvanceStartupB0Admission(")
    inventory = tick.index("CaptureStartupFilesystemInventory()")
    classifier = tick.index("PopulateBaselineSourceInventory(")
    registry = tick.index("cold::CaptureColdRebootBaseline(")
    assert admission < inventory < classifier < registry
    assert "CaptureLiveFilesystemSourceRegistryView" in FIX
    assert "StartupFilesystemSourceClassifier" in FIX


def test_11_loader_pak_identity_is_required_and_nonzero():
    assert (
        "g_frontend_startup_baseline.loader.loader_pak_identity"
        in FIX
    )
    assert "complete ? loader_source_identity : 0" in FIX
    assert (
        "loader_pack_snapshot_matches &&\n"
        "            loader_xui_operational && !local_project_lua_count &&\n"
        "            g_frontend_startup_baseline.loader.loader_pak_identity"
        in FIX
    )
    core = STATE_HEADER[
        STATE_HEADER.index("kStartupB0TransactionCoreDomainMask") :
        STATE_HEADER.index("struct StartupB0AdmissionSample")
    ]
    assert "StartupB0Domain::VfsSourceInventory" in core
    assert "StartupB0Domain::LoaderState" in core


def test_12_zero_touch_reconciliation_contract_is_unchanged():
    for token in (
        "g_bf8_expected_registry_touched_mask",
        "g_bf8_observed_registry_touched_mask",
        "g_bf8_touch_reconciliation_failed",
        "expected_registry_touched_mask",
        "observed_registry_touched_mask",
    ):
        assert token in FIX
    assert "registry_touched_mask_mismatch" in FIX


def test_13_required_player_and_audio_baselines_fail_closed():
    assert "StartupB0Blocker::PlayerSnapshotIncomplete" in FIX
    assert "AudioMinimumHookPlanIncomplete" in STATE_HEADER
    assert "AudioMinimumSemanticSnapshotIncomplete" in FIX
    assert "StartupB0DomainCaptureResult::Incomplete" in FIX
    assert "g_bf8_unresolved_domain_mask" in FIX
    assert "all_resolved" in FIX


def test_14_absent_player_b0_uses_nonzero_cache_identity():
    player_capture = FIX[
        FIX.index(
            "if (!g_startup_b0_session.domains[player_domain_index].ready)"
        ) :
        FIX.index("const ULONGLONG audio_capture_now")
    ]
    assert "baseline_gameplay_cache_fingerprint" in player_capture
    assert "baseline_state_count : 0" not in player_capture


def test_15_audio_b0_uses_a_guaranteed_nonzero_semantic_identity():
    audio_capture = FIX[
        FIX.index(
            "if (!g_startup_b0_session.domains[audio_domain_index].ready"
        ) :
        FIX.index("const cold::StartupB0Domain unresolved_domains[]")
    ]
    assert "script.semantic_fingerprint" in audio_capture
    assert "script.semantic_fingerprint ^" not in audio_capture


def test_14_ondestroy_reports_the_persisted_exact_c10_attempt():
    on_destroy = _function(
        FIX,
        "std::uintptr_t __fastcall HookSessionOnDestroy",
        "void __fastcall HookUnloadLevel",
    )
    assert "MatchColdRebootStateFiveScopeSession(session)" in on_destroy
    assert "RecordSessionOnDestroyC10Diagnostics(c10_match)" in on_destroy
    assert "statefive::RejectReasonName(c10_match)" in on_destroy
    assert "armed_cold_reboot_c10_applicable" not in on_destroy
    assert "prepared_transaction_generation != 0" not in on_destroy
    assert '"not_applicable_no_armed_cold_reboot"' not in on_destroy
    assert "c10_diagnostic" in on_destroy


def test_runtime_counter_contract_is_complete():
    for field in (
        "game_frame_hook_call_count",
        "startup_b0_onframe_pump_call_count",
        "startup_b0_onframe_pump_skipped_count",
        "startup_b0_onframe_last_skip_reason",
        "startup_b0_active_level_query_call_count",
        "startup_b0_active_level_query_success_count",
        "startup_b0_active_level_query_null_count",
        "startup_b0_active_level_query_invalid_count",
    ):
        assert field in COMMON
        assert field in FIX
    assert "dluma_engine_query_level_identity" in ENGINE
