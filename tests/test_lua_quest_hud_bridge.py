from __future__ import annotations

import json
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
API = (ROOT / "src/dlw_lua54_api.h").read_text(encoding="utf-8")
HELPER = (ROOT / "src/dlw_lua54_helper.c").read_text(encoding="utf-8")
NATIVE = (ROOT / "src/dlw_quest_hud.c").read_text(encoding="utf-8")
RUNTIME = (ROOT / "src/dlw_lua_runtime.c").read_text(encoding="utf-8")
GAMEPLAY = (ROOT / "src/dlw_native_gameplay.c").read_text(encoding="utf-8")


def test_01_private_abi_appends_quest_hud_callbacks_and_capability():
    assert "#define DLUMA_LUA54_ABI_VERSION 0x00040000u" in API
    assert "DLUMA_LUA54_CAP_UI_QUEST_HUD = 1u << 19" in API
    assert "#define DLUMA_LUA54_MAX_QUEST_HUD_OBJECTIVES 4u" in API
    assert "#define DLUMA_LUA54_MAX_QUEST_HUD_OVERRIDE_TEXT 512u" in API
    callbacks = API[API.index("typedef struct DlumaLua54HostCallbacks {"):]
    for callback in (
        "quest_hud_snapshot",
        "quest_hud_override_set",
        "quest_hud_override_clear",
        "quest_hud_clear_owner",
    ):
        assert callback in callbacks
    assert callbacks.index("quest_hud_clear_owner") < callbacks.index(
        "capabilities"
    )


def test_01a_visual_studio_project_compiles_the_bridge_translation_unit():
    project = (ROOT / "DLUMA.vcxproj").read_text(encoding="utf-8")
    assert '<ClCompile Include="src\\dlw_quest_hud.c" />' in project
    assert '<ClInclude Include="src\\dlw_quest_hud.h" />' in project


def test_02_exact_player_profile_and_all_recovered_targets_are_gated():
    for value in (
        "cbc7dd7923778936341cdfb755474a0c714079d910849df16ffc5ec88a103b95",
        "a18ca1103614947ae7ff956c6852f45123b440563a967d9e1e496c42fa574a3c",
        "DLUMA_QH_VTABLE_RVA 0x015DA018UL",
        "DLUMA_QH_POST_SLOT_OFFSET 0x0390UL",
        "DLUMA_QH_POST_SLOT_RVA 0x015DA3A8UL",
        "DLUMA_QH_INITIALIZE_RVA 0x00517CD0UL",
        "DLUMA_QH_UPDATE_RVA 0x005188E0UL",
        "DLUMA_QH_TIMER_UPDATE_RVA 0x00519D50UL",
        "DLUMA_QH_APPLY_ENTRY_RVA 0x0051AB20UL",
        "DLUMA_QH_SET_TEXT_CLAMP_RVA 0x00534E10UL",
    ):
        assert value in NATIVE
    assert "dluma_qh_module_hash_matches" in NATIVE
    assert "g_dluma_qh_post_prefix" in NATIVE
    assert "g_dluma_qh_initialize_prefix" in NATIVE
    assert "g_dluma_qh_update_prefix" in NATIVE
    assert "g_dluma_qh_timer_prefix" in NATIVE
    assert "g_dluma_qh_apply_entry_prefix" in NATIVE
    assert "g_dluma_qh_set_clamp_prefix" in NATIVE
    assert "quest_hud_profile_unavailable_fail_closed" in NATIVE


def test_03_live_layout_is_bounded_and_engine_pointers_never_cross_lua():
    for value in (
        "DLUMA_QH_ROWS_OFFSET 88U",
        "DLUMA_QH_ROW_COUNT_OFFSET 96U",
        "DLUMA_QH_HEADER_TEXT_OFFSET 128U",
        "DLUMA_QH_TIMER_TEXT_OFFSET 160U",
        "DLUMA_QH_ENTRIES_OFFSET 240U",
        "DLUMA_QH_ENTRY_COUNT_OFFSET 248U",
        "row_count > (int32_t)DLUMA_LUA54_MAX_QUEST_HUD_OBJECTIVES",
        "entry_count > (int32_t)DLUMA_QH_MAX_ENTRIES",
        "dluma_qh_text_element_valid",
    ):
        assert value in NATIVE
    public_records = API[
        API.index("typedef struct DlumaLua54QuestHudTarget"):
        API.index("typedef struct DlumaLua54QuestRequest")
    ]
    assert "void *" not in public_records
    assert "uintptr_t" not in public_records


def test_04_original_post_update_runs_before_copy_and_no_lua_runs_in_hook():
    hook = NATIVE[
        NATIVE.index("static void __fastcall dluma_qh_post_hook(void *component)\n{"):
        NATIVE.index("static int dluma_qh_patch_slot")
    ]
    assert hook.index("original(component);") < hook.index(
        "dluma_qh_process_component(component);"
    )
    assert "lua_" not in hook.lower()


def test_05_hook_ownership_protection_flush_and_drain_fail_safe():
    assert NATIVE.count("dluma_atomic_pointer_compare_exchange") >= 4
    assert NATIVE.count("VirtualProtect(") >= 6
    assert NATIVE.count("FlushInstructionCache(") >= 2
    assert "quest_hud_hook_restore_ownership_lost_retained" in NATIVE
    assert "quest_hud_hook_drain_timeout_retained" in NATIVE
    assert "DLUMA_QH_STOP_DRAIN_MS 2000ULL" in NATIVE


def test_06_override_lifecycle_is_owned_generation_bound_and_newest_wins():
    assert "candidate->sequence > winner->sequence" in NATIVE
    assert "request->map_generation != g_dluma_qh.map_generation" in NATIVE
    assert "quest_hud_target_expired" in NATIVE
    assert "entry->owner_id != owner_id" in NATIVE
    assert "dluma_quest_hud_clear_owner" in NATIVE
    assert "dluma_quest_hud_on_map_generation_changed" in GAMEPLAY
    assert "dluma_quest_hud_clear_owner(slot->owner_id)" in RUNTIME


def test_07_helper_scope_permissions_and_text_validation_match_contract():
    assert '"ui.quest_hud", "game.read"' in HELPER
    assert '"ui.quest_hud", "ui.overlay"' in HELPER
    assert "script->scope == DLUMA_LUA54_SCOPE_GLOBAL" in HELPER
    assert "quest_hud_scope_not_supported" in HELPER
    assert "dluma_helper_valid_utf8(text, text_length)" in HELPER
    assert "DLUMA_LUA54_MAX_QUEST_HUD_OVERRIDE_TEXT" in HELPER
    assert "memchr(text, 0, text_length)" in HELPER
    assert "dluma_helper_quest_hud_override_gc" in HELPER


