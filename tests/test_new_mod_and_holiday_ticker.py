from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
CORE = (ROOT / "src/dlw_main_menu_presentation_core.c").read_text(
    encoding="utf-8"
)
NATIVE = (ROOT / "src/dlw_main_menu_native_presentation.c").read_text(
    encoding="utf-8"
)
TRACKER = (ROOT / "src/dlw_new_mod_ticker.c").read_text(encoding="utf-8")
EXPORTS = (ROOT / "src/dlw_exports.c").read_text(encoding="utf-8")
PROJECT = (ROOT / "DLUMA.vcxproj").read_text(encoding="utf-8")
WIN32 = (ROOT / "src/dlw_win32_base.h").read_text(encoding="utf-8")


def test_startup_capture_runs_after_all_language_discovery() -> None:
    player = EXPORTS.split(
        "static void* dluma_player_initialize(void)", 1
    )[1].split("static __int64 dluma_player_shutdown(void)", 1)[0]
    lua = player.index("dluma_lua_discover_globals();")
    native = player.index("dluma_native_mods_discover();", lua)
    managed = player.index("dluma_managed_mods_discover();", native)
    capture = player.index("dluma_new_mod_ticker_capture_startup();", managed)
    original = player.index("manager = g_original_initialize();")
    assert lua < native < managed < capture < original


def test_exact_fingerprint_markers_are_independent_from_consent() -> None:
    assert '"DLW_NEW_MOD_TICKER_KEY_V1"' in TRACKER
    assert 'L"new_mod_ticker_announced"' in TRACKER
    assert "announced, fingerprint" in TRACKER
    assert "candidate->fingerprint" in TRACKER
    assert "WritePrivateProfileStringW" in TRACKER
    assert "approval_fingerprint" not in TRACKER[
        TRACKER.index("int dluma_new_mod_ticker_mark_presented")::
    ]
    presented = TRACKER.index("g_dluma_new_mod_ticker.presented = 1;")
    persisted = TRACKER.index("WritePrivateProfileStringW(", presented)
    assert presented < persisted


def test_mod_notice_is_one_shot_but_holiday_selection_is_per_queue() -> None:
    assert "dluma_new_mod_ticker_has_pending()" in NATIVE
    assert "dluma_new_mod_ticker_mark_presented()" in NATIVE
    assert "dluma_main_menu_native_holiday_message(&holiday_message)" in NATIVE
    assert "GetLocalTime(&local_time);" in NATIVE
    assert "dluma_main_menu_presentation_holiday(" in NATIVE
    assert NATIVE.index("new_mod_pending =") < NATIVE.index("holiday_pending =")
    assert NATIVE.index("holiday_pending =") < NATIVE.index(
        "for (index = 0; index < DLUMA_ARRAY_COUNT(messages); ++index)"
    )


def test_holiday_core_contains_bounded_gregorian_rules() -> None:
    for expected in (
        "year < 1583U || year > 9999U",
        "month == 12U && day == 31U",
        "month == 1U && day <= 2U",
        "month == 2U && day >= 13U && day <= 15U",
        "month == 3U && day >= 16U && day <= 18U",
        "current_number + 2ULL >= easter_number",
        "current_number + 1ULL >= mothers_number",
        "current_number + 1ULL >= fathers_number",
        "month == 7U && day >= 3U && day <= 5U",
        "month == 7U && day >= 6U && day <= 8U",
        "month == 10U && day >= 8U && day <= 10U",
        "month == 10U && day >= 28U",
        "thanksgiving_number + 3ULL",
        "month == 12U && day >= 20U && day <= 30U",
    ):
        assert expected in CORE
    assert "GetLocalTime" in WIN32


def test_new_sources_are_in_the_explicit_project_manifest() -> None:
    assert '<ClCompile Include="src\\dlw_new_mod_ticker.c" />' in PROJECT
    assert '<ClInclude Include="src\\dlw_new_mod_ticker.h" />' in PROJECT
