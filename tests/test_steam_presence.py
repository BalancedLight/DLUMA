from __future__ import annotations

import pathlib
import subprocess


ROOT = pathlib.Path(__file__).resolve().parents[1]


def _read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def test_steam_presence_fake_harness() -> None:
    build = subprocess.run(
        [
            "cmd.exe",
            "/d",
            "/s",
            "/c",
            r"call tools\build_steam_presence_fake_tests.bat",
        ],
        cwd=ROOT,
        capture_output=True,
        text=True,
        timeout=120,
    )
    assert build.returncode == 0, build.stdout + build.stderr
    run = subprocess.run(
        [
            ROOT / "build" / "steam-presence-fake" /
            "steam_presence_fake_harness.exe"
        ],
        cwd=ROOT,
        capture_output=True,
        text=True,
        timeout=30,
    )
    assert run.returncode == 0, run.stdout + run.stderr
    assert "steam_presence_fake_harness: ok" in run.stdout


def test_setting_is_separate_default_on_and_immediate() -> None:
    host = _read("src/dlw_host.c")
    ui = _read("src/dlw_ui.c")
    example = _read("config/dluma.ini")
    assert 'L"steam_rich_presence_enabled", 1' in host
    assert 'L"steam_rich_presence_enabled", L"1"' in ui
    assert "dluma_steam_presence_set_enabled(" in ui
    assert "steam_rich_presence_enabled=1" in example
    assert "discord_rich_presence_enabled=1" in example


def test_player_adapter_is_fail_closed_and_non_owning() -> None:
    source = _read("src/dlw_steam_presence.c")
    core = _read("src/dlw_steam_presence_core.h")
    assert "GetModuleHandleW(L\"steam_api64.dll\")" in source
    assert "LoadLibrary" not in source
    assert "SteamAPI_Init" not in source
    assert "SteamAPI_Shutdown" not in source
    assert "SteamAPI_RunCallbacks" not in source
    assert "ClearRichPresence" not in source
    assert "DLUMA_STEAM_PLAYER_API_SHA256" in source
    assert (
        "0ee64af072052ab95438fb8e054a68e4d392de941ef867a0124992aafccc7938"
        in core
    )
    assert "DLUMA_STEAM_PLAYER_API_TIMESTAMP 0x56995865UL" in core
    assert "DLUMA_STEAM_PLAYER_API_IMAGE_SIZE 0x0003D000UL" in core
    assert "dluma_steam_presence_canonical_colocation(" in source
    assert "dluma_steam_presence_pointer_owned_by_module(" in source
    assert "friends_prefix" in source and "setter_prefix" in source
    assert '"SteamFriends"' in source
    assert '"SteamAPI_ISteamFriends_SetRichPresence"' in source
    assert 'g_dluma_steam_presence.friends, "status", value' in source
    assert "state->owns_status" in _read("src/dlw_steam_presence_core.c")


def test_only_player_profiles_are_supported_and_retail_seam_is_inert() -> None:
    source = _read("src/dlw_steam_presence.c")
    ui = _read("src/dlw_ui.c")
    seam = source.split(
        "dluma_steam_presence_retail_profile_seam(void)", 1
    )[1].split(
        "static DlumaSteamPresenceHostProfile dluma_steam_presence_host_profile",
        1,
    )[0]
    assert "DLUMA_STEAM_HOST_RETAIL_UNSUPPORTED" in seam
    assert "GetModuleHandleW" not in seam
    assert "DLUMA_HOST_PLAYER_STANDALONE" in source
    assert "DLUMA_HOST_PLAYER_EDITOR_PLAYTEST" in source
    assert "DLUMA_HOST_CHROME_EDITOR" not in source
    assert "dluma_steam_presence_host_supported()" in source
    assert "dluma_ui_loader_setting_visible(" in ui
    assert "DLUMA_HOST_PLAYER_STANDALONE" in ui
    assert "DLUMA_HOST_PLAYER_EDITOR_PLAYTEST" in ui


def test_shared_observation_skips_engine_reads_only_when_both_are_off() -> None:
    source = _read("src/dlw_discord_presence.c")
    service = source.index("dluma_steam_presence_game_thread_service();")
    both_off = source.index(
        "if (!dluma_discord_presence_is_enabled() &&\n"
        "        !dluma_steam_presence_is_enabled())",
        service,
    )
    engine_read = source.index(
        "dluma_read_game_thread_level_snapshot(&level)", both_off
    )
    assert service < both_off < engine_read
    assert "DlumaPresenceSnapshot observed_snapshot" in source
    assert "dluma_steam_presence_game_thread_observe(&observed_snapshot)" in source
    assert "dluma_steam_presence_game_thread_expire()" in source
    assert (
        "activity->details, DLUMA_ARRAY_COUNT(activity->details),\n"
        "        snapshot->primary_text" in source
    )


def test_status_results_retries_and_clears_are_observable_on_state_changes() -> None:
    source = _read("src/dlw_steam_presence.c")
    assert 'return "main_menu";' in source
    assert 'return "gameplay";' in source
    assert '"steam_presence_context"' in source
    assert '"steam_presence_status"' in source
    assert '"steam_presence_result"' in source
    assert '"steam_presence_clear_result"' in source
    assert '"steam_presence_retry_delay_ms"' in source
    assert "last_action_kind == action->kind" in source
    assert "last_action_context == context" in source
    assert "last_action_result == result" in source
    assert "dluma_stricmp(g_dluma_steam_presence.last_action_value, value) == 0" in source
    assert "g_dluma_steam_presence.observed_context = snapshot->context" in source
    assert (
        "g_dluma_steam_presence.observed_context =\n"
        "        DLUMA_PRESENCE_CONTEXT_NONE;" in source
    )
    assert '"steam_display"' not in source


def test_all_locales_have_separate_steam_setting_strings() -> None:
    catalogs = sorted(
        (ROOT / "PakAssets" / "Locale").glob("*/loader_texts_all.scr")
    )
    assert len(catalogs) == 17
    keys = (
        "DLUMA_Settings_loader_steam_rich_presence_enabled_Label",
        "DLUMA_Settings_loader_steam_rich_presence_enabled_Hint",
    )
    for catalog in catalogs:
        text = catalog.read_text(encoding="utf-8")
        for key in keys:
            assert f'String("{key}"' in text, f"{catalog}: {key}"
