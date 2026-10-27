from __future__ import annotations

import importlib.util
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def load_builder_module():
    path = ROOT / "tools" / "build_content_pack.py"
    spec = importlib.util.spec_from_file_location("dluma_build_content_pack", path)
    assert spec and spec.loader
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def test_locale_scr_is_transcoded_to_runtime_utf16le(tmp_path: Path) -> None:
    builder = load_builder_module()
    source = tmp_path / "loader_texts_all.scr"
    source.write_text(
        '!String(s, s)\n// comment\nString("DLUMA_Test", "日本語 – test")\n',
        encoding="utf-8",
    )
    packed = builder.packed_source_bytes(
        source, "data/locale/Jp/loader_texts_all.scr"
    )
    assert packed.startswith(b"\xff\xfe")
    decoded = packed.decode("utf-16")
    assert decoded.startswith("!String(s, s)\r\n")
    assert "日本語 – test" in decoded
    assert "\r\n" in decoded
    assert "\n" not in decoded.replace("\r\n", "")


def test_non_locale_pack_assets_keep_their_original_bytes(tmp_path: Path) -> None:
    builder = load_builder_module()
    source = tmp_path / "asset.xui"
    original = b"<XuiCanvas>\nraw utf8\n</XuiCanvas>\n"
    source.write_bytes(original)
    assert builder.packed_source_bytes(
        source, "data/menu/scr/asset.xui"
    ) == original


def test_initial_localization_is_explicit_and_future_stock_reloads_reinject() -> None:
    source = (ROOT / "src" / "dlw_localization.c").read_text(encoding="utf-8")
    exports = (ROOT / "src" / "dlw_exports.c").read_text(encoding="utf-8")
    assert "DLUMA_LOCALIZED_FROM_FILE_CORE_RVA" in source
    assert "DLUMA_LOCALIZED_FROM_FILE_RVA 0x002873B0UL" in source
    assert "dluma_localized_from_file_hook" in source
    assert 'dluma_stricmp(leaf, "texts_temp.scr") == 0' in source
    assert 'dluma_stricmp(leaf, "texts.scr") == 0' in source
    assert "stock_GameWithMenu_ForceReloadTexts" in source
    assert "post_initialize_initial_catalogs" in source
    assert "localization_sentinel_DLUMA_Menu_Mods" in source
    assert "dluma_localization_value_is_missing_glyphs" in source
    player = exports.split(
        "static void* dluma_player_initialize(void)", 1
    )[1].split("static __int64 dluma_player_shutdown(void)", 1)[0]
    assert player.index("dluma_localization_bind()") < player.index(
        "manager = g_original_initialize()"
    )
    assert exports.index("manager = g_original_initialize()") < exports.index(
        "dluma_engine_bind(&g_dluma.engine)"
    ) < exports.index("dluma_localization_load_initial()")
    assert "initial stock load precedes this script-DLL lifecycle" in exports
    assert "future stock text reloads" in source


def test_unobserved_stock_routine_is_not_used_as_the_initial_anchor() -> None:
    source = (ROOT / "src" / "dlw_localization.c").read_text(encoding="utf-8")
    assert "DLUMA_STOCK_TEXT_LOAD_RVA" not in source
    assert "stock_text_load_trampoline" not in source
    assert "dluma_stock_text_load_hook" not in source
    assert "VirtualAlloc(" not in source
    assert "VirtualFree(" not in source


def test_pack_validator_rejects_utf8_runtime_catalogs() -> None:
    validator = (ROOT / "tools" / "validate_localization.py").read_text(
        encoding="utf-8"
    )
    assert 'data.startswith(b"\\xff\\xfe")' in validator
    assert "is not UTF-16LE with BOM" in validator
    assert 'data.decode("utf-16")' in validator
