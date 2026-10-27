from __future__ import annotations

import pathlib
import subprocess


ROOT = pathlib.Path(__file__).resolve().parents[1]


def _read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def test_discord_presence_fake_harness() -> None:
    build = subprocess.run(
        [
            "cmd.exe",
            "/d",
            "/s",
            "/c",
            r"call tools\build_discord_presence_fake_tests.bat",
        ],
        cwd=ROOT,
        capture_output=True,
        text=True,
        timeout=120,
    )
    assert build.returncode == 0, build.stdout + build.stderr
    run = subprocess.run(
        [ROOT / "build" / "discord-presence-fake" /
         "discord_presence_fake_harness.exe"],
        cwd=ROOT,
        capture_output=True,
        text=True,
        timeout=30,
    )
    assert run.returncode == 0, run.stdout + run.stderr
    assert "discord_presence_fake_harness: ok" in run.stdout


def test_discord_presence_helper_fake_backend() -> None:
    build = subprocess.run(
        [
            "cmd.exe",
            "/d",
            "/s",
            "/c",
            r"call tools\build_discord_presence_helper_fake_tests.bat",
        ],
        cwd=ROOT,
        capture_output=True,
        text=True,
        timeout=120,
    )
    assert build.returncode == 0, build.stdout + build.stderr
    run = subprocess.run(
        [ROOT / "build" / "discord-presence-helper-fake" /
         "discord_presence_helper_fake_harness.exe"],
        cwd=ROOT,
        capture_output=True,
        text=True,
        timeout=30,
    )
    assert run.returncode == 0, run.stdout + run.stderr
    assert "discord_presence_helper_fake_harness: ok" in run.stdout


def test_setting_is_default_on_and_updates_runtime_controller() -> None:
    host = _read("src/dlw_host.c")
    ui = _read("src/dlw_ui.c")
    example = _read(
        "config/dluma.ini"
    )
    assert 'L"discord_rich_presence_enabled", 1' in host
    assert 'L"discord_rich_presence_enabled", L"1"' in ui
    assert "dluma_discord_presence_set_enabled(" in ui
    assert "discord_rich_presence_enabled=1" in example


def test_authoritative_level_snapshot_refresh_survives_startup_completion() -> None:
    ui = _read("src/dlw_ui.c")
    start = ui.index("static int dluma_ui_startup_b0_onframe_pump(")
    end = ui.index(
        "static int dluma_ui_is_authoritative_game_thread_tick(", start
    )
    pump = ui[start:end]
    status = pump.index("pump_status = dluma_custom_map_startup_b0_pump_status()")
    query = pump.index("query_result = dluma_engine_query_level_identity(")
    publish = pump.index("dluma_ui_publish_level_snapshot(")
    assert status < query < publish
    assert "return 1;" not in pump[status:query]
    assert (
        "pump_status == DLUMA_STARTUP_B0_PUMP_REQUIRED" in pump[status:query]
    )
    assert (
        "The active-level observation is shared process-lifetime state"
        in pump
    )


def test_validated_game_session_supersedes_retained_frontend_snapshot() -> None:
    source = _read("src/dlw_discord_presence.c")
    core = _read("src/dlw_discord_presence_core.c")
    adapter = _read("src/dlw_player_media_adapter.c")
    header = _read("src/dlw_player_media_adapter.h")
    assert "dluma_player_media_adapter_get_session_level(" in header
    assert "dluma_player_media_adapter_get_session_level(&level)" in source
    assert "dluma_discord_session_level_snapshot(" in source
    assert "dluma_discord_choose_level_source(" in source
    assert "DLUMA_DISCORD_LEVEL_SOURCE_SESSION" in source
    assert "if (session_level_positive)" in core
    assert "return DLUMA_DISCORD_LEVEL_SOURCE_SESSION;" in core
    assert (
        "dluma_player_media_resolve_game_session_music_manager("
        in adapter
    )
    assert (
        "DLUMA_DL_MUSIC_SOURCE_GAME_GLOBAL_SESSION_MANAGER_OWNER_CORROBORATED"
        in adapter
    )
    assert "validated_game_global_session" in source


def test_menu_visibility_uses_exact_ui_owned_live_frontend_level() -> None:
    common = _read("src/dlw_common.h")
    ui = _read("src/dlw_ui.c")
    native_main = _read("src/dlw_main_menu_native_presentation.c")
    presence = _read("src/dlw_discord_presence.c")
    snapshot = ui.split(
        "unsigned int dluma_ui_discord_menu_visibility_snapshot(void)", 1
    )[1].split(
        "static int dluma_ui_is_quest_log_screen", 1
    )[0]
    assert "g_dluma_discord_live_frontend_level" in ui
    assert "g_dluma_discord_live_frontend_vtable" in ui
    assert "dluma_ui_discord_copy_live_frontend_level()" in snapshot
    assert "dluma_ui_discord_owned_screen_visible(" in snapshot
    for root in (
        "g_dluma_workshop_menu_screen",
        "g_dluma_ui_settings_screen",
        "g_dluma_ui_registry_screen",
        "g_dluma_ui_mod_details_screen",
        "g_dluma_ui_mod_config_screen",
    ):
        assert root in snapshot
    assert "register_ui_screen" not in snapshot
    assert "load_ui_screen" not in snapshot
    assert "dluma_ui_note_live_frontend_screen(menu_main)" in native_main
    assert "dluma_ui_note_live_frontend_screen(screen)" in ui
    assert "dluma_ui_discord_menu_visibility_snapshot()" in presence
    assert "DlumaDiscordUiVisibilityBitsMustMatch" in presence
    for bit in (
        "DLUMA_UI_MENU_VISIBLE_SELECTING_MAP",
        "DLUMA_UI_MENU_VISIBLE_SETTINGS",
        "DLUMA_UI_MENU_VISIBLE_MODS",
        "DLUMA_UI_MENU_VISIBLE_CREDITS",
        "DLUMA_UI_MENU_VISIBLE_STATISTICS",
    ):
        assert bit in common


def test_map_preview_and_quest_use_only_copied_exact_project_data() -> None:
    source = _read("src/dlw_discord_presence.c")
    workshop = _read("src/dlw_workshop_menu.c")
    assert "dluma_custom_map_exact_project_owner_snapshot(" in source
    assert "dluma_workshop_menu_find_metadata_for_project(" in source
    assert "remote.preview_url" in source
    assert "remote.preview_path" not in source
    assert "dluma_discord_https_asset_url_valid(remote.preview_url)" in source
    assert "metadata.workshop_id" in source
    assert "dluma_discord_build_workshop_item_url(" in source
    assert '"DLUMA_Discord_ViewWorkshop"' in source
    assert "DLUMA_DISCORD_ACTIVITY_HAS_BUTTON" in source
    assert "quest.header.text" in source
    assert "quest.header.display_text" not in source
    assert "dluma_wstricmp(project_root, item->project_root) == 0" in workshop


def test_workshop_button_is_gameplay_only_and_strictly_constructed() -> None:
    source = _read("src/dlw_discord_presence.c")
    core = _read("src/dlw_discord_presence_core.c")
    helper = _read("src/dlw_discord_presence_helper.cpp")
    menu = source.split(
        "static void dluma_discord_build_menu_activity(", 1
    )[1].split("static int dluma_discord_module_identity(", 1)[0]
    gameplay = source.split(
        "static unsigned int dluma_discord_build_gameplay_activity(", 1
    )[1].split("static unsigned int dluma_discord_context_hash(", 1)[0]
    assert "DLUMA_DISCORD_ACTIVITY_HAS_BUTTON" not in menu
    assert "metadata.workshop_id" in gameplay
    assert "remote.workshop_id" not in gameplay
    assert (
        '"https://steamcommunity.com/sharedfiles/filedetails/?id="'
        in core
    )
    assert "0xFFFFFFFFFFFFFFFFULL" in core
    assert '\\"buttons\\":[{\\"label\\":' in helper
    assert "IsValidButtonLabel" in helper
    assert "IsWorkshopItemUrl" in helper


def test_pause_adapter_and_worker_fail_closed_contract() -> None:
    source = _read("src/dlw_discord_presence.c")
    assert "DLUMA_DISCORD_GAMEDI_PAUSE_FLAGS_OFFSET 0x880U" in source
    assert "DLUMA_DISCORD_GAMEDI_PAUSE_MASK 0x1U" in source
    assert "dluma_engine_game_frame_dispatch_active_on_current_thread" in source
    assert "game_frame_expected_primary_vtable" in source
    assert "memcmp(" in source and "frame_prefix" in source
    assert "if (pause_known)" in source
    assert "dluma_discord_publish_to_worker(&observed)" in source


def test_context_timer_survives_presence_only_updates() -> None:
    source = _read("src/dlw_discord_presence.c")
    core = _read("src/dlw_discord_presence_core.c")
    header = _read("src/dlw_discord_presence_api.h")
    helper = _read("src/dlw_discord_presence_helper.cpp")
    gate = source.index(
        "if (!positive || committed_context != observed_context)"
    )
    timer = source.index(
        "start_seconds =\n            dluma_discord_timer_update(", gate
    )
    publish = source.index("dluma_discord_publish_to_worker(&observed)", timer)
    assert gate < timer < publish
    assert "DLUMA_DISCORD_TIMER_MENU" in source
    assert "DLUMA_DISCORD_TIMER_GAMEPLAY" in source
    assert 'DLUMA_ARRAY_COUNT(activity->large_image), "dl_menu"' in source
    assert '"DLUMA_Discord_LoaderVersionFormat"' in source
    assert '"DLUMA v{version}"' in source
    assert "placeholder.value = DLUMA_VERSION" in source
    assert "state->map_context != map_context" in core
    assert "state->level_identity != level_identity" in core
    assert "DLUMA_DISCORD_ACTIVITY_HAS_START_TIMESTAMP" in header
    assert "start_timestamp_seconds" in header
    assert '\\"timestamps\\":{\\"start\\":' in helper


def test_private_helper_abi_is_complete_and_uses_direct_rpc() -> None:
    header = _read("src/dlw_discord_presence_api.h")
    helper = _read("src/dlw_discord_presence_helper.cpp")
    assert "DLUMA_DISCORD_PRESENCE_ABI_VERSION 0x00040000u" in header
    assert (
        'DLUMA_DISCORD_PRESENCE_HELPER_RELEASE "dluma-discord-rpc-3"'
        in header
    )
    assert 'DLUMA_DISCORD_RPC_TRANSPORT_RELEASE "discord-desktop-rpc-v1"' in header
    assert "DLUMA_DISCORD_RPC_PROTOCOL_VERSION 1u" in header
    assert "DLUMA_DiscordPresence_GetApi" in helper
    for member in (
        "initialize",
        "publish",
        "clear",
        "pump",
        "status",
        "quiesce",
    ):
        assert member in header
    assert '"type\\":0' in helper
    assert '"status_display_type\\":2' in helper
    assert "discord-ipc-0" in helper
    assert "kPingOpcode" in helper and "kPongOpcode" in helper
    assert "discordpp" not in helper


def test_loader_locks_validates_direct_rpc_helper_and_keeps_export_private() -> None:
    source = _read("src/dlw_discord_presence.c")
    header = _read("src/dlw_discord_presence_api.h")
    project = _read("DLUMA.vcxproj")
    locked = source.index("dluma_discord_open_locked_candidate(",
                          source.index("static int dluma_discord_load_helper"))
    loaded = source.index("dluma_discord_load_validated_module(", locked)
    assert locked < loaded
    assert "discord_partner_sdk" not in source
    assert "DlumaDiscordSdkRoot" not in project
    assert "DLUMA_DISCORD_PRESENCE_HELPER_EXPORTS" in header
    assert "GenerateDlumaDiscordSdkIdentity" not in project
    assert "<DlumaSkipPackage>" not in project


def test_all_locales_have_presence_keys() -> None:
    english = _read("PakAssets/Locale/En/loader_texts_all.scr")
    required = {
        line.split('"')[1]
        for line in english.splitlines()
        if line.startswith('String("DLUMA_Discord_')
        or line.startswith(
            'String("DLUMA_Settings_loader_discord_rich_presence_enabled_'
        )
    }
    assert len(required) == 17
    catalogs = sorted((ROOT / "PakAssets" / "Locale").glob(
        "*/loader_texts_all.scr"
    ))
    assert len(catalogs) == 17
    for catalog in catalogs:
        text = catalog.read_text(encoding="utf-8")
        for key in required:
            assert f'String("{key}"' in text, f"{catalog}: {key}"


def test_editor_presence_setting_and_stock_options_contract() -> None:
    host = _read("src/dlw_host.c")
    config = _read("config/dluma.ini")
    source = _read("src/dlw_editor_presence.c")
    core = _read("src/dlw_editor_presence_core.c")

    assert 'L"editor_discord_rich_presence_enabled", 1' in host
    assert "editor_discord_rich_presence_enabled=1" in config
    assert 'L"Editor options"' in source
    assert 'L"Workbench Mods"' in source
    assert 'L"Discord Rich Presence"' in source
    for label in ('L"Misc"', 'L"Actions"', 'L"Tabs"', 'L"Classes"'):
        assert label in source
    assert "DLUMA_EDITOR_TCN_SELCHANGE" in source
    assert "dluma_editor_options_model_select(" in source
    assert "DLUMA_EDITOR_OPTIONS_SHOW_CUSTOM" in source
    assert "DLUMA_EDITOR_OPTIONS_FORWARD_STOCK" in source
    assert "dluma_editor_options_model_toggle(" in source
    assert "dluma_editor_options_model_reset(" in source
    assert 'L"editor_discord_rich_presence_enabled"' in source
    assert "WritePrivateProfileStringW(" in source
    assert "dluma_discord_presence_set_enabled(enabled)" in source
    assert "stock_tab_count != 4U" in core
    assert "stock_page_count != 4U" in core
    assert "already_inserted" in core


def test_editor_document_privacy_and_steam_isolation_contract() -> None:
    editor = _read("src/dlw_editor_presence.c")
    document = _read("src/dlw_editor_presence_core.c")
    discord = _read("src/dlw_discord_presence.c")
    steam = _read("src/dlw_presence_core.c")
    presence_header = _read("src/dlw_presence_core.h")

    assert '".map"' in document
    assert '".pxsl"' in document
    assert '".scr"' in document
    assert 'return "Editing Document"' in document
    assert '"Browsing Developer Tools"' in document
    assert "dluma_presence_sanitize_text(" in document
    assert "document_identity" in document
    assert "DLUMA_DISCORD_TIMER_EDITOR" in discord
    assert '"dl_editor"' in discord
    assert '"Dying Light Developer Tools"' in discord
    observe = discord.split(
        "void dluma_discord_presence_observe_editor(", 1
    )[1].split(
        "void dluma_discord_presence_set_suspended(", 1
    )[0]
    assert "dluma_steam" not in observe
    assert "DLUMA_PRESENCE_CONTEXT_EDITOR" in presence_header
    assert "DLUMA_PRESENCE_CONTEXT_EDITOR" not in steam
    assert "return 0;" in steam.split(
        "int dluma_presence_format_steam_status(", 1
    )[1]
    assert "GetWindowTextW" in editor
    assert "GetForegroundWindow" in editor
    assert "GetGUIThreadInfo" in editor


def test_editor_playtest_ownership_and_post_success_handoff_contract() -> None:
    launch = _read("src/dlw_editor_launch.c")
    editor = _read("src/dlw_editor_presence.c")
    core = _read("src/dlw_editor_presence_core.c")

    create_hook = launch.split(
        "static BOOL WINAPI dluma_editor_create_process_hook(", 2
    )[2].split(
        "static HINSTANCE WINAPI dluma_editor_shell_execute_hook(", 1
    )[0]
    shell_hook = launch.split(
        "static HINSTANCE WINAPI dluma_editor_shell_execute_hook(", 2
    )[2].split("static int dluma_editor_replace_iat_target(", 1)[0]
    assert "if (result && published)" in create_hook
    assert "dluma_editor_presence_note_playtest_launch(" in create_hook
    assert "if ((ULONG_PTR)result > 32U && published)" in shell_hook
    assert "dluma_editor_presence_note_playtest_launch(" in shell_hook
    for evidence in (
        "pid_match",
        "start_time_match",
        "executable_match",
        "loader_match",
        "handoff_match",
        "unexpired",
    ):
        assert evidence in core
    assert "QueryFullProcessImageNameW" in editor
    assert "GetProcessTimes(" in editor
    assert "DLUMA_EDITOR_PLAYER_SHA256" in editor
    assert "launch->handoff.loader_sha256" in editor
    assert "launch->handoff.generation" in editor
    assert "dluma_discord_presence_set_suspended(1)" in editor
    assert "dluma_discord_presence_set_suspended(0)" in editor


def test_discord_host_route_and_private_helper_abi_remain_separate() -> None:
    core = _read("src/dlw_discord_presence_core.c")
    header = _read("src/dlw_discord_presence_api.h")

    assert "DLUMA_HOST_PLAYER_STANDALONE" in core
    assert "DLUMA_HOST_CHROME_EDITOR" in core
    assert "DLUMA_HOST_PLAYER_EDITOR_PLAYTEST" in core
    assert "if (forwarding_only) return 0;" in core
    assert "if (!developer_tools_application_id) return 0;" in core
    assert "uses_editor_setting = 1U" in core
    assert "DLUMA_DISCORD_PRESENCE_ABI_VERSION 0x00040000u" in header
    assert 'DLUMA_DISCORD_PRESENCE_HELPER_RELEASE "dluma-discord-rpc-3"' in header


def test_editor_presence_is_started_and_uses_its_own_application() -> None:
    host = _read("src/dlw_editor_host.c")
    startup = host.split("int dluma_editor_host_start(void)", 1)[1].split(
        "int dluma_editor_host_session_begin", 1)[0]
    assert startup.index("dluma_discord_presence_start()") < startup.index(
        "dluma_editor_presence_start()")
    sessions = host.split("int dluma_editor_host_session_begin", 1)[1]
    assert "dluma_discord_presence_stop(" not in sessions
    source = _read("src/dlw_discord_presence.c")
    start = source.split("int dluma_discord_presence_start(void)", 1)[1].split(
        "void dluma_discord_presence_set_enabled", 1)[0]
    assert "dluma_discord_select_host_route(" in start
    assert "dluma_dual_host_runtime_forwarding_only()" in start
    assert "route.application_id" in start
    assert "route.uses_editor_setting" in start
    assert "g_dluma.config.editor_discord_rich_presence_enabled" in start
    assert "DLUMA_DISCORD_DEVELOPER_TOOLS_APPLICATION_ID 1521173705772765376ULL" in source
    observe = source.split("void dluma_discord_presence_observe_editor(", 1)[1].split(
        "void dluma_discord_presence_set_suspended", 1)[0]
    assert "&g_dluma_discord.enabled" not in observe
    assert "dluma_discord_publish_to_worker(&activity)" in observe
