from pathlib import Path
import subprocess


ROOT = Path(__file__).resolve().parents[1]


def test_process_lifetime_source_registry_native_harness():
    subprocess.run(
        [
            "cmd.exe",
            "/d",
            "/s",
            "/c",
            "tools\\build_process_lifetime_source_registry_fake_tests.bat",
        ],
        cwd=ROOT,
        check=True,
    )
    subprocess.run(
        [
            str(
                ROOT
                / "build"
                / "process-lifetime-source-registry-fake"
                / "process_lifetime_source_registry_fake_harness.exe"
            )
        ],
        cwd=ROOT,
        check=True,
    )


def test_registry_is_fixed_capacity_and_mount_owner_populated():
    header = (ROOT / "src/dlw_process_lifetime_sources.h").read_text(
        encoding="utf-8"
    )
    pack = (ROOT / "src/dlw_pack.c").read_text(encoding="utf-8")
    companion = (ROOT / "src/dlw_companion_archive.c").read_text(
        encoding="utf-8"
    )
    project = (ROOT / "DLUMA.vcxproj").read_text(
        encoding="utf-8"
    )
    assert "DLUMA_PROCESS_LIFETIME_SOURCE_CAPACITY 32U" in header
    assert "DLUMA_SOURCE_OWNER_MOD_COMPANION_CONTENT" in header
    assert "DLUMA_SOURCE_OWNER_ID_CUSTOM_MAIN_MENU_BANK" in header
    assert "DLUMA_SOURCE_OWNER_ID_CUSTOM_MAIN_MENU_PROJECT" in header
    assert "DLUMA_SOURCE_OWNER_ID_CUSTOM_MAIN_MENU_DATA" in header
    assert "DLUMA_SOURCE_OWNER_ID_CUSTOM_MAIN_MENU_ASSETS_PC" in header
    assert "DLUMA_SOURCE_OWNER_ID_CUSTOM_MAIN_MENU_OVERLAY" in header
    assert "DlumaProcessLifetimeSourceRecord" in header
    assert "DlumaProcessLifetimeSourceGenerationToken" in header
    assert "exact_source_joined" in header
    assert "registry_generation_floor" in header
    assert "DLUMA_PROCESS_SOURCE_STATE_RETIRED" in header
    assert "dluma_process_lifetime_source_live_presence_in(" in header
    assert "dluma_process_lifetime_source_retire_in(" in header
    assert "dluma_process_lifetime_source_compare_replace_in(" in header
    assert "approval_fingerprint" in header
    assert "manifest_fingerprint" in header
    assert "dluma_pack_find_source_exact(" in pack
    assert "dluma_pack_register_directory_source(" in pack
    assert "dluma_pack_register_pack_source(" in pack
    assert "dluma_native_companion_register_source(" in companion
    assert "dluma_companion_archive_mount_materialized_bank(" in companion
    assert "dlw_process_lifetime_sources.c" in project
