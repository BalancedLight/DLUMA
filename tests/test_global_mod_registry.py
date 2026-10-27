from pathlib import Path
import re
import zipfile


ROOT = Path(__file__).resolve().parents[1]
COMMON = (ROOT / "src" / "dlw_common.h").read_text(encoding="utf-8")
DISCOVERY = (ROOT / "src" / "dlw_lua_discovery.c").read_text(encoding="utf-8")
CONSENT = (ROOT / "src" / "dlw_global_consent.c").read_text(encoding="utf-8")
UI = (ROOT / "src" / "dlw_ui.c").read_text(encoding="utf-8")
XUI_PATH = ROOT / "PakAssets" / "XUI" / "MenuDLUMAMods.xui"
XUI = XUI_PATH.read_text(encoding="ascii")
DETAILS_XUI_PATH = ROOT / "PakAssets" / "XUI" / "MenuDLUMAModDetails.xui"
DETAILS_XUI = DETAILS_XUI_PATH.read_text(encoding="ascii")


def c_block(source: str, start: str, end: str) -> str:
    begin = source.index(start)
    finish = source.index(end, begin)
    return source[begin:finish]


def test_01_global_author_default_never_bypasses_exact_approval():
    classify = c_block(
        CONSENT,
        "void dluma_lua_classify_global_review",
        "static int dluma_ini_buffer_append",
    )
    assert "item->enabled = 0" in classify
    assert "Author preference is display-only until exact player approval" in classify
    assert "item->approval_valid" in classify
    assert "item->enabled = 1" in classify


def test_02_explicit_enabled_override_is_recognized():
    classify = c_block(
        CONSENT,
        "void dluma_lua_classify_global_review",
        "static int dluma_ini_buffer_append",
    )
    assert 'dluma_global_ini_read("global_mods", item->registry_key' in classify
    assert 'dluma_global_ini_read("global_mods", item->id' in classify
    assert "They never create approval for a fingerprint" in classify
    assert "item->has_enabled_override = 1" in classify


def test_03_explicit_disabled_override_is_recognized():
    parser = c_block(
        CONSENT,
        "static int dluma_global_parse_enabled",
        "static int dluma_global_permission_contains",
    )
    assert 'dluma_stricmp(value, "0") != 0' in parser
    assert 'dluma_stricmp(value, "false") != 0' in parser
    assert 'dluma_stricmp(value, "off") != 0' in parser
    assert 'dluma_stricmp(value, "disabled") != 0' in parser


def test_04_duplicate_ids_are_disabled_and_not_toggleable():
    discover = c_block(
        DISCOVERY,
        "void dluma_lua_discover_globals(void)",
        "unsigned int dluma_lua_discover_locals",
    )
    request = c_block(
        CONSENT,
        "int dluma_lua_request_global_enabled",
        "void dluma_lua_process_global_runtime_queue",
    )
    assert "next[existing].enabled = 0" in discover
    assert "item.enabled = 0" in discover
    assert "global_files[index].duplicate_id" in request
    assert '"global_mod_toggle_blocked_duplicate_id"' in UI


def test_05_toggle_persists_using_stable_registry_key_and_fingerprint():
    request = c_block(
        CONSENT,
        "int dluma_lua_request_global_enabled",
        "void dluma_lua_process_global_runtime_queue",
    )
    fill = c_block(
        CONSENT,
        "static unsigned int dluma_global_fill_review_mutations",
        "static int dluma_global_find_exact_locked",
    )
    assert "dluma_global_find_exact_locked(request" in request
    assert "approve_current" in request
    assert 'DLUMA_ADD_MUTATION("global_mod_approved", item->metadata_fingerprint)' in fill
    assert 'DLUMA_ADD_MUTATION("global_mods", enabled_value)' in fill
    assert "registry_key" in fill


def test_06_rediscovery_reclassifies_against_exact_previous_identity():
    discover = c_block(
        DISCOVERY,
        "void dluma_lua_discover_globals(void)",
        "unsigned int dluma_lua_discover_locals",
    )
    assert "scan_start_revision" in discover
    assert "dluma_lua_discover_globals_mode" in discover
    assert "dluma_lua_classify_global_review" in discover
    assert "previous" in discover
    assert "metadata_fingerprint" in discover


def test_07_failed_persistence_cannot_silently_change_memory():
    request = c_block(
        CONSENT,
        "int dluma_lua_request_global_enabled",
        "void dluma_lua_process_global_runtime_queue",
    )
    persist = request.index("dluma_global_atomic_apply_mutations")
    memory_update = request.index("item->enabled =", persist)
    exact_recheck = request.index("dluma_global_find_exact_locked", persist)
    assert persist < exact_recheck < memory_update
    atomic_write = c_block(
        CONSENT,
        "static int dluma_global_atomic_apply_mutations",
        "static unsigned int dluma_global_fill_review_mutations",
    )
    assert "MoveFileExW" in atomic_write
    assert "DLUMA_MOVEFILE_WRITE_THROUGH" in atomic_write
    assert "if (delete_file) delete_file(temp_path)" in atomic_write
    assert "return 0" in atomic_write


def test_08_snapshot_is_locked_while_discovery_commits():
    snapshot = c_block(
        DISCOVERY,
        "unsigned int dluma_lua_snapshot_globals_with_revision",
        "int dluma_lua_set_global_enabled",
    )
    assert snapshot.index("dluma_lua_registry_lock();") < snapshot.index("out[i] =")
    assert snapshot.index("out[i] =") < snapshot.index("dluma_lua_registry_unlock();")
    assert "revision_out" in snapshot
    assert "global_registry_revision" in COMMON


def test_09_exact_root_scoping_preserves_the_real_workshop_controller():
    for hook, following in (
        ("dluma_ui_workshop_post_hook", "dluma_ui_workshop_init_hook"),
        ("dluma_ui_workshop_init_hook", "dluma_ui_workshop_show_hook"),
        ("dluma_ui_workshop_show_hook", "dluma_ui_workshop_selection_hook"),
        ("dluma_ui_workshop_selection_hook", "dluma_ui_registry_log_toggle"),
        ("dluma_ui_workshop_double_hook", "dluma_ui_registry_log_validation_failure"),
    ):
        block = c_block(UI, f"{hook}(", f"{following}(")
        assert "dluma_ui_is_mods_registry_screen(screen)" in block
        assert "else if (original)" in block
        assert "original(screen" in block
    button = c_block(
        UI, "dluma_ui_workshop_button_hook(", "dluma_ui_workshop_double_hook("
    )
    assert "dluma_ui_is_mods_registry_screen(screen)" in button
    assert re.search(
        r"if\s*\(\s*!handled\s*&&\s*original\s*\)\s*"
        r"original\s*\(\s*screen\s*,\s*button\s*,\s*event_code\s*\)",
        button,
    )
    mods_scope = c_block(
        UI,
        "static int dluma_ui_is_mods_registry_screen",
        "static int dluma_ui_is_stock_workshop_screen",
    )
    assert '"MenuDLUMAMods"' in mods_scope
    assert '"MenuExtras"' not in mods_scope


def test_09b_initial_rows_are_deferred_until_native_presenters_are_bound():
    post = c_block(
        UI, "static void __fastcall dluma_ui_workshop_post_hook", "static void __fastcall dluma_ui_workshop_init_hook"
    )
    init = c_block(
        UI, "static void __fastcall dluma_ui_workshop_init_hook", "static void __fastcall dluma_ui_workshop_show_hook"
    )
    show = c_block(
        UI, "static void __fastcall dluma_ui_workshop_show_hook", "static void __fastcall dluma_ui_workshop_selection_hook"
    )
    assert "g_dluma_base_main_menu_post(screen);" in post
    assert post.index("g_dluma_base_main_menu_post(screen);") < post.index("dluma_ui_registry_populate_list(screen, 0);")
    assert "global_mod_registry_deferred_population_completed" in post
    assert "dluma_ui_registry_populate_list(screen, 0);" not in init
    assert "dluma_ui_registry_populate_list(screen, 0);" not in show
    assert "g_dluma_registry_deferred_populate" in init
    assert "g_dluma_registry_deferred_populate" in show


def test_10_registry_rows_use_native_options_and_reserve_metadata_for_details():
    rows = c_block(
        UI,
        "static void *dluma_ui_registry_add_option_row",
        "static void dluma_ui_registry_add_group_header",
    )
    assert "dluma_ui_settings_add_row(" in rows
    assert '"DLUMA_Common_Disabled", "DISABLED"' in rows
    assert '"DLUMA_Common_Enabled", "ENABLED"' in rows
    assert rows.count("dluma_ui_settings_add_native_option") == 3
    assert "mapping->display_type" in rows
    assert "mapping->display_source" in rows
    assert "g_dluma_option_set_selection(item, requested, 0)" in rows
    assert "mapping->option_baseline_index = requested" in rows
    assert "dluma_ui_registry_fixed_status" in rows
    assert "<table" not in rows
    assert "dluma_ui_html_append_escaped" not in rows
    assert "dluma_ui_mod_details_stage_selected" in UI
    assert "dluma_ui_mod_details_add_overview" in UI


def test_11_details_include_every_required_field():
    details = c_block(
        UI,
        "static void dluma_ui_mod_details_add_overview",
        "static void dluma_ui_mod_details_update_for_row",
    )
    for label in (
        "NAME",
        "ID",
        "VERSION",
        "TYPE",
        "SCOPE",
        "SOURCE PROJECT",
        "WORKSHOP ID",
        "CANONICAL RELATIVE PATH",
        "LUA FILE PATH",
        "REQUESTED STATE",
        "LOADED",
        "AUTHOR DEFAULT",
        "USER OVERRIDE",
        "TRANSITION",
        "REVIEW STATE",
        "EXACT APPROVAL",
        "CHANGE REASON",
        "LAST ERROR",
        "PERMISSIONS / RISK",
        "CONTENT / MODULE SHA-256",
        "METADATA / SIDECAR SHA-256",
        "PERMISSION / APPROVAL FINGERPRINT",
        "REGISTRY IDENTITY",
        "CONFLICT STATUS",
        "MANIFEST / PE VALIDATION",
        "SANDBOX",
        "RESTART",
        "AVAILABILITY",
        "SCHEMA VERSION",
        "SCHEMA FINGERPRINT",
        "CATEGORY COUNT",
        "SETTING COUNT",
        "DIAGNOSTIC",
        "APPLICABLE SCOPE",
        "PERSISTENCE",
        "TYPED RECORD STATUS",
    ):
        assert f'"{label}"' in details


def test_12_selection_and_actions_resolve_through_stable_item_id_mapping():
    assert "typedef struct DlumaRegistryRowMap" in UI
    assert "g_dluma_list_get_selected_item" in UI
    assert "dluma_ui_registry_key_for_item" in UI
    assert "selection = dluma_ui_list_get_selection" in UI
    assert "g_dluma_ui_registry_rows[(unsigned int)selection].registry_key" in UI
    assert "native selection index and returned-item mapping are direct" in UI
    assert "script_id[128]" in UI
    assert "OptionTextDummy" in UI
    assert "g_dluma_list_add_item(" in UI
    assert "list, &row_text, 0, &option_template, 1, -1" in UI
    assert "mapping->item = item" in UI
    assert "mapping->list_index" in UI
    assert "mapping->registry_revision = registry_revision" in UI
    assert "wanted_id" in UI
    assert "dluma_stricmp(wanted_id" in UI
    assert "g_dluma_ui_registry_selected_by_category" in UI
    assert "g_dluma_ui_registry_category" in UI
    assert "restore_id" in UI
    assert "list + 120" not in UI
    assert "list + 128" not in UI
    assert "screen + 952" not in UI


def test_13_details_back_uses_native_history_and_mods_reentry_clears_review():
    button = c_block(
        UI, "dluma_ui_workshop_button_hook(", "dluma_ui_workshop_double_hook("
    )
    details_branch = button[
        button.index("dluma_ui_is_mod_details_screen(screen)") :
        button.index("dluma_ui_is_mod_config_screen(screen)")
    ]
    assert 'dluma_ui_object_id_equals(button, "B_Back")' in details_branch
    assert "MenuDLUMAModDetails Back delegated to native screen history." in details_branch
    assert "original(screen, button, event_code)" in details_branch
    show = c_block(
        UI, "static void __fastcall dluma_ui_workshop_show_hook(",
        "static void __fastcall dluma_ui_workshop_selection_hook(",
    )
    mods_show = show[
        show.index("dluma_ui_is_mods_registry_screen(screen)") :
        show.index("dluma_ui_is_validated_quest_log_screen(screen)")
    ]
    assert "dluma_ui_registry_hide_approval_popup()" in mods_show


def test_14_back_in_list_delegates_native_previous_screen_history():
    button = c_block(
        UI, "dluma_ui_workshop_button_hook(", "dluma_ui_workshop_double_hook("
    )
    assert "MenuDLUMAMods native Back delegated to previous-screen history." in button
    assert re.search(
        r"if\s*\(\s*original\s*\)\s*"
        r"original\s*\(\s*screen\s*,\s*button\s*,\s*event_code\s*\)",
        button,
    )


def test_16_pack_replaces_only_the_intentional_stock_extras_route():
    pack = ROOT / "build" / "package" / "Release" / "dluma.pak"
    with zipfile.ZipFile(pack) as archive:
        names = {name.replace("\\", "/") for name in archive.namelist()}
    assert "data/menu/scr/MenuExtras.xui" in names
    assert "data/menu/scr/MenuDLUMALogs.xui" in names
    assert "data/menu/scr/MenuMain_PC_workshop.xui" not in names


def test_17_native_mods_route_consumes_only_valid_id_driven_show_next():
    hook = c_block(
        UI,
        "static void __fastcall dluma_ui_menu_button_hook",
        "static int dluma_ui_install_mods_button_hook",
    )
    assert "if (!handled && original) original(menu_main, button, event_code)" in hook
    mods_route = hook[hook.index('dluma_ui_object_id_equals(button, "B_Mods")') : hook.index("/* On the supported build")]
    assert "handled = dluma_ui_route_mods_button_by_id(menu_main, button)" in mods_route
    assert "PressPath" not in mods_route
    assert "UINaviButton" not in mods_route
    assert "dluma_ui_open_mods_screen" not in hook


def test_18_visible_ui_text_uses_the_native_utf16_abi():
    assert "typedef struct DlumaTtlWideString" in UI
    assert re.search(r"const\s+DlumaTtlWideString\s*\*\s*text", UI)
    assert "DlumaTtlWideString row_text" in UI
    assert "DlumaTtlWideString value" in UI
    assert "dluma_ttl_wide_string_init_utf8" in UI
    assert "dluma_utf8_to_wide(text, storage, storage_count)" in UI
    assert "DlumaTtlString row_text" not in UI


def test_20_mouse_activation_selects_rows_and_details_use_native_rows():
    assert "?SetCanFocus@IUIElementControl@@UEAAX_N@Z" in UI
    assert "?SetVisible@IUIElement@@QEAAX_N@Z" in UI
    assert "?SetFocus@IUIElementControl@@QEAAX_NW4TYPE@EUIFocusChangeReason@@@Z" in UI
    assert "g_dluma_set_element_can_focus(item, 1)" in UI
    assert "dluma_ui_mod_details_stage_selected" in UI
    assert "dluma_ui_register_mod_details_screen" in UI
    assert "dluma_ui_mod_details_select_pointer_item" in UI
    assert "dluma_ui_mod_details_add_field" in UI
    assert "DLUMA_UI_MOD_DETAILS_VISIBLE_ROWS 9U" in UI
    assert "<PressPath>MenuDLUMAModDetails.xui</PressPath>" in XUI
    assert "dluma_ui_registry_native_index_for_item(item)" in UI
    assert "return (int)i;" in UI
    assert "DLUMA_DEVTOOLS_LIST_SET_SELECTION_RVA 0x01122290UL" in UI
    assert re.search(
        r"\(BYTE\s*\*\)\s*list\s*\+\s*DLUMA_UI_LIST_INTERFACE_OFFSET\s*,\s*"
        r"index\s*,\s*1",
        UI,
    )
    assert "0x011239E0 only adjusts" in UI
    assert "dluma_ui_registry_select_pointer_item" in UI
    assert 'button, "single_press"' in UI
    assert 'item, "activate"' in UI
    assert "global_mod_registry_mouse_input_path" in UI
    assert "global_mod_registry_mouse_selected_id" in UI
    assert "dluma_ui_list_set_selection(g_dluma_ui_registry_list, native_index)" in UI


def test_20b_details_panel_contains_wide_values_and_wraps_unbroken_utf8():
    assert "<Width>1050.000000</Width>" in DETAILS_XUI
    assert "<Position>120.000000,63.000000,0.000000</Position>" in DETAILS_XUI
    assert "<Width>470.000000</Width>" in DETAILS_XUI
    assert "<Width>500.000000</Width>" in DETAILS_XUI
    assert "<Width>482.000000</Width>" in DETAILS_XUI
    assert "<Id>G_DetailsValueScroll</Id>" in DETAILS_XUI
    assert "<Visual>LBScrollBarCraftingV</Visual>" in DETAILS_XUI
    assert "<AutoSizeToText>true</AutoSizeToText>" in DETAILS_XUI
    assert "<UseMask>true</UseMask>" in DETAILS_XUI
    assert "<NavRight>G_DetailsValueScroll</NavRight>" in DETAILS_XUI
    assert "<NavLeft>L_Maps</NavLeft>" in DETAILS_XUI
    assert "<Id>B_Start</Id>" in DETAILS_XUI
    assert "&amp;DLUMA_ModDetails_ViewScroll&amp;" in DETAILS_XUI
    formatting = c_block(
        UI,
        "static int dluma_ui_mod_details_append_bytes(",
        "static void dluma_ui_mod_details_update_for_row(",
    )
    assert "DLUMA_UI_MOD_DETAILS_HINT_COLUMNS" in formatting
    assert "(continuation & 0xC0U) != 0x80U" in formatting
    assert "(index % 32U) == 0U" in formatting
    assert "DLUMA_MOD_DETAILS_VALUE_PATH" in formatting
    assert "DLUMA_MOD_DETAILS_VALUE_SHA256" in formatting
    assert "DLUMA_MOD_DETAILS_VALUE_FINGERPRINT_PAIR" in formatting
    assert '"DLUMA_Common_Permissions"' in formatting
    assert '"DLUMA_Common_ExactApproval"' in formatting
    add_field = c_block(
        UI,
        "static int dluma_ui_mod_details_add_field_kind(",
        "static int dluma_ui_mod_details_add_field(",
    )
    assert "dluma_ui_mod_details_add_label_row(" in add_field
    assert "dluma_ui_settings_add_native_option" not in add_field
    assert "dluma_ui_find_required_child" not in add_field
    assert "dluma_ui_set_element_visible" not in add_field
    assert "g_dluma_main_menu_get_text_extent_real" not in add_field
    assert "g_dluma_main_menu_set_size_x" not in add_field
    assert "g_dluma_main_menu_set_relative_pos" not in add_field
    label_row = c_block(
        UI,
        "static void *dluma_ui_mod_details_add_label_row(",
        "static int dluma_ui_settings_add_native_option(",
    )
    assert '"OptionTextDummy"' in label_row
    assert '"ButtonDummy"' not in label_row
    assert "g_dluma_set_element_can_focus(item, 1)" in label_row
    assert "dluma_ui_set_element_text_association" not in label_row
    assert 'dluma_ui_find_required_child(item, "wrap")' in label_row
    assert "dluma_ui_set_element_visible(value_wrap, 0)" in label_row
    position_hook = c_block(
        UI,
        "static __int64 __fastcall dluma_ui_workshop_get_3d_position_hook(",
        "static void dluma_ui_registry_log_validation_failure(",
    )
    assert '"MenuLeaderboardsDL"' in position_hook
    assert "stock_wide_details_position" in position_hook
    assert "dluma_ui_is_mod_details_screen(screen)" in position_hook
    assert '"G_LeftPanel"' not in UI
    placement = (
        ROOT
        / "PakAssets"
        / "Camera"
        / "menuscreen3dplacement_menu_lvl_new.scr"
    ).read_text()
    assert (
        'Placement("MenuDLUMAModDetails", "menu_leaderboards_pos")'
        in placement
    )
    update = c_block(
        UI,
        "static void dluma_ui_mod_details_update_for_row(",
        "static void dluma_ui_mod_details_focus_value(",
    )
    assert "char body[DLUMA_UI_MOD_DETAILS_HINT_CAPACITY]" in update
    assert "dluma_ui_mod_details_format_value(" in update
    assert "dluma_ui_reset_validated_scroll_view(" in update
    button = c_block(
        UI, "dluma_ui_workshop_button_hook(", "dluma_ui_workshop_double_hook("
    )
    details_branch = button[
        button.index("dluma_ui_is_mod_details_screen(screen)") :
        button.index("dluma_ui_is_mod_config_screen(screen)")
    ]
    assert 'dluma_ui_object_id_equals(button, "B_Start")' in details_branch
    assert "dluma_ui_mod_details_focus_value();" in details_branch
    focus = c_block(
        UI,
        "static void dluma_ui_mod_details_focus_value(",
        "static void dluma_ui_mod_details_populate_list(",
    )
    assert "dluma_ui_workshop_bind_row_interaction_exports()" in focus
    assert "g_dluma_process_element_inputs(" in focus


def test_21_controller_activation_uses_shared_actions_and_explicit_shortcuts():
    button = c_block(
        UI, "dluma_ui_workshop_button_hook(", "dluma_ui_workshop_double_hook("
    )
    activation = c_block(
        UI,
        "static int dluma_ui_registry_activate_selected(",
        "static void __fastcall dluma_ui_workshop_button_hook(",
    )
    double = c_block(
        UI,
        "static void __fastcall dluma_ui_workshop_double_hook(",
        "static unsigned char __fastcall dluma_ui_workshop_uses_3d_rotation_hook(",
    )
    assert "(void)dluma_ui_registry_activate_selected(screen);" in button
    assert "(void)dluma_ui_registry_activate_selected(screen);" in double
    assert double.count("dluma_ui_registry_activate_selected(screen)") == 1
    assert "dluma_ui_registry_request_row_state" in activation
    request = c_block(
        UI,
        "static int dluma_ui_registry_request_row_state(",
        "static int dluma_ui_registry_activate_selected(",
    )
    assert "dluma_lua_request_global_enabled" in request
    assert "dluma_native_mods_request_hosts" in request
    assert "requested_state &&" in request
    assert "!g_dluma_ui_global_mods[index].approval_valid" in request
    assert "mapping->option_host_flags[selected]" in UI
    for host_state in (
        "DLUMA_NATIVE_HOST_OFF",
        "DLUMA_NATIVE_HOST_PLAYER",
        "DLUMA_NATIVE_HOST_EDITOR",
        "DLUMA_NATIVE_HOST_BOTH",
    ):
        assert host_state in UI
    assert "Workshop map launch suppressed" in double

    def control(control_id: str) -> str:
        start = XUI.index(f"<Id>{control_id}</Id>")
        return XUI[start:XUI.index("</Properties>", start)]

    assert "<PressKey>" not in control("B_Start")
    assert "<UnfocusedInput>true</UnfocusedInput>" in control("B_Start")
    assert "<PressKey>22530</PressKey>" in control("B_Workshop")
    assert "<PressPath>MenuDLUMAModDetails.xui</PressPath>" in control("B_Workshop")
    assert "<UnfocusedInput>true</UnfocusedInput>" in control("B_Workshop")
    assert "<Show>false</Show>" not in control("B_Workshop")
    assert "<Id>B_Details</Id>" not in XUI
    assert "<PressKey>22529</PressKey>" in control("B_Back")
    assert "<UnfocusedInput>true</UnfocusedInput>" in control("B_Back")
    assert "<PressKey>22594</PressKey>" not in control("L_Maps")


def test_22_approval_focus_and_shortcuts_match_a_confirm_b_cancel():
    lua_approval = c_block(
        UI,
        "static int dluma_ui_registry_show_approval_popup(",
        "static void dluma_ui_registry_hide_approval_popup(",
    )
    native_approval = c_block(
        UI,
        "static int dluma_ui_registry_show_native_approval_popup(",
        "static int dluma_ui_registry_show_approval_popup(",
    )
    assert "dluma_ui_focus_element(g_dluma_ui_registry_approval_confirm)" in lua_approval
    assert "dluma_ui_focus_element(g_dluma_ui_registry_approval_confirm)" in native_approval
    assert "dluma_ui_focus_element(g_dluma_ui_registry_approval_cancel)" not in lua_approval
    assert "dluma_ui_focus_element(g_dluma_ui_registry_approval_cancel)" not in native_approval

    confirm_start = XUI.index("<Id>B_ApprovalConfirm</Id>")
    confirm = XUI[confirm_start:XUI.index("</Properties>", confirm_start)]
    cancel_start = XUI.index("<Id>B_ApprovalCancel</Id>")
    cancel = XUI[cancel_start:XUI.index("</Properties>", cancel_start)]
    assert "<PressKey>" not in confirm
    assert "<UnfocusedInput>true</UnfocusedInput>" in confirm
    assert "<PressKey>22529</PressKey>" in cancel
    assert "<UnfocusedInput>true</UnfocusedInput>" in cancel


def test_24_mods_and_details_receive_native_right_side_3d_and_rail_contracts():
    uses_3d = c_block(
        UI,
        "static unsigned char __fastcall dluma_ui_workshop_uses_3d_rotation_hook(",
        "dluma_ui_workshop_uses_background_screen_name_hook(void *screen)",
    )
    background = c_block(
        UI,
        "dluma_ui_workshop_uses_background_screen_name_hook(void *screen)",
        "static __int64 __fastcall dluma_ui_workshop_get_3d_position_hook(",
    )
    position = c_block(
        UI,
        "static __int64 __fastcall dluma_ui_workshop_get_3d_position_hook(",
        "static void dluma_ui_registry_log_validation_failure(",
    )
    for hook in (uses_3d, background, position):
        assert "dluma_ui_is_mods_registry_screen(screen)" in hook
        assert "dluma_ui_is_mod_details_screen(screen)" in hook
    assert '"MenuOptionsControlsKeyboard"' in position
    assert '"MenuExtras"' in position
    assert '"MenuLeaderboardsDL"' in position
    assert "stock_mods_position" in position
    assert "stock_wide_details_position" in position


def test_25_details_are_staged_and_registered_before_native_navigation():
    button = c_block(
        UI, "dluma_ui_workshop_button_hook(", "dluma_ui_workshop_double_hook("
    )
    details = button[
        button.index('dluma_ui_object_id_equals(button, "B_Workshop")') :
        button.index('dluma_ui_object_id_equals(button, "B_Configure")')
    ]
    assert "dluma_ui_mod_details_stage_selected()" in details
    assert "dluma_ui_register_mod_details_screen(" in details
    assert "dluma_ui_show_next_registered_screen(" in details
    assert (
        "MenuDLUMAMods Details delegated to the native ShowNextUI history route."
        in details
    )
    assert "dluma_ui_mod_details_report_open_failure(" in details
    assert "LoadUIScreen" not in details
    assert "ShowUIScreen" not in details


def test_25a_details_navigation_validates_the_exact_native_show_next_ui_slot():
    helper = c_block(
        UI,
        "static int dluma_ui_show_next_registered_screen_impl(",
        "static int dluma_ui_show_next_registered_screen(",
    )
    assert "DLUMA_MENU_OPEN_VTABLE_BYTE_OFFSET" in helper
    assert "DLUMA_DEVTOOLS_MENU_OPEN_FN_RVA" in helper
    assert "DLUMA_DEVTOOLS_GAMEDLL_TIMESTAMP" in helper
    assert "DLUMA_DEVTOOLS_GAMEDLL_IMAGE_SIZE" in helper
    assert "dluma_ui_is_expected_menu_main_controller(screen, 0)" in helper
    assert "dluma_ui_is_expected_workshop_controller(screen, 0)" in helper
    assert "memcmp(expected, expected_prefix, sizeof(expected_prefix))" in helper
    assert "open_screen(screen, screen_id, 0, 0, 0, 1, 0);" in helper
    assert "LoadUIScreen" not in helper
    assert "ShowUIScreen" not in helper


def test_25b_details_staging_has_no_silent_selection_failure():
    staging = c_block(
        UI,
        "static DlumaRegistryRowMap *dluma_ui_mod_details_selected_row(void)",
        "static int dluma_ui_mod_details_revalidate(void)",
    )
    assert "stable_registry_key" in staging
    assert "first_visible_row" in staging
    assert "no_selectable_mod" in staging
    assert "native_registry_key_stale" in staging
    assert "lua_registry_key_stale" in staging
    assert "DLUMA_ModDetails_OpenFailed" in staging


def test_26_seen_is_committed_only_after_the_details_screen_is_visible():
    confirm = c_block(
        UI,
        "static void dluma_ui_mod_details_confirm_visible(",
        "static void dluma_ui_registry_set_main_controls_visible(",
    )
    assert "dluma_ui_mod_details_is_visible(screen)" in confirm
    assert "dluma_lua_mark_global_seen(&g_dluma_ui_mod_details_request)" in confirm
    assert "g_dluma_ui_mod_details_seen_pending = 0" in confirm
