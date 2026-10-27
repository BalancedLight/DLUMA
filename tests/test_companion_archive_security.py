from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
ARCHIVE = (ROOT / "src" / "dlw_companion_archive.c").read_text(
    encoding="utf-8"
)
ARCHIVE_HEADER = (ROOT / "src" / "dlw_companion_archive.h").read_text(
    encoding="utf-8"
)
HOST = (ROOT / "src" / "game" / "dlw_game_host_context.c").read_text(
    encoding="utf-8"
)
MEDIA = (ROOT / "src" / "game" / "dlw_media_service.c").read_text(
    encoding="utf-8"
)


def block(source: str, start: str, end: str) -> str:
    begin = source.index(start)
    return source[begin : source.index(end, begin)]


def test_archive_paths_reject_traversal_reparse_hardlinks_and_aliases():
    segment = block(
        ARCHIVE,
        "static int dluma_native_companion_segment_valid(",
        "static int dluma_native_companion_path_valid(",
    )
    for name in ("CON", "PRN", "AUX", "NUL", "CLOCK$", "CONIN$", "CONOUT$"):
        assert f'"{name}"' in segment
    assert "value[length - 1U] == '.'" in segment
    assert "value[length - 1U] == ' '" in segment
    path = block(
        ARCHIVE,
        "static int dluma_native_companion_path_valid(",
        "static int dluma_native_companion_ascii_basename(",
    )
    assert "i - segment_start" in path
    assert "length - segment_start" in path
    opened = block(
        ARCHIVE,
        "static int dluma_native_companion_open_regular(",
        "static int dluma_native_companion_verify_entry_bytes(",
    )
    assert "FILE_FLAG_OPEN_REPARSE_POINT" in opened
    assert "FILE_ATTRIBUTE_REPARSE_POINT" in opened
    assert "info.nNumberOfLinks != 1U" in opened
    assert "FILE_SHARE_READ" in opened
    assert "FILE_SHARE_WRITE" not in opened
    assert "FILE_SHARE_DELETE" not in opened


def test_zip_parser_is_bounded_crc_checked_and_case_collision_safe():
    validate = block(
        ARCHIVE,
        "static int dluma_native_companion_validate_zip(",
        "static int dluma_native_companion_parse_config_schema(",
    )
    for contract in (
        "DLUMA_NATIVE_ZIP_EOCD_SIGNATURE",
        "DLUMA_NATIVE_ZIP_CENTRAL_SIGNATURE",
        "DLUMA_NATIVE_ZIP_LOCAL_SIGNATURE",
        "count > DLUMA_COMPANION_ARCHIVE_MAX_ENTRIES",
        "compressed_size != uncompressed_size",
        "uncompressed_size > DLUMA_COMPANION_ARCHIVE_MAX_BYTES",
        "dluma_stricmp(entries[j].virtual_path, name) == 0",
        "dluma_native_companion_verify_entry_bytes(",
    ):
        assert contract in validate
    verifier = block(
        ARCHIVE,
        "static int dluma_native_companion_verify_entry_bytes(",
        "static int dluma_native_companion_csb_name_valid(",
    )
    assert "(crc ^ 0xffffffffUL) != entry->crc32" in verifier
    assert "dluma_sha256_update(&sha256, buffer, chunk)" in verifier


def test_approved_archive_identity_and_bytes_are_revalidated_and_retained():
    discover = block(
        ARCHIVE,
        "int dluma_companion_archive_discover_file(",
        "int dluma_companion_archive_lock(",
    )
    locked = block(
        ARCHIVE,
        "int dluma_companion_archive_lock(",
        "static int dluma_native_companion_module_identity(",
    )
    for contract in (
        "dluma_sha256_handle_hex(",
        "dluma_native_companion_identity_unchanged(",
        "volume_serial",
        "file_index_high",
        "file_index_low",
        "file_size_low",
    ):
        assert contract in discover
        assert contract in locked
    opened = block(
        HOST,
        "static DlumaStatus dluma_game_host_companion_open(",
        "static DlumaStatus dluma_game_host_companion_resolve(",
    )
    assert "DLUMA_STATUS_CHANGED_SINCE_APPROVAL" in opened
    assert "companion->archive" in opened
    read = block(
        ARCHIVE,
        "int dluma_companion_archive_read_entry(",
        "int dluma_companion_archive_manifest_bank_matches(",
    )
    assert "pack->file" in read
    assert "dluma_native_companion_identity_unchanged(" in read


def test_entry_reads_and_audio_materialization_enforce_exact_bounds_and_hashes():
    read = block(
        HOST,
        "static DlumaStatus dluma_game_host_companion_entry_read(",
        "DlumaStatus dluma_game_host_context_initialize(",
    )
    assert "DLUMA_GAME_HOST_COMPANION_ENTRY_LIMIT" in read
    assert "DLUMA_STATUS_LIMIT_EXCEEDED" in read
    assert "DLUMA_STATUS_CHANGED_SINCE_APPROVAL" in read
    assert "#define DLUMA_GAME_HOST_COMPANION_ENTRY_LIMIT (1024U * 1024U)" in HOST
    writer = block(
        ARCHIVE,
        "static int dluma_native_companion_write_bank_temp(",
        "static int dluma_companion_archive_locked_path_revalidates(",
    )
    assert "DLUMA_NATIVE_CREATE_NEW" in writer
    assert "dluma_sha256_update(&hash, buffer, chunk)" in writer
    assert "sha256, pack->audio_manifest.bank_sha256" in writer
    assert "FlushFileBuffers(output)" in writer
    cached = block(
        ARCHIVE,
        "static int dluma_native_companion_open_cached_bank(",
        "static int dluma_native_companion_write_bank_temp(",
    )
    assert "dluma_sha256_handle_hex(" in cached
    assert "dluma_stricmp(sha256, expected_sha256) != 0" in cached


def test_multi_bank_and_multi_owner_registries_are_fixed_and_isolated():
    assert "#define DLUMA_COMPANION_ARCHIVE_MAX_BANKS 16U" in ARCHIVE_HEADER
    assert "materialized_banks[" in ARCHIVE_HEADER
    assert "#define DLUMA_MEDIA_MAX_BANKS 16U" in MEDIA
    assert "DLUMA_SOURCE_OWNER_MOD_COMPANION_CONTENT" in ARCHIVE
    assert "existing->owner_id == owner->owner_id" in MEDIA
    assert "existing->owner_generation == owner->generation" in MEDIA
    assert "DLUMA_STATUS_FOREIGN_HANDLE" in MEDIA
    assert "dluma_media_service_revoke_owner(" in MEDIA
    assert "(!owner_generation ||" in MEDIA
