from pathlib import Path
import re
import subprocess


ROOT = Path(__file__).resolve().parents[1]
CUSTOM = ROOT / "src" / "fixes" / "custom_map_asset_isolation"
WINDOWS_VIEW_HEADER = CUSTOM / "FilesystemSourceWindowsView.h"
WINDOWS_VIEW_SOURCE = CUSTOM / "FilesystemSourceWindowsView.cpp"


def test_native_filesystem_source_inventory_harness():
    subprocess.run(
        [
            "cmd.exe",
            "/d",
            "/s",
            "/c",
            "tools\\build_filesystem_source_inventory_fake_tests.bat",
        ],
        cwd=ROOT,
        check=True,
    )
    subprocess.run(
        [
            str(
                ROOT
                / "build"
                / "filesystem-source-inventory-fake"
                / "filesystem_source_inventory_fake_harness.exe"
            )
        ],
        cwd=ROOT,
        check=True,
    )


def test_inventory_is_bounded_pod_and_requires_synchronization_proof():
    header = (CUSTOM / "FilesystemSourceInventory.h").read_text(
        encoding="utf-8"
    )
    source = (CUSTOM / "FilesystemSourceInventory.cpp").read_text(
        encoding="utf-8"
    )
    assert "kFilesystemSourceInventoryCapacity = 192" in header
    assert "std::vector" not in header + source
    assert "std::string" not in header + source
    assert "synchronization_held" in header
    assert "layout_validated" in header
    assert "SynchronizationNotHeld" in source
    assert "LayoutNotValidated" in source


def test_inventory_has_no_filesystem_mutation_or_logging_calls():
    source = (CUSTOM / "FilesystemSourceInventory.cpp").read_text(
        encoding="utf-8"
    )
    forbidden = (
        "rem_source(",
        "refresh(",
        "add_source(",
        "FreePack(",
        "ReleasePack(",
        "OnDemandEvictAllFromPack(",
        "dluma_log_",
        "CreateFile",
        "WriteFile",
    )
    for call in forbidden:
        assert call not in source


def test_inventory_walk_uses_validated_link_minus_eight_contract():
    header = (CUSTOM / "FilesystemSourceInventory.h").read_text(
        encoding="utf-8"
    )
    source = (CUSTOM / "FilesystemSourceInventory.cpp").read_text(
        encoding="utf-8"
    )
    assert "kFilesystemSourceLinkOffset = 8" in header
    assert re.search(
        r"current\s*-\s*kFilesystemSourceLinkOffset", source
    )
    assert "PreviousLinkMismatch" in source
    assert "DuplicateOrCyclicLink" in source
    assert "FallbackMismatch" in source
    assert "CountMismatch" in source


def test_inventory_files_are_registered_in_project():
    project = (ROOT / "DLUMA.vcxproj").read_text(
        encoding="utf-8"
    )
    assert "FilesystemSourceInventory.cpp" in project
    assert "FilesystemSourceInventory.h" in project
    assert "FilesystemSourceWindowsView.cpp" in project
    assert "FilesystemSourceWindowsView.h" in project


def test_windows_view_has_exact_rvas_shared_lock_and_generation_fence():
    header = WINDOWS_VIEW_HEADER.read_text(encoding="utf-8")
    source = WINDOWS_VIEW_SOURCE.read_text(encoding="utf-8")
    expected_rvas = {
        "kFilesystemSourceHeadRva": "0x00053398U",
        "kFilesystemSourceTailRva": "0x000533A0U",
        "kFilesystemSourceCountRva": "0x000533A8U",
        "kFilesystemFallbackLinkRva": "0x00053AC8U",
        "kFilesystemSourcesSrwLockRva": "0x00053AD0U",
        "kFilesystemAddSourcePathRva": "0x00012DD0U",
        "kFilesystemAddSourceObjectRva": "0x000137B0U",
        "kFilesystemRemoveSourcePathRva": "0x00013BA0U",
        "kFilesystemRemoveSourceObjectRva": "0x00014220U",
    }
    for name, value in expected_rvas.items():
        assert f"{name} = {value}" in header
    assert "AcquireSRWLockShared(lock)" in source
    assert "ReleaseSRWLockShared(lock)" in source
    assert source.index("AcquireSRWLockShared(lock)") < source.index(
        "CaptureFilesystemSourceInventory("
    ) < source.index("ReleaseSRWLockShared(lock)")
    assert "filesystem_registry_generation_before" in source
    assert "filesystem_registry_generation_after" in source
    assert "RegistryChangedDuringCapture" in source
    assert "supported_pe_identity_validated" in source
    assert "registry_layout_validated" in source
    assert "source_path_reader_validated" in source


def test_fallback_model_is_explicit_and_null_known_absent_is_not_unavailable():
    header = (CUSTOM / "FilesystemSourceInventory.h").read_text(
        encoding="utf-8"
    )
    source = (CUSTOM / "FilesystemSourceInventory.cpp").read_text(
        encoding="utf-8"
    )
    assert "fallback_state_known" in header
    assert "fallback_installed" in header
    assert "FallbackStateUnknown" in header
    registry_unavailable = source[
        source.index("bool CaptureFilesystemSourceInventory(") :
        source.index("bool ExactFilesystemSourceInventoryIdentityMatches(")
    ]
    assert "!registry->fallback_link" not in registry_unavailable
    assert "registry->fallback_installed !=" in registry_unavailable
    assert "registry->fallback_installed &&" in registry_unavailable


def test_production_report_contains_complete_registry_view_diagnostics():
    production = (CUSTOM / "CustomMapAssetIsolationFix.cpp").read_text(
        encoding="utf-8"
    )
    for key in (
        "filesystem_registry_view_available",
        "filesystem_registry_head",
        "filesystem_registry_tail",
        "filesystem_registry_count",
        "filesystem_registry_fallback_known",
        "filesystem_registry_fallback_installed",
        "filesystem_registry_fallback_link",
        "filesystem_registry_generation_before",
        "filesystem_registry_generation_after",
        "filesystem_registry_lock_acquired",
        "filesystem_registry_inventory_result",
    ):
        assert f'"\\r\\n{key}="' in production
    assert "ExactFilesystemSourceInventoryIdentityMatches(" in production
    assert "ReturnedFrontendFilesystemMatchesStartupB0" in production
    assert "ReturnedFrontendLevelIdentityIsMenu" in production