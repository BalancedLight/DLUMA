import subprocess
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def test_editor_missing_asset_probe_guard_fake_harness():
    completed = subprocess.run(
        [
            "cmd.exe",
            "/d",
            "/s",
            "/c",
            "call tools\\build_editor_missing_asset_probe_guard_fake_tests.bat",
        ],
        cwd=ROOT,
        check=True,
        capture_output=True,
        text=True,
        timeout=90,
    )
    assert "Editor missing-asset probe guard fake cases passed: 6" in (
        completed.stdout
    )


def test_fix_is_exact_editor_only_and_fail_closed():
    source = (ROOT / "src/dlw_editor_missing_asset_probe_fix.c").read_text(
        encoding="utf-8"
    )
    host = (ROOT / "src/dlw_editor_host.c").read_text(encoding="utf-8")
    project = (ROOT / "DLUMA.vcxproj").read_text(encoding="utf-8")

    assert "dluma_dual_host_runtime_is_editor()" in source
    assert "0x623394CDUL" in source
    assert "0x00060000UL" in source
    assert "C833EBD635572EC9D8D773479FA0DD6E" in source
    assert "DLUMA_EDITOR_FS_CREATE_FILE_IAT_RVA 0x0003E060UL" in source
    assert "DLUMA_EDITOR_FS_GET_FILE_ATTRIBUTES_IAT_RVA 0x0003E098UL" in source
    assert "DLUMA_EDITOR_FS_FIND_FIRST_FILE_EX_IAT_RVA 0x0003E110UL" in source
    assert "DLUMA_EDITOR_FS_FIND_FIRST_FILE_IAT_RVA 0x0003E228UL" in source
    assert "creation_disposition == OPEN_EXISTING" in source
    assert "DLUMA_EDITOR_GENERIC_WRITE" in source
    assert "DLUMA_EDITOR_FILE_FLAG_DELETE_ON_CLOSE" in source
    assert "ERROR_FILE_NOT_FOUND" in source
    assert "stock filesystem behavior remains unchanged" in source
    assert "dluma_editor_missing_asset_probe_fix_start()" in host
    assert "src\\dlw_editor_missing_asset_probe_fix.c" in project
    assert "src\\core\\dlw_editor_missing_asset_probe_guard.c" in project

    for relative_path in (
        "src/fixes/FixRegistry.cpp",
        "src/dlw_ui.c",
        "src/dlw_localization.c",
        "src/dlw_lua_config.c",
    ):
        unrelated = (ROOT / relative_path).read_text(encoding="utf-8")
        assert "missing_asset_probe" not in unrelated
