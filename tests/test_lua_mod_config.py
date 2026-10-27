from __future__ import annotations

from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
CONFIG_C = ROOT / "src" / "dlw_lua_config.c"
CONFIG_H = ROOT / "src" / "dlw_lua_config.h"
DISCOVERY = ROOT / "src" / "dlw_lua_discovery.c"
RUNTIME = ROOT / "src" / "dlw_lua_runtime.c"
HELPER = ROOT / "src" / "dlw_lua54_helper.c"
API = ROOT / "src" / "dlw_lua54_api.h"
UI = ROOT / "src" / "dlw_ui.c"
MODS_XUI = ROOT / "PakAssets" / "XUI" / "MenuDLUMAMods.xui"
CONFIG_XUI = ROOT / "PakAssets" / "XUI" / "MenuDLUMAModConfig.xui"


def text(path: Path) -> str:
    return path.read_text(encoding="utf-8")


def block(source: str, start: str, end: str) -> str:
    begin = source.index(start)
    finish = source.index(end, begin)
    return source[begin:finish]


def test_schema_v1_limits_and_all_four_setting_kinds_are_bounded():
    header = text(CONFIG_H)
    discovery = text(DISCOVERY)
    for contract in (
        "#define DLUMA_LUA_CONFIG_SCHEMA_VERSION 1U",
        "#define DLUMA_LUA_CONFIG_MAX_CATEGORIES 8U",
        "#define DLUMA_LUA_CONFIG_MAX_SETTINGS_PER_CATEGORY 9U",
        "#define DLUMA_LUA_CONFIG_MAX_SETTINGS 48U",
        "#define DLUMA_LUA_CONFIG_MAX_ENUM_OPTIONS 16U",
        "#define DLUMA_LUA_CONFIG_MAX_TEXT_CHARACTERS 128U",
        "#define DLUMA_LUA_CONFIG_MAX_TEXT_BYTES 512U",
    ):
        assert contract in header
    for kind in ("boolean", "integer", "enum", "text"):
        assert f'dluma_stricmp(kind, "{kind}") == 0' in discovery
    assert "(limit - digit) / 10ULL" in discovery
    assert "(ULONGLONG)span % setting->step" in discovery
    assert "(ULONGLONG)default_offset % setting->step" in discovery
    assert "dluma_lua_config_utf8_valid" in discovery


def test_schema_is_static_literal_metadata_and_semantic_failure_is_isolated():
    discovery = text(DISCOVERY)
    parser = block(
        discovery,
        "static int dluma_lua_parse_literal_mod_manifest(",
        "typedef struct DlumaLuaPermissionUse",
    )
    assert 'dluma_stricmp(key, "config") == 0' in parser
    assert "dluma_lua_manifest_config_schema" in parser
    assert "dluma_lua_manifest_skip_value" in parser
    assert "A config schema is optional metadata" in parser
    assert "memset(schema, 0, sizeof(*schema));" in parser
    assert "return saw_id && saw_name && saw_version ? 1 : -1;" in parser
    assert '"dynamic_or_malformed"' in parser
    manifest_string = block(
        discovery,
        "static int dluma_lua_manifest_string(",
        "static void dluma_lua_manifest_skip_string(",
    )
    assert "if (!value) return 0;" in manifest_string
    assert "if (!escaped) return 0;" in manifest_string
    assert '"unsupported_schema_version"' in discovery
    assert '"unsupported_scope"' in discovery
    assert '"duplicate_identifier"' in discovery
    assert '"schema_limits"' in discovery


def test_schema_identity_is_owned_and_metadata_never_carries_worker_pointers():
    config = text(CONFIG_C)
    common = text(ROOT / "src" / "dlw_common.h")
    assert "DlumaLuaConfigSchema *schema;" in config
    assert "copy = (DlumaLuaConfigSchema *)dluma_alloc(sizeof(*copy));" in config
    assert "memcpy(copy, schema, sizeof(*copy));" in config
    assert "memcpy(out, slot->schema, sizeof(*out));" in config
    metadata = block(
        common,
        "typedef struct DlumaLuaMetadata",
        "} DlumaLuaMetadata;",
    )
    assert "config_schema_fingerprint" in metadata
    assert "config_category_count" in metadata
    assert "config_setting_count" in metadata
    assert "DlumaLuaConfigSchema" not in metadata
    assert "*config" not in metadata


def test_persistence_uses_digest_namespaces_typed_records_and_atomic_replace():
    config = text(CONFIG_C)
    assert 'L"dluma_runtime"' in config
    assert 'L"mod_config"' in config
    assert "identity->storage_key" in config
    assert "dluma_sha256_bytes_hex(" in config
    assert 'dluma_str_append(filename_utf8' in config
    assert '".dlumac"' in config
    assert "{'D', 'L', 'W', 'C', 'F', 'G', '1', '\\n'}" in config
    assert "data[8] = setting->kind;" in config
    assert "CREATE_NEW" in config
    assert "FlushFileBuffers(file)" in config
    assert "MOVEFILE_REPLACE_EXISTING |" in config
    assert "MOVEFILE_WRITE_THROUGH" in config
    assert "DLUMA_LUA_CONFIG_TOTAL_STORAGE_LIMIT (64U * 1024U)" in text(CONFIG_H)


def test_persistence_fails_closed_on_aliases_reparse_points_and_stale_schema():
    config = text(CONFIG_C)
    assert "dluma_lua_config_device_name" in config
    assert "(i == 1U && value[0] == '.')" in config
    assert "value[i - 1U] == '.'" in config
    assert "FILE_FLAG_OPEN_REPARSE_POINT" in config
    assert "GetFileInformationByHandle" in config
    assert config.count("nNumberOfLinks != 1U") >= 2
    assert "dluma_get_final_path_checked" in config
    assert "dluma_final_path_equal_or_under" in config
    assert "dluma_lua_config_prepare_directories" in config
    assert "dluma_lua_config_registry_matches_locked" in config
    assert '"config_schema_stale"' in config
    commit = block(
        config,
        "static int dluma_lua_config_write_record(",
        "int dluma_lua_config_write(",
    )
    assert commit.index("dluma_lua_config_registry_matches_locked") < commit.index(
        "MoveFileExW("
    )


def test_corrupt_or_incompatible_records_are_errors_and_reset_is_per_setting():
    config = text(CONFIG_C)
    read = block(
        config,
        "static int dluma_lua_config_read_unlocked(",
        "int dluma_lua_config_read(",
    )
    assert '"config_record_malformed"' in read
    assert '"config_value_invalid"' in read
    assert "data[8] != setting->kind" in read
    assert "value_length >= sizeof(value_text)" in read
    assert "strlen(value_text) != value_length" in read
    reset = block(
        config,
        "int dluma_mod_config_reset(",
        "\n}",
    )
    assert "dluma_lua_config_default_value(setting, &fallback);" in reset
    assert "DeleteFileW(path)" in reset
    assert "has_override &&" in reset
    prune = block(
        config,
        "void dluma_lua_config_prune(",
        "void dluma_lua_config_shutdown(",
    )
    assert "DeleteFileW" not in prune


def test_mods_and_config_screens_use_native_option_rows_and_native_history():
    mods = text(MODS_XUI)
    config_xui = text(CONFIG_XUI)
    ui = text(UI)
    for source in (mods, config_xui):
        assert "<Visual>ListBoxOptionsV</Visual>" in source
        assert "<Height>430.000000</Height>" in source
        assert "<ImagePath>menu_bg</ImagePath>" in source
        assert source.count("<Visual>ButtonDialogV</Visual>") >= 3
        assert "<!--" not in source
        raw = (
            MODS_XUI.read_bytes()
            if source is mods
            else CONFIG_XUI.read_bytes()
        )
        assert not raw.startswith(b"\xef\xbb\xbf")
        assert b"\r\n" in raw
        assert b"\n" not in raw.replace(b"\r\n", b"")
    assert "<Id>B_Configure</Id>" in mods
    assert "<PressPath>MenuDLUMAModConfig.xui</PressPath>" in mods
    configure = block(
        ui,
        'else if (dluma_ui_object_id_equals(button, "B_Configure"))',
        'else if (dluma_ui_object_id_equals(button, "B_Back"))',
    )
    assert "dluma_ui_mod_config_stage_selected()" in configure
    assert "dluma_ui_register_mod_config_screen" in configure
    assert "dluma_ui_show_next_registered_screen(" in configure
    assert "mod_config_show_next_ui_requested_id" in configure
    assert "LoadUIScreen" not in configure
    assert "ShowUIScreen" not in configure


def test_config_controls_use_expected_native_semantics_and_deferred_refresh():
    ui = text(UI)
    seed = block(
        ui,
        "static int dluma_ui_mod_config_seed_options(",
        "static void dluma_ui_mod_config_display_value(",
    )
    assert "DLUMA_LUA_CONFIG_BOOLEAN" in seed
    assert "DLUMA_LUA_CONFIG_INTEGER" in seed
    assert "DLUMA_LUA_CONFIG_ENUM" in seed
    assert "DLUMA_LUA_CONFIG_TEXT" in ui
    assert "dluma_ui_settings_add_native_option" in seed
    assert "g_dluma_option_set_selection" in seed
    assert "LONGLONG candidate" in text(UI)
    assert "dluma_ui_mod_config_show_editor" in ui
    option_hook = block(
        ui,
        "static void __fastcall dluma_ui_workshop_option_selected_hook(",
        "static void dluma_ui_registry_log_toggle(",
    )
    assert "dluma_ui_mod_config_change_direction(" in option_hook
    assert "g_dluma_mod_config_option_refresh_pending" in option_hook
    assert "dluma_ui_mod_config_populate_list(screen)" not in option_hook
    post = block(
        ui,
        "static void __fastcall dluma_ui_workshop_post_hook(",
        "static void __fastcall dluma_ui_workshop_button_hook(",
    )
    assert "g_dluma_mod_config_option_refresh_pending" in post
    assert "dluma_ui_mod_config_populate_list(screen)" in post


def test_staged_config_revalidates_revision_fingerprints_and_setting_key():
    ui = text(UI)
    revalidate = block(
        ui,
        "static int dluma_ui_mod_config_revalidate(",
        "static int dluma_ui_mod_config_resolve_controls(",
    )
    assert "registry_revision" in revalidate
    assert "registry_key" in revalidate
    assert "metadata_fingerprint" in revalidate
    assert "config_schema_fingerprint" in revalidate
    assert "g_dluma_ui_mod_config_stale = 1;" in revalidate
    write = block(
        ui,
        "static int dluma_ui_mod_config_write_value(",
        "static int dluma_ui_mod_config_change_direction(",
    )
    assert "dluma_ui_mod_config_revalidate()" in write
    assert "setting_key" in write
    assert "dluma_mod_config_write(" in write
    assert '"DLUMA_ModConfig_StaleWrite"' in write


def test_live_change_queue_is_copied_coalesced_and_dispatched_on_game_thread():
    runtime = text(RUNTIME)
    enqueue = block(
        runtime,
        "int dluma_lua_runtime_enqueue_config_changed(",
        "static void dluma_lua_runtime_dispatch_config_events(",
    )
    assert "DlumaLuaRuntimeConfigEvent" in enqueue
    assert "existing->change.value_type = new_value_type;" in enqueue
    assert "existing->change.old_value" in enqueue
    assert "config_event_count" in enqueue
    assert "DLUMA_LUA_RUNTIME_MAX_CONFIG_EVENTS" in enqueue
    pump = block(
        runtime,
        "void dluma_lua_runtime_game_thread_pump(void)",
        "void dluma_lua_runtime_stop(void)",
    )
    assert "dluma_lua_runtime_dispatch_config_events();" in pump
    assert "dluma_lua_runtime_dispatch_queued_events();" in pump
    assert pump.index("dluma_lua_runtime_dispatch_config_events();") < pump.index(
        "DLUMA_LUA54_EVENT_UPDATE"
    )
    helper = text(HELPER)
    assert '"on_config_changed"' in helper
    assert "dluma_helper_make_readonly(state);" in helper


def test_public_api_version_docs_and_intellisense_move_together():
    api = text(API)
    helper = text(HELPER)
    docs = text(ROOT / "docs" / "api" / "1.0" / "API_REFERENCE.md")
    intellisense = text(
        ROOT
        / "vscode-dying-light-lua-intellisense"
        / "src"
        / "api.js"
    )
    assert "#define DLUMA_LUA54_ABI_VERSION 0x00040000u" in api
    assert "DlumaLua54ConfigGetFn config_get;" in api
    assert "int (*begin_pump)(void* script);" in api
    assert 'lua_pushliteral(state, "1.0")' in helper
    assert "dluma.configuration.get" in docs
    assert "mod:on_config_changed" in docs
    assert "dluma.configuration.get" in intellisense
    assert "on_config_changed" in intellisense


def test_config_localization_keys_exist_in_all_catalogs():
    locale_root = ROOT / "PakAssets" / "Locale"
    catalogs = sorted(locale_root.glob("*/loader_texts_all.scr"))
    assert len(catalogs) == 17
    required = (
        "DLUMA_Common_Configure",
        "DLUMA_Common_Reset",
        "DLUMA_Mods_LuaGroup",
        "DLUMA_Mods_DllGroup",
        "DLUMA_ModConfig_Title",
        "DLUMA_ModConfig_StaleWrite",
        "DLUMA_ModConfig_SaveFailed",
        "DLUMA_ModConfig_ResetDone",
        "DLUMA_ModConfig_StaticOnly",
    )
    for catalog in catalogs:
        source = text(catalog)
        for key in required:
            assert f'String("{key}",' in source, (catalog, key)
