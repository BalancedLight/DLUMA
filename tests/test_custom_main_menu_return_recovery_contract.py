from pathlib import Path
import subprocess


ROOT = Path(__file__).resolve().parents[1]


def _function_body(source: str, signature: str) -> str:
    start = source.index(signature)
    brace = source.index("{", start)
    depth = 0
    for index in range(brace, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[brace : index + 1]
    raise AssertionError(f"unterminated function: {signature}")


def test_custom_main_menu_return_source_native_harness() -> None:
    subprocess.run(
        [
            "cmd.exe",
            "/d",
            "/s",
            "/c",
            "tools\\build_custom_main_menu_return_source_fake_tests.bat",
        ],
        cwd=ROOT,
        check=True,
    )
    subprocess.run(
        [
            str(
                ROOT
                / "build"
                / "custom-main-menu-return-source-fake"
                / "custom_main_menu_return_source_fake_harness.exe"
            )
        ],
        cwd=ROOT,
        check=True,
    )


def test_retired_custom_menu_paths_cannot_claim_left_mounted() -> None:
    registry_header = (
        ROOT / "src" / "dlw_process_lifetime_sources.h"
    ).read_text(encoding="utf-8")
    registry_source = (
        ROOT / "src" / "dlw_process_lifetime_sources.c"
    ).read_text(encoding="utf-8")

    assert "DLUMA_PROCESS_SOURCE_STATE_RETIRED" in registry_header
    assert "DlumaProcessLifetimeSourceGenerationToken" in registry_header
    retire = _function_body(
        registry_source,
        "int dluma_process_lifetime_source_retire_in(",
    )
    assert "record->exact_source_joined = 0" in retire
    assert "record->intrusive_link = 0" in retire
    assert "record->lifecycle_state =\n            DLUMA_PROCESS_SOURCE_STATE_RETIRED" in retire

    presence = _function_body(
        registry_source,
        "int dluma_process_lifetime_source_live_presence_in(",
    )
    assert "DLUMA_PROCESS_SOURCE_STATE_LIVE" in presence
    assert "record->exact_source_joined" in presence
    assert "record->requested_mount_path" not in presence

    pack = (ROOT / "src" / "dlw_pack.c").read_text(encoding="utf-8")
    unmount = _function_body(pack, "void dluma_unmount_content_pack(")
    assert "dluma_process_lifetime_source_live_presence(" in pack
    assert "custom_main_menu_source_retired_or_absent_at_shutdown" in unmount
    for owner in ("project", "data", "assets_pc", "overlay"):
        mounted_key = (
            f"custom_main_menu_{owner}_left_mounted_until_process_exit"
        )
        assert mounted_key in unmount
    assert unmount.index("custom_main_menu_source_retired_or_absent_at_shutdown") < (
        unmount.index("custom_main_menu_project_left_mounted_until_process_exit")
    )


def test_natural_vfs_removal_and_return_use_generation_safe_registry_paths() -> None:
    isolation = (
        ROOT
        / "src"
        / "fixes"
        / "custom_map_asset_isolation"
        / "CustomMapAssetIsolationFix.cpp"
    ).read_text(encoding="utf-8")
    pack = (ROOT / "src" / "dlw_pack.c").read_text(encoding="utf-8")

    remove_path = _function_body(isolation, "bool __fastcall HookFsRemovePath(")
    remove_object = _function_body(
        isolation, "bool __fastcall HookFsRemoveObject("
    )
    for body, original_call in (
        (remove_path, "g_original_fs_remove_path(path)"),
        (remove_object, "g_original_fs_remove_object(source)"),
    ):
        assert "dluma_custom_main_menu_capture_native_source_generation(" in body
        assert "dluma_custom_main_menu_retire_native_source_generation(" in body
        natural_remove = body.index("const bool result = " + original_call)
        assert body.index(
            "dluma_custom_main_menu_capture_native_source_generation("
        ) < natural_remove
        assert natural_remove < body.index(
            "dluma_custom_main_menu_retire_native_source_generation("
        )
        assert "DLUMA_CMM_SOURCE_RETIRE_NEW" in body
        assert "dluma_custom_main_menu_runtime_note_source_retired(\n" in body
        assert "1U, source_frontend_generation" in body
    assert "dluma_log_" not in remove_path
    assert "dluma_log_" not in remove_object

    bind = _function_body(pack, "int dluma_custom_main_menu_bind_native_source(")
    assert "dluma_process_lifetime_source_compare_replace(" in bind
    assert "custom_main_menu_native_source_replaced" in bind

    rebind = _function_body(
        pack,
        "int dluma_custom_main_menu_rebind_retired_native_source(",
    )
    assert "dluma_custom_main_menu_directory_identity_equal(" in rebind
    assert "exact_source == state->retired_generation.source_object" in rebind
    assert "dluma_process_lifetime_source_compare_replace(" in rebind
    assert "custom_main_menu_native_source_rebound" in rebind

    add_path = _function_body(isolation, "bool __fastcall HookFsAddPath(")
    add_object = _function_body(isolation, "void __fastcall HookFsAddObject(")
    assert "dluma_custom_main_menu_rebind_retired_native_source(" in add_path
    assert "dluma_custom_main_menu_rebind_retired_native_source(" in add_object

    populate = _function_body(
        isolation, "bool PopulateBaselineSourceInventory("
    )
    assert "DLUMA_PROCESS_SOURCE_STATE_RETIRED" in populate
    assert "dluma_custom_main_menu_refresh_active_frontend_sources();" in populate

    capture = _function_body(
        pack,
        "int dluma_custom_main_menu_capture_native_source_generation(",
    )
    retire = _function_body(
        pack,
        "int dluma_custom_main_menu_retire_native_source_generation(",
    )
    assert "dluma_custom_main_menu_source_state_lock();" in capture
    assert "*frontend_generation_out = state->frontend_generation" in capture
    assert "dluma_custom_main_menu_source_state_lock();" in retire
    assert "dluma_process_lifetime_source_retire(" in retire
    assert "Lock order is always pack source-state -> process-lifetime registry" in pack

    ready = _function_body(
        pack, "static int dluma_custom_main_menu_native_sources_ready_locked("
    )
    assert "dluma_process_lifetime_source_live_presence(" in ready
    assert ".mounted" not in ready
    assert ".registered" not in ready


def test_transient_publication_cleanup_is_not_a_frontend_retirement() -> None:
    runtime = (
        ROOT / "src" / "dlw_custom_main_menu_runtime.c"
    ).read_text(encoding="utf-8")

    consume = _function_body(
        runtime, "static LONG dluma_cmm_consume_source_retirement("
    )
    transient = consume.index("reason == 1 && !gameplay_started")
    ignored = consume.index("transient_publication_cleanup_ignored", transient)
    retirement = consume.index(
        "dluma_cmm_retire_frontend_generation_locked(", ignored
    )
    assert transient < ignored < retirement
    assert "reason = 0;" in consume[transient:retirement]
    assert "DLUMA_CMM_PUBLICATION_READY_BEFORE_UI" in consume[transient:ignored]
    assert "DLUMA_CMM_PUBLICATION_LOADING_BACKGROUND" in consume[transient:ignored]
    assert "DLUMA_CMM_PUBLICATION_ACTIVE" not in consume[transient:ignored]

    # Real owning transitions remain authoritative: gameplay retires directly
    # even after an advisory source-removal notice was consumed, while the
    # exact background unload queues reason 2 for the same generation.
    tick = _function_body(
        runtime, "void dluma_custom_main_menu_runtime_tick("
    )
    assert "if (!retirement_reason)" in tick
    assert '"gameplay_started", 1' in tick

    unload = _function_body(
        runtime, "static __int64 __fastcall dluma_cmm_unload_hook("
    )
    assert "if (owns_generation)" in unload
    assert "2U, owned_generation" in unload


def test_fx_gate_and_exact_emitter_lifecycle_are_separate_contracts() -> None:
    runtime = (
        ROOT / "src" / "dlw_custom_main_menu_runtime.c"
    ).read_text(encoding="utf-8")

    enable_fx = _function_body(runtime, "static void dluma_cmm_enable_fx(")
    assert "custom_main_menu_fx_global_gate_previous" in enable_fx
    assert "custom_main_menu_fx_global_enabled" in enable_fx
    assert "custom_main_menu_fx_activation" not in enable_fx
    assert "custom_main_menu_fx_runtime_ready" not in runtime

    # Exact-build evidence only proves that ILevel::FXEnable controls a
    # process-global gate.  The CFXRuntimeManager pointer at concrete CLevel
    # +0x7D0 is not a field on the Menu3DBackgroundDI/ILevel wrapper, so the
    # runtime must not dereference that offset from the wrapper or present an
    # arbitrary readable pointer as a validated manager.
    assert "DLUMA_CMM_LEVEL_FX_MANAGER_OFFSET" not in runtime

    # Mounted source readiness is not evidence that an emitter has started or
    # submitted particles. The exact-build lifecycle bridge must validate the
    # concrete emitter subobjects, manager, and owned frontend generation.
    for synthetic_claim in (
        "fx_catalog_ready",
        "fx_manager_present",
        "fx_manager_registered",
        "fx_update_count",
        "fx_particle_submit_count",
        "custom_main_menu_fx_runtime_ready",
    ):
        assert synthetic_claim not in runtime

    bind_lifecycle = _function_body(
        runtime, "static int dluma_cmm_bind_fx_lifecycle("
    )
    assert "?GetFXManager@IGSObject@@QEAAPEAVIFXManager@@XZ" in bind_lifecycle
    assert "DLUMA_CMM_FX_EMITTER_ON_ENABLED_RVA" in bind_lifecycle
    assert "DLUMA_CMM_FX_EMITTER_ON_DISABLED_RVA" in bind_lifecycle

    start_emitters = _function_body(
        runtime, "static void dluma_cmm_start_fx_emitters_after_activation("
    )
    assert "fx_start_attempted" in start_emitters
    assert "fx_start_generation == generation" in start_emitters
    assert "fx_emitter_on_enabled(" in start_emitters
    assert start_emitters.index("fx_started_emitter_count") < (
        start_emitters.index("fx_emitter_on_enabled(")
    )

    stop_emitters = _function_body(
        runtime, "static void dluma_cmm_stop_started_fx_emitters("
    )
    assert "entry->generation != generation" in stop_emitters
    assert "fx_emitter_on_disabled(" in stop_emitters

    apply_camera = _function_body(runtime, "static void dluma_cmm_apply_camera(")
    assert apply_camera.index("dluma_cmm_enable_fx(") < apply_camera.index(
        "dluma_cmm_start_fx_emitters_after_activation("
    )
    unload = _function_body(runtime, "static __int64 __fastcall dluma_cmm_unload_hook(")
    assert unload.index("dluma_cmm_stop_started_fx_emitters(") < unload.index(
        "dluma_cmm_restore_fx("
    ) < unload.index("original(module)")

    assert "custom_main_menu_fx_lifecycle" in runtime


def test_gameplay_return_renews_frontend_without_ui_initializer_reentry() -> None:
    runtime = (
        ROOT / "src" / "dlw_custom_main_menu_runtime.c"
    ).read_text(encoding="utf-8")

    retire = _function_body(
        runtime, "static void dluma_cmm_retire_frontend_generation_locked("
    )
    assert "restoration_background_required" in runtime
    assert "restoration_candidate = g_dluma_cmm_runtime.publication_module" in retire
    assert "restoration_candidate = g_dluma_cmm_runtime.active_module" in retire
    assert "restoration_candidate = g_dluma_cmm_runtime.loaded_module" in retire
    assert "dluma_cmm_main_menu_module_valid(restoration_candidate)" in retire
    assert "does not destroy the module or the UI controller" in retire
    assert "retained_exact_background_owner_for_frontend_return" in retire
    assert "retain_background_owner ? restoration_candidate : 0" in retire
    assert "gameplay_generation_retired" in retire
    assert "restoration_ui_requested, 0" in retire
    assert "restoration_load_requested, 0" in retire
    assert "restoration_activate_requested, 0" in retire

    initializer = _function_body(
        runtime, "static __int64 __fastcall dluma_cmm_main_initialize_ui_hook("
    )
    assert initializer.index("dluma_cmm_frontend_restore_allowed()") < (
        initializer.index("dluma_cmm_publish_frontend_generation(")
    ) < initializer.index("result = original(module)")
    assert "dluma_custom_main_menu_runtime_frontend_ready();" in initializer
    assert "restoration_pending, 0);" not in initializer

    load_hook = _function_body(
        runtime, "static void __fastcall dluma_cmm_load_hook("
    )
    assert load_hook.index("dluma_cmm_publish_frontend_generation(") < load_hook.index(
        "dluma_cmm_call_custom_load(module)"
    )
    assert "synchronous_return_before_native_background_load" in load_hook
    assert "completed_before_native_return_continues" in load_hook
    activate_hook = _function_body(
        runtime, "static void __fastcall dluma_cmm_activate_hook("
    )
    ui_hook = _function_body(
        runtime, "static void __fastcall dluma_cmm_main_register_ui_elements_hook("
    )
    for body, request in (
        (ui_hook, "restoration_ui_requested"),
        (load_hook, "restoration_load_requested"),
        (activate_hook, "restoration_activate_requested"),
    ):
        assert "dluma_cmm_restoration_request_pending(state)" in body
        assert request in body
        assert "deferred_until_generation_ready" in body

    unload = _function_body(
        runtime, "static __int64 __fastcall dluma_cmm_unload_hook("
    )
    assert "pending_restoration_owner" in unload
    assert "module == g_dluma_cmm_runtime.restoration_module" in unload
    assert "g_dluma_cmm_runtime.restoration_module = module" in unload
    assert "g_dluma_cmm_runtime.restoration_background_required = 1U" in unload
    assert "background_unloaded_exact_module_owner_retained" in unload
    assert "g_dluma_cmm_runtime.restoration_module = 0" not in unload

    restore = _function_body(
        runtime, "static void dluma_cmm_restore_frontend_generation("
    )
    ordered = (
        "dluma_cmm_publish_frontend_generation(",
        "dluma_cmm_texture_definitions_load(ui_level)",
        "main_register_ui_elements_original(ui_level)",
        "DLUMA_CMM_PUBLICATION_EVENT_LOAD_STARTED",
        "dluma_cmm_call_custom_load(module)",
        "activate_original(module)",
        "dluma_cmm_apply_camera(module)",
        "DLUMA_CMM_PUBLICATION_EVENT_ACTIVATE_COMPLETE",
    )
    offsets = [restore.index(token) for token in ordered]
    assert offsets == sorted(offsets)
    assert "gameplay_to_frontend_generation_started" in restore
    assert "background_required" in restore
    assert "activate_requested = activate_requested || background_required" in restore
    assert "should_load = !g_dluma_cmm_runtime.custom_load_complete" in restore
    assert "restoration_processing, 1" in restore
    assert "restoration_processing, 0" in restore
    assert "if (!failure && should_load)" in restore
    assert restore.count("restoration_load_requested, 0") >= 2
    assert restore.count("restoration_activate_requested, 0") >= 3
    assert restore.index("main_register_ui_elements_original(ui_level)") < (
        restore.index("should_load = !g_dluma_cmm_runtime.custom_load_complete")
    ) < restore.index("dluma_cmm_call_custom_load(module)")
    load_offset = restore.index("dluma_cmm_call_custom_load(module)")
    activate_offset = restore.index("activate_original(module)")
    assert load_offset < restore.index(
        "restoration_activate_requested, 0", load_offset
    ) < activate_offset
    assert "generation_ready_awaiting_original_load" in restore
    assert "generation_loaded_awaiting_original_activation" in restore

    tick = _function_body(
        runtime, "void dluma_custom_main_menu_runtime_tick("
    )
    session_gameplay = _function_body(
        runtime, "static int dluma_cmm_session_gameplay_active("
    )
    assert "dluma_player_media_adapter_get_session_level(&level)" in session_gameplay
    assert "state->get_level_name(level)" in session_gameplay
    assert "dluma_normalize_map_name(" in session_gameplay
    assert "!dluma_frontend_level_name_is_frontend(normalized)" in session_gameplay
    assert "frontend_observed" in tick
    assert "dluma_cmm_session_gameplay_active()" in tick
    assert "!gameplay_started && !frontend_observed &&" in tick
    assert tick.index("if (gameplay_started)") < tick.index(
        "dluma_cmm_restore_frontend_generation(frontend_observed ? level : 0)"
    )
    assert "dluma_cmm_frontend_restore_allowed()" in tick
    gate = _function_body(runtime, "static int dluma_cmm_frontend_restore_allowed(")
    assert gate.index("!g_dluma.config.custom_map_asset_isolation_enabled") < gate.index(
        "dluma_custom_map_content_frontend_restore_allowed()"
    )
    request = _function_body(runtime, "static void dluma_cmm_note_native_frontend_request(")
    assert request.index("dluma_cmm_consume_source_retirement(0)") < request.index(
        "dluma_custom_main_menu_runtime_frontend_ready()"
    )
    assert "module == g_dluma_cmm_runtime.restoration_module" in request
    assert "frontend_committed" in tick
    assert "restoration_pending, 0, 0) != 0 && return_ready" in tick
    assert "scene_camera_audio_ready" in tick
    mount = _function_body(
        runtime, "static int dluma_cmm_mount_overlay_after_catalogs("
    )
    assert "gameplay_generation_retired = 0" not in mount
    assert "if (!level || !dluma_custom_main_menu_selected_ready()" not in restore

    pending = _function_body(
        runtime, "static int dluma_cmm_restoration_request_pending("
    )
    assert "restoration_processing" in pending
    assert pending.index("restoration_processing") < pending.index(
        "state >= DLUMA_CMM_PUBLICATION_ACTIVE"
    )
    assert "restoration_pending" in pending

    # Return recovery reuses the already validated stock transaction and
    # lifecycle hooks. It must not add a guessed content or FX entrypoint.
    assert "UpdateDLCContent" not in restore
    assert "InitFXSystem" not in restore
    assert "ReloadMaterials" not in restore
