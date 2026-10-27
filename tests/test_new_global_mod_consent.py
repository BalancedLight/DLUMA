from __future__ import annotations

import hashlib
import re
import struct
from pathlib import Path
import zipfile


ROOT = Path(__file__).resolve().parents[1]
COMMON = (ROOT / "src/dlw_common.h").read_text(encoding="utf-8")
CONSENT = (ROOT / "src/dlw_global_consent.c").read_text(encoding="utf-8")
DISCOVERY = (ROOT / "src/dlw_lua_discovery.c").read_text(encoding="utf-8")
RUNTIME = (ROOT / "src/dlw_lua_runtime.c").read_text(encoding="utf-8")
UI = (ROOT / "src/dlw_ui.c").read_text(encoding="utf-8")
MAIN_XUI = (ROOT / "PakAssets/XUI/DLUMA_Main_PC_workshop.xui").read_text(
    encoding="utf-8"
)
MODS_XUI_PATH = ROOT / "PakAssets/XUI/MenuDLUMAMods.xui"
MODS_XUI = MODS_XUI_PATH.read_text(encoding="utf-8")
DETAILS_XUI_PATH = ROOT / "PakAssets/XUI/MenuDLUMAModDetails.xui"
DETAILS_XUI = DETAILS_XUI_PATH.read_text(encoding="utf-8")


def field_hash(schema: str, fields: list[str]) -> str:
    data = bytearray(struct.pack(">I", len(schema.encode())))
    data.extend(schema.encode())
    data.extend(struct.pack(">I", len(fields)))
    for field in fields:
        encoded = field.encode()
        data.extend(struct.pack(">I", len(encoded)))
        data.extend(encoded)
    return hashlib.sha256(data).hexdigest()


def normalize_permissions(value: str) -> str:
    return ",".join(sorted(set(part.strip().lower() for part in value.split(",") if part.strip())))


def metadata_fingerprint(
    content: bytes,
    *,
    version: str = "1.0.0",
    permissions: str = "game.read, ui.debug",
    source_type: str = "subscribed",
    workshop_id: str = "123",
    source_identity: str = "subscribed-workshop:123",
    path: str = "data/scripts/lua/global/main.lua",
) -> tuple[str, str, str]:
    normalized = normalize_permissions(permissions)
    content_hash = hashlib.sha256(content).hexdigest()
    permission_hash = field_hash("DLW_GLOBAL_PERMISSION_FP_V1", [normalized])
    metadata_hash = field_hash(
        "DLW_GLOBAL_METADATA_FP_V1",
        [
            "example.mod",
            "global",
            version,
            normalized,
            content_hash,
            source_type,
            workshop_id,
            source_identity,
            path,
        ],
    )
    return content_hash, permission_hash, metadata_hash


def block(source: str, start: str, end: str) -> str:
    begin = source.index(start)
    return source[begin : source.index(end, begin)]


def test_exact_content_and_canonical_fingerprints_are_deterministic():
    first = metadata_fingerprint(b"return { value = 1 }\r\n")
    second = metadata_fingerprint(b"return { value = 1 }\r\n")
    changed_bytes = metadata_fingerprint(b"return { value = 1 }\n")
    assert first == second
    assert first[0] != changed_bytes[0]
    assert first[2] != changed_bytes[2]
    assert "DLW_GLOBAL_METADATA_FP_V1" in CONSENT
    assert "DLW_GLOBAL_PERMISSION_FP_V1" in CONSENT
    assert "DLW_GLOBAL_REGISTRY_KEY_V1" in CONSENT
    assert "BYTE buffer[16384]" in (ROOT / "src/dlw_sha256.c").read_text()


def test_version_permission_source_and_path_changes_invalidate_metadata_identity():
    base = metadata_fingerprint(b"return {}")
    assert base[2] != metadata_fingerprint(b"return {}", version="1.0.1")[2]
    assert base[2] != metadata_fingerprint(b"return {}", permissions="game.read,ui.debug,world.query")[2]
    assert base[2] != metadata_fingerprint(b"return {}", source_type="local", workshop_id="", source_identity="local-project:c:/demo")[2]
    assert base[2] != metadata_fingerprint(b"return {}", path="data/scripts/lua/global/renamed.lua")[2]
    assert "DLUMA_GLOBAL_CHANGE_PERMISSION_EXPANSION" in COMMON
    assert '"permission set expanded"' in CONSENT


def test_permission_reordering_does_not_change_permission_fingerprint():
    left = metadata_fingerprint(b"return {}", permissions="ui.debug, game.read,world.query")
    right = metadata_fingerprint(b"return {}", permissions="world.query,game.read, ui.debug")
    assert left[1] == right[1]
    assert left[2] == right[2]


def test_first_discovery_and_duplicate_ids_fail_closed():
    classify = block(
        CONSENT, "void dluma_lua_classify_global_review", "static int dluma_ini_buffer_append"
    )
    discover = block(
        DISCOVERY, "void dluma_lua_discover_globals(void)", "unsigned int dluma_lua_discover_locals"
    )
    assert "Author preference is display-only until exact player approval" in classify
    assert "item->enabled = 0" in classify
    assert "item->enabled_requested = 0" in classify
    assert "DLUMA_GLOBAL_REVIEW_NEW" in classify
    assert "next[existing].enabled = 0" in discover
    assert "next[existing].review_state = DLUMA_GLOBAL_REVIEW_CONFLICT" in discover
    assert "dluma_ui_registry_build_conflict_sources" in UI
    assert "dluma_stricmp(item->id, selected->id)" in UI
    assert '"global_state_unload_queued"' in discover


def test_review_states_are_distinct_and_exact_approval_is_required():
    for state in (
        "DLUMA_GLOBAL_REVIEW_NEW",
        "DLUMA_GLOBAL_REVIEW_SEEN",
        "DLUMA_GLOBAL_REVIEW_APPROVED_DISABLED",
        "DLUMA_GLOBAL_REVIEW_APPROVED_ENABLED",
        "DLUMA_GLOBAL_REVIEW_CHANGED_REVIEW_REQUIRED",
        "DLUMA_GLOBAL_REVIEW_CONFLICT",
    ):
        assert state in COMMON
    request = block(
        CONSENT, "int dluma_lua_request_global_enabled", "void dluma_lua_process_global_runtime_queue"
    )
    assert "dluma_global_find_exact_locked(request" in request
    assert "!snapshot.approval_valid && !approve_current" in request
    assert "dluma_lua_permission_is_unsupported" in request


def test_seen_is_committed_only_after_details_visibility():
    confirm = block(
        UI,
        "static void dluma_ui_mod_details_confirm_visible",
        "static void dluma_ui_registry_set_main_controls_visible",
    )
    assert confirm.index("dluma_ui_mod_details_is_visible") < confirm.index(
        "dluma_lua_mark_global_seen"
    )
    assert "new_global_details_seen_persist_failed" in confirm
    visibility = block(
        UI,
        "static int dluma_ui_mod_details_is_visible",
        "static void dluma_ui_mod_details_refresh_after_seen",
    )
    assert "g_dluma.engine.is_ui_screen_visible(level, screen_id)" in visibility
    stage = block(
        UI,
        "static int dluma_ui_mod_details_stage_selected",
        "static int dluma_ui_mod_details_revalidate",
    )
    assert "dluma_lua_mark_global_seen" not in stage


def test_native_details_contract_is_complete_selectable_and_bounded():
    assert "<Id>MenuDLUMAModDetails</Id>" in DETAILS_XUI
    assert "<ClassOverride>MenuSteamWorkshop</ClassOverride>" in DETAILS_XUI
    assert "<Visual>MySceneVisual</Visual>" in DETAILS_XUI
    assert "<Visual>ListBoxOptionsV</Visual>" in DETAILS_XUI
    for control in (
        "B_PrevScreenMode",
        "B_NextScreenMode",
        "T_CurrentMode",
        "L_Maps",
        "T_DetailsHintHeader",
        "T_DetailsHintBody",
        "T_DetailsStatus",
        "B_Back",
    ):
        assert f"<Id>{control}</Id>" in DETAILS_XUI
    assert "<PressKey>22529</PressKey>" in DETAILS_XUI
    assert "<UIScrollView>" in DETAILS_XUI
    assert "<Id>G_DetailsValueScroll</Id>" in DETAILS_XUI
    assert "<Id>LBScrollBar</Id>" in DETAILS_XUI
    assert "G_DetailsPopup" not in MODS_XUI
    assert "B_DetailsClose" not in MODS_XUI

    populate = block(
        UI,
        "static void dluma_ui_mod_details_populate_list",
        "static int dluma_ui_mod_details_select_pointer_item",
    )
    for category in (
        "DLUMA_MOD_DETAILS_OVERVIEW",
        "DLUMA_MOD_DETAILS_STATE",
        "DLUMA_MOD_DETAILS_SECURITY",
        "DLUMA_MOD_DETAILS_CONFIG",
    ):
        assert category in populate or category in UI
    assert "DLUMA_UI_MOD_DETAILS_VISIBLE_ROWS" in populate
    assert "dluma_ui_settings_add_row" in UI
    assert "dluma_ui_settings_add_native_option" in UI
    assert "dluma_ui_mod_details_update_for_row(0)" in populate
    add_field = block(
        UI,
        "static int dluma_ui_mod_details_add_field_kind",
        "static int dluma_ui_mod_details_add_field(",
    )
    assert "dluma_ui_mod_details_add_label_row(" in add_field
    label_row = block(
        UI,
        "static void *dluma_ui_mod_details_add_label_row",
        "static int dluma_ui_settings_add_native_option",
    )
    assert '"OptionTextDummy"' in label_row
    assert '"ButtonDummy"' not in label_row
    assert 'dluma_ui_find_required_child(item, "wrap")' in label_row
    assert "dluma_ui_set_element_visible(value_wrap, 0)" in label_row
    assert "dluma_ui_settings_add_native_option" not in add_field
    assert "dluma_ui_find_required_child" not in add_field
    assert "dluma_ui_set_element_visible" not in add_field
    assert "g_dluma_main_menu_get_text_extent_real" not in add_field
    assert "g_dluma_main_menu_set_size_x" not in add_field
    assert "g_dluma_main_menu_set_relative_pos" not in add_field
    assert "DLUMA_UI_MOD_DETAILS_ROW_PREVIEW_BYTES" not in UI

    security = block(
        UI,
        "static void dluma_ui_mod_details_add_security",
        "static void dluma_ui_mod_details_add_config",
    )
    assert '"CONTENT / MODULE SHA-256"' in security
    assert '"METADATA / SIDECAR SHA-256"' in security
    assert '"PERMISSION / APPROVAL FINGERPRINT"' in security
    assert '"REGISTRY IDENTITY"' in security
    config = block(
        UI,
        "static void dluma_ui_mod_details_add_config",
        "static void dluma_ui_mod_details_update_for_row",
    )
    assert '"SCHEMA FINGERPRINT"' in config
    assert '"DIAGNOSTIC"' in config


def test_one_aggregate_popup_has_unique_loader_owned_ids_and_frames():
    root_id = "G_DLUMANewGlobalModsPopup"
    assert MAIN_XUI.count(f"<Id>{root_id}</Id>") == 2
    assert re.search(rf"<Properties>\s*<Id>{root_id}</Id>", MAIN_XUI)
    assert re.search(rf"<Timeline>\s*<Id>{root_id}</Id>", MAIN_XUI)
    ids = (
        "G_DLUMANewGlobalModsPanel",
        "T_DLUMANewGlobalModsTitle", "T_DLUMANewGlobalModsText",
        "I_DLUMANewGlobalModsIcon", "B_DLUMAReviewMods", "B_DLUMALater",
    )
    for object_id in ids:
        assert MAIN_XUI.count(f"<Id>{object_id}</Id>") == 1
    popup = MAIN_XUI[MAIN_XUI.index("<Id>G_DLUMANewGlobalModsPopup</Id>") :]
    assert "<Show>false</Show>" in popup[:400]
    for frame in ("Show", "EndShow", "Hide", "EndHide"):
        assert f"<Name>{frame}</Name>" in popup
    for stock_id in ("G_NewsPopup", "T_PopupTxt", "I_PopupIcon", "B_PopupClose"):
        assert f"<Id>{stock_id}</Id>" not in popup
    text_id = popup.index("<Id>T_DLUMANewGlobalModsText</Id>")
    text_start = popup.rfind("<AdvControl>", 0, text_id)
    assert text_start >= 0
    text_end = popup.index("</AdvControl>", text_id) + len("</AdvControl>")
    text_control = popup[text_start:text_end]
    assert "<AdvControl>" in text_control
    assert "<ClassOverride>UIButton</ClassOverride>" in text_control
    assert "<UISmartText>" not in text_control


def test_aggregate_popup_uses_later_as_the_safe_initial_focus():
    assert "dluma_ui_focus_element(g_dluma_new_global_later);" in UI


def test_popup_batch_is_sorted_schema_hashed_and_persisted_after_visibility():
    recompute = block(
        CONSENT,
        "void dluma_lua_recompute_unseen_batch_locked",
        "int dluma_lua_snapshot_unseen_batch",
    )
    assert "DLW_GLOBAL_UNSEEN_BATCH_V1" in recompute
    assert "dluma_stricmp(fingerprints[j - 1], value) > 0" in recompute
    tick = block(UI, "static void dluma_ui_new_global_tick", "static int dluma_ui_new_global_handle_button")
    assert tick.index("dluma_ui_is_element_visible") < tick.index("dluma_lua_mark_unseen_batch_prompted")
    assert '"stock news or Yes/No popup is visible"' in tick
    assert '"screen transition is active"' in tick
    assert "batch.unseen_count == 1U" in tick
    assert "new global Lua mods were found" in tick


def test_popup_review_uses_native_mods_id_route_and_later_never_enables():
    handler = block(
        UI,
        "static int dluma_ui_new_global_handle_button",
        "static void dluma_ui_fail_safe_restore_main_menu",
    )
    assert 'dluma_ui_find_required_child(g_dluma_new_global_main_root, "B_Mods")' in handler
    assert "dluma_ui_route_mods_button_by_id(menu_main, mods_button)" in handler
    assert "original(menu_main, mods_button, event_code)" in handler
    later = handler[handler.index("if (is_later)") :]
    assert "dluma_lua_request_global_enabled" not in later
    assert "new_global_popup_later_selected" in later


def test_unapproved_toggle_shows_confirmation_and_runtime_work_is_queued():
    activation = block(
        UI,
        "static int dluma_ui_registry_activate_selected",
        "static void __fastcall dluma_ui_workshop_button_hook",
    )
    assert "dluma_ui_registry_request_row_state" in activation
    request = block(
        UI,
        "static int dluma_ui_registry_request_row_state",
        "static int dluma_ui_registry_activate_selected",
    )
    assert "dluma_ui_registry_show_approval_popup" in request
    assert "dluma_lua_request_global_enabled" in request
    assert "dluma_lua_runtime_create_slot" not in request
    assert "G_ApprovalPopup" in MODS_XUI
    assert "B_ApprovalConfirm" in MODS_XUI
    assert "B_ApprovalCancel" in MODS_XUI
    for control in (
        "T_ApprovalName", "T_ApprovalId", "T_ApprovalVersion",
        "T_ApprovalSource", "T_ApprovalRisk", "T_ApprovalPermissions",
        "T_ApprovalChange", "T_ApprovalScope",
    ):
        assert f"<Id>{control}</Id>" in MODS_XUI
    assert "<UISmartText>" not in MODS_XUI
    assert "DLUMA_Common_Enable" in MODS_XUI
    assert '"REVIEW & ENABLE"' not in UI
    approval = block(
        UI,
        "static int dluma_ui_registry_show_approval_popup",
        "static void dluma_ui_registry_hide_approval_popup",
    )
    assert '"T_ApprovalText"' not in approval
    assert 'dluma_ui_registry_find_control(screen, "T_ApprovalTitle")' not in approval
    assert "dluma_ui_registry_set_popup_field" in approval
    aggregate = block(
        UI,
        "static void dluma_ui_new_global_tick",
        "static int dluma_ui_new_global_handle_button",
    )
    assert "dluma_ui_set_element_text(g_dluma_new_global_title" not in aggregate
    assert "<br><br>" in aggregate
    assert "DLUMA_GLOBAL_TRANSITION_ENABLING" in CONSENT
    assert "DLUMA_GLOBAL_TRANSITION_DISABLING" in CONSENT
    assert "dluma_lua_process_global_runtime_queue" in RUNTIME


def test_unsupported_permissions_cannot_be_approved_and_only_current_world_write_is_sensitive_supported():
    unsupported = block(
        CONSENT,
        "int dluma_lua_permission_is_unsupported",
        "int dluma_lua_request_global_enabled",
    )
    for permission in (
        "game.read", "world.query", "ui.debug", "world.write",
    ):
        assert f'"{permission}"' in unsupported
    assert "if (!known) return 1" in unsupported
    approved = block(
        RUNTIME,
        "static uint32_t dluma_lua_runtime_approved_permissions",
        "static int dluma_lua_runtime_key_valid",
    )
    assert "DLUMA_LUA54_PERMISSION_WORLD_WRITE" in approved
    assert "DLUMA_LUA54_PERMISSION_PLAYER_WRITE" not in approved


def test_atomic_unknown_preserving_persistence_and_stale_snapshot_rejection():
    assert "else if (!dluma_ini_buffer_append" in CONSENT
    assert "old_text + line_start" in CONSENT
    assert "FlushFileBuffers(file)" in CONSENT
    assert "DLUMA_MOVEFILE_WRITE_THROUGH" in CONSENT
    exact = block(
        CONSENT, "static int dluma_global_find_exact_locked", "static int dluma_global_queue_command"
    )
    assert "request->registry_revision" in exact
    assert "request->metadata_fingerprint" in exact
    assert "request->registry_key" in exact


def test_pack_has_only_loader_owned_paths_and_current_xui_bytes():
    pack = ROOT / "build/package/Release/dluma.pak"
    with zipfile.ZipFile(pack) as archive:
        names = set(archive.namelist())
        assert archive.read("data/menu/scr/MenuDLUMAMods.xui") == MODS_XUI_PATH.read_bytes()
        assert (
            archive.read("data/menu/scr/MenuDLUMAModDetails.xui")
            == DETAILS_XUI_PATH.read_bytes()
        )
    expected = {
        "data/menu/scr/DLUMA_Main_PC_workshop.xui",
        "data/menu/scr/MenuDLUMAMods.xui",
        "data/menu/scr/MenuDLUMAModDetails.xui",
        "data/menu/scr/MenuOptions.xui",
        "data/menu/scr/MenuDLUMALoaderSettings.xui",
        "data/menu/scr/MenuDLUMAModConfig.xui",
        "data/menu/scr/DLUMA_SteamWorkshop.xui",
        "data/menu/menubackgroundcameraactions_menu_lvl_new.scr",
        "data/menu/menuscreen3dplacement_menu_lvl_new.scr",
    }
    assert expected.issubset(names)
    assert sum(name.startswith("data/locale/") for name in names) == 17


def test_xui_is_utf8_without_bom_and_crlf_only():
    for path in (
        ROOT / "PakAssets/XUI/DLUMA_Main_PC_workshop.xui",
        MODS_XUI_PATH,
        ROOT / "PakAssets/XUI/MenuDLUMAModDetails.xui",
        ROOT / "PakAssets/XUI/DLUMA_MenuOptions.xui",
        ROOT / "PakAssets/XUI/MenuDLUMALoaderSettings.xui",
        ROOT / "PakAssets/XUI/MenuDLUMAModConfig.xui",
        ROOT / "PakAssets/XUI/DLUMA_SteamWorkshop.xui",
    ):
        raw = path.read_bytes()
        assert not raw.startswith((b"\xef\xbb\xbf", b"\xff\xfe", b"\xfe\xff"))
        assert b"\r\n" in raw
        assert raw.replace(b"\r\n", b"").find(b"\n") == -1
