from __future__ import annotations

import subprocess
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
NATIVE = (ROOT / "src/dlw_native_gameplay.c").read_text(encoding="utf-8")
HELPER = (ROOT / "src/dlw_lua54_helper.c").read_text(encoding="utf-8")
RUNTIME = (ROOT / "src/dlw_lua_runtime.c").read_text(encoding="utf-8")
HEADER = (ROOT / "src/dlw_native_gameplay.h").read_text(encoding="utf-8")
HARNESS = (ROOT / "tests/native_gameplay_fake_harness.c").read_text(encoding="utf-8")

EXPECTED_FAKE_CASES = 66


def test_01_fake_backend_executes_all_named_adapter_cases():
    subprocess.run(
        ["cmd.exe", "/d", "/s", "/c", "call tools\\build_native_gameplay_fake_tests.bat"],
        cwd=ROOT,
        check=True,
        capture_output=True,
        text=True,
    )
    completed = subprocess.run(
        [str(ROOT / "build/native-fake/native_gameplay_fake_harness.exe")],
        cwd=ROOT,
        check=True,
        capture_output=True,
        text=True,
        timeout=30,
    )
    assert completed.stdout.count("[PASS]") == EXPECTED_FAKE_CASES
    assert (
        f"native gameplay fake-backend cases passed: {EXPECTED_FAKE_CASES}"
        in completed.stdout
    )


def test_02_harness_covers_sparse_rejected_and_disconnected_worlds():
    for case in (
        "generation_change_during_scan_restarts_builder",
        "bad_seed_then_sparse_7935",
        "rejected_head_walks_to_valid_tail",
        "disconnected_components",
        "same_pointer_returned_by_two_ids",
        "rejected_cycle_does_not_end_scan",
        "wrong_level_rejected_but_scan_continues",
        "distinct_same_map_owner_without_id_roundtrip_is_accepted",
        "degraded_streaming_index_recovers_after_objects_arrive",
        "direct_seed_reconsiders_early_ownerless_link",
        "not_found_requests_bounded_streaming_refresh",
        "complete_object_rejection_continues",
        "index_capacity_degrades",
    ):
        assert f'{{"{case}",' in HARNESS
    assert "diagnostics.ids_scanned == 65536u" in HARNESS
    assert "diagnostics.rejected_rtti >= 1u" in HARNESS
    assert "diagnostics.accepted_distinct_owner_level >= 1u" in HARNESS
    assert "diagnostics.observed_owner_id_roundtrip_mismatch >= 1u" in HARNESS
    assert "diagnostics.next_cycle_or_cap >= 1u" in HARNESS


def test_02c_streaming_recovery_is_delayed_bounded_and_nonpermanent():
    assert "DLUMA_NATIVE_INITIAL_STREAMING_DELAY_MS" in NATIVE
    assert "DLUMA_NATIVE_RECOVERY_DELAY_MS" in NATIVE
    assert "DLUMA_NATIVE_MAX_RECOVERY_ATTEMPTS" in NATIVE
    assert "dluma_native_maybe_recover_index" in NATIVE
    assert "index_refresh_requested" in NATIVE
    assert '"world_index_recovery_attempt"' in NATIVE
    assert "reconsidered_direct_seeds" in HEADER
    assert "accepted_reconsidered_direct_seeds" in HEADER


def test_02d_world_publication_is_not_blocked_by_secondary_live_calls():
    complete = NATIVE[
        NATIVE.index("static void dluma_native_complete_index") :
        NATIVE.index("static void dluma_native_tick_index")
    ]
    assert "local_enable_get" not in complete
    assert "sensor_inside" not in complete
    assert "dluma_native_revalidate_entry" not in complete
    assert complete.index("DLUMA_NATIVE_READY_WORLD_FIND") < complete.index(
        '"world_index_ready"'
    )
    assert complete.index('"world_index_ready"') < complete.index(
        "dluma_native_interactive_hook_current"
    )


def test_02e_per_use_revalidation_skips_optional_owner_id_diagnostic():
    owner_check = NATIVE[
        NATIVE.index("static int dluma_native_object_owner_matches_active_map") :
        NATIVE.index("static int dluma_native_revalidate_entry")
    ]
    assert owner_check.index("id_roundtrip_mismatch_out &&") < owner_check.index(
        "g_dluma_native.backend.object_by_id &&"
    )


def test_02f_frame_interaction_health_checks_never_wait_for_lifecycle_locks():
    refresh = NATIVE[
        NATIVE.index("static void dluma_native_refresh_interaction_capability") :
        NATIVE.index("static DWORD dluma_native_image_size_for_base")
    ]
    current = NATIVE[
        NATIVE.index("static int dluma_native_interactive_hook_current(void)\n{") :
        NATIVE.index("static int dluma_native_interactive_hook_owned_locked(void)\n{")
    ]
    assert "dluma_native_try_lock(&g_dluma_interactive_lifecycle_lock)" in refresh
    assert "dluma_native_lock(&g_dluma_interactive_lifecycle_lock)" not in refresh
    assert "dluma_native_try_lock(&g_dluma_native.match_lock)" in refresh
    assert "dluma_native_try_lock(&g_dluma_interactive_lifecycle_lock)" in current


def test_02g_msvc_pointer_load_never_writes_read_only_engine_vtables():
    win32 = "\n".join(
        (ROOT / "src" / name).read_text(encoding="utf-8")
        for name in (
            "dlw_win32.h",
            "dlw_win32_base.h",
            "dlw_win32_extensions.h",
        )
    )
    msvc = win32.split("#elif defined(_MSC_VER)", 1)[1].split(
        "#else\n#error Unsupported compiler", 1
    )[0]
    pointer_load = msvc.split(
        "static __forceinline PVOID dluma_atomic_pointer_load", 1
    )[1].split(
        "static __forceinline PVOID dluma_atomic_pointer_exchange", 1
    )[0]
    assert "return *target;" in pointer_load
    assert "dluma_atomic_pointer_compare_exchange" not in pointer_load
    assert "_InterlockedCompareExchange64" not in pointer_load


def test_02b_fake_objects_model_independent_native_layout_evidence():
    for field in (
        "name_valid",
        "rtti_valid",
        "dynamic_class",
        "gameplay_rtti_sentinel",
        "engine_rtti_sentinel",
        "base_class_flags",
        "complete_object",
        "complete_object_valid",
        "igsobject_subobject_offset",
        "sensor_area_subobject_offset",
        "primary_vtable_identity",
        "checkinside_slot_offset",
        "checkinside_target_identity",
        "checkinside_target_valid",
        "local_enable_subobject_offset",
        "previous",
        "next",
        "level",
        "generation",
    ):
        assert field in HARNESS
    assert "FAKE_GAMEPLAY_RTTI_SENTINEL 0x00276FF0ULL" in HARNESS
    assert "FAKE_ENGINE_RTTI_SENTINEL 0x00277000ULL" in HARNESS
    assert "FAKE_VTABLE_SENSOR_AREA 0x0188EEC8ULL" in HARNESS
    assert "FAKE_VTABLE_SENSOR_GOTO 0x017A1798ULL" in HARNESS
    assert "FAKE_VTABLE_ACTION_CHAIN 0x0195B938ULL" in HARNESS
    assert "FAKE_CHECKINSIDE_SLOT 0x0310u" in HARNESS
    assert "FAKE_CHECKINSIDE_TARGET_SENTINEL 0x00ECB880ULL" in HARNESS
    assert "FAKE_ILOCAL_ENABLE_OFFSET 0x00F8u" in HARNESS


def test_03_correct_dynamic_rtti_slot_is_validated_fail_closed():
    assert "DLUMA_NATIVE_IGSOBJECT_GET_RTTI_SLOT 0x0010UL" in NATIVE
    assert "DLUMA_NATIVE_IGSOBJECT_GET_ENGINE_RTTI_SLOT 0x0018UL" in NATIVE
    assert "DLUMA_NATIVE_IGSOBJECT_GET_RTTI_RVA 0x00276FF0UL" in NATIVE
    assert "DLUMA_NATIVE_IGSOBJECT_GET_ENGINE_RTTI_RVA 0x00277000UL" in NATIVE
    assert "g_dluma_get_rtti_prefix" in NATIVE
    assert "dluma_native_decode_gameplay_rtti_getter" in NATIVE
    assert "code[1] != 0x8D" in NATIVE
    assert "rtti == expected_rtti ? rtti : 0" in NATIVE
    assert "derived_get_rtti_lea_stub_is_accepted" in HARNESS
    assert "get_engine_rtti_mov_stub_is_rejected" in HARNESS
    assert "get_rtti_target_outside_readable_image_is_rejected" in HARNESS
    live_getter = NATIVE[
        NATIVE.index("static void* dluma_native_prod_igsobject_gameplay_rtti") :
        NATIVE.index("static int dluma_native_prod_object_class")
    ]
    assert "DLUMA_NATIVE_IGSOBJECT_GET_RTTI_RVA" not in live_getter
    assert "dluma_native_known_image_for_code" in live_getter
    assert "dluma_native_decode_gameplay_rtti_getter" in live_getter
    assert "DLUMA_NATIVE_DYNAMIC_RTTI_SLOT" not in NATIVE


def test_04_fingerprint_mismatch_is_fail_closed():
    assert "native_adapter_fingerprint_mismatch" in NATIVE
    mismatch = NATIVE.index("native_adapter_fingerprint_mismatch")
    readiness = NATIVE.index("backend->readiness = DLUMA_NATIVE_READY_WORLD_FIND")
    assert mismatch < readiness
    assert "g_dluma_native.started = 1" in NATIVE[mismatch:readiness]


def test_05_index_has_explicit_phases_full_scan_and_diagnostics():
    for phase in (
        "DLUMA_NATIVE_INDEX_UNINITIALIZED",
        "DLUMA_NATIVE_INDEX_SCANNING_IDS",
        "DLUMA_NATIVE_INDEX_COMPLETE",
        "DLUMA_NATIVE_INDEX_DEGRADED",
    ):
        assert phase in HEADER and phase in NATIVE
    assert "DLUMA_NATIVE_SEED_SCAN_MAX 65535u" in NATIVE
    assert "DLUMA_NATIVE_SEED_SCAN_BATCH" in NATIVE
    assert "ids_scanned" in HEADER and "nonnull_id_results" in HEADER
    assert "accepted_entries" in HEADER and "rejected_rtti" in HEADER
    assert "published_entries" in HEADER
    assert '"world_index_not_ready"' in NATIVE


def test_06_raw_pointer_visitation_is_independent_of_acceptance():
    builder = NATIVE[NATIVE.index("static int dluma_native_build_index_from_seed") :]
    assert builder.index("dluma_native_mark_visited(seed") < builder.index(
        "dluma_native_add_index_entry(seed"
    )
    walker = NATIVE[NATIVE.index("static int dluma_native_walk_component_direction") :]
    assert walker.index("dluma_native_mark_visited(linked") < walker.index(
        "dluma_native_add_index_entry(linked"
    )
    assert "object_prev" in HEADER and "object_next" in HEADER
    assert "rejected_capacity" in HEADER


def test_07_capability_lifecycle_is_dynamic_but_api_support_is_stable():
    for state in (
        "DLUMA_NATIVE_CAP_STATIC_UNSUPPORTED",
        "DLUMA_NATIVE_CAP_STATIC_VALIDATED",
        "DLUMA_NATIVE_CAP_WAITING_FOR_MAP",
        "DLUMA_NATIVE_CAP_LIVE_SELF_TESTING",
        "DLUMA_NATIVE_CAP_READY",
        "DLUMA_NATIVE_CAP_DEGRADED",
    ):
        assert state in HEADER
    assert "dluma_native_gameplay_supported_capabilities" in HEADER
    assert "dluma_native_gameplay_capability_state" in HEADER
    assert "dluma_native_gameplay_capabilities" in HEADER


def test_08_only_validated_native_adapters_are_advertised():
    block = RUNTIME[
        RUNTIME.index("static uint32_t dluma_lua_runtime_capabilities_for_script") :
        RUNTIME.index("static int dluma_lua_runtime_create_slot")
    ]
    assert "dluma_native_gameplay_supported_capabilities()" in block
    for bit in (
        "WORLD_FIND", "WORLD_LOCAL_ENABLE", "EVENTS_SENSOR", "EVENTS_INTERACT",
        "UI_SUBTITLES", "AUDIO_RADIO_DIALOG", "ACTION_CHAIN",
    ):
        assert f"DLUMA_LUA54_CAP_{bit}" in block
    helper_world = HELPER[
        HELPER.index("static int dluma_helper_world_find") :
        HELPER.index("static void dluma_helper_push_gate_closure")
    ]
    assert "binding_not_implemented" not in helper_world


def test_08_world_position_logs_only_validated_ready_or_failed_state():
    validation = NATIVE[
        NATIVE.index("if (dluma_native_validate_world_position_adapter())") :
        NATIVE.index("if (dluma_native_validate_local_adapter())")
    ]
    assert '"world_position_ready"' in validation
    assert '"world_position_failed"' in validation
    assert validation.index("DLUMA_NATIVE_READY_WORLD_POSITION") < validation.index(
        '"world_position_ready"'
    )


def test_08a_native_quest_control_binds_only_the_typed_synchronized_route():
    assert "DlumaGameQuestRequest" in HEADER
    assert "DlumaLua54QuestRequest" not in HEADER
    assert "DLUMA_NATIVE_READY_QUEST_MUTATION" in HEADER
    assert "dluma_native_gameplay_quest_mutate" in HEADER
    assert '"quests.native", "quest.write"' in NATIVE
    assert '"unsupported_capability"' in NATIVE
    production = NATIVE[
        NATIVE.index("static int dluma_native_prepare_production_backend") :
        NATIVE.index("int dluma_native_gameplay_start(void)")
    ]
    for token in (
        "DLUMA_NATIVE_QUEST_MANAGER_RVA",
        "DLUMA_NATIVE_QUEST_FIND_RVA",
        "DLUMA_NATIVE_QUEST_FIND_PHASE_RVA",
        "DLUMA_NATIVE_QUEST_START_SYNCHRONIZED_INVOKE_RVA",
        "dluma_native_validate_quest_adapter()",
        "backend->quest_mutate = dluma_native_prod_quest_mutate",
    ):
        assert token in production or token in NATIVE
    adapter = NATIVE[
        NATIVE.index("static int dluma_native_prod_quest_mutate"):
        NATIVE.index(
            "static DlumaNativeTtlString dluma_native_ttl_string_view(const char* text)\n{",
            NATIVE.index("static int dluma_native_prod_quest_mutate"),
        )
    ]
    assert "quest_start_synchronized_invoke" in adapter
    assert "manager, phase, 1, 0, 1" in adapter
    assert "request->operation != DLUMA_LUA54_QUEST_JUMP_TO_PHASE" in adapter
    assert "quest_coop_not_supported" in NATIVE
    assert "unsupported_quest_operation" in adapter
    assert "AdvanceToQuest(" not in adapter
    assert '"quest_mutation_is_typed_and_fail_closed"' in HARNESS


def test_09_duplicate_exact_names_and_expected_class_are_enforced():
    assert '"duplicate_object_name"' in NATIVE
    assert "match_count != 1" in NATIVE
    assert '"wrong_object_class"' in NATIVE
    assert "backend.object_is_a" in NATIVE
    assert '"duplicate_exact_names_rejected"' in HARNESS
    assert '"wrong_expected_class"' in HARNESS


def test_10_handles_copy_metadata_and_bind_owner_generation():
    assert "owner_id" in HEADER and "map_generation" in HEADER
    assert '"foreign_object_handle"' in NATIVE
    assert '"stale_object_handle"' in NATIVE
    assert "dluma_native_revalidate_entry" in NATIVE
    assert "module base" not in HEADER.lower()
    for case in (
        "handle_copies_metadata",
        "foreign_handle_rejected",
        "stale_generation_handle",
        "handle_revalidation_failure",
        "handle_capacity_fails_closed",
        "world_find_epoch_change_rejects_handle_commit",
    ):
        assert f'{{"{case}",' in HARNESS


def test_11_derived_sensor_uses_resolved_base_and_exact_checkinside():
    assert "sensor_area_interface" in HEADER
    assert "backend.sensor_area_interface" in NATIVE
    assert "DLUMA_NATIVE_SENSOR_CHECK_INSIDE_RVA 0x00ECB880UL" in NATIVE
    assert "DLUMA_NATIVE_SENSOR_CHECK_RVA 0x00ECB720UL" in NATIVE
    assert "DLUMA_NATIVE_SENSOR_CHECK_INSIDE_SLOT 0x0310UL" in NATIVE
    assert '"sensor_base_unavailable"' in NATIVE
    assert '"sensor_checkinside_unvalidated"' in NATIVE
    assert '"derived_sensor_and_base_interface"' in HARNESS


def test_12_first_sensor_sample_is_baseline_and_names_poll_once():
    sensor = NATIVE[NATIVE.index("static void dluma_native_tick_sensors") :]
    commit = NATIVE[NATIVE.index("static int dluma_native_commit_sensor_sample") :]
    assert "if (!was_initial)" in commit
    assert commit.index("if (!was_initial)") < commit.index(
        "sensor_enter_edge_queued"
    )
    assert "changed = was_initial && (prior_inside != inside);" in commit
    assert "polled_names[DLUMA_NATIVE_MAX_SUBSCRIPTIONS]" in sensor
    assert "if (already_polled) continue;" in sensor
    assert '"sensor_failed_poll_still_attempted_once_per_name"' in HARNESS
    assert "world.sensor_inside_calls == 4" in HARNESS


def test_13_removed_and_pending_sensor_subscriptions_are_executable_cases():
    assert "dluma_native_gameplay_subscription_remove" in NATIVE
    assert "memset(subscription, 0, sizeof(*subscription))" in NATIVE
    assert '"pending_sensor_subscription_survives_scan"' in HARNESS
    assert '"removed_sensor_subscription_is_silent"' in HARNESS
    assert '"sensor_generation_change_clears_baseline"' in HARNESS
    assert '"sensor_remove_readd_during_sample_rejects_commit"' in HARNESS
    assert '"sensor_generation_reset_during_sample_rejects_commit"' in HARNESS
    assert '"sensor_failed_poll_still_attempted_once_per_name"' in HARNESS
    assert '"off", script, dluma_helper_mod_off' in HELPER


def test_14_atomic_preflight_precedes_commit_and_readback():
    preflight = NATIVE.index('"local_enable_preflight_ok"')
    commit = NATIVE.index('"local_enable_commit_ok"')
    setter = NATIVE.index("backend.local_enable_set", preflight)
    readback = NATIVE.index("backend.local_enable_get", setter)
    assert preflight < setter < readback < commit
    assert '"duplicate_request_name"' in NATIVE
    assert "no rollback is claimed" in NATIVE
    assert '"partial_commit_readback_failure"' in HARNESS
    assert '"unchanged_then_precommit_failure_is_not_partial"' in HARNESS


def test_15_local_enable_uses_only_validated_interface_virtuals():
    assert "DLUMA_NATIVE_LOCAL_ENABLE_SLOT 0x0030UL" in NATIVE
    assert "DLUMA_NATIVE_LOCAL_DISABLE_SLOT 0x0038UL" in NATIVE
    assert "DLUMA_NATIVE_LOCAL_IS_ENABLED_SLOT 0x0040UL" in NATIVE
    assert "m_LEnableState" not in NATIVE
    assert "qenable" not in NATIVE.lower()
    assert '"nonzero_local_enable_interface"' in HARNESS


def test_16_interaction_observer_calls_original_once_and_preserves_result():
    hook = NATIVE[
        NATIVE.index("static char __fastcall dluma_native_interactive_hook") :
        NATIVE.index("static int dluma_native_install_interactive_hook")
    ]
    assert hook.count("original(item, action, context)") == 1
    assert hook.index("original(item, action, context)") < hook.index(
        "enqueue_interactive_item_event"
    )
    assert "return result;" in hook
    assert '"interaction_original_once_result_preserved"' in HARNESS
    assert "g_interaction_original_calls == 1u" in HARNESS
    assert "result == 37" in HARNESS


def test_17_interaction_hook_never_calls_lua_and_is_begin_only():
    hook = NATIVE[
        NATIVE.index("static char __fastcall dluma_native_interactive_hook") :
        NATIVE.index("static int dluma_native_install_interactive_hook")
    ]
    assert "lua" not in hook.lower()
    assert "dluma_native_gameplay_enqueue_interactive_item_event" in hook
    mod_api = HELPER[
        HELPER.index("static int dluma_helper_mod(") :
        HELPER.index("static int dluma_helper_timer_create")
    ]
    assert '"on_interact_begin"' in mod_api
    assert '"on_interact_end"' not in mod_api


def test_18_interaction_removal_generation_and_queue_bounds_are_executable():
    for case in (
        "interaction_nonexecute_action_observed_only",
        "interaction_failed_original_does_not_queue",
        "interaction_without_subscription_does_not_queue",
        "interaction_remove_observer_interleavings",
        "interaction_remove_readd_aba_discards_old_event",
        "interaction_queue_dispatch",
        "interaction_old_generation_discarded",
        "generation_invalidates_handle_subscription_and_event",
        "interaction_removed_subscription_no_queue",
        "interaction_queue_bounded",
        "interaction_lost_hook_withdraws_readiness",
    ):
        assert f'{{"{case}",' in HARNESS
    assert "DLUMA_NATIVE_MAX_HOOK_EVENTS 64u" in NATIVE
    assert "hook_overflow_logged" in NATIVE
    drain = NATIVE[NATIVE.index("static void dluma_native_drain_hook_events") :]
    assert "event.generation != g_dluma_native.generation" in drain
    assert "event.index_epoch != g_dluma_native.index_epoch" in drain
    assert "event.subscription_key_revision" in drain
    assert "dluma_native_has_subscription(" in drain
    assert "event.map_generation != g_dluma_lua_runtime.map_generation" in RUNTIME
    assert "dluma_native_gameplay_event_subscription_current(" in RUNTIME


def test_19_event_queues_are_bounded_and_delivery_count_is_real():
    assert "DLUMA_LUA_RUNTIME_MAX_EVENTS 256u" in RUNTIME
    assert "overflow_reported" in RUNTIME
    assert "callback_count" in RUNTIME
    assert "static int dluma_lua_runtime_dispatch_helper_event" in RUNTIME
    assert "callback_count = g_dluma_lua_runtime.api.dispatch(" in RUNTIME
    assert "slot->helper_script, event);" in RUNTIME


def test_19a_unchanged_debug_values_skip_flush_heavy_log_writes():
    setter = RUNTIME[
        RUNTIME.index("static void dluma_lua_runtime_debug_set") :
        RUNTIME.index("static void dluma_lua_runtime_host_error")
    ]
    assert "dluma_lua_runtime_debug_text_equals" in RUNTIME
    assert setter.index("if (!dluma_lua_runtime_store_debug_value(") < setter.rindex(
        'dluma_log_pair("DEBUG"'
    )


def test_20_shutdown_restores_hook_before_state_release():
    quiesce = NATIVE[NATIVE.index("void dluma_native_gameplay_quiesce") :]
    cas = quiesce.index("previous_slot = dluma_atomic_pointer_compare_exchange(")
    released = quiesce.index("g_dluma_native.hook_installed = 0")
    reset = quiesce.index("memset(&g_dluma_native")
    assert cas < released < reset
    assert "(PVOID)dluma_native_interactive_hook" in quiesce[cas:released]
    assert "previous_slot != (PVOID)dluma_native_interactive_hook" in quiesce
    assert "foreign slot value preserved" in quiesce
    quiesce_only = quiesce[: quiesce.index("void dluma_native_gameplay_stop")]
    assert "dluma_native_lock(" not in quiesce_only
    assert "Sleep(" not in quiesce_only
    assert "WaitForSingleObject" not in quiesce_only
    assert "dluma_native_try_lock(&g_dluma_interactive_lifecycle_lock)" in quiesce_only
    assert quiesce_only.index("g_dluma_native.stopping") < quiesce_only.index(
        "dluma_native_try_lock(&g_dluma_interactive_lifecycle_lock)"
    )
    assert "dluma_native_refresh_interaction_capability();" in NATIVE
    assert "interactive_item_hook_lost" in NATIVE
    shutdown = (ROOT / "src/dlw_shutdown.c").read_text(encoding="utf-8")
    assert "dluma_native_gameplay_quiesce();" in shutdown
    assert '"shutdown_restores_observer_slot"' in HARNESS
    assert '"quiesce_never_waits_for_native_locks"' in HARNESS
    assert '"interaction_lost_hook_withdraws_readiness"' in HARNESS
    assert '"restart_refuses_to_erase_retained_hook_state"' in HARNESS


def test_20a_action_chain_uses_exact_windows_target_and_complete_object_identity():
    assert "DLUMA_NATIVE_ACTION_CHAIN_VTABLE_RVA 0x0195B938UL" in NATIVE
    assert "DLUMA_NATIVE_ACTION_CHAIN_RUN_RVA 0x01189800UL" in NATIVE
    assert "g_dluma_action_chain_run_prefix" in NATIVE
    production = NATIVE[
        NATIVE.index("static int dluma_native_prod_action_run_chain") :
        NATIVE.index("static DlumaNativeTtlString dluma_native_ttl_string_view")
    ]
    assert "complete_action_chain" in production
    assert "DLUMA_NATIVE_ACTION_CHAIN_VTABLE_RVA" in production
    assert "DLUMA_NATIVE_ACTION_CHAIN_RUN_RVA" in production
    assert "run(complete_action_chain);" in production
    public = NATIVE[
        NATIVE.index("int dluma_native_gameplay_action_run_chain") :
        NATIVE.index("int dluma_native_gameplay_enqueue_interactive_item_event")
    ]
    assert 'entry.class_name, "ActionChain"' in public
    assert "entry.index_epoch != g_dluma_native.index_epoch" in public
    assert "entry.complete" in public
    assert '"action_chain_runs_exact_named_chain"' in HARNESS
    assert '"action_chain_backend_failure_is_structured"' in HARNESS


def test_21_player_read_is_advertised_without_broad_native_player_access():
    capability_block = RUNTIME[
        RUNTIME.index("static uint32_t dluma_lua_runtime_capabilities_for_script") :
        RUNTIME.index("static int dluma_lua_runtime_create_slot")
    ]
    assert "DLUMA_LUA54_CAP_PLAYER_READ" in capability_block
    assert "DLUMA_LUA54_PERMISSION_PLAYER_READ" in capability_block
    assert "player" not in NATIVE.lower()
