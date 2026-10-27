from __future__ import annotations

import hashlib
import json
import struct
import subprocess
import zipfile
from pathlib import Path

from tools.local_test_paths import DEVTOOLS_ROOT


ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "src"
UI = SRC / "dlw_ui.c"
DISCOVERY = SRC / "dlw_custom_main_menu.c"
CORE = SRC / "dlw_custom_main_menu_core.c"
RUNTIME = SRC / "dlw_custom_main_menu_runtime.c"
MUSIC = SRC / "dlw_custom_main_menu_music.c"
MEDIA = SRC / "dlw_player_media_adapter.c"
EXPORTS = SRC / "dlw_exports.c"
ENGINE = SRC / "dlw_engine.c"
PACK = SRC / "dlw_pack.c"
SELECTOR = ROOT / "PakAssets/XUI/MenuDLUMACustomMainMenu.xui"
MODS = ROOT / "PakAssets/XUI/MenuDLUMAMods.xui"
PAK = ROOT / "build/package/Release/dluma.pak"


def _source(path: Path) -> str:
    return path.read_text(encoding="utf-8")


def _block(text: str, begin: str, end: str) -> str:
    start = text.index(begin)
    return text[start : text.index(end, start)]


def _function_body(text: str, signature: str) -> str:
    start = text.index(signature)
    opening = text.index("{", start)
    depth = 0
    for index in range(opening, len(text)):
        if text[index] == "{":
            depth += 1
        elif text[index] == "}":
            depth -= 1
            if depth == 0:
                return text[start : index + 1]
    raise AssertionError(f"unterminated function: {signature}")


def _write_neutral_menu_fixture(root: Path) -> Path:
    manifest = {
        "meta": {"id": "example.syntheticmenu", "author": "Example"},
        "general": {
            "name": "Synthetic Menu",
            "description": "Portable generated custom-menu fixture.",
            "version": "1.0.0",
            "is3DMenuEnabled": False,
            "doesUse3DCameraMovement": False,
            "useCustomBanks": False,
            "useCustomTextures": True,
        },
        "scene": {"map": "menu_scene"},
        "cameras": [{"gameObject": ["camera_helper", 7]}],
        "textures": [{"def": "ui_theme.def"}],
        "menus": [
            {
                "name": "main_screen",
                "overrides": "MenuMain_PC_workshop_with_B_Mods",
            },
            {"name": "secondary_screen", "overrides": "AddedScreen"},
        ],
    }
    (root / "data/menu/dluma").mkdir(parents=True)
    (root / "data/menu/texturedefs").mkdir(parents=True)
    (root / "data/maps/menu_scene").mkdir(parents=True)
    (root / "data/FX/ambient_glow").mkdir(parents=True)
    (root / "data/import_default").mkdir(parents=True)
    (root / "data/textures").mkdir(parents=True)
    (root / "assets_pc/import_default").mkdir(parents=True)
    (root / "assets_pc/textures").mkdir(parents=True)
    (root / "custommenu.json").write_text(
        json.dumps(manifest), encoding="utf-8"
    )
    main_controls = "".join(
        f"<XuiNavButton><Id>{control}</Id></XuiNavButton>"
        for control in ("B_PlayCustomMap", "B_Options", "B_Extras", "B_Mods")
    )
    (root / "data/menu/dluma/main_screen.xui").write_text(
        f"<XuiCanvas><XuiScene><Id>Menu</Id>{main_controls}</XuiScene></XuiCanvas>",
        encoding="utf-8",
    )
    (root / "data/menu/dluma/secondary_screen.xui").write_text(
        "<XuiCanvas><XuiScene><Id>Secondary</Id></XuiScene></XuiCanvas>",
        encoding="utf-8",
    )
    (root / "data/menu/texturedefs/ui_theme.def").write_text(
        "TextureDefinition(\"ui_button\", \"ui_button.dds\")\n",
        encoding="utf-8",
    )
    (root / "assets_pc/import_default/menu_prop.msh_obj").write_bytes(
        b"synthetic-compiled-mesh"
    )
    (root / "data/import_default/menu_prop.msh").write_bytes(b"synthetic-mesh")
    (root / "data/FX/ambient_glow/emitter.fx").write_text(
        "SyntheticEmitter()\n", encoding="utf-8"
    )
    (root / "data/textures/ui_button.dds").write_bytes(b"synthetic-dds")
    (root / "assets_pc/textures/ui_button.dds_obj").write_bytes(
        b"synthetic-compiled-texture"
    )
    (root / "assets_pc/local_dx11.mp").write_bytes(b"synthetic-material-pack")
    map_payload = b"LevelDI\0camera_helper\0menu_prop.msh\0Ambient Glow\0"
    map_bytes = bytearray(14 + len(map_payload))
    struct.pack_into("<I", map_bytes, 2, len(map_bytes) - 6)
    struct.pack_into("<H", map_bytes, 6, 0x77)
    struct.pack_into("<I", map_bytes, 8, len(map_payload) + 2)
    struct.pack_into("<H", map_bytes, 12, len(b"LevelDI"))
    map_bytes[14:] = map_payload
    map_root = root / "data/maps/menu_scene"
    for extension in ("map", "exp"):
        (map_root / f"menu_scene.{extension}").write_bytes(map_bytes)
    (map_root / "menu_scene.mis").write_text(
        "menu_scene{LevelDI}\n", encoding="utf-8"
    )
    return root / "custommenu.json"


def _pe_rva_reader(path: Path):
    data = path.read_bytes()
    nt = struct.unpack_from("<I", data, 0x3C)[0]
    assert data[nt : nt + 4] == b"PE\0\0"
    section_count = struct.unpack_from("<H", data, nt + 6)[0]
    timestamp = struct.unpack_from("<I", data, nt + 8)[0]
    optional_size = struct.unpack_from("<H", data, nt + 20)[0]
    optional = nt + 24
    image_size = struct.unpack_from("<I", data, optional + 56)[0]
    table = optional + optional_size
    sections: list[tuple[int, int, int, int]] = []
    for index in range(section_count):
        entry = table + index * 40
        virtual_size, rva, raw_size, raw = struct.unpack_from(
            "<IIII", data, entry + 8
        )
        sections.append((rva, max(virtual_size, raw_size), raw, raw_size))

    def read(rva: int, size: int) -> bytes:
        for start, span, raw, raw_size in sections:
            if start <= rva and rva + size <= start + span:
                offset = raw + rva - start
                assert rva - start + size <= raw_size
                return data[offset : offset + size]
        raise AssertionError(f"unmapped RVA 0x{rva:X}")

    return data, timestamp, image_size, read


def test_custom_main_menu_core_fake_harness(tmp_path: Path) -> None:
    subprocess.run(
        [
            "cmd.exe",
            "/d",
            "/s",
            "/c",
            "call tools\\build_custom_main_menu_fake_tests.bat",
        ],
        cwd=ROOT,
        check=True,
    )
    manifest = _write_neutral_menu_fixture(tmp_path / "synthetic-menu")
    subprocess.run(
        [
            str(
                ROOT
                / "build"
                / "custom-main-menu-fake"
                / "custom_main_menu_core_fake_harness.exe"
            ),
            str(manifest),
        ],
        cwd=ROOT,
        check=True,
    )


def test_discovery_is_bounded_deterministic_and_fail_closed() -> None:
    source = _source(DISCOVERY)
    header = _source(SRC / "dlw_custom_main_menu_core.h")
    assert "#define DLUMA_CUSTOM_MENU_MANIFEST_MAX_BYTES 65536U" in header
    assert "#define DLUMA_CUSTOM_MENU_MAX_CANDIDATES 128U" in header
    assert "dluma_custom_menu_sort_candidates();" in source
    assert "dluma_custom_menu_invalidate_duplicate_ids();" in source
    assert "duplicate meta.id within the same source is ambiguous" in source
    assert "snapshot.source_identity" in source
    assert "g_dluma_custom_menu_candidate_overflow = 1U" in source
    assert "dluma_custom_menu_invalidate_capacity_overflow();" in source
    assert "custom menu project count exceeds the supported limit" in source
    add_candidate = _block(
        source,
        "static void dluma_custom_menu_add_candidate",
        "static void dluma_custom_menu_invalidate_capacity_overflow",
    )
    assert add_candidate.index("dluma_custom_menu_root_already_known") < (
        add_candidate.index("g_dluma_custom_menu_candidate_count >=")
    )
    assert add_candidate.index("g_dluma_custom_menu_candidate_count >=") < (
        add_candidate.index("++g_dluma_custom_menu_candidate_count")
    )


def test_discovery_and_staging_use_handle_identity_and_exact_bytes() -> None:
    source = _source(DISCOVERY)
    assert "FILE_FLAG_OPEN_REPARSE_POINT" in source
    assert "information.nNumberOfLinks != 1U" in source
    assert "dluma_get_final_path_checked" in source
    assert "dluma_custom_menu_wide_under(canonical_out, canonical_root)" in source
    identity = _block(
        source,
        "static int dluma_custom_menu_identity_equal",
        "static int dluma_custom_menu_open_directory",
    )
    for field in (
        "dwVolumeSerialNumber",
        "nFileIndexHigh",
        "nFileIndexLow",
        "nFileSizeHigh",
        "nFileSizeLow",
        "ftLastWriteTime",
        "nNumberOfLinks",
    ):
        assert field in identity
    read = _block(
        source,
        "static int dluma_custom_menu_read_handle",
        "static int dluma_custom_menu_open_manifest",
    )
    assert "ReadFile(file, bytes" in read
    assert "GetFileInformationByHandle(file, &after)" in read
    assert "dluma_custom_menu_identity_equal(before, &after)" in read
    copy = _block(
        source,
        "static int dluma_custom_menu_overlay_payload",
        "static int dluma_custom_menu_overlay_count_files",
    )
    assert "dluma_custom_menu_source_still_same(record)" in copy
    assert "BYTE buffer[1024U * 1024U]" not in copy
    assert "DLUMA_CUSTOM_MENU_STAGE_COPY_BUFFER_BYTES" in copy
    assert "dluma_free(buffer)" in copy
    assert "custom_main_menu_overlay_copy_asset" in source
    assert "custom_main_menu_overlay_copy_failure" in source
    assert "custom_main_menu_overlay_copy_win32_error" in source
    assert "dluma_custom_menu_validate_declared_texture_defs" in source
    assert '"declared texture definition is missing or empty: "' in source
    assert "record->source_size == 0ULL" in source
    assert "dluma.custom-main-menu.loose-overlay.v9" in source
    overlay = _block(
        source,
        "static int dluma_custom_menu_materialize_overlay",
        "static void dluma_custom_menu_receipt_path",
    )
    assert "#define DLUMA_CUSTOM_MENU_TEMP_FINGERPRINT_CHARS 16U" in source
    assert 'L"custom_main_menu\\\\overlays"' in overlay
    assert 'L".tmp."' in overlay
    assert "fingerprint_prefix" in overlay
    assert "CreateDirectoryW(temporary_overlay" in overlay
    assert "CREATE_NEW" in overlay
    assert "FlushFileBuffers(output)" in overlay
    assert "MoveFileExW(" in overlay
    assert "temporary_overlay, final_overlay" in overlay
    assert "MOVEFILE_WRITE_THROUGH" in overlay
    assert "dluma_custom_menu_quarantine_invalid_overlay" in source
    quarantine = _block(
        source,
        "static int dluma_custom_menu_quarantine_invalid_overlay",
        "static int dluma_custom_menu_materialize_overlay",
    )
    assert "dluma_custom_menu_directory_is_under_root" in quarantine
    assert 'L".stale."' in quarantine
    assert "MoveFileExW(" in quarantine
    assert "invalid_generated_overlay_quarantined" in quarantine
    assert "RemoveDirectoryW" not in quarantine
    assert "DeleteFileW" not in quarantine
    assert overlay.index("dluma_custom_menu_validate_overlay(") < overlay.index(
        "dluma_custom_menu_quarantine_invalid_overlay("
    )
    validate = _block(
        source,
        "static int dluma_custom_menu_validate_overlay",
        "static void dluma_custom_menu_cleanup_temporary_overlay",
    )
    assert "size != record->output_size" in validate
    assert "dluma_sha256_handle_hex" in validate
    assert "dluma_stricmp(sha256, record->output_sha256)" in validate
    assert "actual_count != expected_count" in validate
    assert "canonical_root," in validate
    assert (
        "canonical_root, canonical_root, 0U, &actual_count"
        in validate
    )
    assert "legacy MAX_PATH" in validate


def test_loose_project_scan_and_overlay_promote_only_map_and_exp() -> None:
    source = _source(DISCOVERY)
    scan = _block(
        source,
        "static int dluma_custom_menu_scan_runtime_tree",
        "static void dluma_custom_menu_inventory_release",
    )
    roots = _block(
        source,
        "static int dluma_custom_menu_build_inventory",
        "static int dluma_custom_menu_materialize_declared_banks",
    )
    assert 'candidate, L"data", 1' in roots
    assert 'candidate, L"assets_pc", 0' in roots
    assert "runtime_prefix" in scan
    assert 'L"data\\\\maps\\\\"' in scan
    assert "expected_map" in scan and "expected_exp" in scan
    assert scan.count("promote = 1U") == 2
    assert "DLUMA_CUSTOM_MENU_MAX_RUNTIME_FILE_BYTES" in scan
    assert "data_file_count" in scan
    assert "assets_pc_file_count" in scan
    assert "fx_definition_count" in scan
    assert "material_pack_count" in scan
    assert "compiled_mesh_count" in scan
    assert "scripts\\\\lua" in source
    for suffix in (
        ".lua",
        ".bak",
        ".ba1",
        ".ba2",
        ".ba3",
        ".log",
        ".tmp",
        ".temp",
        ".old",
        ".orig",
        ".rej",
        "_log_err",
    ):
        assert f'L"{suffix}"' in source
    declared = _block(
        source,
        "static int dluma_custom_menu_add_declared_assets",
        "static int dluma_custom_menu_build_inventory",
    )
    assert "data\\\\menu\\\\scr" in declared
    assert "data\\\\custom_main_menu" in declared
    assert "manifest.overrides[index].destination" in declared
    assert "manifest.banks[index].path" in declared
    assert "DLUMA_CUSTOM_MENU_MAX_BANK_TOTAL_BYTES" in declared
    assert "assets_pc" not in declared
    assert "scripts\\\\lua" not in declared
    assert "custommenu.json" not in declared
    assert "dluma.custom-main-menu.loose-overlay.v9" in source
    assert "DLUMA_CUSTOM_MENU_MAIN_XUI_DIGEST_CHARS 16U" in source
    assert "dluma_custom_menu_main_xui_alias" in declared
    assert "#define DLUMA_CUSTOM_MENU_MAX_STAGE_FILES 4096U" in source
    assert "(512ULL * 1024ULL * 1024ULL)" in source
    add = _block(
        source,
        "static int dluma_custom_menu_inventory_add",
        "static int dluma_custom_menu_scan_runtime_tree",
    )
    assert '"duplicate staged destination: "' in add
    assert "overlay_output" in add
    assert "overlay_file_count" in add
    assert "information.nNumberOfLinks != 1U" in source
    assert "FILE_ATTRIBUTE_REPARSE_POINT" in scan
    overlay_materialization = _block(
        source,
        "static int dluma_custom_menu_materialize_overlay",
        "static void dluma_custom_menu_receipt_path",
    )
    assert 'L"custom_main_menu\\\\stages"' not in overlay_materialization
    assert 'L".pak"' not in overlay_materialization


def test_custom_menu_marker_always_hides_and_denies_custom_map_launch() -> None:
    source = _source(DISCOVERY)
    ui = _source(UI)
    marker = _block(
        source,
        "int dluma_custom_main_menu_project_marked",
        "int dluma_custom_main_menu_project_identity_marked",
    )
    assert "dluma_custom_menu_marker_exists(project_root)" in marker
    hide = _block(
        ui,
        "static int dluma_ui_workshop_row_rejected_by_game_fix",
        "static unsigned int dluma_ui_workshop_filter_invalid_projects",
    )
    assert "dluma_custom_main_menu_project_marked(metadata.project_root)" in hide
    # Marker hiding is independent from the optional invalid-project game fix.
    assert hide.index("dluma_custom_main_menu_project_marked") < hide.index(
        "hide_invalid_workshop_projects_enabled"
    )
    direct = _block(
        ui,
        "static int dluma_ui_workshop_custom_project_launch_allowed",
        "static void dluma_ui_project_launch_resolve_caller",
    )
    assert "dluma_custom_main_menu_project_identity_marked" in direct
    assert "dluma_ui_present_custom_main_menu_launch_denial(screen)" in direct
    premount = _block(
        ui,
        "static ULONG_PTR dluma_ui_project_premount_hook",
        "static ULONG_PTR __fastcall dluma_ui_custom_game_premount_hook",
    )
    assert "dluma_custom_main_menu_project_identity_marked" in premount
    assert "custom_main_menu_project_denied" in premount
    assert "hook_state->premount_original(screen, argument)" not in premount[
        premount.index("custom_main_menu_project_denied") - 500 :
        premount.index("custom_main_menu_project_denied") + 100
    ]


def test_asset_category_and_selector_are_native_bounded_and_recoverable() -> None:
    ui = _source(UI)
    mods = _source(MODS)
    selector_raw = SELECTOR.read_bytes()
    selector = selector_raw.decode("utf-8")
    assert "#define DLUMA_REGISTRY_CATEGORY_ALL 0U" in ui
    assert "#define DLUMA_REGISTRY_CATEGORY_ASSET 3U" in ui
    assert "#define DLUMA_REGISTRY_CATEGORY_COUNT 4U" in ui
    assert "DLUMA_ASSET_MOD_KIND_MAIN_MENU" in ui
    assert "DLUMA_REGISTRY_ROW_KIND_ASSET" in ui
    labels = _block(
        ui,
        "static void dluma_ui_registry_update_button_labels",
        "static int dluma_ui_registry_validate_list",
    )
    assert "toggle_disabled = 1" in labels
    assert "configure_disabled = 0" in labels
    assert "g_dluma_ui_registry_custom_menu_configure_button, asset" in labels
    assert "<Id>B_CustomMenuConfigure</Id>" in mods
    assert "<PressPath>MenuDLUMACustomMainMenu.xui</PressPath>" in mods

    assert not selector_raw.startswith(b"\xef\xbb\xbf")
    assert b"\r\n" in selector_raw
    assert b"\n" not in selector_raw.replace(b"\r\n", b"")
    assert "<Id>MenuDLUMACustomMainMenu</Id>" in selector
    assert "<ClassOverride>MenuSteamWorkshop</ClassOverride>" in selector
    assert "<Visual>ListBoxOptionsV</Visual>" in selector
    assert "<PressPath>" not in selector
    selector_start = selector[
        selector.index("<Id>B_Start</Id>") : selector.index("<Id>B_Back</Id>")
    ]
    assert "<UnfocusedInput>true</UnfocusedInput>" in selector_start
    assert "<PressKey>22528</PressKey>" in selector_start
    for object_id in (
        "L_Maps",
        "B_Start",
        "B_Back",
        "B_Workshop",
        "T_CustomMenuHintHeader",
        "T_CustomMenuHintBody",
        "T_CustomMenuStatus",
    ):
        assert f"<Id>{object_id}</Id>" in selector

    assert "#define DLUMA_UI_CUSTOM_MENU_VISIBLE_ROWS 9U" in ui
    assert "DLUMA_CUSTOM_MENU_MAX_CANDIDATES + 1U" in ui
    populate = _block(
        ui,
        "static void dluma_ui_custom_menu_populate_list",
        "static int dluma_ui_custom_menu_select_pointer_item",
    )
    assert '"base_game", "", 1, 0, 1,' in populate
    assert "g_dluma_ui_custom_menu_source_identities" in populate
    assert "index < g_dluma_ui_custom_menu_candidate_count" in populate
    assert "DLUMA_UI_CUSTOM_MENU_VISIBLE_ROWS" in populate
    assert "g_dluma_list_set_capacity" in populate
    assert "candidate->valid" in populate
    assert "candidate->quarantined" in populate
    assert "custom_main_menu_selector_rows_truncated" in populate
    assert "DLUMA_CustomMenu_RestartRequired" in ui
    assert "DLUMA_CustomMenu_QuarantinedHint" in ui
    assert "DLUMA_Common_Unavailable" in ui
    assert "dluma_custom_main_menu_select_next_launch" in ui


def test_single_value_custom_menu_rows_remain_enabled_on_exact_layout() -> None:
    ui = _source(UI)
    for contract in (
        "#define DLUMA_DEVTOOLS_OPTION_CAN_BE_ENABLED_RVA 0x0112EA80UL",
        "#define DLUMA_UI_OPTION_FLAGS_OFFSET 0x00CCUL",
        "#define DLUMA_UI_OPTION_ALLOW_SINGLE_VALUE_FLAG 0x80U",
        "#define DLUMA_UI_OPTION_COUNT_OFFSET 0x00E0UL",
    ):
        assert contract in ui
    helper = _block(
        ui,
        "static int dluma_ui_enable_single_option_row",
        "static void dluma_ui_set_text_element_text",
    )
    assert "g_dluma_option_single_value_layout_valid" in helper
    assert "dluma_ui_range_is_writable(flags, sizeof(*flags))" in helper
    assert "dluma_is_readable(count_address, sizeof(option_count))" in helper
    assert "option_count != 1U" in helper
    assert "*flags | DLUMA_UI_OPTION_ALLOW_SINGLE_VALUE_FLAG" in helper
    assert "dluma_ui_set_element_disabled(item, 0)" in helper

    install = _block(
        ui,
        "static int dluma_ui_install_registry_hooks",
        "static void dluma_ui_restore_registry_hooks_now",
    )
    assert "option_can_be_enabled_prefix" in install
    assert (
        'DLUMA_VALIDATE_PREFIX("option_can_be_enabled_prefix", '
        "DLUMA_DEVTOOLS_OPTION_CAN_BE_ENABLED_RVA, option_can_be_enabled_prefix)"
        in install
    )
    assert "g_dluma_option_single_value_layout_valid = 1" in install
    assert "g_dluma_option_single_value_layout_valid = 0" in ui

    asset = _block(
        ui,
        "static DlumaRegistryRowMap *dluma_ui_registry_add_menu_row",
        "static void dluma_ui_registry_add_main_menu_rows",
    )
    assert "dluma_ui_enable_single_option_row(item)" in asset
    selector = _block(
        ui,
        "static void *dluma_ui_custom_menu_add_row",
        "static void dluma_ui_custom_menu_populate_list",
    )
    assert "if (valid || base_game)" in selector
    assert "(void)dluma_ui_enable_single_option_row(item)" in selector


def test_selector_uses_native_history_and_consumes_all_launch_activation() -> None:
    ui = _source(UI)
    registration = _block(
        ui,
        "static int dluma_ui_register_custom_menu_screen",
        "static int dluma_ui_register_mod_config_screen",
    )
    assert "script_path_text[]" in registration
    assert '"MenuDLUMACustomMainMenu.xui"' in registration
    assert "register_screen(" in registration
    assert "load_ui_screen" not in registration
    configure = _block(
        ui,
        'else if (dluma_ui_object_id_equals(\n                     button, "B_CustomMenuConfigure"))',
        'else if (dluma_ui_object_id_equals(button, "B_Configure"))',
    )
    assert "dluma_ui_show_next_registered_screen" in configure
    assert "native ShowNextUI history" in configure
    selector_button = _block(
        ui,
        "else if (!g_dluma_registry_hooks_stopping &&\n             !g_dluma_registry_runtime_disabled && !g_dluma.stop_requested &&\n             dluma_ui_is_custom_menu_screen(screen))",
        "else if (!g_dluma_registry_hooks_stopping &&\n             !g_dluma_registry_runtime_disabled && !g_dluma.stop_requested &&\n             dluma_ui_is_validated_quest_log_screen(screen))",
    )
    assert "dluma_ui_custom_menu_select_pointer_item(button)" in selector_button
    assert "dluma_ui_custom_menu_apply_selection()" in selector_button
    assert "if (original)\n                original(screen, button, event_code);" in selector_button
    assert "This selector is not a Workshop launcher" in selector_button
    option_selected = _block(
        ui,
        "static void __fastcall dluma_ui_workshop_option_selected_hook",
        "static void dluma_ui_registry_log_toggle",
    )
    custom_option = option_selected[
        option_selected.index("dluma_ui_is_custom_menu_screen(screen)") :
        option_selected.index("dluma_ui_is_loader_settings_screen(screen)")
    ]
    assert "dluma_ui_custom_menu_select_source_option(option_control)" in custom_option
    assert "source_result == 2" in custom_option
    assert "dluma_ui_custom_menu_apply_selection()" in custom_option
    double_click = _block(
        ui,
        "static void __fastcall dluma_ui_workshop_double_hook",
        "static unsigned char __fastcall dluma_ui_workshop_uses_3d_rotation_hook",
    )
    assert "dluma_ui_is_custom_menu_screen(screen)" in double_click
    assert "dluma_ui_custom_menu_apply_selection()" in double_click
    assert "Workshop map launch suppressed" in double_click


def test_selector_selected_row_is_an_explicit_toggle_back_to_base_game() -> None:
    ui = _source(UI)
    update = _block(
        ui,
        "static void dluma_ui_custom_menu_update_for_row",
        "static void *dluma_ui_custom_menu_add_row",
    )
    assert "row_is_requested" in update
    assert '"DLUMA_Common_Disable", "DISABLE"' in update
    assert "select_disabled = !candidate->valid" in update
    rows = _block(
        ui,
        "static void *dluma_ui_custom_menu_add_row",
        "static void dluma_ui_custom_menu_populate_list",
    )
    assert "int valid, int quarantined, int base_game, int selected" in rows
    assert '"DLUMA_Common_Selected", "SELECTED"' in rows
    apply = _block(
        ui,
        "static int dluma_ui_custom_menu_apply_selection",
        "static int dluma_ui_settings_read_value",
    )
    assert 'target_id = "base_game"' in apply
    assert "custom_main_menu_duplicate_activation_suppressed" in apply
    assert "g_dluma_ui_custom_menu_post_generation" in apply
    assert "dluma_custom_main_menu_select_next_launch(\n            target_id, target_source" in apply
    assert "g_dluma_custom_menu_deferred_populate" in apply
    assert "g_dluma_custom_menu_saved_status_pending" in apply
    post = _block(
        ui,
        "static void __fastcall dluma_ui_workshop_post_hook",
        "static void __fastcall dluma_ui_workshop_init_hook",
    )
    assert "g_dluma_custom_menu_saved_status_pending" in post
    assert "++g_dluma_ui_custom_menu_post_generation" in post
    assert "DLUMA_CustomMenu_SavedRestart" in post


def test_selector_is_non_overrideable_and_custom_xui_stays_2d() -> None:
    core = _source(CORE)
    discovery = _source(DISCOVERY)
    ui = _source(SRC / "dlw_ui.c")
    assert "dluma_custom_main_menu_resolve_override_destination" in core
    assert 'alias = "DLUMA_Main_PC_workshop.xui"' in core
    assert 'alias = "DLUMA_SteamWorkshop.xui"' in core
    assert 'alias = "MenuExtras.xui"' in core
    assert 'dluma_str_append(destination, destination_capacity, ".xui")' in core
    assert 'dluma_custom_menu_equal(target, "MenuDLUMACustomMainMenu")' in core
    assert "#define DLUMA_CUSTOM_MENU_MAX_OVERRIDES 128U" in _source(
        SRC / "dlw_custom_main_menu_core.h"
    )
    assert "three_dimensional_xui_unsupported" in core
    resolver = _block(
        core,
        "int dluma_custom_main_menu_resolve_override_destination",
        "int dluma_custom_main_menu_xui_contract_valid",
    )
    assert "RegisterUIScreen" not in resolver
    assert "LoadUIScreen" not in resolver
    declared = _block(
        discovery,
        "static int dluma_custom_menu_add_declared_assets",
        "static int dluma_custom_menu_build_inventory",
    )
    assert "RegisterUIScreen" not in declared
    assert "LoadUIScreen" not in declared
    assert "menuscreen3dplacement" not in declared
    assert "menubackgroundcameraactions" not in declared
    assert "DLUMA_CUSTOM_MENU_MAIN_XUI_COMPAT" in declared
    assert "dluma_custom_menu_main_xui_alias" in declared
    assert "dluma_custom_main_menu_selected_main_xui_alias" in ui
    assert "g_dluma_main_menu_redirect_name" in ui
    assert ui.index("dluma_custom_main_menu_selected_main_xui_alias") < ui.index(
        "dluma_ui_prepare_xui_filename_redirect(",
        ui.index("int dluma_ui_prepare_main_menu_redirect"),
    )


def test_pack_mount_precedence_and_process_lifetime_ownership() -> None:
    exports = _source(EXPORTS)
    engine = _source(ENGINE)
    runtime = _source(RUNTIME)
    runtime_header = _source(SRC / "dlw_custom_main_menu_runtime.h")
    pack = _source(PACK)
    menu = _source(DISCOVERY)
    discovery = _source(DISCOVERY)
    loader_mount = exports.index("pack_mounted = dluma_mount_content_pack()")
    runtime_start = exports.index("dluma_custom_main_menu_runtime_start", loader_mount)
    source_prepare = exports.index(
        "dluma_custom_main_menu_prepare_selected_sources", runtime_start
    )
    original_init = exports.index("manager = g_original_initialize()", source_prepare)
    initialize_ui = _function_body(
        runtime, "static __int64 __fastcall dluma_cmm_main_initialize_ui_hook"
    )
    publish_generation = _function_body(
        runtime, "static const char *dluma_cmm_publish_frontend_generation("
    )
    load_hook = _block(
        runtime,
        "static void __fastcall dluma_cmm_load_hook",
        "static __int64 __fastcall dluma_cmm_unload_hook",
    )
    assert loader_mount < runtime_start < source_prepare < original_init
    assert "dluma_cmm_publish_frontend_generation(" in initialize_ui
    assert "dluma_cmm_run_stock_premount()" in publish_generation
    assert "dluma_cmm_validate_stock_publication()" in publish_generation
    assert "dluma_cmm_mount_overlay_after_catalogs()" in publish_generation
    assert publish_generation.index("dluma_cmm_run_stock_premount()") < (
        publish_generation.index("dluma_cmm_validate_stock_publication()")
    ) < publish_generation.index("dluma_cmm_mount_overlay_after_catalogs()")
    assert initialize_ui.count("original(module)") == 1

    publication_validation = _function_body(
        runtime, "static int dluma_cmm_validate_stock_publication(void)\n{"
    )
    assert publication_validation.index(
        "dluma_custom_main_menu_native_sources_ready()"
    ) < publication_validation.index(
        "dluma_custom_main_menu_capture_published_sources()"
    )
    assert "custom_map_asset_isolation" not in publication_validation
    assert "DlumaCmmInitializeUiFn" in runtime
    assert "DLUMA_CMM_MAIN_INITIALIZE_UI_RVA 0x011B8E60UL" in runtime
    assert "DLUMA_CMM_MAIN_INITIALIZE_UI_SLOT_RVA 0x01678FE0UL" in runtime
    assert "install_main_ui_initializer_hook" in runtime
    assert "restore_main_ui_initializer_hook" in runtime
    assert "dluma_custom_main_menu_runtime_preworker_tick" not in runtime_header
    assert "dluma_custom_main_menu_runtime_preworker_tick" not in engine
    assert "dluma_cmm_process_deferred_publication" not in runtime
    assert "dluma_custom_main_menu_runtime_defer_show_menu_main" not in runtime_header
    assert "Sleep(" not in load_hook
    assert "background_resource_settle" not in load_hook
    assert "runtime_resource_settle_remaining" not in runtime
    assert "pending_stock_premount_and_ui" not in load_hook
    assert "repeated_request_coalesced" not in load_hook
    assert "dluma_mount_custom_main_menu_sources" not in exports
    assert "dluma_custom_main_menu_mount_selected_sources" not in exports
    assert "dluma_custom_main_menu_mount_selected_sources" in runtime
    mount_after_catalogs = _function_body(
        runtime, "static int dluma_cmm_mount_overlay_after_catalogs(void)\n{"
    )
    assert "dluma_custom_main_menu_mount_selected_sources" in mount_after_catalogs
    assert "dluma_adopt_custom_main_menu_project_sources" not in pack
    assert "dluma_adopt_custom_main_menu_project_sources" not in menu
    source_prepare = _block(
        pack,
        "int dluma_prepare_custom_main_menu_sources",
        "int dluma_mount_custom_main_menu_overlay",
    )
    assert source_prepare.index("project_root, fingerprint, 1,") < (
        source_prepare.index("data_root, fingerprint, 1,")
    ) < source_prepare.index("assets_pc_root, fingerprint, 0,")
    assert "DLUMA_SOURCE_OWNER_CUSTOM_MAIN_MENU" in pack
    assert "DLUMA_PROCESS_SOURCE_RUNTIME_DIRECTORY" in pack
    assert "add_source(source_path, source_flags)" in pack
    assert "filesystem, add_source, overlay_root, fingerprint," in pack
    mount_overlay = _block(
        pack,
        "int dluma_mount_custom_main_menu_overlay",
        "static DlumaCustomMainMenuDirectorySource*",
    )
    assert "dluma_custom_main_menu_native_sources_ready()" not in mount_overlay
    assert "filesystem, add_source, overlay_root, fingerprint," in mount_overlay
    assert "9U, &g_dluma_custom_main_menu_overlay" in mount_overlay
    assert "project_root" not in mount_overlay
    assert "data_root" not in mount_overlay
    assert "assets_pc_root" not in mount_overlay
    mount_directory = _block(
        pack,
        "static int dluma_mount_custom_main_menu_directory",
        "static int dluma_prepare_custom_main_menu_directory",
    )
    assert mount_directory.index("dluma_pack_find_source_exact") < (
        mount_directory.index("add_source(source_path, source_flags)")
    )
    assert "adopted_existing_exact_engine_directory_source" in mount_directory
    assert "exact_source_missing_after_successful_add" in mount_directory
    assert "int dluma_mount_custom_main_menu_sources" not in pack
    assert "source_flags != 8U && source_flags != 9U" in pack
    assert "dluma_custom_main_menu_bind_native_source" in pack
    assert "dluma_custom_main_menu_capture_published_sources" in pack
    assert "dluma_pack_find_source_exact" in _block(
        pack,
        "int dluma_custom_main_menu_capture_published_sources",
        "int dluma_custom_main_menu_native_sources_ready",
    )
    capture_published = _block(
        pack,
        "int dluma_custom_main_menu_capture_published_sources",
        "static int dluma_custom_main_menu_native_sources_ready_locked",
    )
    assert "dluma_custom_main_menu_adopt_published_directory" in capture_published
    assert "dluma_custom_main_menu_native_sources_ready()" not in capture_published
    assert "dluma_custom_main_menu_bind_live_source_inventory" in publication_validation
    assert "dluma_custom_main_menu_refresh_live_sources" in publication_validation
    assert (
        "ownership_join_pending_custom_presentation_retained"
        in publication_validation
    )
    assert "return 1;" in publication_validation
    assert publication_validation.index(
        "dluma_custom_main_menu_capture_published_sources()"
    ) < publication_validation.index(
        "dluma_custom_main_menu_bind_live_source_inventory()"
    ) < publication_validation.index(
        "dluma_custom_main_menu_refresh_live_sources("
    )
    assert "adopted_exact_engine_published_directory_source" in pack
    assert "replaced_retired_engine_published_directory_source" in pack
    assert "dluma_custom_main_menu_native_sources_ready" in pack

    mount_selected = _function_body(
        menu, "int dluma_custom_main_menu_mount_selected_sources(void)\n{"
    )
    assert "live_ownership_pending_overlay_mount_continues" in mount_selected
    assert mount_selected.index(
        "live_ownership_pending_overlay_mount_continues"
    ) < mount_selected.index("dluma_mount_custom_main_menu_overlay(")
    assert "stock_premount_native_sources_incomplete" not in mount_selected

    assert (
        "live_join_incomplete_overlay_and_presentation_retained"
        in mount_after_catalogs
    )
    assert "!g_dluma_cmm_runtime.content_engine_sources_observed ||" not in (
        mount_after_catalogs
    )
    assert "custom_main_menu_native_source_conflict" in pack
    assert "dluma_process_lifetime_source_compare_replace" in pack
    assert "!g_dluma_custom_main_menu_project.conflicted" in pack
    assert "!g_dluma_custom_main_menu_data.conflicted" in pack
    assert "!g_dluma_custom_main_menu_assets_pc.conflicted" in pack
    assert "dluma_custom_main_menu_publication_scope_enter" in pack
    assert "dluma_custom_main_menu_publication_scope_active" in pack
    assert "DLUMA_CMM_STOCK_PROJECT_SOURCE_FLAGS 0x100U" in pack
    assert "DLUMA_CMM_STOCK_DATA_SOURCE_FLAGS 0x10DU" in pack
    assert "DLUMA_CMM_STOCK_ASSETS_PC_SOURCE_FLAGS 0x59U" in pack
    assert "dluma_custom_main_menu_expected_native_flags" in pack
    assert "DLUMA_SOURCE_OWNER_ID_CUSTOM_MAIN_MENU_PROJECT" in pack
    assert "DLUMA_SOURCE_OWNER_ID_CUSTOM_MAIN_MENU_DATA" in pack
    assert "DLUMA_SOURCE_OWNER_ID_CUSTOM_MAIN_MENU_ASSETS_PC" in pack
    assert "DLUMA_SOURCE_OWNER_ID_CUSTOM_MAIN_MENU_OVERLAY" in pack
    assert "dluma_custom_main_menu_selected_bank_source_snapshot" in discovery
    assert (
        "loader_pak_then_generated_overlay_before_frontend_then_engine_published_project_data_assets_with_overlay_final_precedence"
        in pack
    )
    assert 'project_root, L"data"' in pack
    assert 'project_root, L"assets_pc"' in pack
    assert "record.source_flags = source_flags" in pack
    assert "custom_main_menu_project_left_mounted_until_process_exit" in pack
    assert "custom_main_menu_overlay_left_mounted_until_process_exit" in pack
    isolation = _source(
        ROOT / "src/fixes/custom_map_asset_isolation/CustomMapAssetIsolationFix.cpp"
    )
    assert "g_normalized_custom_main_menu_bank_path" in isolation
    assert "g_normalized_custom_main_menu_data_path" in isolation
    assert "g_normalized_custom_main_menu_assets_pc_path" in isolation
    assert "dluma_custom_main_menu_bind_native_source" in isolation
    assert isolation.count("dluma_custom_main_menu_publication_scope_active()") >= 2
    assert "const bool direct_object_add = g_pending_source_add.depth == 0" in isolation
    assert "custom_main_menu_native_object_source_registration_failed" in isolation
    assert "normalized_live, source, flags" in isolation
    assert "DLUMA_SOURCE_OWNER_ID_CUSTOM_MAIN_MENU_BANK" in isolation
    assert isolation.count("expected_source_flags = 9U") >= 2
    unmount = _block(pack, "void dluma_unmount_content_pack", "int dluma_content_pack_snapshot")
    assert "rem_source" in unmount
    assert "Do not call fs::rem_source" in unmount
    assert "dluma_process_lifetime_source_register" in pack


def test_exact_runtime_bridge_is_profile_gated_and_ownership_safe() -> None:
    runtime = _source(RUNTIME)
    runtime_core = _source(SRC / "dlw_custom_main_menu_runtime_core.c")
    assert "DLUMA_CMM_LOAD_RVA 0x007547D0UL" in runtime
    assert "CBC7" not in runtime  # hashes live once in the pure profile contract
    assert (
        '"cbc7dd7923778936341cdfb755474a0c714079d910849df16ffc5ec88a103b95"'
        in _source(SRC / "dlw_custom_main_menu_runtime_core.h")
    )
    assert (
        '"61d9dfb20cfa179c6d097537da46ba9dbe3d58797938762ef4eacff5dfcc4b3a"'
        in _source(SRC / "dlw_custom_main_menu_runtime_core.h")
    )
    assert "dluma_custom_main_menu_runtime_profile_identity_valid" in runtime
    assert "DLUMA_CMM_STOCK_PREMOUNT_RVA 0x0091C6C0UL" in runtime
    assert "DLUMA_CMM_UPDATE_DLC_RVA" not in runtime
    assert "DLUMA_CMM_TOOLS_PREMOUNT_PREPARE_SLOT 0x1AC8UL" in runtime
    assert "DLUMA_CMM_TOOLS_PREMOUNT_PACKAGES_SLOT 0x1AE8UL" in runtime
    assert "DLUMA_CMM_TOOLS_PREMOUNT_PREPARE_RVA 0x0029CA00UL" in runtime
    assert "DLUMA_CMM_TOOLS_PREMOUNT_PACKAGES_RVA 0x0029CB70UL" in runtime
    assert "DLUMA_CMM_TOOLS_GET_SPEECH_RVA 0x0028DFB0UL" in runtime
    assert "DLUMA_CMM_TOOLS_GET_LOCALE_RVA 0x0028DF00UL" in runtime
    assert "DLUMA_CMM_TOOLS_PUBLISH_CONTENT_RVA 0x00299B50UL" in runtime
    assert "dluma_custom_main_menu_runtime_stock_premount_contract_valid" in runtime
    assert "snapshot_update_dlc_content_detour" not in runtime
    assert "g_dluma_cmm_update_dlc_prefix" not in runtime
    assert "dluma_custom_main_menu_runtime_engine_content_name" in runtime
    assert "engine_dos_path_without_extended_prefix" in runtime
    assert "dluma_custom_main_menu_runtime_activate_content" not in runtime
    assert "immutable_process_lifetime_project_root_ready" in runtime
    assert "SetCustomContentName" not in runtime
    assert "set_custom_content_name" not in runtime
    assert "runtime_register_content_before_frontend" not in runtime
    assert "runtime_prepare_content_on_frame" not in runtime
    assert "runtime_note_frame_completed" not in runtime
    publication = _block(
        runtime,
        "static int dluma_cmm_run_stock_premount(void)",
        "static void dluma_cmm_log_published_source",
    )
    assert publication.index("premount_prepare(") < publication.index(
        "premount_packages("
    )
    assert publication.index("premount_packages(") < publication.index(
        "get_speech_id("
    ) < publication.index("get_locale_id(") < publication.index(
        "publish_content("
    )
    assert "dluma_custom_main_menu_adopt_selected_project_sources" not in publication
    assert "g_dluma_cmm_runtime.update_dlc" not in runtime
    assert "&g_dluma_cmm_runtime.content_name" in publication
    assert (
        "premount_prepare(\n"
        "                tools, &g_dluma_cmm_runtime.package_name,\n"
        "                g_dluma_cmm_runtime.content_identity);"
    ) in publication
    assert publication.count("&g_dluma_cmm_runtime.package_name") >= 2
    assert "content_identity" in publication
    assert "MSVCR100.dll" in runtime
    assert "engine_free(locale.data)" in publication
    assert "engine_free(speech.data)" in publication
    assert "DLUMA_CMM_GAME_DLC_REGISTRY_OFFSET" not in runtime
    assert "DLUMA_CMM_DLC_REGISTRY_PROBE_SLOT" not in runtime
    assert "publication_scope_enter" in publication
    assert "publication_scope_leave" in publication
    assert "Sleep(" not in publication
    assert "frame_barrier" not in publication
    assert "g_dluma_cmm_load_prefix" in runtime
    assert "g_dluma_cmm_unload_prefix" in runtime
    assert "g_dluma_cmm_activate_prefix" in runtime
    assert "DLUMA_CMM_FX_ENABLE_RVA 0x0018CE20UL" in runtime
    assert '"?FXEnable@ILevel@@QEAAX_N@Z"' in runtime
    assert "g_dluma_cmm_fx_enable_prefix" in runtime
    assert "dluma_custom_main_menu_runtime_fx_enable_contract_valid" in runtime
    assert "dluma_custom_main_menu_runtime_fx_restore_should_apply" in runtime
    assert "DLUMA_CMM_RELOAD_MATERIALS_RVA" not in runtime
    assert '"?ReloadMaterials@ILevel@@QEAAXXZ"' not in runtime
    assert "dluma_cmm_reload_material_catalog" not in runtime
    assert "DLUMA_CMM_INIT_FX_SYSTEM_RVA" not in runtime
    assert '"?InitFXSystem@ILevel@@QEAAXXZ"' not in runtime
    assert "dluma_cmm_prepare_fx_before_activation" not in runtime
    assert "DLUMA_CMM_GET_PREV_RVA 0x002772D0UL" in runtime
    assert "DLUMA_CMM_GET_NEXT_RVA 0x002772F0UL" in runtime
    assert "DLUMA_CMM_GET_FX_MANAGER_RVA 0x00281C00UL" in runtime
    assert "DLUMA_CMM_FX_EMITTER_PRIMARY_VTABLE_RVA 0x01905738UL" in runtime
    assert "DLUMA_CMM_FX_EMITTER_IGSOBJECT_VTABLE_RVA 0x01905A08UL" in runtime
    assert "DLUMA_CMM_FX_EMITTER_LIFECYCLE_VTABLE_RVA 0x01906258UL" in runtime
    assert "DLUMA_CMM_FX_EMITTER_ON_ENABLED_RVA 0x010E09B0UL" in runtime
    assert "DLUMA_CMM_FX_EMITTER_ON_DISABLED_RVA 0x010E0AA0UL" in runtime
    assert '"?GetFXManager@IGSObject@@QEAAPEAVIFXManager@@XZ"' in runtime
    assert "g_dluma_cmm_get_fx_manager_prefix" in runtime
    assert "g_dluma_cmm_fx_emitter_on_enabled_prefix" in runtime
    assert "g_dluma_cmm_fx_emitter_on_disabled_prefix" in runtime
    assert "dluma_cmm_bind_fx_lifecycle(" in runtime
    assert "dluma_cmm_observe_fx_emitters_after_load" not in runtime
    fx_start_contract = _function_body(
        runtime, "static int dluma_cmm_fx_emitter_start_contract"
    )
    assert "DLUMA_CMM_FX_EMITTER_PRIMARY_VTABLE_RVA" in fx_start_contract
    assert "DLUMA_CMM_FX_EMITTER_IGSOBJECT_VTABLE_RVA" in fx_start_contract
    assert "DLUMA_CMM_FX_EMITTER_LIFECYCLE_VTABLE_RVA" in fx_start_contract
    assert "DLUMA_CMM_FX_EMITTER_INSTANCE_OFFSET" in fx_start_contract
    assert "require_unstarted && instance != 0U" in fx_start_contract
    assert "get_fx_manager(igs_object)" in fx_start_contract
    fx_start = _function_body(
        runtime, "static void dluma_cmm_start_fx_emitters_after_activation"
    )
    assert "fx_start_attempted" in fx_start
    assert "fx_start_generation" in fx_start
    assert "fx_started_emitters" in fx_start
    assert "fx_emitter_on_enabled(" in fx_start
    assert fx_start.index("++g_dluma_cmm_runtime.fx_started_emitter_count") < (
        fx_start.index("fx_emitter_on_enabled(")
    )
    fx_stop = _function_body(
        runtime, "static void dluma_cmm_stop_started_fx_emitters"
    )
    assert "fx_emitter_on_disabled(" in fx_stop
    assert "dluma_cmm_clear_started_fx_emitters()" in fx_stop
    assert "DLUMA_CMM_MESH_NAME_RVA" not in runtime
    assert "dluma_cmm_mesh_name_guard_hook" not in runtime
    assert '".?AVMenu3DBackgroundDI@@"' in runtime
    assert '".?AVCameraPostionHelper@@"' in runtime
    assert "#define DLUMA_CMM_CAMERA_POSITION_HELPER_TYPE_RVA 0x01CCD128UL" in runtime
    assert '".?AVCameraHelperMenuScene3dDI@@"' in runtime
    assert "dluma_cmm_restore_temporary_menu_name" in runtime
    custom_load = _function_body(
        runtime,
        "static int dluma_cmm_call_custom_load",
    )
    assert "__finally" in custom_load
    menu_name_swap = custom_load.index("dluma_cmm_prepare_temporary_menu_name")
    substitution = custom_load.index("custom_main_menu_background_level")
    original_custom_load = custom_load.index("original(module);", substitution)
    assert menu_name_swap < substitution < original_custom_load
    assert custom_load.index("original(module);") < custom_load.index(
        "dluma_cmm_restore_temporary_menu_name"
    )
    assert "dluma_cmm_same_ttl(game_name, replacement)" in runtime
    camera = _block(
        runtime,
        "static void dluma_cmm_apply_camera",
        "static void __fastcall dluma_cmm_load_hook",
    )
    assert "get_object_by_id(" in camera
    assert "background, manifest->camera_id" in camera
    assert "camera_level == background" in camera
    assert "dluma_cmm_object_name_is" in camera
    assert "dluma_custom_main_menu_runtime_camera_values_valid" in camera
    assert "get_fov(private_camera)" in camera
    assert "get_view_camera(" in camera
    assert "get_world_position(" in camera
    assert "get_up_vector(" in camera
    assert "get_forward_vector(" in camera
    assert camera.index("menu_background =") < camera.index(
        "dluma_cmm_enable_fx(menu_background, background)"
    ) < camera.index(
        "dluma_cmm_start_fx_emitters_after_activation("
    ) < camera.index("camera_object = g_dluma_cmm_runtime.get_object_by_id")
    assert camera.index("from_forward_up_pos") < camera.index("set_fov")
    assert camera.index("set_fov") < camera.index("force_frame")
    assert "set_camera_matrix" not in camera
    assert "CameraPostionHelper_background_camera_bridge" in camera
    assert '"background_view_camera"' in camera
    assert '"background_private_camera"' in camera
    assert "target_camera = private_camera" in camera
    assert "FlushInstructionCache" in runtime
    assert "dluma_atomic_pointer_compare_exchange" in runtime
    assert "_InterlockedCompareExchange128" in runtime
    assert "teardown_failed" in runtime
    assert "ownership_preserved && protection_restored" in runtime_core
    assert "instruction_cache_flushed" in runtime_core


def test_custom_texture_registration_and_movie_camera_contracts() -> None:
    header = _source(SRC / "dlw_custom_main_menu_core.h")
    core = _source(CORE)
    runtime = _source(RUNTIME)
    runtime_core = _source(SRC / "dlw_custom_main_menu_runtime_core.c")

    assert "#define DLUMA_CUSTOM_MENU_MAX_TEXTURE_DEFS 32U" in header
    assert "does_use_3d_camera_movement" in header
    assert "use_custom_textures" in header
    assert 'dluma_custom_menu_equal(key, "doesUse3DCameraMovement")' in core
    assert 'dluma_custom_menu_equal(key, "useCustomTextures")' in core
    assert '"data/menu/texturedefs/"' in core
    assert '"custom_texture_definitions_missing"' in core
    assert '"custom_texture_definitions_disabled"' in core
    assert '"custom_texture_definition_duplicate"' in core
    assert '"custom_texture_definition_unsafe"' in core

    initialize_ui = _function_body(
        runtime, "static __int64 __fastcall dluma_cmm_main_initialize_ui_hook"
    )
    publish_generation = _function_body(
        runtime, "static const char *dluma_cmm_publish_frontend_generation("
    )
    assert "dluma_cmm_publish_frontend_generation(" in initialize_ui
    assert publish_generation.index("dluma_cmm_run_stock_premount()") < (
        publish_generation.index("dluma_cmm_validate_stock_publication()")
    ) < publish_generation.index("dluma_cmm_mount_overlay_after_catalogs()")
    assert initialize_ui.count("original(module)") == 1

    # The loader gates one stock entry point before UI construction.  The only
    # additional UI seam is a non-suppressing +0x3B0 wrapper, because that is
    # where the validated frontend ILevel receiver exists.  It loads declared
    # definitions immediately before forwarding the original registration.
    register_ui = _function_body(
        runtime,
        "static void __fastcall dluma_cmm_main_register_ui_elements_hook",
    )
    assert register_ui.index("dluma_cmm_texture_definitions_load") < (
        register_ui.index("original(level)")
    )
    assert register_ui.count("original(level)") == 1

    # No stock LoadUiTextures, Bind, Show, or transition payload is suppressed
    # or replayed manually.
    for obsolete in (
        "dluma_cmm_main_load_ui_textures_hook",
        "dluma_cmm_main_bind_ui_elements_hook",
        "DLUMA_CMM_UI_PHASE_TEXTURES",
        "DLUMA_CMM_UI_PHASE_REGISTRATION",
        "DLUMA_CMM_UI_PHASE_BINDING",
        "main_bind_ui_elements_original",
        "dluma_cmm_replay_deferred_stock_ui",
        "dluma_cmm_ui_binding_cache_ready",
        "registered_screens_not_instantiated_retrying_bind",
        "stock_ui_binding_cache_deadline_expired",
    ):
        assert obsolete not in runtime

    texture_load = _block(
        runtime,
        "static int dluma_cmm_texture_definitions_load",
        "static int dluma_cmm_ttl_valid",
    )
    assert "dluma_custom_main_menu_runtime_texture_registration_matches" in texture_load
    assert "definition->name" in texture_load
    assert texture_load.index("g_dluma_cmm_runtime.load_ui_textures") < (
        texture_load.index("g_dluma_cmm_runtime.texture_definitions_loaded = 1U")
    )
    assert "for (index = 0; index < manifest->texture_def_count; ++index)" in texture_load
    assert '"loaded_before_ui_registration"' in texture_load
    assert "dluma_cmm_texture_definitions_unload()" in texture_load

    profile = _block(
        runtime,
        "static int dluma_cmm_validate_profile",
        "int dluma_custom_main_menu_runtime_start",
    )
    assert '"?LoadUITextures@ILevel@@QEAAXAEBV?$string_base@D@ttl@@@Z"' in profile
    assert '"?UnloadUITextures@ILevel@@QEAAXAEBV?$string_base@D@ttl@@@Z"' in profile
    assert "DLUMA_CMM_MAIN_INITIALIZE_UI_RVA 0x011B8E60UL" in runtime
    assert "DLUMA_CMM_MAIN_INITIALIZE_UI_SLOT_RVA 0x01678FE0UL" in runtime
    assert "DlumaCmmInitializeUiFn" in runtime
    assert "install_main_ui_initializer_hook" in runtime
    assert "restore_main_ui_initializer_hook" in runtime
    assert "manifest && manifest->use_custom_textures" in profile

    camera = _block(
        runtime,
        "static void dluma_cmm_apply_camera",
        "static void __fastcall dluma_cmm_load_hook",
    )
    camera_gate = camera.index(
        "dluma_custom_main_menu_runtime_static_camera_bridge_enabled"
    )
    assert camera_gate < camera.index("g_dluma_cmm_runtime.get_object_by_id")
    assert '"authored_movie_actions_static_bridge_suppressed"' in camera
    assert '"engine_owned"' in camera
    tick = _block(
        runtime,
        "void dluma_custom_main_menu_runtime_tick",
        "void dluma_custom_main_menu_runtime_quiesce",
    )
    assert "dluma_custom_main_menu_runtime_static_camera_bridge_enabled" in tick
    assert "dluma_cmm_apply_camera(g_dluma_cmm_runtime.loaded_module)" in tick
    assert "return !does_use_3d_camera_movement;" in runtime_core


def test_custom_menu_publication_is_synchronous_at_the_pre_ui_gate() -> None:
    engine = _source(ENGINE)
    runtime = _source(RUNTIME)
    header = _source(SRC / "dlw_custom_main_menu_runtime.h")
    isolation = _source(
        SRC / "fixes/custom_map_asset_isolation/CustomMapAssetIsolationFix.cpp"
    )
    frame_start = engine.index(
        "static ULONG_PTR __fastcall dluma_game_frame_hook(\n"
        "    void* game_subobject, void* gesture_manager, float delta_seconds)\n"
        "{"
    )
    frame = engine[
        frame_start : engine.index(
            "static void __fastcall dluma_i_level_destructor_hook",
            frame_start,
        )
    ]

    authority = "dluma_game_frame_authority_observe_pre("
    ordinary_dispatch = "dluma_ui_game_thread_tick("
    assert frame.index(authority) < frame.index(ordinary_dispatch)
    assert "dluma_custom_main_menu_runtime_preworker_tick" not in header
    assert "dluma_custom_main_menu_runtime_preworker_tick" not in frame
    assert "dluma_cmm_process_deferred_publication" not in runtime
    assert "dluma_custom_main_menu_runtime_defer_show_menu_main" not in header
    show_hook = _function_body(isolation, "void __fastcall HookShowMenuMain")
    assert "dluma_custom_main_menu_runtime_defer_show_menu_main" not in show_hook
    assert show_hook.count("g_original_show_menu_main(") == 1
    assert "DLUMA_CMM_MAIN_INITIALIZE_UI_RVA 0x011B8E60UL" in runtime
    assert "DLUMA_CMM_MAIN_INITIALIZE_UI_SLOT_RVA 0x01678FE0UL" in runtime


def test_custom_menu_publication_has_no_polling_or_manual_ui_replay() -> None:
    runtime = _source(RUNTIME)
    for obsolete in (
        "dluma_custom_main_menu_runtime_preworker_tick",
        "dluma_cmm_process_deferred_publication",
        "next_publication_retry_ms",
        "deferred_ui_level",
        "deferred_ui_registration_pending",
        "deferred_ui_binding_pending",
        "deferred_show_menu_pending",
        "dluma_cmm_replay_deferred_show_menu",
        "dluma_cmm_replay_deferred_stock_ui",
    ):
        assert obsolete not in runtime


def test_generated_custom_texture_manifest_fixture(tmp_path: Path) -> None:
    project = tmp_path / "synthetic-menu"
    manifest_path = _write_neutral_menu_fixture(project)
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    general = manifest["general"]
    definitions = manifest["textures"]

    assert general["doesUse3DCameraMovement"] is False
    assert general["useCustomTextures"] is True
    assert definitions == [{"def": "ui_theme.def"}]

    definition = project / "data/menu/texturedefs" / definitions[0]["def"]
    assert definition.is_file()
    assert definition.stat().st_size > 0
    assert (project / "data/FX/ambient_glow/emitter.fx").is_file()
    assert (project / "assets_pc/import_default/menu_prop.msh_obj").is_file()
    assert (project / "assets_pc/textures/ui_button.dds_obj").is_file()
    assert (project / "assets_pc/local_dx11.mp").is_file()


def test_exact_runtime_bridge_rtti_and_code_match_supported_gamedll() -> None:
    gamedll = DEVTOOLS_ROOT / "gamedll_x64_rwdi.dll"
    if not gamedll.is_file():
        return
    data, timestamp, image_size, read = _pe_rva_reader(gamedll)
    assert hashlib.sha256(data).hexdigest().upper() == (
        "CBC7DD7923778936341CDFB755474A0C714079D910849DF16FFC5EC88A103B95"
    )
    assert timestamp == 0x623397AA
    assert image_size == 0x02082000
    descriptors = {
        0x01CA2F10: b".?AVLevel@@",
        0x01CCD200: b".?AVMenu3DBackgroundDI@@",
        0x01C952A8: b".?AVIGSObject@@",
        0x01C952F0: b".?AVIControlObject@@",
        0x01CCD128: b".?AVCameraPostionHelper@@",
        0x01CCD1D0: b".?AVCameraHelperMenuScene3dDI@@",
    }
    for descriptor_rva, expected_name in descriptors.items():
        assert read(descriptor_rva + 16, 64).split(b"\0", 1)[0] == expected_name
    prefixes = {
        0x007547D0: "40 55 41 54 48 8D AC 24 78 FD FF FF 48 81 EC 88 03 00 00",
        0x00754A70: "40 53 48 83 EC 20 48 8B D9 48 8B 89 E8 0D 00 00",
        0x00757100: "40 53 48 83 EC 30 48 8B 89 E8 0D 00 00",
        0x0130A87E: "FF 25 64 19 0C 00",
        0x011B8E60: (
            "40 53 48 83 EC 20 48 8B 01 48 8B D9 FF 90 38 0A 00 00 "
            "48 8B CB 48 83 C4 20 5B E9 71 50 F4 FF CC"
        ),
    }
    for rva, expected in prefixes.items():
        expected_bytes = bytes.fromhex(expected)
        assert read(rva, len(expected_bytes)) == expected_bytes
    preferred_image_base = 0x180000000
    initialize_ui_slot = struct.unpack("<Q", read(0x01678FE0, 8))[0]
    assert initialize_ui_slot - preferred_image_base == 0x011B8E60
    stock_premount = bytes.fromhex(
        "48 89 5C 24 18 55 41 54 41 55 48 8D 6C 24 B9 48 "
        "81 EC 90 00 00 00 4C 8B E1 48 8B 0D 48 6D 3E 01 "
        "48 89 B4 24 B0 00 00 00 48 8B 01 48 8B DA FF 90 "
        "C8 1A 00 00 48 8B 0D 2D 6D 3E 01 48 8B D3 48 8B "
        "01 FF 90 E8 1A 00 00 48 8B 0D 7A B8 3E 01 E8 ED "
        "10 AC FF"
    )
    assert read(0x0091C6C0, len(stock_premount)) == stock_premount
    first_tools_disp = struct.unpack_from("<i", stock_premount, 28)[0]
    second_tools_disp = struct.unpack_from("<i", stock_premount, 55)[0]
    assert 0x0091C6C0 + 32 + first_tools_disp == 0x01D03428
    assert 0x0091C6C0 + 59 + second_tools_disp == 0x01D03428
    music_assignment = bytes.fromhex(
        "20 0B 00 00 48 0F 45 D1 48 8B CB E8 E0 E8 E4 FE"
    )
    assert read(0x011B7200, 16) == music_assignment
    displacement = struct.unpack_from("<i", music_assignment, 12)[0]
    assert 0x011B720B + 5 + displacement == 0x0005AF0
    engine = gamedll.with_name("engine_x64_rwdi.dll")
    engine_data, _, _, engine_read = _pe_rva_reader(engine)
    assert hashlib.sha256(engine_data).hexdigest().upper() == (
        "A18CA1103614947AE7FF956C6852F45123B440563A967D9E1E496C42FA574A3C"
    )
    engine_prefixes = {
        0x002D9C80: "48 85 D2 74 14 48 8B 82 90 03 09 00 48 85 C0 74 08 48 8B 80 B8 02 00 00",
        0x00136AF0: "40 53 48 83 EC 20 48 8B 41 08 48 8B DA 48 8B 88 F0 00 00 00",
        0x00137150: "40 53 48 83 EC 20 48 8B 49 08 48 8B DA E8 1E E5 FF FF",
        0x00137170: "40 53 48 83 EC 20 48 8B 49 08 48 8B DA E8 9E E5 FF FF",
        0x00125B00: "48 8B C4 48 89 58 08 48 89 70 10 48 89 78 18 55 41 54 41 55 41 56 41 57",
        0x00127750: "48 83 EC 28 48 83 79 08 00 74 0A 48 8B 49 08 48 8B 01 FF 50 28",
        0x0018CE20: "0F B6 C2 89 05 FB 47 90 00 C3",
        0x002DA470: "48 83 EC 28 83 7A 08 00 76 17 48 8B 41 08 48 8B 88 70 05 00 00",
        0x002DA4A0: "48 8B 41 08 48 8B 88 70 05 00 00 48 81 C1 B0 03 00 00",
        0x0028DF00: "48 89 5C 24 10 48 89 6C 24 18 56 48 83 EC 20 48 8B 0D 02 C9 83 00 48 8B F2 48 8D A9 58 04 00 00",
        0x0028DFB0: "48 89 5C 24 10 48 89 6C 24 18 56 48 83 EC 20 48 8B 0D 52 C8 83 00 48 8B EA 48 8D B1 68 04 00 00",
        0x00299B50: "48 83 EC 28 48 8B 0D 55 35 83 00 48 85 C9 74 09 48 8B 01 FF 90 D8 00 00 00 48 83 C4 28 C3",
        0x0029CA00: "48 83 EC 28 48 8B 0D A5 06 83 00 48 85 C9 74 09 48 8B 01 FF 90 10 01 00 00 48 83 C4 28 C3",
        0x0029CB70: "40 53 48 83 EC 20 48 8B DA E8 E2 F2 3E 00 48 8B D3 48 8B C8 E8 C7 F3 3E 00 E8 D2 F2 3E 00 48 8B C8 E8 BA F4 3E 00 E8 C5 F2 3E 00 48 8B C8 48 83 C4 20 5B E9 78 FC 3E 00",
    }
    for rva, expected in engine_prefixes.items():
        expected_bytes = bytes.fromhex(expected)
        assert engine_read(rva, len(expected_bytes)) == expected_bytes
    preferred_image_base = 0x180000000
    expected_slots = {
        0x0158: 0x0028DF00,
        0x0160: 0x0028DFB0,
        0x1408: 0x00299B50,
        0x1AC8: 0x0029CA00,
        0x1AE8: 0x0029CB70,
    }
    for slot, expected_rva in expected_slots.items():
        target = struct.unpack(
            "<Q", engine_read(0x008D55C8 + slot, 8)
        )[0]
        assert target - preferred_image_base == expected_rva


def test_activation_receipt_and_engine_owned_music_substitution() -> None:
    runtime = _source(RUNTIME)
    music = _source(MUSIC)
    media = _source(MEDIA)
    activate = _block(
        runtime,
        "static void __fastcall dluma_cmm_activate_hook",
        "static int dluma_cmm_install_vtable_hooks",
    )
    load_hook = _function_body(
        runtime, "static void __fastcall dluma_cmm_load_hook"
    )
    custom_load = _function_body(
        runtime, "static int dluma_cmm_call_custom_load"
    )
    assert custom_load.index(
        "dluma_custom_main_menu_mark_activation_pending"
    ) < custom_load.index("original(module)")
    assert "dluma_cmm_replay_deferred_stock_ui" not in runtime
    assert "dluma_cmm_replay_deferred_show_menu" not in runtime
    # Stock activation owns level/FX initialization; only after it returns may
    # the validated FX flag and camera bridge be applied.
    assert "dluma_cmm_prepare_fx_before_activation" not in activate
    assert "InitFXSystem" not in activate
    stock_activation = activate.index("original(module)")
    camera_applied = activate.index("dluma_cmm_apply_camera(module)", stock_activation)
    assert stock_activation < camera_applied
    tick = _block(
        runtime,
        "void dluma_custom_main_menu_runtime_tick",
        "void dluma_custom_main_menu_runtime_quiesce",
    )
    assert "DLUMA_CMM_STABLE_MS" in tick
    assert "DLUMA_CMM_CAMERA_RETRY_MS" in tick
    assert "g_dluma_cmm_runtime.loaded_module" in tick
    assert "dluma_cmm_apply_camera(g_dluma_cmm_runtime.loaded_module)" in tick
    assert "dluma_frontend_level_name_is_frontend(map_name)" in tick
    assert "level != g_dluma_cmm_runtime.active_background" not in tick
    assert "background_still_owned" in tick
    assert "!background_still_owned" in tick
    assert "dluma_custom_main_menu_mark_activation_good" in tick
    ui = _source(SRC / "dlw_ui.c")
    null_query = _block(
        ui,
        "else\n    {\n        /* The menu background is a secondary",
        "frontend_identity_latched =",
    )
    assert 'dluma_custom_main_menu_runtime_tick("", 0, now);' in null_query
    unload = _block(
        runtime,
        "static __int64 __fastcall dluma_cmm_unload_hook",
        "static void __fastcall dluma_cmm_activate_hook",
    )
    assert unload.index("dluma_cmm_restore_fx") < unload.index(
        "dluma_cmm_clear_music_manager"
    ) < unload.index(
        "original(module)"
    )
    assert unload.index("dluma_cmm_stop_started_fx_emitters") < unload.index(
        "dluma_cmm_restore_fx"
    )
    assert "dluma_custom_main_menu_music_retire_generation" in unload
    assert "best_effort_dependency_cleanup_failed_original_unload_preserved" in unload
    # Dependency cleanup is best-effort during a natural frontend transition.
    # The owned engine unload must still run exactly once so stale level,
    # animation, and source objects cannot survive into gameplay.
    assert unload.count("original(module)") == 1
    load = _function_body(
        runtime,
        "static int dluma_cmm_call_custom_load",
    )
    assert load.index("original(module)", load.index("__try")) < load.index(
        "dluma_cmm_observe_background_after_load(module)"
    )
    assert "load_module_returned_no_level" in runtime
    assert "level_present_rtti_validated" in runtime
    assert "DlumaMediaPreparedBankDescriptor" in music
    assert "source_mounted = 1U" in music
    assert "source_locked = 1U" in music
    assert "dluma_player_media_adapter_prepare_main_menu_bank" in music
    assert "if (g_dluma_cmm_music.banks[index])" in music
    assert "dluma_player_media_adapter_music_acquire" in music
    assert "dluma_player_media_adapter_music_maintain" in music
    assert "DLUMA_MEDIA_CUE_FLAG_LOOP" in music
    assert "dluma_player_media_adapter_play" in music
    assert "dluma_player_media_adapter_stop_wave" in music
    assert "dluma_player_media_adapter_music_release" in music
    assert "request.fade_in_ms = 1000U" in music
    assert "DLUMA_CMM_MUSIC_RETRY_MS" not in music
    reconcile = _block(
        music,
        "int dluma_custom_main_menu_music_reconcile",
        "int dluma_custom_main_menu_music_release_screen_override",
    )
    no_operation = reconcile[
        reconcile.index("DLUMA_CMM_MUSIC_TRANSITION_NO_OPERATION") :
        reconcile.index("DLUMA_CMM_MUSIC_TRANSITION_RELEASE")
    ]
    assert "dluma_cmm_music_maintain_override()" in no_operation
    assert "dluma_player_media_adapter_play" not in no_operation
    assert reconcile.count("dluma_cmm_music_maintain_override()") == 2
    assert reconcile.rindex("dluma_cmm_music_maintain_override()") < (
        reconcile.index("DLUMA_CMM_MUSIC_TRANSITION_SWITCH")
    )
    prepared = _block(
        media,
        "int dluma_player_media_adapter_prepare_main_menu_bank",
        "int dluma_player_media_adapter_bank_loaded_for_pack",
    )
    assert "dluma_player_media_prepared_descriptor_valid" in prepared
    assert "dluma_player_media_validate_main_menu_audio_owner" in prepared
    assert "DLUMA_DL_MAIN_MENU_MODULE_IGSOBJECT_OFFSET" in prepared
    assert "dluma_player_media_open_prepared_bank_for_owner" in prepared
    direct_prepared = _block(
        media,
        "static int dluma_player_media_open_prepared_bank_for_owner",
        "int dluma_player_media_adapter_open_prepared_bank",
    )
    assert "profile->load_wave_bank" in direct_prepared
    assert "dluma_player_media_cue_probe_owner_internal" in direct_prepared
    assert "prepared_descriptor = *descriptor" in direct_prepared
    assert "dluma_player_media_prepared_bank_identity_equal" in direct_prepared
    assert "descriptor->cue_name" in direct_prepared
    discovery = _source(DISCOVERY)
    companion = _source(SRC / "dlw_companion_archive.c")
    assert "dluma_custom_menu_materialize_declared_banks" in discovery
    assert "dluma_companion_archive_materialize_locked_bank" in discovery
    assert "dluma_companion_archive_mount_materialized_bank" in discovery
    assert 'L"prepared_banks"' in companion
    assert '"DLUMAPrepared_"' in companion
    assert "dluma_native_companion_identity_unchanged" in companion
    assert "DLUMA_SOURCE_OWNER_ID_CUSTOM_MAIN_MENU_BANK" in discovery
    assert "dluma_player_media_adapter_publish_frontend_music_manager" in runtime
    assert (
        "dluma_player_media_adapter_publish_frontend_main_menu_module"
        in runtime
    )
    assert "DLUMA_CMM_BACKGROUND_VIEW_OFFSET 0x09C0UL" in runtime
    assert "DLUMA_CMM_ACTIVATE_VIEW_RVA 0x002D9280UL" in runtime
    assert '"?ActivateView@ILevel@@QEAAXPEAVCLevelView@@@Z"' in runtime
    assert "g_dluma_cmm_runtime.activate_view(menu_background, level_view)" in runtime
    assert '"activation_reinforced"' in runtime
    assert "dluma_custom_main_menu_runtime_editor_camera_forward" in runtime
    assert '"editor_helper_negative_world_z"' in runtime
    assert "DLUMA_CMM_BACKGROUND_MUSIC_MANAGER_OFFSET 0x09E0UL" in runtime
    assert "CUSTOM_FRONTEND_BACKGROUND_OWNER_CORROBORATED" in media
    assert "published_frontend_owner_level" in media
    assert "published_frontend_music_manager" in media
    assert "DLUMA_DL_FRONTEND_MUSICS_MANAGER_VTABLE_RVA 0x0170DBC8UL" in media
    assert "DLUMA_DL_FRONTEND_MUSICS_MANAGER_MUTE_RVA 0x0097AF20UL" in media
    assert "DLUMA_DL_FRONTEND_MUSICS_MANAGER_UNMUTE_RVA 0x0097B060UL" in media
    assert "profile->frontend_music_mute" in media
    assert "profile->frontend_music_unmute" in media
    assert "DLUMA_DL_MAIN_MENU_MODULE_PLAY_MUSIC_RVA 0x011B70F0UL" in media
    assert "DLUMA_DL_MAIN_MENU_MODULE_STOP_MUSIC_RVA 0x011B7380UL" in media
    assert "DLUMA_DL_MAIN_MENU_MODULE_VTABLE_RVA 0x01678C38UL" in media
    assert "DLUMA_DL_MAIN_MENU_MODULE_IGSOBJECT_VTABLE_RVA 0x01679718UL" in media
    assert "DLUMA_DL_MAIN_MENU_MODULE_MUSIC_HANDLE_OFFSET 0x0B18U" in media
    assert "DLUMA_CMM_MUSIC_ASSIGN_BLOCK_RVA 0x011B7200UL" in runtime
    assert "DLUMA_CMM_MUSIC_ASSIGN_CALL_RVA 0x011B720BUL" in runtime
    assert "DLUMA_CMM_MUSIC_ASSIGN_TARGET_RVA 0x0005AF0UL" in runtime
    assert "DLUMA_CMM_MUSIC_MAIN_RVA 0x01DCF1B0UL" in runtime
    assert "DLUMA_CMM_MUSIC_INGAME_RVA 0x01DCF1C0UL" in runtime
    assert "DLUMA_CMM_MAIN_MENU_MUSIC_NAME_OFFSET 0x0B20UL" in runtime
    assert "DLUMA_CMM_MAIN_MENU_MUSIC_HANDLE_OFFSET 0x0B18UL" in runtime
    assert "DLUMA_CMM_MAIN_MENU_VTABLE_RVA 0x01678C38UL" in runtime
    assert "DLUMA_CMM_MAIN_MENU_IGSOBJECT_VTABLE_RVA 0x01679718UL" in runtime
    assert "DLUMA_CMM_PLAY_BACKGROUND_MUSIC_RVA 0x011B70F0UL" in runtime
    assert "DLUMA_CMM_STOP_BACKGROUND_MUSIC_RVA 0x011B7380UL" in runtime
    route = _block(
        runtime,
        "static DlumaCmmTtlString * __fastcall dluma_cmm_music_assign_hook",
        "static void *dluma_cmm_allocate_relay_near",
    )
    assert "source == (const DlumaCmmTtlString *)" in route
    assert "DLUMA_CMM_MUSIC_MAIN_RVA" in route
    assert "MUSIC_INGAME" in route
    assert "dluma_custom_main_menu_music_resolve_main_cue" in route
    assert "original(destination, selected_source)" in route
    install = _block(
        runtime,
        "static int dluma_cmm_install_music_assign_hook",
        "static int dluma_cmm_prepare_temporary_menu_name",
    )
    assert "dluma_custom_main_menu_runtime_relative_call_valid" in install
    assert "dluma_cmm_allocate_relay_near" in install
    assert "dluma_cmm_atomic_music_block_exchange" in install
    assert "music_assign_original_protect" in install
    assert "protection_restore_failed_relay_retained" in install
    assert "g_dluma_cmm_runtime.music_assign_installed = 1U" in install
    refresh = _function_body(
        runtime, "static int dluma_cmm_refresh_main_music_once"
    )
    assert refresh.index("dluma_custom_main_menu_music_resolve_main_cue") < (
        refresh.index("stop_background_music(module)")
    ) < refresh.index("play_background_music(module)")
    assert "DLUMA_CMM_MAIN_MENU_MUSIC_HANDLE_OFFSET" in refresh
    assert "*music_handle != 0U" in refresh
    assert "*music_handle == 0U" in refresh
    assert "main_music_refresh_state, 1, 0" in refresh
    assert "DLUMA_CMM_MUSIC_RESOLVE_RETRY" in refresh
    assert "DLUMA_CMM_MUSIC_RESOLVE_PERMANENT" in refresh
    assert "next_main_music_refresh_ms" in refresh
    assert "bank_or_audio_owner_not_ready_retrying" in refresh
    assert refresh.index("dluma_custom_main_menu_music_resolve_main_cue_state") < (
        refresh.index("engine_attempted = 1")
    ) < refresh.index("stop_background_music(module)")
    retry_log = refresh.index("bank_or_audio_owner_not_ready_retrying")
    retry_deadline = refresh.rfind(
        "next_main_music_refresh_ms =", 0, retry_log
    )
    retry_state_reset = refresh.rfind(
        "main_music_refresh_state, 0", 0, retry_log
    )
    assert retry_deadline >= 0
    assert retry_state_reset > retry_deadline
    retirement = _function_body(
        runtime, "static void dluma_cmm_retire_frontend_generation_locked"
    )
    assert "&g_dluma_cmm_runtime.main_music_refresh_state, 0" in retirement
    assert "next_main_music_refresh_ms = 0" in retirement
    quiesce = _block(
        runtime,
        "void dluma_custom_main_menu_runtime_quiesce",
        "int dluma_custom_main_menu_runtime_stop",
    )
    assert quiesce.index("dluma_cmm_restore_music_assign_hook") < quiesce.index(
        "dluma_cmm_restore_activate_hook"
    )
    stop = runtime[runtime.index("int dluma_custom_main_menu_runtime_stop") :]
    assert "music_assign_installed" in stop
    assert "music_assign_relay" in stop
    assert "dluma_custom_main_menu_music_tick" not in tick
    credits = _source(
        SRC / "fixes/main_menu_credits_music/MainMenuCreditsMusicFix.cpp"
    )
    assert "dluma_custom_main_menu_music_lease_active" not in credits


def test_runtime_generation_retirement_and_failed_load_replay_are_scoped() -> None:
    runtime = _source(RUNTIME)
    runtime_header = _source(SRC / "dlw_custom_main_menu_runtime.h")

    assert "unsigned int reason, ULONGLONG frontend_generation" in runtime_header

    note_retired = _function_body(
        runtime, "void dluma_custom_main_menu_runtime_note_source_retired("
    )
    assert "generation_lock" in note_retired
    assert "frontend_generation == g_dluma_cmm_runtime.frontend_generation" in (
        note_retired
    )
    assert "source_retirement_generation" in note_retired
    assert "source_retirement_publication_state = state" in note_retired

    consume_retired = _function_body(
        runtime, "static LONG dluma_cmm_consume_source_retirement("
    )
    assert "generation_lock" in consume_retired
    assert "source_retirement_pending, 0" in consume_retired
    assert "retirement_generation == active_generation" in consume_retired
    assert "reason == 1 && !gameplay_started" in consume_retired
    assert "DLUMA_CMM_PUBLICATION_READY_BEFORE_UI" in consume_retired
    assert "DLUMA_CMM_PUBLICATION_LOADING_BACKGROUND" in consume_retired
    assert "DLUMA_CMM_PUBLICATION_ACTIVE" not in consume_retired
    assert "transient_publication_cleanup_ignored" in consume_retired
    assert "native_source_removed_after_publication" in consume_retired
    assert "background_unloaded" in consume_retired
    assert "stale_notification_ignored" in consume_retired

    tick = _function_body(
        runtime, "void dluma_custom_main_menu_runtime_tick("
    )
    assert "dluma_cmm_consume_source_retirement(gameplay_started)" in tick
    assert "if (!retirement_reason)" in tick
    assert '"gameplay_started", 1' in tick

    unload = _function_body(
        runtime, "static __int64 __fastcall dluma_cmm_unload_hook("
    )
    for owner in ("publication_module", "active_module", "loaded_module"):
        assert f"module == g_dluma_cmm_runtime.{owner}" in unload
    assert "if (owns_generation)" in unload
    assert "2U, owned_generation" in unload
    assert unload.count("original(module)") == 1

    custom_load = _function_body(runtime, "static int dluma_cmm_call_custom_load(")
    entered = custom_load.index("DLUMA_CMM_CUSTOM_LOAD_ENTERED_FAILED")
    original = custom_load.index("original(module);", entered)
    completed = custom_load.index("DLUMA_CMM_CUSTOM_LOAD_COMPLETED", original)
    assert entered < original < completed

    load_hook = _function_body(
        runtime, "static void __fastcall dluma_cmm_load_hook("
    )
    not_entered = load_hook.index(
        "load_result == DLUMA_CMM_CUSTOM_LOAD_NOT_ENTERED"
    )
    stock_replay = load_hook.index("original(module);", not_entered)
    no_replay = load_hook.index(
        "engine_load_entered_failure_not_replayed", stock_replay
    )
    assert not_entered < stock_replay < no_replay


def test_selected_scene_is_centralized_as_frontend_everywhere() -> None:
    frontend = _source(SRC / "dlw_frontend_level.c")
    assert 'dluma_stricmp(map_name, "menu_lvl_new")' in frontend
    assert "dluma_frontend_level_name_is_selected_custom(map_name)" in frontend
    consumers = (
        SRC / "dlw_host.c",
        SRC / "dlw_engine.c",
        SRC / "dlw_ui.c",
        SRC / "dlw_discord_presence.c",
        SRC / "fixes/custom_map_asset_isolation/CustomMapAssetIsolationFix.cpp",
    )
    for consumer in consumers:
        assert "dluma_frontend_level_name_is_frontend" in _source(consumer)
    exports = _source(EXPORTS)
    player_initialize = exports.index("static void* dluma_player_initialize")
    discover = exports.index(
        "dluma_custom_main_menu_discover_and_prepare", player_initialize
    )
    original = exports.index("manager = g_original_initialize()", discover)
    assert discover < original


def test_content_pack_contains_selector_and_all_localized_catalogs() -> None:
    assert PAK.is_file()
    with zipfile.ZipFile(PAK) as archive:
        names = {name.replace("\\", "/").casefold() for name in archive.namelist()}
    assert "data/menu/scr/menudlumacustommainmenu.xui" in names
    locales = sorted(path for path in (ROOT / "PakAssets/Locale").iterdir() if path.is_dir())
    assert len(locales) == 17
    for locale in locales:
        assert (
            f"data/locale/{locale.name}/loader_texts_all.scr".casefold()
            in names
        )
        text = (locale / "loader_texts_all.scr").read_text(encoding="utf-8")
        for key in (
            "DLUMA_Mods_BuiltInGroup",
            "DLUMA_CustomMenu_Name",
            "DLUMA_CustomMenu_BaseGame",
            "DLUMA_CustomMenu_QuarantinedHint",
            "DLUMA_CustomMainMenu_NotCustomMap",
        ):
            assert f'String("{key}",' in text


def test_multibank_catalog_and_screen_music_override_contracts() -> None:
    header = _source(SRC / "dlw_custom_main_menu_core.h")
    core = _source(CORE)
    discovery = _source(DISCOVERY)
    music = _source(MUSIC)
    runtime = _source(RUNTIME)
    runtime_core = _source(SRC / "dlw_custom_main_menu_runtime_core.c")
    companion = _source(SRC / "dlw_companion_archive.c")
    credits = _source(
        SRC / "fixes/main_menu_credits_music/MainMenuCreditsMusicFix.cpp"
    )
    isolation = _source(
        SRC / "fixes/custom_map_asset_isolation/CustomMapAssetIsolationFix.cpp"
    )

    assert "#define DLUMA_CUSTOM_MENU_MAX_BANKS 16U" in header
    assert "count >= DLUMA_CUSTOM_MENU_MAX_BANKS" in core
    assert "(fields == 1U || fields == 3U)" in core
    assert "manifest->music_override_count >" in core
    assert 'alias = "MenuExtras.xui"' in core
    assert "dluma_custom_main_menu_xui_music_override" in core
    assert '"</MusicOverride>"' in core

    assert "DLUMA_CUSTOM_MENU_MAX_BANK_TOTAL_BYTES" in discovery
    assert "dluma_companion_archive_catalog_locked_bank" in discovery
    assert "dluma_custom_menu_cue_hash" in discovery
    assert "dluma_custom_menu_catalogs_valid" in discovery
    assert "declared_bank_index_plus_one" in discovery
    assert "dluma_custom_main_menu_selected_bank_count" in discovery
    assert "dluma_custom_main_menu_selected_bank_alias_at" in discovery
    assert "dluma_custom_main_menu_selected_bank_source_snapshot_at" in discovery
    assert "prepared->owner_id[owner_length] = '.'" in discovery
    assert "dluma_native_companion_validate_csb_inventory" in companion
    assert "0x08000008UL" in companion
    assert "DLUMA_COMPANION_ARCHIVE_MAX_CUES" in companion

    assert "for (index = 0; index < count; ++index)" in music
    assert "dluma_player_media_adapter_prepare_main_menu_bank" in music
    assert "dluma_custom_main_menu_runtime_music_transition" in music
    assert "DLUMA_MEDIA_CUE_FLAG_LOOP" in music
    assert "request.fade_in_ms = 1000U" in music
    assert "play_failed_normal_music_resumed" in music
    release = music[
        music.index("static int dluma_cmm_music_release_override(void)") :
        music.index("void dluma_custom_main_menu_music_reset(void)")
    ]
    assert "return 0;\n        g_dluma_cmm_music.wave = 0;" in release
    assert "return 0;\n        g_dluma_cmm_music.lease = 0;" in release
    assert "dluma_custom_main_menu_music_release_screen_override" in credits

    for token in (
        "DLUMA_CMM_SCREEN_SHOWN_RVA 0x011E17F0UL",
        "DLUMA_CMM_SCREEN_HIDDEN_RVA 0x011E1840UL",
        "DLUMA_CMM_SCREEN_SHOWN_SLOT_RVA 0x01678DB0UL",
        "DLUMA_CMM_SCREEN_HIDDEN_SLOT_RVA 0x01678DB8UL",
        "DLUMA_CMM_VISIBLE_SCREEN_CAPACITY 32U",
        "dluma_cmm_screen_shown_hook",
        "dluma_cmm_screen_hidden_hook",
        "dluma_cmm_restore_screen_hooks",
        "dluma_cmm_reconcile_screen_audio",
        "DLUMA_CMM_EMPTY_STACK_GRACE_MS",
        "dluma_custom_main_menu_runtime_screen_script_is_cursor(script)",
        '"custom_main_menu_top_screen"',
        '"custom_main_menu_screen_shown_events"',
    ):
        assert token in runtime
    assert "DLUMA_CMM_MUSIC_TRANSITION_SWITCH" in runtime_core
    assert "DLUMA_CMM_MUSIC_TRANSITION_RELEASE" in runtime_core
    assert "dluma_custom_main_menu_runtime_empty_stack_grace_active" in runtime_core
    assert 'script_name, "UICursor.xui"' in runtime_core
    assert "dluma_custom_main_menu_runtime_screen_script_matches(" in runtime_core
    assert "g_normalized_custom_main_menu_bank_paths" in isolation
    assert "CustomMainMenuBankOwnerIndex" in isolation


def test_generated_fixture_is_editor_safe_and_matches_runtime_contract(
    tmp_path: Path,
) -> None:
    project = tmp_path / "portable-menu"
    manifest_path = _write_neutral_menu_fixture(project)
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))

    assert manifest["meta"]["id"] == "example.syntheticmenu"
    assert manifest["scene"]["map"] == "menu_scene"
    assert manifest["general"]["is3DMenuEnabled"] is False
    assert manifest["general"]["useCustomBanks"] is False
    assert manifest["cameras"] == [{"gameObject": ["camera_helper", 7]}]
    assert manifest["textures"] == [{"def": "ui_theme.def"}]
    assert manifest["menus"] == [
        {
            "name": "main_screen",
            "overrides": "MenuMain_PC_workshop_with_B_Mods",
        },
        {"name": "secondary_screen", "overrides": "AddedScreen"},
    ]

    main_xui = (project / "data/menu/dluma/main_screen.xui").read_text(
        encoding="utf-8"
    )
    for control_id in (
        "B_PlayCustomMap",
        "B_Options",
        "B_Extras",
        "B_Mods",
    ):
        assert f"<Id>{control_id}</Id>" in main_xui

    map_root = project / "data/maps/menu_scene"
    compiled: dict[str, bytes] = {}
    for extension in ("map", "exp"):
        data = (map_root / f"menu_scene.{extension}").read_bytes()
        compiled[extension] = data
        assert struct.unpack_from("<I", data, 2)[0] == len(data) - 6
        assert struct.unpack_from("<H", data, 6)[0] == 0x77
        class_length = struct.unpack_from("<H", data, 12)[0]
        assert data[14 : 14 + class_length] == b"LevelDI"
        assert b"camera_helper" in data
        assert b"menu_prop.msh" in data
        assert b"Ambient Glow" in data
    assert (map_root / "menu_scene.mis").read_text(
        encoding="utf-8"
    ) == "menu_scene{LevelDI}\n"

    runtime_assets = (
        "data/import_default/menu_prop.msh",
        "data/FX/ambient_glow/emitter.fx",
        "data/textures/ui_button.dds",
        "assets_pc/import_default/menu_prop.msh_obj",
        "assets_pc/textures/ui_button.dds_obj",
        "assets_pc/local_dx11.mp",
    )
    for relative in runtime_assets:
        assert (project / relative).is_file(), relative
