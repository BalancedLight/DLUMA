#ifndef DLUMA_PROCESS_LIFETIME_SOURCES_H
#define DLUMA_PROCESS_LIFETIME_SOURCES_H

#include "dlw_win32.h"

#define DLUMA_PROCESS_SOURCE_OWNER_ID_CAPACITY 96U
#define DLUMA_PROCESS_SOURCE_PATH_CAPACITY 2048U
#define DLUMA_PROCESS_SOURCE_FINGERPRINT_CAPACITY 65U
#define DLUMA_PROCESS_LIFETIME_SOURCE_CAPACITY 32U
#define DLUMA_NATIVE_ADMISSION_MODULE_ID_CAPACITY 128U
#define DLUMA_NATIVE_ADMISSION_MODULE_NAME_CAPACITY 128U
#define DLUMA_NATIVE_ADMISSION_CANDIDATE_ID_CAPACITY 65U
#define DLUMA_NATIVE_ADMISSION_PATH_CAPACITY 2048U
#define DLUMA_SOURCE_OWNER_ID_LOADER_PAK "dluma.loader.pak"
#define DLUMA_SOURCE_OWNER_ID_DEVELOPER_MODE \
    "dluma.loader.developer_mode"
#define DLUMA_SOURCE_OWNER_ID_THUMBNAILS "dluma.loader.thumbnails"
#define DLUMA_SOURCE_OWNER_ID_CUSTOM_MAIN_MENU_PROJECT \
    "dluma.loader.custom_main_menu.project"
#define DLUMA_SOURCE_OWNER_ID_CUSTOM_MAIN_MENU_DATA \
    "dluma.loader.custom_main_menu.project.data"
#define DLUMA_SOURCE_OWNER_ID_CUSTOM_MAIN_MENU_ASSETS_PC \
    "dluma.loader.custom_main_menu.project.assets_pc"
#define DLUMA_SOURCE_OWNER_ID_CUSTOM_MAIN_MENU_OVERLAY \
    "dluma.loader.custom_main_menu.overlay"
#define DLUMA_SOURCE_OWNER_ID_CUSTOM_MAIN_MENU_BANK \
    "dluma.loader.custom_main_menu.bank"

typedef enum DlumaProcessLifetimeSourceOwnerKind {
    DLUMA_SOURCE_OWNER_NONE = 0,
    DLUMA_SOURCE_OWNER_LOADER_PAK,
    DLUMA_SOURCE_OWNER_LOADER_DEVELOPER_MODE,
    DLUMA_SOURCE_OWNER_LOADER_THUMBNAILS,
    DLUMA_SOURCE_OWNER_NATIVE_COMPANION_PACK,
    DLUMA_SOURCE_OWNER_MOD_COMPANION_CONTENT,
    DLUMA_SOURCE_OWNER_CUSTOM_MAIN_MENU,
    DLUMA_SOURCE_OWNER_ASSET_MOD
} DlumaProcessLifetimeSourceOwnerKind;

typedef enum DlumaProcessLifetimeSourceRuntimeType {
    DLUMA_PROCESS_SOURCE_RUNTIME_UNKNOWN = 0,
    DLUMA_PROCESS_SOURCE_RUNTIME_DIRECTORY,
    DLUMA_PROCESS_SOURCE_RUNTIME_PACK
} DlumaProcessLifetimeSourceRuntimeType;

typedef enum DlumaProcessLifetimeSourceState {
    DLUMA_PROCESS_SOURCE_STATE_EMPTY = 0,
    DLUMA_PROCESS_SOURCE_STATE_REGISTERED,
    DLUMA_PROCESS_SOURCE_STATE_LIVE,
    DLUMA_PROCESS_SOURCE_STATE_RETIRED
} DlumaProcessLifetimeSourceState;

/*
 * An exact source generation is identified by both the native source pointer
 * and the inventory generation at which that pointer was observed.  The
 * mount generation distinguishes a later owner generation even when the
 * engine reuses the same address (ABA).
 */
typedef struct DlumaProcessLifetimeSourceGenerationToken {
    DlumaProcessLifetimeSourceOwnerKind owner_kind;
    char owner_id[DLUMA_PROCESS_SOURCE_OWNER_ID_CAPACITY];
    void* source_object;
    ULONGLONG mount_generation;
    ULONGLONG registry_generation;
    ULONGLONG retirement_registry_generation;
} DlumaProcessLifetimeSourceGenerationToken;

/*
 * One companion-pack mount owns one immutable admission identity.  The
 * process-lifetime source ledger and the loaded native registry entry retain
 * copies with the same nonzero admission_generation.  Mutable UI ordering,
 * selection, discovery revision, and Workshop parent directories are not
 * ownership inputs.
 */
typedef struct DlumaNativeAdmissionIdentity {
    char module_id[DLUMA_NATIVE_ADMISSION_MODULE_ID_CAPACITY];
    char module_name[DLUMA_NATIVE_ADMISSION_MODULE_NAME_CAPACITY];
    char stable_candidate_id[
        DLUMA_NATIVE_ADMISSION_CANDIDATE_ID_CAPACITY];
    char module_path[DLUMA_NATIVE_ADMISSION_PATH_CAPACITY];
    char module_sha256[DLUMA_PROCESS_SOURCE_FINGERPRINT_CAPACITY];
    char approval_fingerprint[
        DLUMA_PROCESS_SOURCE_FINGERPRINT_CAPACITY];
    char manifest_fingerprint[
        DLUMA_PROCESS_SOURCE_FINGERPRINT_CAPACITY];
    char companion_path[DLUMA_NATIVE_ADMISSION_PATH_CAPACITY];
    char companion_sha256[
        DLUMA_PROCESS_SOURCE_FINGERPRINT_CAPACITY];

    void* source_object;
    unsigned int source_runtime_type;
    unsigned int source_flags;
    ULONGLONG mount_generation;
    ULONGLONG native_registry_publication_generation;
    ULONGLONG admission_generation;

    ULONGLONG module_volume_serial;
    ULONGLONG module_file_index_high;
    ULONGLONG module_file_index_low;
    ULONGLONG module_file_size;
    ULONGLONG companion_volume_serial;
    ULONGLONG companion_file_index_high;
    ULONGLONG companion_file_index_low;
    ULONGLONG companion_file_size;

    unsigned int enabled_for_this_launch;
    unsigned int source_record_committed;
    unsigned int owner_snapshot_published;
    unsigned int reserved;
} DlumaNativeAdmissionIdentity;

typedef struct DlumaProcessLifetimeSourceRecord {
    DlumaProcessLifetimeSourceOwnerKind owner_kind;
    char owner_id[DLUMA_PROCESS_SOURCE_OWNER_ID_CAPACITY];

    void* source_object;
    void* intrusive_link;
    unsigned int list_ordinal;

    char requested_mount_path[DLUMA_PROCESS_SOURCE_PATH_CAPACITY];
    char observed_source_path[DLUMA_PROCESS_SOURCE_PATH_CAPACITY];

    unsigned int runtime_source_type;
    unsigned int source_flags;
    ULONGLONG registry_generation;
    ULONGLONG mount_generation;

    ULONGLONG volume_serial;
    ULONGLONG file_index_high;
    ULONGLONG file_index_low;
    ULONGLONG file_size;
    char content_sha256[DLUMA_PROCESS_SOURCE_FINGERPRINT_CAPACITY];

    /*
     * Native companion ownership is approval-bound in addition to being
     * content-bound. These fields are empty for loader-owned sources.
     */
    char approval_fingerprint[
        DLUMA_PROCESS_SOURCE_FINGERPRINT_CAPACITY];
    char manifest_fingerprint[
        DLUMA_PROCESS_SOURCE_FINGERPRINT_CAPACITY];
    DlumaNativeAdmissionIdentity native_admission;

    /*
     * Replacement generations must join an inventory generation strictly
     * newer than this floor.  It is zero for an owner's first generation and
     * records the observed removal generation after compare-and-replace.
     */
    ULONGLONG registry_generation_floor;
    ULONGLONG retirement_registry_generation;

    unsigned char metadata_complete;
    unsigned char exact_source_joined;
    unsigned char lifecycle_state;
    unsigned char reserved[5];
} DlumaProcessLifetimeSourceRecord;

typedef struct DlumaProcessLifetimeSourceRegistry {
    DlumaProcessLifetimeSourceRecord
        records[DLUMA_PROCESS_LIFETIME_SOURCE_CAPACITY];
    unsigned int count;
    ULONGLONG publication_generation;
    ULONGLONG next_mount_generation;
    volatile LONG lock;
} DlumaProcessLifetimeSourceRegistry;

typedef struct DlumaProcessLifetimeSourceSnapshot {
    DlumaProcessLifetimeSourceRecord
        records[DLUMA_PROCESS_LIFETIME_SOURCE_CAPACITY];
    unsigned int count;
    ULONGLONG publication_generation;
    unsigned char complete;
    unsigned char reserved[7];
} DlumaProcessLifetimeSourceSnapshot;

#ifdef __cplusplus
extern "C" {
#endif

void dluma_process_lifetime_source_registry_initialize(
    DlumaProcessLifetimeSourceRegistry* registry);
int dluma_process_lifetime_source_register_in(
    DlumaProcessLifetimeSourceRegistry* registry,
    const DlumaProcessLifetimeSourceRecord* record,
    unsigned int* record_index_out);
int dluma_process_lifetime_source_bind_inventory_in(
    DlumaProcessLifetimeSourceRegistry* registry,
    DlumaProcessLifetimeSourceOwnerKind owner_kind,
    const char* owner_id,
    void* source_object,
    void* intrusive_link,
    unsigned int list_ordinal,
    const char* observed_source_path,
    unsigned int runtime_source_type,
    ULONGLONG registry_generation);
int dluma_process_lifetime_source_live_presence_in(
    DlumaProcessLifetimeSourceRegistry* registry,
    DlumaProcessLifetimeSourceOwnerKind owner_kind,
    const char* owner_id,
    void* source_object,
    ULONGLONG mount_generation,
    ULONGLONG registry_generation,
    DlumaProcessLifetimeSourceGenerationToken* token_out);
int dluma_process_lifetime_source_retire_in(
    DlumaProcessLifetimeSourceRegistry* registry,
    DlumaProcessLifetimeSourceOwnerKind owner_kind,
    const char* owner_id,
    void* source_object,
    ULONGLONG mount_generation,
    ULONGLONG registry_generation,
    ULONGLONG retirement_registry_generation,
    DlumaProcessLifetimeSourceGenerationToken* token_out);
int dluma_process_lifetime_source_compare_replace_in(
    DlumaProcessLifetimeSourceRegistry* registry,
    const DlumaProcessLifetimeSourceGenerationToken* retired_generation,
    const DlumaProcessLifetimeSourceRecord* replacement,
    unsigned int* record_index_out);
int dluma_process_lifetime_source_snapshot_in(
    DlumaProcessLifetimeSourceRegistry* registry,
    DlumaProcessLifetimeSourceSnapshot* snapshot_out);
int dluma_process_lifetime_source_publish_native_admission_in(
    DlumaProcessLifetimeSourceRegistry* registry,
    DlumaProcessLifetimeSourceOwnerKind owner_kind,
    const char* owner_id,
    void* source_object,
    const DlumaNativeAdmissionIdentity* admission);

int dluma_process_lifetime_source_register(
    const DlumaProcessLifetimeSourceRecord* record,
    unsigned int* record_index_out);
int dluma_process_lifetime_source_bind_inventory(
    DlumaProcessLifetimeSourceOwnerKind owner_kind,
    const char* owner_id,
    void* source_object,
    void* intrusive_link,
    unsigned int list_ordinal,
    const char* observed_source_path,
    unsigned int runtime_source_type,
    ULONGLONG registry_generation);
int dluma_process_lifetime_source_live_presence(
    DlumaProcessLifetimeSourceOwnerKind owner_kind,
    const char* owner_id,
    void* source_object,
    ULONGLONG mount_generation,
    ULONGLONG registry_generation,
    DlumaProcessLifetimeSourceGenerationToken* token_out);
int dluma_process_lifetime_source_retire(
    DlumaProcessLifetimeSourceOwnerKind owner_kind,
    const char* owner_id,
    void* source_object,
    ULONGLONG mount_generation,
    ULONGLONG registry_generation,
    ULONGLONG retirement_registry_generation,
    DlumaProcessLifetimeSourceGenerationToken* token_out);
int dluma_process_lifetime_source_compare_replace(
    const DlumaProcessLifetimeSourceGenerationToken* retired_generation,
    const DlumaProcessLifetimeSourceRecord* replacement,
    unsigned int* record_index_out);
int dluma_process_lifetime_sources_snapshot(
    DlumaProcessLifetimeSourceSnapshot* snapshot_out);
int dluma_process_lifetime_source_publish_native_admission(
    DlumaProcessLifetimeSourceOwnerKind owner_kind,
    const char* owner_id,
    void* source_object,
    const DlumaNativeAdmissionIdentity* admission);

int dluma_process_lifetime_source_normalize_path(
    const char* input, char* output, unsigned int capacity);
const char* dluma_process_lifetime_source_owner_name(
    DlumaProcessLifetimeSourceOwnerKind owner_kind);

#ifdef __cplusplus
}
#endif

#endif
