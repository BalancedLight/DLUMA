#include "../src/dlw_process_lifetime_sources.h"

#include <stdio.h>
#include <stdint.h>
#include <string.h>

#define EXPECT(expression)                                                \
    do {                                                                  \
        if (!(expression)) {                                              \
            fprintf(stderr, "failure at line %d: %s\n", __LINE__,       \
                    #expression);                                         \
            return 0;                                                     \
        }                                                                 \
    } while (0)

typedef struct MenuSourceFixture {
    const char* owner_id;
    const char* relative_path;
    void* source_object;
    void* intrusive_link;
    unsigned int source_flags;
} MenuSourceFixture;

static void fill_hex(char output[65], char value)
{
    unsigned int index;
    for (index = 0; index < 64U; ++index)
        output[index] = value;
    output[64] = 0;
}

static DlumaProcessLifetimeSourceRecord make_menu_source(
    const MenuSourceFixture* fixture)
{
    DlumaProcessLifetimeSourceRecord record;
    int written;
    memset(&record, 0, sizeof(record));
    record.owner_kind = DLUMA_SOURCE_OWNER_CUSTOM_MAIN_MENU;
    strcpy(record.owner_id, fixture->owner_id);
    record.source_object = fixture->source_object;
    written = snprintf(
        record.requested_mount_path,
        sizeof(record.requested_mount_path),
        "X:\\SyntheticWorkshop\\Menu\\%s",
        fixture->relative_path);
    if (written <= 0 ||
        (unsigned int)written >= sizeof(record.requested_mount_path))
        record.requested_mount_path[0] = 0;
    record.runtime_source_type =
        DLUMA_PROCESS_SOURCE_RUNTIME_DIRECTORY;
    record.source_flags = fixture->source_flags;
    record.volume_serial = 17U;
    record.file_index_low =
        (ULONGLONG)(uintptr_t)fixture->source_object;
    fill_hex(record.content_sha256, 'a');
    record.metadata_complete = 1U;
    return record;
}

static int register_and_bind(
    DlumaProcessLifetimeSourceRegistry* registry,
    const MenuSourceFixture* fixture,
    ULONGLONG registry_generation,
    unsigned int* record_index_out)
{
    DlumaProcessLifetimeSourceRecord record =
        make_menu_source(fixture);
    return dluma_process_lifetime_source_register_in(
               registry, &record, record_index_out) &&
           dluma_process_lifetime_source_bind_inventory_in(
               registry, record.owner_kind, record.owner_id,
               record.source_object, fixture->intrusive_link,
               (unsigned int)registry_generation,
               record.requested_mount_path,
               record.runtime_source_type,
               registry_generation);
}

static int require_live(
    DlumaProcessLifetimeSourceRegistry* registry,
    unsigned int record_index,
    DlumaProcessLifetimeSourceGenerationToken* token_out)
{
    const DlumaProcessLifetimeSourceRecord* record =
        &registry->records[record_index];
    return dluma_process_lifetime_source_live_presence_in(
        registry, record->owner_kind, record->owner_id,
        record->source_object, record->mount_generation,
        record->registry_generation, token_out);
}

static int retire_source(
    DlumaProcessLifetimeSourceRegistry* registry,
    unsigned int record_index,
    ULONGLONG retirement_registry_generation,
    DlumaProcessLifetimeSourceGenerationToken* token_out)
{
    const DlumaProcessLifetimeSourceRecord record =
        registry->records[record_index];
    return dluma_process_lifetime_source_retire_in(
        registry, record.owner_kind, record.owner_id,
        record.source_object, record.mount_generation,
        record.registry_generation,
        retirement_registry_generation, token_out);
}

static int replace_and_bind(
    DlumaProcessLifetimeSourceRegistry* registry,
    const DlumaProcessLifetimeSourceGenerationToken* retired,
    const MenuSourceFixture* replacement_fixture,
    ULONGLONG rejected_registry_generation,
    ULONGLONG live_registry_generation,
    unsigned int* record_index_out)
{
    DlumaProcessLifetimeSourceRecord replacement =
        make_menu_source(replacement_fixture);
    unsigned int record_index = 99U;
    EXPECT(dluma_process_lifetime_source_compare_replace_in(
        registry, retired, &replacement, &record_index));
    EXPECT(record_index < registry->count);
    EXPECT(registry->records[record_index].lifecycle_state ==
        DLUMA_PROCESS_SOURCE_STATE_REGISTERED);
    EXPECT(registry->records[record_index].mount_generation !=
        retired->mount_generation);
    EXPECT(registry->records[record_index].registry_generation_floor ==
        retired->retirement_registry_generation);
    EXPECT(!dluma_process_lifetime_source_bind_inventory_in(
        registry, replacement.owner_kind, replacement.owner_id,
        replacement.source_object,
        replacement_fixture->intrusive_link,
        (unsigned int)rejected_registry_generation,
        replacement.requested_mount_path,
        replacement.runtime_source_type,
        rejected_registry_generation));
    EXPECT(dluma_process_lifetime_source_bind_inventory_in(
        registry, replacement.owner_kind, replacement.owner_id,
        replacement.source_object,
        replacement_fixture->intrusive_link,
        (unsigned int)live_registry_generation,
        replacement.requested_mount_path,
        replacement.runtime_source_type,
        live_registry_generation));
    if (record_index_out)
        *record_index_out = record_index;
    return 1;
}

static int test_natural_removal_retires_exact_generation(void)
{
    DlumaProcessLifetimeSourceRegistry registry;
    DlumaProcessLifetimeSourceGenerationToken live;
    DlumaProcessLifetimeSourceGenerationToken retired;
    DlumaProcessLifetimeSourceSnapshot snapshot;
    unsigned int index = 99U;
    const MenuSourceFixture project = {
        DLUMA_SOURCE_OWNER_ID_CUSTOM_MAIN_MENU_PROJECT,
        "project", (void*)0x10000, (void*)0x11000, 8U};

    dluma_process_lifetime_source_registry_initialize(&registry);
    EXPECT(register_and_bind(&registry, &project, 10U, &index));
    EXPECT(require_live(&registry, index, &live));
    EXPECT(!dluma_process_lifetime_source_retire_in(
        &registry, live.owner_kind, live.owner_id,
        live.source_object, live.mount_generation,
        live.registry_generation, live.registry_generation,
        &retired));
    EXPECT(retire_source(&registry, index, 11U, &retired));
    EXPECT(retired.mount_generation == live.mount_generation);
    EXPECT(retired.registry_generation == live.registry_generation);
    EXPECT(retired.retirement_registry_generation == 11U);
    EXPECT(!dluma_process_lifetime_source_live_presence_in(
        &registry, live.owner_kind, live.owner_id,
        live.source_object, live.mount_generation,
        live.registry_generation, 0));
    EXPECT(dluma_process_lifetime_source_snapshot_in(
        &registry, &snapshot));
    EXPECT(snapshot.records[index].lifecycle_state ==
        DLUMA_PROCESS_SOURCE_STATE_RETIRED);
    EXPECT(!snapshot.records[index].exact_source_joined);
    EXPECT(!snapshot.records[index].intrusive_link);
    EXPECT(snapshot.records[index].requested_mount_path[0]);
    return 1;
}

static int test_pointer_reuse_requires_exact_compare_replace(void)
{
    DlumaProcessLifetimeSourceRegistry registry;
    DlumaProcessLifetimeSourceGenerationToken captured_before_remove;
    DlumaProcessLifetimeSourceGenerationToken retired;
    DlumaProcessLifetimeSourceGenerationToken replacement_live;
    DlumaProcessLifetimeSourceRecord stale_replacement;
    unsigned int index = 99U;
    const MenuSourceFixture original = {
        DLUMA_SOURCE_OWNER_ID_CUSTOM_MAIN_MENU_DATA,
        "project\\data", (void*)0x20000, (void*)0x21000, 8U};
    const MenuSourceFixture reused_address = {
        DLUMA_SOURCE_OWNER_ID_CUSTOM_MAIN_MENU_DATA,
        "project\\data", (void*)0x20000, (void*)0x22000, 8U};

    dluma_process_lifetime_source_registry_initialize(&registry);
    EXPECT(register_and_bind(&registry, &original, 20U, &index));
    EXPECT(require_live(
        &registry, index, &captured_before_remove));
    EXPECT(retire_source(&registry, index, 25U, &retired));
    EXPECT(replace_and_bind(
        &registry, &retired, &reused_address,
        retired.retirement_registry_generation, 26U, &index));
    EXPECT(require_live(&registry, index, &replacement_live));
    EXPECT(replacement_live.source_object == retired.source_object);
    EXPECT(replacement_live.mount_generation !=
        retired.mount_generation);
    EXPECT(!dluma_process_lifetime_source_retire_in(
        &registry, captured_before_remove.owner_kind,
        captured_before_remove.owner_id,
        captured_before_remove.source_object,
        captured_before_remove.mount_generation,
        captured_before_remove.registry_generation, 27U, 0));
    EXPECT(require_live(&registry, index, &replacement_live));
    stale_replacement = make_menu_source(&reused_address);
    EXPECT(!dluma_process_lifetime_source_compare_replace_in(
        &registry, &retired, &stale_replacement, 0));
    return 1;
}

static int test_nested_remove_cannot_retire_replacement(void)
{
    DlumaProcessLifetimeSourceRegistry registry;
    DlumaProcessLifetimeSourceGenerationToken outer_capture;
    DlumaProcessLifetimeSourceGenerationToken nested_retired;
    DlumaProcessLifetimeSourceGenerationToken replacement_live;
    unsigned int index = 99U;
    const MenuSourceFixture original = {
        DLUMA_SOURCE_OWNER_ID_CUSTOM_MAIN_MENU_PROJECT,
        "project", (void*)0x23000, (void*)0x24000, 8U};
    const MenuSourceFixture replacement = {
        DLUMA_SOURCE_OWNER_ID_CUSTOM_MAIN_MENU_PROJECT,
        "project", (void*)0x25000, (void*)0x26000, 8U};

    dluma_process_lifetime_source_registry_initialize(&registry);
    EXPECT(register_and_bind(&registry, &original, 30U, &index));
    /* The outer remove hook captures this tuple before calling the engine. */
    EXPECT(require_live(&registry, index, &outer_capture));
    /* A nested remove retires it and replacement joins before outer return. */
    EXPECT(retire_source(&registry, index, 31U, &nested_retired));
    EXPECT(replace_and_bind(
        &registry, &nested_retired, &replacement,
        nested_retired.retirement_registry_generation, 32U, &index));
    EXPECT(require_live(&registry, index, &replacement_live));
    EXPECT(replacement_live.source_object != outer_capture.source_object);
    EXPECT(!dluma_process_lifetime_source_retire_in(
        &registry, outer_capture.owner_kind, outer_capture.owner_id,
        outer_capture.source_object, outer_capture.mount_generation,
        outer_capture.registry_generation, 33U, 0));
    EXPECT(require_live(&registry, index, &replacement_live));
    return 1;
}

static int test_minimal_map_return_restores_removed_roots(void)
{
    DlumaProcessLifetimeSourceRegistry registry;
    DlumaProcessLifetimeSourceGenerationToken project_retired;
    DlumaProcessLifetimeSourceGenerationToken data_retired;
    DlumaProcessLifetimeSourceGenerationToken live;
    unsigned int project_index;
    unsigned int data_index;
    unsigned int assets_index;
    unsigned int overlay_index;
    const MenuSourceFixture project = {
        DLUMA_SOURCE_OWNER_ID_CUSTOM_MAIN_MENU_PROJECT,
        "project", (void*)0x30000, (void*)0x31000, 8U};
    const MenuSourceFixture data = {
        DLUMA_SOURCE_OWNER_ID_CUSTOM_MAIN_MENU_DATA,
        "project\\data", (void*)0x32000, (void*)0x33000, 8U};
    const MenuSourceFixture assets = {
        DLUMA_SOURCE_OWNER_ID_CUSTOM_MAIN_MENU_ASSETS_PC,
        "project\\assets_pc", (void*)0x34000, (void*)0x35000, 8U};
    const MenuSourceFixture overlay = {
        DLUMA_SOURCE_OWNER_ID_CUSTOM_MAIN_MENU_OVERLAY,
        "runtime\\overlay", (void*)0x36000, (void*)0x37000, 9U};
    const MenuSourceFixture project_return = {
        DLUMA_SOURCE_OWNER_ID_CUSTOM_MAIN_MENU_PROJECT,
        "project", (void*)0x38000, (void*)0x39000, 8U};
    const MenuSourceFixture data_return = {
        DLUMA_SOURCE_OWNER_ID_CUSTOM_MAIN_MENU_DATA,
        "project\\data", (void*)0x3A000, (void*)0x3B000, 8U};

    dluma_process_lifetime_source_registry_initialize(&registry);
    EXPECT(register_and_bind(
        &registry, &project, 30U, &project_index));
    EXPECT(register_and_bind(&registry, &data, 31U, &data_index));
    EXPECT(register_and_bind(
        &registry, &assets, 32U, &assets_index));
    EXPECT(register_and_bind(
        &registry, &overlay, 33U, &overlay_index));
    EXPECT(retire_source(
        &registry, project_index, 40U, &project_retired));
    EXPECT(retire_source(
        &registry, data_index, 41U, &data_retired));
    EXPECT(require_live(&registry, assets_index, &live));
    EXPECT(require_live(&registry, overlay_index, &live));
    EXPECT(replace_and_bind(
        &registry, &project_retired, &project_return,
        40U, 50U, &project_index));
    EXPECT(replace_and_bind(
        &registry, &data_retired, &data_return,
        41U, 51U, &data_index));
    EXPECT(require_live(&registry, project_index, &live));
    EXPECT(require_live(&registry, data_index, &live));
    EXPECT(require_live(&registry, assets_index, &live));
    EXPECT(require_live(&registry, overlay_index, &live));
    return 1;
}

static int test_asset_heavy_map_return_restores_all_native_roots(void)
{
    DlumaProcessLifetimeSourceRegistry registry;
    DlumaProcessLifetimeSourceGenerationToken retired[3];
    DlumaProcessLifetimeSourceGenerationToken live;
    unsigned int indexes[4];
    unsigned int index;
    const MenuSourceFixture initial[4] = {
        {DLUMA_SOURCE_OWNER_ID_CUSTOM_MAIN_MENU_PROJECT,
         "project", (void*)0x40000, (void*)0x41000, 8U},
        {DLUMA_SOURCE_OWNER_ID_CUSTOM_MAIN_MENU_DATA,
         "project\\data", (void*)0x42000, (void*)0x43000, 8U},
        {DLUMA_SOURCE_OWNER_ID_CUSTOM_MAIN_MENU_ASSETS_PC,
         "project\\assets_pc", (void*)0x44000, (void*)0x45000, 8U},
        {DLUMA_SOURCE_OWNER_ID_CUSTOM_MAIN_MENU_OVERLAY,
         "runtime\\overlay", (void*)0x46000, (void*)0x47000, 9U}};
    const MenuSourceFixture restored[3] = {
        {DLUMA_SOURCE_OWNER_ID_CUSTOM_MAIN_MENU_PROJECT,
         "project", (void*)0x40000, (void*)0x48000, 8U},
        {DLUMA_SOURCE_OWNER_ID_CUSTOM_MAIN_MENU_DATA,
         "project\\data", (void*)0x4A000, (void*)0x4B000, 8U},
        {DLUMA_SOURCE_OWNER_ID_CUSTOM_MAIN_MENU_ASSETS_PC,
         "project\\assets_pc", (void*)0x4C000, (void*)0x4D000, 8U}};

    dluma_process_lifetime_source_registry_initialize(&registry);
    for (index = 0; index < 4U; ++index)
        EXPECT(register_and_bind(
            &registry, &initial[index], 60U + index,
            &indexes[index]));
    for (index = 0; index < 3U; ++index)
        EXPECT(retire_source(
            &registry, indexes[index], 70U + index,
            &retired[index]));
    for (index = 0; index < 3U; ++index)
        EXPECT(replace_and_bind(
            &registry, &retired[index], &restored[index],
            70U + index, 80U + index, &indexes[index]));
    for (index = 0; index < 4U; ++index)
        EXPECT(require_live(&registry, indexes[index], &live));
    return 1;
}

int main(void)
{
    if (!test_natural_removal_retires_exact_generation() ||
        !test_pointer_reuse_requires_exact_compare_replace() ||
        !test_nested_remove_cannot_retire_replacement() ||
        !test_minimal_map_return_restores_removed_roots() ||
        !test_asset_heavy_map_return_restores_all_native_roots())
        return 1;
    puts("custom main-menu return-source fake harness passed");
    return 0;
}
