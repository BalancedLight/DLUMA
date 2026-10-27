from pathlib import Path
import subprocess
import zipfile


ROOT = Path(__file__).resolve().parents[1]
XUI_PATH = ROOT / "PakAssets" / "XUI" / "DLUMA_SteamWorkshop.xui"
UI_PATH = ROOT / "src" / "dlw_ui.c"
MENU_PATH = ROOT / "src" / "dlw_workshop_menu.c"
HOST_PATH = ROOT / "src" / "dlw_host.c"
PACK_PATH = (
    ROOT / "build" / "package" / "Release"
    / "dluma.pak"
)
HARNESS_PATH = ROOT / "tests" / "workshop_menu_metadata_fake_harness.c"
REDIRECT_HARNESS_PATH = ROOT / "tests" / "xui_redirect_fake_harness.c"
REFRESH_HARNESS_PATH = ROOT / "tests" / "workshop_refresh_fake_harness.c"
ENRICHMENT_HARNESS_PATH = (
    ROOT / "tests" / "workshop_enrichment_fake_harness.c"
)
PREVIEW_PATH = ROOT / "src" / "dlw_workshop_preview.c"


def block(source: str, start: str, end: str) -> str:
    begin = source.index(start)
    finish = source.index(end, begin)
    return source[begin:finish]


def test_workshop_xui_preserves_native_controller_and_navigation_contract():
    raw = XUI_PATH.read_bytes()
    text = raw.decode("utf-8")
    assert not raw.startswith(b"\xef\xbb\xbf")
    assert b"\r\n" in raw
    assert b"\n" not in raw.replace(b"\r\n", b"")
    assert "<!--" not in text
    assert "<Id>MenuSteamWorkshop</Id>" in text
    assert "<ClassOverride>MenuSteamWorkshop</ClassOverride>" in text
    assert "<Id>DLUMAListBoxCustomMapV</Id>" not in text
    assert "<Id>DLUMAListButtonCustomMapSlotV</Id>" not in text
    assert "<Visual>DLUMAListBoxCustomMapV</Visual>" not in text
    assert "<Visual>DLUMAListButtonCustomMapSlotV</Visual>" not in text
    assert "<Visual>ListBoxCustomMapV</Visual>" in text
    assert "<Visual>ListBoxHtmlV</Visual>" not in text
    for object_id in (
        "L_Maps", "B_Start", "B_Workshop", "B_Refresh", "B_Back",
        "T_MapName", "G_Author", "T_AuthorValue",
        "G_WorkshopId", "T_WorkshopIdValue",
        "G_WorkshopIdCompact", "T_WorkshopIdCompactValue",
        "G_DescriptionScroll", "LBScrollBar",
        "T_Description",
    ):
        assert text.count(f"<Id>{object_id}</Id>") == 1
    assert "<Text>&amp;DLUMA_Workshop_Maps&amp;</Text>" in text
    assert "<Text>&amp;DLUMA_Common_Browse&amp;</Text>" in text
    assert "<Text>&amp;DLUMA_Common_NoDescription&amp;</Text>" in text


def test_workshop_xui_uses_six_row_custom_map_geometry_and_four_buttons():
    text = XUI_PATH.read_text(encoding="utf-8")
    assert "<Width>754.000000</Width>" in text
    assert text.count("<Height>471.000000</Height>") == 2
    assert "<Position>225.000000,105.000000,0.000000</Position>" in text
    assert "<Width>292.000000</Width>" in text
    assert "<Height>332.000000</Height>" in text
    assert "<Position>43.000000,104.000000,0.000000</Position>" in text
    assert "<Position>0.000000,467.200012,0.000000</Position>" in text
    assert "<Position>0.000000,486.688477,0.000000</Position>" in text
    assert text.count("<Height>215.000000</Height>") == 3
    assert text.count("<Visual>ButtonDialogV</Visual>") == 4
    start = text[text.index("<Id>B_Start</Id>") : text.index("<Id>B_Workshop</Id>")]
    workshop = text[text.index("<Id>B_Workshop</Id>") : text.index("<Id>B_Refresh</Id>")]
    refresh = text[text.index("<Id>B_Refresh</Id>") : text.index("<Id>B_Back</Id>")]
    back = text[text.index("<Id>B_Back</Id>") :]
    assert "<PressKey>" not in start
    assert "<PressKey>22531</PressKey>" in workshop
    assert "<PressKey>22530</PressKey>" in refresh
    assert "<PressKey>22529</PressKey>" in back


def test_workshop_description_uses_stock_masked_scroll_contract():
    text = XUI_PATH.read_text(encoding="utf-8")
    scroll = text[text.index("<Id>G_DescriptionScroll</Id>") :]
    assert "<ClassOverride>UIScrollView</ClassOverride>" in scroll
    assert "<Visual>LBScrollBarCraftingV</Visual>" in scroll
    assert "<ClassOverride>UIMaskedGroup</ClassOverride>" in scroll
    assert "<Id>G_MaskedGroup</Id>" in scroll
    assert "<ImageMaskMaterial>menu_mask_clip.mat</ImageMaskMaterial>" in scroll
    assert "<TextMaskMaterial>menu_text_clip.mat</TextMaskMaterial>" in scroll
    assert "<AARectangleMaskMaterial>menu_antialias_clip.mat</AARectangleMaskMaterial>" in scroll
    assert "<UseMask>true</UseMask>" in scroll
    assert "<AutoSizeToText>true</AutoSizeToText>" in scroll
    assert "<NavLeft>L_Maps</NavLeft>" in scroll


def test_description_scroll_resets_through_validated_windows_owner():
    source = UI_PATH.read_text(encoding="utf-8")
    assert "DLUMA_DEVTOOLS_WORKSHOP_SET_NORMALIZED_VIEWPORT_RVA 0x0113A0B0UL" in source
    binding = block(
        source,
        "static int dluma_ui_bind_normalized_scroll_view(void *scroll_view)",
        "static int dluma_ui_workshop_resolve_controls(void *screen)",
    )
    assert "expected_prefix" in binding
    assert "memcmp(target, expected_prefix" in binding
    assert "dluma_is_readable(scroll_view, 0x98U)" in binding
    assert binding.index("dluma_is_readable(scroll_view, 0x98U)") < binding.index(
        "if (g_dluma_workshop_set_normalized_viewport)"
    )
    assert "g_dluma_workshop_menu_description_scroll, 0.0f" in binding
    assert "dluma_ui_reset_validated_scroll_view(void *scroll_view)" in binding
    presenter = block(
        source,
        "static void dluma_ui_workshop_present(void *screen)",
        "static int dluma_ui_workshop_bind_texture_exports(void)",
    )
    assert "g_dluma_workshop_description_active_item != active_item" in presenter
    assert "dluma_ui_workshop_reset_description_scroll();" in presenter


def test_desc_parser_native_harness_covers_bounded_and_malformed_inputs():
    completed = subprocess.run(
        [
            "cmd.exe", "/d", "/s", "/c",
            "call tools\\build_workshop_menu_fake_tests.bat",
        ],
        cwd=ROOT,
        text=True,
        capture_output=True,
        check=False,
    )
    assert completed.returncode == 0, completed.stdout + completed.stderr
    exe = (
        ROOT / "build" / "workshop-menu-fake"
        / "workshop_menu_metadata_fake_harness.exe"
    )
    run = subprocess.run(
        [str(exe)], cwd=ROOT, text=True, capture_output=True, check=False
    )
    assert run.returncode == 0, run.stdout + run.stderr
    assert "workshop_menu_metadata_fake_harness: 20/20 ok" in run.stdout


def test_fillmaps_refresh_native_replay_preserves_identity_and_nearest_row():
    completed = subprocess.run(
        [
            "cmd.exe", "/d", "/s", "/c",
            "call tools\\build_workshop_refresh_fake_tests.bat",
        ],
        cwd=ROOT,
        text=True,
        capture_output=True,
        check=False,
    )
    assert completed.returncode == 0, completed.stdout + completed.stderr
    exe = (
        ROOT / "build" / "workshop-refresh-fake"
        / "workshop_refresh_fake_harness.exe"
    )
    run = subprocess.run(
        [str(exe)], cwd=ROOT, text=True, capture_output=True, check=False
    )
    assert run.returncode == 0, run.stdout + run.stderr
    assert "workshop_refresh_fake_harness: 16/16 ok" in run.stdout


def test_enrichment_native_replay_covers_offline_cache_and_independent_states():
    completed = subprocess.run(
        [
            "cmd.exe", "/d", "/s", "/c",
            "call tools\\build_workshop_enrichment_fake_tests.bat",
        ],
        cwd=ROOT,
        text=True,
        capture_output=True,
        check=False,
    )
    assert completed.returncode == 0, completed.stdout + completed.stderr
    exe = (
        ROOT / "build" / "workshop-enrichment-fake"
        / "workshop_enrichment_fake_harness.exe"
    )
    run = subprocess.run(
        [str(exe)], cwd=ROOT, text=True, capture_output=True, check=False
    )
    assert run.returncode == 0, run.stdout + run.stderr
    assert "workshop_enrichment_fake_harness: 18/18 ok" in run.stdout


def test_preview_native_harness_converts_png_to_engine_mipped_bgra_dds():
    completed = subprocess.run(
        [
            "cmd.exe", "/d", "/s", "/c",
            "call tools\\build_workshop_preview_fake_tests.bat",
        ],
        cwd=ROOT,
        text=True,
        capture_output=True,
        check=False,
    )
    assert completed.returncode == 0, completed.stdout + completed.stderr
    exe = (
        ROOT / "build" / "workshop-preview-fake"
        / "workshop_preview_fake_harness.exe"
    )
    run = subprocess.run(
        [str(exe)], cwd=ROOT, text=True, capture_output=True, check=False
    )
    assert run.returncode == 0, run.stdout + run.stderr
    assert "workshop_preview_fake_harness: 64/64 ok" in run.stdout


def test_dynamic_texture_native_harness_preserves_gpu_asset_lifetime_contract():
    completed = subprocess.run(
        [
            "cmd.exe", "/d", "/s", "/c",
            "call tools\\build_workshop_dynamic_texture_fake_tests.bat",
        ],
        cwd=ROOT,
        text=True,
        capture_output=True,
        check=False,
    )
    assert completed.returncode == 0, completed.stdout + completed.stderr
    exe = (
        ROOT / "build" / "workshop-dynamic-texture-fake"
        / "workshop_dynamic_texture_fake_harness.exe"
    )
    run = subprocess.run(
        [str(exe)], cwd=ROOT, text=True, capture_output=True, check=False
    )
    assert run.returncode == 0, run.stdout + run.stderr
    assert "workshop_dynamic_texture_fake_harness: 18/18 ok" in run.stdout


def test_vfs_refresh_native_harness_snapshots_and_refreshes_late_dds_files():
    completed = subprocess.run(
        [
            "cmd.exe", "/d", "/s", "/c",
            "call tools\\build_workshop_vfs_refresh_fake_tests.bat",
        ],
        cwd=ROOT,
        text=True,
        capture_output=True,
        check=False,
    )
    assert completed.returncode == 0, completed.stdout + completed.stderr
    exe = (
        ROOT / "build" / "workshop-vfs-refresh-fake"
        / "workshop_vfs_refresh_fake_harness.exe"
    )
    run = subprocess.run(
        [str(exe)], cwd=ROOT, text=True, capture_output=True, check=False
    )
    assert run.returncode == 0, run.stdout + run.stderr
    assert "workshop_vfs_refresh_fake_harness: 26/26 ok" in run.stdout


def test_xui_redirect_native_harness_covers_validation_and_ownership():
    completed = subprocess.run(
        [
            "cmd.exe", "/d", "/s", "/c",
            "call tools\\build_xui_redirect_fake_tests.bat",
        ],
        cwd=ROOT,
        text=True,
        capture_output=True,
        check=False,
    )
    assert completed.returncode == 0, completed.stdout + completed.stderr
    exe = (
        ROOT / "build" / "xui-redirect-fake"
        / "xui_redirect_fake_harness.exe"
    )
    run = subprocess.run(
        [str(exe)], cwd=ROOT, text=True, capture_output=True, check=False
    )
    assert run.returncode == 0, run.stdout + run.stderr
    assert "xui_redirect_fake_harness: 12/12 ok" in run.stdout


def test_metadata_matching_is_unique_and_never_guesses_ambiguous_roots():
    source = MENU_PATH.read_text(encoding="utf-8")
    finder = block(
        source,
        "int dluma_workshop_menu_find_metadata(",
        "static int dluma_workshop_parse_u64",
    )
    assert "matches == 1U" in finder
    assert "return 0;" in finder
    assert "item->subscribed" in finder
    assert "item->workshop_id" in finder
    assert "native_title" in finder
    collector = block(
        source,
        "static unsigned int dluma_workshop_collect_project(",
        "void dluma_workshop_menu_refresh_metadata",
    )
    assert "title_from_desc" in collector
    assert "dluma_workshop_wide_decimal_id" in collector
    assert "if (!item->subscribed)" in collector
    assert 'project, L"out\\\\preview.png"' in collector


def test_invalid_workshop_project_fix_removes_native_rows_before_presentation():
    menu = MENU_PATH.read_text(encoding="utf-8")
    common = (ROOT / "src" / "dlw_common.h").read_text(encoding="utf-8")
    assert "unsigned char has_data_maps;" in common

    collector = block(
        menu,
        "static unsigned int dluma_workshop_collect_project(",
        "void dluma_workshop_menu_refresh_metadata",
    )
    assert "WCHAR maps_path[DLUMA_MAX_WPATH];" in collector
    assert 'L"data\\\\maps"' in collector
    assert "dluma_workshop_local_optional_directory_stamp" in collector
    assert "data_stamp.present && maps_stamp.present" in collector
    assert "include_empty_project" in collector
    assert "items, count, candidate, 1" in menu
    assert "&data_stamp, &maps_stamp" in collector
    assert "&entry->data, data" in menu
    assert "&entry->maps, maps" in menu

    source = UI_PATH.read_text(encoding="utf-8")
    rejected = block(
        source,
        "static int dluma_ui_workshop_row_rejected_by_game_fix(",
        "static unsigned int dluma_ui_workshop_filter_invalid_projects(",
    )
    assert "dluma_custom_main_menu_project_marked" in rejected
    assert "g_dluma.config.hide_invalid_workshop_projects_enabled &&" in rejected
    assert "metadata.exact_project && !metadata.has_data_maps" in rejected
    assert "dluma_workshop_menu_find_metadata" in rejected
    assert "g_dluma_main_menu_set_relative_pos" not in rejected
    assert rejected.index("dluma_custom_main_menu_project_marked") < rejected.index(
        "g_dluma.config.hide_invalid_workshop_projects_enabled &&"
    )

    native_filter = block(
        source,
        "static unsigned int dluma_ui_workshop_filter_invalid_projects(",
        "static int dluma_ui_workshop_is_selectable_index(",
    )
    # The pass must always run because custommenu.json is an unconditional
    # Custom Maps exclusion marker; the optional invalid-project gate remains
    # inside dluma_ui_workshop_row_rejected_by_game_fix.
    assert "!g_dluma.config.hide_invalid_workshop_projects_enabled" not in native_filter
    assert "!g_dluma_list_remove_item" in native_filter
    assert "for (i = count; i > 0; --i)" in native_filter
    assert "const int index = i - 1;" in native_filter
    assert "dluma_ui_workshop_row_rejected_by_game_fix(&row)" in native_filter
    assert "g_dluma_list_remove_item(list, index, 1)" in native_filter
    assert '"workshop_invalid_projects_filtered"' in native_filter

    assert "DLUMA_DEVTOOLS_LIST_REMOVE_ITEM_RVA 0x01122BB0UL" in source
    assert '"list_remove_item_prefix"' in source
    assert "DlumaListRemoveItemFn" in source
    refresh = block(
        source,
        "static void dluma_ui_workshop_refresh_now(",
        "static void __fastcall dluma_ui_workshop_fill_maps_hook(",
    )
    assert refresh.index("dluma_ui_workshop_filter_invalid_projects(list)") < refresh.index(
        "dluma_ui_workshop_restore_refresh_state("
    ) < refresh.index("dluma_ui_workshop_present(screen)")

    presenter = block(
        source,
        "static void dluma_ui_workshop_present(",
        "static int dluma_ui_workshop_bind_texture_exports",
    )
    assert "dluma_ui_workshop_enable_selector_row(&row);" in presenter
    assert "dluma_ui_workshop_row_rejected_by_game_fix" not in presenter
    assert "dluma_ui_workshop_compact_selector_rows" not in presenter
    assert "g_dluma_main_menu_set_relative_pos(row.item" not in presenter
    assert "dluma_ui_set_element_visible(row.item, 1);" not in presenter

    selectable = block(
        source,
        "static int dluma_ui_workshop_is_selectable_index(",
        "static int dluma_ui_workshop_nearest_selectable_index(",
    )
    assert "!row.divider;" in selectable
    assert "row_rejected_by_game_fix" not in selectable

    texture = source[
        source.rindex("static void dluma_ui_workshop_texture_asset_tick") :
        source.index("static void dluma_ui_workshop_refresh_now")
    ]
    assert "row_rejected_by_game_fix" not in texture
    assert "set_row_selector_visible" not in source
    assert "compact_selector_rows" not in source


def test_remote_enrichment_is_dynamic_nonblocking_and_bounded():
    source = MENU_PATH.read_text(encoding="utf-8")
    assert "DLUMA_WORKSHOP_MENU_QUERY_BATCH 8U" in source
    assert "DLUMA_WORKSHOP_MENU_HTTP_SLOTS 2U" in source
    assert "DLUMA_WORKSHOP_MENU_IMAGE_JOBS 2U" in source
    assert "DLUMA_WORKSHOP_MENU_MAX_IMAGE_BYTES (8UL * 1024UL * 1024UL)" in source
    assert 'GetModuleHandleW(L"steam_api64.dll")' in source
    assert "GetProcAddress(api->module" in source
    assert "DLUMA_STEAM_API_TIMESTAMP" in source
    assert "DLUMA_STEAM_API_IMAGE_SIZE" in source
    assert 'dluma_workshop_text_prefix_icase(url, "https://")' in source
    assert 'dluma_workshop_text_prefix_icase(url, "http://")' in source
    assert "body_size <= DLUMA_WORKSHOP_MENU_MAX_IMAGE_BYTES" in source
    assert "valid_image = png ||" in source
    assert '"CSteamUGCQueryCompleted_t_SetCallResult"' in source
    assert '"CSteamUGCQueryCompleted_t_RemoveCallResult"' in source
    assert '"SteamAPI_RegisterCallResult"' in source
    assert '"SteamAPI_UnregisterCallResult"' in source
    assert "SteamAPI_RunCallbacks" not in source
    assert "SteamAPI_ISteamUtils_IsAPICallCompleted" not in source
    assert "SteamAPI_ISteamUtils_GetAPICallResult" not in source
    assert "dluma_workshop_ugc_call_result_callback" in source
    assert "dluma_workshop_http_call_result_run_call" in source
    assert "dluma_workshop_menu_worker_tick" in HOST_PATH.read_text(encoding="utf-8")
    assert "dluma_workshop_menu_game_thread_tick" in UI_PATH.read_text(encoding="utf-8")
    assert "dluma_workshop_menu_presentation_revision" in source
    assert "#pragma comment(lib" not in source


def test_registered_callbacks_only_publish_bounded_pod_and_shutdown_unregisters():
    source = MENU_PATH.read_text(encoding="utf-8")
    callbacks = block(
        source,
        "static void __fastcall dluma_workshop_http_call_result_run_call(",
        "static int dluma_workshop_text_prefix_icase(",
    )
    assert "memcpy(&callback->result" in callbacks
    assert "memcpy(&g_dluma_workshop_query_completion" in callbacks
    assert "dluma_log_" not in callbacks
    assert "dluma_write_" not in callbacks
    assert "dluma_alloc" not in callbacks
    shutdown = block(
        source,
        "void dluma_workshop_menu_shutdown(void)",
        "#endif",
    )
    assert shutdown.index("remove_ugc_call_result") < shutdown.index(
        "release_query"
    )
    assert shutdown.index("unregister_call_result") < shutdown.index(
        "release_http_request"
    )
    assert "g_dluma_workshop_callback_active_calls" in shutdown
    assert "GetTickCount64() - callback_wait_started < 2000ULL" in shutdown


def test_enrichment_and_texture_progress_on_both_authoritative_tick_sources():
    source = UI_PATH.read_text(encoding="utf-8")
    tick = block(
        source,
        "static void dluma_ui_game_thread_tick_impl(unsigned int tick_source)\n{",
        "int dluma_ui_start(void)",
    )
    enrichment_call = tick.index("dluma_workshop_menu_game_thread_tick();")
    ui_exports = tick.index("if (!g_dluma.engine.ui_exports_ready)")
    assert ui_exports < enrichment_call
    assert "if (tick_source == DLUMA_CUSTOM_MAP_TICK_SOURCE_ON_FRAME)\n        dluma_workshop_menu_game_thread_tick();" not in tick
    assert "workshop_enrichment_tick_source" in tick
    assert '"on_frame"' in tick
    assert '"fallback_timer"' in tick
    assert "dluma_workshop_menu_presentation_revision()" in tick
    assert "dluma_ui_workshop_present(g_dluma_workshop_menu_screen);" in tick
    texture_call = tick.index("dluma_ui_workshop_texture_asset_tick(")
    assert "if (tick_source == DLUMA_CUSTOM_MAP_TICK_SOURCE_ON_FRAME)" not in tick[
        max(0, texture_call - 120):texture_call
    ]
    post = block(
        source,
        "static void __fastcall dluma_ui_workshop_post_hook(void *screen)",
        "static void __fastcall dluma_ui_workshop_init_hook(void *screen)",
    )
    stock = post[post.index("dluma_ui_is_stock_workshop_screen(screen)"):]
    assert stock.index("dluma_ui_workshop_present(screen);") < stock.index(
        "dluma_workshop_menu_game_thread_tick();"
    )
    assert '"workshop_post_update"' in stock
    assert "dluma_workshop_menu_presentation_revision()" in stock
    assert "dluma_ui_workshop_texture_asset_tick(" in stock


def test_details_persona_and_preview_capabilities_degrade_independently():
    source = MENU_PATH.read_text(encoding="utf-8")
    resolver = block(
        source,
        "static int dluma_workshop_resolve_steam(void)",
        "static void dluma_workshop_build_cache_path(",
    )
    assert "details_available" in resolver
    assert "author_available" in resolver
    assert "preview_available" in resolver
    assert "api->ready = api->details_available" in resolver
    assert "workshop_steam_details_api" in resolver
    assert "workshop_steam_persona_api" in resolver
    assert "workshop_steam_preview_api" in resolver
    assert "required_export_missing_cache_only" not in resolver
    tick = block(
        source,
        "void dluma_workshop_menu_game_thread_tick(void)",
        "static int dluma_workshop_write_binary(",
    )
    assert "if (api->author_available)" in tick
    assert "if (api->preview_available)" in tick
    assert tick.index("dluma_workshop_apply_cache_snapshot();") < tick.index(
        "dluma_workshop_resolve_steam()"
    )


def test_preview_cache_is_stable_atomic_and_available_before_steam():
    source = MENU_PATH.read_text(encoding="utf-8")
    assert r'L"dluma_runtime\\workshop_thumbnails"' in source
    assert "dluma_workshop_build_cache_path" in source
    assert 'L".png"' in source
    assert 'L".jpg"' in source
    assert 'published_id, L".meta"' in source
    assert '"version=1\\r\\n"' in source
    for field in (
        "published_id=", "updated_time=", "format=", "size=", "sha256=",
    ):
        assert field in source
    assert "DLUMA_WORKSHOP_MENU_MAX_CACHE_ENTRIES 256U" in source
    assert "DLUMA_WORKSHOP_MENU_CACHE_META_MAX_BYTES 1024U" in source
    assert "dluma_sha256_file_hex" in source
    assert "MOVEFILE_REPLACE_EXISTING" in source
    assert "MOVEFILE_WRITE_THROUGH" in source
    assert "dluma_workshop_scan_cache_worker" in source
    assert "dluma_workshop_remove_interrupted_cache_temps" in source
    assert 'L".png.tmp"' in source
    assert 'L".jpg.tmp"' in source
    assert 'L".dds.tmp"' in source
    assert 'L".meta.tmp"' in source
    assert "workshop_preview_cache_temp_removed" in source
    tick = block(
        source,
        "void dluma_workshop_menu_game_thread_tick(void)",
        "static int dluma_workshop_write_binary(",
    )
    assert tick.index("dluma_workshop_apply_cache_snapshot();") < tick.index(
        "dluma_workshop_resolve_steam()"
    )
    assert "dluma_workshop_enrichment_cached_preview_survives_failure" in source
    assert "workshop_preview_cache_hit" in source
    assert "workshop_preview_cache_updated" in source
    worker = block(
        source,
        "void dluma_workshop_menu_worker_tick(void)",
        "void dluma_workshop_menu_shutdown(void)",
    )
    assert "dluma_mkdirs(directory)" in worker
    assert worker.index("dluma_mkdirs(directory)") < worker.index(
        "dluma_workshop_scan_cache_worker();"
    )
    assert "dluma_workshop_preview_prepare_dds(" in worker
    assert "dluma_workshop_build_hashed_dds_path(" in worker
    assert "job->published_id, sha256" in worker


def test_preview_conversion_matches_proven_engine_dds_contract_without_imports():
    preview = PREVIEW_PATH.read_text(encoding="utf-8")
    common = (ROOT / "src" / "dlw_common.h").read_text(encoding="utf-8")
    assert 'LoadLibraryW(L"windowscodecs.dll")' in preview
    assert 'LoadLibraryW(L"ole32.dll")' in preview
    assert '"WICCreateImagingFactory_Proxy"' in preview
    assert '"CoInitializeEx"' in preview
    assert "g_dluma_wic_pixel_format_32bpp_bgra" in preview
    assert "DLUMA_WORKSHOP_PREVIEW_EDGE 512U" in common
    assert "DLUMA_WORKSHOP_PREVIEW_CONTENT_WIDTH 512U" in common
    assert "DLUMA_WORKSHOP_PREVIEW_CONTENT_HEIGHT 288U" in common
    assert "DLUMA_WORKSHOP_PREVIEW_CONTENT_TOP" in common
    assert "DLUMA_WORKSHOP_PREVIEW_STRIDE" in preview
    assert "DLUMA_DDS_FLAGS_MIPPED_2D_RGBA 0x000A1007UL" in preview
    assert "DLUMA_DDS_PIXEL_FORMAT_RGBA 0x00000041UL" in preview
    assert "DLUMA_DDS_CAPS_MIPPED_TEXTURE 0x00401008UL" in preview
    assert "header[0] = 0x20534444UL" in preview
    assert "header[2] = DLUMA_DDS_FLAGS_MIPPED_2D_RGBA" in preview
    assert "header[5] = DLUMA_WORKSHOP_PREVIEW_PIXEL_BYTES" in preview
    assert "header[6] = 0U" in preview
    assert "header[20] = DLUMA_DDS_PIXEL_FORMAT_RGBA" in preview
    assert "header[22] = 32U" in preview
    assert "header[23] = 0x00ff0000UL" in preview
    assert "header[24] = 0x0000ff00UL" in preview
    assert "header[25] = 0x000000ffUL" in preview
    assert "header[26] = 0xff000000UL" in preview
    assert "header[27] = DLUMA_DDS_CAPS_MIPPED_TEXTURE" in preview
    assert "NVT3" not in preview
    assert "DLUMA_WORKSHOP_PREVIEW_STRIDE;" not in preview
    assert "dluma_workshop_preview_imagelib_payload_consumption" in preview
    assert "MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH" in preview
    assert "#pragma comment(lib" not in preview


def test_imagelib_decode_probe_is_exact_build_dynamic_and_one_shot_per_asset():
    source = UI_PATH.read_text(encoding="utf-8")
    resolver = block(
        source,
        "static int dluma_ui_workshop_bind_imagelib_exports(void)",
        "static DlumaWorkshopImageLibProbeStatus",
    )
    probe = block(
        source,
        "static DlumaWorkshopImageLibProbeStatus\n"
        "dluma_ui_workshop_probe_imagelib_decode(",
        "static DlumaWorkshopTextureBackingStatus",
    )
    pipeline = block(
        source,
        "dluma_ui_workshop_update_texture_asset(",
        "static DlumaWorkshopRowTextureBinding *",
    )
    assert "DLUMA_DEVTOOLS_IMAGELIB_TIMESTAMP 0x623394DCUL" in source
    assert "DLUMA_DEVTOOLS_IMAGELIB_IMAGE_SIZE 0x00068000UL" in source
    assert 'GetModuleHandleW(L"imagelib_x64_rwdi.dll")' in resolver
    assert 'GetProcAddress(' in resolver
    assert '"IL_LoadImage"' in resolver
    assert '"IL_DestroyImage"' in resolver
    assert "load_prefix" in resolver
    assert "destroy_prefix" in resolver
    assert "dluma_ui_workshop_loaded_module_identity" in resolver
    assert "g_dluma_workshop_il_load_image(" in probe
    assert "g_dluma_workshop_il_destroy_image(&image)" in probe
    assert "workshop_preview_imagelib_export" in resolver
    assert "workshop_preview_imagelib_probe" in probe
    assert "asset->progress.state" in pipeline
    assert "dluma_workshop_texture_asset_tick_file_proof(" in pipeline
    assert pipeline.count("dluma_ui_workshop_imagelib_probe_callback") == 1
    assert pipeline.count("dluma_ui_workshop_backing_probe_callback") == 1
    assert "#pragma comment(lib" not in source


def test_ui_texture_loader_uses_mounted_cache_dds_and_safe_consumed_material_value():
    source = UI_PATH.read_text(encoding="utf-8")
    binding = block(
        source,
        "static int dluma_ui_workshop_bind_texture_exports(void)",
        "static int dluma_ui_workshop_set_picture",
    )
    assert "?DoesTextureFileExist@ILevel@@" in binding
    assert "?IsUITextureLoaded@ILevel@@" in binding
    assert "?GetIUIManager@ILevel@@" in binding
    assert "?RequestTexturePrefetch@IUIManager@@" in binding
    assert "?IsStreamingTextureLoaded@IUIManager@@" in binding
    assert "?ForceSyncFrameData@IUIElement@@" in binding
    assert "request_prefetch_prefix" in binding
    assert "streaming_loaded_prefix" in binding
    asset_pipeline = block(
        source,
        "dluma_ui_workshop_get_texture_asset(",
        "static DlumaWorkshopRowTextureBinding *",
    )
    assert "g_dluma_workshop_does_texture_file_exist(level, &file)" in asset_pipeline
    assert "workshop_preview_vfs_visibility" in asset_pipeline
    assert "visible_without_refresh" in asset_pipeline
    assert "visible_after_refresh" in asset_pipeline
    assert "memset(&material, 0, sizeof(material));" in asset_pipeline
    assert "dluma_ttl_string_init(&material" not in asset_pipeline
    assert "g_dluma_workshop_is_ui_texture_loaded(" in asset_pipeline
    assert "asset->level, &alias" in asset_pipeline
    assert "g_dluma_workshop_request_texture_prefetch(" in asset_pipeline
    assert "g_dluma_workshop_is_streaming_texture_loaded" in asset_pipeline
    assert "pending_fallback_retained" in asset_pipeline
    assert "dluma_workshop_texture_asset_mark_ready" in asset_pipeline
    core = (
        ROOT / "src" / "dlw_workshop_texture_core.h"
    ).read_text(encoding="utf-8")
    file_proof = block(
        core,
        "dluma_workshop_texture_asset_tick_file_proof(",
        "static unsigned long long",
    )
    assert file_proof.index("imagelib_probe(") < file_proof.index(
        "backing_probe("
    )
    assert asset_pipeline.index(
        "dluma_workshop_texture_asset_tick_file_proof("
    ) < asset_pipeline.index(
        "g_dluma_workshop_load_ui_texture"
    )
    assert asset_pipeline.index("g_dluma_workshop_request_texture_prefetch") < asset_pipeline.index(
        "g_dluma_workshop_is_streaming_texture_loaded"
    ) < asset_pipeline.index("dluma_workshop_texture_asset_mark_ready")
    assert "return DLUMA_WORKSHOP_TEXTURE_LOAD_PENDING;" in asset_pipeline
    assert "prefetch_poll_count" not in asset_pipeline
    load_row = block(
        source,
        "dluma_ui_workshop_load_row_texture_asset(",
        "static void dluma_ui_workshop_texture_asset_tick(",
    )
    assert "const WCHAR *file_name = file_path;" in load_row
    assert "*cursor == L'\\\\' || *cursor == L'/'" in load_row
    menu = MENU_PATH.read_text(encoding="utf-8")
    collector = block(
        menu,
        "static int dluma_workshop_prepare_local_preview(",
        "void dluma_workshop_menu_refresh_metadata",
    )
    assert 'L"local_"' in collector
    assert "dluma_sha256_file_hex(" in collector
    assert "digest_wide" in collector
    assert "dluma_workshop_preview_prepare_dds(" in collector
    assert "dluma_workshop_append_dds_generation_suffix" in collector
    assert "DLUMA_WORKSHOP_PREVIEW_DDS_GENERATION" in menu
    assert "dluma_workshop_remove_stale_dds_generations" in menu
    assert "workshop_preview_stale_dds_removed" in menu


def test_preview_cache_is_a_process_lifetime_loader_owned_vfs_source():
    common = (ROOT / "src" / "dlw_common.h").read_text(encoding="utf-8")
    pack = (ROOT / "src" / "dlw_pack.c").read_text(encoding="utf-8")
    isolation = (
        ROOT
        / "src"
        / "fixes"
        / "custom_map_asset_isolation"
        / "CustomMapAssetIsolationFix.cpp"
    ).read_text(encoding="utf-8")
    assert "dluma_workshop_preview_cache_source_snapshot" in common
    assert 'L"dluma_runtime\\\\workshop_thumbnails"' in pack
    assert "add_source(g_dluma_workshop_preview_cache_source_path, 8u)" in pack
    assert "mounted_process_lifetime" in pack
    assert "workshop_preview_cache_source_left_mounted_until_process_exit" in pack
    assert "fs::rem_source" not in block(
        pack,
        "static int dluma_mount_workshop_preview_cache_source(",
        "int dluma_mount_content_pack(void)",
    )
    assert "g_normalized_loader_preview_cache_path" in isolation
    assert "IsExactLoaderPreviewCacheSourcePath" in isolation
    assert "cold::BaselineSourceKind::ApprovedGlobal" in isolation
    assert "exact-loader-preview-cache-path" in isolation


def test_preview_cache_refresh_is_source_specific_coalesced_and_ordered():
    common = (ROOT / "src" / "dlw_common.h").read_text(encoding="utf-8")
    pack = (ROOT / "src" / "dlw_pack.c").read_text(encoding="utf-8")
    menu = MENU_PATH.read_text(encoding="utf-8")
    ui = UI_PATH.read_text(encoding="utf-8")
    core = (
        ROOT / "src" / "dlw_workshop_vfs_refresh_core.h"
    ).read_text(encoding="utf-8")

    assert "dluma_workshop_preview_cache_note_dds_published" in common
    assert "dluma_workshop_preview_cache_refresh_pending_on_game_thread" in common
    assert "dluma_workshop_preview_cache_refresh_for_visibility_retry" in common
    assert '"?refresh@fs@@YAXPEBD@Z"' in pack
    assert '"?refresh@fs@@YAXXZ"' not in pack
    assert "DLUMA_DEVTOOLS_FILESYSTEM_TIMESTAMP 0x623394CDUL" in pack
    assert "DLUMA_DEVTOOLS_FILESYSTEM_IMAGE_SIZE 0x00060000UL" in pack
    assert "expected_prefix" in pack
    assert "memcmp(target, expected_prefix" in pack
    assert "workshop_preview_vfs_refresh_export" in pack
    assert "g_dluma_workshop_preview_refresh_path(" in pack
    assert pack.count(
        "add_source(g_dluma_workshop_preview_cache_source_path, 8u)"
    ) == 1
    assert '"?rem_source@fs@@' not in pack
    assert "dluma_workshop_vfs_refresh_note_published" in core
    assert "dluma_workshop_vfs_refresh_begin" in core
    assert "dluma_workshop_vfs_refresh_complete" in core

    tick = block(
        menu,
        "void dluma_workshop_menu_game_thread_tick(void)",
        "ULONGLONG dluma_workshop_menu_presentation_revision(void)",
    )
    assert tick.index(
        "dluma_workshop_preview_cache_refresh_pending_on_game_thread"
    ) < tick.index(
        "dluma_workshop_consume_image_jobs"
    ) < tick.index(
        "dluma_workshop_apply_cache_snapshot"
    )

    local = block(
        menu,
        "static int dluma_workshop_prepare_local_preview(",
        "static unsigned int dluma_workshop_collect_project(",
    )
    assert local.index("dluma_workshop_preview_validate_dds(output_path)") < (
        local.index("dluma_workshop_preview_cache_note_dds_published")
    )
    scan = block(
        menu,
        "static void dluma_workshop_scan_cache_worker(void)",
        "static int dluma_workshop_write_cache_metadata(",
    )
    assert scan.index(
        "dluma_workshop_preview_cache_note_dds_published"
    ) < scan.index(
        "dluma_atomic_increment(&g_dluma_workshop_cache_sequence)"
    )
    worker = block(
        menu,
        "void dluma_workshop_menu_worker_tick(void)",
        "void dluma_workshop_menu_shutdown(void)",
    )
    assert worker.index(
            "dluma_workshop_preview_cache_note_dds_published"
        ) < worker.rindex(
            "dluma_atomic_exchange(&job->state, 2)"
        )

    loader = block(
        ui,
        "dluma_ui_workshop_get_texture_asset(",
        "static DlumaWorkshopRowTextureBinding *",
    )
    assert loader.count(
        "g_dluma_workshop_does_texture_file_exist(level, &file)"
    ) == 2
    assert loader.index(
        "dluma_workshop_preview_cache_refresh_for_visibility_retry"
    ) < loader.index(
        "dluma_workshop_texture_asset_mark_vfs_visible"
    )
    update = block(
        ui,
        "dluma_ui_workshop_update_texture_asset(",
        "static DlumaWorkshopRowTextureBinding *",
    )
    assert update.index(
        "dluma_workshop_texture_asset_tick_file_proof("
    ) < update.index("g_dluma_workshop_load_ui_texture")
    assert "visible_without_refresh" in loader
    assert "visible_after_refresh" in loader
    assert "still_missing_after_refresh" in loader


def test_fillmaps_descriptor_hook_is_exact_optional_and_reversible():
    source = UI_PATH.read_text(encoding="utf-8")
    for expected in (
        "DLUMA_DEVTOOLS_WORKSHOP_FILL_MAPS_DESCRIPTOR_RVA 0x01D69EB0UL",
        "DLUMA_DEVTOOLS_WORKSHOP_FILL_MAPS_OWNER_RVA 0x01D69CA0UL",
        "DLUMA_DEVTOOLS_WORKSHOP_FILL_MAPS_NAME_OFFSET 0x08UL",
        "DLUMA_DEVTOOLS_WORKSHOP_FILL_MAPS_OWNER_OFFSET 0x10UL",
        "DLUMA_DEVTOOLS_WORKSHOP_FILL_MAPS_CALLBACK_OFFSET 0x20UL",
        "DLUMA_DEVTOOLS_WORKSHOP_FILL_MAPS_CALLBACK_RVA 0x0091B530UL",
        "DLUMA_DEVTOOLS_WORKSHOP_FILL_MAPS_IMPLEMENTATION_RVA 0x0091B880UL",
        "DLUMA_DEVTOOLS_WORKSHOP_FOCUS_GET_VTABLE_OFFSET 0x0960UL",
        "DLUMA_DEVTOOLS_WORKSHOP_FOCUS_SET_VTABLE_OFFSET 0x0968UL",
        "DLUMA_DEVTOOLS_WORKSHOP_FIRST_VISIBLE_GET_VTABLE_OFFSET 0x0970UL",
        "DLUMA_DEVTOOLS_WORKSHOP_FIRST_VISIBLE_SET_VTABLE_OFFSET 0x0978UL",
        "DLUMA_DEVTOOLS_WORKSHOP_FOCUS_GET_RVA 0x0072DB50UL",
        "DLUMA_DEVTOOLS_WORKSHOP_FOCUS_SET_RVA 0x01125640UL",
        "DLUMA_DEVTOOLS_WORKSHOP_FIRST_VISIBLE_GET_RVA 0x001BEED0UL",
        "DLUMA_DEVTOOLS_WORKSHOP_FIRST_VISIBLE_SET_RVA 0x01126A40UL",
    ):
        assert expected in source
    optional = block(
        source,
        "static int dluma_ui_install_workshop_fill_maps_hook(void)",
        "static int dluma_ui_install_registry_hooks(void)",
    )
    assert "descriptor + DLUMA_DEVTOOLS_WORKSHOP_FILL_MAPS_NAME_OFFSET" in optional
    assert "descriptor + DLUMA_DEVTOOLS_WORKSHOP_FILL_MAPS_OWNER_OFFSET" in optional
    assert 'memcmp(name, "FillMaps", 9U)' in optional
    assert "callback_prefix" in optional
    assert "implementation_prefix" in optional
    assert "focus_get_prefix" in optional
    assert "focus_set_prefix" in optional
    assert "first_visible_get_prefix" in optional
    assert "first_visible_set_prefix" in optional
    assert "DLUMA_VALIDATE_WORKSHOP_LIST_METHOD" in optional
    assert "g_dluma_workshop_fill_maps_hook_attempted" in optional
    assert "g_dluma_workshop_fill_maps_hook_installed" in optional
    assert "g_dluma_workshop_fill_maps_hook_owned" in optional
    assert "g_dluma_registry_runtime_disabled" not in optional
    mandatory = block(
        source,
        "static int dluma_ui_install_registry_hooks(void)",
        "static void dluma_ui_restore_registry_hooks_now(void)",
    )
    assert "void **slots[10]" in mandatory
    assert "void *expected[10]" in mandatory
    assert "void *hooks[10]" in mandatory
    assert "FillMaps_descriptor_callback" not in mandatory
    assert "DLUMA_VALIDATE_PREFIX(\"FillMaps" not in mandatory
    assert "for (j = patched; j > 0; --j)" in mandatory
    assert "slots[j - 1U]" in mandatory
    assert "transaction_rollback_failed_retained" in mandatory
    assert "g_dluma_registry_runtime_disabled = 1" in mandatory
    assert mandatory.index("g_dluma_registry_hooks_installed = 1") < mandatory.index(
        "(void)dluma_ui_install_workshop_fill_maps_hook();"
    )
    assert mandatory.index(
        "(void)dluma_ui_install_workshop_fill_maps_hook();"
    ) < mandatory.rindex("return 1;")
    refresh = block(
        source,
        "static void dluma_ui_workshop_refresh_now(",
        "static void __fastcall dluma_ui_workshop_fill_maps_hook",
    )
    assert refresh.index("dluma_ui_workshop_capture_refresh_state") < refresh.index(
        "original(screen);"
    )
    assert refresh.index("original(screen);") < refresh.index(
        "dluma_ui_workshop_restore_refresh_state"
    )
    assert refresh.count("original(screen);") == 1
    hook = block(
        source,
        "static void __fastcall dluma_ui_workshop_fill_maps_hook",
        "static void dluma_ui_workshop_drain_refresh_diagnostic",
    )
    assert "suppressed_visible_rebuild" not in hook
    assert "g_dluma_workshop_periodic_refresh_suppressed" in hook
    assert "dluma_ui_workshop_capture_refresh_state" not in hook
    assert hook.count("original(screen);") == 1
    assert "dluma_log_" not in hook
    assert "dluma_workshop_refresh_choose_index" in source
    assert "dluma_workshop_refresh_clamp_first_visible" in source
    assert "DLUMA_WORKSHOP_REFRESH_DIAGNOSTIC_LOG_LIMIT 16" in source
    restore = block(
        source,
        "static void dluma_ui_restore_registry_hooks_now(void)",
        "static void dluma_ui_uninstall_registry_hooks(void)",
    )
    assert restore.index("g_dluma_workshop_fill_maps_hook_installed") < restore.index(
        "g_dluma_workshop_uses_background_screen_name_vtable_slot"
    )


def test_workshop_refresh_is_deferred_and_periodic_rebuilds_are_suppressed():
    source = UI_PATH.read_text(encoding="utf-8")
    button = block(
        source,
        "static void __fastcall dluma_ui_workshop_button_hook(",
        "static void __fastcall dluma_ui_workshop_double_hook",
    )
    post = block(
        source,
        "static void __fastcall dluma_ui_workshop_post_hook(",
        "static void __fastcall dluma_ui_workshop_init_hook",
    )
    assert 'dluma_ui_object_id_equals(button, "B_Refresh")' in button
    assert "g_dluma_workshop_manual_refresh_pending" in button
    assert "dluma_ui_workshop_refresh_now" not in button
    assert "g_dluma_workshop_manual_refresh_pending" in post
    assert "dluma_ui_workshop_refresh_now" in post
    assert post.index("original(screen);") < post.index(
        "dluma_ui_workshop_refresh_now"
    )


def test_refresh_uses_exact_focus_and_first_visible_abis():
    source = UI_PATH.read_text(encoding="utf-8")
    accessors = block(
        source,
        "static int dluma_ui_workshop_get_focus(void *list, int *focused)",
        "static void dluma_ui_workshop_capture_refresh_identity(",
    )
    assert "DlumaWorkshopFocusSetFn" in accessors
    assert "void *list_object, int focused_index, unsigned char force_update" in accessors
    assert ")(list, focused, force);" in accessors
    assert "DlumaWorkshopFirstVisibleSetFn" in accessors
    assert "void *list_object, int first_visible_index" in accessors
    assert ")(list, first_visible);" in accessors
    assert "WorkshopViewport" not in accessors
    assert "WORKSHOP_VIEWPORT" not in source
    capture = block(
        source,
        "static void dluma_ui_workshop_capture_refresh_state(",
        "static int dluma_ui_workshop_restore_refresh_state(",
    )
    assert "state->selected" in capture
    assert "state->focused" in capture
    assert "state->first_visible" in capture
    assert "state->focused.selectable" in capture
    restore = block(
        source,
        "static int dluma_ui_workshop_restore_refresh_state(",
        "static int dluma_ui_workshop_resolve_row(",
    )
    assert restore.index("g_dluma_list_set_selection") < restore.index(
        "dluma_ui_workshop_set_focus"
    ) < restore.index("dluma_ui_workshop_set_first_visible")
    assert "dluma_workshop_refresh_clamp_first_visible" in restore
    assert "active_relative_offset >= 0" not in restore


def test_ui_decorates_after_native_callbacks_without_mutating_payloads():
    source = UI_PATH.read_text(encoding="utf-8")
    row_reader = block(
        source,
        "static int dluma_ui_workshop_read_row(",
        "static int dluma_ui_workshop_resolve_row(",
    )
    assert "const int *" in row_reader
    assert "const ULONGLONG *" in row_reader
    assert "memcpy(payload" not in row_reader
    assert "memset(payload" not in row_reader
    assert "*(int *)payload =" not in row_reader
    assert "DLUMA_DEVTOOLS_UI_DATA_VOID_VTABLE_RVA" in row_reader
    post = block(
        source,
        "static void __fastcall dluma_ui_workshop_post_hook",
        "static void __fastcall dluma_ui_workshop_init_hook",
    )
    stock = post[post.index("dluma_ui_is_stock_workshop_screen") :]
    assert stock.index("original(screen);") < stock.index(
        "dluma_ui_workshop_present(screen);"
    )
    show = block(
        source,
        "static void __fastcall dluma_ui_workshop_show_hook",
        "static void __fastcall dluma_ui_workshop_selection_hook",
    )
    stock = show[show.index("dluma_ui_is_stock_workshop_screen") :]
    assert stock.index("original(screen, ui_data);") < stock.index(
        "dluma_ui_workshop_present(screen);"
    )
    button = block(
        source,
        "static void __fastcall dluma_ui_workshop_button_hook",
        "static void __fastcall dluma_ui_workshop_double_hook",
    )
    double = block(
        source,
        "static void __fastcall dluma_ui_workshop_double_hook",
        "static unsigned char __fastcall dluma_ui_workshop_uses_3d_rotation_hook",
    )
    assert "MenuSteamWorkshop" not in button
    assert "MenuSteamWorkshop" not in double


def test_custom_map_double_click_uses_the_exact_footer_start_action():
    source = UI_PATH.read_text(encoding="utf-8")
    double = block(
        source,
        "static void __fastcall dluma_ui_workshop_double_hook",
        "static unsigned char __fastcall dluma_ui_workshop_uses_3d_rotation_hook",
    )
    stock = double[double.index("dluma_ui_is_stock_workshop_screen(screen)") :]
    stock = stock[: stock.index("else if (original)")]
    admission = (
        "dluma_ui_workshop_custom_project_launch_allowed(\n"
        "                screen,\n"
        "                DLUMA_PROJECT_LAUNCH_ROUTE_LIST_DOUBLE_CLICK)"
    )
    assert admission in stock
    assert 'dluma_ui_find_required_child(screen, "B_Start")' in stock
    assert "g_dluma_workshop_button_original(" in stock
    assert "screen, start_button, event_code" in stock
    assert stock.index(admission) < (
        stock.index("g_dluma_workshop_button_original(")
    )
    assert "original(screen, item, event_code)" not in stock


def test_local_rows_use_only_snapshot_confirmed_workshop_ids():
    source = UI_PATH.read_text(encoding="utf-8")
    resolver = block(
        source,
        "static int dluma_ui_workshop_resolve_row(",
        "static const char *dluma_ui_workshop_row_title(",
    )
    assert "row->subscribed && row->workshop_id[0]" in resolver
    assert "metadata_found && metadata && metadata->workshop_id[0]" in resolver
    row_id = block(
        source,
        "static const char *dluma_ui_workshop_row_id(",
        "static int dluma_ui_workshop_item_has_texture(",
    )
    assert "row->subscribed && row->workshop_id[0]" in row_id
    assert 'return "LOCAL";' in row_id


def test_divider_details_author_and_fallback_presentation_contracts():
    source = UI_PATH.read_text(encoding="utf-8")
    presenter = block(
        source,
        "static void dluma_ui_workshop_present(",
        "static int dluma_ui_workshop_bind_texture_exports",
    )
    assert '"LOCAL PROJECTS"' in presenter
    assert '"LOCAL PROJECT"' in presenter
    assert '"No description provided."' in presenter
    assert "remote.author_ready && remote.author[0]" in presenter
    assert "dluma_ui_workshop_set_row_secondary" in presenter
    assert "g_dluma_workshop_menu_id_group, 0" in presenter
    assert "g_dluma_workshop_menu_compact_id_group, 1" in presenter
    assert "g_dluma_workshop_menu_author_group, 0" in presenter
    assert "metadata->title_from_desc" in source
    assert "remote->details_ready" in source
    assert "metadata->title" in source
    assert "row->native_title" in source
    assert 'return "Unnamed map";' in source
    assert '"playgame_slot_icon_empty"' in source
    assert "active = dluma_ui_workshop_active_index" in presenter


def test_row_visual_and_presenter_use_stock_safe_secondary_line_contract():
    xui = XUI_PATH.read_text(encoding="utf-8")
    assert "<Visual>ListBoxCustomMapV</Visual>" in xui
    assert "<Id>DLUMAListButtonCustomMapSlotV</Id>" not in xui
    assert "<DataAssociation>3</DataAssociation>" not in xui

    source = UI_PATH.read_text(encoding="utf-8")
    secondary = block(
        source,
        "static void dluma_ui_workshop_set_row_secondary(",
        "static void dluma_ui_workshop_set_row_chrome(",
    )
    assert "remote->author_ready" in secondary
    assert '"AUTHOR:"' in secondary
    assert '"ID:"' in secondary
    assert "else if (!row->subscribed)" in secondary
    assert 'row->item, "LOCAL", 2' in secondary
    assert 'row->item, "", 2' in secondary
    assert "const char *id" not in secondary
    assert "id && id[0]" not in secondary
    assert '"T_Label"' in secondary
    assert "dluma_ui_set_element_visible(stock_author_label, 1)" in secondary
    assert "dluma_ui_set_element_visible(stock_author_label, 0)" in secondary
    assert "remote->author, 2" in secondary
    assert "\\r\\n" not in secondary
    assert "SetMultiline" not in secondary
    assert "SetSizeY" not in secondary
    assert ", 3)" not in secondary
    assert '" | "' not in secondary


def test_row_secondary_uses_author_local_fallback_or_blank_subscribed_line():
    source = UI_PATH.read_text(encoding="utf-8")
    secondary = block(
        source,
        "static void dluma_ui_workshop_set_row_secondary(",
        "static void dluma_ui_workshop_set_row_chrome(",
    )
    assert "if (has_author)" in secondary
    assert 'stock_author_label, "AUTHOR:"' in secondary
    assert 'stock_author_label, "ID:"' in secondary
    assert "remote->author, 2" in secondary
    assert "else if (!row->subscribed)" in secondary
    assert 'row->item, "LOCAL", 2' in secondary
    assert 'row->item, "", 2' in secondary
    assert "dluma_ui_set_element_visible(stock_author_label, 0)" in secondary
    presenter = block(
        source,
        "static void dluma_ui_workshop_present(",
        "static int dluma_ui_workshop_bind_texture_exports",
    )
    row_call = presenter[
        presenter.index("dluma_ui_workshop_set_row_secondary(") :
        presenter.index("dluma_ui_workshop_enable_selector_row")
    ]
    assert "dluma_ui_workshop_row_id" not in row_call
    assert "&row," in row_call
    assert "&remote, has_remote" in row_call
    divider = presenter[
        presenter.index("if (row.divider)") :
        presenter.index("else if (dluma_ui_workshop_resolve_row")
    ]
    assert 'row.item, "", 2' in divider
    assert 'row.item, "", 3' not in divider


def test_selected_map_details_keep_numeric_or_local_id():
    source = UI_PATH.read_text(encoding="utf-8")
    presenter = block(
        source,
        "static void dluma_ui_workshop_present(",
        "static int dluma_ui_workshop_bind_texture_exports",
    )
    assert "const char *detail_id = dluma_ui_workshop_row_id(" in presenter
    assert "g_dluma_workshop_menu_id, detail_id" in presenter
    assert "g_dluma_workshop_menu_compact_id, detail_id" in presenter


def _a21_file_backed_contract_reference():
    # Historical diagnostic contract retained as a non-collected reference.
    source = UI_PATH.read_text(encoding="utf-8")
    tick = block(
        source,
        "static void dluma_ui_workshop_texture_asset_tick(",
        "static void __fastcall dluma_ui_workshop_post_hook",
    )
    local = tick.index("!row.subscribed && has_metadata")
    remote = tick.index("row.subscribed && has_remote")
    load = tick.index("dluma_ui_workshop_load_row_texture_asset")
    assert local < remote < load
    assert "DLUMA_WORKSHOP_TEXTURE_WINDOW_RADIUS 3" in source
    assert "DLUMA_WORKSHOP_TEXTURE_ASSET_COUNT 24" in source
    assert "DLUMA_WORKSHOP_ROW_TEXTURE_BINDING_COUNT 16" in source
    assert "dluma_ui_workshop_release_textures" in tick
    assert "g_dluma_workshop_unload_ui_texture" in source
    assert "g_dluma_workshop_precache_ui_texture" in source
    assert "g_dluma_workshop_release_precached_ui_texture" in source
    assert source.index(
        "g_dluma_workshop_release_precached_ui_texture("
    ) < source.index(
        "(void)g_dluma_workshop_unload_ui_texture("
    )
    update = block(
        source,
        "dluma_ui_workshop_update_texture_asset(",
        "static DlumaWorkshopRowTextureBinding *",
    )
    core = (
        ROOT / "src" / "dlw_workshop_texture_core.h"
    ).read_text(encoding="utf-8")
    file_proof = block(
        core,
        "dluma_workshop_texture_asset_tick_file_proof(",
        "static unsigned long long",
    )
    assert file_proof.index("imagelib_probe(") < file_proof.index(
        "backing_probe("
    )
    assert update.index(
        "dluma_workshop_texture_asset_tick_file_proof("
    ) < update.index(
        "g_dluma_workshop_load_ui_texture"
    ) < update.index(
        "g_dluma_workshop_is_ui_texture_loaded"
    ) < update.index(
        "g_dluma_workshop_precache_ui_texture"
    ) < update.index(
        "g_dluma_workshop_request_texture_prefetch"
    ) < update.index(
        "g_dluma_workshop_is_streaming_texture_loaded"
    )
    assert '"ui_definition_registered_after_real_backing"' in source
    assert '"row_texture_association_0_bound"' in source
    assert '"level_precache_retained"' in source
    picture = block(
        source,
        "static int dluma_ui_workshop_set_picture(",
        "static ULONGLONG dluma_ui_workshop_texture_hash(",
    )
    assert "g_dluma_workshop_set_element_texture(item, &name, 0)" in picture
    assert "?SetTexture@IUIElementControl@@UEAA_N" in source
    assert "?SetTexture@IUIImage@@QEAA_N" not in source
    assert "DLUMA_DEVTOOLS_ENGINE_TIMESTAMP" in source
    assert "DLUMA_DEVTOOLS_ENGINE_IMAGE_SIZE" in source
    assert "DLUMA_DEVTOOLS_FORCE_LOAD_TEXTURE_RVA 0x0029A9A0UL" in source
    assert "DLUMA_DEVTOOLS_FORCE_UNLOAD_TEXTURE_RVA 0x0029A9F0UL" in source
    assert '"workshop_preview_file_backing"' in source
    assert '"workshop_preview_backing_probe_control"' in source
    assert '"workshop_preview_imagelib_probe"' in source
    assert '"workshop_preview_virtual_filename"' in source
    load_texture = block(
        source,
        "dluma_ui_workshop_load_row_texture_asset(",
        "static void dluma_ui_workshop_texture_asset_tick(",
    )
    assert "g_dluma_workshop_load_ui_texture" not in load_texture
    assert "dluma_workshop_texture_asset_can_bind" in load_texture
    assert load_texture.index(
        "dluma_workshop_texture_asset_can_bind"
    ) < load_texture.index(
        "dluma_ui_workshop_set_picture"
    )
    core = (
        ROOT / "src" / "dlw_workshop_texture_core.h"
    ).read_text(encoding="utf-8")
    assert "DLUMA_WORKSHOP_TEXTURE_BACKING_PROBE_INTERVAL_MS 100ULL" in core
    assert "DLUMA_WORKSHOP_TEXTURE_BACKING_TIMEOUT_MS 5000ULL" in core
    assert "DLUMA_WORKSHOP_TEXTURE_BACKING_PROBE_LIMIT" not in core


def test_dynamic_texture_upload_uses_native_registry_and_ui_alias_contract():
    source = UI_PATH.read_text(encoding="utf-8")
    menu = (ROOT / "src" / "dlw_workshop_menu.c").read_text(encoding="utf-8")
    preview = PREVIEW_PATH.read_text(encoding="utf-8")

    assert "DLUMA_DEVTOOLS_CREATE_DYNAMIC_TEXTURE_RVA 0x0029C1C0UL" in source
    assert "DLUMA_DEVTOOLS_UPDATE_DYNAMIC_TEXTURE_RVA 0x0029C230UL" in source
    assert "DLUMA_CTEXTURE_ACTUAL_NAME_OFFSET 0x60UL" in source
    assert "g_dluma_workshop_create_dynamic_texture(" in source
    assert "512, 512, 1U, 0, 2U" in source
    assert "512, 512, 2048, 32" in source
    assert "workshop_preview_dynamic_texture_registry_probe" in source
    assert "registered_dynamic_backing" in source
    assert "dynamic_ready_success" in source
    assert "dluma_workshop_menu_request_preview_pixels" in source
    assert "dluma_workshop_menu_take_preview_pixels" in source
    assert "DLUMA_WORKSHOP_MENU_PIXEL_JOBS 4U" in menu
    assert "dluma_workshop_preview_read_dds_top_mip_bgra" in menu
    assert "dluma_workshop_preview_read_dds_top_mip_bgra" in preview
    assert "DLUMA_WORKSHOP_PREVIEW_PIXEL_BYTES" in preview

    update = block(
        source,
        "dluma_ui_workshop_update_texture_asset(DlumaWorkshopTextureAsset *asset)",
        "static DlumaWorkshopRowTextureBinding *",
    )
    assert update.index("g_dluma_workshop_create_dynamic_texture") < update.index(
        "g_dluma_workshop_update_dynamic_texture"
    ) < update.index("g_dluma_workshop_force_load_texture") < update.index(
        "g_dluma_workshop_load_ui_texture"
    )
    assert "g_dluma_workshop_request_texture_prefetch" not in update
    assert "g_dluma_workshop_is_streaming_texture_loaded" not in update


def test_frontend_preview_alias_selects_embedded_16_9_content_rect():
    source = UI_PATH.read_text(encoding="utf-8")
    update = block(
        source,
        "dluma_ui_workshop_update_texture_asset(DlumaWorkshopTextureAsset *asset)",
        "static DlumaWorkshopTextureAsset *dluma_ui_loading_pin_preview_asset(",
    )
    call_start = update.index("g_dluma_workshop_load_ui_texture(")
    alias_call = update[call_start:update.index(");", call_start) + 2]
    assert "DLUMA_WORKSHOP_PREVIEW_EDGE" in alias_call
    assert "DLUMA_WORKSHOP_PREVIEW_CONTENT_TOP" in alias_call
    assert "DLUMA_WORKSHOP_PREVIEW_CONTENT_WIDTH" in alias_call
    assert "DLUMA_WORKSHOP_PREVIEW_CONTENT_BOTTOM" in alias_call
    assert "left, top, right, bottom" in update
    assert "512, 512, 0, 0, 512, 512" not in " ".join(alias_call.split())
    assert "expose only that embedded content rectangle" in update


def test_dynamic_texture_upload_color_contract_is_worker_owned_and_one_way():
    source = UI_PATH.read_text(encoding="utf-8")
    menu = (ROOT / "src" / "dlw_workshop_menu.c").read_text(encoding="utf-8")
    assert "DLUMA_WORKSHOP_PIXEL_LAYOUT_BGRA8_CACHE" in menu
    assert "DLUMA_WORKSHOP_PIXEL_LAYOUT_RGBA8_DYNAMIC_UPLOAD" in menu
    assert "dluma_workshop_bgra_to_rgba_in_place" in menu
    assert "a8r8g8b8_bgra_cache_to_rgba_dynamic_upload" in source
    assert '"rgba_ready"' in source
    assert '"rgba8_complete"' in source
    worker = block(menu, "void dluma_workshop_menu_worker_tick(void)",
                   "void dluma_workshop_menu_shutdown(void)")
    assert worker.index("dluma_workshop_bgra_to_rgba_in_place") < worker.index(
        "dluma_atomic_exchange(&job->state, 2)"
    )
    update = block(
        source,
        "dluma_ui_workshop_update_texture_asset(DlumaWorkshopTextureAsset *asset)",
        "static DlumaWorkshopRowTextureBinding *",
    )
    assert "bgra_ready" not in update
    assert update.index("rgba_ready") < update.index(
        "g_dluma_workshop_update_dynamic_texture"
    )


def test_dynamic_texture_color_harness_rejects_noop_and_double_swap():
    completed = subprocess.run(
        ["cmd.exe", "/d", "/s", "/c",
         "call tools\\build_workshop_preview_color_fake_tests.bat"],
        cwd=ROOT, text=True, capture_output=True, check=False,
    )
    assert completed.returncode == 0, completed.stdout + completed.stderr
    executable = ROOT / "build" / "workshop-preview-color-fake" / (
        "workshop_preview_color_fake_harness.exe"
    )
    run = subprocess.run([str(executable)], cwd=ROOT, text=True,
                         capture_output=True, check=False)
    assert run.returncode == 0, run.stdout + run.stderr
    assert "workshop_preview_color_fake_harness: 14/14 ok" in run.stdout
def test_divider_is_noninteractive_and_initial_focus_skips_it():
    source = UI_PATH.read_text(encoding="utf-8")
    chrome = block(
        source,
        "static void dluma_ui_workshop_set_row_chrome(",
        "static int dluma_ui_workshop_bind_description_scroll",
    )
    assert "g_dluma_set_element_can_focus" in chrome
    assert "g_dluma_process_element_inputs" in chrome
    for child in (
        "focus_picture",
        "antifocus_picture",
        "focus_triangle",
        "back_belt",
        "G_Info_23",
    ):
        assert f'"{child}"' in chrome
    assert "?ProcessInputs@IUIElementControl@@QEAAX_N@Z" in source
    assert "static int dluma_ui_workshop_is_selectable_index(" in source
    assert "static int dluma_ui_workshop_nearest_selectable_index(" in source
    initial = block(
        source,
        "static int dluma_ui_workshop_ensure_initial_focus(",
        "static void dluma_ui_workshop_capture_refresh_state(",
    )
    assert "g_dluma_list_set_selection" in initial
    assert "dluma_ui_workshop_set_focus(list, target, 1)" in initial
    assert "dluma_ui_workshop_set_first_visible" in initial
    assert "g_dluma_set_element_focus(list, 1, 0)" in initial


def test_workshop_uses_native_background_rail_without_changing_3d_route():
    source = UI_PATH.read_text(encoding="utf-8")
    hook = block(
        source,
        "dluma_ui_workshop_uses_background_screen_name_hook(void *screen)",
        "static __int64 __fastcall dluma_ui_workshop_get_3d_position_hook",
    )
    assert "dluma_ui_is_stock_workshop_screen(screen)" in hook
    assert "result = 1;" in hook
    assert "g_dluma_workshop_rail_diagnostic_pending" in hook
    assert "result = original(screen);" in hook
    workshop_branch = hook[
        hook.index("if (dluma_ui_is_stock_workshop_screen(screen))") :
        hook.index("else if (dluma_ui_is_mods_registry_screen(screen))")
    ]
    assert "dluma_log_" not in workshop_branch
    drain = block(
        source,
        "static void dluma_ui_workshop_drain_refresh_diagnostic(void)",
        "static void __fastcall dluma_ui_workshop_post_hook",
    )
    assert "workshop_menu_uses_background_screen_name" in drain
    assert "stock MenuBackgroundDL previous/current screen rail" in drain
    rotation = block(
        source,
        "static unsigned char __fastcall dluma_ui_workshop_uses_3d_rotation_hook",
        "dluma_ui_workshop_uses_background_screen_name_hook(void *screen)",
    )
    assert "dluma_ui_is_stock_workshop_screen(screen)" not in rotation


def test_pack_contains_loader_owned_workshop_and_extras_overrides():
    assert PACK_PATH.is_file()
    with zipfile.ZipFile(PACK_PATH) as archive:
        names = archive.namelist()
        packed = archive.read("data/menu/scr/DLUMA_SteamWorkshop.xui")
    assert names.count("data/menu/scr/DLUMA_SteamWorkshop.xui") == 1
    assert "data/menu/scr/MenuSteamWorkshop.xui" not in names
    assert "data/menu/scr/MenuMain_PC_workshop.xui" not in names
    assert names.count("data/menu/scr/MenuExtras.xui") == 1
    assert names.count("data/menu/scr/MenuDLUMALogs.xui") == 1
    assert packed == XUI_PATH.read_bytes()


def test_workshop_filename_redirect_is_exact_independent_and_owned():
    ui = UI_PATH.read_text(encoding="utf-8")
    exports = (ROOT / "src" / "dlw_exports.c").read_text(encoding="utf-8")
    core = (ROOT / "src" / "dlw_ui_redirect_core.h").read_text(
        encoding="utf-8"
    )

    assert "DLUMA_DEVTOOLS_GAMEDLL_TIMESTAMP 0x623397AAUL" in ui
    assert "DLUMA_DEVTOOLS_GAMEDLL_IMAGE_SIZE 0x02082000UL" in ui
    assert "DLUMA_DEVTOOLS_WORKSHOP_MENU_NAME_RVA 0x016784A0UL" in ui
    assert 'original_name[] = "MenuSteamWorkshop.xui"' in ui
    assert 'redirected_name[] = "DLUMA_SteamWorkshop.xui"' in ui
    assert "sizeof(original_name), sizeof(redirected_name)" in ui
    assert "g_dluma_workshop_menu_redirect_owned" in ui
    assert "already_present_not_owned" in ui
    assert "not_restored_modified_after_install" in ui
    assert "dluma_xui_redirect_should_restore" in core

    initialize = block(
        exports,
        "static void* dluma_player_initialize(void)",
        "static __int64 dluma_player_shutdown(void)",
    )
    assert initialize.index("dluma_ui_prepare_main_menu_redirect()") < initialize.index(
        "g_original_initialize()"
    )
    assert initialize.index("dluma_ui_prepare_workshop_menu_redirect()") < initialize.index(
        "g_original_initialize()"
    )
    assert "&& !dluma_ui_prepare_workshop_menu_redirect" not in initialize
    assert initialize.index("dluma_ui_restore_workshop_menu_redirect();") < initialize.index(
        "dluma_ui_restore_main_menu_redirect();"
    )
    shutdown = exports[exports.index(
        "static __int64 dluma_player_shutdown(void)"
    ):exports.index('extern "C" __declspec(dllexport) void*')]
    assert shutdown.index("dluma_ui_restore_workshop_menu_redirect();") < shutdown.index(
        "dluma_ui_restore_main_menu_redirect();"
    )


def test_custom_layout_validation_is_observational_and_bounded():
    source = UI_PATH.read_text(encoding="utf-8")
    resolver = block(
        source,
        "static int dluma_ui_workshop_resolve_controls(void *screen)",
        "static void dluma_ui_workshop_present(void *screen)",
    )
    assert "workshop_menu_custom_layout_validated" in resolver
    assert "workshop_menu_custom_layout_unresolved" in resolver
    assert "g_dluma_workshop_custom_layout_validated_log_once" in resolver
    assert "g_dluma_workshop_custom_layout_unresolved_log_once" in resolver
    assert "LoadUIScreen" not in resolver
    assert "ShowUIScreen" not in resolver
