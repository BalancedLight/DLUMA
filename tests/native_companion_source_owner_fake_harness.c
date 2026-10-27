#include "dlw_native_companion_source_owner.h"

#include <stdio.h>
#include <string.h>

#define CHECK(expression)                                                   \
    do {                                                                    \
        if (!(expression)) {                                                \
            fprintf(stderr, "CHECK failed at line %d: %s\n",              \
                    __LINE__, #expression);                                 \
            return 0;                                                       \
        }                                                                   \
    } while (0)

static void fill_hex(char output[65], char value)
{
    unsigned int index;
    for (index = 0; index < 64U; ++index)
        output[index] = value;
    output[64] = 0;
}

static void make_valid(
    DlumaProcessLifetimeSourceRecord* record,
    DlumaNativeAdmissionIdentity* observed)
{
    DlumaNativeAdmissionIdentity* registered;
    memset(record, 0, sizeof(*record));
    memset(observed, 0, sizeof(*observed));
    record->owner_kind =
        DLUMA_SOURCE_OWNER_NATIVE_COMPANION_PACK;
    strcpy_s(
        record->owner_id, sizeof(record->owner_id),
        "example.vehicle_audio");
    record->source_object = (void*)0x10000;
    strcpy_s(
        record->requested_mount_path,
        sizeof(record->requested_mount_path),
        "X:\\workshop\\3773041738\\mods\\native\\global\\"
        "example.vehicle_audio\\PlayerMedia.pak");
    record->runtime_source_type =
        DLUMA_PROCESS_SOURCE_RUNTIME_PACK;
    record->source_flags = 9U;
    record->mount_generation = 7U;
    record->volume_serial = 11U;
    record->file_index_high = 12U;
    record->file_index_low = 13U;
    record->file_size = 4096U;
    fill_hex(record->content_sha256, 'c');
    fill_hex(record->approval_fingerprint, 'a');
    fill_hex(record->manifest_fingerprint, 'b');
    record->metadata_complete = 1U;

    registered = &record->native_admission;
    strcpy_s(
        registered->module_id, sizeof(registered->module_id),
        record->owner_id);
    strcpy_s(
        registered->module_name, sizeof(registered->module_name),
        "PlayerMedia Stemmed Buggy Music");
    fill_hex(registered->stable_candidate_id, 'd');
    strcpy_s(
        registered->module_path, sizeof(registered->module_path),
        "X:\\workshop\\3773041738\\mods\\native\\global\\"
        "example.vehicle_audio\\PlayerMedia.mod");
    fill_hex(registered->module_sha256, 'e');
    strcpy_s(
        registered->approval_fingerprint,
        sizeof(registered->approval_fingerprint),
        record->approval_fingerprint);
    strcpy_s(
        registered->manifest_fingerprint,
        sizeof(registered->manifest_fingerprint),
        record->manifest_fingerprint);
    strcpy_s(
        registered->companion_path,
        sizeof(registered->companion_path),
        "\\\\?\\X:\\workshop\\3773041738\\mods\\native\\global\\"
        "example.vehicle_audio\\PlayerMedia.pak");
    strcpy_s(
        registered->companion_sha256,
        sizeof(registered->companion_sha256),
        record->content_sha256);
    registered->source_object = record->source_object;
    registered->source_runtime_type =
        DLUMA_PROCESS_SOURCE_RUNTIME_PACK;
    registered->source_flags = record->source_flags;
    registered->mount_generation = record->mount_generation;
    registered->native_registry_publication_generation = 41U;
    registered->admission_generation = 19U;
    registered->module_volume_serial = 21U;
    registered->module_file_index_high = 22U;
    registered->module_file_index_low = 23U;
    registered->module_file_size = 8192U;
    registered->companion_volume_serial =
        record->volume_serial;
    registered->companion_file_index_high =
        record->file_index_high;
    registered->companion_file_index_low =
        record->file_index_low;
    registered->companion_file_size = record->file_size;
    registered->enabled_for_this_launch = 1U;
    registered->source_record_committed = 1U;
    registered->owner_snapshot_published = 1U;
    *observed = *registered;
}

static DlumaNativeCompanionOwnerValidationResult validate(
    const DlumaProcessLifetimeSourceRecord* record,
    const DlumaNativeAdmissionIdentity* observed)
{
    DlumaNativeCompanionOwnerValidationDiagnostic diagnostic;
    memset(&diagnostic, 0, sizeof(diagnostic));
    return dluma_native_companion_source_owner_validate(
        record, observed, &diagnostic);
}

static int test_nested_exact_approved_owner(void)
{
    DlumaProcessLifetimeSourceRecord record;
    DlumaNativeAdmissionIdentity observed;
    make_valid(&record, &observed);
    CHECK(validate(&record, &observed) ==
          DLUMA_COMPANION_ARCHIVE_OWNER_VALID);
    CHECK(dluma_native_companion_source_owner_matches(
        &record, &observed));
    return 1;
}

static int test_named_identity_failures(void)
{
    DlumaProcessLifetimeSourceRecord record;
    DlumaNativeAdmissionIdentity observed;

    make_valid(&record, &observed);
    observed.source_object = (void*)0x20000;
    CHECK(validate(&record, &observed) ==
          DLUMA_COMPANION_ARCHIVE_OWNER_SOURCE_POINTER_MISMATCH);

    make_valid(&record, &observed);
    observed.module_sha256[0] = 'f';
    CHECK(validate(&record, &observed) ==
          DLUMA_COMPANION_ARCHIVE_OWNER_MODULE_HASH_MISMATCH);

    make_valid(&record, &observed);
    observed.companion_sha256[0] = 'f';
    CHECK(validate(&record, &observed) ==
          DLUMA_COMPANION_ARCHIVE_OWNER_COMPANION_HASH_MISMATCH);

    make_valid(&record, &observed);
    observed.approval_fingerprint[0] = 'f';
    CHECK(validate(&record, &observed) ==
          DLUMA_COMPANION_ARCHIVE_OWNER_APPROVAL_FINGERPRINT_MISMATCH);

    make_valid(&record, &observed);
    observed.manifest_fingerprint[0] = 'f';
    CHECK(validate(&record, &observed) ==
          DLUMA_COMPANION_ARCHIVE_OWNER_MANIFEST_FINGERPRINT_MISMATCH);

    make_valid(&record, &observed);
    ++observed.mount_generation;
    CHECK(validate(&record, &observed) ==
          DLUMA_COMPANION_ARCHIVE_OWNER_MOUNT_GENERATION_MISMATCH);

    make_valid(&record, &observed);
    observed.source_runtime_type =
        DLUMA_PROCESS_SOURCE_RUNTIME_DIRECTORY;
    CHECK(validate(&record, &observed) ==
          DLUMA_COMPANION_ARCHIVE_OWNER_SOURCE_TYPE_MISMATCH);

    make_valid(&record, &observed);
    ++observed.source_flags;
    CHECK(validate(&record, &observed) ==
          DLUMA_COMPANION_ARCHIVE_OWNER_SOURCE_FLAGS_MISMATCH);

    make_valid(&record, &observed);
    observed.companion_path[0] = 'F';
    CHECK(validate(&record, &observed) ==
          DLUMA_COMPANION_ARCHIVE_OWNER_REQUESTED_PACK_PATH_MISMATCH);

    make_valid(&record, &observed);
    observed.enabled_for_this_launch = 0U;
    CHECK(validate(&record, &observed) ==
          DLUMA_COMPANION_ARCHIVE_OWNER_MODULE_NOT_ENABLED);
    return 1;
}

static int test_publication_and_revision_contract(void)
{
    DlumaProcessLifetimeSourceRecord record;
    DlumaNativeAdmissionIdentity observed;
    ULONGLONG unrelated_current_registry_revision = 999U;

    make_valid(&record, &observed);
    observed.owner_snapshot_published = 0U;
    CHECK(validate(&record, &observed) ==
          DLUMA_COMPANION_ARCHIVE_OWNER_OWNER_SNAPSHOT_NOT_PUBLISHED);
    CHECK(dluma_native_companion_owner_validation_result_retryable(
        DLUMA_COMPANION_ARCHIVE_OWNER_OWNER_SNAPSHOT_NOT_PUBLISHED));
    observed.owner_snapshot_published = 1U;
    CHECK(validate(&record, &observed) ==
          DLUMA_COMPANION_ARCHIVE_OWNER_VALID);

    make_valid(&record, &observed);
    CHECK(unrelated_current_registry_revision !=
          observed.native_registry_publication_generation);
    CHECK(validate(&record, &observed) ==
          DLUMA_COMPANION_ARCHIVE_OWNER_VALID);
    ++observed.native_registry_publication_generation;
    CHECK(validate(&record, &observed) ==
          DLUMA_COMPANION_ARCHIVE_OWNER_NATIVE_REGISTRY_REVISION_MISMATCH);
    CHECK(!dluma_native_companion_owner_validation_result_retryable(
        DLUMA_COMPANION_ARCHIVE_OWNER_MODULE_HASH_MISMATCH));
    return 1;
}

static int test_proven_drive_path_forms(void)
{
    char canonical[2048];
    char normalized[2048];
    const char* forms[] = {
        "X:\\mods\\native\\global\\PlayerMedia.pak",
        "\\\\?\\X:\\mods\\native\\global\\PlayerMedia.pak",
        "\\?\\X:\\mods\\native\\global\\PlayerMedia.pak",
        "//?/X:/mods/native/global/PlayerMedia.pak"
    };
    unsigned int index;
    CHECK(dluma_process_lifetime_source_normalize_path(
        forms[0], canonical, (unsigned int)sizeof(canonical)));
    for (index = 1U;
         index < sizeof(forms) / sizeof(forms[0]); ++index) {
        CHECK(dluma_process_lifetime_source_normalize_path(
            forms[index], normalized,
            (unsigned int)sizeof(normalized)));
        CHECK(_stricmp(canonical, normalized) == 0);
    }
    CHECK(!dluma_process_lifetime_source_normalize_path(
        "//?/X:/mods/../escape", normalized,
        (unsigned int)sizeof(normalized)));
    CHECK(!dluma_process_lifetime_source_normalize_path(
        "//?/bad-prefix", normalized,
        (unsigned int)sizeof(normalized)));
    return 1;
}

int main(void)
{
    if (!test_nested_exact_approved_owner() ||
        !test_named_identity_failures() ||
        !test_publication_and_revision_contract() ||
        !test_proven_drive_path_forms())
        return 1;
    puts("native companion source owner fake harness passed");
    return 0;
}
