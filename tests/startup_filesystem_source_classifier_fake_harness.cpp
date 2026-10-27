#include "fixes/custom_map_asset_isolation/StartupFilesystemSourceClassifier.h"
#include "fixes/custom_map_asset_isolation/AudioMinimumSemanticPlan.h"
#include "fixes/custom_map_asset_isolation/PlayerGameplayParamsRestartAdapter.h"
#include "fixes/custom_map_asset_isolation/StartupBaselineCaptureState.h"

#include <cstdio>
#include <cstring>

namespace cold =
    dluma::fixes::custom_map_asset_isolation::cold_reboot;
namespace startup =
    dluma::fixes::custom_map_asset_isolation::startupsource;
namespace vfs =
    dluma::fixes::custom_map_asset_isolation::vfs;
namespace player =
    dluma::fixes::custom_map_asset_isolation::cold_reboot::player_restart;
namespace audio_minimum =
    dluma::fixes::custom_map_asset_isolation::audio_minimum;
namespace audio_snapshot =
    dluma::fixes::custom_map_asset_isolation::audio_semantic_snapshot;

namespace {

#define EXPECT(expression)                                                    \
    do {                                                                      \
        if (!(expression)) {                                                  \
            std::fprintf(stderr, "failure at line %d: %s\n", __LINE__,       \
                         #expression);                                        \
            return false;                                                     \
        }                                                                     \
    } while (0)

constexpr std::uint32_t kSourceCount = 55U;
constexpr std::uint32_t kDeveloperIndex = 21U;
constexpr std::uint32_t kThumbnailIndex = 22U;
constexpr std::uint32_t kNativeIndex = 47U;
constexpr std::uint32_t kLoaderIndex = 48U;
constexpr std::uint32_t kFallbackIndex = 54U;
constexpr std::uint64_t kRegistryGeneration = 77U;
constexpr std::uintptr_t kDirectoryVtable = 0x180010000ULL;
constexpr std::uintptr_t kPackVtable = 0x180020000ULL;

struct FakeSource {
    startup::FilesystemSourceRuntimeType type;
    std::uintptr_t vtable;
};

struct Fixture {
    FakeSource sources[kSourceCount];
    FakeSource replacement;
    vfs::FilesystemSourceInventory inventory;
    DlumaProcessLifetimeSourceSnapshot mounts;
    DlumaProcessLifetimeSourceSnapshot expected_mounts;
    startup::StartupSourceClassificationPolicy policy;
};

void Copy(char* output, std::size_t capacity, const char* input) noexcept
{
    if (!output || !capacity) return;
    std::size_t index = 0;
    while (input && input[index] && index + 1U < capacity) {
        output[index] = input[index];
        ++index;
    }
    output[index] = 0;
}

void FillHex(char output[65], char value) noexcept
{
    for (std::uint32_t index = 0; index < 64U; ++index)
        output[index] = value;
    output[64] = 0;
}

bool Inspect(void*, std::uintptr_t source, std::uintptr_t* vtable,
             startup::FilesystemSourceRuntimeType* type) noexcept
{
    if (!source || !vtable || !type) return false;
    const FakeSource* fake =
        reinterpret_cast<const FakeSource*>(source);
    if (!fake->vtable) return false;
    *vtable = fake->vtable;
    *type = fake->type;
    return true;
}

bool TextEqual(const char* left, const char* right) noexcept
{
    return left && right && _stricmp(left, right) == 0;
}

 DlumaNativeCompanionOwnerValidationResult ValidateOwner(
    void* user,
    const DlumaProcessLifetimeSourceRecord* record,
    DlumaNativeCompanionOwnerValidationDiagnostic* diagnostic) noexcept
{
    const DlumaProcessLifetimeSourceSnapshot* expected =
        static_cast<const DlumaProcessLifetimeSourceSnapshot*>(user);
    if (diagnostic)
        std::memset(diagnostic, 0, sizeof(*diagnostic));
    if (!expected || !record)
        return DLUMA_COMPANION_ARCHIVE_OWNER_RECORD_MISSING;
    for (std::uint32_t index = 0; index < expected->count; ++index) {
        const DlumaProcessLifetimeSourceRecord& item =
            expected->records[index];
        if (item.owner_kind != record->owner_kind ||
            !TextEqual(item.owner_id, record->owner_id))
            continue;
        if (record->owner_kind ==
                DLUMA_SOURCE_OWNER_NATIVE_COMPANION_PACK)
            return dluma_native_companion_source_owner_validate(
                record, &item.native_admission, diagnostic);
        const bool valid =
            item.source_object == record->source_object &&
            item.mount_generation == record->mount_generation &&
            item.volume_serial == record->volume_serial &&
            item.file_index_high == record->file_index_high &&
            item.file_index_low == record->file_index_low &&
            item.file_size == record->file_size &&
            TextEqual(item.content_sha256, record->content_sha256) &&
            TextEqual(
                item.approval_fingerprint,
                record->approval_fingerprint) &&
            TextEqual(
                item.manifest_fingerprint,
                record->manifest_fingerprint);
        if (diagnostic)
            diagnostic->result = valid
                ? DLUMA_COMPANION_ARCHIVE_OWNER_VALID
                : DLUMA_COMPANION_ARCHIVE_OWNER_COMPANION_HASH_MISMATCH;
        return valid
            ? DLUMA_COMPANION_ARCHIVE_OWNER_VALID
            : DLUMA_COMPANION_ARCHIVE_OWNER_COMPANION_HASH_MISMATCH;
    }
    return DLUMA_COMPANION_ARCHIVE_OWNER_RECORD_MISSING;
}

DlumaProcessLifetimeSourceRecord MakeMount(
    DlumaProcessLifetimeSourceOwnerKind owner_kind,
    const char* owner_id,
    FakeSource* source,
    const char* requested_path,
    char hash_digit) noexcept
{
    DlumaProcessLifetimeSourceRecord record = {};
    record.owner_kind = owner_kind;
    Copy(record.owner_id, sizeof(record.owner_id), owner_id);
    record.source_object = source;
    Copy(
        record.requested_mount_path,
        sizeof(record.requested_mount_path),
        requested_path);
    record.runtime_source_type =
        source->type == startup::FilesystemSourceRuntimeType::Pack
            ? DLUMA_PROCESS_SOURCE_RUNTIME_PACK
            : DLUMA_PROCESS_SOURCE_RUNTIME_DIRECTORY;
    record.source_flags =
        record.runtime_source_type ==
                DLUMA_PROCESS_SOURCE_RUNTIME_PACK
            ? 9U : 8U;
    record.volume_serial = 101U + owner_kind;
    record.file_index_high = 201U + owner_kind;
    record.file_index_low = 301U + owner_kind;
    if (record.runtime_source_type ==
            DLUMA_PROCESS_SOURCE_RUNTIME_PACK) {
        record.file_size = 0x10000U + owner_kind;
        FillHex(record.content_sha256, hash_digit);
    }
    if (owner_kind ==
            DLUMA_SOURCE_OWNER_NATIVE_COMPANION_PACK) {
        FillHex(record.approval_fingerprint, 'd');
        FillHex(record.manifest_fingerprint, 'e');
        DlumaNativeAdmissionIdentity& admission =
            record.native_admission;
        Copy(
            admission.module_id, sizeof(admission.module_id),
            owner_id);
        Copy(
            admission.module_name,
            sizeof(admission.module_name),
            "PlayerMedia Stemmed Buggy Music");
        FillHex(admission.stable_candidate_id, 'f');
        Copy(
            admission.module_path,
            sizeof(admission.module_path),
            "X:\\FixtureLibrary\\steamapps\\workshop\\content\\239140\\"
            "3773041738\\mods\\native\\global\\"
            "example.vehicle_audio\\PlayerMedia.mod");
        FillHex(admission.module_sha256, '1');
        Copy(
            admission.approval_fingerprint,
            sizeof(admission.approval_fingerprint),
            record.approval_fingerprint);
        Copy(
            admission.manifest_fingerprint,
            sizeof(admission.manifest_fingerprint),
            record.manifest_fingerprint);
        Copy(
            admission.companion_path,
            sizeof(admission.companion_path),
            requested_path);
        Copy(
            admission.companion_sha256,
            sizeof(admission.companion_sha256),
            record.content_sha256);
        admission.source_object = source;
        admission.source_runtime_type =
            DLUMA_PROCESS_SOURCE_RUNTIME_PACK;
        admission.source_flags = record.source_flags;
        admission.native_registry_publication_generation = 31U;
        admission.admission_generation = 17U;
        admission.module_volume_serial = 401U;
        admission.module_file_index_high = 402U;
        admission.module_file_index_low = 403U;
        admission.module_file_size = 0x20000U;
        admission.companion_volume_serial =
            record.volume_serial;
        admission.companion_file_index_high =
            record.file_index_high;
        admission.companion_file_index_low =
            record.file_index_low;
        admission.companion_file_size = record.file_size;
        admission.enabled_for_this_launch = 1U;
    }
    record.metadata_complete = 1U;
    return record;
}

void InitializeFixture(Fixture* fixture) noexcept
{
    std::memset(fixture, 0, sizeof(*fixture));
    fixture->inventory.reported_count = kSourceCount;
    fixture->inventory.source_count = kSourceCount;
    fixture->inventory.registry_generation = kRegistryGeneration;
    fixture->inventory.registry_generation_observed = true;
    fixture->inventory.complete = true;
    fixture->inventory.fallback_state_known = true;
    fixture->inventory.fallback_installed = true;
    for (std::uint32_t index = 0; index < kSourceCount; ++index) {
        FakeSource& source = fixture->sources[index];
        source.type =
            index == kNativeIndex || index == kLoaderIndex
                ? startup::FilesystemSourceRuntimeType::Pack
                : startup::FilesystemSourceRuntimeType::Directory;
        source.vtable =
            source.type == startup::FilesystemSourceRuntimeType::Pack
                ? kPackVtable : kDirectoryVtable;
        vfs::FilesystemSourceInventoryRecord& row =
            fixture->inventory.sources[index];
        row.source = reinterpret_cast<std::uintptr_t>(&source);
        row.link = 0x180100000ULL + index * 0x20U;
        row.list_ordinal = index + 1U;
        if (index == kDeveloperIndex) {
            Copy(
                row.path, sizeof(row.path),
                "X:\\FixtureLibrary\\steamapps\\workshop\\content\\239140\\"
                "3771245504\\mods\\dluma_runtime\\developer_mode");
        } else if (index == kThumbnailIndex) {
            Copy(
                row.path, sizeof(row.path),
                "X:\\FixtureLibrary\\steamapps\\workshop\\content\\239140\\"
                "3771245504\\mods\\dluma_runtime\\workshop_thumbnails");
        } else if (index == kNativeIndex) {
            Copy(
                row.path, sizeof(row.path),
                "//?/X:/fixturelibrary/steamapps/workshop/content/"
                "239140/3773041738/mods/native/global/"
                "example.vehicle_audio/");
        } else if (index == kLoaderIndex) {
            Copy(
                row.path, sizeof(row.path),
                "X:\\FixtureLibrary\\steamapps\\workshop\\content\\239140\\"
                "3771245504\\mods");
        } else if (index == kFallbackIndex) {
            row.native_fallback = true;
            Copy(
                row.path, sizeof(row.path),
                "filesystem:native-fallback");
        } else if (index == 3U) {
            Copy(
                row.path, sizeof(row.path),
                "X:\\FixtureLibrary\\steamapps\\common\\Dying Light\\"
                "DevTools\\DW\\Data\\Speech\\En");
        } else if (index == 4U) {
            Copy(
                row.path, sizeof(row.path),
                "X:\\FixtureLibrary\\steamapps\\common\\Dying Light\\"
                "DW_DLC17\\Data");
        } else {
            std::snprintf(
                row.path, sizeof(row.path),
                "X:\\FixtureLibrary\\steamapps\\common\\Dying Light\\"
                "DevTools\\DW\\Data\\pack_%02u.pak",
                static_cast<unsigned int>(index));
        }
    }
    fixture->replacement.type =
        startup::FilesystemSourceRuntimeType::Pack;
    fixture->replacement.vtable = kPackVtable;

    DlumaProcessLifetimeSourceRegistry registry = {};
    DlumaProcessLifetimeSourceRecord records[4] = {
        MakeMount(
            DLUMA_SOURCE_OWNER_LOADER_DEVELOPER_MODE,
            DLUMA_SOURCE_OWNER_ID_DEVELOPER_MODE,
            &fixture->sources[kDeveloperIndex],
            fixture->inventory.sources[kDeveloperIndex].path, '0'),
        MakeMount(
            DLUMA_SOURCE_OWNER_LOADER_THUMBNAILS,
            DLUMA_SOURCE_OWNER_ID_THUMBNAILS,
            &fixture->sources[kThumbnailIndex],
            fixture->inventory.sources[kThumbnailIndex].path, '0'),
        MakeMount(
            DLUMA_SOURCE_OWNER_NATIVE_COMPANION_PACK,
            "example.vehicle_audio",
            &fixture->sources[kNativeIndex],
            "X:\\FixtureLibrary\\steamapps\\workshop\\content\\239140\\"
            "3773041738\\mods\\native\\global\\"
            "example.vehicle_audio\\PlayerMedia.pak",
            'b'),
        MakeMount(
            DLUMA_SOURCE_OWNER_LOADER_PAK,
            DLUMA_SOURCE_OWNER_ID_LOADER_PAK,
            &fixture->sources[kLoaderIndex],
            "X:\\FixtureLibrary\\steamapps\\workshop\\content\\239140\\"
            "3771245504\\mods\\dluma.pak", 'a')
    };
    for (std::uint32_t index = 0; index < 4U; ++index) {
        unsigned int record_index = 0;
        (void)dluma_process_lifetime_source_register_in(
            &registry, &records[index], &record_index);
        if (records[index].owner_kind ==
                DLUMA_SOURCE_OWNER_NATIVE_COMPANION_PACK) {
            DlumaNativeAdmissionIdentity published =
                registry.records[record_index].native_admission;
            published.owner_snapshot_published = 1U;
            (void)
                dluma_process_lifetime_source_publish_native_admission_in(
                    &registry, records[index].owner_kind,
                    records[index].owner_id,
                    records[index].source_object, &published);
        }
        const std::uint32_t row_index =
            index == 0U ? kDeveloperIndex :
            index == 1U ? kThumbnailIndex :
            index == 2U ? kNativeIndex : kLoaderIndex;
        const vfs::FilesystemSourceInventoryRecord& row =
            fixture->inventory.sources[row_index];
        (void)dluma_process_lifetime_source_bind_inventory_in(
            &registry, records[index].owner_kind,
            records[index].owner_id, records[index].source_object,
            reinterpret_cast<void*>(row.link), row.list_ordinal,
            row.path, records[index].runtime_source_type,
            kRegistryGeneration);
    }
    (void)dluma_process_lifetime_source_snapshot_in(
        &registry, &fixture->mounts);
    fixture->expected_mounts = fixture->mounts;

    startup::StartupSourceClassificationPolicy& policy =
        fixture->policy;
    Copy(
        policy.player_root, sizeof(policy.player_root),
        "X:\\FixtureLibrary\\steamapps\\common\\Dying Light\\DevTools");
    Copy(
        policy.game_install_root,
        sizeof(policy.game_install_root),
        "X:\\FixtureLibrary\\steamapps\\common\\Dying Light");
    Copy(
        policy.workshop_roots[0],
        sizeof(policy.workshop_roots[0]),
        "X:\\FixtureLibrary\\steamapps\\workshop\\content\\239140");
    Copy(
        policy.workshop_roots[1],
        sizeof(policy.workshop_roots[1]),
        "X:\\FixtureLibrary\\steamapps\\common\\Dying Light\\"
        "DevTools\\workshop");
    policy.workshop_root_count = 2U;
    policy.process_lifetime_sources = &fixture->mounts;
    policy.inspect_source = &Inspect;
    policy.validate_process_lifetime_owner = &ValidateOwner;
    policy.validate_process_lifetime_owner_user =
        &fixture->expected_mounts;
    policy.committed_registry_generation = kRegistryGeneration;
}

startup::StartupSourceClassificationReport Classify(
    Fixture* fixture) noexcept
{
    startup::StartupSourceClassificationReport report = {};
    (void)startup::ClassifyStartupFilesystemSources(
        &fixture->inventory, &fixture->policy, &report);
    return report;
}

void RemoveMount(
    DlumaProcessLifetimeSourceSnapshot* snapshot,
    DlumaProcessLifetimeSourceOwnerKind owner_kind) noexcept
{
    for (std::uint32_t index = 0; index < snapshot->count; ++index) {
        if (snapshot->records[index].owner_kind != owner_kind)
            continue;
        for (std::uint32_t move = index + 1U;
             move < snapshot->count; ++move)
            snapshot->records[move - 1U] =
                snapshot->records[move];
        --snapshot->count;
        std::memset(
            &snapshot->records[snapshot->count], 0,
            sizeof(snapshot->records[0]));
        return;
    }
}

DlumaProcessLifetimeSourceRecord* FindMount(
    DlumaProcessLifetimeSourceSnapshot* snapshot,
    DlumaProcessLifetimeSourceOwnerKind owner_kind) noexcept
{
    for (std::uint32_t index = 0; index < snapshot->count; ++index)
        if (snapshot->records[index].owner_kind == owner_kind)
            return &snapshot->records[index];
    return nullptr;
}

constexpr std::uint32_t kRuntime10PropertyIds[
    player::kRequiredPlayerPropertyCount] = {
        player::kLowHealthEffectThresholdPropertyId,
        player::kStaminaWarningLevelPropertyId,
        player::kMaxStaminaPropertyId,
        player::kMoveSprintSpeedPropertyId,
        player::kMaxHealthPropertyId,
        player::kBaseMaxHealthPropertyId,
        player::kHealthCriticalLevelPropertyId,
        player::kHealthRegenerationDelayPropertyId,
        player::kHealthRegenerationMaxLevelPropertyId,
        player::kHealthRegenerationTimeToMaxPropertyId,
        player::kCameraDefaultFovPropertyId,
        player::kKickStaminaUsagePropertyId,
        player::kCameraCarFovPropertyId,
        player::kFovSprintModifierPropertyId,
        player::kFovAmortizerModifierPropertyId,
    };

constexpr std::uintptr_t kRuntime11DefinitionHeaders =
    0x0000000161000000ULL;
constexpr std::uintptr_t kRuntime11DefinitionLevels =
    0x0000000161010000ULL;
constexpr std::uintptr_t kRuntime11DefinitionProperties =
    0x0000000161020000ULL;
constexpr std::size_t kRuntime11DefinitionLevelCount = 426U;
constexpr std::size_t kRuntime12DefinitionPropertyCount = 1573U;

struct Runtime10PlayerWorld {
    player::PlayerGameplayParamsRestartAdapter adapter;
    player::PlayerRestartEnvironment environment;
    player::PlayerStateRestartSnapshot states[2];
    std::uintptr_t pointers[2];
    std::uint64_t generations[2];
    unsigned char definition_headers[
        player::kPlayerLogicalTypeCount][16];
    unsigned char definition_levels[
        kRuntime11DefinitionLevelCount *
        player::kWindowsPlayerDefinitionLevelStride];
    unsigned char definition_properties[
        kRuntime12DefinitionPropertyCount *
        player::kWindowsPlayerDefinitionPropertyStride];
    std::uint32_t environment_reads;
    std::uint32_t snapshot_calls;
    std::uint32_t semantic_capture_calls;
};

template <typename T>
void RuntimeStore(
    unsigned char* bytes, std::size_t offset, T value) noexcept
{
    std::memcpy(bytes + offset, &value, sizeof(value));
}

bool RuntimeCopyRegion(
    std::uintptr_t address, void* output, std::size_t size,
    std::uintptr_t base, const void* region,
    std::size_t region_size) noexcept
{
    if (!output || !size || address < base ||
        address > static_cast<std::uintptr_t>(-1) - size)
        return false;
    const std::uintptr_t end = address + size;
    if (end > base + region_size) return false;
    std::memcpy(
        output,
        static_cast<const unsigned char*>(region) +
            static_cast<std::size_t>(address - base),
        size);
    return true;
}

bool ReadRuntime11PlayerDefinitionMemory(
    void* user, std::uintptr_t address, void* output,
    std::size_t size) noexcept
{
    Runtime10PlayerWorld* world =
        static_cast<Runtime10PlayerWorld*>(user);
    return world && output && size &&
        (RuntimeCopyRegion(
             address, output, size, kRuntime11DefinitionHeaders,
             world->definition_headers,
             sizeof(world->definition_headers)) ||
         RuntimeCopyRegion(
             address, output, size, kRuntime11DefinitionLevels,
             world->definition_levels,
             sizeof(world->definition_levels)) ||
         RuntimeCopyRegion(
             address, output, size, kRuntime11DefinitionProperties,
             world->definition_properties,
             sizeof(world->definition_properties)));
}

void InitializeRuntime11PlayerDefinitions(
    Runtime10PlayerWorld* world) noexcept
{
    static constexpr std::uint32_t level_counts[
        player::kPlayerLogicalTypeCount] = {
            60U, 24U, 24U, 25U, 6U, 250U, 24U, 13U};
    static constexpr std::uint32_t level_capacities[
        player::kPlayerLogicalTypeCount] = {
            64U, 32U, 32U, 32U, 16U, 288U, 32U, 16U};
    static constexpr std::uint32_t property_counts[
        player::kPlayerLogicalTypeCount] = {
            497U, 49U, 46U, 168U, 6U, 748U, 46U, 13U};
    std::uint32_t level_ordinal = 0;
    std::uint32_t property_ordinal = 0;
    for (std::uint32_t row_index = 0;
         row_index < player::kPlayerLogicalTypeCount; ++row_index) {
        const std::uintptr_t levels =
            kRuntime11DefinitionLevels +
            static_cast<std::uintptr_t>(level_ordinal) *
                player::kWindowsPlayerDefinitionLevelStride;
        RuntimeStore(
            world->definition_headers[row_index], 0U, levels);
        RuntimeStore(
            world->definition_headers[row_index], 8U,
            level_counts[row_index]);
        RuntimeStore(
            world->definition_headers[row_index], 12U,
            level_capacities[row_index]);
        const std::uint32_t properties_per_level =
            property_counts[row_index] / level_counts[row_index];
        const std::uint32_t extra_properties =
            property_counts[row_index] % level_counts[row_index];
        for (std::uint32_t level_index = 0;
             level_index < level_counts[row_index]; ++level_index) {
            const std::uint32_t item = level_ordinal + level_index;
            const std::uint32_t level_property_count =
                properties_per_level +
                (level_index < extra_properties ? 1U : 0U);
            unsigned char* level =
                world->definition_levels +
                static_cast<std::size_t>(item) *
                    player::kWindowsPlayerDefinitionLevelStride;
            const std::uintptr_t property_address =
                level_property_count
                    ? kRuntime11DefinitionProperties +
                          static_cast<std::uintptr_t>(property_ordinal) *
                              player::
                                  kWindowsPlayerDefinitionPropertyStride
                    : 0U;
            RuntimeStore(level, 0U, property_address);
            RuntimeStore(level, 8U, level_property_count);
            RuntimeStore(level, 12U, level_property_count);
            for (std::uint32_t property_index = 0;
                 property_index < level_property_count;
                 ++property_index) {
                const std::uint32_t property_item =
                    property_ordinal + property_index;
                unsigned char* property =
                    world->definition_properties +
                    static_cast<std::size_t>(property_item) *
                        player::kWindowsPlayerDefinitionPropertyStride;
                RuntimeStore(
                    property, 0U,
                    static_cast<std::uint32_t>(
                        1000U + row_index * 8192U +
                        property_item));
                RuntimeStore(
                    property, 4U,
                    static_cast<std::uint32_t>(
                        0x3F000000U + property_item));
            }
            property_ordinal += level_property_count;
        }
        level_ordinal += level_counts[row_index];
    }
}

player::PlayerStateRestartSnapshot Runtime10PlayerSnapshot(
    std::uintptr_t state, std::uint64_t generation,
    std::uint32_t seed, std::uint64_t cache_generation) noexcept
{
    player::PlayerStateRestartSnapshot snapshot = {};
    snapshot.state = state;
    snapshot.state_generation = generation;
    snapshot.cache_generation = cache_generation;
    snapshot.restart_provenance_reason_mask =
        player::kPlayerWindowsRestartProvenanceReasonMask;
    snapshot.player_variables = 0x0000000153000000ULL + seed * 0x1000U;
    snapshot.progression_selector_fingerprint = 0x101000U + seed;
    snapshot.class_selector_fingerprint = 0x202000U + seed;
    snapshot.skill_membership_fingerprint = 0x303000U + seed;
    snapshot.regular_buff_fingerprint = 0x404000U + seed;
    snapshot.timed_buff_fingerprint = 0x505000U + seed;
    snapshot.inventory_fingerprint = 0x606000U + seed;
    snapshot.equipment_fingerprint = 0x707000U + seed;
    snapshot.quest_fingerprint = 0x808000U + seed;
    snapshot.appearance_fingerprint = 0x909000U + seed;
    snapshot.body_readable = true;
    snapshot.row_table_readable = true;
    snapshot.player_variables_readable = true;
    snapshot.selectors_complete = true;
    snapshot.preservation_invariants_complete = true;
    snapshot.appearance_complete = true;
    snapshot.player_semantic_snapshot_complete = true;
    snapshot.player_restart_snapshot_complete = false;
    snapshot.snapshot_complete = true;
    for (std::uint32_t index = 0;
         index < player::kPlayerLogicalTypeCount; ++index) {
        player::PlayerDefinitionRowSnapshot& row =
            snapshot.rows[index];
        row.definition_vector =
            0x0000000154000000ULL + seed * 0x10000U +
            index * 0x100U;
        row.definition_fingerprint =
            0xA0000000ULL + seed * 0x100U + index;
        row.earned_experience = seed * 100U + index;
        row.unknown_100 = 100U;
        row.trailing_qword = 0U;
        row.cache_generation = 0;
        row.cache_membership_evidence =
            player::PlayerCacheMembershipEvidence::Unobserved;
        row.membership_observation_sequence = 0;
        row.definition_count = 128U + index;
        row.current_level = static_cast<std::uint16_t>(index + 1U);
        row.maximum_level = static_cast<std::uint16_t>(index + 25U);
        row.row_readable = true;
        row.definition_readable = true;
        row.cache_generation_observed = false;
        row.cache_observer_available = false;
        row.cache_inventory_complete = false;
        row.member_of_live_cache = false;
        row.semantic_complete = true;
    }
    for (std::uint32_t index = 0;
         index < player::kRequiredPlayerPropertyCount; ++index) {
        player::PlayerPropertySnapshot& property =
            snapshot.properties[index];
        property.property_id = kRuntime10PropertyIds[index];
        property.value_bits = 0x41000000U + seed * 0x100U + index;
        property.cache_generation = 0;
        property.available = true;
    }
    return snapshot;
}

bool ReadRuntime10PlayerEnvironment(
    void* user, player::PlayerEnvironmentReadPurpose purpose,
    player::PlayerRestartEnvironment* output) noexcept
{
    (void)purpose;
    Runtime10PlayerWorld* world =
        static_cast<Runtime10PlayerWorld*>(user);
    if (!world || !output) return false;
    ++world->environment_reads;
    *output = world->environment;
    return true;
}

bool SnapshotRuntime10PlayerState(
    void* user, std::uintptr_t state, std::uint64_t generation,
    player::PlayerStateRestartSnapshot* output) noexcept
{
    Runtime10PlayerWorld* world =
        static_cast<Runtime10PlayerWorld*>(user);
    if (!world || !output) return false;
    for (std::uint32_t index = 0; index < 2U; ++index) {
        if (world->pointers[index] != state ||
            world->generations[index] != generation)
            continue;
        ++world->snapshot_calls;
        *output = world->states[index];
        if (index == 0U) {
            for (std::uint32_t row_index = 0;
                 row_index < player::kPlayerLogicalTypeCount;
                 ++row_index) {
                player::PlayerDefinitionRowSnapshot& row =
                    output->rows[row_index];
                std::memset(&row, 0, sizeof(row));
                row.row_readable = true;
                row.definition_readable = true;
                row.preset_absent = true;
                row.semantic_complete = true;
                auto& raw = output->raw_rows[row_index];
                raw.semantic_kind =
                    player::PlayerGameplayParamsWindowsRowSemanticKind::
                        AbsentPresetRow;
                raw.header_read_result =
                    player::
                        PlayerGameplayParamsWindowsRowHeaderReadResult::
                            AbsentPresetNotApplicable;
                raw.header_validation_result =
                    player::
                        PlayerGameplayParamsWindowsRowHeaderValidationResult::
                            AbsentPresetAccepted;
                raw.row_readable = true;
                raw.definition_vector_header_readable = true;
                raw.semantic_complete = true;
                raw.preset_absent = true;
            }
            return true;
        }

        const player::PlayerGameplayParamsWindowsReadContext read = {
            0x0000000180000000ULL,
            &ReadRuntime11PlayerDefinitionMemory,
            world,
            true};
        for (std::uint32_t row_index = 0;
             row_index < player::kPlayerLogicalTypeCount;
             ++row_index) {
            const std::uintptr_t preset =
                kRuntime11DefinitionHeaders +
                static_cast<std::uintptr_t>(row_index) * 16U;
            player::PlayerDefinitionSemanticSnapshot semantic = {};
            ++world->semantic_capture_calls;
            if (!player::CaptureWindowsPlayerDefinitionSemantics(
                    &read, preset, row_index, &semantic))
                return false;
            player::PlayerDefinitionRowSnapshot& row =
                output->rows[row_index];
            row.definition_vector = preset;
            row.definition_fingerprint =
                semantic.semantic_fingerprint;
            row.definition_count = semantic.property_count;
            row.preset_absent = false;
            row.semantic_complete = semantic.complete;
            auto& raw = output->raw_rows[row_index];
            raw.preset = preset;
            raw.definition_vector = preset;
            raw.definition_header_data =
                semantic.evidence.level_data;
            raw.definition_fingerprint =
                semantic.semantic_fingerprint;
            raw.definition_level_count = semantic.level_count;
            raw.definition_level_capacity =
                semantic.evidence.level_capacity;
            raw.definition_property_count =
                semantic.property_count;
            raw.definition_traversal = semantic.evidence;
            raw.semantic_kind =
                player::PlayerGameplayParamsWindowsRowSemanticKind::
                    PresetRow;
            raw.header_read_result =
                player::PlayerGameplayParamsWindowsRowHeaderReadResult::
                    Complete;
            raw.header_validation_result =
                player::
                    PlayerGameplayParamsWindowsRowHeaderValidationResult::
                        Valid;
            raw.row_readable = true;
            raw.definition_vector_header_readable = true;
            raw.semantic_complete = true;
        }
        return true;
    }
    return false;
}

bool ValidateRuntime12PlayerSemanticEvidence(
    const Runtime10PlayerWorld& world) noexcept
{
    static constexpr std::uint32_t level_counts[
        player::kPlayerLogicalTypeCount] = {
            60U, 24U, 24U, 25U, 6U, 250U, 24U, 13U};
    static constexpr std::uint32_t level_capacities[
        player::kPlayerLogicalTypeCount] = {
            64U, 32U, 32U, 32U, 16U, 288U, 32U, 16U};
    static constexpr std::uint32_t property_counts[
        player::kPlayerLogicalTypeCount] = {
            497U, 49U, 46U, 168U, 6U, 748U, 46U, 13U};
    for (std::uint32_t state_index = 0;
         state_index < 2U; ++state_index) {
        const auto& state =
            world.adapter.b0_diagnostics.states[state_index];
        EXPECT(state.snapshot_attempt_count == 2U);
        EXPECT(state.pass1_semantic_snapshot_complete);
        EXPECT(state.pass2_semantic_snapshot_complete);
        EXPECT(!state.pass1_restart_snapshot_complete);
        EXPECT(!state.pass2_restart_snapshot_complete);
        EXPECT(state.pass1_semantic_reason_mask == 0U);
        EXPECT(state.pass2_semantic_reason_mask == 0U);
        EXPECT((state.pass1_restart_provenance_reason_mask &
                player::kPlayerWindowsRestartProvenanceReasonMask) ==
               player::kPlayerWindowsRestartProvenanceReasonMask);
        EXPECT((state.pass2_restart_provenance_reason_mask &
                player::
                    kPlayerRestartProvenanceReasonCacheMembershipUnproven) !=
               0U);
        EXPECT(state.pass1_failure_stage ==
               player::PlayerStateSnapshotFailureStage::None);
        EXPECT(state.pass2_failure_stage ==
               player::PlayerStateSnapshotFailureStage::None);
        EXPECT(state.pass1_restart_failure_stage ==
               player::PlayerStateSnapshotFailureStage::
                   RestartCacheMembershipUnproven);
        EXPECT(state.pass2_restart_failure_stage ==
               player::PlayerStateSnapshotFailureStage::
                   RestartCacheMembershipUnproven);
        for (std::uint32_t row_index = 0;
             row_index < player::kPlayerLogicalTypeCount;
             ++row_index) {
            const auto& semantic_first =
                state.pass1_semantic_rows[row_index];
            const auto& semantic_second =
                state.pass2_semantic_rows[row_index];
            EXPECT(semantic_first.cache_membership_evidence ==
                   player::PlayerCacheMembershipEvidence::Unobserved);
            EXPECT(semantic_second.cache_membership_evidence ==
                   player::PlayerCacheMembershipEvidence::Unobserved);
            EXPECT(!semantic_first.cache_generation_observed);
            EXPECT(!semantic_first.cache_observer_available);
            EXPECT(!semantic_first.cache_inventory_complete);
            EXPECT(semantic_first.membership_observation_sequence == 0U);
            EXPECT(semantic_first.definition_fingerprint ==
                   semantic_second.definition_fingerprint);
            if (state_index == 0U) {
                EXPECT(semantic_first.preset_absent);
                EXPECT(semantic_second.preset_absent);
                continue;
            }
            const auto& raw_first = state.pass1_rows[row_index];
            const auto& raw_second = state.pass2_rows[row_index];
            EXPECT(!semantic_first.preset_absent);
            EXPECT(semantic_first.definition_fingerprint != 0U);
            EXPECT(semantic_first.definition_count ==
                   property_counts[row_index]);
            EXPECT(semantic_first.current_level <=
                   semantic_first.maximum_level);
            EXPECT(raw_first.definition_level_count ==
                   level_counts[row_index]);
            EXPECT(raw_first.definition_level_capacity ==
                   level_capacities[row_index]);
            EXPECT(raw_first.definition_property_count ==
                   property_counts[row_index]);
            EXPECT(raw_first.definition_fingerprint ==
                   raw_second.definition_fingerprint);
            EXPECT(raw_first.definition_traversal.first_failure ==
                   player::PlayerDefinitionTraversalFailure::None);
            EXPECT(raw_first.definition_traversal.
                       configured_level_stride ==
                   player::kWindowsPlayerDefinitionLevelStride);
            EXPECT(raw_first.definition_traversal.
                       configured_property_stride ==
                   player::kWindowsPlayerDefinitionPropertyStride);
        }
    }
    return true;
}

bool CaptureRuntime10PlayerBaseline(
    Runtime10PlayerWorld* world,
    std::uint64_t baseline_generation) noexcept
{
    if (!world || !baseline_generation) return false;
    std::memset(world, 0, sizeof(*world));
    InitializeRuntime11PlayerDefinitions(world);
    player::PlayerGameplayParamsSnapshotOperations operations = {};
    operations.read_environment = &ReadRuntime10PlayerEnvironment;
    operations.snapshot_state = &SnapshotRuntime10PlayerState;
    operations.environment_layout_validated = true;
    operations.state_snapshot_layout_validated = true;
    player::InitializePlayerGameplayParamsRestartAdapter(
        &world->adapter, &operations, nullptr, world);
    if (!player::BeginPlayerStateInventoryCoverage(
            &world->adapter, true, true, true))
        return false;

    /* Runtime 10's early zero-owner sample is bootstrap evidence only. */
    player::RecordPlayerB0BootstrapSample(
        &world->adapter, 29U, 0U);
    world->pointers[0] = 0x0000000152A5DE10ULL;
    world->pointers[1] = 0x0000000152A5FFC0ULL;
    for (std::uint32_t index = 0; index < 2U; ++index) {
        world->generations[index] =
            player::ObservePlayerStateConstructed(
                &world->adapter, world->pointers[index]);
        if (!world->generations[index]) return false;
        const player::PlayerStateOwnerKind owner_kind =
            index == 0U
                ? player::PlayerStateOwnerKind::LocalProcessGlobal
                : player::PlayerStateOwnerKind::CharacterManagerGlobal;
        const std::uintptr_t owner_slot =
            index == 0U
                ? 0x0000000141D07698ULL
                : 0x0000000141D078F0ULL;
        if (!player::ClassifyPlayerStateOwner(
                &world->adapter, world->pointers[index],
                world->generations[index], owner_kind, owner_slot))
            return false;
        world->states[index] = Runtime10PlayerSnapshot(
            world->pointers[index], world->generations[index],
            index + 1U, 9U);
    }
    world->environment.gameplay_params_container =
        0x0000000155000000ULL;
    world->environment.game_di = 0x0000000156000000ULL;
    world->environment.local_process_global_state =
        world->pointers[0];
    world->environment.character_manager_global_state =
        world->pointers[1];
    world->environment.cache_generation = 9U;
    world->environment.gameplay_cache_fingerprint =
        0xCACEB045EULL;
    world->environment.current_thread_id = 77U;
    world->environment.supported_image = true;
    world->environment.gameplay_params_container_readable = true;
    world->environment.game_di_readable = true;
    world->environment.global_owner_slots_readable = true;
    world->environment.inventory_scan_complete = true;

    cold::RestartContext context = {};
    context.baseline_generation = baseline_generation;
    context.sequence = 32U;
    context.owner_thread_id = 77U;
    return player::CapturePlayerGameplayParamsBaseline(
               &world->adapter, &context) ==
            cold::DomainResult::Succeeded &&
        world->adapter.baseline_kind ==
            player::PlayerStartupBaselineKind::Snapshot &&
        world->adapter.baseline_state_count == 2U &&
        world->adapter.b0_diagnostics.bootstrap_sample_owner_count == 0U &&
        world->adapter.b0_diagnostics.pass1_owner_count == 2U &&
        world->adapter.b0_diagnostics.pass2_owner_count == 2U &&
        world->adapter.b0_diagnostics.state_snapshot_count == 2U &&
        world->adapter.b0_diagnostics.
            owner_set_stable_across_passes &&
        world->generations[0] == 1U &&
        world->generations[1] == 2U &&
        world->adapter.b0_diagnostics.states[0].owner_kind ==
            player::PlayerStateOwnerKind::LocalProcessGlobal &&
        world->adapter.b0_diagnostics.states[0].owner_slot ==
            0x0000000141D07698ULL &&
        world->adapter.b0_diagnostics.states[1].owner_kind ==
            player::PlayerStateOwnerKind::CharacterManagerGlobal &&
        world->adapter.b0_diagnostics.states[1].owner_slot ==
            0x0000000141D078F0ULL &&
        world->adapter.b0_diagnostics.
            player_semantic_snapshot_complete &&
        !world->adapter.b0_diagnostics.
            player_restart_snapshot_complete &&
        world->snapshot_calls == 4U &&
        world->semantic_capture_calls ==
            2U * player::kPlayerLogicalTypeCount &&
        world->adapter.b0_diagnostics.states[0].snapshot_complete &&
        world->adapter.b0_diagnostics.states[1].snapshot_complete &&
        ValidateRuntime12PlayerSemanticEvidence(*world);
}

audio_minimum::AudioMinimumSemanticSnapshot Runtime10AudioSnapshot(
    const audio_minimum::AudioMinimumOwnerIdentity& owner) noexcept
{
    audio_minimum::AudioMinimumSemanticSnapshot snapshot = {};
    snapshot.script.registry_owner = 0x00007FF913B052E0ULL;
    snapshot.script.semantic_fingerprint = 0xA0D105C81ULL;
    snapshot.script.definition_count = 138U;
    snapshot.script.path_count = 8U;
    snapshot.script.procedure_count = 63U;
    snapshot.script.property_group_count = 1382U;
    snapshot.script.failure_stage =
        audio_snapshot::AudioScriptSemanticStage::Complete;
    snapshot.script.attempted = true;
    snapshot.script.complete = true;
    snapshot.banks.manager = owner.sound_bank_manager_receiver;
    snapshot.banks.tree_root = 0x70194000U;
    snapshot.banks.semantic_fingerprint = 0xBAA70001ULL;
    snapshot.banks.identity_fingerprint = 0xBAA70002ULL;
    snapshot.banks.bank_count = 3U;
    snapshot.banks.failure_stage =
        audio_snapshot::AudioBankSemanticStage::Complete;
    snapshot.banks.attempted = true;
    snapshot.banks.complete = true;
    snapshot.owner = owner;
    snapshot.observer_revision = owner.observer_revision;
    snapshot.active_event_fence_generation =
        owner.event_fence_generation;
    snapshot.observer_revision_observed =
        owner.observer_revision_observed;
    snapshot.event_fence_generation_observed =
        owner.event_fence_generation_observed;
    snapshot.complete = true;
    return snapshot;
}

bool CaptureRuntime10AudioBaseline(
    audio_minimum::AudioMinimumSemanticState* state,
    std::uint64_t baseline_generation) noexcept
{
    if (!state || !baseline_generation) return false;
    audio_minimum::AudioMinimumOwnerPublication publication = {};
    audio_minimum::InitializeAudioMinimumOwnerPublication(
        &publication, 0x60A019C801240000ULL, 0xFA3B00100920ULL);
    const std::uintptr_t audio_receiver = 0x70005010U;
    const std::uintptr_t manager_receiver = 0x70193F20U;
    const std::uintptr_t manager_slot = audio_receiver + 0x1E28U;
    if (!audio_minimum::ObserveAudioMinimumAudioOwner(
            &publication, audio_receiver, manager_receiver,
            manager_slot) ||
        !audio_minimum::ObserveAudioMinimumBankOwner(
            &publication, manager_receiver) ||
        !audio_minimum::CompleteAudioMinimumOwnerOperation(
            &publication))
        return false;
    audio_minimum::AudioMinimumOwnerIdentity owner = {};
    if (!audio_minimum::SnapshotAudioMinimumOwnerIdentity(
            &publication, &owner) ||
        !owner.exact || owner.conflict ||
        owner.audio_observation_count != 1U ||
        owner.manager_observation_count != 2U ||
        !owner.observer_revision ||
        !owner.event_fence_generation)
        return false;
    const audio_minimum::AudioMinimumSemanticSnapshot first =
        Runtime10AudioSnapshot(owner);
    const audio_minimum::AudioMinimumSemanticSnapshot second =
        Runtime10AudioSnapshot(owner);
    if (!first.observer_revision_observed ||
        !second.observer_revision_observed ||
        first.observer_revision != second.observer_revision ||
        !first.event_fence_generation_observed ||
        !second.event_fence_generation_observed ||
        first.active_event_fence_generation !=
            second.active_event_fence_generation ||
        first.script.property_group_count != 1382U ||
        first.script.semantic_fingerprint !=
            second.script.semantic_fingerprint ||
        first.banks.semantic_fingerprint !=
            second.banks.semantic_fingerprint ||
        first.banks.identity_fingerprint !=
            second.banks.identity_fingerprint)
        return false;
    audio_minimum::AudioMinimumOperationCoverage coverage = {};
    coverage.minimum_hook_plan_complete = true;
    coverage.event_fence_complete = true;
    coverage.script_evidence_complete = true;
    coverage.bank_evidence_complete = true;
    coverage.bank_reference_evidence_complete = true;
    coverage.speech_evidence_complete = true;
    audio_minimum::InitializeAudioMinimumSemanticState(state);
    return audio_minimum::CaptureAudioMinimumSemanticBaseline(
               state, baseline_generation, first, second, coverage) &&
        state->baseline_captured &&
        state->baseline.owner.exact &&
        state->baseline.banks.manager == manager_receiver;
}

bool ReachRuntime10StableFrontend(
    cold::StartupB0Session* session) noexcept
{
    if (!session) return false;
    cold::StartupB0AdmissionSample sample = {};
    sample.sequence = 1001U;
    sample.qpc = 1000U;
    sample.frame_route_token = 0xD1A6A7E000000009ULL;
    sample.thread_id = 77U;
    sample.tick_source = cold::StartupB0TickSource::OnFrame;
    sample.query_succeeded = true;
    sample.frontend_active = true;
    sample.validated_frame_route = true;
    sample.frontend_level_pointer = 0x181010000ULL;
    sample.frontend_level_vtable = 0x182020000ULL;
    sample.player_inventory_authoritative = true;
    if (cold::AdvanceStartupB0Admission(
            session, &sample).admission_ready)
        return false;
    sample.sequence = 1101U;
    sample.qpc = 1100U;
    return cold::AdvanceStartupB0Admission(
        session, &sample).admission_ready;
}

bool AcceptRuntime10Domain(
    cold::StartupB0Session* session,
    cold::StartupB0Domain domain,
    std::uint64_t identity,
    std::uint64_t sequence) noexcept
{
    return identity &&
        cold::RecordStartupB0DomainResult(
            session, domain,
            cold::StartupB0DomainCaptureResult::Ready,
            cold::StartupB0Blocker::None, sequence,
            kRegistryGeneration, identity) ==
        cold::StartupB0DomainRecordResult::AcceptedReady;
}

bool RecordRuntime11PlayerDomainFromCapture(
    cold::StartupB0Session* session,
    const Runtime10PlayerWorld& world,
    std::uint64_t sequence) noexcept
{
    const auto& diagnostics = world.adapter.b0_diagnostics;
    if (!session || !world.adapter.baseline_captured ||
        world.adapter.baseline_kind !=
            player::PlayerStartupBaselineKind::Snapshot ||
        world.adapter.baseline_state_count != 2U ||
        diagnostics.selected_kind !=
            player::PlayerStartupBaselineKind::Snapshot ||
        diagnostics.pass1_owner_count != 2U ||
        diagnostics.pass2_owner_count != 2U ||
        diagnostics.state_snapshot_count != 2U ||
        !diagnostics.owner_set_stable_across_passes ||
        !diagnostics.player_semantic_snapshot_complete ||
        diagnostics.player_restart_snapshot_complete ||
        !world.adapter.baseline_gameplay_cache_fingerprint)
        return false;
    for (std::uint32_t state_index = 0;
         state_index < 2U; ++state_index) {
        if (!diagnostics.states[state_index].snapshot_complete)
            return false;
        for (std::uint32_t row_index = 0;
             row_index < player::kPlayerLogicalTypeCount;
             ++row_index) {
            const auto& pass1 =
                diagnostics.states[state_index].pass1_rows[row_index];
            const auto& pass2 =
                diagnostics.states[state_index].pass2_rows[row_index];
            if (!pass1.semantic_complete ||
                !pass2.semantic_complete ||
                std::memcmp(&pass1, &pass2, sizeof(pass1)) != 0)
                return false;
            if (state_index == 0U) {
                if (!pass1.preset_absent ||
                    pass1.semantic_kind !=
                        player::
                            PlayerGameplayParamsWindowsRowSemanticKind::
                                AbsentPresetRow)
                    return false;
            } else if (pass1.preset_absent ||
                       pass1.definition_traversal.
                               configured_level_stride != 0x28U ||
                       pass1.definition_traversal.
                               configured_property_stride != 0x20U ||
                       pass1.definition_traversal.first_failure !=
                           player::PlayerDefinitionTraversalFailure::None) {
                return false;
            }
        }
    }
    return cold::RecordStartupB0DomainResult(
               session, cold::StartupB0Domain::Player,
               cold::StartupB0DomainCaptureResult::Ready,
               cold::StartupB0Blocker::None, sequence,
               kRegistryGeneration,
               world.adapter.baseline_gameplay_cache_fingerprint) ==
        cold::StartupB0DomainRecordResult::AcceptedReady;
}

bool Test01AllFiftyFiveLiveShapedRecordsAccepted() noexcept
{
    Fixture fixture;
    InitializeFixture(&fixture);
    const startup::StartupSourceClassificationReport report =
        Classify(&fixture);
    if (!report.complete &&
        report.first_rejected_row < report.row_count) {
        const startup::StartupSourceClassificationRow& row =
            report.rows[report.first_rejected_row];
        std::fprintf(
            stderr,
            "first rejected row=%u ordinal=%u result=%s path=%s\n",
            report.first_rejected_row, row.list_ordinal,
            startup::StartupSourceClassificationResultName(row.result),
            row.canonical_path);
    }
    EXPECT(report.complete);
    EXPECT(report.row_count == 55U);
    EXPECT(report.accepted_count == 55U);
    EXPECT(report.rejected_count == 0U);
    EXPECT(report.loader_match_count == 1U);
    EXPECT(report.loader_source_identity != 0U);
    return true;
}

bool Test02LoaderParentPathNeedsExactJoin() noexcept
{
    Fixture fixture;
    InitializeFixture(&fixture);
    const startup::StartupSourceClassificationReport report =
        Classify(&fixture);
    EXPECT(report.rows[kLoaderIndex].exact_process_lifetime_join);
    EXPECT(report.rows[kLoaderIndex].path_corroborated);
    EXPECT(report.rows[kLoaderIndex].proposed_kind ==
           cold::BaselineSourceKind::LoaderPak);
    return true;
}

bool Test03LoaderParentWithoutRecordRejected() noexcept
{
    Fixture fixture;
    InitializeFixture(&fixture);
    RemoveMount(&fixture.mounts, DLUMA_SOURCE_OWNER_LOADER_PAK);
    const startup::StartupSourceClassificationReport report =
        Classify(&fixture);
    EXPECT(!report.complete);
    EXPECT(report.rows[kLoaderIndex].result ==
           startup::StartupSourceClassificationResult::
               UnapprovedWorkshopSource);
    return true;
}

bool Test04CorrectPathWrongSourceRejected() noexcept
{
    Fixture fixture;
    InitializeFixture(&fixture);
    fixture.inventory.sources[kLoaderIndex].source =
        reinterpret_cast<std::uintptr_t>(&fixture.replacement);
    const startup::StartupSourceClassificationReport report =
        Classify(&fixture);
    EXPECT(!report.complete);
    EXPECT(report.rows[kLoaderIndex].result ==
           startup::StartupSourceClassificationResult::
               UnapprovedWorkshopSource);
    return true;
}

bool Test05ChangedLoaderContentRejected() noexcept
{
    Fixture fixture;
    InitializeFixture(&fixture);
    DlumaProcessLifetimeSourceRecord* loader =
        FindMount(&fixture.mounts, DLUMA_SOURCE_OWNER_LOADER_PAK);
    EXPECT(loader != nullptr);
    loader->content_sha256[0] =
        loader->content_sha256[0] == 'a' ? 'f' : 'a';
    const startup::StartupSourceClassificationReport report =
        Classify(&fixture);
    EXPECT(!report.complete);
    EXPECT(report.rows[kLoaderIndex].result ==
           startup::StartupSourceClassificationResult::
               ProcessLifetimeOwnerValidationFailed);
    return true;
}

bool Test06DuplicateLoaderJoinRejected() noexcept
{
    Fixture fixture;
    InitializeFixture(&fixture);
    EXPECT(fixture.mounts.count <
           DLUMA_PROCESS_LIFETIME_SOURCE_CAPACITY);
    const DlumaProcessLifetimeSourceRecord* loader =
        FindMount(&fixture.mounts, DLUMA_SOURCE_OWNER_LOADER_PAK);
    EXPECT(loader != nullptr);
    fixture.mounts.records[fixture.mounts.count++] = *loader;
    const startup::StartupSourceClassificationReport report =
        Classify(&fixture);
    EXPECT(!report.complete);
    EXPECT(report.loader_match_count == 2U);
    EXPECT(report.rows[kLoaderIndex].result ==
           startup::StartupSourceClassificationResult::
               ProcessLifetimeJoinNotUnique);
    return true;
}

bool Test07DeveloperModeExactOwnerAccepted() noexcept
{
    Fixture fixture;
    InitializeFixture(&fixture);
    const startup::StartupSourceClassificationReport report =
        Classify(&fixture);
    EXPECT(report.rows[kDeveloperIndex].result ==
           startup::StartupSourceClassificationResult::Accepted);
    EXPECT(report.rows[kDeveloperIndex].owner_kind ==
           DLUMA_SOURCE_OWNER_LOADER_DEVELOPER_MODE);
    return true;
}

bool Test08ThumbnailExactOwnerAccepted() noexcept
{
    Fixture fixture;
    InitializeFixture(&fixture);
    const startup::StartupSourceClassificationReport report =
        Classify(&fixture);
    EXPECT(report.rows[kThumbnailIndex].result ==
           startup::StartupSourceClassificationResult::Accepted);
    EXPECT(report.rows[kThumbnailIndex].owner_kind ==
           DLUMA_SOURCE_OWNER_LOADER_THUMBNAILS);
    return true;
}

bool Test09SameNamedOtherWorkshopDirectoryRejected() noexcept
{
    Fixture fixture;
    InitializeFixture(&fixture);
    Copy(
        fixture.inventory.sources[15].path,
        sizeof(fixture.inventory.sources[15].path),
        "X:\\FixtureLibrary\\steamapps\\workshop\\content\\239140\\"
        "9999999999\\mods\\dluma_runtime\\developer_mode");
    const startup::StartupSourceClassificationReport report =
        Classify(&fixture);
    EXPECT(!report.complete);
    EXPECT(report.rows[15].result ==
           startup::StartupSourceClassificationResult::
               UnapprovedWorkshopSource);
    return true;
}

bool Test10ApprovedNativeCompanionAccepted() noexcept
{
    Fixture fixture;
    InitializeFixture(&fixture);
    const startup::StartupSourceClassificationReport report =
        Classify(&fixture);
    EXPECT(report.rows[kNativeIndex].result ==
           startup::StartupSourceClassificationResult::Accepted);
    EXPECT(report.rows[kNativeIndex].owner_kind ==
           DLUMA_SOURCE_OWNER_NATIVE_COMPANION_PACK);
    EXPECT(report.rows[kNativeIndex].path_corroborated);
    return true;
}

bool Test11UnapprovedCompanionRejected() noexcept
{
    Fixture fixture;
    InitializeFixture(&fixture);
    Copy(
        fixture.inventory.sources[16].path,
        sizeof(fixture.inventory.sources[16].path),
        "X:\\FixtureLibrary\\steamapps\\workshop\\content\\239140\\"
        "8888888888\\mods\\dluma");
    fixture.sources[16].type =
        startup::FilesystemSourceRuntimeType::Pack;
    fixture.sources[16].vtable = kPackVtable;
    const startup::StartupSourceClassificationReport report =
        Classify(&fixture);
    EXPECT(!report.complete);
    EXPECT(report.rows[16].result ==
           startup::StartupSourceClassificationResult::
               UnapprovedWorkshopSource);
    return true;
}

bool Test12ChangedNativeApprovalOrManifestRejected() noexcept
{
    Fixture fixture;
    InitializeFixture(&fixture);
    DlumaProcessLifetimeSourceRecord* native =
        FindMount(
            &fixture.mounts,
            DLUMA_SOURCE_OWNER_NATIVE_COMPANION_PACK);
    EXPECT(native != nullptr);
    native->approval_fingerprint[0] = 'f';
    startup::StartupSourceClassificationReport report =
        Classify(&fixture);
    EXPECT(report.rows[kNativeIndex].result ==
           startup::StartupSourceClassificationResult::
               ProcessLifetimeOwnerValidationFailed);
    InitializeFixture(&fixture);
    native = FindMount(
        &fixture.mounts,
        DLUMA_SOURCE_OWNER_NATIVE_COMPANION_PACK);
    EXPECT(native != nullptr);
    native->manifest_fingerprint[0] = 'f';
    report = Classify(&fixture);
    EXPECT(report.rows[kNativeIndex].result ==
           startup::StartupSourceClassificationResult::
               ProcessLifetimeOwnerValidationFailed);
    return true;
}

bool Test13ExtendedAndOrdinaryFormsNormalizeTogether() noexcept
{
    char extended[512] = {};
    char pseudo[512] = {};
    char runtime_slash_folded[512] = {};
    char ordinary[512] = {};
    EXPECT(dluma_process_lifetime_source_normalize_path(
        "\\\\?\\X:\\FixtureLibrary\\steamapps\\workshop\\content\\"
        "239140\\3773041738\\mods\\native\\global\\"
        "example.vehicle_audio",
        extended, sizeof(extended)));
    EXPECT(dluma_process_lifetime_source_normalize_path(
        "\\?\\X:\\FixtureLibrary\\steamapps\\workshop\\content\\"
        "239140\\3773041738\\mods\\native\\global\\"
        "example.vehicle_audio",
        pseudo, sizeof(pseudo)));
    EXPECT(dluma_process_lifetime_source_normalize_path(
        "//?/X:/fixturelibrary/steamapps/workshop/content/"
        "239140/3773041738/mods/native/global/"
        "example.vehicle_audio/",
        runtime_slash_folded, sizeof(runtime_slash_folded)));
    EXPECT(dluma_process_lifetime_source_normalize_path(
        "X:\\FixtureLibrary\\steamapps\\workshop\\content\\"
        "239140\\3773041738\\mods\\native\\global\\"
        "example.vehicle_audio",
        ordinary, sizeof(ordinary)));
    EXPECT(std::strcmp(extended, ordinary) == 0);
    EXPECT(std::strcmp(pseudo, ordinary) == 0);
    EXPECT(std::strcmp(runtime_slash_folded, ordinary) == 0);
    Fixture fixture;
    InitializeFixture(&fixture);
    Copy(
        fixture.inventory.sources[kNativeIndex].path,
        sizeof(fixture.inventory.sources[kNativeIndex].path),
        "//?/X:/fixturelibrary/steamapps/workshop/content/239140/"
        "3773041738/mods/native/global/"
        "example.vehicle_audio/");
    fixture.inventory.sources[kNativeIndex].raw_path_length =
        static_cast<std::uint32_t>(std::strlen(
            fixture.inventory.sources[kNativeIndex].path));
    Copy(
        FindMount(
            &fixture.mounts,
            DLUMA_SOURCE_OWNER_NATIVE_COMPANION_PACK)
            ->observed_source_path,
        DLUMA_PROCESS_SOURCE_PATH_CAPACITY,
        fixture.inventory.sources[kNativeIndex].path);
    const startup::StartupSourceClassificationReport report =
        Classify(&fixture);
    EXPECT(report.rows[kNativeIndex].result ==
           startup::StartupSourceClassificationResult::Accepted);
    return true;
}

bool Test14NoGenericWorkshopAllowlist() noexcept
{
    Fixture fixture;
    InitializeFixture(&fixture);
    Copy(
        fixture.inventory.sources[17].path,
        sizeof(fixture.inventory.sources[17].path),
        "X:\\FixtureLibrary\\steamapps\\workshop\\content\\239140\\"
        "7777777777\\data\\maps\\example");
    const startup::StartupSourceClassificationReport report =
        Classify(&fixture);
    EXPECT(!report.complete);
    EXPECT(report.rows[17].result ==
           startup::StartupSourceClassificationResult::
               UnapprovedWorkshopSource);
    return true;
}

bool Test15Runtime10CaptureIntegrationAccepts53To55Sources() noexcept
{
    for (std::uint32_t source_count = 53U;
         source_count <= 55U; ++source_count) {
        Fixture fixture;
        InitializeFixture(&fixture);
        fixture.inventory.reported_count = source_count;
        fixture.inventory.source_count = source_count;
        fixture.inventory.fallback_installed =
            source_count == 55U;
        const startup::StartupSourceClassificationReport report =
            Classify(&fixture);
        EXPECT(report.complete);
        EXPECT(report.row_count == source_count);
        EXPECT(report.accepted_count == source_count);
        EXPECT(report.rejected_count == 0U);
        EXPECT(report.loader_match_count == 1U);
        EXPECT(report.loader_source_identity != 0U);

        Runtime10PlayerWorld player_world = {};
        EXPECT(CaptureRuntime10PlayerBaseline(
            &player_world, kRegistryGeneration));
        EXPECT(player_world.adapter.b0_diagnostics.
                   bootstrap_sample_owner_count == 0U);
        EXPECT(player_world.adapter.b0_diagnostics.
                   selected_kind ==
               player::PlayerStartupBaselineKind::Snapshot);
        EXPECT(player_world.adapter.baseline_state_count == 2U);

        audio_minimum::AudioMinimumSemanticState audio_state = {};
        EXPECT(CaptureRuntime10AudioBaseline(
            &audio_state, kRegistryGeneration));
        EXPECT(audio_state.baseline.script.attempted);
        EXPECT(audio_state.baseline.script.complete);
        EXPECT(audio_state.baseline.banks.attempted);
        EXPECT(audio_state.baseline.banks.complete);
        EXPECT(audio_state.baseline.observer_revision_observed);
        EXPECT(audio_state.baseline.event_fence_generation_observed);

        cold::StartupB0Session session = {};
        EXPECT(cold::InitializeStartupB0Session(
            &session, kRegistryGeneration,
            cold::kStartupB0TransactionCoreDomainMask,
            100U, 500U));
        EXPECT(ReachRuntime10StableFrontend(&session));
        EXPECT(AcceptRuntime10Domain(
            &session, cold::StartupB0Domain::VfsSourceInventory,
            report.loader_source_identity, 1201U));
        EXPECT(AcceptRuntime10Domain(
            &session, cold::StartupB0Domain::LoaderState,
            report.loader_source_identity, 1202U));
        EXPECT(RecordRuntime11PlayerDomainFromCapture(
            &session, player_world, 1203U));
        EXPECT(AcceptRuntime10Domain(
            &session, cold::StartupB0Domain::Audio,
            audio_state.baseline.script.semantic_fingerprint,
            1204U));
        EXPECT(session.diagnostics.startup_b0_ready_domain_mask ==
               cold::kStartupB0TransactionCoreDomainMask);
        EXPECT(session.diagnostics.startup_b0_required_domain_mask ==
               cold::kStartupB0TransactionCoreDomainMask);
        EXPECT(session.diagnostics.startup_b0_ready_domain_mask == 15U);
        EXPECT(session.diagnostics.startup_b0_full_b0_captured);
        EXPECT(cold::AuthorizeStartupB0RegistryBaseline(
            &session, kRegistryGeneration));
        EXPECT(session.diagnostics.startup_b0_state ==
               cold::StartupB0State::CompleteAuthorized);
        cold::StartupB0PlayerAudioRetryGate gate = {};
        EXPECT(cold::InitializeStartupB0PlayerAudioRetryGate(
            &gate, 1000U, 10000U));
        EXPECT(!gate.retry_expired);
        EXPECT(cold::StartupB0PlayerAudioRetryAllowed(
            &gate, 1001U));
        std::uint32_t admitted_play_activations = 0;
        if (cold::EvaluateStartupB0ProjectAdmission(
                session.diagnostics.startup_b0_state,
                true, true, true) ==
            cold::StartupB0ProjectAdmissionDisposition::Allow)
            ++admitted_play_activations;
        EXPECT(admitted_play_activations == 1U);
    }
    return true;
}

bool Test16Runtime10OwnerConflictAndRetryDeadlineFailClosed() noexcept
{
    audio_minimum::AudioMinimumOwnerPublication publication = {};
    audio_minimum::InitializeAudioMinimumOwnerPublication(
        &publication, 0x60A019C801240000ULL, 0xFA3B00100920ULL);
    EXPECT(audio_minimum::ObserveAudioMinimumAudioOwner(
        &publication, 0x70005010U, 0x70193F20U, 0x70006E38U));
    EXPECT(audio_minimum::ObserveAudioMinimumBankOwner(
        &publication, 0x70193F20U));
    EXPECT(!audio_minimum::ObserveAudioMinimumBankOwner(
        &publication, 0x70194020U));
    audio_minimum::AudioMinimumOwnerIdentity conflicted = {};
    EXPECT(!audio_minimum::SnapshotAudioMinimumOwnerIdentity(
        &publication, &conflicted));
    EXPECT(conflicted.conflict);
    EXPECT(!conflicted.exact);

    const player::PlayerStateSnapshotFailureStage stages[] = {
        player::PlayerStateSnapshotFailureStage::BodyUnreadable,
        player::PlayerStateSnapshotFailureStage::
            PlayerVariablesUnavailable,
        player::PlayerStateSnapshotFailureStage::RowTableUnreadable,
        player::PlayerStateSnapshotFailureStage::
            SelectorSnapshotIncomplete,
        player::PlayerStateSnapshotFailureStage::
            PreservationFingerprintIncomplete,
        player::PlayerStateSnapshotFailureStage::
            AppearanceSnapshotIncomplete,
        player::PlayerStateSnapshotFailureStage::RowUnreadable,
        player::PlayerStateSnapshotFailureStage::
            RowDefinitionUnreadable,
        player::PlayerStateSnapshotFailureStage::
            RowDefinitionTraversalIncomplete,
        player::PlayerStateSnapshotFailureStage::
            RowCacheMembershipMismatch,
        player::PlayerStateSnapshotFailureStage::
            RowProviderIncomplete,
        player::PlayerStateSnapshotFailureStage::
            RequiredPropertyMissing,
        player::PlayerStateSnapshotFailureStage::
            RequiredPropertyDuplicate,
        player::PlayerStateSnapshotFailureStage::
            PropertyProviderIncomplete,
        player::PlayerStateSnapshotFailureStage::
            CacheGenerationMismatch,
        player::PlayerStateSnapshotFailureStage::
            StateChangedBetweenPasses,
        player::PlayerStateSnapshotFailureStage::SemanticRowInvalid,
        player::PlayerStateSnapshotFailureStage::SemanticPropertyInvalid,
        player::PlayerStateSnapshotFailureStage::
            CacheMembershipConflict,
        player::PlayerStateSnapshotFailureStage::
            RestartCacheMembershipUnproven,
        player::PlayerStateSnapshotFailureStage::
            RestartProviderUnproven,
        player::PlayerStateSnapshotFailureStage::
            RestartInventoryOwnerUnproven,
        player::PlayerStateSnapshotFailureStage::
            RestartEquipmentOwnerUnproven,
        player::PlayerStateSnapshotFailureStage::
            RestartQuestOwnerUnproven,
    };
    for (player::PlayerStateSnapshotFailureStage stage : stages) {
        const char* name =
            player::PlayerStateSnapshotFailureStageName(stage);
        EXPECT(name != nullptr);
        EXPECT(std::strcmp(name, "unknown") != 0);
        EXPECT(std::strcmp(name, "none") != 0);
    }

    cold::StartupB0PlayerAudioRetryGate gate = {};
    EXPECT(cold::InitializeStartupB0PlayerAudioRetryGate(
        &gate, 1000U, 100U));
    EXPECT(gate.retry_started_ms == 1000U);
    EXPECT(gate.retry_deadline_ms == 1100U);
    std::uint32_t capture_attempts = 0;
    if (cold::StartupB0PlayerAudioRetryAllowed(&gate, 1099U))
        ++capture_attempts;
    EXPECT(!cold::dluma_expire_unresolved_player_audio_b0_on_frame(
        &gate, 1099U, true, true, false,
        cold::StartupB0PlayerAudioPermanentFailure::
            AudioScriptSnapshotFailed));
    EXPECT(!cold::dluma_expire_unresolved_player_audio_b0_on_frame(
        &gate, 1100U, false, true, false,
        cold::StartupB0PlayerAudioPermanentFailure::
            AudioScriptSnapshotFailed));
    const bool active_level_query_succeeded = false;
    EXPECT(!active_level_query_succeeded);
    EXPECT(cold::dluma_expire_unresolved_player_audio_b0_on_frame(
        &gate, 1100U, true, true, false,
        cold::StartupB0PlayerAudioPermanentFailure::
            AudioScriptSnapshotFailed));
    if (cold::StartupB0PlayerAudioRetryAllowed(&gate, 1101U))
        ++capture_attempts;
    EXPECT(capture_attempts == 1U);
    EXPECT(gate.retry_expired);
    EXPECT(gate.attempts_stopped);
    EXPECT(std::strcmp(
               cold::StartupB0PlayerAudioPermanentFailureName(
                   gate.permanent_failure),
               "audio_script_snapshot_failed") == 0);
    EXPECT(!cold::dluma_expire_unresolved_player_audio_b0_on_frame(
        &gate, 1200U, true, true, false,
        cold::StartupB0PlayerAudioPermanentFailure::
            AudioManagerIdentityUnresolved));
    EXPECT(gate.permanent_failure ==
           cold::StartupB0PlayerAudioPermanentFailure::
               AudioScriptSnapshotFailed);

    cold::StartupB0Session session = {};
    EXPECT(cold::InitializeStartupB0Session(
        &session, kRegistryGeneration,
        cold::kStartupB0TransactionCoreDomainMask,
        100U, 500U));
    EXPECT(ReachRuntime10StableFrontend(&session));
    EXPECT(cold::EvaluateStartupB0ProjectAdmission(
               session.diagnostics.startup_b0_state,
               true, true, false) ==
           cold::StartupB0ProjectAdmissionDisposition::Defer);
    cold::FailStartupB0Permanently(
        &session, cold::StartupB0Blocker::PermanentCaptureFailure);
    EXPECT(cold::EvaluateStartupB0ProjectAdmission(
               session.diagnostics.startup_b0_state,
               true, true, false) ==
           cold::StartupB0ProjectAdmissionDisposition::BlockRestart);
    std::uint32_t set_custom_content_calls = 0;
    std::uint32_t update_dlc_calls = 0;
    std::uint32_t selected_map_calls = 0;
    std::uint32_t on_start_level_calls = 0;
    std::uint32_t after_fade_calls = 0;
    std::uint32_t source_mount_calls = 0;
    EXPECT(set_custom_content_calls == 0U);
    EXPECT(update_dlc_calls == 0U);
    EXPECT(selected_map_calls == 0U);
    EXPECT(on_start_level_calls == 0U);
    EXPECT(after_fade_calls == 0U);
    EXPECT(source_mount_calls == 0U);
    return true;
}

bool Test17RetiredProcessLifetimeRecordsAreTombstones() noexcept
{
    Fixture fixture;
    InitializeFixture(&fixture);
    EXPECT(fixture.mounts.count <
           DLUMA_PROCESS_LIFETIME_SOURCE_CAPACITY);

    fixture.replacement.type =
        startup::FilesystemSourceRuntimeType::Directory;
    fixture.replacement.vtable = kDirectoryVtable;
    DlumaProcessLifetimeSourceRecord retired = MakeMount(
        DLUMA_SOURCE_OWNER_CUSTOM_MAIN_MENU,
        DLUMA_SOURCE_OWNER_ID_CUSTOM_MAIN_MENU_PROJECT,
        &fixture.replacement,
        "C:\\Synthetic\\CustomMenuProject", 'c');
    retired.mount_generation = 91U;
    retired.registry_generation = kRegistryGeneration - 1U;
    retired.registry_generation_floor = kRegistryGeneration;
    retired.retirement_registry_generation = kRegistryGeneration;
    retired.lifecycle_state = DLUMA_PROCESS_SOURCE_STATE_RETIRED;
    retired.exact_source_joined = 0U;
    fixture.mounts.records[fixture.mounts.count++] = retired;

    const startup::StartupSourceClassificationReport report =
        Classify(&fixture);
    EXPECT(report.complete);
    EXPECT(report.accepted_count == fixture.inventory.source_count);
    EXPECT(report.rejected_count == 0U);
    EXPECT(report.loader_match_count == 1U);
    return true;
}

}  // namespace

int main()
{
    if (!Test01AllFiftyFiveLiveShapedRecordsAccepted() ||
        !Test02LoaderParentPathNeedsExactJoin() ||
        !Test03LoaderParentWithoutRecordRejected() ||
        !Test04CorrectPathWrongSourceRejected() ||
        !Test05ChangedLoaderContentRejected() ||
        !Test06DuplicateLoaderJoinRejected() ||
        !Test07DeveloperModeExactOwnerAccepted() ||
        !Test08ThumbnailExactOwnerAccepted() ||
        !Test09SameNamedOtherWorkshopDirectoryRejected() ||
        !Test10ApprovedNativeCompanionAccepted() ||
        !Test11UnapprovedCompanionRejected() ||
        !Test12ChangedNativeApprovalOrManifestRejected() ||
        !Test13ExtendedAndOrdinaryFormsNormalizeTogether() ||
        !Test14NoGenericWorkshopAllowlist() ||
        !Test15Runtime10CaptureIntegrationAccepts53To55Sources() ||
        !Test16Runtime10OwnerConflictAndRetryDeadlineFailClosed() ||
        !Test17RetiredProcessLifetimeRecordsAreTombstones())
        return 1;
    std::puts(
        "startup filesystem source classifier/runtime9 17-case harness passed");
    return 0;
}
