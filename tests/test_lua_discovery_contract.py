from pathlib import Path
import importlib.util
import sys

ROOT = Path(__file__).resolve().parents[1]


def load_validator():
    path = ROOT / "tools" / "validate_lua_metadata.py"
    spec = importlib.util.spec_from_file_location("validate_lua_metadata", path)
    module = importlib.util.module_from_spec(spec)
    assert spec and spec.loader
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


def test_literal_dlua_mod_is_the_canonical_metadata_source(tmp_path):
    validator = load_validator()
    path = tmp_path / "data" / "scripts" / "lua" / "project" / "manifest.lua"
    path.parent.mkdir(parents=True)
    path.write_text(
        '''local mod = dlua.mod {
    id = "example.single_manifest",
    name = "Single Manifest",
    version = "1.2.3",
    scope = "project",
    enabled_by_default = false,
    permissions = { "ui.debug", "world.query" },
}
return mod
''',
        encoding="utf-8",
    )
    result = validator.parse_metadata(path)
    assert result.valid, result.errors
    assert result.id == "example.single_manifest"
    assert result.enabled_by_default is False
    assert result.permissions == ["ui.debug", "world.query"]
    assert not result.warnings


def test_dynamic_manifest_permissions_fail_closed(tmp_path):
    validator = load_validator()
    path = tmp_path / "data" / "scripts" / "lua" / "project" / "dynamic.lua"
    path.parent.mkdir(parents=True)
    path.write_text(
        '''local requested = { "ui.debug" }
local mod = dlua.mod {
    id = "example.dynamic_permissions",
    name = "Dynamic permissions",
    version = "1.0.0",
    scope = "project",
    permissions = requested,
}
return mod
''',
        encoding="utf-8",
    )
    result = validator.parse_metadata(path)
    assert not result.valid
    assert any("literal list" in error for error in result.errors)


def test_canonical_folders_can_supply_scope(tmp_path):
    validator = load_validator()
    cases = {
        tmp_path / "data" / "scripts" / "lua" / "global" / "a.lua": "global",
        tmp_path / "data" / "scripts" / "lua" / "project" / "b.lua": "project",
        tmp_path / "data" / "scripts" / "lua" / "maps" / "Map_01" / "c.lua": "map",
        tmp_path / "data" / "maps" / "Map_02" / "scripts" / "lua" / "d.lua": "map",
    }
    for index, (path, expected_scope) in enumerate(cases.items()):
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(f"-- dlua:id = example.case{index}\n", encoding="utf-8")
        result = validator.parse_metadata(path)
        assert result.valid, result.errors
        assert result.scope == expected_scope


def test_legacy_local_scope_is_project_alias(tmp_path):
    validator = load_validator()
    path = tmp_path / "data" / "scripts" / "legacy.lua"
    path.parent.mkdir(parents=True)
    path.write_text(
        "-- dlua:id = example.legacy\n-- dlua:scope = local\n",
        encoding="utf-8",
    )
    result = validator.parse_metadata(path)
    assert result.valid
    assert result.scope == "project"
    assert any("deprecated" in warning for warning in result.warnings)


def test_map_scope_cannot_escape_owning_map_folder(tmp_path):
    validator = load_validator()
    path = tmp_path / "data" / "scripts" / "lua" / "project" / "bad.lua"
    path.parent.mkdir(parents=True)
    path.write_text(
        "-- dlua:id = example.badmap\n-- dlua:scope = map\n",
        encoding="utf-8",
    )
    result = validator.parse_metadata(path)
    assert not result.valid
    assert any("conflicts" in error for error in result.errors)


def test_script_ids_reject_path_escape_devices_and_non_ascii(tmp_path):
    validator = load_validator()
    invalid_ids = ("..", "con.script", "LPT1.data", "trailing.", "mód.test")
    for index, script_id in enumerate(invalid_ids):
        path = tmp_path / "data" / "scripts" / "lua" / "global" / f"bad{index}.lua"
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(f"-- dlua:id = {script_id}\n", encoding="utf-8")
        result = validator.parse_metadata(path)
        assert not result.valid, script_id


def test_project_ownership_is_not_gated_on_script_presence():
    source = (ROOT / "src" / "dlw_workshop.c").read_text(encoding="utf-8")
    assert "dluma_candidate_has_scripts" not in source
    assert "Multiple workshop projects claim the active map name" in source
    assert "dluma_same_logical_workshop_project" not in source
    assert "duplicate_workshop_project_id_merged" not in source
    assert "dluma_directory_identity_equal" in source
    assert "ambiguous_map_resolved_by_exact_source" in source
    assert "project_owner_proof_pending" in source
    assert "project_owner_proof_rejected" in source


def test_duplicate_map_resolution_uses_exact_source_and_final_directory_identity():
    workshop = (ROOT / "src" / "dlw_workshop.c").read_text(encoding="utf-8")
    identity = (
        ROOT / "src" / "dlw_project_owner_identity.c"
    ).read_text(encoding="utf-8")
    adapter = (
        ROOT
        / "src"
        / "fixes"
        / "custom_map_asset_isolation"
        / "CustomMapAssetIsolationFix.cpp"
    ).read_text(encoding="utf-8")
    ledger = (
        ROOT
        / "src"
        / "fixes"
        / "custom_map_asset_isolation"
        / "CustomContentLedger.cpp"
    ).read_text(encoding="utf-8")
    assert "GetFinalPathNameByHandleW" in identity
    assert "GetFileInformationByHandle" in identity
    assert "dwVolumeSerialNumber" in identity
    assert "nFileIndexHigh" in identity
    assert "nFileIndexLow" in identity
    assert "dluma_validate_map_anchor_under_project" in workshop
    assert "dluma_custom_map_exact_project_owner_snapshot" in workshop
    assert "dluma_custom_map_exact_project_owner_consume" in workshop
    assert "dluma_custom_map_exact_project_owner_snapshot" in adapter
    assert "RecordExactMapOwnerEvidence" in adapter
    assert "ConsumeExactMapOwnerProof" in ledger
    assert "evidence->consumed = true" in ledger


def test_lua_scan_contract_includes_data_scripts():
    source = (ROOT / "src" / "dlw_lua_discovery.c").read_text(encoding="utf-8")
    assert 'L"data\\\\scripts\\\\lua\\\\global"' in source
    assert 'L"data\\\\scripts\\\\lua\\\\project"' in source
    assert 'L"data\\\\scripts\\\\lua\\\\maps"' in source
    assert 'L"scripts\\\\lua"' in source
    assert 'L"data\\\\scripts\\\\dluma"' in source
    assert 'L"data\\\\scripts"' in source
    assert 'L"*.lua"' in source


def test_active_project_owns_project_and_map_scans():
    host = (ROOT / "src" / "dlw_host.c").read_text(encoding="utf-8")
    discovery = (ROOT / "src" / "dlw_lua_discovery.c").read_text(encoding="utf-8")
    assert "dluma_find_project_for_map" in host
    assert "dluma_activate_map(" in host
    assert "same_map_exact_project_transition" in host
    assert "dluma_project_roots_same_identity" in host
    assert "active_project_owner_transition_generation" in host
    assert "dluma_lua_discover_locals(project_root, map_name)" in host
    assert "data\\\\scripts\\\\lua\\\\maps" in discovery
    assert "project_root, L\"data\\\\maps\"" in discovery


def test_global_registry_tracks_item_title_and_workshop_id():
    common = (ROOT / "src" / "dlw_common.h").read_text(encoding="utf-8")
    workshop = (ROOT / "src" / "dlw_workshop.c").read_text(encoding="utf-8")
    discovery = (ROOT / "src" / "dlw_lua_discovery.c").read_text(encoding="utf-8")
    assert "source_name[128]" in common
    assert "workshop_id[64]" in common
    assert 'dluma_read_desc_field(project_root, "title"' in workshop
    assert 'dluma_read_workshop_id(project_root' in workshop
    assert '" | workshop_id="' in discovery


def test_level_transition_logging_is_independent():
    source = (ROOT / "src" / "dlw_host.c").read_text(encoding="utf-8")
    assert '"observed_level_transition"' in source
    assert '"map_detected_but_no_project_owner_found"' in source


def test_null_level_samples_retain_the_confirmed_context():
    source = (ROOT / "src" / "dlw_host.c").read_text(encoding="utf-8")
    assert "Null is an unknown sample" in source
    assert '"transient_level_query_failure"' in source
    assert '"confirmed frontend level became active"' in source
    assert '"no active level; main menu or level shutdown detected"' not in source
    assert source.index("dluma_scripts_tick(&g_dluma.scripts, now)") < source.index("dluma_ui_read_level_snapshot")


def test_engine_uses_exported_level_accessors_directly():
    source = (ROOT / "src" / "dlw_engine.c").read_text(encoding="utf-8")
    assert "state->get_active_level(game_interface)" in source
    assert "state->get_level_name(level_interface)" in source
    assert "dluma_parse_level_accessors" not in source


def test_engine_tracks_the_exact_level_destructor_for_unload():
    source = (ROOT / "src" / "dlw_engine.c").read_text(encoding="utf-8")
    host = (ROOT / "src" / "dlw_host.c").read_text(encoding="utf-8")
    assert 'GetProcAddress(state->engine_module, "??1ILevel@@MEAA@XZ")' in source
    assert "memcmp(target + 7, tail" in source
    assert "level_interface == (void*)state->tracked_level_interface" in source
    assert '"tracked_level_destroyed = authoritative ILevel destructor signal received"' in host


def test_devtools_frame_pump_is_fingerprint_and_vtable_validated():
    source = (ROOT / "src" / "dlw_engine.c").read_text(encoding="utf-8")
    ui = (ROOT / "src" / "dlw_ui.c").read_text(encoding="utf-8")
    assert "DLUMA_DEVTOOLS_GAME_FRAME_VTABLE_RVA" in source
    assert "DLUMA_DEVTOOLS_GAME_FRAME_RVA" in source
    assert "memcmp(frame, frame_signature" in source
    assert "*slot != frame" in source
    assert "dluma_game_frame_hook" in source
    assert "game_frame_authority_missing_static_proof" in source
    assert "game_frame_authority_result" in source
    assert (
        "loader dispatch latched from positive frontend evidence "
        "on one exact serialized route"
    ) in source
    assert "dluma_ui_game_thread_tick" in ui
    assert "g_dluma.config.universal_screen_poll_interval_ms" in ui
    assert "now - g_dluma_info_last_update_ms < 500" not in ui


def test_key_probe_preserves_short_taps():
    source = (ROOT / "src" / "dlw_script.c").read_text(encoding="utf-8")
    assert "key_state & 1" in source


def test_legacy_probe_accepts_the_tested_data_scripts_location():
    source = (ROOT / "src" / "dlw_workshop.c").read_text(encoding="utf-8")
    assert 'L"data\\\\scripts"' in source
    assert 'L"data\\\\scripts\\\\dluma"' in source
    assert 'L"*.dlscript"' in source
