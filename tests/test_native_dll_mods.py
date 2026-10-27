import hashlib
import json
import struct
import subprocess
from pathlib import Path

from tools.local_test_paths import DEVTOOLS_ROOT


ROOT = Path(__file__).resolve().parents[1]
NATIVE = (ROOT / "src" / "dlw_native_mods.c").read_text(encoding="utf-8")
HEADER = (ROOT / "src" / "dlw_native_mods.h").read_text(encoding="utf-8")
UI = (ROOT / "src" / "dlw_ui.c").read_text(encoding="utf-8")
EXPORTS = (ROOT / "src" / "dlw_exports.c").read_text(encoding="utf-8")
RECOVERY = (ROOT / "src" / "dlw_startup_recovery.c").read_text(encoding="utf-8")
LUA_DISCOVERY = (ROOT / "src" / "dlw_lua_discovery.c").read_text(
    encoding="utf-8"
)
PATH_SECURITY = (ROOT / "src" / "dlw_path_security.c").read_text(
    encoding="utf-8"
)
C_API = (ROOT / "sdk" / "c" / "dluma_api.h").read_text(encoding="utf-8")
SDK = C_API
SERVICES = (ROOT / "src" / "game" / "dlw_media_service.c").read_text(
    encoding="utf-8"
)
ADAPTER = (ROOT / "src" / "dlw_player_media_adapter.c").read_text(
    encoding="utf-8"
)
HOOK_ATTESTATION = (
    ROOT / "src" / "dlw_internal_hook_attestation.c"
).read_text(encoding="utf-8")
ASSET_ISOLATION = (
    ROOT
    / "src"
    / "fixes"
    / "custom_map_asset_isolation"
    / "CustomMapAssetIsolationFix.cpp"
).read_text(encoding="utf-8")
ENGINE = (ROOT / "src" / "dlw_engine.c").read_text(encoding="utf-8")
PROJECT = (ROOT / "DLUMA.vcxproj").read_text(encoding="utf-8")
LOCALE_EN = (
    ROOT / "PakAssets" / "Locale" / "En" / "loader_texts_all.scr"
).read_text(encoding="utf-8")


def _load_pe_rva_image(path: Path):
    data = path.read_bytes()
    nt = struct.unpack_from("<I", data, 0x3C)[0]
    section_count = struct.unpack_from("<H", data, nt + 6)[0]
    optional_size = struct.unpack_from("<H", data, nt + 20)[0]
    optional = nt + 24
    image_base = struct.unpack_from("<Q", data, optional + 24)[0]
    section_table = optional + optional_size
    sections = []
    for index in range(section_count):
        entry = section_table + index * 40
        name = data[entry : entry + 8].split(b"\0", 1)[0].decode("ascii")
        virtual_size, rva, raw_size, raw = struct.unpack_from(
            "<IIII", data, entry + 8
        )
        characteristics = struct.unpack_from("<I", data, entry + 36)[0]
        sections.append(
            (
                name,
                rva,
                max(virtual_size, raw_size),
                raw,
                raw_size,
                characteristics,
            )
        )

    def section_for_rva(rva: int):
        for section in sections:
            _, start, span, _, _, _ = section
            if start <= rva < start + span:
                return section
        raise AssertionError(f"unmapped RVA 0x{rva:X}")

    def read(rva: int, size: int):
        section = section_for_rva(rva)
        _, start, _, raw, raw_size, _ = section
        delta = rva - start
        assert delta <= raw_size and size <= raw_size - delta
        return data[raw + delta : raw + delta + size]

    return data, image_base, read, section_for_rva


def test_native_registry_is_separate_and_bounded():
    assert "DlumaNativeModSnapshot" in HEADER
    assert "DLUMA_MAX_NATIVE_MODS 128U" in HEADER
    assert "DLUMA_NATIVE_MOD_FILE_MAX_BYTES" in HEADER
    assert "DlumaLuaDiscoveryState" not in NATIVE
    assert "native_mods.ini" in NATIVE
    assert "DLW_NATIVE_MOD_APPROVAL_V1" in NATIVE


def test_discovery_recurses_safely_beneath_stock_mods_folder():
    assert 'L"mods"' in NATIVE
    assert 'L".mod"' in NATIVE
    assert "dluma_native_scan_directory(" in NATIVE
    assert "DLUMA_NATIVE_MAX_DISCOVERY_DEPTH 16U" in NATIVE
    assert "DLUMA_NATIVE_MAX_DISCOVERY_DIRECTORIES 4096U" in NATIVE
    assert "native_mod_directories_scanned" in NATIVE
    assert "native_mod_directory_traversal_truncated" in NATIVE
    assert "dluma_native_path_join_checked(" in NATIVE
    assert "FILE_ATTRIBUTE_REPARSE_POINT" in NATIVE
    assert "dluma_get_final_path_checked" in NATIVE
    assert "GetFinalPathNameByHandleW" in PATH_SECURITY
    assert "GetFileInformationByHandle" in NATIVE
    assert "native_mod_candidate_deduplicated_physical_file" in NATIVE
    scan = NATIVE.split("static void dluma_native_scan_directory", 1)[1].split(
        "static void dluma_native_scan_project", 1
    )[0]
    assert "dluma_native_open_scan_directory(directory, final_mods)" in scan
    assert "data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT" in scan
    assert "dluma_wends_with_icase(data.cFileName, L\".mod\")" in scan
    assert "depth + 1U" in scan
    discovery = NATIVE.split("void dluma_native_mods_discover", 1)[1].split(
        "unsigned int dluma_native_mods_snapshot", 1
    )[0]
    assert "LoadLibrary" not in discovery


def test_nested_native_mod_consent_identity_keeps_full_relative_path():
    candidate = NATIVE.split("static void dluma_native_add_candidate", 1)[1].split(
        "void dluma_native_mods_set_original_gamedll", 1
    )[0]
    assert "const WCHAR* relative = final_path + wcslen(final_mods);" in candidate
    assert "relative_utf8" in candidate
    assert '"mods\\\\"' in candidate
    assert "canonical_relative_path" in candidate
    assert "const WCHAR* base = final_path;" not in candidate


def test_nested_discovery_rejects_truncated_paths_and_sidecars():
    join = NATIVE.split("static int dluma_native_path_join_checked", 1)[1].split(
        "static DWORD dluma_native_u16", 1
    )[0]
    assert "right_length >" in join
    assert "out[0] = 0;" in join
    sidecar = NATIVE.split("static int dluma_native_sidecar_path", 1)[1].split(
        "static void dluma_native_make_default_id", 1
    )[0]
    assert "DLUMA_ARRAY_COUNT(suffix) > (SIZE_T)capacity - n" in sidecar
    assert "sidecar path exceeds the native discovery limit" in NATIVE


def test_discovery_keeps_its_bounded_project_scratch_space_off_game_stack():
    discovery = NATIVE.split("void dluma_native_mods_discover", 1)[1].split(
        "unsigned int dluma_native_mods_snapshot", 1
    )[0]
    assert "DLUMA_NATIVE_MAX_DISCOVERY_PROJECTS" in NATIVE
    assert "dluma_alloc(" in discovery
    assert "dluma_free(projects)" in discovery
    assert "WCHAR projects[" not in discovery


def test_static_pe_classification_fails_closed_for_legacy_modules():
    assert "DLUMA_NATIVE_MOD_GET_API_V2_EXPORT" in NATIVE
    assert "DLUMANativeMod_QueryV1" in NATIVE
    assert "InitializeGameScriptDLL" in NATIVE
    assert "DLW_NATIVE_ABI_LEGACY_V1" in NATIVE
    assert "DLUMA_NATIVE_ABI_LEGACY_GAME_SCRIPT" in NATIVE
    assert "item->abi_kind != DLUMA_NATIVE_ABI_V2" in NATIVE
    assert "module, sidecar, companion pack, path, or file identity " in NATIVE
    assert "changed before approved load" in NATIVE
    assert "dluma_native_revalidate_before_load(&claimed, &artifact)" in NATIVE
    assert "dluma_sha256_handle_hex" in NATIVE
    assert "FILE_SHARE_READ," in NATIVE
    assert "sidecar_sha256" in NATIVE


def test_native_consent_is_exact_and_next_launch_only():
    assert "enabled_next_launch" in NATIVE
    assert "Write disabled first" in NATIVE
    assert "DLUMA_NATIVE_STATE_CHANGED_REVIEW_REQUIRED" in NATIVE
    assert "DLUMA_NATIVE_STATE_DISABLE_NEXT_LAUNCH" in HEADER
    assert "The DLL will not be loaded now" in LOCALE_EN
    assert "FULL PROCESS ACCESS - NOT SANDBOXED" in UI


def test_startup_recovery_suppresses_then_offers_exact_or_all_next_launch_choice():
    assert "startup_recovery.ini" in RECOVERY
    assert '"armed"' in RECOVERY
    assert "previous_startup_incomplete" in RECOVERY
    assert "dluma_startup_recovery_suppresses_mods" in NATIVE
    assert "dluma_startup_recovery_note_native_failure" in NATIVE
    assert "DLUMA_NATIVE_STATE_STARTUP_RECOVERY_SUPPRESSED" in HEADER
    assert "dluma_native_mods_disable_exact_next_launch" in UI
    assert "dluma_native_mods_disable_all_next_launch" in UI
    assert "dluma_lua_disable_all_globals_next_launch" in UI
    assert "MB_YESNO" in UI
    assert "dluma_lua_initial_global_discovery_complete" in LUA_DISCOVERY
    assert "dluma_startup_recovery_prompt_before_game_init" in RECOVERY
    assert "MB_SETFOREGROUND" in RECOVERY
    assert "MB_TOPMOST" in RECOVERY
    assert "dluma_lua_discover_globals();" in EXPORTS
    arm = EXPORTS.index("dluma_startup_recovery_begin")
    global_discovery = EXPORTS.index("dluma_lua_discover_globals();", arm)
    native_discovery = EXPORTS.index("dluma_native_mods_discover();", arm)
    early_prompt = EXPORTS.index(
        "dluma_startup_recovery_prompt_before_game_init();", arm
    )
    native_pre = EXPORTS.index("dluma_native_mods_pre_game_init")
    assert arm < global_discovery < native_discovery < early_prompt < native_pre


def test_global_lua_discovery_uses_heap_scratch_for_early_recovery_scan():
    discovery = LUA_DISCOVERY.split("void dluma_lua_discover_globals(void)", 1)[1].split(
        "unsigned int dluma_lua_discover_locals", 1
    )[0]
    assert "global_lua_discovery_scratch_alloc_failed" in discovery
    initializer = LUA_DISCOVERY.split(
        "static int dluma_lua_workspace_initialize(void)", 1
    )[1].split("void dluma_lua_discovery_shutdown", 1)[0]
    assert initializer.count("dluma_alloc(") >= 5
    assert "WCHAR projects[" not in discovery
    assert "DlumaLuaMetadata next[" not in discovery
    assert "sizeof(*g_dluma_lua_workspace.projects) *" in initializer
    assert "sizeof(projects)" not in discovery
    assert "dluma_lua_discovery_shutdown" in LUA_DISCOVERY


def test_native_callbacks_are_claimed_and_invoked_without_registry_lock():
    assert "DlumaNativeLifecyclePhase" in NATIVE
    assert "lifecycle_epoch" in NATIVE
    assert "next_lifecycle_epoch" in NATIVE
    assert "dluma_native_next_lifecycle_epoch_locked" in NATIVE
    assert "native_mod_stale_epoch_commit_ignored" in NATIVE
    assert "lifecycle_phase !=\n                DLUMA_NATIVE_PHASE_IDLE" in NATIVE
    pre = NATIVE.split("void dluma_native_mods_pre_game_init", 1)[1].split(
        "void dluma_native_mods_post_game_init", 1
    )[0]
    unlock = pre.index("dluma_native_unlock();", pre.index("DLUMA_NATIVE_PHASE_LOADING"))
    load = pre.index("LoadLibraryExW")
    get_api = pre.index("callback_source = get_api_v2()")
    callback = pre.index("callbacks.pre_game_bootstrap(api)")
    assert unlock < load < get_api < callback


def test_loaded_module_identity_is_checked_before_any_export_resolution():
    pre = NATIVE.split("void dluma_native_mods_pre_game_init", 1)[1].split(
        "void dluma_native_mods_post_game_init", 1
    )[0]
    identity = pre.index("dluma_native_loaded_module_matches")
    first_export = pre.index("GetProcAddress")
    assert identity < first_export
    assert "loaded module path or file identity did not match locked artifact" in pre
    assert "dluma_get_module_path_checked" in NATIVE


def test_crash_context_is_preformatted_and_lock_free_to_read():
    copy = NATIVE.split("int dluma_native_mods_copy_crash_context", 1)[1]
    assert "dluma_native_lock" not in copy
    assert "g_dluma_native_crash_context_sequence" in copy
    assert "attempt < 2U" in copy


def test_sdk_is_versioned_c_pod_and_has_chain_callbacks():
    for token in (
        "DLW_NATIVE_MOD_ABI_V2",
        "DlumaNativeModApiV2",
        "DlumaApiV1",
        "DlumaGetServiceFn",
        "DLUMANativeMod_GetApiV2",
    ):
        assert token in SDK + C_API
    assert "uint32_t struct_size;" in C_API
    assert "uint32_t abi_version;" in C_API
    for removed in (
        "DlumaNativeModInfoV1",
        "DlumaNativeHostV1",
        "DLUMANativeMod_QueryV1",
        "DLUMANativeMod_PreGameInitV1",
        "DLUMANativeMod_PostGameInitV1",
        "DLUMANativeMod_ShutdownV1",
    ):
        assert removed not in SDK


def test_v2_api_directory_is_size_prefixed_and_declares_common_service_ids():
    api = C_API.split("typedef struct DlumaApiV1 {", 1)[1].split(
        "} DlumaApiV1;", 1
    )[0]
    fields = (
        "    uint32_t struct_size;",
        "    uint32_t abi_version;",
        "    uint32_t api_major;",
        "    uint32_t api_minor;",
        "    const char* catalog_sha256;",
        "    void* context;",
        "    DlumaGetServiceFn get_service;",
    )
    offsets = [api.index(field) for field in fields]
    assert offsets == sorted(offsets)
    assert "#define DLUMA_NATIVE_MOD_ABI_V2 0x00020000u" in C_API
    assert "minimum_version" in C_API
    assert "output_table" in C_API
    assert "output_size" in C_API

    expected_ids = {
        "DLW_SERVICE_PLAYER_V1": "dluma.player.v1",
        "DLW_SERVICE_EVENTS_V1": "dluma.events.v1",
        "DLW_SERVICE_COMPANION_CONTENT_V1": "dluma.companion_content.v1",
        "DLW_SERVICE_CONFIGURATION_V1": "dluma.configuration.v1",
        "DLW_SERVICE_AUDIO_V1": "dluma.audio.v1",
    }
    for macro, service_id in expected_ids.items():
        assert f'#define {macro} "{service_id}"' in SDK
    for table in (
        "DlumaPlayerV1",
        "DlumaEventsV1",
        "DlumaCompanionContentV1",
        "DlumaConfigurationV1",
        "DlumaAudioV1",
    ):
        body = SDK.split(f"typedef struct {table} {{", 1)[1].split(
            f"}} {table};", 1
        )[0]
        assert body.index("uint32_t struct_size;") < body.index(
            "uint32_t version;"
        ) < body.index("void* context;")


def test_companion_approval_preserves_unpaired_v1_and_binds_pack_in_v2():
    fingerprint = NATIVE.split("static void dluma_native_fingerprint", 1)[1].split(
        "static int dluma_native_compare_entry", 1
    )[0]
    v1 = fingerprint.index('"DLW_NATIVE_MOD_APPROVAL_V1"')
    v2 = fingerprint.index('"DLW_NATIVE_MOD_APPROVAL_V2"')
    assert fingerprint.index("if (!item->companion_pack.declared)") < v1 < v2
    assert '"DLW_NATIVE_MOD_APPROVAL_V1", fields, 0, 8U' in fingerprint
    for token in (
        "item->companion_pack.filename",
        "item->companion_pack.manifest_path",
        "item->companion_pack.sha256",
        "item->companion_pack.volume_serial",
        "item->companion_pack.file_index_high",
        "item->companion_pack.file_index_low",
        "item->companion_pack.file_size_high",
        "item->companion_pack.file_size_low",
    ):
        assert token in fingerprint
    assert '"DLW_NATIVE_MOD_APPROVAL_V2", fields, 0' in fingerprint
    assert "DLUMA_ARRAY_COUNT(fields)" in fingerprint


def test_companion_is_discovered_before_fingerprint_and_locked_before_load():
    candidate = NATIVE.split("static void dluma_native_add_candidate", 1)[1].split(
        "void dluma_native_mods_set_original_gamedll", 1
    )[0]
    assert candidate.index(
        "dluma_native_read_sidecar(item);"
    ) < candidate.index("dluma_native_fingerprint(item);")

    revalidate = NATIVE.split("static int dluma_native_revalidate_before_load", 1)[
        1
    ].split("static int dluma_native_loaded_module_matches", 1)[0]
    assert "dluma_companion_archive_lock(" in revalidate
    pre = NATIVE.split("void dluma_native_mods_pre_game_init", 1)[1].split(
        "void dluma_native_mods_post_game_init", 1
    )[0]
    recheck = pre.index("dluma_native_revalidate_before_load(&claimed, &artifact)")
    load = pre.index("LoadLibraryExW")
    assert recheck < load


def test_common_api_owner_exists_before_pre_and_revokes_after_shutdown():
    pre = NATIVE.split("void dluma_native_mods_pre_game_init", 1)[1].split(
        "void dluma_native_mods_post_game_init", 1
    )[0]
    initialize = pre.index("dluma_native_api_initialize_entry(")
    publish = pre.index("dluma_api_runtime_api(", initialize)
    callback = pre.index("callbacks.pre_game_bootstrap(api)", publish)
    assert initialize < publish < callback

    shutdown = NATIVE.split("void dluma_native_mods_shutdown_chain", 1)[1].split(
        "int dluma_native_mods_copy_crash_context", 1
    )[0]
    drained_guard = shutdown.index("if (loader_callbacks_drained && api)")
    callback = shutdown.index("callbacks.shutdown(api", drained_guard)
    begin = shutdown.index("dluma_mod_owner_begin_revoke(api_owner)", callback)
    revoke = shutdown.index("dluma_game_host_context_revoke(", begin)
    finish = shutdown.index("dluma_mod_owner_finish_revoke(api_owner)", revoke)
    assert drained_guard < callback < begin < revoke < finish


def test_common_media_frame_pump_and_global_shutdown_order_are_explicit():
    frame = ENGINE.split("static ULONG_PTR __fastcall dluma_game_frame_hook", 1)[1].split(
        "static void __fastcall dluma_i_level_destructor_hook", 1
    )[0]
    service_tick = frame.index("dluma_media_service_tick(")
    localization_tick = frame.index("dluma_localization_game_thread_tick()")
    ui_tick = frame.index("dluma_ui_game_thread_tick(")
    assert service_tick < localization_tick < ui_tick

    init = EXPORTS.split("static void* dluma_player_initialize(void)", 1)[1].split(
        "static __int64 dluma_player_shutdown(void)", 1
    )[0]
    service_start = init.index("dluma_media_service_start();")
    native_pre = init.index("dluma_native_mods_pre_game_init", service_start)
    original = init.index("manager = g_original_initialize()", native_pre)
    bind = init.index("dluma_media_service_bind", original)
    native_post = init.index("dluma_native_mods_post_game_init", bind)
    assert service_start < native_pre < original < bind < native_post

    shutdown = EXPORTS.split("static __int64 dluma_player_shutdown(void)", 1)[1].split(
        'extern "C" __declspec(dllexport) void*', 1
    )[0]
    service_quiesce = shutdown.index("dluma_media_service_quiesce_all();")
    host_stop = shutdown.index("dluma_host_stop();", service_quiesce)
    native_shutdown = shutdown.index("dluma_native_mods_shutdown_chain", host_stop)
    original_shutdown = shutdown.index("g_original_shutdown()", native_shutdown)
    assert service_quiesce < host_stop < native_shutdown < original_shutdown
    assert (
        "loader_stopped && services_stopped && quest_phase_stopped"
        in shutdown
    )


def test_reinitialize_after_shutdown_is_rejected_before_returning_cached_manager():
    init = EXPORTS.split("static void* dluma_player_initialize(void)", 1)[1].split(
        "static __int64 dluma_player_shutdown(void)", 1
    )[0]
    reject = init.index("&g_dluma_shutdown_entered, 0, 0")
    shutdown_count = init.index("&g_original_shutdown_count, 0, 0", reject)
    stale_manager = init.index("if (g_original_script_manager)", shutdown_count)
    assert reject < shutdown_count < stale_manager
    assert "reinitialization after shutdown is unsupported" in init
    assert "dluma_atomic_exchange(&g_dluma_shutdown_entered, 0);" not in init


def test_common_media_sources_are_in_the_explicit_project_manifest():
    for path in (
        r"src\dlw_vehicle_gate_core.h",
        r"src\dlw_player_media_adapter.c",
        r"src\dlw_companion_archive.c",
        r"src\game\dlw_media_service.c",
        r"src\game\dlw_game_host_context.c",
        r"src\dlw_player_media_adapter.h",
        r"src\dlw_companion_archive.h",
        r"src\game\dlw_media_service.h",
        r"src\game\dlw_game_host_context.h",
        r"src\dlw_internal_hook_attestation.c",
        r"src\dlw_internal_hook_attestation.h",
    ):
        assert PROJECT.count(f'Include="{path}"') == 1


def test_player_media_adapter_keeps_exact_profile_constants():
    hashes = (
        "61D9DFB20CFA179C6D097537DA46BA9DBE3D58797938762EF4EACFF5DFCC4B3A",
        "A18CA1103614947AE7FF956C6852F45123B440563A967D9E1E496C42FA574A3C",
        "CBC7DD7923778936341CDFB755474A0C714079D910849DF16FFC5EC88A103B95",
        "CB6FF4B0C9AA62C8A35742D463AE3C34FE803767E8E2AC9B33E4604886B9846D",
        "1BB15CC4F8062235F128822FC6F29E72A2E1EC6B6E40F41122A372FB9713BB17",
    )
    for digest in hashes:
        assert digest in ADAPTER
    for token in (
        "DLUMA_DL_LOCAL_BUGGY_RVA 0x004B3260UL",
        "DLUMA_DL_PLAYERDI_TYPE_RVA 0x01CA3FB0UL",
        "DLUMA_DL_PLAYERDI_PRIMARY_VTABLE_RVA 0x017DD738UL",
        "DLUMA_DL_PLAYERDI_ACTOR_VTABLE_RVA 0x017DE468UL",
        "DLUMA_DL_PLAYERDI_CURRENT_VEHICLE_RVA 0x00B3B830UL",
        "DLUMA_DL_VEHICLE_BASE_GET_DRIVER_RVA 0x00F8C310UL",
        "DLUMA_DL_CAR_BUGGY_VTABLE_RVA 0x018C37C8UL",
        "DLUMA_DL_CAR_BUGGY_CONTROL_VTABLE_RVA 0x018C3D28UL",
        "DLUMA_DL_CAR_BUGGY_LINEAR_VELOCITY_SLOT 0x01B8U",
        "DLUMA_DL_CAR_BUGGY_LINEAR_VELOCITY_RVA 0x00E63160UL",
        "DLUMA_DL_CAR_BUGGY_PHYSICS_BIND_IAT_RVA 0x013CD360UL",
        "DLUMA_DL_CAR_BUGGY_LINEAR_VELOCITY_IAT_RVA 0x013CD7A0UL",
        "DLUMA_DL_PHYSICS_BIND_RVA 0x00141870UL",
        "DLUMA_DL_PHYSICS_BIND_VTABLE_RVA 0x008B1158UL",
        "DLUMA_DL_PHYSICS_INTERFACE_RVA 0x00383090UL",
        "DLUMA_DL_PHYSICS_BODY_AT_RVA 0x003833B0UL",
        "DLUMA_DL_LINEAR_VELOCITY_RVA 0x0034A3B0UL",
        "DLUMA_DL_AUDIO_SINGLETON_RVA 0x00AC8D78UL",
        "DLUMA_DL_AUDIO_VTABLE_RVA 0x008A9628UL",
    ):
        assert token in ADAPTER


def test_exact_player_cooperative_session_rtti_profiles_and_getter():
    pe = DEVTOOLS_ROOT / "gamedll_x64_rwdi.dll"
    if not pe.exists():
        return
    data, image_base, read, section_for_rva = _load_pe_rva_image(pe)
    assert hashlib.sha256(data).hexdigest().upper() == (
        "CBC7DD7923778936341CDFB755474A0C714079D910849DF16FFC5EC88A103B95"
    )
    assert image_base == 0x180000000
    session_base_descriptor = 0x01A57378
    session_base_descriptor_fields = (
        0x01CBCBB8,
        8,
        0,
        -1,
        0,
        0x40,
        0x01A573A0,
    )
    profiles = (
        {
            "name": b".?AVSessionDI@@",
            "type": 0x01CBCBB8,
            "col": 0x01A57B08,
            "vtable": 0x01591FC8,
            "hierarchy": 0x01A573A0,
            "array": 0x01A573C0,
            "count": 9,
            "self": 0x01A57378,
            "prefix": (0x01A57378,),
            "session_index": 0,
        },
        {
            "name": b".?AVSessionCooperative@@",
            "type": 0x01CBCBD8,
            "col": 0x01A572E0,
            "vtable": 0x0158E488,
            "hierarchy": 0x01A57308,
            "array": 0x01A57320,
            "count": 10,
            "self": 0x01A57498,
            "prefix": (0x01A57498, 0x01A57378),
            "session_index": 1,
        },
        {
            "name": b".?AVSessionCooperativeDI@@",
            "type": 0x01CBCC50,
            "col": 0x01A575B8,
            "vtable": 0x0158F838,
            "hierarchy": 0x01A575E0,
            "array": 0x01A57600,
            "count": 11,
            "self": 0x01A57660,
            "prefix": (0x01A57660, 0x01A57498, 0x01A57378),
            "session_index": 2,
        },
    )
    for profile in profiles:
        type_name = read(profile["type"] + 16, 64).split(b"\0", 1)[0]
        assert type_name == profile["name"]
        assert struct.unpack("<Q", read(profile["vtable"] - 8, 8))[0] == (
            image_base + profile["col"]
        )
        col = struct.unpack("<IIIIII", read(profile["col"], 24))
        assert col == (
            1,
            0,
            0,
            profile["type"],
            profile["hierarchy"],
            profile["col"],
        )
        hierarchy = struct.unpack(
            "<IIII", read(profile["hierarchy"], 16)
        )
        assert hierarchy == (
            0,
            1,
            profile["count"],
            profile["array"],
        )
        base_entries = struct.unpack(
            f"<{profile['count']}I",
            read(profile["array"], profile["count"] * 4),
        )
        assert base_entries[: len(profile["prefix"])] == profile["prefix"]
        assert base_entries[profile["session_index"]] == (
            session_base_descriptor
        )
        self_descriptor = struct.unpack(
            "<IIiiiII", read(profile["self"], 28)
        )
        assert self_descriptor == (
            profile["type"],
            profile["count"] - 1,
            0,
            -1,
            0,
            0x40,
            profile["hierarchy"],
        )
        assert struct.unpack(
            "<IIiiiII", read(session_base_descriptor, 28)
        ) == session_base_descriptor_fields
        assert struct.unpack(
            "<Q", read(profile["vtable"] + 0x7A0, 8)
        )[0] == image_base + 0x006AD140
        for metadata_rva in (
            profile["col"],
            profile["vtable"] - 8,
            profile["hierarchy"],
            profile["array"],
            profile["self"],
            session_base_descriptor,
        ):
            characteristics = section_for_rva(metadata_rva)[5]
            assert characteristics & 0x40000000
            assert not characteristics & 0x80000000
    assert read(0x006AD140, 8) == bytes.fromhex(
        "48 8B 81 B0 00 00 00 C3"
    )


def test_exact_playerdi_vehicle_driver_owner_route():
    gamedll = DEVTOOLS_ROOT / "gamedll_x64_rwdi.dll"
    if not gamedll.exists():
        return
    data, image_base, read, section_for_rva = _load_pe_rva_image(gamedll)
    assert hashlib.sha256(data).hexdigest().upper() == (
        "CBC7DD7923778936341CDFB755474A0C714079D910849DF16FFC5EC88A103B95"
    )
    assert image_base == 0x180000000

    player_type = 0x01CA3FB0
    player_hierarchy = 0x01AA4EC8
    player_array = 0x01AA4EE0
    player_self = 0x01AA4EA0
    actor_type = 0x01CA9818
    actor_hierarchy = 0x01A1C648
    actor_array = 0x01A1C660
    actor_descriptor = 0x01A1C620
    player_vtables = (
        (0x017DD738, 0x01AD6198, 0),
        (0x017DE468, 0x01AD6148, 0x28),
    )
    assert read(player_type + 16, 64).split(b"\0", 1)[0] == (
        b".?AVPlayerDI@@"
    )
    assert read(actor_type + 16, 64).split(b"\0", 1)[0] == (
        b".?AVActorDI@@"
    )
    for vtable, col, offset in player_vtables:
        assert struct.unpack("<Q", read(vtable - 8, 8))[0] == (
            image_base + col
        )
        assert struct.unpack("<IIIIII", read(col, 24)) == (
            1,
            offset,
            0,
            player_type,
            player_hierarchy,
            col,
        )
    assert struct.unpack("<IIII", read(player_hierarchy, 16)) == (
        0,
        5,
        42,
        player_array,
    )
    player_bases = struct.unpack("<42I", read(player_array, 42 * 4))
    assert player_bases[:2] == (player_self, actor_descriptor)
    assert struct.unpack("<IIiiiII", read(player_self, 28)) == (
        player_type,
        41,
        0,
        -1,
        0,
        0x40,
        player_hierarchy,
    )
    assert struct.unpack("<IIiiiII", read(actor_descriptor, 28)) == (
        actor_type,
        27,
        0,
        -1,
        0,
        0x40,
        actor_hierarchy,
    )
    assert struct.unpack("<IIII", read(actor_hierarchy, 16)) == (
        0,
        5,
        28,
        actor_array,
    )
    assert struct.unpack("<I", read(actor_array, 4))[0] == (
        actor_descriptor
    )
    assert struct.unpack("<Q", read(0x017DD738 + 0x8D0, 8))[0] == (
        image_base + 0x00B3B830
    )
    assert struct.unpack("<Q", read(0x017DE468 + 0x690, 8))[0] == (
        image_base + 0x00937E80
    )
    assert read(0x00B3B830, 32) == bytes.fromhex(
        "48 89 5C 24 08 48 89 74 24 10 57 48 83 EC 20 48 "
        "8B B9 00 0C 00 00 33 DB 48 8B F1 48 85 FF 74 79"
    )
    assert read(0x00937E80, 3) == bytes.fromhex("B0 01 C3")

    car_type = 0x01CA61A8
    vehicle_type = 0x01CB2BB8
    car_hierarchy = 0x01B0C798
    car_array = 0x01B0C7B0
    car_self = 0x01B0C958
    vehicle_hierarchy = 0x01B0C8A8
    vehicle_array = 0x01B0C8C0
    vehicle_descriptor = 0x01B0C880
    assert read(car_type + 16, 64).split(b"\0", 1)[0] == (
        b".?AVCarBuggy@@"
    )
    assert read(vehicle_type + 16, 64).split(b"\0", 1)[0] == (
        b".?AVVehicleBase@@"
    )
    assert struct.unpack("<IIII", read(car_hierarchy, 16)) == (
        0,
        1,
        20,
        car_array,
    )
    car_bases = struct.unpack("<20I", read(car_array, 20 * 4))
    assert car_bases[:2] == (car_self, vehicle_descriptor)
    assert struct.unpack("<IIiiiII", read(car_self, 28)) == (
        car_type,
        19,
        0,
        -1,
        0,
        0x40,
        car_hierarchy,
    )
    assert struct.unpack(
        "<IIiiiII", read(vehicle_descriptor, 28)
    ) == (
        vehicle_type,
        17,
        0,
        -1,
        0,
        0x40,
        vehicle_hierarchy,
    )
    assert struct.unpack("<IIII", read(vehicle_hierarchy, 16)) == (
        0,
        5,
        18,
        vehicle_array,
    )
    assert struct.unpack("<I", read(vehicle_array, 4))[0] == (
        vehicle_descriptor
    )

    get_driver = 0x00F8C310
    assert read(get_driver, 80) == bytes.fromhex(
        "48 8B 81 D0 01 00 00 33 D2 8B CA 48 85 C0 74 3D "
        "39 50 F0 7D 06 48 8B 40 10 EB 06 48 8B C8 48 8B "
        "00 48 85 C0 75 EA 48 85 C9 74 22 39 51 F0 7F 1D "
        "48 8B 41 F8 48 85 C0 74 14 48 39 90 48 03 00 00 "
        "74 07 48 8B 90 50 03 00 00 48 8B C2 C3 33 C0 C3"
    )

    def relative_call_target(call_rva: int) -> int:
        assert read(call_rva, 1) == b"\xE8"
        displacement = struct.unpack("<i", read(call_rva + 1, 4))[0]
        return call_rva + 5 + displacement

    assert relative_call_target(0x0035B501) == get_driver
    assert read(0x0035B506, 3) == bytes.fromhex("49 3B C5")
    assert relative_call_target(0x00F5A8DB) == get_driver
    assert read(0x00F5A8E0, 13) == bytes.fromhex(
        "48 85 C0 74 17 48 8D 48 28 48 8B 40 28"
    )
    assert bytes.fromhex("FF 90 D0 08 00 00") in read(
        0x00F5A890, 123
    )

    velocity_wrapper = 0x00E63160
    velocity_slot = 0x018C37C8 + 0x01B8
    velocity_bytes = bytes.fromhex(
        "40 53 48 83 EC 20 48 83 C1 18 48 8B DA FF 15 ED "
        "A1 56 00 48 8B C8 4C 8B 00 41 FF 90 A0 00 00 00 "
        "48 85 C0 74 22 4C 8B 00 33 D2 48 8B C8 41 FF 90 "
        "10 01 00 00 48 85 C0 74 0E 48 8B C8 FF 15 FE A5 "
        "56 00 4C 8B D8 EB 07 4C 8D 1D 82 51 EA 00 41 8B "
        "03 89 03 41 8B 43 04 89 43 04 41 8B 43 08 89 43 "
        "08 48 8B C3 48 83 C4 20 5B C3"
    )
    assert struct.unpack("<Q", read(velocity_slot, 8))[0] == (
        image_base + velocity_wrapper
    )
    assert read(velocity_wrapper, len(velocity_bytes)) == velocity_bytes

    def rip_indirect_slot(call_rva: int) -> int:
        assert read(call_rva, 2) == b"\xFF\x15"
        displacement = struct.unpack("<i", read(call_rva + 2, 4))[0]
        return call_rva + 6 + displacement

    assert rip_indirect_slot(velocity_wrapper + 0x0D) == 0x013CD360
    assert rip_indirect_slot(velocity_wrapper + 0x3C) == 0x013CD7A0
    for import_slot in (0x013CD360, 0x013CD7A0):
        characteristics = section_for_rva(import_slot)[5]
        assert characteristics & 0x40000000

    for metadata_rva in (
        0x017DD738 - 8,
        0x017DE468 - 8,
        0x01AD6198,
        0x01AD6148,
        player_hierarchy,
        player_array,
        player_self,
        actor_hierarchy,
        actor_array,
        actor_descriptor,
        car_hierarchy,
        car_array,
        car_self,
        vehicle_hierarchy,
        vehicle_array,
        vehicle_descriptor,
    ):
        characteristics = section_for_rva(metadata_rva)[5]
        assert characteristics & 0x40000000
        assert not characteristics & 0x80000000
    for code_rva in (
        0x00B3B830,
        0x00937E80,
        get_driver,
        0x0035B501,
        0x00F5A8DB,
        velocity_wrapper,
    ):
        assert section_for_rva(code_rva)[5] & 0x20000000


def test_exact_player_leveldi_and_music_manager_igsobject_routes():
    gamedll = DEVTOOLS_ROOT / "gamedll_x64_rwdi.dll"
    engine = DEVTOOLS_ROOT / "engine_x64_rwdi.dll"
    if not gamedll.exists() or not engine.exists():
        return
    data, image_base, read, section_for_rva = _load_pe_rva_image(gamedll)
    assert hashlib.sha256(data).hexdigest().upper() == (
        "CBC7DD7923778936341CDFB755474A0C714079D910849DF16FFC5EC88A103B95"
    )
    assert image_base == 0x180000000
    routes = (
        {
            "name": b".?AVLevelDI@@",
            "type": 0x01CA2F30,
            "vtable": 0x015822E8,
            "col": 0x01A54340,
            "offset": 0x10,
            "hierarchy": 0x01A54390,
            "attributes": 5,
            "count": 13,
            "array": 0x01A543B0,
            "igs_index": 4,
            "igs_descriptor": 0x01A05128,
            "igs_member": 0x10,
        },
        {
            "name": b".?AVMusicManager@@",
            "type": 0x01CD2B48,
            "vtable": 0x0170D318,
            "col": 0x01AA7060,
            "offset": 0,
            "hierarchy": 0x01AA7088,
            "attributes": 1,
            "count": 8,
            "array": 0x01AA70A0,
            "igs_index": 2,
            "igs_descriptor": 0x01A05248,
            "igs_member": 0,
        },
    )
    for route in routes:
        assert read(route["type"] + 16, 64).split(b"\0", 1)[0] == (
            route["name"]
        )
        assert struct.unpack("<Q", read(route["vtable"] - 8, 8))[0] == (
            image_base + route["col"]
        )
        assert struct.unpack("<IIIIII", read(route["col"], 24)) == (
            1,
            route["offset"],
            0,
            route["type"],
            route["hierarchy"],
            route["col"],
        )
        assert struct.unpack(
            "<IIII", read(route["hierarchy"], 16)
        ) == (
            0,
            route["attributes"],
            route["count"],
            route["array"],
        )
        base_entries = struct.unpack(
            f"<{route['count']}I",
            read(route["array"], route["count"] * 4),
        )
        assert base_entries[route["igs_index"]] == (
            route["igs_descriptor"]
        )
        assert struct.unpack(
            "<IIiiiII", read(route["igs_descriptor"], 28)
        ) == (
            0x01C952A8,
            4,
            route["igs_member"],
            -1,
            0,
            0x40,
            0x01A05270,
        )
        assert read(0x01C952A8 + 16, 64).split(b"\0", 1)[0] == (
            b".?AVIGSObject@@"
        )
        for metadata_rva in (
            route["vtable"] - 8,
            route["col"],
            route["hierarchy"],
            route["array"],
            route["igs_descriptor"],
            0x01A05270,
        ):
            characteristics = section_for_rva(metadata_rva)[5]
            assert characteristics & 0x40000000
            assert not characteristics & 0x80000000
    assert struct.unpack("<IIII", read(0x01A05270, 16)) == (
        0,
        1,
        5,
        0x01A05288,
    )
    assert struct.unpack("<I", read(0x01A05288, 4))[0] == 0x01A05248

    manager_vtables = (
        0x0170D318,
        0x0170DAB0,
        0x0170DAD8,
        0x0170DAE8,
    )
    for vtable_rva in manager_vtables:
        vtable_characteristics = section_for_rva(vtable_rva)[5]
        assert vtable_characteristics & 0x40000000
        assert not vtable_characteristics & 0x80000000
        col_va = struct.unpack("<Q", read(vtable_rva - 8, 8))[0]
        assert image_base <= col_va < image_base + len(data)
        col_rva = col_va - image_base
        col_characteristics = section_for_rva(col_rva)[5]
        assert col_characteristics & 0x40000000
        assert not col_characteristics & 0x80000000
        target_va = struct.unpack("<Q", read(vtable_rva, 8))[0]
        assert image_base <= target_va < image_base + len(data)
        target_characteristics = section_for_rva(target_va - image_base)[5]
        assert target_characteristics & 0x20000000

    engine_data, engine_base, engine_read, _ = _load_pe_rva_image(engine)
    assert hashlib.sha256(engine_data).hexdigest().upper() == (
        "A18CA1103614947AE7FF956C6852F45123B440563A967D9E1E496C42FA574A3C"
    )
    assert engine_base == 0x180000000
    assert engine_read(0x00277A00, 21) == bytes.fromhex(
        "48 8B 41 20 48 8B 40 48 48 85 C0 74 08 "
        "48 8B 80 B0 00 00 00 C3"
    )


def test_exact_player_wave_bank_outer_route():
    engine = DEVTOOLS_ROOT / "engine_x64_rwdi.dll"
    if not engine.exists():
        return
    data, image_base, read, section_for_rva = _load_pe_rva_image(engine)
    assert hashlib.sha256(data).hexdigest().upper() == (
        "A18CA1103614947AE7FF956C6852F45123B440563A967D9E1E496C42FA574A3C"
    )
    assert image_base == 0x180000000
    outer_rva = 0x000FA3B0
    call_rva = 0x000FA44D
    inner_rva = 0x00100920
    audio_vtable_rva = 0x008A9628
    assert read(outer_rva, 16) == bytes.fromhex(
        "48 89 5C 24 08 48 89 6C 24 10 48 89 74 24 18 48"
    )
    assert read(call_rva, 1) == b"\xE8"
    displacement = struct.unpack("<i", read(call_rva + 1, 4))[0]
    assert call_rva + 5 + displacement == inner_rva
    assert struct.unpack(
        "<Q", read(audio_vtable_rva + 0x28, 8)
    )[0] == image_base + outer_rva
    for rva in (outer_rva, call_rva, inner_rva):
        characteristics = section_for_rva(rva)[5]
        assert characteristics & 0x20000000


def test_player_media_rtti_names_allow_writable_module_data_only():
    exact_text = ADAPTER.split(
        "static int dluma_player_media_exact_text", 1
    )[1].split(
        "static int dluma_player_media_validate_rtti_vtable", 1
    )[0]
    assert "dluma_player_media_module_range(" in exact_text
    assert "dluma_is_readable(text, length)" in exact_text
    assert "memcmp(text, expected, length) == 0" in exact_text
    assert "dluma_player_media_readonly_range(" not in exact_text

    rtti = ADAPTER.split(
        "static int dluma_player_media_validate_rtti_vtable", 1
    )[1].split(
        "static int dluma_player_media_live_vtable_in_module", 1
    )[0]
    assert rtti.count("dluma_player_media_readonly_range(") >= 3
    assert "dluma_player_media_exact_text(" in rtti
    for exact_name in (
        ".?AVPlayerDI@@",
        ".?AVActorDI@@",
        ".?AVVehicleBase@@",
        ".?AVCarBuggy@@",
        ".?AVCPhysicsBind@@",
    ):
        assert exact_name in ADAPTER


def test_player_media_live_session_gate_is_finite_and_clause_specific():
    table = ADAPTER.split(
        "g_dluma_player_media_session_classes[]", 1
    )[1].split(
        "typedef char DlumaPlayerMediaSessionClassProfileCount", 1
    )[0]
    for class_name in (
        '"SessionDI"',
        '"SessionCooperative"',
        '"SessionCooperativeDI"',
    ):
        assert table.count(class_name) == 1
    for rva in (
        "DLUMA_DL_SESSIONDI_TYPE_RVA 0x01CBCBB8UL",
        "DLUMA_DL_SESSIONDI_COL_RVA 0x01A57B08UL",
        "DLUMA_DL_SESSIONDI_VTABLE_RVA 0x01591FC8UL",
        "DLUMA_DL_SESSION_COOPERATIVE_TYPE_RVA 0x01CBCBD8UL",
        "DLUMA_DL_SESSION_COOPERATIVE_COL_RVA 0x01A572E0UL",
        "DLUMA_DL_SESSION_COOPERATIVE_VTABLE_RVA 0x0158E488UL",
        "DLUMA_DL_SESSION_COOPERATIVE_DI_TYPE_RVA 0x01CBCC50UL",
        "DLUMA_DL_SESSION_COOPERATIVE_DI_COL_RVA 0x01A575B8UL",
        "DLUMA_DL_SESSION_COOPERATIVE_DI_VTABLE_RVA 0x0158F838UL",
    ):
        assert rva in ADAPTER

    metadata = ADAPTER.split(
        "static int dluma_player_media_base_descriptor_matches", 1
    )[1].split(
        "static int dluma_player_media_validate_live_rtti_object", 1
    )[0]
    for proof in (
        "dluma_player_media_validate_rtti_vtable(",
        "col->class_hierarchy_rva !=",
        "hierarchy->signature != 0U",
        "hierarchy->attributes != 1U",
        "hierarchy->base_count != session_profile->base_count",
        "hierarchy->base_array_rva !=",
        "dluma_player_media_readonly_range(",
        "descriptor->member_displacement == 0",
        "descriptor->vbtable_displacement == -1",
        "descriptor->displacement_inside_vbtable == 0",
        "DLUMA_DL_SESSIONDI_BASE_DESCRIPTOR_RVA",
    ):
        assert proof in metadata

    selector = ADAPTER.split(
        "dluma_player_media_session_profile_for_vtable(", 1
    )[1].split(
        "static int dluma_player_media_validate_live_session_object", 1
    )[0]
    assert "g_dluma_player_media_session_classes" in selector
    assert "vtable ==" in selector
    assert "return 0;" in selector

    live = ADAPTER.split(
        "static int dluma_player_media_validate_live_session_object", 1
    )[1].split(
        "static int dluma_player_media_engine_export", 1
    )[0]
    for proof in (
        "DLUMA_DL_LIVE_GAME_SESSION_OBJECT_UNREADABLE",
        "DLUMA_DL_LIVE_GAME_SESSION_CLASS_UNSUPPORTED",
        "DLUMA_DL_LIVE_GAME_SESSION_RTTI_INVALID",
        "DLUMA_DL_LIVE_GAME_SESSION_LEVEL_SLOT_MISMATCH",
        "dluma_player_media_validate_session_profile_metadata(",
        "dluma_player_media_validate_live_rtti_object(",
        "dluma_player_media_live_vtable_in_module(",
    ):
        assert proof in live
    generic = ADAPTER.split(
        "static int dluma_player_media_validate_live_rtti_object", 1
    )[1].split(
        "static int dluma_player_media_live_vtable_in_module", 1
    )[0]
    assert "col->type_descriptor_rva != expected_type_rva" in generic
    assert "player_media_game_session_class" in ADAPTER


def test_player_media_static_bindings_are_separate_from_live_readiness():
    wave_bank = ADAPTER.split(
        "static int dluma_player_media_bind_wave_bank_detailed(", 2
    )[1].split(
        "static int dluma_player_media_bind_wave_exports", 1
    )[0]
    music = ADAPTER.split(
        "static int dluma_player_media_bind_music_manager_detailed(", 1
    )[1].split(
        "static int dluma_player_media_bind_local_buggy", 1
    )[0]
    assert "dluma_player_media_validate_audio_object" not in wave_bank
    assert "dluma_player_media_validate_music_manager" not in music
    assert "profile->load_wave_bank =" in wave_bank
    assert "profile->music_stop =" in music
    assert "profile->music_start =" in music

    live_probe = ADAPTER.split(
        "static void dluma_player_media_probe_live_readiness", 1
    )[1].split(
        "static int dluma_player_media_bind_wave_bank", 1
    )[0]
    assert "DLUMA_DL_LIVE_READINESS_PROBE_MS" in live_probe
    assert "dluma_player_media_validate_audio_object_detailed" in live_probe
    assert "dluma_player_media_resolve_music_manager_detailed" in live_probe
    assert "dluma_player_media_bind_wave_bank_detailed" in live_probe
    assert "dluma_player_media_music_snapshot_take" in live_probe
    for diagnostic in (
        "player_media_wave_bank_live_ready",
        "player_media_wave_bank_binding_ready",
        "player_media_music_manager_handoff_ready",
        "player_media_music_manager_source",
        "player_media_game_session_class",
        "player_media_music_filter_ready",
        "waiting_for_live_objects",
    ):
        assert diagnostic in live_probe


def test_player_media_manager_readiness_is_transactional_and_clause_specific():
    validator = ADAPTER.split(
        "static int dluma_player_media_validate_music_manager_object_detailed",
        1,
    )[1].split(
        "static int dluma_player_media_resolve_game_session_music_manager",
        1,
    )[0]
    expected_checks = (
        (
            "*(void**)manager !=",
            "DLUMA_DL_LIVE_MANAGER_PRIMARY_VTABLE_MISMATCH",
        ),
        (
            "*(void**)((BYTE*)manager + 8U) !=",
            "DLUMA_DL_LIVE_MANAGER_SECONDARY_VTABLE_8_MISMATCH",
        ),
        (
            "*(void**)((BYTE*)manager + 16U) !=",
            "DLUMA_DL_LIVE_MANAGER_SECONDARY_VTABLE_16_MISMATCH",
        ),
        (
            "*(void**)((BYTE*)manager + 48U) !=",
            "DLUMA_DL_LIVE_MANAGER_SECONDARY_VTABLE_48_MISMATCH",
        ),
    )
    for comparison, reason in expected_checks:
        check = validator.index(comparison)
        diagnostic = validator.index(reason, check)
        assert check < diagnostic
    assert "DLUMA_DL_LIVE_MANAGER_OBJECT_UNREADABLE" in validator
    assert "DLUMA_DL_LIVE_MANAGER_PRIMARY_TARGET_INVALID" in validator
    assert "*reason_out = DLUMA_DL_LIVE_READY;" in validator

    for reason_name in (
        "resolver_argument_invalid",
        "rt_dynamic_cast_unbound",
        "get_session_level_unbound",
        "get_object_level_unbound",
        "manager_object_unreadable",
        "manager_primary_vtable_mismatch",
        "manager_secondary_vtable_8_mismatch",
        "manager_secondary_vtable_16_mismatch",
        "manager_secondary_vtable_48_mismatch",
        "manager_primary_target_invalid",
        "manager_owner_query_failed",
        "manager_owner_mismatch",
    ):
        assert f'return "{reason_name}";' in ADAPTER

    resolver = ADAPTER.split(
        "static int dluma_player_media_resolve_music_manager_detailed", 1
    )[1].split(
        "static int dluma_player_media_validate_music_manager(", 1
    )[0]
    output_reset = resolver.index(
        "if (owner_level_out) *owner_level_out = 0;"
    )
    first_validation = resolver.index("if (!profile ||")
    assert output_reset < first_validation
    assert (
        "if (source_out) *source_out = DLUMA_DL_MUSIC_SOURCE_NONE;"
        in resolver
    )
    assert resolver.count(
        "dluma_player_media_music_result_complete("
    ) == 3
    assert resolver.count("*owner_level_out = owner_level;") == 3
    assert resolver.count("*manager_out = manager;") == 3

    probe = ADAPTER.split(
        "static void dluma_player_media_probe_live_readiness", 1
    )[1].split(
        "static int dluma_player_media_bind_wave_bank_detailed", 1
    )[0]
    assert "player_media_readiness_invariant_violation" in probe
    assert "partial_manager_state_cleared" in probe
    assert "owner_level = 0;" in probe
    assert "manager = 0;" in probe
    assert "music_source = DLUMA_DL_MUSIC_SOURCE_NONE;" in probe
    assert "profile->live_owner_level = owner_level;" in probe
    assert "profile->live_level_di = level_di;" in probe
    assert "profile->live_music_manager = manager;" in probe
    for label in (
        "player_media_music_manager_pointer",
        "player_media_music_manager_primary_vtable_rva",
        "player_media_music_manager_vtable_8_rva",
        "player_media_music_manager_vtable_16_rva",
        "player_media_music_manager_vtable_48_rva",
        "player_media_music_manager_expected_primary_rva",
        "player_media_music_manager_expected_vtable_8_rva",
        "player_media_music_manager_expected_vtable_16_rva",
        "player_media_music_manager_expected_vtable_48_rva",
        "player_media_music_manager_validation_stage",
    ):
        assert label in probe


def test_player_media_filter_override_is_optional_for_music_handoff():
    capabilities = ADAPTER.split(
        "unsigned int dluma_player_media_adapter_capabilities", 1
    )[1].split(
        "int dluma_player_media_adapter_get_vehicle_state", 1
    )[0]
    frontend_handoff = capabilities.split(
        "capabilities |= DLUMA_PLAYER_MEDIA_CAP_MUSIC_EXCLUSIVE;", 1
    )[0]
    stock_handoff = capabilities.split(
        "} else if (dluma_player_media_validate_music_manager", 1
    )[1].split(
        "capabilities |= DLUMA_PLAYER_MEDIA_CAP_MUSIC_EXCLUSIVE;", 1
    )[0]
    override = capabilities.split(
        "DLUMA_PLAYER_MEDIA_CAP_MUSIC_FILTER;", 1
    )[0].rsplit("if (", 1)[1]
    assert "profile->frontend_music_ready" in frontend_handoff
    assert "profile->frontend_music_mute" in frontend_handoff
    assert "profile->frontend_music_unmute" in frontend_handoff
    assert "profile->music_manager_ready" in stock_handoff
    assert "profile->music_stop" in stock_handoff
    assert "profile->music_start" in stock_handoff
    assert "music_filter" not in frontend_handoff
    assert "profile->music_filter_binding_ready" in override
    assert "profile->music_filter_ready" in override

    acquire = ADAPTER.split(
        "int dluma_player_media_adapter_music_acquire", 1
    )[1].split(
        "int dluma_player_media_adapter_music_state", 1
    )[0]
    stock_acquire = acquire.split(
        "if (!profile->music_stop || !profile->music_start", 1
    )[1]
    optional_snapshot = stock_acquire.index(
        "DLUMA_PLAYER_MEDIA_CAP_MUSIC_FILTER"
    )
    ownership = stock_acquire.index("profile->music_owned = 1U;")
    stop = stock_acquire.index("profile->music_stop(manager);")
    assert optional_snapshot < ownership < stop
    assert (
        "music_filter_snapshot_unavailable_handoff_continues"
        in stock_acquire
    )
    assert "return DLUMA_NATIVE_RESULT_UNAVAILABLE;" not in stock_acquire[
        optional_snapshot:ownership
    ]
    assert "profile->frontend_music_mute(" in acquire

    maintain = ADAPTER.split(
        "int dluma_player_media_adapter_music_maintain", 1
    )[1].split(
        "int dluma_player_media_adapter_music_filter", 1
    )[0]
    assert "profile->main_menu_music_stop(main_menu_module);" in maintain
    assert "profile->frontend_music_mute(manager, 0.0f);" in maintain
    assert "profile->music_stop(manager);" in maintain
    assert "profile->main_menu_music_start" not in maintain
    assert "profile->frontend_music_unmute" not in maintain
    assert "profile->music_start" not in maintain
    assert (
        "restarted_main_menu_music_stopped_without_custom_wave_restart"
        in maintain
    )
    assert "DLUMA_DL_FRONTEND_MUSICS_MANAGER_MUTED_OFFSET" in acquire


def test_player_media_uses_exact_windows_active_level_music_owner():
    resolver = ADAPTER.split(
        "static int dluma_player_media_resolve_music_manager_detailed", 1
    )[1].split(
        "static int dluma_player_media_validate_music_manager(", 1
    )[0]
    assert "profile->rt_dynamic_cast(" in resolver
    assert "DLUMA_DL_ILEVEL_TYPE_RVA" in resolver
    assert "DLUMA_DL_LEVELDI_TYPE_RVA" in resolver
    assert (
        "*(void**)observed_active_level ==\n"
        "                (BYTE*)profile->gamedll +"
        in resolver
    )
    assert resolver.index("*(void**)observed_active_level ==") < resolver.index(
        "profile->rt_dynamic_cast("
    )
    assert (
        "if (observed_active_level &&\n"
        "        !dluma_is_readable(observed_active_level"
        in resolver
    )
    assert "DLUMA_DL_LEVELDI_MUSIC_MANAGER_OFFSET" in resolver
    assert "dluma_player_media_validate_live_rtti_object(" in resolver
    assert "DLUMA_DL_MUSIC_MANAGER_SINGLETON_RVA" in resolver
    assert "ACTIVE_LEVEL_STALE_SINGLETON_IGNORED" in resolver
    assert "singleton_manager == manager" in resolver
    assert "dluma_player_media_resolve_game_session_music_manager(" in resolver
    assert "dluma_player_media_resolve_owned_singleton_music_manager(" in resolver
    assert "dluma_player_media_resolve_published_frontend_music_manager(" not in resolver
    session_route = ADAPTER.split(
        "static int dluma_player_media_resolve_game_session_music_manager", 1
    )[1].split(
        "static int dluma_player_media_resolve_owned_singleton_music_manager", 1
    )[0]
    for evidence in (
        "DLUMA_DL_GAMEDI_FRAME_SUBOBJECT_OFFSET",
        "DLUMA_DL_GAMEDI_GLOBAL_RVA",
        "DLUMA_DL_GAMEDI_SESSION_OFFSET",
        "DLUMA_DL_GAMEDI_PRIMARY_VTABLE_RVA",
        "DLUMA_DL_GAMEDI_FRAME_VTABLE_RVA",
        "dluma_player_media_validate_live_session_object(",
        "session_class_out",
        "game_primary = (BYTE*)*game_global_slot",
        "profile->get_session_level(session)",
        "dluma_player_media_leveldi_igsobject(",
        "profile->get_object_level(level_igsobject)",
        "profile->get_object_level(manager)",
        "manager_owner != level_owner",
        "level_owner != observed_active_level",
            "DLUMA_DL_MUSIC_SOURCE_GAME_GLOBAL_SESSION_MANAGER_OWNER_CORROBORATED",
            "DLUMA_DL_LIVE_SESSION_LEVEL_OWNER_QUERY_FAILED",
            "DLUMA_DL_LIVE_MANAGER_OWNER_QUERY_FAILED",
            "DLUMA_DL_LIVE_MANAGER_OWNER_MISMATCH",
            "DLUMA_DL_LIVE_ACTIVE_LEVEL_SESSION_OWNER_MISMATCH",
    ):
        assert evidence in session_route
    assert "!profile || !observed_active_level" not in session_route
    assert "*owner_level_out = level_owner;" in session_route
    assert "frame_game_primary" in session_route
    assert "*(void**)session !=" not in session_route
    assert "profile->get_object_level(level_di)" not in session_route
    assert (
        "\n    game_primary = (BYTE*)profile->game_subobject -" not in
        session_route
    )
    fallback = ADAPTER.split(
        "static int dluma_player_media_resolve_owned_singleton_music_manager", 1
    )[1].split(
        "static int dluma_player_media_resolve_music_manager_detailed", 1
    )[0]
    assert "profile->get_object_level(manager)" in fallback
    assert "owner_level != level_interface" in fallback
    assert "DLUMA_DL_MUSIC_SOURCE_SINGLETON_OWNER_LEVEL_CORROBORATED" in fallback
    assert "DLUMA_DL_LIVE_MANAGER_OWNER_MISMATCH" in fallback

    revalidation = ADAPTER.split(
        "static int dluma_player_media_validate_music_manager(", 1
    )[1].split(
        "static const char* dluma_player_media_live_reason_name", 1
    )[0]
    assert "dluma_player_media_resolve_music_manager_detailed(" in revalidation
    assert "profile->observed_active_level" in revalidation
    assert "owner_level != profile->live_owner_level" in revalidation
    assert "level_di != profile->live_level_di" in revalidation
    assert "manager != profile->live_music_manager" in revalidation
    assert "session_class != profile->live_session_class" in revalidation

    live_probe = ADAPTER.split(
        "static void dluma_player_media_probe_live_readiness", 1
    )[1].split(
        "static int dluma_player_media_bind_wave_bank_detailed", 1
    )[0]
    assert "profile->live_owner_level != owner_level" in live_probe
    assert "profile->live_level_di != level_di" in live_probe
    assert "profile->live_session_class != session_class" in live_probe
    assert "profile->live_owner_level = owner_level" in live_probe
    assert "profile->live_session_class = session_class" in live_probe
    assert "profile->observed_active_level !=" not in live_probe
    tick = ADAPTER.split(
        "void dluma_player_media_adapter_tick", 1
    )[1].split(
        "unsigned int dluma_player_media_adapter_capabilities", 1
    )[0]
    assert "profile->observed_active_level = level_interface;" in tick

    published = ADAPTER.split(
        "int dluma_player_media_adapter_publish_frontend_music_manager", 1
    )[1].split(
        "int dluma_player_media_adapter_clear_frontend_music_manager", 1
    )[0]
    assert (
        "dluma_player_media_validate_frontend_musics_manager_object_detailed("
        in published
    )
    assert "profile->get_object_level(music_manager)" in published
    assert "manager_owner != owner_level" in published
    assert "dluma_player_media_probe_live_readiness(" not in published
    cleared = ADAPTER.split(
        "int dluma_player_media_adapter_clear_frontend_music_manager", 1
    )[1].split(
        "int dluma_player_media_adapter_get_session_level", 1
    )[0]
    assert "profile->music_owned || profile->music_snapshot.valid" in cleared
    assert "profile->waves[index].in_use" in cleared
    assert "dluma_player_media_resolve_published_frontend_music_manager(" in cleared

    wave_owner = ADAPTER.split(
        "static int dluma_player_media_validate_wave_owner", 1
    )[1].split(
        "static const char* dluma_player_media_live_reason_name", 1
    )[0]
    assert "dluma_player_media_resolve_published_frontend_music_manager(" in wave_owner
    assert "dluma_player_media_validate_music_manager(profile, owner_out)" in wave_owner

    frontend_binding = ADAPTER.split(
        "static int dluma_player_media_bind_frontend_musics_manager", 1
    )[1].split(
        "static int dluma_player_media_bind_music_manager_detailed", 1
    )[0]
    for evidence in (
        "DLUMA_DL_FRONTEND_MUSICS_MANAGER_MUTE_RVA",
        "DLUMA_DL_FRONTEND_MUSICS_MANAGER_UNMUTE_RVA",
        "DLUMA_DL_FRONTEND_MUSICS_MANAGER_VTABLE_RVA",
        "DLUMA_DL_FRONTEND_MUSICS_MANAGER_COL_RVA",
        '".?AVManager@Musics@@"',
    ):
        assert evidence in frontend_binding

    binding = ADAPTER.split(
        "static int dluma_player_media_bind_music_manager_detailed(", 1
    )[1].split(
        "static int dluma_player_media_bind_local_buggy", 1
    )[0]
    for evidence in (
        "g_dluma_dl_leveldi_music_create_evidence_prefix",
        "g_dluma_dl_leveldi_music_store_prefix",
        "DLUMA_DL_LEVELDI_MUSIC_RTTI_LEA_RVA",
        "DLUMA_DL_MUSIC_MANAGER_CREATE_TYPE_RVA",
        "DLUMA_DL_LEVELDI_COL_RVA",
        "DLUMA_DL_LEVELDI_VTABLE_RVA",
        "DLUMA_DL_LEVELDI_IGSOBJECT_OFFSET",
        "DLUMA_DL_LEVELDI_IGSOBJECT_COL_RVA",
        "DLUMA_DL_LEVELDI_IGSOBJECT_VTABLE_RVA",
        "DLUMA_DL_LEVELDI_IGSOBJECT_BASE_DESCRIPTOR_RVA",
        "DLUMA_DL_MUSIC_MANAGER_TYPE_RVA",
        "DLUMA_DL_MUSIC_MANAGER_COL_RVA",
        "DLUMA_DL_MUSIC_MANAGER_IGSOBJECT_BASE_DESCRIPTOR_RVA",
        "dluma_player_media_validate_igsobject_route_metadata(",
        "dluma_player_media_validate_rtti_vtable(",
        '".?AVILevel@@"',
        '".?AVLevelDI@@"',
        '".?AVMusicManager@@"',
        "g_dluma_dl_get_object_level_export",
        "g_dluma_dl_get_object_level_prefix",
        "DLUMA_DL_IGSOBJECT_GET_LEVEL_RVA",
        "DLUMA_DL_MUSIC_BIND_OWNER_LEVEL_EXPORT_MISMATCH",
        "DLUMA_DL_GAMEDI_PRIMARY_COL_RVA",
        "DLUMA_DL_GAMEDI_FRAME_COL_RVA",
        "session_di",
        "session_cooperative",
        "session_cooperative_di",
        "dluma_player_media_validate_session_profile_metadata(",
        "DLUMA_DL_SESSION_LEVEL_SLOT",
        "DLUMA_DL_SESSION_LEVEL_RVA",
        "g_dluma_dl_session_level_prefix",
        "DLUMA_DL_DRIVING_CONDITION_RVA",
        "g_dluma_dl_driving_condition_prefix",
        "DLUMA_DL_GAMEDI_GLOBAL_RVA",
        "DLUMA_DL_MUSIC_BIND_GAMEDI_GLOBAL_PREFIX_MISMATCH",
        "DLUMA_DL_MUSIC_BIND_GAMEDI_GLOBAL_TARGET_MISMATCH",
        "DLUMA_DL_MUSIC_BIND_GAMEDI_GLOBAL_SLOT_OUT_OF_RANGE",
        '".?AVGameDI@@"',
    ):
        assert evidence in binding

    frame = SERVICES.split("void dluma_media_service_tick", 1)[1].split(
        "static void dluma_media_set_capability", 1
    )[0]
    assert "dluma_player_media_adapter_get_session_level(&level)" in frame
    assert "dluma_player_media_adapter_tick(" in frame


def test_player_media_wave_binding_retries_with_clause_diagnostics():
    binding = ADAPTER.split(
        "static int dluma_player_media_bind_wave_bank_detailed(", 2
    )[1].split(
        "static int dluma_player_media_bind_wave_exports", 1
    )[0]
    for reason in (
        "DLUMA_DL_WAVE_BIND_PREFIX_MISMATCH",
        "DLUMA_DL_WAVE_BIND_CALL_TARGET_MISMATCH",
        "DLUMA_DL_WAVE_BIND_VTABLE_SLOT_MISMATCH",
        "DLUMA_DL_WAVE_BIND_READY_ORIGINAL_ENTRY",
        "DLUMA_DL_WAVE_BIND_READY_LOADER_OWNED_DETOUR",
    ):
        assert reason in binding
    probe = ADAPTER.split(
        "static void dluma_player_media_probe_live_readiness", 1
    )[1].split(
        "static int dluma_player_media_bind_wave_bank_detailed", 1
    )[0]
    assert "wave_ready = dluma_player_media_bind_wave_bank_detailed(" in probe
    assert "profile->wave_binding_reason != wave_reason" in probe
    assert "profile->wave_bank_ready = wave_ready ? 1U : 0U;" in probe
    assert (
        "dluma_custom_map_asset_isolation_snapshot_audio_fmod_load_banks_detour("
        in binding
    )
    assert "dluma_internal_classify_wave_bank_entry(" in binding
    assert "loader_owned_detour" in ADAPTER
    for invariant in (
        "owner_route_count != 1U",
        "owned_route_count != 1U",
        "!evidence->owner_validated",
        "!evidence->owner_installed",
        "!evidence->route_installed",
        "!evidence->current_routing_bytes_match",
        "!evidence->untouched_tail_matches",
        "evidence->bound_original != evidence->route_relay",
    ):
        assert invariant in HOOK_ATTESTATION
    for ownership_evidence in (
        "route->owner != owner",
        "route->hook_target",
        "route->relay",
        "RouteUntouchedTailMatches(route)",
        "BytesEqual(",
        "g_original_audio_fmod_load_banks",
    ):
        assert ownership_evidence in ASSET_ISOLATION


def test_player_media_music_binding_reports_exact_static_clause():
    binding = ADAPTER.split(
        "static int dluma_player_media_bind_music_manager_detailed(", 1
    )[1].split(
        "static int dluma_player_media_bind_local_buggy", 1
    )[0]
    for reason in (
        "DLUMA_DL_MUSIC_BIND_CTOR_PREFIX_MISMATCH",
        "DLUMA_DL_MUSIC_BIND_STOP_PREFIX_MISMATCH",
        "DLUMA_DL_MUSIC_BIND_START_PREFIX_MISMATCH",
        "DLUMA_DL_MUSIC_BIND_DELAYED_START_PREFIX_MISMATCH",
        "DLUMA_DL_MUSIC_BIND_RTDYNAMICCAST_PREFIX_MISMATCH",
        "DLUMA_DL_MUSIC_BIND_ILEVEL_TYPE_NAME_MISMATCH",
        "DLUMA_DL_MUSIC_BIND_LEVELDI_TYPE_NAME_MISMATCH",
        "DLUMA_DL_MUSIC_BIND_LEVELDI_PRIMARY_RTTI_MISMATCH",
        "DLUMA_DL_MUSIC_BIND_CREATE_PREFIX_MISMATCH",
        "DLUMA_DL_MUSIC_BIND_CREATE_TYPE_TARGET_MISMATCH",
        "DLUMA_DL_MUSIC_BIND_STORE_PREFIX_MISMATCH",
        "DLUMA_DL_MUSIC_BIND_OWNER_LEVEL_EXPORT_MISMATCH",
        "DLUMA_DL_MUSIC_BIND_GAMEDI_PRIMARY_RTTI_MISMATCH",
        "DLUMA_DL_MUSIC_BIND_GAMEDI_FRAME_RTTI_MISMATCH",
        "DLUMA_DL_MUSIC_BIND_SESSIONDI_RTTI_MISMATCH",
        "DLUMA_DL_MUSIC_BIND_SESSION_COOPERATIVE_RTTI_MISMATCH",
        "DLUMA_DL_MUSIC_BIND_SESSION_COOPERATIVE_DI_RTTI_MISMATCH",
        "DLUMA_DL_MUSIC_BIND_SESSION_LEVEL_PREFIX_MISMATCH",
        "DLUMA_DL_MUSIC_BIND_SESSION_LEVEL_SLOT_MISMATCH",
        "DLUMA_DL_MUSIC_BIND_SESSION_COOPERATIVE_LEVEL_SLOT_MISMATCH",
        "DLUMA_DL_MUSIC_BIND_SESSION_COOPERATIVE_DI_LEVEL_SLOT_MISMATCH",
        "DLUMA_DL_MUSIC_BIND_LEVELDI_IGSOBJECT_RTTI_MISMATCH",
        "DLUMA_DL_MUSIC_BIND_MANAGER_IGSOBJECT_RTTI_MISMATCH",
        "DLUMA_DL_MUSIC_BIND_GAMEDI_GLOBAL_PREFIX_MISMATCH",
        "DLUMA_DL_MUSIC_BIND_GAMEDI_GLOBAL_TARGET_MISMATCH",
        "DLUMA_DL_MUSIC_BIND_GAMEDI_GLOBAL_SLOT_OUT_OF_RANGE",
        "DLUMA_DL_MUSIC_BIND_READY",
    ):
        assert reason in binding
    assert "dluma_player_media_music_binding_reason_name(" in ADAPTER


def test_player_media_audio_capabilities_are_exact_profile_gated():
    for token in (
        "DLUMA_DL_LOAD_WAVE_BANK_RVA 0x000FA3B0UL",
        "DLUMA_DL_LOAD_WAVE_BANK_CALL_RVA 0x000FA44DUL",
        "DLUMA_DL_SOUND_BANK_LOAD_RVA 0x00100920UL",
        "DLUMA_DL_PLAY_WAVE_2D_RVA 0x0027C890UL",
        "DLUMA_DL_STOP_WAVE_RVA 0x0027D7C0UL",
        "DLUMA_DL_IS_WAVE_PLAYING_RVA 0x0027DA30UL",
        "DLUMA_DL_SET_WAVE_POSITION_SECONDS_RVA 0x0027DAD0UL",
        "DLUMA_DL_SET_WAVE_VOLUME_RVA 0x0027DE10UL",
        "DLUMA_DL_GET_WAVE_TIME_SECONDS_RVA 0x002B1040UL",
        "DLUMA_DL_GET_WAVE_DURATION_NAME_RVA 0x0027E740UL",
        "DLUMA_DL_IGSOBJECT_GET_LEVEL_RVA 0x00277A00UL",
        "DLUMA_DL_GAMEDI_TYPE_RVA 0x01CB60C8UL",
        "DLUMA_DL_GAMEDI_PRIMARY_COL_RVA 0x01A530D8UL",
        "DLUMA_DL_GAMEDI_PRIMARY_VTABLE_RVA 0x0157D5F8UL",
        "DLUMA_DL_GAMEDI_FRAME_COL_RVA 0x01A530B0UL",
        "DLUMA_DL_GAMEDI_FRAME_VTABLE_RVA 0x0157E2E8UL",
        "DLUMA_DL_GAMEDI_FRAME_SUBOBJECT_OFFSET 0x0010U",
        "DLUMA_DL_GAMEDI_GLOBAL_RVA 0x01D07F88UL",
        "DLUMA_DL_GAMEDI_SESSION_OFFSET 0x0508U",
        "DLUMA_DL_DRIVING_CONDITION_RVA 0x0037F4F0UL",
        "DLUMA_DL_SESSIONDI_TYPE_RVA 0x01CBCBB8UL",
        "DLUMA_DL_SESSIONDI_COL_RVA 0x01A57B08UL",
        "DLUMA_DL_SESSIONDI_VTABLE_RVA 0x01591FC8UL",
        "DLUMA_DL_SESSIONDI_HIERARCHY_RVA 0x01A573A0UL",
        "DLUMA_DL_SESSIONDI_BASE_ARRAY_RVA 0x01A573C0UL",
        "DLUMA_DL_SESSIONDI_BASE_DESCRIPTOR_RVA 0x01A57378UL",
        "DLUMA_DL_SESSION_COOPERATIVE_TYPE_RVA 0x01CBCBD8UL",
        "DLUMA_DL_SESSION_COOPERATIVE_COL_RVA 0x01A572E0UL",
        "DLUMA_DL_SESSION_COOPERATIVE_VTABLE_RVA 0x0158E488UL",
        "DLUMA_DL_SESSION_COOPERATIVE_HIERARCHY_RVA 0x01A57308UL",
        "DLUMA_DL_SESSION_COOPERATIVE_BASE_ARRAY_RVA 0x01A57320UL",
        "DLUMA_DL_SESSION_COOPERATIVE_BASE_DESCRIPTOR_RVA 0x01A57498UL",
        "DLUMA_DL_SESSION_COOPERATIVE_DI_TYPE_RVA 0x01CBCC50UL",
        "DLUMA_DL_SESSION_COOPERATIVE_DI_COL_RVA 0x01A575B8UL",
        "DLUMA_DL_SESSION_COOPERATIVE_DI_VTABLE_RVA 0x0158F838UL",
        "DLUMA_DL_SESSION_COOPERATIVE_DI_HIERARCHY_RVA 0x01A575E0UL",
        "DLUMA_DL_SESSION_COOPERATIVE_DI_BASE_ARRAY_RVA 0x01A57600UL",
        "DLUMA_DL_SESSION_COOPERATIVE_DI_BASE_DESCRIPTOR_RVA 0x01A57660UL",
        "DLUMA_DL_SESSION_LEVEL_SLOT 0x07A0U",
        "DLUMA_DL_SESSION_LEVEL_RVA 0x006AD140UL",
        "DLUMA_DL_MUSIC_MANAGER_STOP_RVA 0x00979BE0UL",
        "DLUMA_DL_MUSIC_MANAGER_START_RVA 0x00979C60UL",
        "DLUMA_DL_MUSIC_MANAGER_DELAYED_START_RVA 0x00979CC0UL",
        "DLUMA_DL_ILEVEL_TYPE_RVA 0x01CBB4C0UL",
        "DLUMA_DL_LEVELDI_TYPE_RVA 0x01CA2F30UL",
        "DLUMA_DL_LEVELDI_COL_RVA 0x01A54368UL",
        "DLUMA_DL_LEVELDI_VTABLE_RVA 0x01581C98UL",
        "DLUMA_DL_LEVELDI_HIERARCHY_RVA 0x01A54390UL",
        "DLUMA_DL_LEVELDI_BASE_ARRAY_RVA 0x01A543B0UL",
        "DLUMA_DL_LEVELDI_IGSOBJECT_OFFSET 0x0010U",
        "DLUMA_DL_LEVELDI_IGSOBJECT_COL_RVA 0x01A54340UL",
        "DLUMA_DL_LEVELDI_IGSOBJECT_VTABLE_RVA 0x015822E8UL",
        "DLUMA_DL_LEVELDI_IGSOBJECT_BASE_DESCRIPTOR_RVA 0x01A05128UL",
        "DLUMA_DL_IGSOBJECT_TYPE_RVA 0x01C952A8UL",
        "DLUMA_DL_IGSOBJECT_HIERARCHY_RVA 0x01A05270UL",
        "DLUMA_DL_IGSOBJECT_BASE_ARRAY_RVA 0x01A05288UL",
        "DLUMA_DL_MUSIC_MANAGER_TYPE_RVA 0x01CD2B48UL",
        "DLUMA_DL_MUSIC_MANAGER_COL_RVA 0x01AA7060UL",
        "DLUMA_DL_MUSIC_MANAGER_HIERARCHY_RVA 0x01AA7088UL",
        "DLUMA_DL_MUSIC_MANAGER_BASE_ARRAY_RVA 0x01AA70A0UL",
        "DLUMA_DL_MUSIC_MANAGER_IGSOBJECT_BASE_DESCRIPTOR_RVA 0x01A05248UL",
        "DLUMA_DL_MUSIC_MANAGER_CREATE_TYPE_RVA 0x01D6CC50UL",
        "DLUMA_DL_LEVELDI_MUSIC_MANAGER_OFFSET 0x09D0U",
        "DLUMA_DL_LEVELDI_MUSIC_CREATE_EVIDENCE_RVA 0x003E925AUL",
        "DLUMA_DL_LEVELDI_MUSIC_RTTI_LEA_RVA 0x003E9267UL",
        "DLUMA_DL_LEVELDI_MUSIC_STORE_RVA 0x003E927AUL",
    ):
        assert token in ADAPTER
    assert "0x01CBA6C0UL" not in ADAPTER
    assert "0x01CA2130UL" not in ADAPTER
    for symbol in (
        "?PlayWave2D@IGSObject@@QEBAHAEBV?$string_base@D@ttl@@",
        "?StopWave@IGSObject@@QEAAXHM@Z",
        "?IsWavePlaying@IGSObject@@QEAA_NH@Z",
        "?SetPlayingWavePos@IGSObject@@QEAA_NHM@Z",
        "?SetWaveVolume@IGSObject@@QEAA_NHM@Z",
        "?GetPlayingWaveTimePos@IGSObject@@QEAAMH@Z",
        "?GetWaveDuration@IGSObject@@QEBAMAEBV?$string_base@D@ttl@@@Z",
        "?GetLevel@IGSObject@@QEBAPEAVILevel@@XZ",
    ):
        assert symbol in ADAPTER
    assert ADAPTER.count("SetPlayingWavePos@IGSObject") == 1
    assert "sizeof(DlumaPlayerMediaTtlStringView) == 16U" in ADAPTER
    for prefix in (
        "g_dluma_dl_load_wave_bank_prefix",
        "g_dluma_dl_play_wave_2d_prefix",
        "g_dluma_dl_stop_wave_prefix",
        "g_dluma_dl_is_wave_playing_prefix",
        "g_dluma_dl_set_wave_position_seconds_prefix",
        "g_dluma_dl_set_wave_volume_prefix",
        "g_dluma_dl_get_wave_time_seconds_prefix",
        "g_dluma_dl_get_wave_duration_name_prefix",
        "g_dluma_dl_get_object_level_prefix",
        "g_dluma_dl_session_level_prefix",
        "g_dluma_dl_music_manager_stop_prefix",
        "g_dluma_dl_music_manager_start_prefix",
        "g_dluma_dl_music_manager_delayed_start_prefix",
    ):
        assert prefix in ADAPTER
    capabilities = ADAPTER.split(
        "unsigned int dluma_player_media_adapter_capabilities", 1
    )[1].split(
        "int dluma_player_media_adapter_get_vehicle_state", 1
    )[0]
    for capability in (
        "DLUMA_PLAYER_MEDIA_CAP_WAVE_BANK",
        "DLUMA_PLAYER_MEDIA_CAP_CUE_PLAYBACK",
        "DLUMA_PLAYER_MEDIA_CAP_MUSIC_EXCLUSIVE",
    ):
        assert f"capabilities |= {capability};" in capabilities
    assert "DLUMA_PLAYER_MEDIA_CAP_MUSIC_FILTER" in capabilities
    assert "profile->wave_bank_ready &&" in ADAPTER
    assert "profile->wave_exports_ready &&" in ADAPTER
    assert "profile->music_manager_ready" in ADAPTER
    handoff = capabilities.split(
        "capabilities |= DLUMA_PLAYER_MEDIA_CAP_MUSIC_EXCLUSIVE;", 1
    )[0]
    assert "profile->music_filter_ready" not in handoff
    assert "profile->music_filter_ready" in capabilities
    assert "profile->load_wave_bank(" in ADAPTER
    assert "audio, engine_bank_name, 0U" in ADAPTER
    assert "audio, DLUMA_COMPANION_ARCHIVE_BANK_ID, 0U" not in ADAPTER
    open_bank = ADAPTER.split(
        "int dluma_player_media_adapter_open_bank", 1
    )[1].split(
        "int dluma_player_media_adapter_probe_duration", 1
    )[0]
    assert open_bank.index(
        "dluma_companion_archive_prepare_bank("
    ) < open_bank.index("profile->load_wave_bank(")
    assert "load_result != 1" in ADAPTER
    assert "load_delivered_and_cues_validated" in ADAPTER
    assert "already_loaded_revalidated" not in ADAPTER
    assert "player_media_bank_probe_cue" in ADAPTER
    assert "profile->bank_diagnostic_reason == reason" in ADAPTER
    assert ".spb" not in ADAPTER.lower()

    acquire = ADAPTER.split(
        "int dluma_player_media_adapter_music_acquire", 1
    )[1].split("int dluma_player_media_adapter_music_state", 1)[0]
    stock_acquire = acquire.split(
        "if (!profile->music_stop || !profile->music_start", 1
    )[1]
    provisional = stock_acquire.index("profile->music_owned = 1U;")
    stop = stock_acquire.index("profile->music_stop(manager);")
    recovery = stock_acquire.index("profile->music_start(manager, 0.0f);")
    assert provisional < stop < recovery
    assert "DLUMA_DL_MUSIC_MANAGER_PAUSED" in stock_acquire
    assert "DLUMA_DL_MUSIC_MANAGER_WANTED_TYPE_OFFSET" in stock_acquire
    assert "ownership_retained" in stock_acquire
    assert "dluma_player_media_music_snapshot_take(" in stock_acquire
    assert "dluma_player_media_music_snapshot_restore(" in stock_acquire

def test_lifecycle_order_keeps_one_original_script_owner():
    player_init = EXPORTS.split(
        "static void* dluma_player_initialize(void)", 1
    )[1].split("static __int64 dluma_player_shutdown(void)", 1)[0]
    pre = player_init.index("dluma_native_mods_pre_game_init")
    original = player_init.index("manager = g_original_initialize()")
    post = player_init.index("dluma_native_mods_post_game_init")
    lua = player_init.index('"lua_start_after_native_chain"')
    assert pre < original < post < lua
    assert "g_original_initialize_count" in EXPORTS
    player_shutdown = EXPORTS.split(
        "static __int64 dluma_player_shutdown(void)", 1
    )[1].split('extern "C" __declspec(dllexport) void*', 1)[0]
    shutdown_native = player_shutdown.index("dluma_native_mods_shutdown_chain")
    shutdown_original = player_shutdown.index("g_original_shutdown()")
    assert shutdown_native < shutdown_original
    assert "native_mod_retained_until_process_exit" in NATIVE


def test_mods_ui_keeps_typed_rows_and_no_lua_safety_claim_for_dlls():
    for token in (
        "DLUMA_REGISTRY_CATEGORY_ALL",
        "DLUMA_REGISTRY_CATEGORY_CODE",
        "DLUMA_REGISTRY_CATEGORY_LUA",
        "dluma_ui_registry_add_option_row",
        "DLUMA_REGISTRY_ROW_KIND_NATIVE",
        "DLUMA_Common_RestartRequired",
    ):
        assert token in UI
    details = UI.split("static void dluma_ui_mod_details_add_overview", 1)[1].split(
        "static void dluma_ui_mod_details_update_for_row", 1
    )[0]
    assert "FULL PROCESS ACCESS - NOT SANDBOXED" in details
    assert "FINAL FILE PATH" in details
    assert "CONTENT / MODULE SHA-256" in details


def test_native_mod_toggle_is_disabled_for_incompatible_entries_and_reviews_v2():
    update_start = UI.index("static void dluma_ui_registry_update_button_labels(void)\n{")
    update_end = UI.index("static int dluma_ui_registry_validate_list", update_start)
    update = UI[update_start:update_end]
    assert "?SetDisable@IUIElementControl@@UEAAX_N@Z" in UI
    assert "static void dluma_ui_set_element_disabled" in UI
    assert "g_dluma_set_element_disable(element, disabled ? 1 : 0)" in UI
    assert "int toggle_disabled = 0;" in update
    assert "!dluma_ui_native_host_abi_compatible(native)" in update
    assert '"REVIEW"' in update
    assert '"ENABLE NEXT LAUNCH"' not in update
    assert '"DISABLE NEXT LAUNCH"' not in update
    assert "dluma_ui_registry_selected_status(" in update
    assert "dluma_ui_set_element_disabled(g_dluma_ui_registry_toggle_button," in update
    assert "native_mod_toggle_blocked_incompatible" in UI
    assert "native->description" in update
    assert "native->runtime_error[0]" in UI


def test_manifest_native_persistence_uses_final_v2_identity_and_reports_rejections():
    manifest = NATIVE.split(
        "static void dluma_native_add_manifest_candidate(", 1
    )[1].split(
        "void dluma_native_mods_set_original_gamedll", 1
    )[0]
    fingerprint = manifest.rindex("dluma_native_fingerprint(item);")
    persistence = manifest.rindex("dluma_native_apply_persistence(item);")
    assert fingerprint < persistence

    request = NATIVE.split(
        "int dluma_native_mods_request_hosts(", 1
    )[1].split(
        "int dluma_native_mods_disable_exact_next_launch", 1
    )[0]
    for clause in (
        "invalid_request",
        "stale_registry_revision",
        "registry_key_not_found",
        "approval_fingerprint_changed",
        "duplicate_mod_id",
        "incompatible_native_host_abi",
        "companion_pack_invalid",
        "exact_player_approval_required",
        "separate_editor_full_trust_approval_required",
        "host_not_declared_by_manifest",
        "native_mods_ini_write_failed",
    ):
        assert f'"{clause}"' in request
    assert '"native_mod_approval_rejected"' in request


def test_native_mod_list_rows_use_native_fixed_statuses_while_details_keep_diagnostics():
    fixed_status = UI.split("static void dluma_ui_registry_fixed_status", 1)[1].split(
        "static void dluma_ui_mod_config_diagnostic_text", 1
    )[0]
    rows = UI.split("static void *dluma_ui_registry_add_option_row", 1)[1].split(
        "static void dluma_ui_registry_snapshot", 1
    )[0]
    details = UI.split("static void dluma_ui_mod_details_add_state", 1)[1].split(
        "static void dluma_ui_mod_details_update_for_row", 1
    )[0]
    assert '"DLUMA_Mods_State_Conflict"' in fixed_status
    assert '"DLUMA_Mods_State_Incompatible"' in fixed_status
    assert 'fallback = "INCOMPATIBLE"' in fixed_status
    assert "dluma_ui_settings_add_native_option(item, value)" in rows
    assert "dluma_native_mods_state_text(item)" in details
    assert '"LAST ERROR"' in details


def test_fake_v2_modules_execute_only_in_the_offline_harness():
    subprocess.run(
        [
            "cmd.exe",
            "/d",
            "/s",
            "/c",
            "call tools\\build_native_mod_fake_tests.bat Release",
        ],
        cwd=ROOT,
        check=True,
        capture_output=True,
        text=True,
        timeout=60,
    )
    completed = subprocess.run(
        [
            str(
                ROOT
                / "build"
                / "native-mod-fake"
                / "Release"
                / "native_mod_chain_fake_harness.exe"
            ),
            str(ROOT / "build" / "native-mod-fake" / "Release"),
        ],
        cwd=ROOT,
        check=True,
        capture_output=True,
        text=True,
        timeout=30,
    )
    assert completed.stdout.count("[PASS]") == 4
    assert "native DLL MOD fake-chain cases passed: 4" in completed.stdout


def test_fixture_set_contains_v2_legacy_v1_and_failure_cases():
    fixtures = ROOT / "tests" / "native_mod_fixtures"
    for name in (
        "fixture_v2_a.c",
        "fixture_v2_b.c",
        "fixture_v2_fail.c",
        "fixture_legacy_v1.c",
        "fixture_legacy.c",
    ):
        assert (fixtures / name).is_file()
    assert "fixture_v2_fail.mod" in (
        ROOT / "tests" / "native_mod_chain_fake_harness.c"
    ).read_text(encoding="utf-8")
    assert "fixture_legacy.mod" in (
        ROOT / "tests" / "native_mod_chain_fake_harness.c"
    ).read_text(encoding="utf-8")
    assert "fake_is_amd64_dll" in (
        ROOT / "tests" / "native_mod_chain_fake_harness.c"
    ).read_text(encoding="utf-8")
