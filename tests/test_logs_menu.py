from __future__ import annotations

import importlib.util
from pathlib import Path
import re
import subprocess
import xml.etree.ElementTree as ET
import zipfile

import pytest


ROOT = Path(__file__).resolve().parents[1]
EXTRAS = ROOT / "PakAssets" / "XUI" / "DLUMA_MenuExtras.xui"
LOGS = ROOT / "PakAssets" / "XUI" / "MenuDLUMALogs.xui"
CAMERA = (
    ROOT / "PakAssets" / "Camera"
    / "menubackgroundcameraactions_menu_lvl_new.scr"
)
PLACEMENT = (
    ROOT / "PakAssets" / "Camera"
    / "menuscreen3dplacement_menu_lvl_new.scr"
)
LOCALE_CODES = (
    "En", "De", "Fr", "It", "Es", "Ru", "Jp", "Pl", "Nl",
    "Br", "Ko", "Cn", "Tw", "El", "Tr", "Th", "Cs",
)
LOG_KEYS = (
    "DLUMA_Menu_Logs",
    "DLUMA_Logs_Category_All",
    "DLUMA_Logs_Category_Info",
    "DLUMA_Logs_Category_Warnings",
    "DLUMA_Logs_Category_Errors",
    "DLUMA_Logs_Category_Debug",
    "DLUMA_Logs_Search",
    "DLUMA_Logs_SearchLabel",
    "DLUMA_Logs_SearchEditing",
    "DLUMA_Logs_Details",
    "DLUMA_Logs_Empty",
    "DLUMA_Logs_SearchUnavailable",
    "DLUMA_Logs_StatusEmpty",
    "DLUMA_Logs_Status",
)


def source(relative: str) -> str:
    return (ROOT / relative).read_text(encoding="utf-8")


def block(text: str, start: str, end: str) -> str:
    return text.split(start, 1)[1].split(end, 1)[0]


def load_pack_builder():
    path = ROOT / "tools" / "build_content_pack.py"
    spec = importlib.util.spec_from_file_location("logs_pack_builder", path)
    assert spec and spec.loader
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def load_localization_generator():
    path = ROOT / "tools" / "generate_localization_catalogs.py"
    spec = importlib.util.spec_from_file_location("logs_localization_generator", path)
    assert spec and spec.loader
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def test_logs_xuis_are_engine_safe_and_have_the_native_contract():
    for path in (EXTRAS, LOGS):
        data = path.read_bytes()
        assert not data.startswith(b"\xef\xbb\xbf")
        assert b"<!--" not in data
        ET.fromstring(data.decode("utf-8"))

    extras = EXTRAS.read_text(encoding="utf-8")
    assert "<Id>MenuExtras</Id>" in extras
    assert "<ClassOverride>MenuExtras</ClassOverride>" in extras
    stock_ids = (
        "G_MenuGroup", "G_Buttons", "B_PatchMovie",
        "B_DownloadContent", "B_JoinLive", "B_RedeemCode", "B_Store",
        "B_Leaderboards", "B_PersonalStats", "B_Credits",
    )
    for object_id in stock_ids:
        assert extras.count(f"<Id>{object_id}</Id>") == 1
    route_position = extras.index("<Id>B_DLUMALogs</Id>")
    route = extras[
        extras.rfind("<AdvButton>", 0, route_position) :
        extras.index("</AdvButton>", route_position)
    ]
    assert "<AdvButton>" in route
    assert "<ClassOverride>UiButtonMainDi</ClassOverride>" in route
    assert "<PressPath>" not in route
    assert "<Text>&amp;DLUMA_Menu_Logs&amp;</Text>" in route
    assert route_position > extras.index("<Id>B_Credits</Id>")

    logs = LOGS.read_text(encoding="utf-8")
    assert "<Id>MenuDLUMALogs</Id>" in logs
    assert "<ClassOverride>MenuSteamWorkshop</ClassOverride>" in logs
    for object_id in (
        "G_MenuGroup", "L_Maps", "B_PrevScreenMode",
        "B_NextScreenMode", "T_CurrentMode", "E_LogsSearch", "B_Search",
        "B_ApplySearch",
        "G_LogsRowMessages", "T_LogRow0Message", "T_LogRow1Message",
        "T_LogRow2Message", "T_LogRow3Message", "T_LogRow4Message",
        "T_LogRow5Message", "T_LogRow6Message", "T_LogRow7Message",
        "T_LogRow8Message", "I_LogsListRail",
        "T_LogsDetailSeverity", "T_LogsDetail", "T_LogsStatus",
        "B_Start", "B_Workshop", "B_Back",
    ):
        assert logs.count(f"<Id>{object_id}</Id>") == 1
    assert "<Visual>ListBoxOptionsV</Visual>" in logs
    assert "<MaxChars>128</MaxChars>" in logs
    list_properties = block(logs, "<Id>L_Maps</Id>", "</Properties>")
    assert "<NavUp>L_Maps</NavUp>" in list_properties
    assert "<NavDown>L_Maps</NavDown>" in list_properties
    assert "<FocusSetSelection>true</FocusSetSelection>" in list_properties
    edit_properties = block(logs, "<Id>E_LogsSearch</Id>", "</Properties>")
    assert "Enter text here..." not in edit_properties
    assert "<CanFocus>false</CanFocus>" in edit_properties
    assert logs.index("<Id>G_LogsRowMessages</Id>") > logs.index("<Id>L_Maps</Id>")
    for row in range(9):
        row_properties = block(
            logs, f"<Id>T_LogRow{row}Message</Id>", "</Properties>"
        )
        assert "<Show>false</Show>" in row_properties
    detail_message = block(logs, "<Id>T_LogsDetail</Id>", "</Properties>")
    assert "<ClassOverride>UISmartText</ClassOverride>" not in detail_message
    search = logs[
        logs.index("<Id>B_Search</Id>") :
        logs.index("</Properties>", logs.index("<Id>B_Search</Id>"))
    ]
    assert "<Show>false</Show>" not in search
    assert "<CanFocus>false</CanFocus>" not in search
    accept = block(logs, "<Id>B_ApplySearch</Id>", "</Properties>")
    assert "<Position>-4096.000000,-4096.000000,0.000000</Position>" in accept
    assert "<Show>false</Show>" in accept
    assert "<CanFocus>false</CanFocus>" in accept
    assert "<UnfocusedInput>true</UnfocusedInput>" in accept
    assert "<PressKey>22528</PressKey>" in accept
    for hidden in ("B_Start", "B_Workshop"):
        hidden_block = logs[
            logs.index(f"<Id>{hidden}</Id>") :
            logs.index("</Properties>", logs.index(f"<Id>{hidden}</Id>"))
        ]
        assert "<Show>false</Show>" in hidden_block
        assert "<CanFocus>false</CanFocus>" in hidden_block


def test_logs_packager_does_not_reject_valid_cosmetic_layout_edits():
    packager = source("tools/build_content_pack.py")
    logs_gate = packager[
        packager.index("if '<!--' in logs_text") :
        packager.index("if '<Id>MenuDLUMAModConfig</Id>'")
    ]
    for brittle_layout_check in (
        "'<Width>852.000000</Width>' not in list_block",
        "'<Height>355.000000</Height>' not in list_block",
        "'<Position>24.000000,106.000000,0.000000</Position>' not in list_block",
        "expected_position = f'<Position>",
        "'<Width>600.000000</Width>' not in row_block",
        "'<TextStyle>16</TextStyle>' not in row_block",
        "'<HorizontalAlign>left</HorizontalAlign>' not in row_block",
        "'<VerticalAlign>middle</VerticalAlign>' not in row_block",
    ):
        assert brittle_layout_check not in logs_gate


def test_logs_runtime_uses_bounded_history_and_exact_root_dispatch():
    history_header = source("src/dlw_log_history_core.h")
    menu_header = source("src/dlw_logs_menu_core.h")
    logger = source("src/dlw_log.c")
    ui = source("src/dlw_ui.c")
    assert "DLUMA_LOG_HISTORY_CAPACITY 1024U" in history_header
    assert "char message[DLUMA_LOG_RECORD_CAPACITY]" in history_header
    assert "DLUMA_LOGS_QUERY_CAPACITY 129U" in menu_header
    assert "DLUMA_LOGS_MAX_RESULTS 256U" in menu_header
    assert "DLUMA_LOGS_ROW_MESSAGE_LIMIT 68U" in menu_header
    assert "dluma_log_history_store_publish" in block(
        logger,
        "static unsigned int dluma_log_dequeue_batch",
        "static unsigned int dluma_log_append_drop_warning",
    )
    drop = block(
        logger,
        "static unsigned int dluma_log_append_drop_warning",
        "static DWORD WINAPI dluma_log_writer_thread",
    )
    assert '"WARN"' in drop
    assert "dluma_log_history_store_publish" in drop
    emergency = block(logger, "void dluma_log_emergency", "void dluma_log_line")
    assert "dluma_log_history" not in emergency
    assert "dluma_log_history_store_start_session" in block(
        logger, "int dluma_log_start", "int dluma_log_stop"
    )
    assert "dluma_log_history_store_reset" in block(
        logger, "int dluma_log_stop", "void dluma_log_emergency"
    )

    registration = block(
        ui,
        "static int dluma_ui_register_logs_screen",
        "static int dluma_ui_register_quest_log_screen",
    )
    assert 'script_path_text[] = "MenuDLUMALogs.xui"' in registration
    assert "level, &script_path, 2, 1, 0" in registration
    assert '"MenuDLUMALogs"' in registration
    assert "DLUMA_DEVTOOLS_MENU_EXTRAS_VTABLE0_RVA 0x016AD2F8UL" in ui
    assert "DLUMA_DEVTOOLS_MENU_EXTRAS_BUTTON_FN_RVA 0x007FF7B0UL" in ui
    assert "DLUMA_DEVTOOLS_MENU_EXTRAS_BUTTON_SLOT_RVA 0x016ADCE0UL" in ui
    extras_hook = block(
        ui,
        "static void __fastcall dluma_ui_extras_button_hook",
        "static int dluma_ui_install_extras_button_hook",
    )
    assert 'dluma_ui_object_id_equals(button, "B_DLUMALogs")' in extras_hook
    assert "dluma_ui_route_logs_button_by_id(menu_extras, button)" in extras_hook
    assert "original(menu_extras, button, event_code)" in extras_hook
    logs_route = block(
        ui,
        "static int dluma_ui_route_logs_button_by_id",
        "static void __fastcall dluma_ui_workshop_button_hook",
    )
    assert "dluma_ui_register_logs_screen" in logs_route
    assert "dluma_ui_show_next_registered_screen_from_menu_extras" in logs_route
    assert "ShowUIScreen" not in logs_route and "LoadUIScreen" not in logs_route
    assert "DLUMA_UI_LOGS_REFRESH_MS 250UL" in ui
    assert "DLUMA_UI_LOGS_VISIBLE_ROWS 9U" in ui
    assert "DLUMA_LOGS_ROW_MESSAGE_LIMIT 68U" in source(
        "src/dlw_logs_menu_core.h"
    )
    assert "revision != g_dluma_ui_logs_last_revision" in ui
    assert "dluma_ui_is_logs_screen(screen)" in ui
    assert (
        "MenuDLUMALogs lifecycle entered; native Workshop enumeration and map "
        "activation are suppressed."
    ) in ui
    button_hook_start = ui.index(
        "static void __fastcall dluma_ui_workshop_button_hook"
    )
    logs_button_start = ui.index(
        "dluma_ui_is_logs_screen(screen)", button_hook_start
    )
    logs_button = ui[
        logs_button_start : ui.index(
            "else if (!g_dluma_registry_hooks_stopping",
            logs_button_start + 1,
        )
    ]
    assert '"B_Start"' in logs_button and '"B_Workshop"' in logs_button
    assert "original(screen, button, event_code)" in logs_button
    assert "dluma_workshop_menu_activate" not in logs_button
    search_action = block(
        logs_button,
        'dluma_ui_object_id_equals(button, "B_Search")',
        'dluma_ui_object_id_equals(button, "B_ApplySearch")',
    )
    assert "dluma_ui_logs_set_search_edit_mode(screen, 1)" in search_action
    assert "dluma_ui_logs_apply_search" not in search_action
    accept_action = block(
        logs_button,
        'dluma_ui_object_id_equals(button, "B_ApplySearch")',
        'dluma_ui_object_id_equals(button, "B_Back")',
    )
    assert "g_dluma_ui_logs_search_edit_mode" in accept_action
    assert "dluma_ui_logs_apply_search(screen)" in accept_action
    assert "dluma_ui_logs_set_search_edit_mode(screen, 0)" in accept_action
    back_action = block(
        logs_button,
        'dluma_ui_object_id_equals(button, "B_Back")',
        'dluma_ui_object_id_equals(button, "B_Start")',
    )
    assert "g_dluma_ui_logs_search_edit_mode" in back_action
    assert "g_dluma_ui_logs_query" in back_action
    assert "dluma_ui_logs_set_search_edit_mode(screen, 0)" in back_action
    assert "original(screen, button, event_code)" in back_action

    edit_mode = block(
        ui,
        "static void dluma_ui_logs_set_search_edit_mode",
        "static int dluma_ui_settings_save_editor",
    )
    assert "dluma_ui_focus_element(g_dluma_ui_logs_search_box)" in edit_mode
    assert "dluma_ui_focus_element(g_dluma_ui_logs_list)" in edit_mode
    assert "dluma_ui_workshop_set_focus" in edit_mode
    assert "g_dluma_ui_logs_search_accept_button" in edit_mode
    assert '"DLUMA_Logs_SearchEditing"' in ui


def test_search_failure_retains_query_and_selected_detail_is_complete():
    ui = source("src/dlw_ui.c")
    search = block(
        ui,
        "static int dluma_ui_logs_apply_search",
        "static int dluma_ui_settings_save_editor",
    )
    failed = search.split("return 0;", 1)[0]
    assert "g_dluma_ui_logs_search_unavailable = 1" in failed
    assert "g_dluma_ui_logs_query" not in failed
    assert search.index("g_dluma_ui_logs_query") > search.index("return 0;")
    assert "query[DLUMA_LOGS_QUERY_CAPACITY - 1U] = 0" in search

    reader = block(
        ui,
        "static int dluma_ui_read_edit_box_text",
        "static int dluma_ui_settings_get_edit_text",
    )
    assert "DLUMA_UI_EDIT_TEXT_OFFSET 0x00A0UL" in ui
    assert "DLUMA_UI_EDIT_TEXT_OFFSET" in reader
    assert "+0xA0" in reader and "+0xA8" in reader
    assert "gamedll_fingerprint_mismatch" in reader
    assert "edit_box_vtable_not_in_gamedll" in reader
    assert "presenter_is_complete" in reader
    assert "UIEditBox text reading is unsupported" in reader
    assert "dluma_ui_settings_get_edit_text" in ui
    assert "dluma_ui_logs_apply_search" in ui

    detail = block(
        ui,
        "static void dluma_ui_logs_update_detail",
        "static int dluma_ui_logs_select_pointer_item",
    )
    assert "g_dluma_ui_logs_detail_severity" in detail
    assert "entry->level" in detail
    assert "entry->message" in detail
    assert "dluma_ui_logs_set_detail_colors(entry->severity)" in detail
    assert "dluma_logs_format_row" not in detail


def test_logs_rows_use_fixed_columns_and_severity_colors():
    ui = source("src/dlw_ui.c")
    patched = source("src/dlw_ui_patched.c")
    menu = source("src/dlw_logs_menu_core.c")
    add_row = block(
        ui,
        "static void *dluma_ui_logs_add_row",
        "static void dluma_ui_logs_update_detail",
    )
    assert 'option_template_text[] = "OptionTextDummy"' in add_row
    assert "dluma_ui_set_element_visible(value_wrap, 0)" in add_row
    assert "dluma_ui_set_element_text_association(item, message, 1)" not in add_row
    assert "g_dluma_set_element_text_color(item, &header_color, 0)" in add_row
    populate = block(
        ui,
        "static void dluma_ui_logs_populate_list",
        "static void dluma_ui_logs_refresh_if_needed",
    )
    assert "dluma_logs_format_row_header" in populate
    assert "dluma_logs_format_row_message" not in populate
    assert "dluma_logs_format_row(" not in populate
    present = block(
        patched,
        "static void dluma_ui_fix_present_logs_messages",
        "static void __fastcall dluma_ui_workshop_show_hook",
    )
    assert "dluma_ui_workshop_get_first_visible" in present
    assert "dluma_logs_format_row_message" in present
    assert "dluma_ui_set_element_visible(value_wrap, 0)" in present
    assert "g_dluma_set_element_text_color(" in present
    assert "mapping->item, &header_color, 0" in present
    assert "g_dluma_set_text_element_color(" in present
    assert "header_text, &header_rgb" in present
    assert "g_dluma_set_text_element_color(control, &message_rgb)" in present
    assert "compact_index < DLUMA_LOGS_ROW_MESSAGE_LIMIT" in menu
    assert "?SetTextColor@IUIElementControl@@QEAAXAEBVvec4@@H@Z" in ui
    assert "?SetTextColor@IUIText@@QEAAXAEBVvec3@@@Z" in ui


def test_logs_keyboard_navigation_is_focused_stable_and_non_wrapping():
    ui = source("src/dlw_ui.c")
    patched = source("src/dlw_ui_patched.c")
    navigation = block(
        patched,
        "static void dluma_ui_fix_sync_logs_navigation",
        "static void dluma_ui_fix_hide_registry_columns",
    )
    assert "g_dluma_ui_fix_logs_initial_focus_pending" in navigation
    assert "dluma_ui_list_set_selection(g_dluma_ui_logs_list, 0)" in navigation
    assert "dluma_ui_workshop_set_focus(" in navigation
    assert "dluma_ui_focus_element(g_dluma_ui_logs_list)" in navigation
    assert "previous == 0" in navigation
    assert "navigation_candidate == count - 1" in navigation
    assert "previous == count - 1" in navigation
    assert "navigation_candidate == 0" in navigation
    assert "dluma_ui_logs_update_detail((unsigned int)target)" in navigation
    assert "dluma_ui_workshop_set_first_visible(" in navigation

    post = block(
        patched,
        "static void __fastcall dluma_ui_workshop_post_hook(void *screen)\n{\n",
        "static VOID CALLBACK dluma_ui_fix_timer_proc",
    )
    base_post = post.index("dluma_ui_workshop_post_hook_base(screen)")
    post_sync = post.index("dluma_ui_fix_sync_logs_navigation(1)")
    presentation = post.index("dluma_ui_fix_present_logs_messages(screen)")
    assert base_post < post_sync < presentation
    logs_post = block(
        post,
        "else if (dluma_ui_is_logs_screen(screen))",
        "else if (dluma_ui_is_stock_workshop_screen(screen))",
    )
    assert "dluma_ui_fix_sync_logs_navigation(1)" in logs_post
    assert "dluma_ui_fix_set_capacity" not in logs_post

    populate = block(
        ui,
        "static void dluma_ui_logs_populate_list",
        "static void dluma_ui_logs_refresh_if_needed",
    )
    assert "previous_first_sequence" in populate
    assert "dluma_ui_workshop_get_first_visible(" in populate
    assert "restore_first_visible" in populate
    assert "dluma_ui_workshop_set_first_visible(" in populate


def test_logs_localization_is_complete_in_all_catalogs():
    for code in LOCALE_CODES:
        text = source(f"PakAssets/Locale/{code}/loader_texts_all.scr")
        for key in LOG_KEYS:
            assert len(re.findall(rf'^String\("{re.escape(key)}",', text,
                                  re.MULTILINE)) == 1
        status = re.search(
            r'^String\("DLUMA_Logs_Status", "(.*)"\)$', text, re.MULTILINE
        )
        assert status
        assert all(token in status.group(1) for token in (
            "{shown}", "{matches}", "{retained}"
        ))

    generator = source("tools/generate_localization_catalogs.py")
    assert "LOG_KEYS" in generator
    assert "LOG_TEXT" in generator
    assert '"--logs-only"' in generator


def test_logs_localization_backfill_is_narrow_and_idempotent(tmp_path):
    generator = load_localization_generator()
    locale_root = tmp_path / "Locale"
    catalog = locale_root / "De" / "loader_texts_all.scr"
    catalog.parent.mkdir(parents=True)
    original = "\r\n".join(
        f'String("{key}", "existing")'
        for key in generator.LOG_KEYS
        if key != "DLUMA_Logs_SearchEditing"
    ) + "\r\n"
    catalog.write_text(original, encoding="utf-8", newline="")
    generator.LOCALES = ("De",)
    generator.LOCALE_ROOT = locale_root

    assert generator.sync_log_catalogs() == 0
    repaired = catalog.read_bytes()
    assert repaired.count(b'String("DLUMA_Logs_SearchEditing",') == 1
    assert generator.sync_log_catalogs() == 0
    assert catalog.read_bytes() == repaired

    damaged = catalog.read_text(encoding="utf-8").replace(
        'String("DLUMA_Logs_Search", "existing")\n', ""
    ).replace(
        'String("DLUMA_Logs_SearchLabel", "existing")\n', ""
    )
    catalog.write_text(damaged, encoding="utf-8", newline="")
    with pytest.raises(ValueError, match="partial Logs localization block"):
        generator.sync_log_catalogs()


def test_camera_and_custom_extras_contract_preserve_native_history():
    camera = CAMERA.read_text(encoding="utf-8")
    placement = PLACEMENT.read_text(encoding="utf-8")
    assert 'MenuWithCameraActions("MenuDLUMALogs")' in camera
    assert not any(
        "CameraAction(" in line and "MenuDLUMALogs" in line
        for line in camera.splitlines()
    )
    assert 'Placement("MenuDLUMALogs", "menu_extras_pos")' in placement
    assert 'Correction("MenuDLUMALogs",' not in placement

    readme = source("PakAssets/XUI/README.md")
    assert "B_DLUMALogs" in readme
    assert "MenuDLUMALogs.xui" in readme
    assert "&DLUMA_Menu_Logs&" in readme
    assert "custom-menu overlay keeps normal precedence" in readme
    assert "C:\\" not in readme and "X:\\" not in readme and "Y:\\" not in readme


def test_logs_fake_harness():
    result = subprocess.run(
        ["cmd.exe", "/c", "tools\\build_log_history_fake_tests.bat"],
        cwd=ROOT,
        capture_output=True,
        text=True,
        timeout=120,
        check=False,
    )
    assert result.returncode == 0, result.stdout + result.stderr
    assert "log_history_fake_harness: PASS" in result.stdout


def test_pack_contains_exact_logs_and_intentional_extras_override(tmp_path):
    builder = load_pack_builder()
    output = tmp_path / "logs-content.pak"
    builder.build(output)
    with zipfile.ZipFile(output) as archive:
        names = {name.replace("\\", "/") for name in archive.namelist()}
        assert archive.read("data/menu/scr/MenuExtras.xui") == EXTRAS.read_bytes()
        assert archive.read("data/menu/scr/MenuDLUMALogs.xui") == LOGS.read_bytes()
    assert "data/menu/scr/MenuExtras.xui" in names
    assert "data/menu/scr/MenuDLUMALogs.xui" in names
    assert "data/menu/scr/MenuMain_PC_workshop.xui" not in names
    assert "data/menu/scr/MenuSteamWorkshop.xui" not in names
