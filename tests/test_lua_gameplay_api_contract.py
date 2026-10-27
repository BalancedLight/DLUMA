from __future__ import annotations

import importlib.util
import json
import re
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]

def load_validator():
    path = ROOT / "tools" / "validate_lua_metadata.py"
    spec = importlib.util.spec_from_file_location("dluma_api_metadata_validator", path)
    assert spec and spec.loader
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


def test_quest_permissions_are_recognized_by_the_metadata_validator():
    validator = load_validator()
    assert {"quest.write", "events.enemy_killed"}.issubset(
        validator.KNOWN_PERMISSIONS
    )




def test_game_frame_hook_preserves_the_complete_native_onframe_abi():
    engine = (ROOT / "src/dlw_engine.c").read_text(encoding="utf-8")
    hook = engine[
        engine.index("typedef ULONG_PTR (__fastcall *DlumaGameFrameFn)"):
        engine.index("static void __fastcall dluma_i_level_destructor_hook")
    ]
    assert "void* game_subobject, void* gesture_manager, float delta_seconds" in hook
    assert "result = original(game_subobject, gesture_manager, delta_seconds);" in hook
    assert "return result;" in hook
    assert "typedef void (__fastcall *DlumaGameFrameFn)(void* game_subobject)" not in engine
    assert "original(game_subobject);" not in hook


def test_optional_info_panel_uses_a_destroy_before_replace_lifecycle():
    ui = (ROOT / "src/dlw_ui.c").read_text(encoding="utf-8")
    assert "#define DLUMA_ENABLE_INFO_PANEL_RUNTIME 1" in ui
    assert "#define DLUMA_DEVTOOLS_INFO_DESTROY_RVA 0x010EDDA0UL" in ui
    assert "devtools_destroy_signature" in ui
    assert "g_dluma_info_destroy_channel" in ui
    assert "DlumaDevToolsInfoPanelDestroyChannelFn" in ui
    tick = ui[
        ui.index("void dluma_ui_game_thread_tick("):
        ui.index("int dluma_ui_start(void)")
    ]
    call = "dluma_ui_tick_info_panel(level, map_name, now);"
    assert call in tick
    call_offset = tick.index(call)
    guard_start = tick.rfind("#if DLUMA_ENABLE_INFO_PANEL_RUNTIME", 0, call_offset)
    guard_end = tick.index("#endif", guard_start)
    assert guard_start >= 0
    assert guard_start < call_offset < guard_end
    lifecycle = ui[
        ui.index("static void dluma_ui_tick_info_panel"):
        ui.index("static void dluma_ui_observe_native_mods_screen")
    ]
    assert "dluma_ui_is_frontend(map_name)" in lifecycle
    assert "dluma_ui_destroy_info_panel_channel" in lifecycle
    assert lifecycle.index("dluma_ui_destroy_info_panel_channel") < lifecycle.index(
        "dluma_ui_update_info_panel"
    )
    assert "DLUMA_INFO_PANEL_MAP_STABLE_MS" in lifecycle
    assert "g_dluma.active_map" not in lifecycle
    assert "not script- or Workshop-owner-owned" in lifecycle
    assert "retail InfoPanel binding is disabled" in ui


def test_info_panel_refresh_survives_transient_level_query_gaps():
    common = (ROOT / "src/dlw_common.h").read_text(encoding="utf-8")
    host = (ROOT / "src/dlw_host.c").read_text(encoding="utf-8")
    ui = (ROOT / "src/dlw_ui.c").read_text(encoding="utf-8")
    ini = (
        ROOT
        / "config/dluma.ini"
    ).read_text(encoding="utf-8")

    assert "DWORD universal_screen_poll_interval_ms;" in common
    assert re.search(
        r'L"loader",\s*L"universal_screen_poll_interval_ms",\s*100', host
    )
    assert "universal_screen_poll_interval_ms < 10" in host
    assert "universal_screen_poll_interval_ms > 5000" in host
    assert "universal_screen_poll_interval_ms=100" in ini

    refresh = ui[
        ui.index("static void dluma_ui_refresh_info_panel_text") :
        ui.index("static void dluma_ui_update_info_panel")
    ]
    assert "g_dluma.config.universal_screen_poll_interval_ms" in refresh
    assert "g_dluma_info_show_text" in refresh
    assert "< 500" not in refresh

    tick = ui[
        ui.index("void dluma_ui_game_thread_tick(") :
        ui.index("int dluma_ui_start(void)")
    ]
    snapshot_failure_start = tick.index("if (!snapshot_consistent)")
    snapshot_failure_end = tick.index("goto done;", snapshot_failure_start) + len(
        "goto done;"
    )
    snapshot_failure = tick[snapshot_failure_start:snapshot_failure_end]
    assert "g_dluma_info_channel_id >= 0" in snapshot_failure
    assert "dluma_ui_refresh_info_panel_text" in snapshot_failure
    assert "dluma_ui_update_info_panel" not in snapshot_failure
    assert "dluma_ui_destroy_info_panel_channel" not in snapshot_failure
    assert "dluma_engine_query_level" not in snapshot_failure

    start = ui[ui.index("int dluma_ui_start(void)") : ui.index("void dluma_ui_stop(void)")]
    assert "DLUMA_UI_FALLBACK_TIMER_MAX_MS" in start
    assert "g_dluma.config.universal_screen_poll_interval_ms" in start
    assert "SetTimer(0, 0, fallback_timer_ms, dluma_ui_timer_proc)" in start


def test_native_capability_bits_are_enabled_only_through_readiness_adapter():
    runtime = (ROOT / "src/dlw_lua_runtime.c").read_text(encoding="utf-8")
    native = (ROOT / "src/dlw_native_gameplay.c").read_text(encoding="utf-8")
    block = runtime[
        runtime.index("static uint32_t dluma_lua_runtime_capabilities_for_script"):
        runtime.index("static int dluma_lua_runtime_create_slot")
    ]
    assert "DLUMA_LUA54_CAP_LUA_RUNTIME" in block
    assert "DLUMA_LUA54_CAP_STORAGE_MOD" in block
    assert "dluma_native_gameplay_capabilities()" in block
    # PlayerVariables is a helper-owned exact-build bridge rather than a
    # dluma_native_gameplay adapter.  Its own PE/vtable/prologue/live-state
    # validation remains required for every public player call.
    assert "DLUMA_LUA54_CAP_PLAYER_READ" in block
    assert "PlayerVariables bridge performs its own exact PE" in block
    for bit in (
        "WORLD_FIND", "WORLD_LOCAL_ENABLE", "EVENTS_SENSOR", "EVENTS_INTERACT",
        "UI_SUBTITLES", "AUDIO_RADIO_DIALOG", "ACTION_CHAIN",
        "QUEST_NATIVE_CONTROL",
    ):
        assert f"DLUMA_LUA54_CAP_{bit}" in block
        assert f"DLUMA_LUA54_CAP_{bit}" in native
    for bit in (
        "UI_OVERLAY", "AUDIO_REGISTERED_CUES",
        "ACTION_INVOKE", "MODIFIER_ALLOWLIST",
    ):
        assert f"DLUMA_LUA54_CAP_{bit}" not in block


def test_subtitle_and_radio_dialog_api_uses_bounded_typed_game_thread_adapters():
    api = (ROOT / "src/dlw_lua54_api.h").read_text(encoding="utf-8")
    helper = (ROOT / "src/dlw_lua54_helper.c").read_text(encoding="utf-8")
    native = (ROOT / "src/dlw_native_gameplay.c").read_text(encoding="utf-8")
    assert "DlumaLua54SubtitleRequest" in api
    assert "DlumaLua54SubtitleUpdateRequest" in api
    assert "DlumaLua54RadioDialogRequest" in api
    assert '"send_subtitle", script' in helper
    assert '"update_subtitle", script' in helper
    assert '"play_radio_dialog", script' in helper
    assert "DLUMA_NATIVE_SUBTITLE_ID" in native
    assert "subtitle_expires_ms" in native
    assert "DLUMA_NATIVE_DIALOG_MANAGER_RVA" in native
    assert "DLUMA_NATIVE_DIALOG_MANAGER_FIND_SPEAKER_RVA" in native
    assert "DLUMA_NATIVE_DIALOG_MANAGER_FIND_GROUP_RVA" in native
    assert "DLUMA_NATIVE_DIALOG_SPEAKER_FIND_GROUP_SLOT" in native
    assert "DLUMA_NATIVE_DIALOG_SPEAKER_SET_QUEST_SLOT" in native
    assert "lua_State" not in native


def test_action_chain_api_is_typed_gated_and_game_thread_owned():
    api = (ROOT / "src/dlw_lua54_api.h").read_text(encoding="utf-8")
    helper = (ROOT / "src/dlw_lua54_helper.c").read_text(encoding="utf-8")
    runtime = (ROOT / "src/dlw_lua_runtime.c").read_text(encoding="utf-8")
    native = (ROOT / "src/dlw_native_gameplay.c").read_text(encoding="utf-8")
    assert "DlumaLua54ActionRunChainFn" in api
    assert '"run_chain", script' in helper
    assert "dluma_helper_action_run_chain" in helper
    assert "dluma_lua_runtime_action_run_chain" in runtime
    assert "dluma_native_gameplay_action_run_chain" in native
    assert '"actions.chain", "world.write"' in helper


def test_quest_native_gate_is_typed_and_lua_owned_presentation_is_retired():
    api = (ROOT / "src/dlw_lua54_api.h").read_text(encoding="utf-8")
    helper = (ROOT / "src/dlw_lua54_helper.c").read_text(encoding="utf-8")
    consent = (ROOT / "src/dlw_global_consent.c").read_text(encoding="utf-8")
    host = (ROOT / "src/dlw_host.c").read_text(encoding="utf-8")
    runtime = (ROOT / "src/dlw_lua_runtime.c").read_text(encoding="utf-8")
    native = (ROOT / "src/dlw_native_gameplay.c").read_text(encoding="utf-8")
    native_header = (ROOT / "src/dlw_native_gameplay.h").read_text(
        encoding="utf-8"
    )

    for token in (
        "DlumaLua54QuestRequest",
        "DlumaLua54QuestResult",
        "DlumaLua54QuestMutateFn",
        "DlumaLua54QuestStateChangedFn",
        "DLUMA_LUA54_CAP_QUEST_NATIVE_CONTROL",
        "DLUMA_LUA54_EVENT_ENEMY_KILLED",
        "DlumaLua54EnemyKilledEvent",
    ):
        assert token in api
    assert "quest_mutate" in api
    assert '"quest.write"' in consent
    assert '"quest_write"' in host
    assert '"events_enemy_killed"' in host

    assert "dluma_lua_runtime_quest_mutate" in runtime
    assert "dluma_helper_quest_advance_to_phase" in helper
    assert "dluma_helper_quest_start_removed" in helper
    assert '"removed_operation"' in helper
    assert "DLUMA_NATIVE_READY_QUEST_MUTATION" in native_header
    gate = native[
        native.index("int dluma_native_gameplay_quest_mutate") :
        native.index("int dluma_native_gameplay_enqueue_interactive_item_event")
    ]
    assert '"invalid_quest_request"' in gate
    assert "dluma_native_require_capability" in gate
    assert '"unsupported_capability"' in native
    assert '"quests.native", "quest.write"' in gate
    for token in (
        "DLUMA_NATIVE_QUEST_MANAGER_RVA",
        "DLUMA_NATIVE_QUEST_FIND_RVA",
        "DLUMA_NATIVE_QUEST_FIND_PHASE_RVA",
        "DLUMA_NATIVE_QUEST_START_SYNCHRONIZED_INVOKE_RVA",
        "dluma_native_validate_quest_adapter",
        "dluma_native_prod_quest_mutate",
        "quest_coop_not_supported",
        "unsupported_quest_operation",
        "StartSynchronizedInvoke",
    ):
        assert token in native
    adapter = native[
        native.index("static int dluma_native_prod_quest_mutate"):
        native.index(
            "static DlumaNativeTtlString dluma_native_ttl_string_view(const char* text)\n{",
            native.index("static int dluma_native_prod_quest_mutate"),
        )
    ]
    assert "quest_start_synchronized_invoke" in adapter
    assert "manager, phase, 1, 0, 1" in adapter
    assert "AdvanceToQuest(" not in adapter


def test_live_subtitle_validation_accepts_compatible_derived_vtables_only_by_exact_slots():
    native = (ROOT / "src/dlw_native_gameplay.c").read_text(encoding="utf-8")
    validation = native[
        native.index("static int dluma_native_prod_validate_level_subtitles"):
        native.index("static int dluma_native_prod_subtitle_show")
    ]
    for exact_slot_target in (
        "DLUMA_NATIVE_LEVEL_SHOW_SUBTITLE_RVA",
        "DLUMA_NATIVE_LEVEL_HIDE_SUBTITLE_RVA",
        "DLUMA_NATIVE_GAME_MOVIE_SHOW_SUBTITLE_RVA",
        "DLUMA_NATIVE_GAME_MOVIE_HIDE_SUBTITLE_RVA",
    ):
        assert exact_slot_target in validation
    assert "subtitle_live_level_compatible_derived_vtable" in validation
    assert "subtitle_live_movie_compatible_derived_vtable" in validation
    assert "subtitle_live_validation_failed = level_subtitle_slots_mismatch" in validation
    assert "subtitle_live_validation_failed = game_movie_subtitle_slots_mismatch" in validation


def test_radio_dialog_uses_stock_level_aware_speaker_and_group_resolution():
    native = (ROOT / "src/dlw_native_gameplay.c").read_text(encoding="utf-8")
    production = native[
        native.index("static int dluma_native_prod_radio_dialog"):
        native.index("static uint64_t dluma_native_now")
    ]
    assert "find_speaker_from_level" in production
    assert "find_group_from_level" in production
    assert "g_dluma_native.level" in production
    assert "g_dluma.engine.get_level_name(g_dluma_native.level)" in production
    assert "raw_level_name, level_name" in production
    assert "manager, &speaker_view, level_name" in production
    assert "manager, &group_view, level_name" in production
    assert "manager, &speaker_view, g_dluma_native.level" not in production
    assert "manager, &group_view, g_dluma_native.level" not in production
    assert "DLUMA_NATIVE_DIALOG_MANAGER_SPEAKERS_OFFSET" not in production


def test_local_defaults_feed_runtime_and_universal_screen_diagnostics():
    common = (ROOT / "src/dlw_common.h").read_text(encoding="utf-8")
    host = (ROOT / "src/dlw_host.c").read_text(encoding="utf-8")
    discovery = (ROOT / "src/dlw_lua_discovery.c").read_text(encoding="utf-8")
    runtime = (ROOT / "src/dlw_lua_runtime.c").read_text(encoding="utf-8")
    helper = (ROOT / "src/dlw_lua54_helper.c").read_text(encoding="utf-8")
    ui = (ROOT / "src/dlw_ui.c").read_text(encoding="utf-8")
    assert "out->enabled_requested = out->enabled_by_default;" in discovery
    assert "out->enabled = out->enabled_by_default;" in discovery
    for permission in (
        "GAME_READ", "PLAYER_READ", "WORLD_QUERY", "EVENTS_SENSOR",
        "EVENTS_INTERACT", "STORAGE_MOD", "UI_DEBUG", "UI_OVERLAY",
        "AUDIO_PLAY", "WORLD_WRITE",
    ):
        assert f"DLUMA_LOCAL_LUA_POLICY_{permission}" in common
    for key in (
        "game_read", "player_read", "world_query", "events_sensor",
        "events_interact", "storage_mod", "ui_debug", "ui_overlay",
        "audio_play", "world_write",
    ):
        assert f'"{key}"' in host
    assert "requested & dluma_lua_runtime_local_policy_permissions()" in runtime
    assert "dluma_lua_runtime_append_info_text" in runtime
    assert '"Lua states loaded: "' in runtime
    assert '" [local policy off: "' in runtime
    assert '"Script debug:\\n"' in runtime
    assert "dluma_helper_debug_append" not in helper
    assert '"capability", script, dluma_helper_capability' in helper
    assert "API unavailable: capability" in helper
    assert "dluma_lua_runtime_append_info_text(text, capacity);" in ui


def test_lifecycle_reconciliation_preserves_project_and_recreates_map():
    runtime = (ROOT / "src/dlw_lua_runtime.c").read_text(encoding="utf-8")
    block = runtime[
        runtime.index("static void dluma_lua_runtime_apply_context"):
        runtime.index("static void dluma_lua_runtime_take_pending")
    ]
    assert "same_project" in block
    assert "dluma_lua_runtime_destroy_scope(DLUMA_LUA_SCOPE_MAP, 1);" in block
    assert "if (!same_project) dluma_lua_runtime_destroy_scope(DLUMA_LUA_SCOPE_PROJECT, 1);" in block
    assert block.index("DLUMA_LUA54_EVENT_MAP_UNLOAD") < block.index("++g_dluma_lua_runtime.map_generation")
    assert block.index("++g_dluma_lua_runtime.map_generation") < block.index("DLUMA_LUA54_EVENT_MAP_LOAD")
    assert "dluma_lua_runtime_destroy_scope(DLUMA_LUA_SCOPE_GLOBAL" not in block


def test_transient_null_never_publishes_an_unload_context():
    host = (ROOT / "src/dlw_host.c").read_text(encoding="utf-8")
    null_branch = host[
        host.index("if (!has_level || !observed_map[0])"):
        host.index("if (query_failure_warning_logged)")
    ]
    assert "Null is an unknown sample" in null_branch
    assert "dluma_lua_runtime_publish_context" not in null_branch
    assert "dluma_unload_active_map" not in null_branch
    assert 'dluma_unload_active_map("confirmed frontend level became active", 1)' in host
    assert 'dluma_unload_active_map("different confirmed level became active", 0)' in host


def test_shutdown_quiesces_then_closes_owned_states_before_helper_unload():
    runtime = (ROOT / "src/dlw_lua_runtime.c").read_text(encoding="utf-8")
    stop = runtime[runtime.index("void dluma_lua_runtime_stop") :]
    assert stop.index("quiescing") < stop.index("pump_active")
    assert stop.index("pump_active") < stop.index("dluma_lua_runtime_destroy_slot")
    assert stop.index("dluma_lua_runtime_destroy_slot") < stop.index("FreeLibrary")
    assert "private helper retained for shutdown safety" in stop
    assert "if (remaining)" in stop
    helper = (ROOT / "src/dlw_lua54_helper.c").read_text(encoding="utf-8")
    destroy = helper[helper.index("static int dluma_helper_destroy_script") : helper.index("DLUMA_HELPER_EXPORT")]
    assert "active_callbacks" in destroy
    assert "return 0" in destroy






def test_public_helper_has_no_pointer_or_native_ffi_surface():
    helper = (ROOT / "src/dlw_lua54_helper.c").read_text(encoding="utf-8")
    api = (ROOT / "src/dlw_lua54_api.h").read_text(encoding="utf-8")
    assert 'lua_setglobal(state, "io")' in helper
    assert 'lua_setglobal(state, "os")' in helper
    assert 'lua_setglobal(state, "package")' in helper
    assert 'lua_setglobal(state, "debug")' in helper
    assert "luaopen_io" not in helper and "luaopen_os" not in helper
    assert "luaopen_package" not in helper and "luaopen_debug" not in helper
    for exposed in ("module_base", "vtable", "memory_read", "native_call"):
        assert f'"{exposed}"' not in helper
    assert "source_fingerprint" in api


def test_build_scripts_package_private_helper_and_license_without_launching_game():
    scripts = [
        ROOT / "build_windows_msvc.bat",
        ROOT / "build_windows_clang.bat",
        ROOT / "tools/build_lua54_helper_msvc.bat",
        ROOT / "tools/build_lua54_helper_clang.bat",
    ]
    combined = "\n".join(path.read_text(encoding="utf-8") for path in scripts)
    assert "dluma_lua54.dll" in combined
    assert "LICENSE_LUA_5.4.8.txt" in combined
    for forbidden in ("DyingLightPlayer.exe", "DyingLightEditor.exe", "ChromeEd"):
        assert forbidden not in combined
    assert "dlw_lua54_helper.c" in combined
    assert "lualib.c" not in combined
