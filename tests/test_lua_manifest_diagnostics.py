from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def test_native_discovery_reads_literal_dlua_mod_and_keeps_legacy_fallback():
    source = (ROOT / "src/dlw_lua_discovery.c").read_text(encoding="utf-8")
    assert "dluma_lua_parse_literal_mod_manifest" in source
    assert 'dluma_stricmp(token, "dlua")' in source
    assert 'dluma_stricmp(token, "mod")' in source
    assert 'dluma_stricmp(key, "permissions")' in source
    assert "Backward compatibility for 0.2.x scripts" in source


def test_native_permission_scan_reports_undeclared_api_permissions():
    source = (ROOT / "src/dlw_lua_discovery.c").read_text(encoding="utf-8")
    for api, permission in (
        ("dlua.world.find", "world.query"),
        (":on_sensor_enter", "events.sensor"),
        (":on_interact_begin", "events.interact"),
        (":on_enemy_killed", "events.enemy_killed"),
        ("dlua.quest.", "quest.write"),
        ("dlua.debug.", "ui.debug"),
        ("dlua.ui.", "ui.overlay"),
        ("dlua.audio.", "audio.play"),
    ):
        assert f'{{ "{api}", "{permission}" }}' in source
    assert 'dluma_log_line("ERROR", item.permission_diagnostic)' in source


def test_universal_screen_queues_each_error_for_ten_seconds_and_colors_it():
    log = (ROOT / "src/dlw_log.c").read_text(encoding="utf-8")
    ui = (ROOT / "src/dlw_ui.c").read_text(encoding="utf-8")
    assert 'dluma_stricmp(level, "ERROR") == 0' in log
    assert "dluma_ui_report_error" in log
    assert "#define DLUMA_UI_ERROR_DISPLAY_MS 10000UL" in ui
    assert "g_dluma_ui_error_expires_ms = now + DLUMA_UI_ERROR_DISPLAY_MS" in ui
    assert "DLUMA_DEVTOOLS_IUITEXT_SET_COLOR_VEC3_IAT_RVA" in ui
    assert "dluma_ui_set_info_panel_color" in ui


def test_vscode_extension_uses_api_catalog_for_permission_diagnostics():
    extension = (
        ROOT / "vscode-dying-light-lua-intellisense/extension.js"
    ).read_text(encoding="utf-8")
    api = (
        ROOT / "vscode-dying-light-lua-intellisense/src/api.js"
    ).read_text(encoding="utf-8")
    assert "const PERMISSION_RULES = permissionRules()" in extension
    assert "This API requires '${rule.permission}'" in extension
    assert "vscode.languages.createDiagnosticCollection" in extension
    assert "generated.operations.map" in api
    assert "catalogHash: generated.catalog_sha256" in api
    assert "manifestForDocument" in extension
    assert "Array.isArray(manifest.access)" in extension
    assert "Legacy Lua API 2.1 uses dlua and is unsupported" in extension
