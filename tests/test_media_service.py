from __future__ import annotations

import subprocess
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
MEDIA = (ROOT / "src/game/dlw_media_service.c").read_text(encoding="utf-8")
ARCHIVE = (ROOT / "src/dlw_companion_archive.c").read_text(encoding="utf-8")


def test_media_service_fake_harness() -> None:
    completed = subprocess.run(
        ["cmd.exe", "/d", "/s", "/c",
         "call tools\\build_media_service_fake_tests.bat"],
        cwd=ROOT,
        check=True,
        capture_output=True,
        text=True,
        timeout=90,
    )
    assert completed.stdout.count("[PASS]") == 4
    assert "media service fake cases passed: 4" in completed.stdout


def test_media_registry_is_mod_neutral_bounded_and_owner_scoped() -> None:
    for bound in (
        "DLUMA_MEDIA_MAX_BANKS 16U",
        "DLUMA_MEDIA_MAX_WAVES 128U",
        "DLUMA_MEDIA_MAX_WAVES_PER_OWNER 16U",
        "DLUMA_MEDIA_MAX_POSITION_MS 86400000ULL",
    ):
        assert bound in MEDIA
    for field in ("owner_id", "owner_generation", "generation", "adapter_token"):
        assert field in MEDIA
    assert "drivinglight" not in MEDIA.casefold()


def test_filter_operations_are_explicitly_gated() -> None:
    cue = MEDIA.split("static DlumaStatus dluma_media_cue_filter", 1)[1].split(
        "static DlumaStatus dluma_media_cue_stop", 1
    )[0]
    music = MEDIA.split("static DlumaStatus dluma_media_music_filter", 1)[1].split(
        "static DlumaStatus dluma_media_music_release", 1
    )[0]
    for block in (cue, music):
        assert "dluma_media_filter_values(" in block
        assert "DLUMA_STATUS_UNAVAILABLE" in block
    assert "DLUMA_CAPABILITY_AUDIO_WAVE_FILTER" in MEDIA
    assert "DLUMA_CAPABILITY_AUDIO_MUSIC_FILTER" in MEDIA


def test_vehicle_success_requires_adapter_snapshot_availability() -> None:
    block = MEDIA.split("static DlumaStatus dluma_media_vehicle_state", 1)[1].split(
        "static DlumaStatus dluma_media_resolve_bank", 1
    )[0]
    assert "if (!state.available) return DLUMA_STATUS_UNAVAILABLE;" in block


def test_companion_archive_keeps_approved_handle_and_generic_audio_schema() -> None:
    assert '"dluma.companion.audio.v1"' in ARCHIVE
    assert "DLUMA_COMPANION_ARCHIVE_MAX_BYTES" in ARCHIVE
    assert "DLUMA_COMPANION_ARCHIVE_MAX_BANKS" in ARCHIVE
    assert "DLUMA_COMPANION_ARCHIVE_MAX_CUES" in ARCHIVE
    assert "FILE_SHARE_READ" in ARCHIVE
    assert "FILE_SHARE_WRITE" not in ARCHIVE.split(
        "static int dluma_native_companion_open_regular", 1
    )[1].split("static int", 1)[0]
    assert "dluma_sha256_handle_hex(" in ARCHIVE
    assert "data/drivinglight" not in ARCHIVE


def test_bank_open_materializes_only_the_manifest_approved_alias() -> None:
    service = MEDIA.split("static DlumaStatus dluma_media_bank_open", 1)[1].split(
        "static DlumaStatus dluma_media_cue_duration", 1
    )[0]
    adapter = (ROOT / "src/dlw_player_media_adapter.c").read_text(
        encoding="utf-8"
    ).split("int dluma_player_media_adapter_open_bank", 1)[1].split(
        "int dluma_player_media_adapter_probe_duration", 1
    )[0]
    prepare = ARCHIVE.split("int dluma_companion_archive_prepare_bank", 1)[1].split(
        "int dluma_companion_archive_lock", 1
    )[0]
    materialize = ARCHIVE.split(
        "static int dluma_native_companion_materialize_bank", 1
    )[1].split("int dluma_companion_archive_prepare_bank", 1)[0]

    assert "dluma_companion_archive_find_bank" in service
    assert "dluma_media_bank_entry" in service
    assert "dluma_companion_archive_mount" not in service
    assert "!pack->mounted" not in adapter
    assert "dluma_companion_archive_prepare_bank" in adapter
    assert "dluma_companion_archive_ensure_vfs_entry" not in prepare
    assert "!pack->mounted" not in materialize
    for required in (
        "bank_sha256",
        "dluma_native_companion_write_bank_temp",
        "dluma_native_companion_identity_unchanged",
        "DLUMA_SOURCE_OWNER_MOD_COMPANION_CONTENT",
    ):
        assert required in ARCHIVE
