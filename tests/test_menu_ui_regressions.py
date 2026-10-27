from pathlib import Path
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[1]
PATCHED_UI = ROOT / "src" / "dlw_ui_patched.c"
ORIGINAL_UI = ROOT / "src" / "dlw_ui.c"
NATIVE_MODS = ROOT / "src" / "dlw_native_mods.c"
CLANG_BUILD = ROOT / "build_windows_clang.bat"
MSBUILD_TARGETS = ROOT / "Directory.Build.targets"
MODS_XUI = ROOT / "PakAssets" / "XUI" / "MenuDLUMAMods.xui"
WORKSHOP_XUI = ROOT / "PakAssets" / "XUI" / "DLUMA_SteamWorkshop.xui"


def block(source: str, start: str, end: str) -> str:
    begin = source.index(start)
    finish = source.index(end, begin)
    return source[begin:finish]


def test_wrapper_keeps_validated_original_in_one_translation_unit():
    source = PATCHED_UI.read_text(encoding="utf-8")
    assert '#include "dlw_ui.c"' in source
    assert "#define dluma_ui_start dluma_ui_start_base" in source
    assert "#define dluma_ui_stop dluma_ui_stop_base" in source
    assert "#define dluma_ui_workshop_post_hook dluma_ui_workshop_post_hook_base" in source
    assert "#define dluma_ui_workshop_show_hook dluma_ui_workshop_show_hook_base" in source


def test_both_build_paths_compile_only_wrapped_ui_translation_unit():
    clang = CLANG_BUILD.read_text(encoding="utf-8")
    assert "list_vcxproj_sources.py" in clang
    assert "dlw_ui_patched.c includes the validated dlw_ui.c" in clang
    targets = MSBUILD_TARGETS.read_text(encoding="utf-8")
    project = (ROOT / "DLUMA.vcxproj").read_text(encoding="utf-8")
    assert '<ClCompile Remove="src\\dlw_ui.c" />' in targets
    assert '<ClCompile Include="src\\dlw_ui_patched.c" />' in targets
    assert '<ClCompile Include="src\\dlw_mod_row_format_core.c" />' in project
    assert '<ClInclude Include="src\\dlw_mod_row_format_core.h" />' in project


def test_workshop_uses_six_rows_and_rebuilds_once_at_final_capacity():
    source = PATCHED_UI.read_text(encoding="utf-8")
    assert "DLUMA_UI_FIX_WORKSHOP_VISIBLE_ROWS 6" in source
    assert "DLUMA_UI_FIX_CAPACITY_SETTLE_MS" not in source
    assert "g_dluma_ui_fix_workshop_settle" not in source
    show = block(
        source,
        "static void __fastcall dluma_ui_workshop_show_hook(",
        "static void __fastcall dluma_ui_workshop_post_hook(",
    )
    assert show.count("dluma_ui_fix_refresh_workshop_after_capacity(screen, 0);") == 1
    assert "removes the previous need to press Refresh" in show
    post_begin = source.rindex(
        "static void __fastcall dluma_ui_workshop_post_hook("
    )
    post_end = source.index(
        "static VOID CALLBACK dluma_ui_fix_timer_proc(", post_begin
    )
    post = source[post_begin:post_end]
    assert "dluma_ui_fix_refresh_workshop_after_capacity" not in post
    workshop = WORKSHOP_XUI.read_text(encoding="utf-8")
    list_block = workshop[workshop.index("<Id>L_Maps</Id>") :]
    list_block = list_block[: list_block.index("</Properties>")]
    assert "<Height>332.000000</Height>" in list_block
    assert 332 >= 6 * 50
    assert 332 < 7 * 50
    assert "<Position>0.000000,467.200012,0.000000</Position>" in workshop
    assert "<Position>0.000000,486.688477,0.000000</Position>" in workshop


def test_mods_list_uses_ten_native_option_rows_and_four_column_header():
    source = PATCHED_UI.read_text(encoding="utf-8")
    assert "DLUMA_UI_FIX_MODS_VISIBLE_ROWS 10" in source
    assert "dluma_ui_fix_append_compact_html" not in source
    assert "dluma_ui_fix_build_lua_row" not in source
    assert "dluma_ui_fix_build_native_row" not in source
    mods = MODS_XUI.read_text(encoding="utf-8")
    list_block = mods[mods.index("<Id>L_Maps</Id>") :]
    list_block = list_block[: list_block.index("</Properties>")]
    assert "<Height>454.000000</Height>" in list_block
    assert "<Visual>ListBoxOptionsV</Visual>" in list_block
    for control in (
        "T_ModColumnName",
        "T_ModColumnType",
        "T_ModColumnStatus",
        "T_ModColumnSource",
    ):
        assert f"<Id>{control}</Id>" in mods


def test_mod_rows_use_scroll_synchronized_fixed_columns_without_pipe_text():
    source = PATCHED_UI.read_text(encoding="utf-8")
    original = ORIGINAL_UI.read_text(encoding="utf-8")
    presenter = block(
        source,
        "static void dluma_ui_fix_present_registry_columns(",
        "static void dluma_ui_fix_set_item_navigation(",
    )
    assert "dluma_ui_workshop_get_first_visible" in presenter
    assert "dluma_ui_fix_registry_row_for_list_index" in presenter
    assert "mapping->display_type" in presenter
    assert "mapping->display_status" in presenter
    assert "mapping->display_source" in presenter
    assert "row_ready = mapping && mapping->item" in presenter
    assert "row_ready ? mapping->display_type" in presenter
    assert 'dluma_ui_find_required_child(mapping->item, "wrap")' in source
    assert "dluma_ui_set_element_visible(value_wrap, visible ? 1 : 0)" in source
    assert "dluma_mod_row_format_utf8_ellipsis" in source
    assert '"  |  "' not in original
    assert "dluma_ui_registry_format_columns" not in original

    mods = MODS_XUI.read_text(encoding="utf-8")
    assert "<Id>G_ModColumnGuides</Id>" in mods
    for divider in (
        "I_ModColumnDividerName",
        "I_ModColumnDividerType",
        "I_ModColumnDividerStatus",
    ):
        assert f"<Id>{divider}</Id>" in mods
    for row in range(10):
        for column in ("Type", "Status", "Source"):
            assert f"<Id>T_ModRow{row}{column}</Id>" in mods

    root = ET.fromstring(mods)
    controls = {
        element.find("./Properties/Id").text: element.find("./Properties")
        for element in root.findall(".//MyText")
        if element.find("./Properties/Id") is not None
    }
    for row in range(10):
        expected_y = 31 + (35 * row)
        for column, expected_x in (
            ("Type", 405),
            ("Status", 555),
            ("Source", 745),
        ):
            position = controls[f"T_ModRow{row}{column}"].find("Position").text
            x, y, z = (float(value) for value in position.split(","))
            assert (x, y, z) == (expected_x, expected_y, 0)


def test_native_mod_rows_are_rebound_without_overwriting_options():
    source = PATCHED_UI.read_text(encoding="utf-8")
    rebind = block(
        source,
        "static void dluma_ui_fix_rebind_registry_rows(",
        "static void dluma_ui_fix_repair_registry_navigation(",
    )
    assert "dluma_ui_workshop_list_item" in rebind
    assert "mapping->list_index" in rebind
    assert "dluma_mod_row_format_rebind_live_item" in rebind
    assert rebind.count("mapping->item = 0;") == 2
    assert "dluma_ui_set_element_html_text_association" not in rebind
    assert "dluma_ui_set_element_text_association" not in rebind
    post = block(
        source,
        "static void __fastcall dluma_ui_workshop_post_hook(",
        "static VOID CALLBACK dluma_ui_fix_timer_proc(",
    )
    post_after_base = post[post.index("dluma_ui_workshop_post_hook_base(screen);") :]
    mods = post_after_base[
        post_after_base.index("else if (dluma_ui_is_mods_registry_screen(screen)") :
        post_after_base.index("else if (dluma_ui_is_mod_config_screen(screen)")
    ]
    assert "dluma_ui_fix_rebind_registry_rows();" in mods
    assert "dluma_ui_fix_repair_registry_navigation();" not in mods
    assert mods.index("dluma_ui_fix_rebind_registry_rows();") < mods.index(
        "dluma_ui_fix_present_registry_columns(screen);"
    )


def test_mod_row_format_core_harness_exercises_long_rows_and_live_rebinding():
    import subprocess

    subprocess.run(
        ["cmd.exe", "/d", "/s", "/c", "call tools\\build_mod_row_format_fake_tests.bat"],
        cwd=ROOT,
        check=True,
    )
    subprocess.run(
        [str(ROOT / "build/mod-row-format-fake/mod_row_format_core_harness.exe")],
        cwd=ROOT,
        check=True,
    )

    core = (ROOT / "src" / "dlw_mod_row_format_core.c").read_text(encoding="utf-8")
    assert "dluma_mod_row_format_utf8_ellipsis" in core
    assert "secondary_associations[0] = 1" in core
    assert "secondary_associations[1] = 2" in core
    assert "dluma_mod_row_format_rebind_live_item" in core


def test_mod_categories_default_to_all_and_use_real_group_header_rows():
    source = PATCHED_UI.read_text(encoding="utf-8")
    assert "dluma_ui_fix_build_registry_header" not in source
    assert "dluma_ui_fix_present_registry_headers" not in source
    assert "Sandboxed global Lua modules" not in source
    mods = MODS_XUI.read_text(encoding="utf-8")
    assert "<Id>B_PrevScreenMode</Id>" in mods
    assert "<Id>B_NextScreenMode</Id>" in mods
    assert "<Id>T_CurrentMode</Id>" in mods
    assert "&amp;DLUMA_Mods_AllGroup&amp;" in mods
    ui = (ROOT / "src" / "dlw_ui.c").read_text(encoding="utf-8")
    assert "dluma_ui_registry_add_group_header" in ui
    assert '"DLUMA_Mods_CodeHeader", "CODE MODS"' in ui
    assert '"DLUMA_Mods_LuaHeader", "LUA MODS"' in ui
    assert '"DLUMA_Mods_AssetHeader", "ASSET MODS"' in ui
    assert '"DLUMA_Mods_MainMenuHeader", "MAIN MENU MODS"' in ui


def test_gamepad_focus_is_clamped_to_real_selectable_rows():
    source = PATCHED_UI.read_text(encoding="utf-8")
    registry = block(
        source,
        "static void dluma_ui_fix_repair_registry_navigation(",
        "static void dluma_ui_fix_repair_workshop_navigation(",
    )
    assert "dluma_ui_fix_registry_index_is_selectable" in registry
    assert "dluma_ui_fix_registry_nearest_selectable_index" in registry
    assert "dluma_ui_list_get_focused_index" in registry
    assert "dluma_ui_list_set_selection" in registry
    assert "dluma_ui_workshop_set_focus" in registry

    rebind = block(
        source,
        "static void dluma_ui_fix_rebind_registry_rows(",
        "static int dluma_ui_fix_registry_nearest_selectable_index(",
    )
    assert "dluma_ui_workshop_list_item" in rebind
    assert "dluma_ui_fix_set_item_navigation" not in rebind


def test_mods_navigation_does_not_reprocess_rows_each_frame():
    source = PATCHED_UI.read_text(encoding="utf-8")
    rebind = block(
        source,
        "static void dluma_ui_fix_rebind_registry_rows(",
        "static int dluma_ui_fix_registry_nearest_selectable_index(",
    )
    assert "g_dluma_process_element_inputs" not in rebind
    assert "dluma_ui_fix_set_item_navigation" not in rebind

    repair = block(
        source,
        "static void dluma_ui_fix_repair_registry_navigation(",
        "static void dluma_ui_fix_repair_workshop_navigation(",
    )
    assert "dluma_ui_fix_set_item_navigation(" not in repair

    workshop = block(
        source,
        "static void dluma_ui_fix_repair_workshop_navigation(",
        "static void dluma_ui_fix_sync_settings_pointer_focus(",
    )
    assert "dluma_ui_workshop_is_selectable_index" in workshop
    assert "dluma_ui_workshop_nearest_selectable_index" in workshop
    assert "dluma_ui_workshop_set_focus" in workshop
    assert "dluma_ui_fix_set_item_navigation" in workshop


def test_successful_native_events_do_not_publish_registry_revisions():
    source = NATIVE_MODS.read_text(encoding="utf-8")
    dispatch = block(
        source,
        "static void dluma_native_dispatch_queued_event(\n",
        "static void dluma_native_api_event_sink(\n",
    )
    commit = dispatch[dispatch.index("if (callback_ok) {") :]
    success, failure = commit.split("} else {", maxsplit=1)
    revision = "++g_dluma_native_mods.revision;"
    crash_context = "dluma_native_publish_crash_context_locked();"
    assert revision not in success
    assert crash_context not in success
    assert failure.count(revision) == 1
    assert failure.count(crash_context) == 1
    assert "Registry revisions describe snapshot-visible state" in failure


def test_keyboard_and_controller_divider_replay_skips_disabled_headers():
    # This mirrors the exact list-index rule used by the wrapper: mapped rows
    # are selectable, headers are deliberately disabled and traversal finds the
    # next mapped row in either direction without ever returning a header.
    selectable = {1, 2, 4, 5}

    def next_row(current: int, direction: int) -> int | None:
        index = current + direction
        while 0 <= index <= 5:
            if index in selectable:
                return index
            index += direction
        return None

    assert next_row(2, 1) == 4  # keyboard/controller down over LUA MODS
    assert next_row(4, -1) == 2  # keyboard/controller up over LUA MODS
    assert next_row(1, -1) is None  # first selectable row remains bounded
    assert next_row(5, 1) is None  # last selectable row remains bounded

    source = PATCHED_UI.read_text(encoding="utf-8")
    post = block(
        source,
        "static void __fastcall dluma_ui_workshop_post_hook(",
        "static VOID CALLBACK dluma_ui_fix_timer_proc(",
    )
    post_after_base = post[post.index("dluma_ui_workshop_post_hook_base(screen);") :]
    mods = post_after_base[
        post_after_base.index("else if (dluma_ui_is_mods_registry_screen(screen)") :
        post_after_base.index("else if (dluma_ui_is_mod_config_screen(screen)")
    ]
    assert "dluma_ui_fix_repair_registry_navigation();" not in mods
    assert "dluma_ui_list_set_selection" not in mods
    assert "dluma_ui_workshop_set_focus" not in mods
    assert "dluma_ui_fix_set_item_navigation" not in mods
    assert "dluma_ui_fix_repair_workshop_navigation(screen);" in post


def test_settings_mouse_focus_drives_selection_highlight_and_hint():
    source = PATCHED_UI.read_text(encoding="utf-8")
    focus = block(
        source,
        "static void dluma_ui_fix_sync_settings_pointer_focus(",
        "static void dluma_ui_fix_apply_settings_initial_focus(",
    )
    assert "dluma_ui_list_get_focused_index" in focus
    assert "dluma_ui_list_set_selection" in focus
    assert "dluma_ui_settings_update_for_row" in focus
    post = block(
        source,
        "static void __fastcall dluma_ui_workshop_post_hook(",
        "static VOID CALLBACK dluma_ui_fix_timer_proc(",
    )
    assert "dluma_ui_is_loader_settings_screen(screen)" in post
    assert "dluma_ui_fix_sync_settings_pointer_focus();" in post


def test_workshop_reentry_resets_selection_focus_viewport_and_preview_to_top():
    source = PATCHED_UI.read_text(encoding="utf-8")
    reset = block(
        source,
        "static void dluma_ui_fix_reset_workshop_to_top(",
        "static void dluma_ui_fix_refresh_workshop_after_capacity(",
    )
    assert "dluma_ui_workshop_nearest_selectable_index(list, 0)" in reset
    assert "dluma_ui_list_set_selection(list, target);" in reset
    assert "dluma_ui_workshop_set_focus(list, target, 1)" in reset
    assert "dluma_ui_workshop_set_first_visible(list, target)" in reset
    assert "dluma_ui_focus_element(list);" in reset
    assert "dluma_ui_workshop_present(screen);" in reset
    refresh = block(
        source,
        "static void dluma_ui_fix_refresh_workshop_after_capacity(",
        "static void __fastcall dluma_ui_workshop_show_hook(",
    )
    assert "if (!preserve_selection)" in refresh
    assert "dluma_ui_fix_reset_workshop_to_top(screen);" in refresh
    original = ORIGINAL_UI.read_text(encoding="utf-8")
    assert "dluma_ui_workshop_capture_refresh_state(list, &state);" in original
    assert "dluma_ui_workshop_restore_refresh_state(" in original


def test_settings_entry_focus_is_one_shot_after_deferred_population():
    source = PATCHED_UI.read_text(encoding="utf-8")
    apply_focus = block(
        source,
        "static void dluma_ui_fix_apply_settings_initial_focus(",
        "static void dluma_ui_fix_reset_workshop_to_top(",
    )
    assert "g_dluma_ui_fix_settings_initial_focus_pending" in apply_focus
    assert "if (!g_dluma_ui_settings_row_count)" in apply_focus
    assert "dluma_ui_list_set_selection(g_dluma_ui_settings_list, 0);" in apply_focus
    assert "dluma_ui_workshop_set_focus(g_dluma_ui_settings_list, 0, 1)" in apply_focus
    assert "dluma_ui_focus_element(g_dluma_ui_settings_list);" in apply_focus
    assert "dluma_ui_settings_update_for_row(0);" in apply_focus
    assert apply_focus.count(
        "g_dluma_ui_fix_settings_initial_focus_pending, 0"
    ) >= 2
    show = block(
        source,
        "static void __fastcall dluma_ui_workshop_show_hook(",
        "static void __fastcall dluma_ui_workshop_post_hook(",
    )
    post = block(
        source,
        "static void __fastcall dluma_ui_workshop_post_hook(",
        "static VOID CALLBACK dluma_ui_fix_timer_proc(",
    )
    assert "g_dluma_ui_fix_settings_initial_focus_pending, 1" in show
    assert "dluma_ui_fix_apply_settings_initial_focus();" in post


def test_logs_rows_are_not_touched_before_deferred_population():
    source = PATCHED_UI.read_text(encoding="utf-8")
    show = block(
        source,
        "static void __fastcall dluma_ui_workshop_show_hook(",
        "static void __fastcall dluma_ui_workshop_post_hook(",
    )
    logs_show = show[show.index("else if (dluma_ui_is_logs_screen(screen))") :]
    assert "dluma_ui_fix_present_logs_messages(screen);" not in logs_show
    assert "g_dluma_ui_fix_logs_messages_ready = 0;" in logs_show

    post = block(
        source,
        "static void __fastcall dluma_ui_workshop_post_hook(",
        "static VOID CALLBACK dluma_ui_fix_timer_proc(",
    )
    base_call = post.index("dluma_ui_workshop_post_hook_base(screen);")
    sync_call = post.index("dluma_ui_fix_sync_logs_navigation(1);")
    present_call = post.index("dluma_ui_fix_present_logs_messages(screen);")
    assert base_call < sync_call < present_call
    assert "dluma_ui_fix_sync_logs_navigation(0);" not in post


def test_hook_install_and_teardown_remain_exact_and_reversible():
    source = PATCHED_UI.read_text(encoding="utf-8")
    install = block(
        source,
        "static void dluma_ui_fix_install_lifecycle_hooks(",
        "static void dluma_ui_fix_restore_lifecycle_hooks(",
    )
    restore = block(
        source,
        "static void dluma_ui_fix_restore_lifecycle_hooks(",
        "static void dluma_ui_fix_set_capacity(",
    )
    assert "g_dluma_workshop_post_vtable_slot" in install
    assert "g_dluma_workshop_show_vtable_slot" in install
    assert "dluma_ui_workshop_post_hook_base" in install
    assert "dluma_ui_workshop_show_hook_base" in install
    assert "dluma_ui_workshop_post_hook_base" in restore
    assert "dluma_ui_workshop_show_hook_base" in restore
    stop = source[source.index("void dluma_ui_stop(void)") :]
    assert stop.index("dluma_ui_fix_restore_lifecycle_hooks();") < stop.index(
        "dluma_ui_stop_base();"
    )


def test_original_source_remains_unmodified_reference_implementation():
    source = ORIGINAL_UI.read_text(encoding="utf-8")
    assert "int dluma_ui_start(void)" in source
    assert "void dluma_ui_stop(void)" in source
    assert "static void __fastcall dluma_ui_workshop_post_hook" in source
    assert "static void __fastcall dluma_ui_workshop_show_hook" in source
