from pathlib import Path
import re
import subprocess


ROOT = Path(__file__).resolve().parents[1]
LOCALES = (
    "En", "De", "Fr", "It", "Es", "Ru", "Jp", "Pl", "Nl", "Br",
    "Ko", "Cn", "Tw", "El", "Tr", "Th", "Cs",
)


def test_developer_mode_is_a_restart_only_loader_option_and_gates_lua_limits():
    common = (ROOT / "src" / "dlw_common.h").read_text()
    host = (ROOT / "src" / "dlw_host.c").read_text()
    ui = (ROOT / "src" / "dlw_ui.c").read_text()
    example_ini = (ROOT / "config" / "dluma.ini").read_text()

    assert "unsigned char developer_mode;" in common
    assert 'dluma_read_ini_u32(L"loader", L"developer_mode", 0)' in host
    assert "DLUMA_Settings_loader_developer_mode_Label" in ui
    assert 'L"loader", L"developer_mode", L"0"' in ui
    assert "DLUMA_LOADER_CATEGORY_LUA &&\n        !g_dluma.config.developer_mode" in ui
    assert "debug configuration itself is startup-only" in ui
    assert "developer_mode=0" in example_ini
    assert "enables cheats/developer keys" in example_ini
    assert "-debugconf=debugconf.scr" in example_ini


def test_developer_mode_keeps_explicit_debugconf_source_without_claiming_parse():
    pack = (ROOT / "src" / "dlw_pack.c").read_text()
    for action in (
        "Cheats()", "UseDeveloperKeys()", "NoLogos()",
        "SkipWaitingForInputToStartLoadedLevel()", "SetLogLevel(15)",
    ):
        assert action in pack
    assert 'L"dluma_runtime\\\\developer_mode"' in pack
    assert 'L"debugconf.scr"' in pack
    assert "if (!g_dluma.config.developer_mode)" in pack
    assert "Mounting alone does not make the gamedll parse" in pack
    assert "mounted_process_lifetime_parse_not_implied" in pack


def test_developer_mode_applies_exact_profile_before_original_initializer():
    adapter = (ROOT / "src" / "dlw_developer_mode.c").read_text()
    core = (ROOT / "src" / "dlw_developer_mode_core.c").read_text()
    header = (ROOT / "src" / "dlw_developer_mode_core.h").read_text()
    exports = (ROOT / "src" / "dlw_exports.c").read_text()
    project = (ROOT / "DLUMA.vcxproj").read_text()

    call = "dluma_developer_mode_apply_startup_profile(g_original_game_dll)"
    assert call in exports
    player = exports.split(
        "static void* dluma_player_initialize(void)", 1
    )[1].split("static __int64 dluma_player_shutdown(void)", 1)[0]
    assert player.index(call) < player.index("manager = g_original_initialize();")
    assert "DLUMA_DEVELOPER_MODE_GAMEDLL_TIMESTAMP 0x623397AAUL" in header
    assert "DLUMA_DEVELOPER_MODE_GAMEDLL_IMAGE_SIZE 0x02082000UL" in header
    for rva in ("0x01D07FC3UL", "0x01D07FC4UL", "0x01D07208UL"):
        assert rva in header
    assert "DLUMA_DEVELOPER_MODE_GAMEDLL_SHA256" in adapter
    assert "dluma_developer_mode_validate_data_section" in adapter
    assert "DLUMA_IMAGE_SCN_MEM_WRITE" in adapter
    assert "DLUMA_IMAGE_SCN_MEM_EXECUTE" in adapter
    assert "instruction_reference_mismatch" in core
    assert "dluma_developer_mode_core_restore" in core
    assert "write_or_readback_failed" in core
    assert '<ClCompile Include="src\\dlw_developer_mode.c" />' in project
    assert '<ClCompile Include="src\\dlw_developer_mode_core.c" />' in project


def test_developer_mode_core_fake_harness():
    subprocess.run(
        [
            "cmd.exe", "/d", "/s", "/c",
            "call tools\\build_developer_mode_fake_tests.bat",
        ],
        cwd=ROOT,
        check=True,
    )
    subprocess.run(
        [
            str(
                ROOT
                / "build/developer-mode-fake/developer_mode_core_harness.exe"
            )
        ],
        cwd=ROOT,
        check=True,
    )


def _control_block(xui: str, control_id: str) -> str:
    marker = f"<Id>{control_id}</Id>"
    start = xui.index(marker)
    return xui[xui.rfind("<MyText>", 0, start) : xui.index("</MyText>", start)]


def test_main_menu_ticker_uses_the_stock_news_group_lifecycle():
    xui = (ROOT / "PakAssets" / "XUI" / "DLUMA_Main_PC_workshop.xui").read_text()
    native = (ROOT / "src" / "dlw_main_menu_native_presentation.c").read_text()

    assert xui.count("<Id>newsfeed</Id>") == 1
    assert xui.count("<Id>T_Message</Id>") == 1
    # G_NewsFeed appears once as the stock element and once as its timeline
    # target; neither is a loader-owned replacement.
    assert xui.count("<Id>G_NewsFeed</Id>") == 2
    for removed_id in (
        "DLUMA_newsfeed", "G_DLUMANewsTicker",
        "I_DLUMANewsTickerBackground", "T_DLUMANewsMessage",
    ):
        assert f"<Id>{removed_id}</Id>" not in xui
    assert "&amp;DLUMA_Ticker_StaticFallback&amp;" in xui
    assert "DLUMA_MAIN_MENU_NATIVE_NEWS_GROUP_OFFSET 0x0480UL" in native
    assert "DLUMA_MAIN_MENU_NATIVE_ADD_MESSAGE_RVA 0x00863870UL" in native
    assert "DLUMA_MAIN_MENU_NATIVE_CLEAR_TYPE_RVA 0x008639A0UL" in native
    assert "DLUMA_MAIN_MENU_NATIVE_NEWS_POST_RVA 0x00863A50UL" in native
    assert "state->add_message(" in native
    assert "state->clear_type(" in native
    assert "state->news_post_update(" in native
    assert "state->get_time_delta(menu_main)" in native
    assert "main_menu_news_native_update_active" in native
    assert (
        "typedef __int64 (__fastcall *DlumaNewsFeedClearTypeFn)(void *news_group);"
        in native
    )
    assert "before.count >= before.capacity" not in native
    assert "CNewsFeedGroup::AddMessage owns capacity growth" in native
    assert native.count('{"DLUMA_Ticker_') == 4
    assert "DLUMA_Ticker_Welcome" in native
    assert "DLUMA_Ticker_Stats" in native
    assert "DLUMA_Ticker_DevMode" in native
    assert "DLUMA_Ticker_ReportErrors" in native
    assert "DLUMA_Ticker_TechlandThankYou" in native
    assert "DLUMA_Ticker_NewMods" in native
    for seasonal in (
        "DLUMA_Ticker_Thanksgiving",
        "DLUMA_Ticker_HappyHolidays",
        "DLUMA_Ticker_Halloween",
        "DLUMA_Ticker_NewYear",
        "DLUMA_Ticker_Valentines",
        "DLUMA_Ticker_SaintPatricks",
        "DLUMA_Ticker_Easter",
        "DLUMA_Ticker_MothersDay",
        "DLUMA_Ticker_FathersDay",
        "DLUMA_Ticker_IndependenceDay",
        "DLUMA_Ticker_Tanabata",
        "DLUMA_Ticker_LeifErikson",
    ):
        assert seasonal in native
    assert "DLUMA_Ticker_Active" not in native
    assert "dluma_ui_get_main_menu_mod_counts" in native
    assert "dluma_main_menu_presentation_expand_localized" in native
    new_mod = native.index("new_mod_pending = dluma_new_mod_ticker_has_pending()")
    holiday = native.index(
        "holiday_pending =\n        dluma_main_menu_native_holiday_message"
    )
    regular = native.index(
        "for (index = 0; index < DLUMA_ARRAY_COUNT(messages); ++index)"
    )
    assert new_mod < holiday < regular
    assert "developer_mode_pending = g_dluma.config.developer_mode != 0;" in native
    assert "index == 2U && developer_mode_pending" in native


def test_main_menu_strings_are_translated_in_every_catalog():
    required = (
        "DLUMA_Settings_loader_developer_mode_Label",
        "DLUMA_Settings_loader_developer_mode_Hint",
        "DLUMA_MainMenu_LoaderVersion",
        "DLUMA_MainMenu_LoaderCommit",
        "DLUMA_MainMenu_LoaderCommitUnavailable",
        "DLUMA_Ticker_StaticFallback",
        "DLUMA_Ticker_Welcome", "DLUMA_Ticker_Stats",
        "DLUMA_Ticker_DevMode", "DLUMA_Ticker_ReportErrors",
        "DLUMA_Ticker_TechlandThankYou",
        "DLUMA_Ticker_NewMods", "DLUMA_Ticker_NewYear",
        "DLUMA_Ticker_Thanksgiving",
        "DLUMA_Ticker_HappyHolidays", "DLUMA_Ticker_Halloween",
        "DLUMA_Ticker_Valentines", "DLUMA_Ticker_SaintPatricks",
        "DLUMA_Ticker_Easter", "DLUMA_Ticker_MothersDay",
        "DLUMA_Ticker_FathersDay", "DLUMA_Ticker_IndependenceDay",
        "DLUMA_Ticker_Tanabata", "DLUMA_Ticker_LeifErikson",
    )
    for code in LOCALES:
        text = (ROOT / "PakAssets" / "Locale" / code / "loader_texts_all.scr").read_text(encoding="utf-8")
        for key in required:
            assert f'String("{key}"' in text, f"{code}: {key}"
    english = (ROOT / "PakAssets" / "Locale" / "En" / "loader_texts_all.scr").read_text(encoding="utf-8")
    assert 'String("DLUMA_MainMenu_LoaderCommit", "commit: {commit}")' in english


def test_new_ticker_strings_preserve_authored_color_control_sequences():
    keys = (
        "DLUMA_Ticker_NewMods",
        "DLUMA_Ticker_NewYear",
        "DLUMA_Ticker_Thanksgiving",
        "DLUMA_Ticker_HappyHolidays",
        "DLUMA_Ticker_Halloween",
        "DLUMA_Ticker_Valentines",
        "DLUMA_Ticker_SaintPatricks",
        "DLUMA_Ticker_Easter",
        "DLUMA_Ticker_MothersDay",
        "DLUMA_Ticker_FathersDay",
        "DLUMA_Ticker_IndependenceDay",
        "DLUMA_Ticker_Tanabata",
        "DLUMA_Ticker_LeifErikson",
    )

    def value(catalog: str, key: str) -> str:
        match = re.search(
            rf'^String\("{re.escape(key)}", "(.*)"\)$',
            catalog,
            flags=re.MULTILINE,
        )
        assert match, key
        return match.group(1)

    english = (
        ROOT / "PakAssets" / "Locale" / "En" / "loader_texts_all.scr"
    ).read_text(encoding="utf-8")
    expected = {
        key: re.findall(r"%COLOR\([^)]+\)", value(english, key))
        for key in keys
    }
    for code in LOCALES:
        catalog = (
            ROOT / "PakAssets" / "Locale" / code / "loader_texts_all.scr"
        ).read_text(encoding="utf-8")
        for key in keys:
            assert re.findall(
                r"%COLOR\([^)]+\)", value(catalog, key)
            ) == expected[key], f"{code}: {key}"
