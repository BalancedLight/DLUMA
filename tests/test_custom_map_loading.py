from pathlib import Path
import re
import subprocess


ROOT = Path(__file__).resolve().parents[1]
COMMON = ROOT / "src" / "dlw_common.h"
HOST = ROOT / "src" / "dlw_host.c"
MENU = ROOT / "src" / "dlw_workshop_menu.c"
UI = ROOT / "src" / "dlw_ui.c"
PREVIEW = ROOT / "src" / "dlw_workshop_preview.c"
INI = ROOT / "config" / "dluma.ini"


def test_custom_map_loading_native_core_harness():
    completed = subprocess.run(
        ["cmd.exe", "/d", "/s", "/c",
         "call tools\\build_custom_map_loading_fake_tests.bat"],
        cwd=ROOT,
        text=True,
        capture_output=True,
        check=False,
    )
    assert completed.returncode == 0, completed.stdout + completed.stderr
    run = subprocess.run(
        [str(ROOT / "build" / "custom-map-loading-fake"
             / "custom_map_loading_fake_harness.exe")],
        cwd=ROOT,
        text=True,
        capture_output=True,
        check=False,
    )
    assert run.returncode == 0, run.stdout + run.stderr
    assert "custom_map_loading_fake_harness: ok" in run.stdout


def test_loading_settings_are_independent_default_enabled_and_live_updated():
    common = COMMON.read_text(encoding="utf-8")
    host = HOST.read_text(encoding="utf-8")
    ui = UI.read_text(encoding="utf-8")
    ini = INI.read_text(encoding="utf-8")
    assert "custom_map_workshop_loading_screen_enabled" in common
    assert "custom_map_loading_screen_content_enabled" in common
    assert (
        'L"loader", L"custom_map_workshop_loading_screen", 1'
        in host
    )
    assert (
        'L"GameFixes", L"CustomMapLoadingScreenContent", 1'
        in host
    )
    assert "custom_map_workshop_loading_screen=1" in ini
    assert "CustomMapLoadingScreenContent=1" in ini
    table = ui[
        ui.index("static const DlumaLoaderSettingDefinition g_dluma_loader_settings"):
        ui.index("static DWORD dluma_ui_read_u32")
    ]
    for section, key in (
        ("loader", "custom_map_workshop_loading_screen"),
        ("GameFixes", "CustomMapLoadingScreenContent"),
    ):
        row = re.search(
            rf'\{{L"{section}",\s*L"{key}",\s*L"1"[\s\S]*?\}},',
            table,
        )
        assert row is not None
        assert "DLUMA_LOADER_SETTING_BOOL" in row.group(0)
    writer = ui[
        ui.index("static int dluma_ui_settings_write_value"):
        ui.index("static int dluma_ui_settings_parse_uint")
    ]
    assert "custom_map_workshop_loading_screen_enabled" in writer
    assert "custom_map_loading_screen_content_enabled" in writer


def test_steam_details_description_is_bounded_and_authoritative():
    common = COMMON.read_text(encoding="utf-8")
    menu = MENU.read_text(encoding="utf-8")
    assert "#define DLUMA_WORKSHOP_STEAM_DESCRIPTION_CAPACITY 8001" in common
    assert "#define DLUMA_STEAM_UGC_DESCRIPTION_OFFSET 153U" in menu
    assert "#define DLUMA_STEAM_UGC_DESCRIPTION_SIZE 8000U" in menu
    assert "dluma_custom_map_loading_plain_text(" in menu
    assert "state->description" in menu
    assert "out->description" in menu
    assert "description_authoritative" in common


def test_loading_hooks_are_exact_build_gated_and_presence_based():
    ui = UI.read_text(encoding="utf-8")
    for literal in (
        "DLUMA_DEVTOOLS_LEVEL_INFO_PARSE_RVA 0x004065F0UL",
        "DLUMA_DEVTOOLS_LEVEL_INFO_PARSE_PROPERTY_RVA 0x00406F80UL",
        "DLUMA_DEVTOOLS_LEVEL_INFO_ACTIVE_LEVEL_PARSE_RETURN_RVA 0x003F3538UL",
        "DLUMA_DEVTOOLS_LEVEL_INFO_SESSION_PARSE_RETURN_RVA_0 0x0043390EUL",
        "DLUMA_DEVTOOLS_LEVEL_INFO_SESSION_PARSE_RETURN_RVA_1 0x0043404FUL",
        "DLUMA_DEVTOOLS_LEVEL_INFO_SESSION_PARSE_RETURN_RVA_2 0x004347DEUL",
        "DLUMA_DEVTOOLS_BACKGROUND_SET_LEVEL_INFO_RVA 0x00762070UL",
        "DLUMA_DEVTOOLS_BACKGROUND_CLEAR_LEVEL_INFO_RVA 0x007632B0UL",
    ):
        assert literal in ui
    transition_route = ui[
        ui.index("static const char *dluma_ui_loading_transition_parse_route"):
        ui.index("static void dluma_ui_loading_finalize_parse")
    ]
    for callsite in (
        "DLUMA_DEVTOOLS_LEVEL_INFO_ACTIVE_LEVEL_PARSE_RETURN_RVA",
        "DLUMA_DEVTOOLS_LEVEL_INFO_SESSION_PARSE_RETURN_RVA_0",
        "DLUMA_DEVTOOLS_LEVEL_INFO_SESSION_PARSE_RETURN_RVA_1",
        "DLUMA_DEVTOOLS_LEVEL_INFO_SESSION_PARSE_RETURN_RVA_2",
    ):
        assert callsite in transition_route
    assert "0x00754943UL" not in transition_route
    callsite_validation = ui[
        ui.index(
            "static int dluma_ui_loading_validate_transition_callsites"
        ):
        ui.index("static int dluma_ui_install_loading_hooks")
    ]
    for call_bytes in (
        "{0xE8, 0xB8, 0x30, 0x01, 0x00}",
        "{0xE8, 0xE2, 0x2C, 0xFD, 0xFF}",
        "{0xE8, 0xA1, 0x25, 0xFD, 0xFF}",
        "{0xE8, 0x12, 0x1E, 0xFD, 0xFF}",
    ):
        assert call_bytes in callsite_validation
    assert "dluma_custom_map_loading_directive_mask" in ui
    assert "authored_directives" in ui
    assert "DLUMA_LOADING_DIRECTIVE_TITLE" in ui
    assert "DLUMA_LOADING_DIRECTIVE_OBJECTIVE" in ui
    assert "DLUMA_LOADING_DIRECTIVE_DESCRIPTION" in ui
    assert "DLUMA_LOADING_DIRECTIVE_VISUAL" in ui
    assert "loading_hooks_unsupported_build" in ui
    assert "dluma_ui_loading_restore_hooks_now" in ui


def test_loading_snapshot_is_exact_project_and_frozen_per_transition():
    ui = UI.read_text(encoding="utf-8")
    assert "DlumaCustomMapLoadingPresentation" in ui
    assert "normalized_project_root" in ui
    assert "workshop_id" in ui
    assert "launch_generation" in ui
    assert "transition_key" in ui
    assert "map_name" in ui
    assert "dluma_ui_loading_capture_presentation(" in ui
    assert "frozen" in ui
    assert "Never correlate a loading presentation by map name alone" in ui
    assert "presentation.workshop_details_enabled" in ui
    assert "presentation.content_fix_enabled" in ui
    assert "static PVOID volatile g_dluma_loading_pending_presentation" in ui
    assert "dluma_ui_loading_claim_pending_presentation(" in ui
    assert "dluma_atomic_pointer_exchange(" in ui
    claim = ui[
        ui.index(
            "dluma_ui_loading_claim_pending_presentation("
        ):
        ui.index(
            "static const char *dluma_ui_loading_transition_parse_route"
        )
    ]
    assert "capture_thread_id != GetCurrentThreadId()" not in claim
    assert "g_dluma_loading_active_presentation = *pending" in ui
    assert "memset(pending, 0, sizeof(*pending))" in ui


def test_preview_lease_and_project_texture_fix_have_bounded_ownership():
    ui = UI.read_text(encoding="utf-8")
    preview = PREVIEW.read_text(encoding="utf-8")
    path_security = (ROOT / "src" / "dlw_path_security.c").read_text(
        encoding="utf-8"
    )
    assert "loading_pin_count" in ui
    assert "dluma_ui_loading_pin_preview_asset" in ui
    assert "dluma_ui_loading_release_preview_lease" in ui
    assert "DLUMA_CUSTOM_LOADING_DEF_MAX_BYTES" in ui
    assert "FILE_FLAG_OPEN_REPARSE_POINT" in ui
    assert "dluma_get_final_path_checked" in ui
    assert "GetFinalPathNameByHandleW" in path_security
    assert "dluma_custom_map_loading_parse_texture_definition" in ui
    assert "dluma_custom_map_loading_resource_path_valid" in ui
    assert "assets_pc" in ui
    assert "dluma_load_authored_" in ui
    assert "ready_count != 1U" in ui
    assert "nNumberOfLinks != 1U" in ui
    assert "authored_resource_identity" in ui
    assert "dluma_ui_loading_file_identity_equal" in ui
    assert "DLUMA_WORKSHOP_PREVIEW_DDS_GENERATION 4U" in (
        COMMON.read_text(encoding="utf-8")
    )
    common = COMMON.read_text(encoding="utf-8")
    assert "DLUMA_WORKSHOP_PREVIEW_CONTENT_WIDTH 512U" in common
    assert "DLUMA_WORKSHOP_PREVIEW_CONTENT_HEIGHT 288U" in common
    assert "DLUMA_WORKSHOP_PREVIEW_CONTENT_BOTTOM" in common
    assert "DlumaWicFactoryCreateBitmapClipperFn" in preview
    assert "dluma_custom_map_loading_cover_crop(" in preview


def test_ui_texture_registration_uses_rect_edges_not_extents():
    ui = UI.read_text(encoding="utf-8")
    generated = ui[
        ui.index("static int dluma_ui_loading_register_generated_preview("):
        ui.index("static int dluma_ui_loading_register_authored_texture(")
    ]
    authored = ui[
        ui.index("static int dluma_ui_loading_register_authored_texture("):
        ui.index("static void dluma_ui_loading_apply_background_fallback(")
    ]
    assert "crop_right = crop.x + crop.width;" in generated
    assert "crop_bottom = crop.y + crop.height;" in generated
    assert "(int)crop_right, (int)crop_bottom" in generated
    assert "(int)crop.width, (int)crop.height" not in generated
    assert "(int)definition->right, (int)definition->bottom" in authored
    assert "(int)width, (int)height" not in authored
    assert (
        "g_dluma_workshop_load_ui_texture(" in generated
        and "custom_map_loading_preview_alias" in generated
    )
    after_generated_load = generated[
        generated.index("g_dluma_workshop_load_ui_texture("):
    ]
    assert (
        "if (!g_dluma_workshop_is_ui_texture_loaded(level, &alias))"
        not in after_generated_load
    )
    after_authored_load = authored[
        authored.index("g_dluma_workshop_load_ui_texture("):
    ]
    assert (
        "if (!g_dluma_workshop_is_ui_texture_loaded(level, &alias))"
        not in after_authored_load
    )


def test_generated_metadata_bypasses_stock_localization_lookup_at_render():
    ui = UI.read_text(encoding="utf-8")
    finalize = ui[
        ui.index("static void dluma_ui_loading_finalize_parse("):
        ui.index("static unsigned char __fastcall dluma_ui_loading_parse_hook")
    ]
    render = ui[
        ui.index("static void dluma_ui_loading_apply_generated_text("):
        ui.index("static BYTE *__fastcall dluma_ui_loading_set_level_info_hook")
    ]
    hook = ui[
        ui.index("static BYTE *__fastcall dluma_ui_loading_set_level_info_hook"):
        ui.index("static void __fastcall dluma_ui_loading_clear_level_info_hook")
    ]
    assert "pending->generated_title = 1U;" in finalize
    assert "pending->generated_objective = 1U;" in finalize
    assert "pending->generated_description = 1U;" in finalize
    assert "DLUMA_BACKGROUND_MAP_NAME_TEXT_OFFSET" in render
    assert "DLUMA_BACKGROUND_MAP_NAME_ALT_TEXT_OFFSET" in render
    assert "DLUMA_BACKGROUND_OBJECTIVE_TEXT_OFFSET" in render
    assert "DLUMA_BACKGROUND_DESCRIPTION_TEXT_OFFSET" in render
    assert "dluma_ui_set_text_element_text(" in render
    assert "dluma_ui_set_text_element_long(" in render
    assert "Authored startprop identifiers are" in render
    assert hook.index("g_dluma_loading_set_level_info_original(") < hook.index(
        "dluma_ui_loading_apply_generated_text("
    )
    assert "DLUMA_BACKGROUND_OBJECTIVE_GROUP_OFFSET" in render
    assert "DLUMA_BACKGROUND_OBJECTIVE_HEADER_OFFSET" in render
    assert "g_dluma_set_element_visible(group, 1)" in render
    assert "g_dluma_set_element_visible(header, 1)" in render
    assert "DLUMA_LOADING_DIRECTIVE_OBJECTIVE" in render
    assert "custom_map_loading_forced_objective" in render


def test_registered_loading_alias_is_bound_to_concrete_background_image():
    ui = UI.read_text(encoding="utf-8")
    binder = ui[
        ui.index("static int dluma_ui_workshop_bind_texture_exports("):
        ui.index("static DlumaWorkshopTextureBackingStatus")
    ]
    background = ui[
        ui.index(
            "static void dluma_ui_loading_apply_registered_background("
        ):
        ui.index("static BYTE *__fastcall dluma_ui_loading_set_level_info_hook")
    ]
    hook = ui[
        ui.index("static BYTE *__fastcall dluma_ui_loading_set_level_info_hook"):
        ui.index("static void __fastcall dluma_ui_loading_clear_level_info_hook")
    ]
    assert "?SetTexture@IUIImage@@" in binder
    assert "set_image_texture_prefix" in binder
    assert "g_dluma_workshop_set_image_texture(image, &texture_name)" in (
        background
    )
    assert "g_dluma_workshop_force_sync_frame_data(image)" in background
    assert "presentation->preview_alias_registered" in background
    assert "presentation->authored_alias_registered" in background
    assert "DLUMA_BACKGROUND_IMAGE_OFFSET" in background
    assert "DLUMA_BACKGROUND_REFRESH_PENDING_OFFSET" in background
    assert "custom_map_loading_background_widget" in background
    assert "dluma_ui_loading_apply_registered_background(" in hook


def test_description_only_path_uses_stock_layout_and_hides_header():
    ui = UI.read_text(encoding="utf-8")
    hook = ui[
        ui.index("static BYTE *__fastcall dluma_ui_loading_set_level_info_hook"):
        ui.index("static void __fastcall dluma_ui_loading_clear_level_info_hook")
    ]
    assert "description_only" in hook
    assert 'DLUMA_LEVEL_INFO_OBJECTIVE_OFFSET, " "' in hook
    assert "g_dluma_loading_set_level_info_original(" in hook
    assert 'DLUMA_LEVEL_INFO_OBJECTIVE_OFFSET, ""' in hook
    assert 'DLUMA_BACKGROUND_OBJECTIVE_VALUE_OFFSET, ""' in hook
    assert "dluma_ui_set_text_element_text(objective_text, \"\")" in hook
    assert "g_dluma_set_element_visible(header, 0)" in hook
    assert "SmartText measurement, wrapping, and" in hook


def test_transition_failure_shutdown_and_lost_ownership_release_safely():
    ui = UI.read_text(encoding="utf-8")
    assert ui.count(
        "dluma_ui_loading_release_pending_presentation("
    ) >= 6
    assert "dluma_ui_loading_release_owned_presentation(" in ui
    assert "dluma_ui_loading_release_active_presentation()" in ui
    assert "dluma_ui_loading_clear_level_info_hook" in ui
    assert "ownership_lost_state_retained" in ui
    restore = ui[
        ui.index("static int dluma_ui_loading_restore_code_hook"):
        ui.index("static int dluma_ui_loading_release_code_hooks")
    ]
    assert "state->target, state->route," in restore
    assert "g_dluma_loading_runtime_disabled = 1" in ui
