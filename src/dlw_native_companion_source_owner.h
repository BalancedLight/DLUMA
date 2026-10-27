#ifndef DLUMA_COMPANION_ARCHIVE_SOURCE_OWNER_H
#define DLUMA_COMPANION_ARCHIVE_SOURCE_OWNER_H

#include "dlw_process_lifetime_sources.h"

typedef enum DlumaNativeCompanionOwnerValidationResult {
    DLUMA_COMPANION_ARCHIVE_OWNER_VALID = 0,
    DLUMA_COMPANION_ARCHIVE_OWNER_RECORD_MISSING,
    DLUMA_COMPANION_ARCHIVE_OWNER_MODULE_ID_MISMATCH,
    DLUMA_COMPANION_ARCHIVE_OWNER_MODULE_NOT_ENABLED,
    DLUMA_COMPANION_ARCHIVE_OWNER_NATIVE_REGISTRY_ITEM_MISSING,
    DLUMA_COMPANION_ARCHIVE_OWNER_NATIVE_REGISTRY_ITEM_NOT_PUBLISHED,
    DLUMA_COMPANION_ARCHIVE_OWNER_APPROVAL_FINGERPRINT_MISMATCH,
    DLUMA_COMPANION_ARCHIVE_OWNER_MANIFEST_FINGERPRINT_MISMATCH,
    DLUMA_COMPANION_ARCHIVE_OWNER_MODULE_HASH_MISMATCH,
    DLUMA_COMPANION_ARCHIVE_OWNER_COMPANION_HASH_MISMATCH,
    DLUMA_COMPANION_ARCHIVE_OWNER_REQUESTED_PACK_PATH_MISMATCH,
    DLUMA_COMPANION_ARCHIVE_OWNER_SOURCE_POINTER_MISMATCH,
    DLUMA_COMPANION_ARCHIVE_OWNER_SOURCE_TYPE_MISMATCH,
    DLUMA_COMPANION_ARCHIVE_OWNER_SOURCE_FLAGS_MISMATCH,
    DLUMA_COMPANION_ARCHIVE_OWNER_MOUNT_GENERATION_MISMATCH,
    DLUMA_COMPANION_ARCHIVE_OWNER_NATIVE_REGISTRY_REVISION_MISMATCH,
    DLUMA_COMPANION_ARCHIVE_OWNER_SOURCE_RECORD_NOT_COMMITTED,
    DLUMA_COMPANION_ARCHIVE_OWNER_OWNER_SNAPSHOT_NOT_PUBLISHED,
    DLUMA_COMPANION_ARCHIVE_OWNER_STABLE_CANDIDATE_ID_MISMATCH,
    DLUMA_COMPANION_ARCHIVE_OWNER_MODULE_PATH_MISMATCH,
    DLUMA_COMPANION_ARCHIVE_OWNER_FILE_IDENTITY_MISMATCH
} DlumaNativeCompanionOwnerValidationResult;

typedef struct DlumaNativeCompanionOwnerValidationDiagnostic {
    DlumaNativeCompanionOwnerValidationResult result;
    DlumaNativeAdmissionIdentity registered;
    DlumaNativeAdmissionIdentity observed;
    ULONGLONG current_native_registry_revision;
    unsigned int native_registry_item_found;
    unsigned int loaded_module_present;
    unsigned int approval_valid;
    unsigned int enabled_requested_now;
} DlumaNativeCompanionOwnerValidationDiagnostic;

#ifdef __cplusplus
extern "C" {
#endif

DlumaNativeCompanionOwnerValidationResult
dluma_native_companion_source_owner_validate(
    const DlumaProcessLifetimeSourceRecord* record,
    const DlumaNativeAdmissionIdentity* observed,
    DlumaNativeCompanionOwnerValidationDiagnostic* diagnostic);

int dluma_native_companion_source_owner_matches(
    const DlumaProcessLifetimeSourceRecord* record,
    const DlumaNativeAdmissionIdentity* observed);

const char* dluma_native_companion_owner_validation_result_name(
    DlumaNativeCompanionOwnerValidationResult result);
int dluma_native_companion_owner_validation_result_retryable(
    DlumaNativeCompanionOwnerValidationResult result);

#ifdef __cplusplus
}
#endif

#endif
