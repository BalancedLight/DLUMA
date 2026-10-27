from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def text(relative: str) -> str:
    return (ROOT / relative).read_text(encoding="utf-8")


def block(source: str, start: str, end: str) -> str:
    begin = source.index(start)
    finish = source.index(end, begin)
    return source[begin:finish]


def test_source_origin_uses_canonical_location_not_retained_published_id() -> None:
    workshop = text("src/dlw_workshop.c")
    classify = block(
        workshop,
        "int dluma_classify_project_source(",
        "static int dluma_mod_source_key(",
    )
    assert "dluma_open_directory_identity" in classify
    assert "dluma_source_is_subscribed_path(identity.final_path)" in classify
    assert "dluma_read_workshop_id" in classify
    assert "local-project:" in classify
    assert "DLW_MOD_SOURCE_PATH_V1" in classify
    assert classify.index("dluma_source_is_subscribed_path") < classify.index(
        "source.is_subscribed_workshop" if "source.is_subscribed_workshop" in classify else "subscribed-workshop:"
    )


def test_source_persistence_is_kind_and_normalized_id_scoped() -> None:
    common = text("src/dlw_common.h")
    workshop = text("src/dlw_workshop.c")
    assert "dluma_mod_source_selection_read" in common
    assert "dluma_mod_source_selection_write" in common
    assert 'GetPrivateProfileStringW(L"mod_sources"' in workshop
    assert 'WritePrivateProfileStringW(L"mod_sources"' in workshop
    assert 'dluma_str_append(output, capacity, ":")' in workshop


def test_distinct_lua_sources_are_alternatives_and_same_source_is_conflict() -> None:
    discovery = text("src/dlw_lua_discovery.c")
    duplicate = block(
        discovery,
        "existing = -1;",
        "/* A declared ID is one logical mod.",
    )
    selection = block(
        discovery,
        "/* A declared ID is one logical mod.",
        "dluma_lua_registry_lock();",
    )
    assert "next[duplicate_index].source_identity" in duplicate
    assert 'dluma_mod_source_selection_read(\n            "lua"' in selection
    assert "!next[j].source_is_subscribed" in selection
    assert "source_selection_missing" in selection


def test_every_kind_can_resolve_a_source_when_no_copy_is_usable() -> None:
    """A declared ID whose copies all fail must still resolve to one of them.

    Otherwise nothing is selected, the row reports a missing source, and the
    picker that is supposed to fix it has nothing to offer.
    """
    selections = {
        "src/dlw_lua_discovery.c": (
            "/* A declared ID is one logical mod.",
            "dluma_lua_registry_lock();",
        ),
        "src/dlw_native_mods.c": (
            'dluma_mod_source_selection_read(\n            "dll"',
            "static void dluma_native_config_identity(",
        ),
        "src/hosts/dlw_managed_host.c": (
            'dluma_mod_source_selection_read(\n            "csharp"',
            "static void dluma_managed_add_package(",
        ),
    }
    for path, (start, end) in selections.items():
        selection = block(text(path), start, end)
        # A usable copy is still preferred; the fallback only runs after it.
        assert "any_local_index" in selection, path
        assert "any_index" in selection, path
        assert selection.index("if (!explicit_selection && local_index >= 0)") < (
            selection.index("any_local_index >= 0 ? any_local_index : any_index")
        ), path


def test_source_requests_are_independent_of_copy_validity() -> None:
    """Choosing which copy provides an ID is not an enable or a load."""
    consent = text("src/dlw_global_consent.c")
    native = text("src/dlw_native_mods.c")
    managed = text("src/hosts/dlw_managed_host.c")
    ui = text("src/dlw_ui.c")
    request = block(
        consent,
        "int dluma_lua_request_global_source(",
        "void dluma_lua_process_global_runtime_queue(",
    )
    assert "item->valid && !item->duplicate_id" not in request
    assert "!item->duplicate_id) {" in request
    enable = block(
        consent,
        "int dluma_lua_request_global_enabled(",
        "int dluma_lua_request_global_source(",
    )
    assert "g_dluma.lua_discovery.global_files[index].valid" in enable
    for source, entry, leave in (
        (native, "int dluma_native_mods_request_source(", "static void dluma_native_"),
        (managed, "int dluma_managed_mods_request_source(", "static void dluma_managed_"),
    ):
        request = block(source, entry, leave)
        assert "!item->duplicate_id && !item->runtime_error[0]" not in request
    picker = block(
        ui,
        "static void dluma_ui_mod_details_add_source(void)",
        "static int dluma_ui_mod_details_select_source(",
    )
    assert "!source->valid" not in picker
    assert '"DLUMA_ModDetails_SourceConflict"' in picker
    assert '"DLUMA_Mods_SourceMissing"' not in picker


def test_native_and_managed_load_gates_require_selected_source() -> None:
    native = text("src/dlw_native_mods.c")
    managed = text("src/hosts/dlw_managed_host.c")
    assert "int dluma_native_mods_request_source(" in native
    assert "int dluma_managed_mods_request_source(" in managed
    assert native.count("source_selected") >= 5
    assert managed.count("source_selected") >= 6
    assert '!dluma_mod_source_selection_write(\n            "dll"' in native
    assert '!dluma_mod_source_selection_write(\n            "csharp"' in managed


def test_details_has_one_logical_row_and_focusable_exact_source_rows() -> None:
    ui = text("src/dlw_ui.c")
    assert "dluma_ui_registry_show_lua_provider" in ui
    assert "dluma_ui_registry_show_native_provider" in ui
    assert "dluma_ui_registry_show_managed_provider" in ui
    picker = block(
        ui,
        "static void dluma_ui_mod_details_add_source(void)",
        "static int dluma_ui_mod_details_select_source(",
    )
    add_source_row = block(
        ui,
        "static int dluma_ui_mod_details_add_source_row(",
        "static void dluma_ui_mod_details_add_source(void)",
    )
    select = block(
        ui,
        "static int dluma_ui_mod_details_select_source(",
        "static int dluma_ui_mod_details_append_bytes(",
    )
    assert "source->source_identity" in picker
    assert "dluma_ui_mod_details_add_label_row" in add_source_row
    assert "row->source_identity" in add_source_row
    assert "dluma_ui_settings_add_native_option" not in picker
    source_label = block(
        ui,
        "static void dluma_ui_mod_details_source_label(",
        "static void dluma_ui_mod_details_add_source(void)",
    )
    assert "project_root" not in source_label
    assert "DLUMA_UI_SOURCE_OPTION_CODE_UNIT_LIMIT" in source_label
    assert "dluma_utf8_to_wide" in source_label
    assert "dluma_wide_to_utf8" in source_label
    assert "selection_missing" in picker
    assert picker.count("!g_dluma_ui_mod_details_") >= 3
    assert '"DLUMA_Common_Unavailable", "UNAVAILABLE"' in picker
    assert "mod_details_source_selection_missing_choices_shown" in picker
    assert "dluma_lua_request_global_source" in select
    assert "dluma_native_mods_request_source" in select
    assert "dluma_managed_mods_request_source" in select
    assert "SOURCE SAVED. RESTART REQUIRED." in select
    details_buttons = block(
        ui,
        "else if (!g_dluma_registry_hooks_stopping &&\n             !g_dluma_registry_runtime_disabled && !g_dluma.stop_requested &&\n             dluma_ui_is_mod_details_screen(screen))",
        "else if (!g_dluma_registry_hooks_stopping &&\n             !g_dluma_registry_runtime_disabled && !g_dluma.stop_requested &&\n             dluma_ui_is_mod_config_screen(screen))",
    )
    assert 'dluma_ui_object_id_equals(button, "B_Start")' in details_buttons
    assert "dluma_ui_mod_details_select_source(" in details_buttons


def test_lua_registry_hash_tracks_resolved_source_state() -> None:
    discovery = text("src/dlw_lua_discovery.c")
    registry_hash = block(
        discovery,
        "static ULONGLONG dluma_lua_hash_global_registry(",
        "void dluma_lua_rehash_globals_locked(void)",
    )
    assert "entries[i].source_is_subscribed" in registry_hash
    assert "entries[i].source_selected" in registry_hash
    assert "entries[i].source_selection_missing" in registry_hash


def test_custom_main_menu_source_options_hide_paths_and_fit_the_row() -> None:
    ui = text("src/dlw_ui.c")
    source_label = block(
        ui,
        "static void dluma_ui_custom_menu_source_label(",
        "static void dluma_ui_custom_menu_populate_list(void *screen)",
    )
    assert "project_root" not in source_label
    assert "DLUMA_UI_SOURCE_OPTION_CODE_UNIT_LIMIT" in source_label
    assert "dluma_utf8_to_wide" in source_label
    assert "dluma_wide_to_utf8" in source_label
    assert 'candidate->source_name[0]' in source_label
    assert 'candidate->workshop_id[0]' in source_label


def test_unapproved_selected_provider_renders_off_and_requires_review() -> None:
    ui = text("src/dlw_ui.c")
    add_row = block(
        ui,
        "static void *dluma_ui_registry_add_option_row(",
        "static void dluma_ui_registry_add_group_header(",
    )
    buttons = block(
        ui,
        "static void dluma_ui_registry_update_button_labels(void)",
        "static int dluma_ui_registry_validate_list(",
    )
    assert "lua->enabled_requested && lua->approval_valid" in add_row
    assert "native, native->requested_host_flags" in add_row
    assert "(unsigned int)requested" in add_row
    assert "managed->enabled_requested && managed->approval_valid" in add_row
    assert "native->requested_host_flags" in buttons
    assert '"DLUMA_Common_Review", "REVIEW"' in buttons


def test_custom_menu_selection_is_exact_id_and_source() -> None:
    custom = text("src/dlw_custom_main_menu.c")
    ui = text("src/dlw_ui.c")
    assert 'L"active_source"' in custom
    assert "dluma_custom_menu_find_id_source" in custom
    assert "requested_source" in custom
    assert "target_id, target_source" in ui
    assert "g_dluma_ui_custom_menu_source_candidates" in ui
    assert "dluma_ui_custom_menu_select_source_option" in ui


def test_player_only_native_row_reports_enabled_scope_and_details_keep_host_wording() -> None:
    ui = text("src/dlw_ui.c")
    main_state = block(
        ui,
        "static void dluma_ui_native_row_state_text(",
        "static int dluma_ui_registry_add_native_host_option(",
    )
    host_text = block(
        ui,
        "static void dluma_ui_native_host_state_text(",
        "static void dluma_ui_native_row_state_text(",
    )
    assert '"DLUMA_Common_Enabled", "ENABLED"' in main_state
    assert 'dluma_str_append(out, capacity, " - ")' in main_state
    assert '"DLUMA_Common_Player"' in host_text


def test_all_locales_define_source_picker_messages() -> None:
    locale_root = ROOT / "PakAssets" / "Locale"
    catalogs = sorted(locale_root.glob("*/loader_texts_all.scr"))
    assert len(catalogs) == 17
    keys = (
        "DLUMA_ModDetails_Category_Source",
        "DLUMA_ModDetails_SourceConflict",
        "DLUMA_ModDetails_SourceHint",
        "DLUMA_Mods_SourceRequired",
        "DLUMA_Mods_SourceMissing",
        "DLUMA_Mods_SourceSaveFailed",
        "DLUMA_Mods_SourceApplied",
        "DLUMA_Mods_SourceRestart",
    )
    for catalog in catalogs:
        contents = catalog.read_text(encoding="utf-8")
        for key in keys:
            assert f'String("{key}",' in contents, (catalog, key)


def test_fixed_registry_rows_remain_focusable_and_use_short_status() -> None:
    source = text("src/dlw_ui.c")
    fixed_status = source[
        source.index("static void dluma_ui_registry_fixed_status") :
        source.index("static void *dluma_ui_registry_add_option_row")
    ]
    fixed_row = source[
        source.index("    if (fixed)\n    {", source.index("static void *dluma_ui_registry_add_option_row")) :
        source.index("    if (native)\n    {", source.index("static void *dluma_ui_registry_add_option_row"))
    ]

    assert '"DLUMA_Mods_SourceRequired"' in fixed_status
    assert '"DLUMA_Mods_SourceMissing"' not in fixed_status
    assert "dluma_ui_enable_single_option_row(item)" in fixed_row
    assert "mod_registry_fixed_single_option_enable_failed" in fixed_row
