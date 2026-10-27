import subprocess
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
PARSER = (ROOT / "src" / "dlw_native_mod_config.c").read_text(
    encoding="utf-8"
)
COMPANION = (ROOT / "src" / "dlw_companion_archive.c").read_text(
    encoding="utf-8"
)
CONFIG_CORE = (ROOT / "src" / "dlw_lua_config.c").read_text(
    encoding="utf-8"
)
GAME_HOST = (ROOT / "src" / "game" / "dlw_game_host_context.c").read_text(
    encoding="utf-8"
)
UI = (ROOT / "src" / "dlw_ui.c").read_text(encoding="utf-8")
SDK = (ROOT / "sdk" / "c" / "dluma_api.h").read_text(
    encoding="utf-8"
)


def test_native_config_parser_fake_harness_executes_production_parser():
    completed = subprocess.run(
        [
            "cmd.exe",
            "/d",
            "/s",
            "/c",
            "call tools\\build_native_mod_config_fake_tests.bat",
        ],
        cwd=ROOT,
        check=True,
        capture_output=True,
        text=True,
        timeout=90,
    )
    assert completed.stdout.count("[PASS]") == 3
    assert "native mod config fake cases passed: 3" in completed.stdout


def block(source: str, start: str, end: str) -> str:
    begin = source.index(start)
    return source[begin : source.index(end, begin)]


def test_shared_json_schema_is_strict_bounded_and_supports_all_types():
    parse = block(
        PARSER,
        "int dluma_mod_config_parse_json(",
        "\nmalformed:",
    )
    assert 'key, "schemaVersion"' in parse
    assert 'key, "categories"' in parse
    assert "fields != 3U" in parse
    assert "DLUMA_LUA_CONFIG_SCHEMA_VERSION" in parse
    setting = block(
        PARSER,
        "static int dluma_native_config_parse_setting(",
        "static int dluma_native_config_parse_category(",
    )
    for kind in ("boolean", "integer", "enum", "text"):
        assert f'type, "{kind}"' in setting
    assert "DLUMA_LUA_CONFIG_MAX_ENUM_OPTIONS" in PARSER
    assert "DLUMA_LUA_CONFIG_MAX_SETTINGS_PER_CATEGORY" in PARSER
    assert "DLUMA_LUA_CONFIG_MAX_SETTINGS" in PARSER
    assert "DLUMA_LUA_CONFIG_MAX_CATEGORIES" in PARSER
    assert "(ULONGLONG)span % setting->step" in setting
    assert "duplicate_identifier" in PARSER
    assert "invalid_setting" in PARSER
    assert "unsupported_schema_version" in PARSER
    assert "int dluma_native_mod_config_parse_json(" in PARSER
    assert "return dluma_mod_config_parse_json(" in PARSER


def test_native_config_path_and_pack_entry_are_exact_and_optional():
    path = block(
        PARSER,
        "int dluma_native_mod_config_path_valid(",
        "static void dluma_native_config_space(",
    )
    assert '"data/"' in path
    assert '".json"' in path
    assert "dluma_native_config_segment_valid" in path
    discover = block(
        COMPANION,
        "int dluma_companion_archive_discover_file(",
        "int dluma_companion_archive_lock(",
    )
    assert "content_manifest_path" in discover
    assert "dluma_native_companion_path_valid(content_manifest_path)" in discover
    assert "snapshot->manifest_path" in discover
    assert "snapshot->config_manifest_path[0] = 0" in discover
    allowed = block(
        COMPANION,
        "static int dluma_native_companion_allowed_entry(",
        "static int dluma_native_companion_validate_zip(",
    )
    assert "dluma_native_companion_path_valid(name)" in allowed
    assert '".json"' in allowed
    assert '".csb"' in allowed


def test_manifest_declared_companion_has_no_mod_specific_config_entry():
    discover = block(
        COMPANION,
        "int dluma_companion_archive_discover_file(",
        "int dluma_companion_archive_lock(",
    )
    assert "data/drivinglight" not in COMPANION
    assert "snapshot->config_manifest_path[0] = 0" in discover
    assert "content_manifest_path" in discover


def test_shared_registry_keeps_lua_wrappers_and_native_owner_isolation():
    assert "DLUMA_MOD_CONFIG_OWNER_LUA" in CONFIG_CORE
    assert "DLUMA_MOD_CONFIG_OWNER_NATIVE" in CONFIG_CORE
    assert "slot->owner_kind == identity->owner_kind" in CONFIG_CORE
    assert "identity->storage_key" in CONFIG_CORE
    for legacy in (
        "int dluma_lua_config_publish(",
        "int dluma_lua_config_snapshot(",
        "int dluma_lua_config_read(",
        "int dluma_lua_config_write(",
        "int dluma_lua_config_reset(",
    ):
        assert legacy in CONFIG_CORE
    assert "dluma_mod_config_publish(" in CONFIG_CORE
    assert "dluma_mod_config_read(" in CONFIG_CORE
    assert "dluma_mod_config_write(" in CONFIG_CORE
    assert "dluma_mod_config_reset(" in CONFIG_CORE


def test_common_configuration_service_is_owner_bound_and_read_only():
    assert '#define DLUMA_SERVICE_CONFIGURATION_V1 "dluma.configuration.v1"' in SDK
    table = block(
        SDK,
        "typedef struct DlumaConfigurationV1",
        "} DlumaConfigurationV1;",
    )
    assert "*get" in table
    assert "*subscribe_changed" in table
    assert "*set" not in table
    read = block(
        GAME_HOST,
        "static DlumaStatus dluma_game_host_config_get(",
        "static int dluma_game_host_storage_encode(",
    )
    assert "state->config_identity.config_available" in read
    assert "dluma_lua_config_find_setting(" in read
    assert "dluma_mod_config_read(" in read
    assert "case 9001U:" in GAME_HOST


def test_native_ui_editing_is_independent_of_enable_and_approval_state():
    buttons = block(
        UI,
        "static void dluma_ui_registry_update_button_labels(void)\n{",
        "static int dluma_ui_registry_validate_list(",
    )
    config_enable = buttons.index("native->config_available")
    approval = buttons.index("!dluma_ui_native_hosts_approved")
    assert config_enable < approval
    stage = block(
        UI,
        "static int dluma_ui_mod_config_stage_selected(",
        "static int dluma_ui_mod_config_revalidate(",
    )
    assert "DLUMA_REGISTRY_ROW_KIND_NATIVE" in stage
    assert "g_dluma_ui_mod_config_native_request" in stage
    assert "approval_valid" not in stage
    assert "enabled_requested" not in stage
    revalidate = block(
        UI,
        "static int dluma_ui_mod_config_revalidate(",
        "static int dluma_ui_mod_config_resolve_controls(",
    )
    for identity_component in (
        "registry_revision",
        "registry_key",
        "approval_fingerprint",
        "config_schema_fingerprint",
    ):
        assert identity_component in revalidate
    assert "dluma_mod_config_snapshot(" in revalidate
    native_branch = revalidate.split(
        "if (g_dluma_ui_mod_config_owner_kind ==\n"
        "        DLUMA_MOD_CONFIG_OWNER_CSHARP)",
        1,
    )[0]
    assert (
        "revision !=\n"
        "            g_dluma_ui_mod_config_native_request.registry_revision"
        not in native_branch
    )
    schema_check = native_branch.index("dluma_mod_config_snapshot(")
    revision_refresh = native_branch.index(
        "g_dluma_ui_mod_config_native_request.registry_revision = revision;"
    )
    assert revision_refresh > schema_check
    assert "dluma_mod_config_write(" in UI
    assert "dluma_mod_config_reset(" in UI
    assert "DLUMA_MOD_CONFIG_OWNER_LUA" in UI
