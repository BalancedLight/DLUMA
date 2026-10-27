#include "dlw_native_companion_source_owner.h"

static char dluma_native_source_lower(char value)
{
    return value >= 'A' && value <= 'Z'
        ? (char)(value + ('a' - 'A')) : value;
}

static int dluma_native_source_text_equal(
    const char* left, const char* right)
{
    unsigned int index = 0;
    if (!left || !right || !left[0] || !right[0])
        return 0;
    while (left[index] && right[index]) {
        if (dluma_native_source_lower(left[index]) !=
            dluma_native_source_lower(right[index]))
            return 0;
        ++index;
    }
    return left[index] == right[index];
}

static int dluma_native_source_path_equal(
    const char* left, const char* right)
{
    char normalized_left[DLUMA_PROCESS_SOURCE_PATH_CAPACITY];
    char normalized_right[DLUMA_PROCESS_SOURCE_PATH_CAPACITY];
    return dluma_process_lifetime_source_normalize_path(
               left, normalized_left,
               (unsigned int)sizeof(normalized_left)) &&
           dluma_process_lifetime_source_normalize_path(
               right, normalized_right,
               (unsigned int)sizeof(normalized_right)) &&
           dluma_native_source_text_equal(
               normalized_left, normalized_right);
}

static int dluma_native_source_file_identity_equal(
    ULONGLONG left_volume,
    ULONGLONG left_index_high,
    ULONGLONG left_index_low,
    ULONGLONG left_size,
    ULONGLONG right_volume,
    ULONGLONG right_index_high,
    ULONGLONG right_index_low,
    ULONGLONG right_size)
{
    return left_volume == right_volume &&
        left_index_high == right_index_high &&
        left_index_low == right_index_low &&
        left_size == right_size;
}

static DlumaNativeCompanionOwnerValidationResult
dluma_native_companion_source_owner_validate_exact(
    const DlumaProcessLifetimeSourceRecord* record,
    const DlumaNativeAdmissionIdentity* registered,
    const DlumaNativeAdmissionIdentity* observed)
{
    if (!record ||
        record->owner_kind !=
            DLUMA_SOURCE_OWNER_NATIVE_COMPANION_PACK ||
        !record->metadata_complete ||
        !record->owner_id[0] ||
        !registered ||
        !registered->admission_generation)
        return DLUMA_COMPANION_ARCHIVE_OWNER_RECORD_MISSING;
    if (!registered->source_record_committed)
        return DLUMA_COMPANION_ARCHIVE_OWNER_SOURCE_RECORD_NOT_COMMITTED;
    if (!observed || !observed->admission_generation ||
        !observed->owner_snapshot_published)
        return DLUMA_COMPANION_ARCHIVE_OWNER_OWNER_SNAPSHOT_NOT_PUBLISHED;
    if (!dluma_native_source_text_equal(
            record->owner_id, registered->module_id) ||
        !dluma_native_source_text_equal(
            registered->module_id, observed->module_id))
        return DLUMA_COMPANION_ARCHIVE_OWNER_MODULE_ID_MISMATCH;
    if (!dluma_native_source_text_equal(
            registered->stable_candidate_id,
            observed->stable_candidate_id))
        return DLUMA_COMPANION_ARCHIVE_OWNER_STABLE_CANDIDATE_ID_MISMATCH;
    if (!registered->enabled_for_this_launch ||
        !observed->enabled_for_this_launch)
        return DLUMA_COMPANION_ARCHIVE_OWNER_MODULE_NOT_ENABLED;
    if (!dluma_native_source_path_equal(
            registered->module_path, observed->module_path))
        return DLUMA_COMPANION_ARCHIVE_OWNER_MODULE_PATH_MISMATCH;
    if (!dluma_native_source_text_equal(
            registered->module_sha256,
            observed->module_sha256))
        return DLUMA_COMPANION_ARCHIVE_OWNER_MODULE_HASH_MISMATCH;
    if (!dluma_native_source_text_equal(
            record->approval_fingerprint,
            registered->approval_fingerprint) ||
        !dluma_native_source_text_equal(
            registered->approval_fingerprint,
            observed->approval_fingerprint))
        return
            DLUMA_COMPANION_ARCHIVE_OWNER_APPROVAL_FINGERPRINT_MISMATCH;
    if (!dluma_native_source_text_equal(
            record->manifest_fingerprint,
            registered->manifest_fingerprint) ||
        !dluma_native_source_text_equal(
            registered->manifest_fingerprint,
            observed->manifest_fingerprint))
        return
            DLUMA_COMPANION_ARCHIVE_OWNER_MANIFEST_FINGERPRINT_MISMATCH;
    if (!dluma_native_source_text_equal(
            record->content_sha256,
            registered->companion_sha256) ||
        !dluma_native_source_text_equal(
            registered->companion_sha256,
            observed->companion_sha256))
        return DLUMA_COMPANION_ARCHIVE_OWNER_COMPANION_HASH_MISMATCH;
    if (!dluma_native_source_path_equal(
            record->requested_mount_path,
            registered->companion_path) ||
        !dluma_native_source_path_equal(
            registered->companion_path,
            observed->companion_path))
        return
            DLUMA_COMPANION_ARCHIVE_OWNER_REQUESTED_PACK_PATH_MISMATCH;
    if (record->source_object != registered->source_object ||
        registered->source_object != observed->source_object)
        return DLUMA_COMPANION_ARCHIVE_OWNER_SOURCE_POINTER_MISMATCH;
    if (record->runtime_source_type !=
            DLUMA_PROCESS_SOURCE_RUNTIME_PACK ||
        registered->source_runtime_type !=
            DLUMA_PROCESS_SOURCE_RUNTIME_PACK ||
        observed->source_runtime_type !=
            DLUMA_PROCESS_SOURCE_RUNTIME_PACK ||
        record->runtime_source_type !=
            registered->source_runtime_type ||
        registered->source_runtime_type !=
            observed->source_runtime_type)
        return DLUMA_COMPANION_ARCHIVE_OWNER_SOURCE_TYPE_MISMATCH;
    if (record->source_flags != registered->source_flags ||
        registered->source_flags != observed->source_flags)
        return DLUMA_COMPANION_ARCHIVE_OWNER_SOURCE_FLAGS_MISMATCH;
    if (!record->mount_generation ||
        record->mount_generation != registered->mount_generation ||
        registered->mount_generation !=
            observed->mount_generation)
        return
            DLUMA_COMPANION_ARCHIVE_OWNER_MOUNT_GENERATION_MISMATCH;
    if (!registered->native_registry_publication_generation ||
        registered->native_registry_publication_generation !=
            observed->native_registry_publication_generation ||
        registered->admission_generation !=
            observed->admission_generation)
        return
            DLUMA_COMPANION_ARCHIVE_OWNER_NATIVE_REGISTRY_REVISION_MISMATCH;
    if (!dluma_native_source_file_identity_equal(
            registered->module_volume_serial,
            registered->module_file_index_high,
            registered->module_file_index_low,
            registered->module_file_size,
            observed->module_volume_serial,
            observed->module_file_index_high,
            observed->module_file_index_low,
            observed->module_file_size) ||
        !dluma_native_source_file_identity_equal(
            record->volume_serial,
            record->file_index_high,
            record->file_index_low,
            record->file_size,
            registered->companion_volume_serial,
            registered->companion_file_index_high,
            registered->companion_file_index_low,
            registered->companion_file_size) ||
        !dluma_native_source_file_identity_equal(
            registered->companion_volume_serial,
            registered->companion_file_index_high,
            registered->companion_file_index_low,
            registered->companion_file_size,
            observed->companion_volume_serial,
            observed->companion_file_index_high,
            observed->companion_file_index_low,
            observed->companion_file_size))
        return DLUMA_COMPANION_ARCHIVE_OWNER_FILE_IDENTITY_MISMATCH;
    return DLUMA_COMPANION_ARCHIVE_OWNER_VALID;
}

DlumaNativeCompanionOwnerValidationResult
dluma_native_companion_source_owner_validate(
    const DlumaProcessLifetimeSourceRecord* record,
    const DlumaNativeAdmissionIdentity* observed,
    DlumaNativeCompanionOwnerValidationDiagnostic* diagnostic)
{
    const DlumaNativeAdmissionIdentity* registered =
        record ? &record->native_admission : 0;
    DlumaNativeCompanionOwnerValidationResult result =
        dluma_native_companion_source_owner_validate_exact(
            record, registered, observed);
    if (diagnostic) {
        memset(diagnostic, 0, sizeof(*diagnostic));
        diagnostic->result = result;
        if (registered)
            diagnostic->registered = *registered;
        if (observed)
            diagnostic->observed = *observed;
    }
    return result;
}

int dluma_native_companion_source_owner_matches(
    const DlumaProcessLifetimeSourceRecord* record,
    const DlumaNativeAdmissionIdentity* observed)
{
    return dluma_native_companion_source_owner_validate(
               record, observed, 0) ==
           DLUMA_COMPANION_ARCHIVE_OWNER_VALID;
}

const char* dluma_native_companion_owner_validation_result_name(
    DlumaNativeCompanionOwnerValidationResult result)
{
    switch (result) {
    case DLUMA_COMPANION_ARCHIVE_OWNER_VALID:
        return "valid";
    case DLUMA_COMPANION_ARCHIVE_OWNER_RECORD_MISSING:
        return "record_missing";
    case DLUMA_COMPANION_ARCHIVE_OWNER_MODULE_ID_MISMATCH:
        return "module_id_mismatch";
    case DLUMA_COMPANION_ARCHIVE_OWNER_MODULE_NOT_ENABLED:
        return "module_not_enabled";
    case DLUMA_COMPANION_ARCHIVE_OWNER_NATIVE_REGISTRY_ITEM_MISSING:
        return "native_registry_item_missing";
    case DLUMA_COMPANION_ARCHIVE_OWNER_NATIVE_REGISTRY_ITEM_NOT_PUBLISHED:
        return "native_registry_item_not_published";
    case DLUMA_COMPANION_ARCHIVE_OWNER_APPROVAL_FINGERPRINT_MISMATCH:
        return "approval_fingerprint_mismatch";
    case DLUMA_COMPANION_ARCHIVE_OWNER_MANIFEST_FINGERPRINT_MISMATCH:
        return "manifest_fingerprint_mismatch";
    case DLUMA_COMPANION_ARCHIVE_OWNER_MODULE_HASH_MISMATCH:
        return "module_hash_mismatch";
    case DLUMA_COMPANION_ARCHIVE_OWNER_COMPANION_HASH_MISMATCH:
        return "companion_hash_mismatch";
    case DLUMA_COMPANION_ARCHIVE_OWNER_REQUESTED_PACK_PATH_MISMATCH:
        return "requested_pack_path_mismatch";
    case DLUMA_COMPANION_ARCHIVE_OWNER_SOURCE_POINTER_MISMATCH:
        return "source_pointer_mismatch";
    case DLUMA_COMPANION_ARCHIVE_OWNER_SOURCE_TYPE_MISMATCH:
        return "source_type_mismatch";
    case DLUMA_COMPANION_ARCHIVE_OWNER_SOURCE_FLAGS_MISMATCH:
        return "source_flags_mismatch";
    case DLUMA_COMPANION_ARCHIVE_OWNER_MOUNT_GENERATION_MISMATCH:
        return "mount_generation_mismatch";
    case DLUMA_COMPANION_ARCHIVE_OWNER_NATIVE_REGISTRY_REVISION_MISMATCH:
        return "native_registry_revision_mismatch";
    case DLUMA_COMPANION_ARCHIVE_OWNER_SOURCE_RECORD_NOT_COMMITTED:
        return "source_record_not_committed";
    case DLUMA_COMPANION_ARCHIVE_OWNER_OWNER_SNAPSHOT_NOT_PUBLISHED:
        return "owner_snapshot_not_published";
    case DLUMA_COMPANION_ARCHIVE_OWNER_STABLE_CANDIDATE_ID_MISMATCH:
        return "stable_candidate_id_mismatch";
    case DLUMA_COMPANION_ARCHIVE_OWNER_MODULE_PATH_MISMATCH:
        return "module_path_mismatch";
    case DLUMA_COMPANION_ARCHIVE_OWNER_FILE_IDENTITY_MISMATCH:
        return "file_identity_mismatch";
    default:
        return "unknown";
    }
}

int dluma_native_companion_owner_validation_result_retryable(
    DlumaNativeCompanionOwnerValidationResult result)
{
    return
        result ==
            DLUMA_COMPANION_ARCHIVE_OWNER_NATIVE_REGISTRY_ITEM_NOT_PUBLISHED ||
        result ==
            DLUMA_COMPANION_ARCHIVE_OWNER_OWNER_SNAPSHOT_NOT_PUBLISHED;
}
