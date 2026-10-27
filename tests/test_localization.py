from pathlib import Path
import re
import zipfile

import pytest

ROOT = Path(__file__).resolve().parents[1]
LOCALE_ROOT = ROOT / "PakAssets" / "Locale"
PACK = ROOT / "build/package/Release/dluma.pak"
SUPPORTED = ("En", "De", "Fr", "It", "Es", "Ru", "Jp", "Pl", "Nl", "Br", "Ko", "Cn", "Tw", "El", "Tr", "Th", "Cs")

import sys
sys.path.insert(0, str(ROOT / "tools"))
from compile_localized_scr import compile_file, parse_scr
from decompile_localized_bin import decompile_values
from validate_localization import (
    PROTECTED_TOKEN_PATTERNS,
    has_terminal_punctuation,
    has_wrapping_quotes,
    validate,
)


def test_all_supported_locales_have_exact_key_parity_and_unicode_values():
    catalogs = {
        code: parse_scr(LOCALE_ROOT / code / "loader_texts_all.scr")
        for code in SUPPORTED
    }
    assert len(catalogs) == 17
    keys = set(catalogs["En"])
    assert all(set(values) == keys for values in catalogs.values())
    for code in ("Jp", "Ko", "Cn", "Tw", "Ru", "Th", "Tr", "Cs"):
        assert any(ord(ch) > 127 for value in catalogs[code].values() for ch in value)
    assert catalogs["Es"]["DLUMA_Crash_Note"] != catalogs["El"]["DLUMA_Crash_Note"]


def test_localization_validator_accepts_the_english_baseline_contract():
    assert validate(check_pack=False) == []


def test_settings_translations_do_not_collapse_distinct_english_rows():
    catalogs = {
        code: parse_scr(LOCALE_ROOT / code / "loader_texts_all.scr")
        for code in SUPPORTED
    }
    english = catalogs["En"]
    families = (
        [
            key for key in english
            if key.startswith("DLUMA_Settings_") and key.endswith("_Label")
        ],
        [
            key for key in english
            if key.startswith("DLUMA_Settings_") and key.endswith("_Hint")
        ],
    )
    for code in SUPPORTED[1:]:
        for keys in families:
            for index, left in enumerate(keys):
                for right in keys[index + 1:]:
                    if english[left] != english[right]:
                        assert catalogs[code][left] != catalogs[code][right], (
                            code, left, right
                        )


def test_permission_rows_are_distinct_and_preserve_exact_capability_names():
    permission_tokens = (
        "game.read",
        "player.read",
        "world.query",
        "events.sensor",
        "events.interact",
        "storage.mod",
        "ui.debug",
        "ui.overlay",
        "audio.play",
        "world.write",
    )
    catalogs = {
        code: parse_scr(LOCALE_ROOT / code / "loader_texts_all.scr")
        for code in SUPPORTED
    }
    for code, values in catalogs.items():
        label_values = []
        hint_values = []
        for token in permission_tokens:
            stem = token.replace(".", "_")
            label_key = f"DLUMA_Settings_lua_local_permissions_{stem}_Label"
            hint_key = f"DLUMA_Settings_lua_local_permissions_{stem}_Hint"
            label_values.append(values[label_key])
            hint_values.append(values[hint_key])
            assert token in values[hint_key]
        assert len(set(label_values)) == len(permission_tokens), code
        assert len(set(hint_values)) == len(permission_tokens), code

    chinese = catalogs["Cn"]
    assert chinese["DLUMA_Settings_lua_local_permissions_game_read_Label"] == "读取游戏"
    assert chinese["DLUMA_Settings_lua_local_permissions_player_read_Label"] == "读取玩家"
    assert chinese["DLUMA_Settings_lua_local_permissions_world_write_Label"] == "写入本地世界"


def test_mod_detail_and_review_state_placeholders_are_gone():
    catalogs = {
        code: parse_scr(LOCALE_ROOT / code / "loader_texts_all.scr")
        for code in SUPPORTED
    }
    detail_keys = [
        key for key in catalogs["En"] if key.startswith("DLUMA_ModDetails_")
    ]
    for code in SUPPORTED[1:]:
        values = catalogs[code]
        assert len({values[key] for key in detail_keys}) == len(detail_keys), code
        assert values["DLUMA_Mods_State_New"] != values["DLUMA_Mods_State_Changed"]
        assert "review required" not in values["DLUMA_Mods_State_New"].casefold()
        assert "review required" not in values["DLUMA_Mods_State_Changed"].casefold()


def test_scr_comments_platform_blocks_and_include_round_trip(tmp_path):
    include = tmp_path / "shared.scr"
    include.write_text('String("DLUMA_Include", "日本語")' + "\n", encoding="utf-8")
    source = tmp_path / "sample.scr"
    source.write_text("\n".join([
        "// line comment",
        "/* block comment */",
        "!String(s, s)",
        '!include "shared.scr"',
        'PlatformSpecific("pc") { String("DLUMA_Platform", "한국어") }',
        r'String("DLUMA_Escaped", "line\nquote\"")',
        "",
    ]), encoding="utf-8")
    values = parse_scr(source)
    output = tmp_path / "sample.bin"
    compile_file(source, output)
    assert decompile_values(output.read_bytes()) == values
    assert values["DLUMA_Escaped"] == 'line\nquote"'


def test_pak_contains_only_the_expected_locale_entries():
    with zipfile.ZipFile(PACK) as archive:
        names = {name.replace("\\", "/") for name in archive.namelist()}
        expected = {
            f"data/locale/{code}/loader_texts_all.scr" for code in SUPPORTED
        }
        assert expected <= names
        for code in SUPPORTED:
            entry = f"data/locale/{code}/loader_texts_all.scr"
            packaged = archive.read(entry).decode("utf-16")
            source = (
                LOCALE_ROOT / code / "loader_texts_all.scr"
            ).read_text(encoding="utf-8-sig")
            assert packaged.replace("\r\n", "\n") == source.replace("\r\n", "\n")
        assert not any(
            name.endswith("dluma_locales.ini") for name in names
        )


def test_native_runtime_uses_text_locale_and_caches_crash_strings():
    localization = (ROOT / "src/dlw_localization.c").read_text(encoding="utf-8")
    exports = (ROOT / "src/dlw_exports.c").read_text(encoding="utf-8")
    crash = (ROOT / "src/dlw_crash.c").read_text(encoding="utf-8")
    assert "GetVoiceLocaleID" not in localization
    assert "DLUMA_GET_LOCALE_ID_RVA" in localization
    assert 'dluma_localization_load_catalog("En", 1)' in localization
    assert "localized_from_file_core(\n        path, replace_existing)" in localization
    assert "dluma_engine_get_game_interface_for_localization" in localization
    assert "dluma_localization_load_bootstrap()" in exports
    player = exports.split(
        "static void* dluma_player_initialize(void)", 1
    )[1].split("static __int64 dluma_player_shutdown(void)", 1)[0]
    assert player.index("dluma_localization_load_bootstrap()") < player.index("manager = g_original_initialize()")
    assert exports.index("manager = g_original_initialize()") < exports.index(
        "dluma_engine_bind(&g_dluma.engine)"
    ) < exports.index("dluma_localization_load_initial()")
    assert "dluma_crash_localization_refresh();" in localization
    assert "dluma_read_file_utf8(g_dluma.locale_path" not in crash
    assert "dluma_crash_load_localization" not in crash
    assert "DLUMA_Crash_Title_01" in (LOCALE_ROOT / "En/loader_texts_all.scr").read_text(encoding="utf-8")
    titles_start = crash.index("g_dluma_crash_english_titles")
    titles_end = crash.index("};", titles_start)
    assert len(re.findall(r'^\s*L"', crash[titles_start:titles_end], re.MULTILINE)) == 16
    assert "dluma_text_get_wide(key, g_dluma_crash_english_titles[i]," in crash
    picker = crash[crash.index("static const WCHAR* dluma_crash_pick_title") :]
    assert "if (!g_dluma.config.crash_random_titles)" in picker
    assert "return g_dluma_crash.text.neutral_title;" in picker


def test_selected_catalog_replaces_english_and_missing_catalog_falls_back():
    localization = (ROOT / "src/dlw_localization.c").read_text(encoding="utf-8")
    english = parse_scr(LOCALE_ROOT / "En/loader_texts_all.scr")
    chinese = parse_scr(LOCALE_ROOT / "Cn/loader_texts_all.scr")
    assert english["DLUMA_Menu_Mods"] == "Mods"
    assert chinese["DLUMA_Menu_Mods"] == "模组"

    apply_start = localization.index(
        "static int dluma_localization_apply_catalogs("
    )
    apply_end = localization.index(
        "static unsigned char __fastcall dluma_localized_from_file_hook(",
        apply_start,
    )
    apply = localization[apply_start:apply_end]
    english_load = apply.index('dluma_localization_load_catalog("En", 1)')
    locale_query = apply.index("dluma_localization_query_locale(selected)")
    selected_load = apply.index(
        "dluma_localization_load_catalog(selected, 1)"
    )
    fallback = apply.index(
        'dluma_localization_copy_ascii(selected, 8, "En")'
    )
    sentinel = apply.index("dluma_localization_verify_sentinel()")
    assert english_load < locale_query < selected_load < fallback < sentinel
    assert "english_loaded && selected_loaded" not in apply
    assert '"localization_catalog_fallback", "En"' in apply
    assert '"localization_active_locale"' in apply


def test_english_crash_quips_are_nonempty_and_balanced():
    english = parse_scr(LOCALE_ROOT / "En/loader_texts_all.scr")
    titles = [english[f"DLUMA_Crash_Title_{index:02d}"] for index in range(1, 17)]
    assert all(title for title in titles)
    assert all(title.startswith('"') and title.endswith('"') for title in titles)


def test_native_localization_binding_matches_the_exact_devtools_engine():
    localization = (ROOT / "src/dlw_localization.c").read_text(encoding="utf-8")
    assert "0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x6C" in localization
    assert "0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B" in localization
    assert "0x48, 0x8B, 0x02, 0x48, 0x8D, 0x0D, 0x87, 0xFF" in localization
    assert 'GetModuleHandleW(L"MSVCR100.dll")' in localization


@pytest.mark.parametrize("path", [
    ROOT / "PakAssets/XUI/DLUMA_Main_PC_workshop.xui",
    ROOT / "PakAssets/XUI/DLUMA_MenuOptions.xui",
    ROOT / "PakAssets/XUI/MenuDLUMALoaderSettings.xui",
    ROOT / "PakAssets/XUI/MenuDLUMAMods.xui",
    ROOT / "PakAssets/XUI/MenuDLUMAModDetails.xui",
    ROOT / "PakAssets/XUI/MenuDLUMAModConfig.xui",
    ROOT / "PakAssets/XUI/DLUMA_SteamWorkshop.xui",
])
def test_loader_xui_visible_literals_use_engine_keys(path):
    text = path.read_text(encoding="utf-8")
    assert "&amp;DLUMA_" in text
