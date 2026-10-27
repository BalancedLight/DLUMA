#include "../src/dlw_process_lifetime_sources.h"

#include <stdio.h>
#include <string.h>

#define EXPECT(expression)                                                \
    do {                                                                  \
        if (!(expression)) {                                              \
            fprintf(stderr, "failure at line %d: %s\n", __LINE__,        \
                    #expression);                                         \
            return 0;                                                     \
        }                                                                 \
    } while (0)

static void fill_hex(char output[65], char value)
{
    unsigned int index;
    for (index = 0; index < 64U; ++index)
        output[index] = value;
    output[64] = 0;
}

static DlumaProcessLifetimeSourceRecord make_loader(void)
{
    DlumaProcessLifetimeSourceRecord record;
    memset(&record, 0, sizeof(record));
    record.owner_kind = DLUMA_SOURCE_OWNER_LOADER_PAK;
    strcpy(record.owner_id, DLUMA_SOURCE_OWNER_ID_LOADER_PAK);
    record.source_object = (void*)0x10000;
    strcpy(
        record.requested_mount_path,
        "X:\\workshop\\3771245504\\mods\\dluma.pak");
    record.runtime_source_type = DLUMA_PROCESS_SOURCE_RUNTIME_PACK;
    record.source_flags = 9U;
    record.volume_serial = 11U;
    record.file_index_high = 12U;
    record.file_index_low = 13U;
    record.file_size = 4096U;
    fill_hex(record.content_sha256, 'a');
    record.metadata_complete = 1U;
    return record;
}

static int test_register_and_exact_bind(void)
{
    DlumaProcessLifetimeSourceRegistry registry;
    DlumaProcessLifetimeSourceRecord loader = make_loader();
    DlumaProcessLifetimeSourceSnapshot snapshot;
    unsigned int index = 99U;
    dluma_process_lifetime_source_registry_initialize(&registry);
    EXPECT(dluma_process_lifetime_source_register_in(
        &registry, &loader, &index));
    EXPECT(index == 0U);
    EXPECT(dluma_process_lifetime_source_register_in(
        &registry, &loader, &index));
    EXPECT(registry.count == 1U);
    EXPECT(dluma_process_lifetime_source_bind_inventory_in(
        &registry, loader.owner_kind, loader.owner_id,
        loader.source_object, (void*)0x20000, 49U,
        "X:\\workshop\\3771245504\\mods",
        DLUMA_PROCESS_SOURCE_RUNTIME_PACK, 55U));
    EXPECT(dluma_process_lifetime_source_snapshot_in(
        &registry, &snapshot));
    EXPECT(snapshot.complete && snapshot.count == 1U);
    EXPECT(snapshot.records[0].exact_source_joined);
    EXPECT(snapshot.records[0].source_object == loader.source_object);
    EXPECT(snapshot.records[0].intrusive_link == (void*)0x20000);
    EXPECT(snapshot.records[0].list_ordinal == 49U);
    EXPECT(snapshot.records[0].registry_generation == 55U);
    EXPECT(snapshot.records[0].mount_generation != 0U);
    EXPECT(snapshot.records[0].lifecycle_state ==
        DLUMA_PROCESS_SOURCE_STATE_LIVE);
    return 1;
}

static int test_live_retire_replace_and_aba(void)
{
    DlumaProcessLifetimeSourceRegistry registry;
    DlumaProcessLifetimeSourceRecord original = make_loader();
    DlumaProcessLifetimeSourceRecord replacement = make_loader();
    DlumaProcessLifetimeSourceSnapshot snapshot;
    DlumaProcessLifetimeSourceGenerationToken live_generation;
    DlumaProcessLifetimeSourceGenerationToken retired_generation;
    DlumaProcessLifetimeSourceGenerationToken reused_generation;
    DlumaProcessLifetimeSourceGenerationToken stale_generation;
    ULONGLONG first_mount_generation;
    ULONGLONG second_mount_generation;
    unsigned int index = 99U;

    dluma_process_lifetime_source_registry_initialize(&registry);
    EXPECT(dluma_process_lifetime_source_register_in(
        &registry, &original, &index));
    EXPECT(index == 0U);
    EXPECT(dluma_process_lifetime_source_snapshot_in(
        &registry, &snapshot));
    first_mount_generation = snapshot.records[0].mount_generation;
    EXPECT(snapshot.records[0].lifecycle_state ==
        DLUMA_PROCESS_SOURCE_STATE_REGISTERED);
    EXPECT(!dluma_process_lifetime_source_live_presence_in(
        &registry, original.owner_kind, original.owner_id,
        original.source_object, first_mount_generation, 55U,
        &live_generation));
    EXPECT(dluma_process_lifetime_source_bind_inventory_in(
        &registry, original.owner_kind, original.owner_id,
        original.source_object, (void*)0x20000, 49U,
        original.requested_mount_path,
        DLUMA_PROCESS_SOURCE_RUNTIME_PACK, 55U));
    EXPECT(dluma_process_lifetime_source_live_presence_in(
        &registry, original.owner_kind, original.owner_id,
        original.source_object, first_mount_generation, 55U,
        &live_generation));
    EXPECT(live_generation.source_object == original.source_object);
    EXPECT(live_generation.mount_generation == first_mount_generation);
    EXPECT(live_generation.registry_generation == 55U);
    EXPECT(!live_generation.retirement_registry_generation);
    EXPECT(!dluma_process_lifetime_source_live_presence_in(
        &registry, original.owner_kind, original.owner_id,
        original.source_object, first_mount_generation, 54U, 0));
    EXPECT(!dluma_process_lifetime_source_retire_in(
        &registry, original.owner_kind, original.owner_id,
        original.source_object, first_mount_generation, 55U, 55U,
        &retired_generation));
    EXPECT(!dluma_process_lifetime_source_retire_in(
        &registry, original.owner_kind, original.owner_id,
        (void*)0x10008, first_mount_generation, 55U, 56U,
        &retired_generation));
    EXPECT(dluma_process_lifetime_source_retire_in(
        &registry, original.owner_kind, original.owner_id,
        original.source_object, first_mount_generation, 55U, 56U,
        &retired_generation));
    EXPECT(retired_generation.source_object == original.source_object);
    EXPECT(retired_generation.mount_generation == first_mount_generation);
    EXPECT(retired_generation.registry_generation == 55U);
    EXPECT(retired_generation.retirement_registry_generation == 56U);
    EXPECT(!dluma_process_lifetime_source_live_presence_in(
        &registry, original.owner_kind, original.owner_id,
        original.source_object, first_mount_generation, 55U, 0));
    EXPECT(dluma_process_lifetime_source_snapshot_in(
        &registry, &snapshot));
    EXPECT(snapshot.records[0].lifecycle_state ==
        DLUMA_PROCESS_SOURCE_STATE_RETIRED);
    EXPECT(!snapshot.records[0].exact_source_joined);
    EXPECT(!snapshot.records[0].intrusive_link);
    EXPECT(snapshot.records[0].registry_generation_floor == 56U);
    EXPECT(snapshot.records[0].retirement_registry_generation == 56U);

    replacement.source_object = (void*)0x30000;
    EXPECT(!dluma_process_lifetime_source_register_in(
        &registry, &replacement, 0));
    stale_generation = retired_generation;
    stale_generation.registry_generation = 54U;
    EXPECT(!dluma_process_lifetime_source_compare_replace_in(
        &registry, &stale_generation, &replacement, 0));
    EXPECT(dluma_process_lifetime_source_compare_replace_in(
        &registry, &retired_generation, &replacement, &index));
    EXPECT(index == 0U);
    EXPECT(dluma_process_lifetime_source_snapshot_in(
        &registry, &snapshot));
    second_mount_generation = snapshot.records[0].mount_generation;
    EXPECT(second_mount_generation != first_mount_generation);
    EXPECT(snapshot.records[0].lifecycle_state ==
        DLUMA_PROCESS_SOURCE_STATE_REGISTERED);
    EXPECT(snapshot.records[0].registry_generation_floor == 56U);
    EXPECT(!dluma_process_lifetime_source_bind_inventory_in(
        &registry, replacement.owner_kind, replacement.owner_id,
        replacement.source_object, (void*)0x30008, 50U,
        replacement.requested_mount_path,
        DLUMA_PROCESS_SOURCE_RUNTIME_PACK, 56U));
    EXPECT(dluma_process_lifetime_source_bind_inventory_in(
        &registry, replacement.owner_kind, replacement.owner_id,
        replacement.source_object, (void*)0x30008, 50U,
        replacement.requested_mount_path,
        DLUMA_PROCESS_SOURCE_RUNTIME_PACK, 57U));
    EXPECT(dluma_process_lifetime_source_live_presence_in(
        &registry, replacement.owner_kind, replacement.owner_id,
        replacement.source_object, second_mount_generation, 57U,
        &live_generation));
    EXPECT(!dluma_process_lifetime_source_compare_replace_in(
        &registry, &retired_generation, &replacement, 0));

    EXPECT(dluma_process_lifetime_source_retire_in(
        &registry, replacement.owner_kind, replacement.owner_id,
        replacement.source_object, second_mount_generation, 57U, 58U,
        &reused_generation));
    EXPECT(dluma_process_lifetime_source_compare_replace_in(
        &registry, &reused_generation, &replacement, &index));
    EXPECT(dluma_process_lifetime_source_snapshot_in(
        &registry, &snapshot));
    EXPECT(snapshot.records[0].source_object ==
        replacement.source_object);
    EXPECT(snapshot.records[0].mount_generation !=
        second_mount_generation);
    EXPECT(snapshot.records[0].registry_generation_floor == 58U);
    EXPECT(!dluma_process_lifetime_source_bind_inventory_in(
        &registry, replacement.owner_kind, replacement.owner_id,
        replacement.source_object, (void*)0x30008, 50U,
        replacement.requested_mount_path,
        DLUMA_PROCESS_SOURCE_RUNTIME_PACK, 57U));
    EXPECT(!dluma_process_lifetime_source_bind_inventory_in(
        &registry, replacement.owner_kind, replacement.owner_id,
        replacement.source_object, (void*)0x30008, 50U,
        replacement.requested_mount_path,
        DLUMA_PROCESS_SOURCE_RUNTIME_PACK, 58U));
    EXPECT(dluma_process_lifetime_source_bind_inventory_in(
        &registry, replacement.owner_kind, replacement.owner_id,
        replacement.source_object, (void*)0x30008, 50U,
        replacement.requested_mount_path,
        DLUMA_PROCESS_SOURCE_RUNTIME_PACK, 59U));
    EXPECT(!dluma_process_lifetime_source_live_presence_in(
        &registry, replacement.owner_kind, replacement.owner_id,
        replacement.source_object, second_mount_generation, 57U, 0));
    EXPECT(dluma_process_lifetime_source_live_presence_in(
        &registry, replacement.owner_kind, replacement.owner_id,
        replacement.source_object,
        snapshot.records[0].mount_generation, 59U, 0));
    return 1;
}

static int test_ownership_cannot_transfer(void)
{
    DlumaProcessLifetimeSourceRegistry registry;
    DlumaProcessLifetimeSourceRecord loader = make_loader();
    DlumaProcessLifetimeSourceRecord replacement = loader;
    dluma_process_lifetime_source_registry_initialize(&registry);
    EXPECT(dluma_process_lifetime_source_register_in(
        &registry, &loader, 0));
    replacement.source_object = (void*)0x30000;
    EXPECT(!dluma_process_lifetime_source_register_in(
        &registry, &replacement, 0));
    replacement = loader;
    strcpy(
        replacement.requested_mount_path,
        "X:\\workshop\\3771245504\\mods\\replacement.pak");
    EXPECT(!dluma_process_lifetime_source_register_in(
        &registry, &replacement, 0));
    replacement = loader;
    replacement.owner_kind = DLUMA_SOURCE_OWNER_NATIVE_COMPANION_PACK;
    strcpy(replacement.owner_id, "PlayerMedia");
    fill_hex(replacement.approval_fingerprint, 'b');
    fill_hex(replacement.manifest_fingerprint, 'c');
    EXPECT(!dluma_process_lifetime_source_register_in(
        &registry, &replacement, 0));
    EXPECT(!dluma_process_lifetime_source_bind_inventory_in(
        &registry, loader.owner_kind, loader.owner_id,
        (void*)0x30000, (void*)0x40000, 1U,
        "X:\\workshop\\3771245504\\mods",
        DLUMA_PROCESS_SOURCE_RUNTIME_PACK, 1U));
    EXPECT(dluma_process_lifetime_source_bind_inventory_in(
        &registry, loader.owner_kind, loader.owner_id,
        loader.source_object, (void*)0x40000, 1U,
        "X:\\workshop\\3771245504\\mods",
        DLUMA_PROCESS_SOURCE_RUNTIME_PACK, 1U));
    EXPECT(!dluma_process_lifetime_source_bind_inventory_in(
        &registry, loader.owner_kind, loader.owner_id,
        loader.source_object, (void*)0x50000, 1U,
        "X:\\workshop\\3771245504\\mods",
        DLUMA_PROCESS_SOURCE_RUNTIME_PACK, 2U));
    EXPECT(dluma_process_lifetime_source_bind_inventory_in(
        &registry, loader.owner_kind, loader.owner_id,
        loader.source_object, (void*)0x40000, 2U,
        "X:\\workshop\\3771245504\\mods",
        DLUMA_PROCESS_SOURCE_RUNTIME_PACK, 2U));
    return 1;
}

static int test_directory_and_native_metadata(void)
{
    DlumaProcessLifetimeSourceRegistry registry;
    DlumaProcessLifetimeSourceRecord directory;
    DlumaProcessLifetimeSourceRecord native;
    DlumaProcessLifetimeSourceSnapshot snapshot;
    DlumaNativeAdmissionIdentity published;
    unsigned int native_index = 99U;
    dluma_process_lifetime_source_registry_initialize(&registry);
    memset(&directory, 0, sizeof(directory));
    directory.owner_kind = DLUMA_SOURCE_OWNER_LOADER_THUMBNAILS;
    strcpy(directory.owner_id, DLUMA_SOURCE_OWNER_ID_THUMBNAILS);
    directory.source_object = (void*)0x60000;
    strcpy(
        directory.requested_mount_path,
        "X:\\mods\\dluma_runtime\\workshop_thumbnails");
    directory.runtime_source_type =
        DLUMA_PROCESS_SOURCE_RUNTIME_DIRECTORY;
    directory.source_flags = 8U;
    directory.volume_serial = 1U;
    directory.file_index_low = 2U;
    directory.metadata_complete = 1U;
    EXPECT(dluma_process_lifetime_source_register_in(
        &registry, &directory, 0));

    native = make_loader();
    native.owner_kind = DLUMA_SOURCE_OWNER_NATIVE_COMPANION_PACK;
    strcpy(
        native.owner_id,
        "example.vehicle_audio");
    native.source_object = (void*)0x70000;
    strcpy(
        native.requested_mount_path,
        "X:\\workshop\\3773041738\\mods\\native\\global\\"
        "example.vehicle_audio\\PlayerMedia.pak");
    EXPECT(!dluma_process_lifetime_source_register_in(
        &registry, &native, 0));
    fill_hex(native.approval_fingerprint, 'b');
    fill_hex(native.manifest_fingerprint, 'c');
    strcpy(
        native.native_admission.module_id,
        native.owner_id);
    strcpy(
        native.native_admission.module_name,
        "PlayerMedia Stemmed Buggy Music");
    fill_hex(
        native.native_admission.stable_candidate_id, 'd');
    strcpy(
        native.native_admission.module_path,
        "X:\\workshop\\3773041738\\mods\\native\\global\\"
        "example.vehicle_audio\\PlayerMedia.mod");
    fill_hex(native.native_admission.module_sha256, 'e');
    strcpy(
        native.native_admission.approval_fingerprint,
        native.approval_fingerprint);
    strcpy(
        native.native_admission.manifest_fingerprint,
        native.manifest_fingerprint);
    strcpy(
        native.native_admission.companion_path,
        native.requested_mount_path);
    strcpy(
        native.native_admission.companion_sha256,
        native.content_sha256);
    native.native_admission.source_object =
        native.source_object;
    native.native_admission.source_runtime_type =
        native.runtime_source_type;
    native.native_admission.source_flags = native.source_flags;
    native.native_admission.
        native_registry_publication_generation = 71U;
    native.native_admission.admission_generation = 91U;
    native.native_admission.module_volume_serial = 21U;
    native.native_admission.module_file_index_high = 22U;
    native.native_admission.module_file_index_low = 23U;
    native.native_admission.module_file_size = 8192U;
    native.native_admission.companion_volume_serial =
        native.volume_serial;
    native.native_admission.companion_file_index_high =
        native.file_index_high;
    native.native_admission.companion_file_index_low =
        native.file_index_low;
    native.native_admission.companion_file_size =
        native.file_size;
    native.native_admission.enabled_for_this_launch = 1U;
    EXPECT(dluma_process_lifetime_source_register_in(
        &registry, &native, &native_index));
    EXPECT(native_index == 1U);
    EXPECT(dluma_process_lifetime_source_snapshot_in(
        &registry, &snapshot));
    EXPECT(snapshot.records[native_index].
               native_admission.source_record_committed);
    EXPECT(!snapshot.records[native_index].
                native_admission.owner_snapshot_published);
    published =
        snapshot.records[native_index].native_admission;
    published.owner_snapshot_published = 1U;
    EXPECT(dluma_process_lifetime_source_publish_native_admission_in(
        &registry, native.owner_kind, native.owner_id,
        native.source_object, &published));
    EXPECT(dluma_process_lifetime_source_snapshot_in(
        &registry, &snapshot));
    EXPECT(snapshot.records[native_index].
               native_admission.owner_snapshot_published);
    published.module_sha256[0] = 'f';
    EXPECT(!dluma_process_lifetime_source_publish_native_admission_in(
        &registry, native.owner_kind, native.owner_id,
        native.source_object, &published));
    native.manifest_fingerprint[0] = 'd';
    EXPECT(!dluma_process_lifetime_source_register_in(
        &registry, &native, 0));
    return 1;
}

static int test_custom_main_menu_directories_are_owned(void)
{
    DlumaProcessLifetimeSourceRegistry registry;
    DlumaProcessLifetimeSourceRecord project = make_loader();
    DlumaProcessLifetimeSourceRecord data = make_loader();
    DlumaProcessLifetimeSourceRecord assets_pc = make_loader();
    DlumaProcessLifetimeSourceRecord overlay = make_loader();
    dluma_process_lifetime_source_registry_initialize(&registry);
    project.owner_kind = DLUMA_SOURCE_OWNER_CUSTOM_MAIN_MENU;
    strcpy(
        project.owner_id,
        DLUMA_SOURCE_OWNER_ID_CUSTOM_MAIN_MENU_PROJECT);
    project.source_object = (void*)0x80000;
    strcpy(
        project.requested_mount_path,
        "X:\\DevTools\\workshop\\MenuOverride");
    project.runtime_source_type =
        DLUMA_PROCESS_SOURCE_RUNTIME_DIRECTORY;
    project.source_flags = 8U;
    project.file_size = 0U;
    EXPECT(dluma_process_lifetime_source_register_in(
        &registry, &project, 0));
    data = project;
    strcpy(data.owner_id, DLUMA_SOURCE_OWNER_ID_CUSTOM_MAIN_MENU_DATA);
    data.source_object = (void*)0x80008;
    strcpy(
        data.requested_mount_path,
        "X:\\DevTools\\workshop\\MenuOverride\\data");
    EXPECT(dluma_process_lifetime_source_register_in(
        &registry, &data, 0));
    assets_pc = project;
    strcpy(
        assets_pc.owner_id,
        DLUMA_SOURCE_OWNER_ID_CUSTOM_MAIN_MENU_ASSETS_PC);
    assets_pc.source_object = (void*)0x8000C;
    strcpy(
        assets_pc.requested_mount_path,
        "X:\\DevTools\\workshop\\MenuOverride\\assets_pc");
    EXPECT(dluma_process_lifetime_source_register_in(
        &registry, &assets_pc, 0));
    overlay = project;
    strcpy(
        overlay.owner_id,
        DLUMA_SOURCE_OWNER_ID_CUSTOM_MAIN_MENU_OVERLAY);
    overlay.source_object = (void*)0x80010;
    strcpy(
        overlay.requested_mount_path,
        "X:\\mods\\dluma_runtime\\custom_main_menu\\overlays\\"
        "0123456789abcdef");
    overlay.source_flags = 9U;
    EXPECT(dluma_process_lifetime_source_register_in(
        &registry, &overlay, 0));
    overlay.source_flags = 8U;
    EXPECT(!dluma_process_lifetime_source_register_in(
        &registry, &overlay, 0));
    EXPECT(registry.count == 4U);
    return 1;
}

static int test_custom_main_menu_bank_directory_is_owned(void)
{
    DlumaProcessLifetimeSourceRegistry registry;
    DlumaProcessLifetimeSourceRecord bank = make_loader();
    dluma_process_lifetime_source_registry_initialize(&registry);
    bank.owner_kind = DLUMA_SOURCE_OWNER_CUSTOM_MAIN_MENU;
    strcpy(
        bank.owner_id,
        DLUMA_SOURCE_OWNER_ID_CUSTOM_MAIN_MENU_BANK);
    bank.source_object = (void*)0x81000;
    strcpy(
        bank.requested_mount_path,
        "X:\\mods\\dluma_runtime\\prepared_banks\\0123456789abcdef");
    bank.runtime_source_type =
        DLUMA_PROCESS_SOURCE_RUNTIME_DIRECTORY;
    bank.source_flags = 9U;
    bank.file_size = 0U;
    EXPECT(dluma_process_lifetime_source_register_in(
        &registry, &bank, 0));
    EXPECT(registry.count == 1U);
    EXPECT(registry.records[0].runtime_source_type ==
        DLUMA_PROCESS_SOURCE_RUNTIME_DIRECTORY);
    return 1;
}

static int test_extended_path_normalization(void)
{
    char ordinary[512];
    char extended[512];
    char pseudo[512];
    char runtime_slash_folded[512];
    char unc[512];
    EXPECT(dluma_process_lifetime_source_normalize_path(
        "X:/Workshop/3773041738/mods/native/global/"
        "example.vehicle_audio",
        ordinary, sizeof(ordinary)));
    EXPECT(dluma_process_lifetime_source_normalize_path(
        "\\\\?\\X:\\Workshop\\3773041738\\mods\\native\\global\\"
        "example.vehicle_audio\\",
        extended, sizeof(extended)));
    EXPECT(dluma_process_lifetime_source_normalize_path(
        "\\?\\X:\\Workshop\\3773041738\\mods\\native\\global\\"
        "example.vehicle_audio",
        pseudo, sizeof(pseudo)));
    EXPECT(dluma_process_lifetime_source_normalize_path(
        "//?/X:/Workshop/3773041738/mods/native/global/"
        "example.vehicle_audio/",
        runtime_slash_folded, sizeof(runtime_slash_folded)));
    EXPECT(strcmp(ordinary, extended) == 0);
    EXPECT(strcmp(ordinary, pseudo) == 0);
    EXPECT(strcmp(ordinary, runtime_slash_folded) == 0);
    EXPECT(dluma_process_lifetime_source_normalize_path(
        "\\\\?\\UNC\\server\\share\\mods\\dluma",
        unc, sizeof(unc)));
    EXPECT(strcmp(unc, "\\\\server\\share\\mods\\dluma") == 0);
    EXPECT(!dluma_process_lifetime_source_normalize_path(
        "X:\\mods\\..\\escape", extended, sizeof(extended)));
    EXPECT(!dluma_process_lifetime_source_normalize_path(
        "\\?foo", extended, sizeof(extended)));
    EXPECT(!dluma_process_lifetime_source_normalize_path(
        "\\?\\1:\\root", extended, sizeof(extended)));
    EXPECT(!dluma_process_lifetime_source_normalize_path(
        "\\?\\E:root", extended, sizeof(extended)));
    EXPECT(!dluma_process_lifetime_source_normalize_path(
        "\\?\\UNC\\server\\share", extended, sizeof(extended)));
    EXPECT(!dluma_process_lifetime_source_normalize_path(
        "X:\\root\\?\\Y:\\escape", extended, sizeof(extended)));
    EXPECT(!dluma_process_lifetime_source_normalize_path(
        "\\\\?\\not-a-drive\\root", extended, sizeof(extended)));
    EXPECT(!dluma_process_lifetime_source_normalize_path(
        "//?/1:/root", extended, sizeof(extended)));
    EXPECT(!dluma_process_lifetime_source_normalize_path(
        "//?/e:root", extended, sizeof(extended)));
    EXPECT(!dluma_process_lifetime_source_normalize_path(
        "//?/unc/server/share", extended, sizeof(extended)));
    EXPECT(!dluma_process_lifetime_source_normalize_path(
        "/?/X:/root", extended, sizeof(extended)));
    EXPECT(!dluma_process_lifetime_source_normalize_path(
        "\\\\?/X:/root", extended, sizeof(extended)));
    return 1;
}

int main(void)
{
    if (!test_register_and_exact_bind() ||
        !test_live_retire_replace_and_aba() ||
        !test_ownership_cannot_transfer() ||
        !test_directory_and_native_metadata() ||
        !test_custom_main_menu_directories_are_owned() ||
        !test_custom_main_menu_bank_directory_is_owned() ||
        !test_extended_path_normalization())
        return 1;
    puts("process-lifetime source registry fake harness passed");
    return 0;
}
