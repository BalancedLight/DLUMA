from __future__ import annotations

import subprocess
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def _function_body(source: str, signature: str) -> str:
    start = source.index(signature)
    brace = source.index("{", start)
    depth = 0
    for offset in range(brace, len(source)):
        if source[offset] == "{":
            depth += 1
        elif source[offset] == "}":
            depth -= 1
            if depth == 0:
                return source[start : offset + 1]
    raise AssertionError(f"unterminated function: {signature}")


def test_native_companion_source_owner_harness() -> None:
    completed = subprocess.run(
        [
            "cmd.exe",
            "/d",
            "/s",
            "/c",
            r"tools\build_native_companion_source_owner_fake_tests.bat",
        ],
        cwd=ROOT,
        check=False,
        capture_output=True,
        text=True,
    )
    assert completed.returncode == 0, (
        completed.stdout + "\n" + completed.stderr
    )


def test_production_registers_common_companion_sources_for_all_mod_hosts() -> None:
    archive = (ROOT / "src/dlw_companion_archive.c").read_text(
        encoding="utf-8"
    )
    host = (ROOT / "src/game/dlw_game_host_context.c").read_text(
        encoding="utf-8"
    )
    project = (ROOT / "DLUMA.vcxproj").read_text(
        encoding="utf-8"
    )
    assert "DLUMA_SOURCE_OWNER_MOD_COMPANION_CONTENT" in archive
    assert "dluma_companion_archive_register_bank_source(" in archive
    assert "record.content_sha256" in archive
    assert "record.approval_fingerprint" in archive
    assert "record.manifest_fingerprint" in archive
    assert "dluma_companion_archive_set_source_owner(" in host
    assert "artifact.sha256, artifact.sha256" in host
    assert "dlw_companion_archive.c" in project


def test_companion_mount_reuses_an_existing_exact_vfs_source() -> None:
    archive = (ROOT / "src/dlw_companion_archive.c").read_text(
        encoding="utf-8"
    )
    mount = _function_body(
        archive, "int dluma_companion_archive_mount(DlumaCompanionArchive* pack)"
    )
    first_lookup = mount.index("source = find_source(pack->final_path_utf8);")
    missing_guard = mount.index("if (!source)", first_lookup)
    add_source = mount.index(
        "add_source(pack->final_path_utf8, 9U)", missing_guard
    )
    second_lookup = mount.index(
        "source = find_source(pack->final_path_utf8);", first_lookup + 1
    )
    register = mount.index(
        "dluma_native_companion_register_source(pack, source)"
    )
    assert first_lookup < missing_guard < add_source < second_lookup < register
    assert mount.count("add_source(pack->final_path_utf8, 9U)") == 1


def test_owner_validation_is_reason_coded_and_retry_bounded() -> None:
    owner = (ROOT / "src/dlw_native_companion_source_owner.c").read_text(
        encoding="utf-8"
    )
    startup = (
        ROOT
        / "src/fixes/custom_map_asset_isolation/"
        "StartupBaselineCaptureState.cpp"
    ).read_text(encoding="utf-8")
    for reason in (
        "module_id_mismatch",
        "module_not_enabled",
        "native_registry_item_not_published",
        "approval_fingerprint_mismatch",
        "manifest_fingerprint_mismatch",
        "module_hash_mismatch",
        "companion_hash_mismatch",
        "requested_pack_path_mismatch",
        "source_pointer_mismatch",
        "source_type_mismatch",
        "mount_generation_mismatch",
        "native_registry_revision_mismatch",
        "source_record_not_committed",
        "owner_snapshot_not_published",
    ):
        assert reason in owner
    assert "RecordStartupB0SourceOwnerFailure" in startup
    assert "SourceOwnerPublicationPending" in startup
    assert "PermanentSourceOwnerFailure" in startup
