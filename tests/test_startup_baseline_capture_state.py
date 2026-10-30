from pathlib import Path
import re
import subprocess


ROOT = Path(__file__).resolve().parents[1]
CUSTOM = ROOT / "src" / "fixes" / "custom_map_asset_isolation"
HEADER = CUSTOM / "StartupBaselineCaptureState.h"
SOURCE = CUSTOM / "StartupBaselineCaptureState.cpp"


def test_native_retryable_startup_b0_state_machine_harness():
    subprocess.run(
        [
            "cmd.exe",
            "/d",
            "/s",
            "/c",
            "tools\\build_startup_baseline_capture_state_fake_tests.bat",
        ],
        cwd=ROOT,
        check=True,
    )
    subprocess.run(
        [
            str(
                ROOT
                / "build"
                / "startup-baseline-capture-state-fake"
                / "startup_baseline_capture_state_fake_harness.exe"
            )
        ],
        cwd=ROOT,
        check=True,
    )


def test_state_machine_has_retryable_and_irreversible_states():
    joined = HEADER.read_text(encoding="utf-8") + SOURCE.read_text(
        encoding="utf-8"
    )
    for state in (
        "NeverSeen",
        "FrontendCandidate",
        "WaitingForStability",
        "CapturingDomains",
        "PartialDiagnostic",
        "CompleteAuthorized",
        "MissedBeforeFirstCustomEpoch",
        "PermanentFailure",
    ):
        assert state in joined
    for close_reason in (
        "ActiveCustomEpoch",
        "ActiveCustomSource",
        "LocalLuaPresent",
        "NonFrontendLevelActive",
    ):
        assert close_reason in joined
    assert "CloseStartupB0Window" in joined
    assert "StartupB0BlockerIsTransient" in joined


def test_startup_report_contract_is_reason_coded_and_complete():
    header = HEADER.read_text(encoding="utf-8")
    for key in (
        "startup_b0_state",
        "startup_b0_attempt_count",
        "startup_b0_transient_retry_count",
        "startup_b0_tick_source",
        "startup_b0_last_diagnostic_tick_source",
        "startup_b0_query_success_count",
        "startup_b0_query_failure_count",
        "startup_b0_frontend_level_pointer",
        "startup_b0_frontend_level_vtable",
        "startup_b0_frontend_pointer_change_count",
        "startup_b0_first_frontend_tick_qpc",
        "startup_b0_stable_since_qpc",
        "startup_b0_first_blocker",
        "startup_b0_last_blocker",
        "startup_b0_frame_route_token",
        "startup_b0_last_dispatch_thread",
        "startup_b0_current_epoch",
        "startup_b0_active_custom_sources",
        "startup_b0_local_lua_states",
        "startup_b0_source_registry_reported_count",
        "startup_b0_source_registry_observed_count",
        "startup_b0_source_inventory_result",
        "startup_b0_player_inventory_authoritative",
        "startup_b0_player_snapshot_result",
        "startup_b0_audio_snapshot_result",
        "startup_b0_global_snapshot_result",
        "startup_b0_frontend_snapshot_result",
        "startup_b0_loader_pak_result",
        "startup_b0_loader_xui_result",
        "startup_b0_ready_domain_mask",
        "startup_b0_required_domain_mask",
    ):
        assert key in header

    source = SOURCE.read_text(encoding="utf-8")
    for reason in (
        "not_game_thread",
        "frontend_not_stable",
        "active_custom_epoch",
        "active_custom_source",
        "local_lua_present",
        "ledger_busy",
        "source_registry_busy",
        "source_registry_changed",
        "source_inventory_incomplete",
        "player_inventory_incomplete",
        "player_snapshot_incomplete",
        "audio_inventory_incomplete",
        "global_data_incomplete",
        "frontend_resources_incomplete",
        "loader_state_incomplete",
    ):
        assert reason in source


def test_domain_readiness_is_incremental_immutable_and_separately_authorized():
    joined = HEADER.read_text(encoding="utf-8") + SOURCE.read_text(
        encoding="utf-8"
    )
    for domain in (
        "VfsSourceInventory",
        "Player",
        "LoaderState",
        "Audio",
        "GlobalData",
        "FrontendResources",
    ):
        assert domain in joined
    assert "if (readiness.ready)" in joined
    assert "AlreadyReady" in joined
    assert "startup_b0_full_b0_captured" in joined
    assert "startup_b0_cold_reboot_baseline_authorized" in joined
    assert "AuthorizeStartupB0RegistryBaseline" in joined
    assert "capture_generation != session->session_generation" in joined


def test_on_frame_route_owns_capture_and_timer_remains_diagnostic():
    source = SOURCE.read_text(encoding="utf-8")
    fix = (CUSTOM / "CustomMapAssetIsolationFix.cpp").read_text(
        encoding="utf-8"
    )
    harness = (
        ROOT / "tests" / "startup_baseline_capture_state_fake_harness.cpp"
    ).read_text(encoding="utf-8")

    assert "sample->tick_source == StartupB0TickSource::OnFrame" in source
    assert "sample->tick_source == StartupB0TickSource::FallbackTimer" in source
    assert "sample->validated_frame_route" in source
    assert "!diagnostics.startup_b0_frame_route_token" in source
    assert (
        "diagnostics.startup_b0_frame_route_token !=\n"
        "                sample->frame_route_token"
        in source
    )
    assert "startup_b0_last_diagnostic_tick_source" in source
    assert "!diagnostics.startup_b0_cold_reboot_baseline_authorized" in source
    assert "cold::StartupB0TickSource::FallbackTimer" in fix
    assert (
        "dluma_engine_game_frame_route_token(&g_dluma.engine)"
        in fix
    )
    assert (
        "sample.validated_frame_route = exact_frame_route"
        in fix
    )
    assert "game_thread_owner == current_thread_id" not in fix
    worker_start = fix.index(
        "dluma_custom_map_asset_trace_active_level_observed("
    )
    worker_end = fix.index('\nextern "C" {', worker_start)
    assert "AdvanceStartupB0Admission" not in fix[worker_start:worker_end]
    assert "TestCompleteDomainsRemainAdmissibleForRegistryRetry" in harness
    assert "TestMigratingOnFrameLatchedIdentityReplay" in harness
    assert "TestTimerRemainsDiagnosticAfterFrameRouteLatch" in harness
    assert "startup_b0_fallback_timer_tick_count == 0" in harness


def test_failed_query_can_complete_a_fresh_latched_frontend_candidate():
    header = HEADER.read_text(encoding="utf-8")
    source = SOURCE.read_text(encoding="utf-8")
    fix = (CUSTOM / "CustomMapAssetIsolationFix.cpp").read_text(
        encoding="utf-8"
    )
    harness = (
        ROOT / "tests" / "startup_baseline_capture_state_fake_harness.cpp"
    ).read_text(encoding="utf-8")

    assert "maximum_candidate_age_qpc" in header
    assert "last_valid_frontend_qpc" in header
    assert "latched_frontend_identity_readable" in header
    assert "use_latched_candidate = true" in source
    assert (
        "sample->qpc - session->last_valid_frontend_qpc <=\n"
        "                session->maximum_candidate_age_qpc" in source
    )
    assert "latched_frontend_identity_readable" in fix
    assert "startup_b0_frequency.QuadPart) * 5U" in fix
    for case in (
        "TestOneSuccessThenFailuresCompletesStability",
        "TestFailedQueryCandidateSafety",
    ):
        assert case in harness


def test_on_frame_requires_positive_read_only_evidence_before_dispatch():
    engine = (ROOT / "src" / "dlw_engine.c").read_text(encoding="utf-8")
    probe_start = engine.index(
        "static void dluma_engine_readonly_routed_probe("
    )
    probe_end = engine.index(
        "static void dluma_engine_validate_pending_frontend_freshness(",
        probe_start,
    )
    probe = engine[probe_start:probe_end]
    # Both frontend and standalone startup routes collect read-only evidence.
    frontend_probe = probe.index("dluma_engine_readonly_frontend_probe(")
    standalone_gate = probe.index("if (!state || !probe ||")
    standalone_probe = probe.index(
        "dluma_engine_query_startup_session_level_identity("
    )
    assert frontend_probe < standalone_gate < standalone_probe
    assert "game_frame_standalone_start_configured" in probe[
        standalone_gate:standalone_probe
    ]

    hook_start = engine.rindex(
        "static ULONG_PTR __fastcall dluma_game_frame_hook("
    )
    hook_end = engine.index(
        "static void __fastcall dluma_i_level_destructor_hook", hook_start
    )
    hook = engine[hook_start:hook_end]
    readonly_probe = hook.index("dluma_engine_readonly_routed_probe(")
    observe = hook.index("dluma_game_frame_authority_observe_pre(")
    dispatch_marker = hook.index(
        "dluma_game_frame_authority_mark_loader_dispatch_started("
    )
    pump = hook.index("dluma_ui_game_thread_tick(")
    assert readonly_probe < observe < dispatch_marker < pump
    assert "&observation.pre_original_probe" in hook[readonly_probe:observe]
    assert "game_frame_thread_id" not in hook
    assert "if (!decision.run_loader_dispatch)" in hook
    assert "DLUMA_STARTUP_B0_ONFRAME_SKIP_AUTHORITY_PENDING" in hook

    source = SOURCE.read_text(encoding="utf-8")
    owner_admission = source.index(
        "sample->tick_source == StartupB0TickSource::OnFrame"
    )
    query_result = source.index("if (!sample->query_succeeded)")
    assert owner_admission < query_result

    harness = (
        ROOT / "tests" / "startup_baseline_capture_state_fake_harness.cpp"
    ).read_text(encoding="utf-8")
    assert "TestOnFrameClaimsRouteBeforeLevelQuerySucceeds" in harness
    assert "startup_b0_query_failure_count == 1" in harness


def test_only_active_serialized_onframe_route_advances_and_drains():
    common = (ROOT / "src" / "dlw_common.h").read_text(encoding="utf-8")
    ui = (ROOT / "src" / "dlw_ui.c").read_text(encoding="utf-8")
    host = (ROOT / "src" / "dlw_host.c").read_text(encoding="utf-8")

    for field in (
        "volatile LONG sequence",
        "LONG publisher_thread",
        "ULONGLONG qpc",
        "ULONGLONG sample_ms",
    ):
        assert field in common

    tick_start = ui.index(
        "static void dluma_ui_game_thread_tick_impl(unsigned int tick_source)\n{"
    )
    tick = ui[tick_start : ui.index("int dluma_ui_start(void)", tick_start)]
    helper_start = ui.index(
        "static int dluma_ui_is_authoritative_game_thread_tick("
    )
    helper = ui[helper_start:tick_start]
    assert "DLUMA_CUSTOM_MAP_TICK_SOURCE_ON_FRAME" in helper
    assert "dluma_engine_game_frame_dispatch_active_on_current_thread" in helper
    assert "DLUMA_CUSTOM_MAP_TICK_SOURCE_FALLBACK_TIMER" not in helper

    authority = tick.index("authoritative_tick =")
    lua_pump = tick.index("dluma_lua_runtime_game_thread_pump()")
    guard = tick.index(
        "dluma_atomic_compare_exchange(&g_dluma_ui_in_tick, 1, 0)"
    )
    drain = tick.index("dluma_custom_map_asset_trace_game_thread_drain()")
    assert authority < lua_pump < guard < drain
    assert "if (authoritative_tick)" in tick[: drain + 1]
    assert "if (!authoritative_tick)" in tick
    assert "if (!g_dluma.engine.ui_exports_ready)" in tick
    assert tick.index("dluma_custom_map_content_game_thread_tick(") < tick.index(
        "if (!g_dluma.engine.ui_exports_ready)"
    )

    publish = tick.index("dluma_ui_publish_level_snapshot(")
    direct_admission = tick.rindex("dluma_custom_map_content_game_thread_tick(")
    consume = tick.index("dluma_read_game_thread_level_snapshot(")
    assert publish < direct_admission < consume
    for required in (
        "level_snapshot.publisher_thread == current_thread",
        "dluma_is_readable(level_snapshot.level_interface,",
        "live_level_vtable == level_snapshot.level_vtable",
        "level_snapshot.sample_ms <= now",
        "now - level_snapshot.sample_ms <= 3000U",
    ):
        assert required in tick
    assert "g_dluma_ui_last_on_frame_level_sequence" not in ui
    assert tick.count("dluma_read_game_thread_level_snapshot(") == 1
    pump_start = ui.index(
        "static int dluma_ui_startup_b0_onframe_pump("
    )
    pump = ui[pump_start : ui.index(
        "static int dluma_ui_is_authoritative_game_thread_tick(",
        pump_start,
    )]
    assert "dluma_engine_query_level_identity(" in pump
    assert "dluma_engine_copy_fresh_latched_frontend(" in pump
    assert "map_name, level, level_vtable, now" in pump
    assert "DLUMA_CUSTOM_MAP_TICK_SOURCE_ON_FRAME, 1" in pump
    assert '"", 0, 0, now, DLUMA_CUSTOM_MAP_TICK_SOURCE_ON_FRAME, 0' in pump
    wrapper_start = ui.index("void dluma_ui_game_thread_tick(")
    wrapper = ui[wrapper_start : ui.index(
        "static void dluma_ui_game_thread_tick_impl", wrapper_start
    )]
    assert wrapper.index("dluma_ui_startup_b0_onframe_pump(") < wrapper.index(
        "dluma_ui_game_thread_tick_impl("
    )
    assert "dluma_custom_map_content_game_thread_tick" not in host


def test_startup_transaction_core_requires_player_and_audio_baselines():
    header = HEADER.read_text(encoding="utf-8")
    fix = (CUSTOM / "CustomMapAssetIsolationFix.cpp").read_text(
        encoding="utf-8"
    )
    registry_header = (CUSTOM / "ContentDomainRestartRegistry.h").read_text(
        encoding="utf-8"
    )
    registry_source = (CUSTOM / "ContentDomainRestartRegistry.cpp").read_text(
        encoding="utf-8"
    )
    harness = (
        ROOT / "tests" / "frontend_content_cold_reboot_fake_harness.cpp"
    ).read_text(encoding="utf-8")

    core_definition = header[
        header.index("kStartupB0TransactionCoreDomainMask") :
        header.index("struct StartupB0AdmissionSample")
    ]
    assert "StartupB0Domain::VfsSourceInventory" in core_definition
    assert "StartupB0Domain::Player" in core_definition
    assert "StartupB0Domain::LoaderState" in core_definition
    assert "StartupB0Domain::Audio" in core_definition
    for optional in (
        "StartupB0Domain::GlobalData",
        "StartupB0Domain::FrontendResources",
    ):
        assert optional not in core_definition
    assert "cold::kStartupB0TransactionCoreDomainMask" in fix
    assert "RestartResult ExtendRegistryBaseline(" in registry_header
    assert "RestartResult ExtendRegistryBaseline(" in registry_source
    assert "cold::ExtendRegistryBaseline(" in fix
    assert "ContentDomainId::PlayerGameplayParams" in fix
    player_status = fix[fix.index("const bool transaction_b0_ready") :]
    player_status = player_status[: player_status.index(
        "if (after_capture.startup_b0_full_b0_captured"
    )]
    assert "g_cold_reboot_registry.baseline_ready_mask" in player_status
    assert "ComponentRuntimeState::RuntimePartial" in player_status
    assert "TestOptionalPlayerBaselineAddsToReadyMaskBeforeFirstEpoch" in harness
    assert "cold::ExtendRegistryBaseline(" in harness
    assert "TestTransactionCoreRequiresPlayerAndAudioBaselines" in (
        ROOT / "tests" / "startup_baseline_capture_state_fake_harness.cpp"
    ).read_text(encoding="utf-8")


def test_startup_state_machine_is_pod_observation_only_and_no_crt():
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
        "PlayerState::Clear(",
        "RemovePrecachedPlayerLevelPresets(",
        "InitializeVariables(",
        "reset_global_cache(",
        "initialize_variables(",
    ):
        assert forbidden not in joined
    project = (ROOT / "DLUMA.vcxproj").read_text(encoding="utf-8")
    assert "StartupBaselineCaptureState.cpp" in project
    assert "StartupBaselineCaptureState.h" in project


def test_production_admission_closes_on_local_lua_and_reports_tick_discrepancy():
    fix = (CUSTOM / "CustomMapAssetIsolationFix.cpp").read_text(
        encoding="utf-8"
    )
    discovery = (ROOT / "src" / "dlw_lua_discovery.c").read_text(
        encoding="utf-8"
    )
    common = (ROOT / "src" / "dlw_common.h").read_text(encoding="utf-8")
    assert "dluma_custom_map_asset_isolation_note_local_lua_active" in common
    assert re.search(
        r"if\s*\(\s*(?:local_)?count\s*!=\s*0\s*\)\s*"
        r"dluma_custom_map_asset_isolation_note_local_lua_active\s*\(\s*\)\s*;",
        discovery,
    )
    assert "g_startup_b0_custom_staging_seen" in fix
    assert "startup_b0_worker_frontend_seen" in fix
    assert "startup_b0_on_frame_frontend_seen" in fix
    assert "startup_b0_worker_seen_but_onframe_not_seen" in fix
    assert "if (frontend_identity_valid && exact_frame_route &&" in fix
    assert "source == cold::StartupB0TickSource::OnFrame" in fix


def test_player_audio_deadline_runs_on_every_authoritative_frame_before_early_exit():
    header = HEADER.read_text(encoding="utf-8")
    source = SOURCE.read_text(encoding="utf-8")
    fix = (CUSTOM / "CustomMapAssetIsolationFix.cpp").read_text(
        encoding="utf-8"
    )
    harness = (
        ROOT / "tests" / "startup_baseline_capture_state_fake_harness.cpp"
    ).read_text(encoding="utf-8")

    assert "retry_started_ms" in header
    assert "attempts_stopped" in header
    assert "dluma_expire_unresolved_player_audio_b0_on_frame" in source
    deadline_call = fix.index("if (ExpireUnresolvedPlayerAudioB0OnFrame(")
    admission_early_exit = fix.index(
        "if (!admission_result.admission_ready &&",
        deadline_call,
    )
    first_domain_capture = fix.index(
        "const std::size_t vfs_domain_index",
        admission_early_exit,
    )
    assert deadline_call < admission_early_exit < first_domain_capture
    assert "GetTickCount64()" in fix[
        fix.rfind("void ", 0, deadline_call):deadline_call
    ]
    assert "player_audio_b0_retry_started=" in fix
    assert "player_audio_b0_attempts_stopped=" in fix
    assert "if (!state.state || !state.generation || !state.owner_slot" in fix
    assert "script.attempted && !script.complete" in fix
    assert "banks.attempted && !banks.complete" in fix
    assert (
        "TestPlayerAudioDeadlineExpiresOnAuthoritativeFrameExactlyOnce"
        in harness
    )
    deadline_fixture = harness[
        harness.index(
            "bool TestPlayerAudioDeadlineExpiresOnAuthoritativeFrameExactlyOnce"
        ):
        harness.index("bool TestProjectAdmissionPolicy")
    ]
    assert "active_level_query_succeeded = false" in deadline_fixture
    assert "kWindowMs = 10000" in deadline_fixture
    assert "kDeadlineMs" in deadline_fixture


def test_source_registry_change_is_retryable_and_post_capture_is_rechecked():
    fix = (CUSTOM / "CustomMapAssetIsolationFix.cpp").read_text(
        encoding="utf-8"
    )
    assert "cold::StartupB0DomainCaptureResult::RetryChanged" in fix
    assert "RegistryChangedDuringCapture" in fix
    assert "post_capture_latched_reason" in fix
    assert "post_capture_epoch_read" in fix
    assert "post_capture_sources" in fix
    assert "post_capture_local_lua" in fix
    assert "CloseIncompleteStartupBaselineCapture" in fix
