#ifndef DLUMA_COMPANION_ARCHIVE_H
#define DLUMA_COMPANION_ARCHIVE_H

#include "dlw_common.h"
#include "dlw_lua_config.h"
#include "dlw_process_lifetime_sources.h"

#define DLUMA_COMPANION_ARCHIVE_MAX_BYTES (512ULL * 1024ULL * 1024ULL)
#define DLUMA_COMPANION_ARCHIVE_MAX_ENTRIES 64U
#define DLUMA_COMPANION_ARCHIVE_MAX_JSON_BYTES (64U * 1024U)
#define DLUMA_COMPANION_ARCHIVE_MAX_TOTAL_JSON_BYTES (1024U * 1024U)
#define DLUMA_COMPANION_ARCHIVE_MAX_NAME 260U
#define DLUMA_COMPANION_ARCHIVE_MAX_BANKS 16U
#define DLUMA_COMPANION_ARCHIVE_MAX_CUES 64U
#define DLUMA_COMPANION_ARCHIVE_MAX_CUE_NAME 128U
#define DLUMA_COMPANION_ARCHIVE_MAX_ID 64U
#define DLUMA_COMPANION_ARCHIVE_MAX_ENGINE_BANK_NAME 64U

typedef struct DlumaCompanionArchiveSnapshot {
    WCHAR final_path[DLUMA_MAX_WPATH];
    char filename[128];
    char manifest_path[DLUMA_COMPANION_ARCHIVE_MAX_NAME];
    char config_manifest_path[DLUMA_COMPANION_ARCHIVE_MAX_NAME];
    char sha256[DLUMA_SHA256_HEX_CAPACITY];
    char config_schema_fingerprint[DLUMA_SHA256_HEX_CAPACITY];
    char error[160];
    char config_error[160];
    DWORD volume_serial;
    DWORD file_index_high;
    DWORD file_index_low;
    DWORD file_size_high;
    DWORD file_size_low;
    unsigned char declared;
    unsigned char valid;
    unsigned char config_declared;
    unsigned char config_available;
    unsigned short config_category_count;
    unsigned short config_setting_count;
} DlumaCompanionArchiveSnapshot;

typedef struct DlumaCompanionArchiveEntry {
    char virtual_path[DLUMA_COMPANION_ARCHIVE_MAX_NAME];
    DWORD local_header_offset;
    DWORD data_offset;
    DWORD size;
    DWORD crc32;
    unsigned char is_json;
    unsigned char is_csb;
    unsigned char reserved[2];
} DlumaCompanionArchiveEntry;

typedef struct DlumaCompanionArchiveCue {
    char name[DLUMA_COMPANION_ARCHIVE_MAX_CUE_NAME];
    DWORD duration_ms;
} DlumaCompanionArchiveCue;

typedef struct DlumaCompanionArchiveAudioManifest {
    char bank_identifier[DLUMA_COMPANION_ARCHIVE_MAX_ID];
    char bank_name[DLUMA_COMPANION_ARCHIVE_MAX_ID];
    char route[32];
    char bank_path[DLUMA_COMPANION_ARCHIVE_MAX_NAME];
    char bank_sha256[DLUMA_SHA256_HEX_CAPACITY];
    DlumaCompanionArchiveCue cues[DLUMA_COMPANION_ARCHIVE_MAX_CUES];
    DWORD bank_size;
    unsigned int cue_count;
    unsigned char valid;
    unsigned char reserved[3];
} DlumaCompanionArchiveAudioManifest;

typedef struct DlumaCompanionArchiveAudioCatalog {
    char schema[64];
    DlumaCompanionArchiveAudioManifest banks[DLUMA_COMPANION_ARCHIVE_MAX_BANKS];
    unsigned int bank_count;
    unsigned char valid;
    unsigned char reserved[3];
} DlumaCompanionArchiveAudioCatalog;

typedef struct DlumaCompanionArchiveMaterializedBank {
    HANDLE runtime_dir;
    HANDLE cache_dir;
    HANDLE bank_dir;
    HANDLE bank_file;
    BY_HANDLE_FILE_INFORMATION bank_identity;
    BY_HANDLE_FILE_INFORMATION directory_identity;
    WCHAR bank_dir_path[DLUMA_MAX_WPATH];
    WCHAR bank_path[DLUMA_MAX_WPATH];
    char bank_dir_utf8[DLUMA_MAX_WPATH * 2];
    char bank_name[DLUMA_COMPANION_ARCHIVE_MAX_ENGINE_BANK_NAME];
    void* source_object;
    ULONGLONG source_mount_generation;
    unsigned int catalog_index;
    unsigned char active;
    unsigned char source_registered;
    unsigned char reserved[2];
} DlumaCompanionArchiveMaterializedBank;

typedef struct DlumaCompanionArchive {
    HANDLE file;
    BY_HANDLE_FILE_INFORMATION identity;
    WCHAR final_path[DLUMA_MAX_WPATH];
    char final_path_utf8[DLUMA_MAX_WPATH * 2];
    char manifest_path[DLUMA_COMPANION_ARCHIVE_MAX_NAME];
    char config_manifest_path[DLUMA_COMPANION_ARCHIVE_MAX_NAME];
    char sha256[DLUMA_SHA256_HEX_CAPACITY];
    char config_schema_fingerprint[DLUMA_SHA256_HEX_CAPACITY];
    char source_owner_id[DLUMA_PROCESS_SOURCE_OWNER_ID_CAPACITY];
    char source_approval_fingerprint[
        DLUMA_PROCESS_SOURCE_FINGERPRINT_CAPACITY];
    char source_manifest_fingerprint[
        DLUMA_PROCESS_SOURCE_FINGERPRINT_CAPACITY];
    DlumaCompanionArchiveEntry entries[DLUMA_COMPANION_ARCHIVE_MAX_ENTRIES];
    DlumaCompanionArchiveAudioManifest audio_manifest;
    DlumaCompanionArchiveAudioCatalog audio_catalog;
    DlumaCompanionArchiveMaterializedBank materialized_banks[
        DLUMA_COMPANION_ARCHIVE_MAX_BANKS];
    unsigned int selected_materialized_bank_index;
    void* source_object;
    ULONGLONG source_mount_generation;
    unsigned int entry_count;
    unsigned char mounted;
    unsigned char validated;
    unsigned char source_registered;
    unsigned char vfs_diagnostic;
    unsigned char bank_diagnostic;
} DlumaCompanionArchive;
#ifdef __cplusplus
extern "C" {
#endif

int dluma_companion_archive_discover_file(
    const WCHAR* final_path,
    const char* content_manifest_path,
    DlumaCompanionArchiveSnapshot* snapshot);
int dluma_companion_archive_lock(
    const DlumaCompanionArchiveSnapshot* snapshot,
    DlumaCompanionArchive* pack);
int dluma_companion_archive_set_source_owner(
    DlumaCompanionArchive* pack,
    const char* owner_id,
    const char* approval_fingerprint,
    const char* manifest_fingerprint);
int dluma_companion_archive_mount(DlumaCompanionArchive* pack);
int dluma_companion_archive_ensure_vfs_entry(
    DlumaCompanionArchive* pack, const char* virtual_path);
int dluma_companion_archive_prepare_bank(
    DlumaCompanionArchive* pack, const char* virtual_path,
    char* engine_bank_name_out, unsigned int engine_bank_name_capacity);
int dluma_companion_archive_materialize_locked_bank(
    DlumaCompanionArchiveMaterializedBank* materialized,
    HANDLE source_file,
    const BY_HANDLE_FILE_INFORMATION* source_identity,
    DWORD source_size,
    const char* source_sha256);
int dluma_companion_archive_catalog_locked_bank(
    HANDLE source_file,
    const BY_HANDLE_FILE_INFORMATION* source_identity,
    DWORD source_size,
    DlumaCompanionArchiveAudioManifest* catalog_out);
int dluma_companion_archive_mount_materialized_bank(
    DlumaCompanionArchiveMaterializedBank* materialized,
    DlumaProcessLifetimeSourceOwnerKind owner_kind,
    const char* owner_id,
    const char* content_sha256,
    const char* approval_fingerprint,
    const char* manifest_fingerprint,
    char* engine_bank_name_out,
    unsigned int engine_bank_name_capacity);
void dluma_companion_archive_close(DlumaCompanionArchive* pack);
int dluma_companion_archive_read_entry(
    DlumaCompanionArchive* pack, const char* virtual_path,
    void* bytes_out, unsigned int bytes_capacity,
    unsigned int* required_size_out);
int dluma_companion_archive_has_entry(
    const DlumaCompanionArchive* pack, const char* virtual_path);
int dluma_companion_archive_manifest_bank_matches(
    const DlumaCompanionArchive* pack, const char* bank_identifier);
int dluma_companion_archive_manifest_has_cue(
    const DlumaCompanionArchive* pack, const char* bank_identifier,
    const char* cue_name,
    DWORD* duration_ms_out);
const DlumaCompanionArchiveAudioManifest* dluma_companion_archive_find_bank(
    const DlumaCompanionArchive* pack, const char* bank_identifier);

#ifdef __cplusplus
}
#endif
#endif
