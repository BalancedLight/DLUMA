from pathlib import Path
import zipfile


ROOT = Path(__file__).resolve().parents[1]
MODS_XUI = ROOT / "PakAssets" / "XUI" / "MenuDLUMAMods.xui"
CAMERA = ROOT / "PakAssets" / "Camera" / "menubackgroundcameraactions_menu_lvl_new.scr"
PLACEMENT = ROOT / "PakAssets" / "Camera" / "menuscreen3dplacement_menu_lvl_new.scr"
PACK = ROOT / "build" / "package" / "Release" / "dluma.pak"


def test_mods_page_no_longer_exposes_the_retired_lua_quest_log():
    text = MODS_XUI.read_text(encoding="utf-8")
    assert "B_DLUMAQuestLog" not in text
    assert "MenuDLUMAQuestLog" not in text
    assert "QUEST LOG" not in text


def test_loader_camera_merges_have_no_quest_log_route_or_placement():
    assert "MenuDLUMAQuestLog" not in CAMERA.read_text(encoding="utf-8")
    assert "MenuDLUMAQuestLog" not in PLACEMENT.read_text(encoding="utf-8")


def test_quest_log_is_not_packed():
    with zipfile.ZipFile(PACK) as archive:
        names = {name.replace("\\", "/") for name in archive.namelist()}

    assert "data/menu/scr/MenuDLUMAQuestLog.xui" not in names
    assert "data/menu/scr/hud.xui" not in names
    assert "data/menu/scr/MenuMain_PC_workshop.xui" not in names
    assert "data/menu/scr/MenuExtras.xui" in names
    assert "data/menu/scr/MenuDLUMALogs.xui" in names
