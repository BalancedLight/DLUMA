from pathlib import Path
import zipfile

ROOT = Path(__file__).resolve().parents[1]


def test_main_menu_mods_button_navigation():
    text = (ROOT / "PakAssets" / "XUI" / "DLUMA_Main_PC_workshop.xui").read_text()
    assert "<Id>MenuMain</Id>" in text
    assert "<ClassOverride>MenuMainDI</ClassOverride>" in text
    assert "<Id>B_Mods</Id>" in text
    mods_id = text.index("<Id>B_Mods</Id>")
    mods_start = text.rfind("<XuiNavButton>", 0, mods_id)
    mods_end = text.index("</XuiNavButton>", mods_id) + len("</XuiNavButton>")
    mods = text[mods_start:mods_end]
    assert "<XuiNavButton>" in mods
    assert "<ClassOverride>" not in mods
    assert "<PressPath>MenuDLUMAMods.xui</PressPath>" in mods
    assert "<NavUp>B_PlayCustomMap</NavUp>" in mods
    assert "<NavDown>B_Continue</NavDown>" in mods
    play = text[text.index("<Id>B_PlayCustomMap</Id>") :]
    assert "<NavDown>B_Mods</NavDown>" in play[:900]
    cont = text[text.index("<Id>B_Continue</Id>") :]
    assert "<NavUp>B_Mods</NavUp>" in cont[:900]
    # UIStackPanel renders the menu children in reverse visual order.
    assert text.index("<Id>B_Mods</Id>") < text.index("<Id>B_PlayCustomMap</Id>")


def test_mods_id_route_is_independent_of_frontend_polling_and_button_class():
    ui = (ROOT / "src" / "dlw_ui.c").read_text()
    start = ui[ui.index("int dluma_ui_start(void)") : ui.index("void dluma_ui_stop(void)")]
    hook = ui[ui.index("static void __fastcall dluma_ui_menu_button_hook") : ui.index("static int dluma_ui_install_mods_button_hook")]
    route = ui[ui.index("static int dluma_ui_route_mods_button_by_id") : ui.index("static void __fastcall dluma_ui_workshop_button_hook")]
    assert "dluma_ui_install_mods_button_hook()" in start
    assert "dluma_engine_query_level" not in hook
    assert "g_dluma_get_object_level(button)" in route
    assert "dluma_ui_register_mods_screen(&g_dluma.engine, level)" in route
    assert "dluma_ui_show_next_registered_screen_from_menu_main" in route
    assert "if (!handled && original) original(menu_main, button, event_code);" in hook
    mods_route = hook[hook.index('dluma_ui_object_id_equals(button, "B_Mods")') : hook.index("/* On the supported build")]
    assert "handled = dluma_ui_route_mods_button_by_id(menu_main, button);" in mods_route
    assert "dluma_ui_open_mods_screen" not in hook
    assert "PressPath" not in mods_route
    assert "UINaviButton" not in mods_route


def test_worker_consumes_main_thread_level_snapshot():
    host = (ROOT / "src" / "dlw_host.c").read_text()
    ui = (ROOT / "src" / "dlw_ui.c").read_text()
    assert "dluma_ui_read_level_snapshot" in host
    assert "dluma_engine_query_level" not in host
    assert "dluma_ui_publish_level_snapshot" in ui


def test_mods_screen_uses_concrete_workshop_controller_contract():
    path = ROOT / "PakAssets" / "XUI" / "MenuDLUMAMods.xui"
    raw = path.read_bytes()
    text = raw.decode("utf-8")
    assert "<Id>MenuDLUMAMods</Id>" in text
    assert "<ClassOverride>MenuSteamWorkshop</ClassOverride>" in text
    assert "<ClassOverride>UIMainMenuScreen</ClassOverride>" not in text
    assert "<ClassOverride>MenuExtras</ClassOverride>" not in text
    assert "<!--" not in text
    assert b"\r\n" in raw
    for object_id in (
        "I_ModsPanel",
        "L_Maps",
        "B_Start",
        "B_Workshop",
        "B_Configure",
        "B_Back",
    ):
        assert f"<Id>{object_id}</Id>" in text
    assert "<ClassOverride>UIListBox</ClassOverride>" in text
    assert "<Visual>ListBoxOptionsV</Visual>" in text
    assert "<UINaviButton>" in text


def test_mods_screen_bridge_has_visible_identity_back_and_stock_timeline():
    text = (ROOT / "PakAssets" / "XUI" / "MenuDLUMAMods.xui").read_text()
    assert "<Text>&amp;DLUMA_Menu_Mods&amp;</Text>" in text
    assert (
        "<Text>TOGGLE</Text>" in text
        or "<Text>&amp;DLUMA_Common_Toggle&amp;</Text>" in text
    )
    assert "<Text>&amp;MSkills_Details&amp;</Text>" in text
    assert "<PressPath>MenuDLUMAModDetails.xui</PressPath>" in text
    assert "<Text>&amp;MenuMain_BtnBack&amp;</Text>" in text
    assert "<Id>B_Back</Id>" in text
    assert "<Name>TransIn</Name>" in text
    assert "<Name>EndTransIn</Name>" in text
    assert "<Name>TransOut</Name>" in text
    assert "<Name>EndTransOut</Name>" in text
    assert "<TimelineProp>Opacity</TimelineProp>" in text
    assert "<Id>MenuDLUMAMods</Id>" in text


def test_mods_footer_keeps_details_in_the_stock_center_slot():
    text = (ROOT / "PakAssets" / "XUI" / "MenuDLUMAMods.xui").read_text()
    assert text.count("<Id>B_Workshop</Id>") == 1
    assert "<Id>B_Details</Id>" not in text
    details_id = text.index("<Id>B_Workshop</Id>")
    details_start = text.rfind("<UINaviButton>", 0, details_id)
    details_end = text.index("</UINaviButton>", details_id)
    details = text[details_start:details_end]
    assert "<Position>550.000000,0.000000,0.000000</Position>" in details
    assert "<Visual>ButtonDialogV</Visual>" in details
    assert "<PressKey>22530</PressKey>" in details
    assert "<PressPath>MenuDLUMAModDetails.xui</PressPath>" in details


def test_merged_camera_script_preserves_stock_and_adds_mods_route():
    text = (ROOT / "PakAssets" / "Camera" / "menubackgroundcameraactions_menu_lvl_new.scr").read_text()
    assert 'MenuWithCameraActions("MenuOptions")' in text
    assert 'MenuWithCameraActions("MenuExtras")' in text
    assert 'MenuWithCameraActions("MenuSteamWorkshop")' in text
    assert 'MenuWithCameraActions("MenuDLUMAMods")' in text
    assert '"B_Options"' in text
    assert '"B_Extras"' in text
    assert '"B_Mods"' in text
    assert '"Movie_extras_in"' in text
    assert '"Movie_extras_out"' in text


def test_loader_mounts_the_content_pack_dynamically():
    source = (ROOT / "src" / "dlw_pack.c").read_text()
    assert "GetProcAddress" in source
    assert "?add_source@fs@@" in source
    assert 'L"dluma.pak"' in source
    assert "content_pack_mounted" in source


def test_camera_routes_keep_mods_on_the_established_right_side():
    text = (ROOT / "PakAssets" / "Camera" / "menubackgroundcameraactions_menu_lvl_new.scr").read_text()
    placement = (ROOT / "PakAssets" / "Camera" / "menuscreen3dplacement_menu_lvl_new.scr").read_text()
    assert 'CameraAction("MenuMain", "B_Extras", "", "OnButtonPressed", "", "Movie_extras_in", "MenuExtras")' in text
    assert 'CameraAction("MenuExtras", "", "", "OnBack", "", "Movie_extras_out", "MenuMain")' in text
    assert 'CameraAction("MenuMain", "B_Mods", "", "OnButtonPressed", "", "Movie_extras_in", "MenuDLUMAMods")' in text
    assert 'CameraAction("MenuDLUMAMods", "", "", "OnBack", "", "Movie_extras_out", "MenuMain")' in text
    dluma_routes = [line for line in text.splitlines() if "MenuDLUMA" in line]
    assert all("Movie_options_" not in line for line in dluma_routes)
    for screen_name in ("MenuDLUMAMods", "MenuDLUMAModConfig"):
        assert f'Placement("{screen_name}", "menu_extras_pos")' in placement
        assert f'Correction("{screen_name}",' not in placement
    assert (
        'Placement("MenuDLUMAModDetails", "menu_leaderboards_pos")'
        in placement
    )
    assert 'Correction("MenuDLUMAModDetails",' not in placement


def test_steam_workshop_camera_route_matches_play_game():
    camera = (ROOT / "PakAssets" / "Camera" / "menubackgroundcameraactions_menu_lvl_new.scr").read_text()
    placement = (ROOT / "PakAssets" / "Camera" / "menuscreen3dplacement_menu_lvl_new.scr").read_text()

    assert 'CameraAction("MenuSteamWorkshop", "", "MenuMain", "OnShowScreen", "", "Movie_new_game_in", "MenuSteamWorkshop")' in camera
    assert 'CameraAction("MenuSteamWorkshop", "", "", "OnBack", "", "Movie_new_game_out", "MenuMain")' in camera
    assert 'CameraAction("MenuMain", "", "MenuSteamWorkshop", "OnShowScreen", "", "Movie_new_game_out", "MenuMain")' in camera
    assert 'Placement("MenuSteamWorkshop", "menu_play_game_pos")' in placement
    assert 'Correction("MenuSteamWorkshop", 150, 0, 0)' in placement


def test_packed_mods_xui_matches_the_validated_source():
    pack = ROOT / "build" / "package" / "Release" / "dluma.pak"
    source = (ROOT / "PakAssets" / "XUI" / "MenuDLUMAMods.xui").read_bytes()
    with zipfile.ZipFile(pack) as archive:
        packed = archive.read("data/menu/scr/MenuDLUMAMods.xui")
    assert packed == source
    assert b"<ClassOverride>MenuSteamWorkshop</ClassOverride>" in packed
    assert b"<ClassOverride>UIMainMenuScreen</ClassOverride>" not in packed
    assert b"<ClassOverride>MenuExtras</ClassOverride>" not in packed
    assert b"<!--" not in packed
