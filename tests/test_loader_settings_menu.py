from pathlib import Path
import re
import zipfile

import pytest

from tools import build_content_pack


ROOT = Path(__file__).resolve().parents[1]
OPTIONS_PATH = ROOT / "PakAssets" / "XUI" / "DLUMA_MenuOptions.xui"
SETTINGS_PATH = ROOT / "PakAssets" / "XUI" / "MenuDLUMALoaderSettings.xui"
UI_PATH = ROOT / "src" / "dlw_ui.c"
PACK_PATH = (
    ROOT
    / "build"
    / "package"
    / "Release"
    / "dluma.pak"
)


def xui_button(text: str, object_id: str) -> str:
    marker = f"<Id>{object_id}</Id>"
    position = text.index(marker)
    start = text.rfind("<UINaviButton>", 0, position)
    end = text.index("</UINaviButton>", position) + len("</UINaviButton>")
    assert start >= 0
    return text[start:end]


def test_options_override_preserves_stock_controller_and_adds_loader_entry():
    raw = OPTIONS_PATH.read_bytes()
    text = raw.decode("utf-8")
    assert not raw.startswith(b"\xef\xbb\xbf")
    assert b"\r\n" in raw
    assert b"\n" not in raw.replace(b"\r\n", b"")
    assert "<!--" not in text
    assert "<Id>MenuOptions</Id>" in text
    assert "<ClassOverride>MenuOptions</ClassOverride>" in text
    entry = xui_button(text, "B_DLUMALoaderSettings")
    assert "<Text>&amp;DLUMA_Menu_ModLoader&amp;</Text>" in entry
    assert "<PressPath>MenuDLUMALoaderSettings.xui</PressPath>" in entry
    assert "<NavUp>B_VideoConsole</NavUp>" in entry
    assert "<NavDown>B_EyeTracking</NavDown>" in entry
    eye = xui_button(text, "B_EyeTracking")
    video = xui_button(text, "B_VideoConsole")
    assert "<NavUp>B_DLUMALoaderSettings</NavUp>" in eye
    assert "<NavDown>B_DLUMALoaderSettings</NavDown>" in video
    # UIStackPanel paints these stock options in reverse child order.
    assert text.index("<Id>B_EyeTracking</Id>") < text.index(
        "<Id>B_DLUMALoaderSettings</Id>"
    )


def test_settings_page_uses_proven_bridge_and_stock_edit_box():
    raw = SETTINGS_PATH.read_bytes()
    text = raw.decode("utf-8")
    assert not raw.startswith(b"\xef\xbb\xbf")
    assert b"\r\n" in raw
    assert b"\n" not in raw.replace(b"\r\n", b"")
    assert "<!--" not in text
    assert "<Id>MenuDLUMALoaderSettings</Id>" in text
    assert "<ClassOverride>MenuSteamWorkshop</ClassOverride>" in text
    assert "<ClassOverride>MenuExtras</ClassOverride>" not in text
    assert "<Visual>MySceneVisual</Visual>" in text
    for object_id in (
        "L_Maps",
        "B_Start",
        "B_Workshop",
        "B_Back",
        "G_MenuGroup",
        "B_PrevScreenMode",
        "B_NextScreenMode",
        "T_CurrentMode",
        "T_SettingsHintHeader",
        "T_SettingsHintBody",
        "T_SettingsStatus",
        "G_SettingsEditPopup",
        "G_SettingsExperimentalWarningPopup",
        "T_SettingsExperimentalWarningTitle",
        "T_SettingsExperimentalWarningBody",
        "B_SettingsExperimentalWarningYes",
        "B_SettingsExperimentalWarningNo",
        "E_SettingsValue",
        "B_SettingsSave",
        "B_SettingsCancel",
    ):
        assert f"<Id>{object_id}</Id>" in text
    assert "<UIEditBox>" in text
    assert "<Visual>EditBoxVisual</Visual>" in text
    assert "<MaxChars>512</MaxChars>" in text
    assert "<Position>313.000000,63.000000,0.000000</Position>" in text
    assert "<IUIEffectGroup>" not in text
    assert "<ClassOverride>UiFloatingGroupDi</ClassOverride>" not in text
    assert "<Visual>ListBoxOptionsV</Visual>" in text
    assert "<Visual>ListBoxHtmlV</Visual>" not in text
    assert "<Position>0.000000,529.000000,0.000000</Position>" in text
    status_start = text.rfind("<MyText>", 0, text.index("<Id>T_SettingsStatus</Id>"))
    status = text[status_start : text.index("</MyText>", status_start)]
    assert "<Position>11.000000,486.000000,0.000000</Position>" in status
    assert "<ImagePath>menu_bg</ImagePath>" in text
    assert "<Visual>ButtonDialogV</Visual>" in text
    assert "<Visual>ButtonWithHintV</Visual>" in text
    assert "<Id>I_SettingsBlack</Id>" not in text
    assert "<PressKey>22533</PressKey>" in text
    assert "<PressKey>22532</PressKey>" in text
    cancel = xui_button(text, "B_SettingsCancel")
    assert "<UnfocusedInput>true</UnfocusedInput>" in cancel
    assert "<PressKey>22529</PressKey>" in cancel
    popup = text[text.index("<Id>G_SettingsEditPopup</Id>") :]
    popup = popup[: popup.index("</Properties>")]
    assert "<UseScreenTransform>false</UseScreenTransform>" in popup
    for frame in ("Idle", "TransIn", "EndTransIn", "TransOut", "EndTransOut"):
        assert f"<Name>{frame}</Name>" in text
    assert "<TimelineProp>Opacity</TimelineProp>" in text


def test_settings_page_delegates_left_rail_and_back_to_stock_menu_background():
    text = SETTINGS_PATH.read_text(encoding="utf-8")
    for object_id in (
        "G_DLUMASettingsRail",
        "G_DLUMASettingsRailBack",
        "I_DLUMASettingsRailBack",
        "B_DLUMASettingsRailBack",
        "G_DLUMASettingsRailTitle",
        "I_DLUMASettingsRailTitle",
        "T_DLUMASettingsRailTitle",
    ):
        assert f"<Id>{object_id}</Id>" not in text
    assert "<Text>&amp;DLUMA_Menu_ModLoader&amp;</Text>" in text
    assert "<Visual>ButtonBackMainVertV</Visual>" not in text
    assert "<PressPath>MenuOptions.xui</PressPath>" not in text


def test_settings_page_reuses_the_validated_stock_options_3d_transform():
    source = UI_PATH.read_text(encoding="utf-8")
    assert "DLUMA_UI_USES_3D_ROTATION_VTABLE_BYTE_OFFSET 0x0A60UL" in source
    assert "DLUMA_DEVTOOLS_UIMENU_USES_3D_ROTATION_FN_RVA 0x000BF2C0UL" in source
    assert "DLUMA_DEVTOOLS_WORKSHOP_USES_3D_ROTATION_SLOT_RVA 0x016F5DE8UL" in source
    assert "DLUMA_UI_GET_3D_POSITION_VTABLE_BYTE_OFFSET 0x0A68UL" in source
    assert "DLUMA_DEVTOOLS_UIMENU_GET_3D_POSITION_FN_RVA 0x00767B90UL" in source
    assert "DLUMA_DEVTOOLS_WORKSHOP_GET_3D_POSITION_SLOT_RVA 0x016F5DF0UL" in source
    uses_3d_hook = source[
        source.index("static unsigned char __fastcall dluma_ui_workshop_uses_3d_rotation_hook") :
        source.index("static __int64 __fastcall dluma_ui_workshop_get_3d_position_hook")
    ]
    assert "dluma_ui_is_loader_settings_screen(screen)" in uses_3d_hook
    assert "result = 1;" in uses_3d_hook
    assert "result = original(screen);" in uses_3d_hook
    assert "loader_settings_uses_3d_rotation" in uses_3d_hook
    hook = source[
        source.index("static __int64 __fastcall dluma_ui_workshop_get_3d_position_hook") :
        source.index("static void dluma_ui_registry_log_validation_failure")
    ]
    assert '"MenuOptionsControlsKeyboard"' in hook
    assert "menu_options_full_pos" in hook
    assert "original(screen, out_name)" in hook
    assert "dluma_ui_is_loader_settings_screen(screen)" in hook
    assert "out_name->size = required" in hook
    assert "loader_settings_3d_position_name" in hook
    assert "Get3dPositionName_prefix" in source
    assert "Uses3dRotation_prefix" in source
    assert "dluma_ui_registry_restore_slot(" in source
    assert "g_dluma_workshop_uses_3d_rotation_vtable_slot" in source
    assert "g_dluma_workshop_get_3d_position_vtable_slot" in source


def test_settings_page_enables_the_native_background_name_rail_gate_safely():
    source = UI_PATH.read_text(encoding="utf-8")
    assert "G_LeftPanel" not in source
    assert "ShowCommonPart(" not in source
    assert "DLUMA_UI_USES_BACKGROUND_SCREEN_NAME_VTABLE_BYTE_OFFSET 0x0E40UL" in source
    assert "DLUMA_DEVTOOLS_WORKSHOP_USES_BACKGROUND_SCREEN_NAME_SLOT_RVA 0x016F61C8UL" in source
    assert "DLUMA_DEVTOOLS_UIMENU_USES_BACKGROUND_SCREEN_NAME_FN_RVA 0x000BF2C0UL" in source
    assertion = source[
        source.index("typedef char DlumaWorkshopUsesBackgroundScreenNameOffsetMustMatch") :
        source.index("static const DlumaLoaderSettingDefinition")
    ]
    assert "DLUMA_DEVTOOLS_MENU_STEAM_WORKSHOP_VTABLE0_RVA" in assertion
    assert "DLUMA_UI_USES_BACKGROUND_SCREEN_NAME_VTABLE_BYTE_OFFSET" in assertion
    assert "DLUMA_DEVTOOLS_WORKSHOP_USES_BACKGROUND_SCREEN_NAME_SLOT_RVA" in assertion
    hook = source[
        source.index("dluma_ui_workshop_uses_background_screen_name_hook") :
        source.index("static __int64 __fastcall dluma_ui_workshop_get_3d_position_hook")
    ]
    assert "dluma_ui_is_stock_workshop_screen(screen)" in hook
    assert "dluma_ui_is_loader_settings_screen(screen)" in hook
    assert "result = 1;" in hook
    assert "result = original(screen);" in hook
    assert "g_dluma_workshop_rail_diagnostic_pending" in hook
    assert "loader_settings_uses_background_screen_name" in hook
    assert "stock MenuBackgroundDL common left-rail Show/Hide route" in hook
    installer = source[
        source.index("static int dluma_ui_install_registry_hooks") :
        source.index("static void dluma_ui_restore_registry_hooks_now")
    ]
    assert re.search(r"void\s*\*\s*\*\s*slots\s*\[10\]", installer)
    assert re.search(r"void\s*\*\s*expected\s*\[10\]", installer)
    assert re.search(r"void\s*\*\s*hooks\s*\[10\]", installer)
    assert "FillMaps_descriptor_callback" not in installer
    assert "UsesBackgroundScreenName_slot" in installer
    assert "UsesBackgroundScreenName_prefix" in installer
    assert "uses_background_screen_name_prefix" in installer
    assert "DLUMA_ARRAY_COUNT(slots)" in installer
    assert "for (j = patched; j > 0; --j)" in installer
    assert "loader_settings_uses_background_screen_name_slot_address" in installer
    assert "loader_settings_uses_background_screen_name_observed_target" in installer
    assert "loader_settings_uses_background_screen_name_expected_target" in installer
    assert "loader_settings_uses_background_screen_name_pe_timestamp" in installer
    assert "loader_settings_uses_background_screen_name_image_size" in installer
    restore = source[
        source.index("static void dluma_ui_restore_registry_hooks_now") :
        source.index("static void dluma_ui_clear_active_mods_screen")
    ]
    assert "g_dluma_workshop_uses_background_screen_name_vtable_slot" in restore
    assert "dluma_ui_workshop_uses_background_screen_name_hook" in restore
    assert "g_dluma_workshop_uses_background_screen_name_original" in restore
    assert "g_dluma_workshop_uses_background_screen_name_vtable_slot = 0" in restore
    assert "g_dluma_workshop_uses_background_screen_name_original = 0" in restore


def test_settings_screen_is_registered_for_native_presspath_navigation():
    source = UI_PATH.read_text(encoding="utf-8")
    registration = source[
        source.index("static int dluma_ui_register_settings_screen") :
        source.index("static int dluma_ui_object_id_equals")
    ]
    assert 'script_path_text[] = "MenuDLUMALoaderSettings.xui"' in registration
    assert "register_screen(" in registration
    assert "load_ui_screen" not in registration
    assert "show_ui_screen" not in registration
    assert "g_dluma_settings_registered_level = level" in registration
    assert "dluma_ui_register_settings_screen(&g_dluma.engine, level)" in source


def test_options_click_bridge_registers_on_live_level_then_delegates():
    source = UI_PATH.read_text(encoding="utf-8")
    hook = source[
        source.index("static void __fastcall dluma_ui_options_button_hook") :
        source.index("static int dluma_ui_install_options_button_hook")
    ]
    assert 'dluma_ui_object_id_equals(button, "B_DLUMALoaderSettings")' in hook
    assert "level = g_dluma_get_object_level(button)" in hook
    assert "level = g_dluma_get_object_level(menu_options)" in hook
    assert "dluma_ui_register_settings_screen(&g_dluma.engine, level)" in hook
    assert "loader_settings_registration_bridge_presspath_id" in hook
    assert "original(menu_options, button, event_code)" in hook
    assert "load_ui_screen" not in hook
    assert "show_ui_screen" not in hook


def test_options_click_hook_is_exact_build_validated_and_restored():
    source = UI_PATH.read_text(encoding="utf-8")
    assert "DLUMA_DEVTOOLS_MENU_OPTIONS_VTABLE0_RVA 0x019D1788UL" in source
    assert "DLUMA_DEVTOOLS_MENU_OPTIONS_BUTTON_FN_RVA 0x01240910UL" in source
    assert "DLUMA_DEVTOOLS_MENU_OPTIONS_BUTTON_SLOT_RVA 0x019D2170UL" in source
    assert "DLUMA_UI_ON_BUTTON_VTABLE_BYTE_OFFSET 0x09E8UL" in source
    installer = source[
        source.index("static int dluma_ui_install_options_button_hook") :
        source.index("static int dluma_ui_restore_options_button_hook_now")
    ]
    assert "timestamp != DLUMA_DEVTOOLS_GAMEDLL_TIMESTAMP" in installer
    assert "image_size != DLUMA_DEVTOOLS_GAMEDLL_IMAGE_SIZE" in installer
    assert "*slot != expected" in installer
    assert re.search(
        r"\*slot\s*=\s*\(void\s*\*\)\s*dluma_ui_options_button_hook",
        installer,
    )
    assert "dluma_ui_restore_options_button_hook_now" in source
    assert "dluma_ui_uninstall_options_button_hook();" in source


def test_settings_table_matches_supported_ini_contract_and_safe_bounds():
    source = UI_PATH.read_text(encoding="utf-8")
    table = source[
        source.index("static const DlumaLoaderSettingDefinition g_dluma_loader_settings") :
        source.index("static DWORD dluma_ui_read_u32")
    ]
    for section, key in (
        ("loader", "poll_interval_ms"),
        ("loader", "universal_screen_poll_interval_ms"),
        ("loader", "unmatched_level_grace_ms"),
        ("loader", "enable_key_events"),
        ("loader", "log_timer_events"),
        ("loader", "developer_mode"),
        ("lua", "memory_limit_bytes"),
        ("lua", "instruction_limit"),
        ("lua", "wall_clock_limit_ms"),
        ("lua", "max_callbacks"),
        ("lua", "max_timers"),
        ("lua", "max_rules"),
        ("lua", "log_messages_per_second"),
        ("lua_local_permissions", "game_read"),
        ("lua_local_permissions", "player_read"),
        ("lua_local_permissions", "world_query"),
        ("lua_local_permissions", "events_sensor"),
        ("lua_local_permissions", "events_interact"),
        ("lua_local_permissions", "storage_mod"),
        ("lua_local_permissions", "ui_debug"),
        ("lua_local_permissions", "ui_overlay"),
        ("lua_local_permissions", "audio_play"),
        ("lua_local_permissions", "world_write"),
        ("crash_dialog", "enabled"),
        ("crash_dialog", "random_titles"),
        ("GameFixes", "CustomMapQuestLevelNameCrash"),
        ("GameFixes", "CustomMapCampaignCreditsExitCrash"),
        ("GameFixes", "CustomMapAssetIsolation"),
        ("GameFixes", "HideInvalidWorkshopProjects"),
        ("GameFixes", "CustomMapLoadingScreenContent"),
        ("GameFixes", "MouseWindowLock"),
        ("roots", "root1"),
        ("roots", "root8"),
    ):
        assert f'L"{section}", L"{key}"' in table
    assert "DLUMA_LOADER_SETTING_BOOL" in table
    assert "DLUMA_LOADER_SETTING_UINT" in table
    assert "DLUMA_LOADER_SETTING_TEXT" in table
    assert "label_key" in source
    assert "label_fallback" in source
    assert "hint_key" in source
    assert "hint_fallback" in source
    assert "CONTROL CHARACTERS ARE NOT ALLOWED" in source
    assert "VALUE IS OUTSIDE THE SAFE RANGE" in source
    for category in (
        "DLUMA_LOADER_CATEGORY_GENERAL",
        "DLUMA_LOADER_CATEGORY_LUA",
        "DLUMA_LOADER_CATEGORY_MOD_PERMISSIONS",
        "DLUMA_LOADER_CATEGORY_CRASH_DIALOG",
        "DLUMA_LOADER_CATEGORY_GAME_FIXES",
        "DLUMA_LOADER_CATEGORY_WORKSHOP_ROOTS",
    ):
        assert category in table
    assert table.count('L"roots", L"root') == 8
    universal_refresh = re.search(
        r'\{L"loader",\s*L"universal_screen_poll_interval_ms",\s*L"100"[\s\S]*?\},',
        table,
    )
    assert universal_refresh is not None
    assert "DLUMA_LOADER_SETTING_UINT, 10, 5000, 10, 5" in universal_refresh.group(0)
    assert "DLUMA_LOADER_CATEGORY_GENERAL" in universal_refresh.group(0)
    assert table.count("DLUMA_LOADER_CATEGORY_GAME_FIXES") == 7
    for retired_key in (
        "CustomMapAssetIsolationTrace",
        "CustomMapContentIsolation",
        "ResetSources",
        "ResetGlobalData",
        "ResetAudioBanks",
        "VerifyFrontendBaseline",
        "ExperimentalPlayerPresetRebuild",
    ):
        assert retired_key not in table

    local_defaults = {
        "game_read": "1",
        "player_read": "1",
        "world_query": "1",
        "events_sensor": "1",
        "events_interact": "1",
        "storage_mod": "1",
        "ui_debug": "1",
        "ui_overlay": "1",
        "audio_play": "1",
        "world_write": "1",
    }
    for key, fallback in local_defaults.items():
        row = re.search(
            rf'\{{\s*L"lua_local_permissions",\s*L"{key}",\s*L"{fallback}"[\s\S]*?\}},',
            table,
        )
        assert row is not None
        assert "DLUMA_LOADER_SETTING_BOOL" in row.group(0)
    host = (ROOT / "src" / "dlw_host.c").read_text(encoding="utf-8")
    sample_ini = (ROOT / "config" / "dluma.ini").read_text(
        encoding="utf-8"
    )
    for key, fallback in local_defaults.items():
        assert f'DLUMA_READ_LOCAL_PERMISSION("{key}", {fallback},' in host
        assert f"{key}={fallback}" in sample_ini
    assert table.count("DLUMA_LOADER_CATEGORY_MOD_PERMISSIONS") == 10
    assert "DLUMA_LOADER_CATEGORY_LOCAL_LUA_ACCESS" not in source
    assert "DLUMA_LOADER_CATEGORY_LOCAL_LUA_WRITE" not in source


def test_content_isolation_ui_uses_component_status_wording_not_a_fix_claim():
    source = UI_PATH.read_text(encoding="utf-8")
    table = source[
        source.index("static const DlumaLoaderSettingDefinition g_dluma_loader_settings") :
        source.index("g_dluma_loader_category_names")
    ]

    fix_source = (
        ROOT
        / "src"
        / "fixes"
        / "custom_map_asset_isolation"
        / "CustomMapAssetIsolationFix.cpp"
    ).read_text(encoding="utf-8")
    assert '"Custom Map Content Isolation\\nOverall: "' in fix_source
    assert '"Custom-map asset isolation: "' not in source
    assert "RESET CUSTOM PROJECT ON EXIT" in table
    assert "EXPERIMENTAL. UNLOADS CUSTOM PROJECT RESOURCES AFTER EXIT, BUT IS NOT FULLY IMPLEMENTED." in table
    assert "TARGETED PLAYER PRESET REPAIR" not in table
    assert "DIAGNOSTICONLY MEANS TRACE HOOKS ACTIVE" not in table


def test_settings_categories_filter_rows_and_support_stock_prev_next_controls():
    source = UI_PATH.read_text(encoding="utf-8")
    populate = source[
        source.index("static void dluma_ui_settings_populate_list") :
        source.index("static void dluma_ui_settings_select_setting")
    ]
    button_route = source[
        source.index("static void __fastcall dluma_ui_workshop_button_hook") :
        source.index("static void __fastcall dluma_ui_workshop_double_hook")
    ]
    for label in (
        '"GENERAL"',
        '"LUA"',
        '"MOD PERMISSIONS"',
        '"CRASH DIALOG"',
        '"GAME FIXES"',
        '"WORKSHOP ROOTS"',
    ):
        assert label in source
    assert "g_dluma_loader_settings[i].category !=" in populate
    assert "g_dluma_ui_settings_category" in populate
    assert 'dluma_ui_object_id_equals(button, "B_PrevScreenMode")' in button_route
    assert 'dluma_ui_object_id_equals(button, "B_NextScreenMode")' in button_route
    assert "dluma_ui_settings_cycle_category(screen, -1)" in button_route
    assert "dluma_ui_settings_cycle_category(screen, 1)" in button_route


def test_settings_rows_use_native_label_and_value_associations():
    source = UI_PATH.read_text(encoding="utf-8")
    builders = source[
        source.index("static void dluma_ui_settings_build_label") :
        source.index("static unsigned int dluma_ui_settings_current_index")
    ]
    for forbidden in ("<table", "<tr", "<td", "<font", "RESTART"):
        assert forbidden not in builders
    assert '"ON" : "OFF"' in builders
    assert 'value[0] ? value : "(EMPTY)"' in builders
    helper = source[
        source.index("static int dluma_ui_set_element_text_association") :
        source.index("static void dluma_ui_set_text_element_text")
    ]
    assert "g_dluma_set_element_text(element, &value, association)" in helper
    assert "dluma_ui_set_element_text_association(element, text, 0)" in helper
    populate = source[
        source.index("static void dluma_ui_settings_populate_list") :
        source.index("static void dluma_ui_settings_select_setting")
    ]
    assert "dluma_ui_settings_add_row(" in populate
    assert "dluma_ui_registry_add_row(" not in populate
    assert "VALUES APPLY AFTER RESTART." in populate
    option_row = source[
        source.index("dluma_ui_settings_add_row(") :
        source.index("static void dluma_ui_registry_snapshot")
    ]
    assert 'option_template_text[] = "OptionTextDummy"' in option_row
    assert "dluma_ui_set_element_text_association(item, value, 1)" in option_row
    assert "ButtonDummy hides that value until Focus" in option_row


def test_settings_rows_seed_real_native_option_models_for_arrow_state():
    source = UI_PATH.read_text(encoding="utf-8")
    assert "DLUMA_DEVTOOLS_OPTION_ADD_EXPLICIT_RVA 0x0112C680UL" in source
    assert "DLUMA_DEVTOOLS_OPTION_SET_SELECTION_RVA 0x0112C8E0UL" in source
    assert "option_add_explicit_prefix" in source
    assert "option_set_selection_prefix" in source
    seed = source[
        source.index("static int dluma_ui_settings_seed_native_options") :
        source.index("static void dluma_ui_registry_snapshot")
    ]
    assert 'dluma_ui_settings_add_native_option(option_control, "OFF")' in seed
    assert 'dluma_ui_settings_add_native_option(option_control, "ON")' in seed
    assert "current - definition->step" in seed
    assert "current + definition->step" in seed
    assert "g_dluma_option_set_selection(option_control, baseline, 0);" in seed
    assert "definition->kind == DLUMA_LOADER_SETTING_TEXT" in seed
    populate = source[
        source.index("static void dluma_ui_settings_populate_list") :
        source.index("static void dluma_ui_settings_select_setting")
    ]
    assert "mapping->option_baseline_index" in populate
    assert "dluma_ui_settings_seed_native_options(item, i)" in populate


def test_native_option_callback_changes_ini_and_defers_row_rebuild():
    source = UI_PATH.read_text(encoding="utf-8")
    assert "DLUMA_UI_ON_OPTION_SELECTED_VTABLE_BYTE_OFFSET 2560UL" in source
    assert "DLUMA_DEVTOOLS_WORKSHOP_OPTION_SELECTED_SLOT_RVA 0x016F5D88UL" in source
    assert "DLUMA_DEVTOOLS_BASE_OPTION_SELECTED_FN_RVA 0x00428080UL" in source
    callback = source[
        source.index("static void __fastcall dluma_ui_workshop_option_selected_hook") :
        source.index("static void dluma_ui_registry_log_toggle")
    ]
    assert "DLUMA_UI_OPTION_SELECTED_INDEX_OFFSET" in callback
    assert "selected < mapping->option_baseline_index" in callback
    assert "selected > mapping->option_baseline_index" in callback
    assert "dluma_ui_settings_change_direction(" in callback
    assert "mapping->setting_index, direction, 1" in callback
    assert "dluma_ui_settings_populate_list" not in callback
    post = source[
        source.index("static void __fastcall dluma_ui_workshop_post_hook") :
        source.index("static void __fastcall dluma_ui_workshop_init_hook")
    ]
    assert "g_dluma_settings_option_refresh_pending" in post
    assert "g_dluma_settings_option_refresh_index" in post
    assert "dluma_ui_settings_populate_list(screen);" in post
    assert "dluma_ui_settings_select_setting((unsigned int)setting_index);" in post
    restore = source[
        source.index("static void dluma_ui_restore_registry_hooks_now") :
        source.index("static void dluma_ui_clear_active_mods_screen")
    ]
    assert "g_dluma_workshop_option_selected_vtable_slot" in restore
    assert "dluma_ui_workshop_option_selected_hook" in restore
    assert "g_dluma_workshop_option_selected_original" in restore


def test_native_setting_directions_are_bounded_instead_of_wrapping():
    source = UI_PATH.read_text(encoding="utf-8")
    change = source[
        source.index("static int dluma_ui_settings_change_direction") :
        source.index("static int dluma_ui_settings_quick_change")
    ]
    assert "direction < 0 ? 0U : 1U" in change
    assert "current <= definition->minimum ? definition->minimum" in change
    assert "current >= definition->maximum ? definition->maximum" in change
    assert "THIS VALUE IS ALREADY AT ITS SAFE LIMIT." in change
    assert "next = definition->minimum;" not in change


def test_settings_selection_uses_the_immediate_callback_row_and_cache():
    source = UI_PATH.read_text(encoding="utf-8")
    assert "g_dluma_ui_settings_selected_row = 0xFFFFFFFFUL" in source
    update = source[
        source.index("static void dluma_ui_settings_update_for_row") :
        source.index("static int dluma_ui_settings_resolve_controls")
    ]
    assert "row_index < g_dluma_ui_settings_row_count" in update
    assert "g_dluma_ui_settings_selected_row = row_index" in update
    assert 'label_key = "DLUMA_Common_Toggle"' in update
    assert 'label_key = "DLUMA_Common_EditValue"' in update
    assert 'label_key = "DLUMA_Common_NextValue"' in update
    selection = source[
        source.index("static void __fastcall dluma_ui_workshop_selection_hook") :
        source.index("static void dluma_ui_registry_log_toggle")
    ]
    assert "g_dluma_base_main_menu_selection(screen, selection);" in selection
    assert "dluma_ui_settings_update_for_row((unsigned int)selection);" in selection
    populate = source[
        source.index("static void dluma_ui_settings_populate_list") :
        source.index("static void dluma_ui_settings_select_setting")
    ]
    assert "dluma_ui_list_set_selection(g_dluma_ui_settings_list, 0);" in populate
    assert "dluma_ui_settings_update_for_row(0);" in populate
    select_setting = source[
        source.index("static void dluma_ui_settings_select_setting") :
        source.index("static void dluma_ui_settings_cycle_category")
    ]
    assert "dluma_ui_settings_update_for_row(i);" in select_setting


def test_settings_mouse_focus_updates_hint_without_waiting_for_selection():
    source = UI_PATH.read_text(encoding="utf-8")
    assert "DLUMA_UI_LIST_FOCUSED_INDEX_OFFSET 0x94UL" in source
    focus_reader = source[
        source.index("static int dluma_ui_list_get_focused_index") :
        source.index("static void dluma_ui_list_set_selection")
    ]
    assert "dluma_is_readable" in focus_reader
    assert "DLUMA_UI_LIST_FOCUSED_INDEX_OFFSET" in focus_reader
    post_hook = source[
        source.index("static void __fastcall dluma_ui_workshop_post_hook") :
        source.index("static void __fastcall dluma_ui_workshop_init_hook")
    ]
    assert "dluma_ui_list_get_focused_index" in post_hook
    assert "dluma_ui_list_get_selection" in post_hook
    assert "int active = selected >= 0 ? selected : focused;" in post_hook
    assert "active < (int)g_dluma_ui_settings_row_count" in post_hook
    assert "dluma_ui_settings_update_for_row((unsigned int)active)" in post_hook
    pointer = source[
        source.index("static int dluma_ui_settings_select_pointer_item") :
        source.index("static void dluma_ui_settings_update_for_row")
    ]
    assert "*row_out = i;" in pointer
    button_route = source[
        source.index("static void __fastcall dluma_ui_workshop_button_hook") :
        source.index("static void __fastcall dluma_ui_workshop_double_hook")
    ]
    assert "dluma_ui_settings_update_for_row(settings_pointer_row);" in button_route
    double_route = source[
        source.index("static void __fastcall dluma_ui_workshop_double_hook") :
        source.index("static unsigned char __fastcall dluma_ui_workshop_uses_3d_rotation_hook")
    ]
    assert "dluma_ui_settings_update_for_row(settings_pointer_row);" in double_route
    current = source[
        source.index("static unsigned int dluma_ui_settings_current_index") :
        source.index("static int dluma_ui_settings_select_pointer_item")
    ]
    assert "g_dluma_ui_settings_selected_row < g_dluma_ui_settings_row_count" in current
    assert "selection = dluma_ui_list_get_selection" in current
    category = source[
        source.index("static void dluma_ui_settings_cycle_category") :
        source.index("static void dluma_ui_settings_set_main_controls_visible")
    ]
    assert "g_dluma_ui_settings_selected_row = 0xFFFFFFFFUL" in category
    uninstall = source[
        source.index("static void dluma_ui_uninstall_registry_hooks") :
        source.index("static void dluma_ui_clear_active_mods_screen")
    ]
    assert "g_dluma_ui_settings_selected_row = 0xFFFFFFFFUL" in uninstall


def test_selected_setting_updates_stock_style_bottom_hint_text():
    source = UI_PATH.read_text(encoding="utf-8")
    update = source[
        source.index("static void dluma_ui_settings_update_for_row") :
        source.index("static int dluma_ui_settings_resolve_controls")
    ]
    assert "g_dluma_ui_settings_hint_header" in update
    assert "g_dluma_ui_settings_hint_body" in update
    assert "g_dluma_loader_settings[index].hint" in update


def test_custom_project_game_fixes_have_menu_sized_hint_descriptions():
    source = UI_PATH.read_text(encoding="utf-8")
    credits_hint = (
        "PREVENTS A VALIDATED NULL CLEANUP CRASH AFTER CUSTOM CAMPAIGN CREDITS."
    )
    reset_hint = (
        "EXPERIMENTAL. UNLOADS CUSTOM PROJECT RESOURCES AFTER EXIT, BUT IS NOT FULLY IMPLEMENTED."
    )
    assert credits_hint in source
    assert reset_hint in source
    assert len(credits_hint) < 128
    assert len(reset_hint) < 128


def test_invalid_workshop_project_fix_defaults_enabled_and_updates_selector_state():
    source = UI_PATH.read_text(encoding="utf-8")
    table = source[
        source.index("static const DlumaLoaderSettingDefinition g_dluma_loader_settings") :
        source.index("static DWORD dluma_ui_read_u32")
    ]
    row = re.search(
        r'\{L"GameFixes",\s*L"HideInvalidWorkshopProjects",\s*L"1"[\s\S]*?\},',
        table,
    )
    assert row is not None
    assert "DLUMA_LOADER_SETTING_BOOL" in row.group(0)
    assert "DLUMA_LOADER_CATEGORY_GAME_FIXES" in row.group(0)
    assert "DLUMA_Settings_GameFixes_HideInvalidWorkshopProjects_Label" in row.group(0)
    assert "DLUMA_Settings_GameFixes_HideInvalidWorkshopProjects_Hint" in row.group(0)

    host = (ROOT / "src" / "dlw_host.c").read_text(encoding="utf-8")
    assert 'L"GameFixes", L"HideInvalidWorkshopProjects", 1' in host
    assert "hide_invalid_workshop_projects_enabled" in host

    sample_ini = (ROOT / "config" / "dluma.ini").read_text(
        encoding="utf-8"
    )
    assert "HideInvalidWorkshopProjects=1" in sample_ini

    writer = source[
        source.index("static int dluma_ui_settings_write_value") :
        source.index("static int dluma_ui_settings_parse_uint")
    ]
    assert 'L"HideInvalidWorkshopProjects"' in writer
    assert "g_dluma.config.hide_invalid_workshop_projects_enabled" in writer


def test_stock_mytext_labels_use_iuitext_setter_not_control_setter():
    source = UI_PATH.read_text(encoding="utf-8")
    assert (
        '"?SetTextExplicit@IUIText@@UEAAXAEBV?$string_base@_W@ttl@@@Z"'
        in source
    )
    assert re.search(
        r"typedef\s+void\s*\(\s*__fastcall\s*\*\s*"
        r"DlumaSetTextElementTextFn\s*\)",
        source,
    )
    assert "g_dluma_set_text_element_text(element, &value);" in source
    for pointer in (
        "g_dluma_ui_settings_category_title",
        "g_dluma_ui_settings_hint_header",
        "g_dluma_ui_settings_hint_body",
        "g_dluma_ui_settings_status",
        "g_dluma_ui_settings_edit_title",
    ):
        assert re.search(
            rf"dluma_ui_set_text_element_text\(\s*{pointer}", source
        )
        assert not re.search(
            rf"dluma_ui_set_element_text\(\s*{pointer}", source
        )


def test_text_entry_reads_stock_ui_editbox_and_writes_only_loader_ini():
    source = UI_PATH.read_text(encoding="utf-8")
    assert "DLUMA_UI_EDIT_TEXT_OFFSET 0x00A0UL" in source
    assert "+0xA0" in source and "+0xA8" in source
    assert "DLUMA_UI_EDIT_ENGINE_TIMESTAMP" not in source
    assert "DLUMA_UI_EDIT_ENGINE_IMAGE_SIZE" not in source
    assert 'GetModuleHandleW(L"gamedll_x64_rwdi.dll")' in source
    assert "timestamp != DLUMA_DEVTOOLS_GAMEDLL_TIMESTAMP" in source
    assert "image_size != DLUMA_DEVTOOLS_GAMEDLL_IMAGE_SIZE" in source
    assert "the UIEditBox layout is unsupported on this gamedll build" in source
    assert "edit_box_vtable_not_in_gamedll" in source
    assert (
        '"?GetTextExplicit@IUIElementControl@@UEAAAEBV?$string_base@_W@ttl@@H@Z"'
        in source
    )
    assert (
        '"?GetTextExplicit@IUIText@@UEAAAEBV?$string_base@_W@ttl@@XZ"'
        in source
    )
    assert re.search(
        r"typedef\s+const\s+DlumaTtlWideString\s*\*\s*\(\s*__fastcall\s*\*\s*"
        r"DlumaGetElementTextFn\s*\)",
        source,
    )
    assert "g_dluma_get_element_text(edit_box, 0)" in source
    assert "g_dluma_get_text_element_text(" in source
    assert "g_dluma_ui_settings_edit_text_presenter" in source
    assert 'g_dluma_ui_settings_edit_box, "Text"' in source
    shared_reader = source[
        source.index("static int dluma_ui_read_edit_box_text") :
        source.index("static int dluma_ui_settings_get_edit_text")
    ]
    assert "g_dluma_get_element_text(edit_box, 0)" in shared_reader
    accessor = shared_reader.index("g_dluma_get_element_text(edit_box, 0)")
    offset = shared_reader.index("+ DLUMA_UI_EDIT_TEXT_OFFSET", accessor)
    fallback = shared_reader[accessor:offset]
    assert 'GetModuleHandleW(L"engine_x64_rwdi.dll")' not in fallback
    assert 'GetModuleHandleW(L"gamedll_x64_rwdi.dll")' in fallback
    assert accessor < offset
    settings_reader = source[
        source.index("static int dluma_ui_settings_get_edit_text") :
        source.index("static const char *const g_dluma_ui_logs_category_keys")
    ]
    assert "dluma_ui_read_edit_box_text(" in settings_reader
    assert "g_dluma_ui_settings_edit_box" in settings_reader
    assert "loader_settings_edit_text_failure_stage" in source
    assert "loader_settings_save_failure_stage" in source
    assert "text->capacity < text->size" in source
    assert "text->data[text->size] != 0" in source
    assert "WritePrivateProfileStringW(" in source
    assert "definition->section, definition->key, value_wide, g_dluma.config_path" in source
    assert "apply after restart" in source.lower()
    save = source[
        source.index("static int dluma_ui_settings_save_editor") :
        source.index("static int dluma_ui_settings_quick_change")
    ]
    assert "dluma_ui_settings_hide_editor();" in save
    assert "dluma_ui_settings_populate_list(screen);" in save
    assert "SAVED. THE NEW VALUE WILL APPLY AFTER RESTARTING THE LOADER." in save
    assert "CreateFileW" not in source[
        source.index("static int dluma_ui_settings_write_value") :
        source.index("static int dluma_ui_settings_parse_uint")
    ]


def test_content_pack_builder_rejects_an_xui_entry_that_differs_from_source(
    tmp_path,
):
    pack = tmp_path / "dluma.pak"
    build_content_pack.build(pack)
    rewritten = tmp_path / "rewritten.pak"
    settings_entry = "data/menu/scr/MenuDLUMALoaderSettings.xui"
    with zipfile.ZipFile(pack) as source, zipfile.ZipFile(
        rewritten, "w", compression=zipfile.ZIP_DEFLATED
    ) as destination:
        for info in source.infolist():
            data = source.read(info.filename)
            if info.filename == settings_entry:
                data += b"\r\n"
            destination.writestr(info, data)

    with pytest.raises(ValueError, match="content pack source mismatch"):
        build_content_pack.verify_archive_sources(rewritten)


def test_loader_settings_has_unique_native_3d_placement_entry():
    text = (
        ROOT
        / "PakAssets"
        / "camera"
        / "menuscreen3dplacement_menu_lvl_new.scr"
    ).read_text()
    assert 'Placement("MenuDLUMALoaderSettings", "menu_options_full_pos")' in text
    assert 'Correction("MenuDLUMALoaderSettings", -100, -20, 0)' in text
    for stock_entry in (
        'Placement("MenuMain", "menu_main_pos")',
        'Placement("MenuOptionsControlsKeyboard", "menu_options_full_pos")',
        'Placement("MenuOptionsEyeTracking", "menu_options_full_pos")',
    ):
        assert stock_entry in text

    settings = SETTINGS_PATH.read_text()
    assert "<Id>MenuDLUMALoaderSettings</Id>" in settings
    assert "<Id>MenuOptionsControlsKeyboard</Id>" not in settings
