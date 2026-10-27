from pathlib import Path
import subprocess


ROOT = Path(__file__).resolve().parents[1]
CUSTOM = ROOT / "src" / "fixes" / "custom_map_asset_isolation"


def test_native_55_source_classifier_and_exact_loader_join():
    subprocess.run(
        [
            "cmd.exe",
            "/d",
            "/s",
            "/c",
            "tools\\build_startup_filesystem_source_classifier_fake_tests.bat",
        ],
        cwd=ROOT,
        check=True,
    )
    subprocess.run(
        [
            str(
                ROOT
                / "build"
                / "startup-filesystem-source-classifier-fake"
                / "startup_filesystem_source_classifier_fake_harness.exe"
            )
        ],
        cwd=ROOT,
        check=True,
    )


def test_classifier_is_bounded_pure_and_registered_in_production():
    header = (CUSTOM / "StartupFilesystemSourceClassifier.h").read_text(
        encoding="utf-8"
    )
    source = (CUSTOM / "StartupFilesystemSourceClassifier.cpp").read_text(
        encoding="utf-8"
    )
    project = (ROOT / "DLUMA.vcxproj").read_text(
        encoding="utf-8"
    )
    production = (CUSTOM / "CustomMapAssetIsolationFix.cpp").read_text(
        encoding="utf-8"
    )
    assert "kFilesystemSourceInventoryCapacity" in header
    assert "StartupSourceClassificationRow" in header
    assert "ProcessLifetimeInventoryBindingMismatch" in header
    assert "ProcessLifetimeOwnerValidationFailed" in header
    assert "LoaderSourceTypeMismatch" in header
    assert "std::vector" not in header + source
    assert "std::string" not in header + source
    assert "StartupFilesystemSourceClassifier.cpp" in project
    assert "StartupFilesystemSourceClassifier.h" in project
    assert "ClassifyStartupFilesystemSources(" in production
    assert "dluma_process_lifetime_source_bind_inventory(" in production
    assert "dluma_content_pack_bind_source_identity(" in production


def test_frontend_report_has_one_reason_coded_row_per_observed_source():
    production = (CUSTOM / "CustomMapAssetIsolationFix.cpp").read_text(
        encoding="utf-8"
    )
    for field in (
        "startup_source_classification_accepted=",
        "startup_source_classification_rejected=",
        "startup_loader_source_match_count=",
        "startup_source list_ordinal=",
        " source=",
        " link=",
        " path=",
        " raw_path_length=",
        " raw_path_hex=",
        " flags_known=",
        " type=",
        " proposed_kind=",
        " classification_result=",
        " rejection_reason=",
        " owner_validation_result=",
        " owner_validation_retryable=",
        " identity=",
    ):
        assert field in production
    assert "startup_b0_source_classification_rejected" in production
    assert "startup_b0_source_rejected_list_ordinal" in production
    assert "startup_b0_source_rejected_path" in production
    assert "startup_b0_source_rejected_raw_length" in production
    assert "startup_b0_source_rejected_raw_hex" in production
    assert "startup_b0_source_owner_validation_result" in production
    assert "startup_owner_registered_module_sha256" in production
    assert "startup_owner_observed_module_sha256" in production
    assert "process_lifetime_source_registry_available=" in production


def test_loader_mount_record_carries_stable_metadata_and_content_hash():
    common = (ROOT / "src" / "dlw_common.h").read_text(encoding="utf-8")
    pack = (ROOT / "src" / "dlw_pack.c").read_text(encoding="utf-8")
    for field in (
        "content_sha256",
        "file_size",
        "mount_generation",
        "volume_serial",
        "file_index_high",
        "file_index_low",
        "exact_source",
        "exact_link",
        "source_identity",
    ):
        assert field in common
    assert "dluma_sha256_handle_hex(" in pack
    assert "g_dluma_pack_identity_handle" in pack
    assert "FILE_SHARE_READ," in pack
    assert "dluma_content_pack_mount_snapshot(" in pack
    assert "dluma_content_pack_bind_source_identity(" in pack
    assert "exact_source_bound" in pack
