from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]
EXPORTS = ROOT / "src" / "dlw_exports.c"
LOCALIZATION = ROOT / "src" / "dlw_localization.c"
RUNTIME_UI = ROOT / "src" / "dlw_runtime_ui_fixes.h"
WORKSHOP_XUI = ROOT / "PakAssets" / "XUI" / "DLUMA_SteamWorkshop.xui"
MODS_XUI = ROOT / "PakAssets" / "XUI" / "MenuDLUMAMods.xui"


def test_dynamic_localization_uses_native_wide_wrapped_tokens() -> None:
    source = LOCALIZATION.read_text(encoding="utf-8")
    assert "DLUMA_LOCALIZED_GET_RVA 0x00287280UL" in source
    assert "const DlumaLocalizationTtlWideString *key" in source
    assert "dluma_localization_build_lookup_token" in source
    assert "storage[0] = L'&';" in source
    assert "storage[length + 1U] = L'&';" in source
    assert "c < 0x21U || c > 0x7EU || c == '&'" in source
    assert "length >= capacity - 3U" in source
    assert "localized_get(0, &value, &key_token)" in source
    lookup_start = source.index("static int dluma_localization_lookup_text(")
    lookup_end = source.index(
        "static int dluma_localization_copy_text(", lookup_start
    )
    assert "DlumaLocalizationTtlString key_string" not in source[
        lookup_start:lookup_end
    ]


def test_localized_get_detour_is_removed_and_initial_load_is_post_bind() -> None:
    exports = EXPORTS.read_text(encoding="utf-8")
    runtime_ui = RUNTIME_UI.read_text(encoding="utf-8")
    assert "localized_get_adapter" not in exports
    assert "localized_get_adapter" not in runtime_ui
    assert "DlumaRuntimeLocalizedGet" not in runtime_ui
    assert "DLUMA_RUNTIME_LOCALIZED_GET" not in runtime_ui
    assert "VirtualProtect(" not in runtime_ui
    assert "FlushInstructionCache(" not in runtime_ui
    original_init = exports.index("manager = g_original_initialize()")
    engine_bind = exports.index("dluma_engine_bind(&g_dluma.engine)")
    load_initial = exports.index("dluma_localization_load_initial()")
    assert original_init < engine_bind < load_initial
    assert "dluma_localization_shutdown();" in exports


def test_mods_capacity_uses_native_ten_row_viewport() -> None:
    source = RUNTIME_UI.read_text(encoding="utf-8")
    xui = MODS_XUI.read_text(encoding="utf-8")
    assert "DLUMA_RUNTIME_MODS_VISIBLE_ROWS 10" in source
    assert '"MenuDLUMAMods.xui"' in source
    assert '"mods_menu_visible_capacity"' in source
    list_block = xui[xui.index("<Id>L_Maps</Id>"):]
    list_block = list_block[:list_block.index("</Properties>")]
    assert "<Height>454.000000</Height>" in list_block
    assert "<Visual>ListBoxOptionsV</Visual>" in list_block


def test_workshop_capacity_has_one_six_row_owner() -> None:
    source = RUNTIME_UI.read_text(encoding="utf-8")
    patched = (ROOT / "src" / "dlw_ui_patched.c").read_text(encoding="utf-8")
    xui = WORKSHOP_XUI.read_text(encoding="utf-8")
    assert "DLUMA_RUNTIME_WORKSHOP_VISIBLE_ROWS" not in source
    assert '"workshop_menu_visible_capacity"' not in source
    assert "DLUMA_UI_FIX_WORKSHOP_VISIBLE_ROWS 6" in patched
    list_pos = xui.index("<Id>L_Maps</Id>")
    list_start = xui.rfind("<UIListBox>", 0, list_pos)
    list_end = xui.index("</UIListBox>", list_pos)
    block = xui[list_start:list_end]
    assert "<Position>43.000000,104.000000,0.000000</Position>" in block
    assert "<Height>332.000000</Height>" in block
    assert "<Position>0.000000,467.200012,0.000000</Position>" in xui
    assert "<Position>0.000000,486.688477,0.000000</Position>" in xui
    assert xui.count("<Height>471.000000</Height>") == 2
    assert xui.count("<Height>215.000000</Height>") == 3


def test_capacity_adapter_is_exact_build_and_list_contract_gated() -> None:
    source = RUNTIME_UI.read_text(encoding="utf-8")
    assert "DLUMA_RUNTIME_UI_GAMEDLL_TIMESTAMP 0x623397AAUL" in source
    assert "DLUMA_RUNTIME_UI_GAMEDLL_IMAGE_SIZE 0x02082000UL" in source
    assert "DLUMA_RUNTIME_LIST_SET_CAPACITY_RVA 0x011242D0UL" in source
    assert "DLUMA_RUNTIME_UI_LIST_VTABLE_RVA 0x01917C98UL" in source
    assert "DLUMA_RUNTIME_UI_LIST_INTERFACE_VTABLE_RVA 0x01918688UL" in source
    assert re.search(r"set_capacity\(list, capacity, 1\)", source)


def test_capacity_timer_is_lifecycle_owned() -> None:
    exports = EXPORTS.read_text(encoding="utf-8")
    source = RUNTIME_UI.read_text(encoding="utf-8")
    assert "SetTimer(" in source
    assert "KillTimer(" in source
    assert "dluma_runtime_list_capacity_start()" in exports
    assert "dluma_runtime_list_capacity_stop();" in exports
    assert exports.index("dluma_runtime_list_capacity_stop();") < exports.index(
        "loader_stopped = dluma_host_stop();"
    )
