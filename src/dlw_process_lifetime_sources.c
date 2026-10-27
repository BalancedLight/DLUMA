#include "dlw_process_lifetime_sources.h"

static DlumaProcessLifetimeSourceRegistry
    g_dluma_process_lifetime_sources;

static void dluma_process_source_lock(
    DlumaProcessLifetimeSourceRegistry* registry)
{
    if (!registry) return;
    while (dluma_atomic_compare_exchange(
               &registry->lock, 1, 0) != 0)
        Sleep(0);
}

static void dluma_process_source_unlock(
    DlumaProcessLifetimeSourceRegistry* registry)
{
    if (registry)
        dluma_atomic_exchange(&registry->lock, 0);
}

static ULONGLONG dluma_process_source_increment(
    ULONGLONG value)
{
    if (value == ~(ULONGLONG)0)
        return value;
    ++value;
    return value ? value : 1U;
}

static char dluma_process_source_lower(char value)
{
    return value >= 'A' && value <= 'Z'
        ? (char)(value + ('a' - 'A')) : value;
}

static int dluma_process_source_is_drive_letter(char value)
{
    value = dluma_process_source_lower(value);
    return value >= 'a' && value <= 'z';
}

static int dluma_process_source_is_separator(char value)
{
    return value == '\\' || value == '/';
}

static int dluma_process_source_text_equal(
    const char* left, const char* right)
{
    unsigned int index = 0;
    if (!left || !right) return 0;
    while (left[index] && right[index]) {
        if (dluma_process_source_lower(left[index]) !=
            dluma_process_source_lower(right[index]))
            return 0;
        ++index;
    }
    return left[index] == right[index];
}

static int dluma_process_source_path_equal(
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
           dluma_process_source_text_equal(
               normalized_left, normalized_right);
}

static int dluma_process_source_copy(
    char* output, unsigned int capacity, const char* input)
{
    unsigned int index = 0;
    if (!output || !capacity || !input)
        return 0;
    while (input[index]) {
        if (index + 1U >= capacity) {
            output[0] = 0;
            return 0;
        }
        output[index] = input[index];
        ++index;
    }
    output[index] = 0;
    return index != 0;
}

static int dluma_process_source_hex_fingerprint(
    const char* value)
{
    unsigned int index;
    if (!value) return 0;
    for (index = 0;
         index + 1U < DLUMA_PROCESS_SOURCE_FINGERPRINT_CAPACITY;
         ++index) {
        const char ch = value[index];
        if (!ch) return 0;
        if (!((ch >= '0' && ch <= '9') ||
              (ch >= 'a' && ch <= 'f') ||
              (ch >= 'A' && ch <= 'F')))
            return 0;
    }
    return value[index] == 0;
}

static int dluma_process_source_owner_valid(
    DlumaProcessLifetimeSourceOwnerKind owner_kind)
{
    return owner_kind >= DLUMA_SOURCE_OWNER_LOADER_PAK &&
           owner_kind <=
               DLUMA_SOURCE_OWNER_ASSET_MOD;
}

static int dluma_process_source_native_admission_same(
    const DlumaNativeAdmissionIdentity* left,
    const DlumaNativeAdmissionIdentity* right,
    int compare_publication_state)
{
    if (!left || !right)
        return 0;
    return
        dluma_process_source_text_equal(
            left->module_id, right->module_id) &&
        dluma_process_source_text_equal(
            left->module_name, right->module_name) &&
        dluma_process_source_text_equal(
            left->stable_candidate_id,
            right->stable_candidate_id) &&
        dluma_process_source_path_equal(
            left->module_path, right->module_path) &&
        dluma_process_source_text_equal(
            left->module_sha256, right->module_sha256) &&
        dluma_process_source_text_equal(
            left->approval_fingerprint,
            right->approval_fingerprint) &&
        dluma_process_source_text_equal(
            left->manifest_fingerprint,
            right->manifest_fingerprint) &&
        dluma_process_source_path_equal(
            left->companion_path, right->companion_path) &&
        dluma_process_source_text_equal(
            left->companion_sha256,
            right->companion_sha256) &&
        left->source_object == right->source_object &&
        left->source_runtime_type == right->source_runtime_type &&
        left->source_flags == right->source_flags &&
        (!compare_publication_state ||
         left->mount_generation == right->mount_generation) &&
        left->native_registry_publication_generation ==
            right->native_registry_publication_generation &&
        left->admission_generation ==
            right->admission_generation &&
        left->module_volume_serial ==
            right->module_volume_serial &&
        left->module_file_index_high ==
            right->module_file_index_high &&
        left->module_file_index_low ==
            right->module_file_index_low &&
        left->module_file_size == right->module_file_size &&
        left->companion_volume_serial ==
            right->companion_volume_serial &&
        left->companion_file_index_high ==
            right->companion_file_index_high &&
        left->companion_file_index_low ==
            right->companion_file_index_low &&
        left->companion_file_size ==
            right->companion_file_size &&
        left->enabled_for_this_launch ==
            right->enabled_for_this_launch &&
        (!compare_publication_state ||
         (left->source_record_committed ==
              right->source_record_committed &&
          left->owner_snapshot_published ==
              right->owner_snapshot_published));
}

static int dluma_process_source_native_admission_valid(
    const DlumaProcessLifetimeSourceRecord* record)
{
    const DlumaNativeAdmissionIdentity* admission;
    if (!record ||
        record->owner_kind !=
            DLUMA_SOURCE_OWNER_NATIVE_COMPANION_PACK)
        return 0;
    admission = &record->native_admission;
    return
        admission->module_id[0] &&
        admission->module_name[0] &&
        admission->stable_candidate_id[0] &&
        admission->module_path[0] &&
        admission->module_sha256[0] &&
        admission->approval_fingerprint[0] &&
        admission->manifest_fingerprint[0] &&
        admission->companion_path[0] &&
        admission->companion_sha256[0] &&
        admission->source_object == record->source_object &&
        admission->source_runtime_type ==
            record->runtime_source_type &&
        admission->source_flags == record->source_flags &&
        !admission->mount_generation &&
        admission->native_registry_publication_generation &&
        admission->admission_generation &&
        admission->module_file_size &&
        admission->companion_file_size == record->file_size &&
        admission->enabled_for_this_launch &&
        !admission->source_record_committed &&
        !admission->owner_snapshot_published &&
        dluma_process_source_text_equal(
            admission->module_id, record->owner_id) &&
        dluma_process_source_path_equal(
            admission->companion_path,
            record->requested_mount_path) &&
        dluma_process_source_text_equal(
            admission->companion_sha256,
            record->content_sha256) &&
        dluma_process_source_text_equal(
            admission->approval_fingerprint,
            record->approval_fingerprint) &&
        dluma_process_source_text_equal(
            admission->manifest_fingerprint,
            record->manifest_fingerprint) &&
        dluma_process_source_hex_fingerprint(
            admission->stable_candidate_id) &&
        dluma_process_source_hex_fingerprint(
            admission->module_sha256) &&
        dluma_process_source_hex_fingerprint(
            admission->approval_fingerprint) &&
        dluma_process_source_hex_fingerprint(
            admission->manifest_fingerprint) &&
        dluma_process_source_hex_fingerprint(
            admission->companion_sha256);
}

static int dluma_process_source_record_valid(
    const DlumaProcessLifetimeSourceRecord* record)
{
    const int generic_companion =
        record && record->owner_kind ==
            DLUMA_SOURCE_OWNER_MOD_COMPANION_CONTENT;
    const int custom_main_menu =
        record && record->owner_kind ==
            DLUMA_SOURCE_OWNER_CUSTOM_MAIN_MENU;
    const int asset_mod = record &&
        record->owner_kind == DLUMA_SOURCE_OWNER_ASSET_MOD;
    const int directory =
        record &&
        (record->owner_kind ==
             DLUMA_SOURCE_OWNER_LOADER_DEVELOPER_MODE ||
         record->owner_kind ==
             DLUMA_SOURCE_OWNER_LOADER_THUMBNAILS ||
         asset_mod ||
         (custom_main_menu && record->runtime_source_type ==
              DLUMA_PROCESS_SOURCE_RUNTIME_DIRECTORY) ||
         (generic_companion && record->runtime_source_type ==
              DLUMA_PROCESS_SOURCE_RUNTIME_DIRECTORY));
    const int pack =
        record &&
        (record->owner_kind == DLUMA_SOURCE_OWNER_LOADER_PAK ||
         (custom_main_menu && record->runtime_source_type ==
              DLUMA_PROCESS_SOURCE_RUNTIME_PACK) ||
         record->owner_kind ==
             DLUMA_SOURCE_OWNER_NATIVE_COMPANION_PACK ||
         (generic_companion && record->runtime_source_type ==
              DLUMA_PROCESS_SOURCE_RUNTIME_PACK));
    if (!record ||
        !dluma_process_source_owner_valid(record->owner_kind) ||
        !record->owner_id[0] ||
        !record->source_object ||
        !record->requested_mount_path[0] ||
        !record->metadata_complete ||
        (!record->volume_serial &&
         !record->file_index_high &&
         !record->file_index_low) ||
        (directory &&
         record->runtime_source_type !=
             DLUMA_PROCESS_SOURCE_RUNTIME_DIRECTORY) ||
        (pack &&
         record->runtime_source_type !=
             DLUMA_PROCESS_SOURCE_RUNTIME_PACK))
        return 0;
    if (pack &&
        (!record->file_size ||
         !dluma_process_source_hex_fingerprint(
             record->content_sha256)))
        return 0;
    if (record->owner_kind ==
            DLUMA_SOURCE_OWNER_NATIVE_COMPANION_PACK &&
        (!dluma_process_source_hex_fingerprint(
             record->approval_fingerprint) ||
         !dluma_process_source_hex_fingerprint(
             record->manifest_fingerprint) ||
         !dluma_process_source_native_admission_valid(record)))
        return 0;
    if (generic_companion &&
        (!dluma_process_source_hex_fingerprint(
             record->content_sha256) ||
         !dluma_process_source_hex_fingerprint(
             record->approval_fingerprint) ||
         !dluma_process_source_hex_fingerprint(
             record->manifest_fingerprint)))
        return 0;
    if ((custom_main_menu || asset_mod) &&
        !dluma_process_source_hex_fingerprint(
            record->content_sha256))
        return 0;
    return 1;
}

static int dluma_process_source_same_mount(
    const DlumaProcessLifetimeSourceRecord* left,
    const DlumaProcessLifetimeSourceRecord* right)
{
    char left_path[DLUMA_PROCESS_SOURCE_PATH_CAPACITY];
    char right_path[DLUMA_PROCESS_SOURCE_PATH_CAPACITY];
    if (!left || !right) return 0;
    if (!dluma_process_lifetime_source_normalize_path(
            left->requested_mount_path, left_path,
            (unsigned int)sizeof(left_path)) ||
        !dluma_process_lifetime_source_normalize_path(
            right->requested_mount_path, right_path,
            (unsigned int)sizeof(right_path)))
        return 0;
    return left->owner_kind == right->owner_kind &&
        dluma_process_source_text_equal(
            left->owner_id, right->owner_id) &&
        dluma_process_source_text_equal(left_path, right_path) &&
        left->source_object == right->source_object &&
        left->runtime_source_type == right->runtime_source_type &&
        left->source_flags == right->source_flags &&
        left->volume_serial == right->volume_serial &&
        left->file_index_high == right->file_index_high &&
        left->file_index_low == right->file_index_low &&
        left->file_size == right->file_size &&
        dluma_process_source_text_equal(
            left->content_sha256, right->content_sha256) &&
        dluma_process_source_text_equal(
            left->approval_fingerprint,
            right->approval_fingerprint) &&
        dluma_process_source_text_equal(
            left->manifest_fingerprint,
            right->manifest_fingerprint) &&
        (left->owner_kind !=
             DLUMA_SOURCE_OWNER_NATIVE_COMPANION_PACK ||
         dluma_process_source_native_admission_same(
             &left->native_admission,
             &right->native_admission, 0));
}

static void dluma_process_source_commit_record(
    DlumaProcessLifetimeSourceRegistry* registry,
    DlumaProcessLifetimeSourceRecord* destination,
    const DlumaProcessLifetimeSourceRecord* source,
    ULONGLONG registry_generation_floor)
{
    if (!registry || !destination || !source)
        return;
    *destination = *source;
    destination->intrusive_link = 0;
    destination->list_ordinal = 0;
    destination->observed_source_path[0] = 0;
    destination->registry_generation = 0;
    destination->registry_generation_floor =
        registry_generation_floor;
    destination->retirement_registry_generation = 0;
    destination->exact_source_joined = 0;
    destination->lifecycle_state =
        DLUMA_PROCESS_SOURCE_STATE_REGISTERED;
    registry->next_mount_generation =
        dluma_process_source_increment(
            registry->next_mount_generation);
    destination->mount_generation =
        registry->next_mount_generation;
    if (destination->owner_kind ==
            DLUMA_SOURCE_OWNER_NATIVE_COMPANION_PACK) {
        destination->native_admission.source_object =
            destination->source_object;
        destination->native_admission.source_runtime_type =
            destination->runtime_source_type;
        destination->native_admission.source_flags =
            destination->source_flags;
        destination->native_admission.mount_generation =
            destination->mount_generation;
        destination->native_admission.source_record_committed = 1U;
        destination->native_admission.owner_snapshot_published = 0U;
    }
}

static int dluma_process_source_fill_generation_token(
    const DlumaProcessLifetimeSourceRecord* record,
    DlumaProcessLifetimeSourceGenerationToken* token_out)
{
    if (!record || !token_out)
        return 0;
    memset(token_out, 0, sizeof(*token_out));
    token_out->owner_kind = record->owner_kind;
    if (!dluma_process_source_copy(
            token_out->owner_id,
            (unsigned int)sizeof(token_out->owner_id),
            record->owner_id)) {
        memset(token_out, 0, sizeof(*token_out));
        return 0;
    }
    token_out->source_object = record->source_object;
    token_out->mount_generation = record->mount_generation;
    token_out->registry_generation = record->registry_generation;
    token_out->retirement_registry_generation =
        record->retirement_registry_generation;
    return 1;
}

void dluma_process_lifetime_source_registry_initialize(
    DlumaProcessLifetimeSourceRegistry* registry)
{
    if (registry)
        memset(registry, 0, sizeof(*registry));
}

int dluma_process_lifetime_source_register_in(
    DlumaProcessLifetimeSourceRegistry* registry,
    const DlumaProcessLifetimeSourceRecord* record,
    unsigned int* record_index_out)
{
    unsigned int index;
    int result = 0;
    if (record_index_out) *record_index_out = 0;
    if (!registry || !dluma_process_source_record_valid(record))
        return 0;
    dluma_process_source_lock(registry);
    for (index = 0; index < registry->count; ++index) {
        DlumaProcessLifetimeSourceRecord* existing =
            &registry->records[index];
        if (existing->source_object == record->source_object ||
            (existing->owner_kind == record->owner_kind &&
             dluma_process_source_text_equal(
                 existing->owner_id, record->owner_id))) {
            result =
                existing->lifecycle_state !=
                    DLUMA_PROCESS_SOURCE_STATE_RETIRED &&
                dluma_process_source_same_mount(existing, record);
            if (result && record_index_out)
                *record_index_out = index;
            goto done;
        }
    }
    if (registry->count >=
            DLUMA_PROCESS_LIFETIME_SOURCE_CAPACITY)
        goto done;
    index = registry->count;
    dluma_process_source_commit_record(
        registry, &registry->records[index], record, 0);
    registry->count = index + 1U;
    registry->publication_generation =
        dluma_process_source_increment(
            registry->publication_generation);
    if (record_index_out) *record_index_out = index;
    result = 1;
done:
    dluma_process_source_unlock(registry);
    return result;
}

int dluma_process_lifetime_source_bind_inventory_in(
    DlumaProcessLifetimeSourceRegistry* registry,
    DlumaProcessLifetimeSourceOwnerKind owner_kind,
    const char* owner_id,
    void* source_object,
    void* intrusive_link,
    unsigned int list_ordinal,
    const char* observed_source_path,
    unsigned int runtime_source_type,
    ULONGLONG registry_generation)
{
    unsigned int index;
    int result = 0;
    if (!registry || !dluma_process_source_owner_valid(owner_kind) ||
        !owner_id || !owner_id[0] || !source_object ||
        !intrusive_link || !list_ordinal ||
        !observed_source_path || !observed_source_path[0] ||
        runtime_source_type == DLUMA_PROCESS_SOURCE_RUNTIME_UNKNOWN ||
        !registry_generation)
        return 0;
    dluma_process_source_lock(registry);
    for (index = 0; index < registry->count; ++index) {
        DlumaProcessLifetimeSourceRecord* record =
            &registry->records[index];
        if (record->owner_kind != owner_kind ||
            record->source_object != source_object ||
            !dluma_process_source_text_equal(
                record->owner_id, owner_id))
            continue;
        if ((record->lifecycle_state !=
                 DLUMA_PROCESS_SOURCE_STATE_REGISTERED &&
             record->lifecycle_state !=
                 DLUMA_PROCESS_SOURCE_STATE_LIVE) ||
            registry_generation <=
                record->registry_generation_floor ||
            (record->lifecycle_state ==
                 DLUMA_PROCESS_SOURCE_STATE_LIVE &&
             registry_generation < record->registry_generation) ||
            record->runtime_source_type != runtime_source_type ||
            (record->exact_source_joined &&
             record->intrusive_link != intrusive_link))
            goto done;
        if (!dluma_process_source_copy(
                record->observed_source_path,
                (unsigned int)sizeof(
                    record->observed_source_path),
                observed_source_path))
            goto done;
        record->intrusive_link = intrusive_link;
        record->list_ordinal = list_ordinal;
        record->registry_generation = registry_generation;
        record->exact_source_joined = 1;
        record->lifecycle_state = DLUMA_PROCESS_SOURCE_STATE_LIVE;
        registry->publication_generation =
            dluma_process_source_increment(
                registry->publication_generation);
        result = 1;
        goto done;
    }
done:
    dluma_process_source_unlock(registry);
    return result;
}

int dluma_process_lifetime_source_live_presence_in(
    DlumaProcessLifetimeSourceRegistry* registry,
    DlumaProcessLifetimeSourceOwnerKind owner_kind,
    const char* owner_id,
    void* source_object,
    ULONGLONG mount_generation,
    ULONGLONG registry_generation,
    DlumaProcessLifetimeSourceGenerationToken* token_out)
{
    unsigned int index;
    int result = 0;
    if (token_out) memset(token_out, 0, sizeof(*token_out));
    if (!registry || !dluma_process_source_owner_valid(owner_kind) ||
        !owner_id || !owner_id[0] || !source_object ||
        !mount_generation || !registry_generation)
        return 0;
    dluma_process_source_lock(registry);
    for (index = 0; index < registry->count; ++index) {
        const DlumaProcessLifetimeSourceRecord* record =
            &registry->records[index];
        if (record->owner_kind != owner_kind ||
            !dluma_process_source_text_equal(
                record->owner_id, owner_id) ||
            record->source_object != source_object ||
            record->mount_generation != mount_generation ||
            record->registry_generation != registry_generation)
            continue;
        if (record->lifecycle_state !=
                DLUMA_PROCESS_SOURCE_STATE_LIVE ||
            !record->exact_source_joined ||
            registry_generation <=
                record->registry_generation_floor)
            goto done;
        if (token_out && !dluma_process_source_fill_generation_token(
                record, token_out))
            goto done;
        result = 1;
        goto done;
    }
done:
    dluma_process_source_unlock(registry);
    return result;
}

int dluma_process_lifetime_source_retire_in(
    DlumaProcessLifetimeSourceRegistry* registry,
    DlumaProcessLifetimeSourceOwnerKind owner_kind,
    const char* owner_id,
    void* source_object,
    ULONGLONG mount_generation,
    ULONGLONG registry_generation,
    ULONGLONG retirement_registry_generation,
    DlumaProcessLifetimeSourceGenerationToken* token_out)
{
    unsigned int index;
    int result = 0;
    if (token_out) memset(token_out, 0, sizeof(*token_out));
    if (!registry || !dluma_process_source_owner_valid(owner_kind) ||
        !owner_id || !owner_id[0] || !source_object ||
        !mount_generation || !registry_generation ||
        retirement_registry_generation <= registry_generation)
        return 0;
    dluma_process_source_lock(registry);
    for (index = 0; index < registry->count; ++index) {
        DlumaProcessLifetimeSourceRecord* record =
            &registry->records[index];
        if (record->owner_kind != owner_kind ||
            !dluma_process_source_text_equal(
                record->owner_id, owner_id) ||
            record->source_object != source_object ||
            record->mount_generation != mount_generation ||
            record->registry_generation != registry_generation)
            continue;
        if (record->lifecycle_state !=
                DLUMA_PROCESS_SOURCE_STATE_LIVE ||
            !record->exact_source_joined ||
            registry_generation <=
                record->registry_generation_floor)
            goto done;
        record->lifecycle_state =
            DLUMA_PROCESS_SOURCE_STATE_RETIRED;
        record->retirement_registry_generation =
            retirement_registry_generation;
        record->registry_generation_floor =
            retirement_registry_generation;
        record->intrusive_link = 0;
        record->list_ordinal = 0;
        record->exact_source_joined = 0;
        registry->publication_generation =
            dluma_process_source_increment(
                registry->publication_generation);
        if (token_out && !dluma_process_source_fill_generation_token(
                record, token_out))
            goto done;
        result = 1;
        goto done;
    }
done:
    dluma_process_source_unlock(registry);
    return result;
}

int dluma_process_lifetime_source_compare_replace_in(
    DlumaProcessLifetimeSourceRegistry* registry,
    const DlumaProcessLifetimeSourceGenerationToken* retired_generation,
    const DlumaProcessLifetimeSourceRecord* replacement,
    unsigned int* record_index_out)
{
    unsigned int index;
    unsigned int collision_index;
    int result = 0;
    if (record_index_out) *record_index_out = 0;
    if (!registry || !retired_generation ||
        !dluma_process_source_record_valid(replacement) ||
        !dluma_process_source_owner_valid(
            retired_generation->owner_kind) ||
        !retired_generation->owner_id[0] ||
        !retired_generation->source_object ||
        !retired_generation->mount_generation ||
        !retired_generation->registry_generation ||
        retired_generation->retirement_registry_generation <=
            retired_generation->registry_generation ||
        replacement->owner_kind != retired_generation->owner_kind ||
        !dluma_process_source_text_equal(
            replacement->owner_id, retired_generation->owner_id))
        return 0;
    dluma_process_source_lock(registry);
    for (index = 0; index < registry->count; ++index) {
        DlumaProcessLifetimeSourceRecord* record =
            &registry->records[index];
        if (record->owner_kind != retired_generation->owner_kind ||
            !dluma_process_source_text_equal(
                record->owner_id, retired_generation->owner_id))
            continue;
        if (record->lifecycle_state !=
                DLUMA_PROCESS_SOURCE_STATE_RETIRED ||
            record->source_object !=
                retired_generation->source_object ||
            record->mount_generation !=
                retired_generation->mount_generation ||
            record->registry_generation !=
                retired_generation->registry_generation ||
            record->retirement_registry_generation !=
                retired_generation->retirement_registry_generation ||
            record->registry_generation_floor !=
                retired_generation->retirement_registry_generation)
            goto done;
        for (collision_index = 0;
             collision_index < registry->count;
             ++collision_index) {
            const DlumaProcessLifetimeSourceRecord* collision =
                &registry->records[collision_index];
            if (collision_index != index &&
                collision->lifecycle_state !=
                    DLUMA_PROCESS_SOURCE_STATE_RETIRED &&
                collision->source_object ==
                    replacement->source_object)
                goto done;
        }
        dluma_process_source_commit_record(
            registry, record, replacement,
            retired_generation->retirement_registry_generation);
        registry->publication_generation =
            dluma_process_source_increment(
                registry->publication_generation);
        if (record_index_out) *record_index_out = index;
        result = 1;
        goto done;
    }
done:
    dluma_process_source_unlock(registry);
    return result;
}

int dluma_process_lifetime_source_snapshot_in(
    DlumaProcessLifetimeSourceRegistry* registry,
    DlumaProcessLifetimeSourceSnapshot* snapshot_out)
{
    if (!registry || !snapshot_out) return 0;
    memset(snapshot_out, 0, sizeof(*snapshot_out));
    dluma_process_source_lock(registry);
    if (registry->count <=
            DLUMA_PROCESS_LIFETIME_SOURCE_CAPACITY) {
        if (registry->count)
            memcpy(
                snapshot_out->records, registry->records,
                sizeof(registry->records[0]) *
                    registry->count);
        snapshot_out->count = registry->count;
        snapshot_out->publication_generation =
            registry->publication_generation;
        snapshot_out->complete = 1;
    }
    dluma_process_source_unlock(registry);
    return snapshot_out->complete != 0;
}

int dluma_process_lifetime_source_publish_native_admission_in(
    DlumaProcessLifetimeSourceRegistry* registry,
    DlumaProcessLifetimeSourceOwnerKind owner_kind,
    const char* owner_id,
    void* source_object,
    const DlumaNativeAdmissionIdentity* admission)
{
    unsigned int index;
    int result = 0;
    if (!registry ||
        owner_kind != DLUMA_SOURCE_OWNER_NATIVE_COMPANION_PACK ||
        !owner_id || !owner_id[0] || !source_object || !admission ||
        admission->source_object != source_object ||
        admission->source_runtime_type !=
            DLUMA_PROCESS_SOURCE_RUNTIME_PACK ||
        !admission->mount_generation ||
        !admission->source_record_committed ||
        !admission->owner_snapshot_published)
        return 0;
    dluma_process_source_lock(registry);
    for (index = 0; index < registry->count; ++index) {
        DlumaProcessLifetimeSourceRecord* record =
            &registry->records[index];
        DlumaNativeAdmissionIdentity expected;
        if (record->owner_kind != owner_kind ||
            record->source_object != source_object ||
            !dluma_process_source_text_equal(
                record->owner_id, owner_id))
            continue;
        expected = record->native_admission;
        expected.owner_snapshot_published = 1U;
        if (!dluma_process_source_native_admission_same(
                &expected, admission, 1))
            goto done;
        record->native_admission = *admission;
        registry->publication_generation =
            dluma_process_source_increment(
                registry->publication_generation);
        result = 1;
        goto done;
    }
done:
    dluma_process_source_unlock(registry);
    return result;
}

int dluma_process_lifetime_source_register(
    const DlumaProcessLifetimeSourceRecord* record,
    unsigned int* record_index_out)
{
    return dluma_process_lifetime_source_register_in(
        &g_dluma_process_lifetime_sources, record,
        record_index_out);
}

int dluma_process_lifetime_source_bind_inventory(
    DlumaProcessLifetimeSourceOwnerKind owner_kind,
    const char* owner_id,
    void* source_object,
    void* intrusive_link,
    unsigned int list_ordinal,
    const char* observed_source_path,
    unsigned int runtime_source_type,
    ULONGLONG registry_generation)
{
    return dluma_process_lifetime_source_bind_inventory_in(
        &g_dluma_process_lifetime_sources, owner_kind,
        owner_id, source_object, intrusive_link, list_ordinal,
        observed_source_path, runtime_source_type,
        registry_generation);
}

int dluma_process_lifetime_source_live_presence(
    DlumaProcessLifetimeSourceOwnerKind owner_kind,
    const char* owner_id,
    void* source_object,
    ULONGLONG mount_generation,
    ULONGLONG registry_generation,
    DlumaProcessLifetimeSourceGenerationToken* token_out)
{
    return dluma_process_lifetime_source_live_presence_in(
        &g_dluma_process_lifetime_sources, owner_kind, owner_id,
        source_object, mount_generation, registry_generation,
        token_out);
}

int dluma_process_lifetime_source_retire(
    DlumaProcessLifetimeSourceOwnerKind owner_kind,
    const char* owner_id,
    void* source_object,
    ULONGLONG mount_generation,
    ULONGLONG registry_generation,
    ULONGLONG retirement_registry_generation,
    DlumaProcessLifetimeSourceGenerationToken* token_out)
{
    return dluma_process_lifetime_source_retire_in(
        &g_dluma_process_lifetime_sources, owner_kind, owner_id,
        source_object, mount_generation, registry_generation,
        retirement_registry_generation, token_out);
}

int dluma_process_lifetime_source_compare_replace(
    const DlumaProcessLifetimeSourceGenerationToken* retired_generation,
    const DlumaProcessLifetimeSourceRecord* replacement,
    unsigned int* record_index_out)
{
    return dluma_process_lifetime_source_compare_replace_in(
        &g_dluma_process_lifetime_sources, retired_generation,
        replacement, record_index_out);
}

int dluma_process_lifetime_sources_snapshot(
    DlumaProcessLifetimeSourceSnapshot* snapshot_out)
{
    return dluma_process_lifetime_source_snapshot_in(
        &g_dluma_process_lifetime_sources, snapshot_out);
}

int dluma_process_lifetime_source_publish_native_admission(
    DlumaProcessLifetimeSourceOwnerKind owner_kind,
    const char* owner_id,
    void* source_object,
    const DlumaNativeAdmissionIdentity* admission)
{
    return dluma_process_lifetime_source_publish_native_admission_in(
        &g_dluma_process_lifetime_sources, owner_kind, owner_id,
        source_object, admission);
}

int dluma_process_lifetime_source_normalize_path(
    const char* input, char* output, unsigned int capacity)
{
    unsigned int input_index = 0;
    unsigned int output_index = 0;
    unsigned int segment_begin = 0;
    int preserve_unc = 0;
    int last_separator = 0;
    if (!input || !output || capacity < 2U) return 0;
    output[0] = 0;
    /*
     * The supported engine build can expose either the previously rendered
     * one-leading-backslash pseudo form or the now byte-proven, slash-folded
     * standard extended prefix:
     *
     *   \?\X:\...
     *   //?/X:/...
     *
     * Neither is a generic namespace. Keep both matches literal and
     * drive-only; the ordinary loop below folds the accepted drive path to
     * the same canonical representation.
     */
    if (input[0] == '/' && input[1] == '/' &&
        input[2] == '?' && input[3] == '/') {
        if (!dluma_process_source_is_drive_letter(input[4]) ||
            input[5] != ':' || input[6] != '/')
            return 0;
        input_index = 4U;
    } else if (input[0] == '\\' && input[1] == '?' &&
        input[2] == '\\') {
        if (!dluma_process_source_is_drive_letter(input[3]) ||
            input[4] != ':' || input[5] != '\\')
            return 0;
        input_index = 3U;
    } else if (input[0] == '\\' && input[1] == '\\' &&
        input[2] == '?' && input[3] == '\\') {
        if ((input[4] == 'u' || input[4] == 'U') &&
            (input[5] == 'n' || input[5] == 'N') &&
            (input[6] == 'c' || input[6] == 'C') &&
            input[7] == '\\') {
            preserve_unc = 1;
            input_index = 8U;
            if (capacity < 3U) return 0;
            output[output_index++] = '\\';
            output[output_index++] = '\\';
            last_separator = 1;
        } else {
            if (!dluma_process_source_is_drive_letter(input[4]) ||
                input[5] != ':' || input[6] != '\\')
                return 0;
            input_index = 4U;
        }
    } else if (input[0] == '\\' && input[1] == '\\') {
        preserve_unc = 1;
        output[output_index++] = '\\';
        output[output_index++] = '\\';
        input_index = 2U;
        last_separator = 1;
    }
    while (input[input_index]) {
        if (dluma_process_source_is_separator(input[input_index]) &&
            input[input_index + 1U] == '?' &&
            dluma_process_source_is_separator(
                input[input_index + 2U])) {
            output[0] = 0;
            return 0;
        }
        char value =
            dluma_process_source_lower(input[input_index++]);
        if (value == '/') value = '\\';
        if (value == ':' &&
            (!output_index || output_index != 1U ||
             preserve_unc)) {
            output[0] = 0;
            return 0;
        }
        if (value == '\\') {
            if (last_separator) continue;
            last_separator = 1;
        } else {
            last_separator = 0;
        }
        if (output_index + 1U >= capacity) {
            output[0] = 0;
            return 0;
        }
        output[output_index++] = value;
    }
    while (output_index > (preserve_unc ? 2U : 0U) &&
           output[output_index - 1U] == '\\' &&
           !(output_index == 3U && output[1] == ':'))
        --output_index;
    output[output_index] = 0;
    if (!output_index) return 0;
    if (preserve_unc) {
        unsigned int separator_count = 0;
        unsigned int index;
        for (index = 2U; index < output_index; ++index)
            if (output[index] == '\\')
                ++separator_count;
        if (output_index <= 2U || output[2] == '\\' ||
            separator_count < 1U ||
            output[output_index - 1U] == '\\') {
            output[0] = 0;
            return 0;
        }
    } else if (output_index < 3U ||
               !dluma_process_source_is_drive_letter(output[0]) ||
               output[1] != ':' || output[2] != '\\') {
        output[0] = 0;
        return 0;
    }
    segment_begin = preserve_unc ? 2U : 0U;
    for (input_index = segment_begin;
         input_index <= output_index; ++input_index) {
        unsigned int length;
        if (input_index != output_index &&
            output[input_index] != '\\')
            continue;
        length = input_index - segment_begin;
        if ((length == 1U &&
             output[segment_begin] == '.') ||
            (length == 2U &&
             output[segment_begin] == '.' &&
             output[segment_begin + 1U] == '.')) {
            output[0] = 0;
            return 0;
        }
        segment_begin = input_index + 1U;
    }
    return 1;
}

const char* dluma_process_lifetime_source_owner_name(
    DlumaProcessLifetimeSourceOwnerKind owner_kind)
{
    switch (owner_kind) {
    case DLUMA_SOURCE_OWNER_LOADER_PAK:
        return "loader_pak";
    case DLUMA_SOURCE_OWNER_LOADER_DEVELOPER_MODE:
        return "loader_developer_mode";
    case DLUMA_SOURCE_OWNER_LOADER_THUMBNAILS:
        return "loader_thumbnails";
    case DLUMA_SOURCE_OWNER_NATIVE_COMPANION_PACK:
        return "native_companion_pack";
    case DLUMA_SOURCE_OWNER_MOD_COMPANION_CONTENT:
        return "mod_companion_content";
    case DLUMA_SOURCE_OWNER_CUSTOM_MAIN_MENU:
        return "custom_main_menu";
    case DLUMA_SOURCE_OWNER_ASSET_MOD:
        return "asset_mod";
    default:
        return "none";
    }
}
