from pathlib import Path
import xml.etree.ElementTree as ET


ROOT = Path(__file__).resolve().parents[1]
HOST_PATH = ROOT / "src" / "dlw_host.c"
UI_PATH = ROOT / "src" / "dlw_ui.c"
SETTINGS_XUI_PATH = ROOT / "PakAssets" / "XUI" / "MenuDLUMALoaderSettings.xui"
SAMPLE_INI_PATH = ROOT / "config" / "dluma.ini"
LOCALE_ROOT = ROOT / "PakAssets" / "Locale"

WARNING_KEYS = (
    "DLUMA_Settings_GameFixes_CustomMapAssetIsolation_ExperimentalTitle",
    "DLUMA_Settings_GameFixes_CustomMapAssetIsolation_ExperimentalBody",
    "DLUMA_Settings_GameFixes_CustomMapAssetIsolation_ExperimentalYes",
    "DLUMA_Settings_GameFixes_CustomMapAssetIsolation_ExperimentalNo",
    "DLUMA_Settings_GameFixes_CustomMapAssetIsolation_ExperimentalUnavailable",
    "DLUMA_Settings_GameFixes_CustomMapAssetIsolation_ExperimentalKeptOff",
    "DLUMA_Settings_GameFixes_CustomMapAssetIsolation_ExperimentalEnabled",
)


def _function(source: str, start: str, end: str) -> str:
    start_index = source.index(start)
    return source[start_index : source.index(end, start_index)]


def _xui_control(root, object_id):
    return next(e for e in root.iter() if e.findtext("Properties/Id") == object_id)


def test_project_resource_unload_defaults_off_in_each_shipped_contract():
    host = HOST_PATH.read_text(encoding="utf-8")
    ui = UI_PATH.read_text(encoding="utf-8")
    ini = SAMPLE_INI_PATH.read_text(encoding="utf-8")
    table = ui[
        ui.index("static const DlumaLoaderSettingDefinition g_dluma_loader_settings") : ui.index(
            "g_dluma_loader_category_names"
        )
    ]

    assert 'L"GameFixes", L"CustomMapAssetIsolation", 0' in host
    assert 'L"GameFixes", L"CustomMapAssetIsolation", L"0"' in table
    assert ini.count("CustomMapAssetIsolation=0") == 1
    assert "CustomMapAssetIsolation=1" not in ini
    assert "Experimental project-resource unload." in ini


def test_warning_overlay_reuses_stock_dialog_layout_and_direct_actions():
    root = ET.parse(SETTINGS_XUI_PATH).getroot()
    popup = _xui_control(root, "G_SettingsExperimentalWarningPopup")
    ids = [e.text for e in popup.findall(".//Properties/Id")]
    assert len(ids) == len(set(ids))
    assert popup.findtext("Properties/Show") == "false"
    assert popup.findtext("Properties/UseScreenTransform") == "false"
    window = _xui_control(popup, "G_SettingsExperimentalWarningWindow")
    assert window.findtext("Properties/Width") == "550.000000"
    assert window.findtext("Properties/Anchor") == "48"
    assert popup.find(".//ImagePath[.='dialog_icon_warning']") is not None
    assert popup.find(".//Visual[.='ButtonMainV']") is None
    assert popup.find(".//ImagePath[.='menu_bg']") is None
    for suffix in ("Title", "Body"):
        text = _xui_control(popup, "T_SettingsExperimentalWarning" + suffix)
        assert text.findtext("Properties/MultiLine") == "true"
        assert text.findtext("Properties/AutoSizeToText") == "true"
        parent = next(e for e in popup.iter() if text in list(e))
        assert parent.findtext("Properties/ClassOverride") == "UIStackPanel"
        assert parent.findtext("Properties/AutoSizeToContentY") == "true"
        assert parent.findtext("Properties/AutoSizeParentY") == "true"
    for suffix, key in (("Yes", "22592"), ("No", "22593")):
        button = _xui_control(popup, "B_SettingsExperimentalWarning" + suffix)
        assert button.tag == "AdvButton"
        assert button.findtext("Properties/ClassOverride") == "UIDialogButton"
        assert button.findtext("Properties/Visual") == "ButtonDialogV"
        assert button.findtext("Properties/PressKey") == key
        assert button.findtext("Properties/UnfocusedInput") == "true"
        assert button.findtext("Properties/CanFocus") == "false"
        assert button.findtext("Properties/WidthAdjust") == "22"
        assert button.find("Properties/NavLeft") is None
    no = _xui_control(popup, "B_SettingsExperimentalWarningNo")
    assert no.findtext("Properties/Anchor") == "4"
    assert no.findtext("Properties/Position").startswith("506.")


def test_opening_input_cannot_also_confirm_and_dialog_does_not_force_focus():
    source = UI_PATH.read_text(encoding="utf-8")
    show = _function(source, "static int dluma_ui_settings_show_experimental_warning",
                     "static void dluma_ui_settings_cancel_experimental_warning")
    confirm = _function(source, "static int dluma_ui_settings_confirm_experimental_warning",
                        "static int dluma_ui_settings_is_custom_map_asset_isolation")
    assert "g_dluma_ui_settings_experimental_warning_open_generation =" in show
    assert "g_dluma_ui_settings_post_generation;" in show
    assert "dluma_ui_focus_element(g_dluma_ui_settings_experimental_warning_no_button)" not in show
    assert "g_dluma_set_element_can_focus" not in show
    guard = confirm.index("g_dluma_ui_settings_experimental_warning_open_generation ==")
    assert "g_dluma_ui_settings_post_generation)\n        return 0;" in confirm
    assert guard < confirm.index("dluma_ui_settings_close_experimental_warning(0)")
    assert guard < confirm.index('dluma_ui_settings_write_value(setting_index, "1")')
    post = _function(source, "static void __fastcall dluma_ui_workshop_post_hook",
                      "static void __fastcall dluma_ui_workshop_show_hook")
    settings = post[post.index("dluma_ui_is_loader_settings_screen(screen)"):]
    assert settings.index("++g_dluma_ui_settings_post_generation;") < settings.index("g_dluma_base_main_menu_post(screen);")


def test_warning_strings_are_present_once_in_all_shipped_locale_catalogs():
    catalogs = sorted(LOCALE_ROOT.glob("*/loader_texts_all.scr"))
    assert [catalog.parent.name for catalog in catalogs] == [
        "Br",
        "Cn",
        "Cs",
        "De",
        "El",
        "En",
        "Es",
        "Fr",
        "It",
        "Jp",
        "Ko",
        "Nl",
        "Pl",
        "Ru",
        "Th",
        "Tr",
        "Tw",
    ]
    for catalog in catalogs:
        text = catalog.read_text(encoding="utf-8")
        assert "DLUMA_Settings_GameFixes_CustomMapAssetIsolation_Hint" in text
        for key in WARNING_KEYS:
            assert text.count(f'String("{key}", ') == 1, catalog


def test_all_enable_routes_use_one_off_to_on_confirmation_gate():
    source = UI_PATH.read_text(encoding="utf-8")
    request = _function(
        source,
        "static DlumaSettingsWriteRequestResult dluma_ui_settings_request_write",
        "static int dluma_ui_copy_edit_text",
    )
    save = _function(
        source,
        "static int dluma_ui_settings_save_editor",
        "static int dluma_ui_settings_change_direction",
    )
    change = _function(
        source,
        "static int dluma_ui_settings_change_direction",
        "static int dluma_ui_settings_quick_change",
    )
    quick = _function(
        source,
        "static int dluma_ui_settings_quick_change",
        "static int dluma_ui_mod_config_stage_selected",
    )
    option_hook = _function(
        source,
        "static void __fastcall dluma_ui_workshop_option_selected_hook",
        "static void dluma_ui_registry_log_toggle",
    )

    assert "dluma_ui_settings_should_confirm_custom_map_asset_isolation" in request
    assert "DLUMA_SETTINGS_WRITE_PROMPTED" in request
    assert "DLUMA_SETTINGS_WRITE_REJECTED" in request
    assert request.count("dluma_ui_settings_write_value(") == 1
    assert "dluma_ui_settings_request_write(screen, index, value)" in save
    assert "dluma_ui_settings_write_value(index, value)" not in save
    assert "dluma_ui_settings_request_write(screen, index, next_text)" in change
    assert "dluma_ui_settings_write_value(index, next_text)" not in change
    assert "dluma_ui_settings_change_direction(\n        screen, index, direction, 0);" in quick
    settings_option = option_hook[
        option_hook.index("dluma_ui_is_loader_settings_screen(screen) && option_control") :
    ]
    assert "dluma_ui_settings_change_direction(" in settings_option
    assert "mapping->setting_index, direction, 1" in settings_option


def test_cancel_and_missing_overlay_fail_closed_and_cleanup_modal_state():
    source = UI_PATH.read_text(encoding="utf-8")
    close = _function(
        source,
        "static void dluma_ui_settings_close_experimental_warning",
        "static int dluma_ui_settings_show_experimental_warning",
    )
    show = _function(
        source,
        "static int dluma_ui_settings_show_experimental_warning",
        "static void dluma_ui_settings_cancel_experimental_warning",
    )
    cancel = _function(
        source,
        "static void dluma_ui_settings_cancel_experimental_warning",
        "static int dluma_ui_settings_confirm_experimental_warning",
    )
    button = _function(
        source,
        "static void __fastcall dluma_ui_workshop_button_hook",
        "static void __fastcall dluma_ui_workshop_double_hook",
    )
    show_hook = _function(
        source,
        "static void __fastcall dluma_ui_workshop_show_hook",
        "static void __fastcall dluma_ui_workshop_selection_hook",
    )

    assert "g_dluma_ui_settings_experimental_warning_mode = 0;" in close
    assert "g_dluma_ui_settings_experimental_warning_index = 0xFFFFFFFFUL;" in close
    assert "dluma_ui_settings_set_main_controls_visible(1);" in close
    assert "!dluma_ui_settings_experimental_warning_available()" in show
    assert "dluma_ui_settings_close_editor(0);" in show
    assert "DLUMA_SETTINGS_WRITE_REJECTED" in _function(
        source,
        "static DlumaSettingsWriteRequestResult dluma_ui_settings_request_write",
        "static int dluma_ui_copy_edit_text",
    )
    assert "dluma_ui_settings_write_value" not in cancel
    assert "dluma_ui_settings_populate_list(screen);" in cancel
    assert button.index("g_dluma_ui_settings_experimental_warning_mode") < button.index(
        "g_dluma_ui_settings_edit_mode"
    )
    assert '"B_SettingsExperimentalWarningNo"' in button
    assert 'dluma_ui_object_id_equals(button, "B_Back")' in button
    assert "dluma_ui_settings_cancel_experimental_warning(screen);" in button
    assert "dluma_ui_settings_close_experimental_warning(0);" in show_hook
