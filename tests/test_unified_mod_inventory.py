from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
UI = (ROOT / "src/dlw_ui.c").read_text(encoding="utf-8")
PATCHED = (ROOT / "src/dlw_ui_patched.c").read_text(encoding="utf-8")
XUI = (ROOT / "PakAssets/XUI/MenuDLUMAMods.xui").read_text(encoding="utf-8")


def block(source: str, start: str, end: str) -> str:
    begin = source.index(start)
    return source[begin : source.index(end, begin)]


def test_all_mods_is_the_default_four_filter_inventory() -> None:
    assert "#define DLUMA_REGISTRY_CATEGORY_ALL 0U" in UI
    assert "#define DLUMA_REGISTRY_CATEGORY_CODE 1U" in UI
    assert "#define DLUMA_REGISTRY_CATEGORY_LUA 2U" in UI
    assert "#define DLUMA_REGISTRY_CATEGORY_ASSET 3U" in UI
    assert "g_dluma_ui_registry_category = DLUMA_REGISTRY_CATEGORY_ALL" in UI
    assert "&amp;DLUMA_Mods_AllGroup&amp;" in XUI


def test_all_view_groups_code_lua_and_assets_with_menu_subcategory() -> None:
    populate = block(
        UI,
        "static void dluma_ui_registry_populate_list(",
        "static void dluma_ui_registry_refresh_if_needed(",
    )
    code = populate.index('"DLUMA_Mods_CodeHeader", "CODE MODS"')
    native = populate.index("g_dluma_ui_native_mod_count", code)
    managed = populate.index("g_dluma_ui_managed_mod_count", native)
    lua_header = populate.index('"DLUMA_Mods_LuaHeader", "LUA MODS"', managed)
    lua = populate.index("g_dluma_ui_global_mod_count", lua_header)
    asset = populate.index('"DLUMA_Mods_AssetHeader", "ASSET MODS"', lua)
    menu = populate.index(
        '"DLUMA_Mods_MainMenuHeader", "MAIN MENU MODS"', asset
    )
    assert code < native < managed < lua_header < lua < asset < menu
    assert "dluma_ui_registry_add_main_menu_rows" in populate


def test_main_menu_rows_collapse_sources_and_keep_exact_active_provider() -> None:
    rows = block(
        UI,
        "static void dluma_ui_registry_add_main_menu_rows(",
        "static void dluma_ui_registry_snapshot(",
    )
    assert "processed[DLUMA_CUSTOM_MENU_MAX_CANDIDATES]" in rows
    assert "requested_choice" in rows
    assert "first_valid_local" in rows
    assert "first_valid_steam" in rows
    assert "has_local" in rows and "has_steam" in rows
    assert "candidate->source_identity" in rows
    assert "DLUMA_Mods_Source_SteamLocalAvailable" in UI
    assert "DLUMA_Mods_Source_LocalSteamAvailable" in UI


def test_stock_is_builtin_fallback_and_menu_toggle_is_mutually_exclusive() -> None:
    rows = block(
        UI,
        "static void dluma_ui_registry_add_main_menu_rows(",
        "static void dluma_ui_registry_snapshot(",
    )
    toggle = block(
        UI,
        "static int dluma_ui_registry_request_row_state(",
        "static int dluma_ui_registry_activate_selected(",
    )
    assert '"base_game"' in rows
    assert "!requested_valid" in rows
    assert "stock_enabled" in rows
    assert "asset_menu_stock_disable_ignored" in toggle
    assert '? selected_row->script_id : "base_game"' in toggle
    assert "dluma_custom_main_menu_select_next_launch" in toggle


def test_rows_expose_four_columns_and_future_order_keys() -> None:
    assert "DLUMA_UI_FIX_MODS_VISIBLE_ROWS 10" in PATCHED
    for control in (
        "T_ModColumnName",
        "T_ModColumnType",
        "T_ModColumnStatus",
        "T_ModColumnSource",
    ):
        assert f"<Id>{control}</Id>" in XUI
    assert "mapping->display_type" in UI
    assert "mapping->display_status" in UI
    assert "mapping->display_source" in UI
    assert "dluma_ui_fix_present_registry_columns" in PATCHED
    assert "dluma_ui_workshop_get_first_visible" in PATCHED
    assert '"  |  "' not in UI
    for row in range(10):
        for column in ("Type", "Status", "Source"):
            assert f"<Id>T_ModRow{row}{column}</Id>" in XUI
    assert "mapping->order_key" in UI
    assert '"asset.main_menu:"' in UI


def test_selected_mod_hint_uses_manifest_description_and_colored_status() -> None:
    update = block(
        UI,
        "static void dluma_ui_registry_update_button_labels(void)",
        "static int dluma_ui_registry_validate_list",
    )
    assert "dluma_ui_registry_description" in update
    assert "item->description" in update
    assert "native->description" in update
    assert "managed->description" in update
    assert "dluma_ui_registry_selected_status" in update
    assert '"00c600"' in UI
    assert '"ff0000"' in UI
    assert '"f28c18"' in UI
    assert '"8d8581"' in UI
    assert '"%COLOR(RESET)"' in UI
    assert "<Id>T_ModsStatus</Id>" in XUI
    assert "<ColorControlSequenceEnabled>true</ColorControlSequenceEnabled>" in XUI


def test_asset_seam_randomizer_boundary_and_legacy_loader_exclusion_are_explicit() -> None:
    assert "DLUMA_REGISTRY_ROW_KIND_ASSET" in UI
    assert "DLUMA_ASSET_MOD_KIND_MAIN_MENU" in UI


def test_stock_and_menu_rows_have_read_only_details() -> None:
    stage = block(
        UI,
        "static int dluma_ui_mod_details_stage_selected(",
        "static void dluma_ui_mod_details_report_open_failure(",
    )
    details = block(
        UI,
        "static void dluma_ui_mod_details_add_overview(",
        "static void dluma_ui_mod_details_update_for_row(",
    )
    source = block(
        UI,
        "static void dluma_ui_mod_details_add_source(",
        "static int dluma_ui_mod_details_select_source(",
    )
    assert "DLUMA_REGISTRY_ROW_KIND_ASSET" in stage
    assert "g_dluma_ui_mod_details_asset_row = *row" in stage
    assert "STOCK MENU" in details
    assert "MENU MAP" in details
    assert "EFFECTIVE SOURCE" in source
    assert "Exactly one main menu is enabled for each launch" in details
    assert "Reserved for a future opt-in menu pool" in details
